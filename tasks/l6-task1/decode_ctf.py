#!/usr/bin/env python3
"""
decode_ctf.py - minimal, metadata-driven decoder for Zephyr CTF traces.

Why this exists: Babeltrace 2.1.2 aborts on Zephyr's CTF stream (a library
precondition fails for packets without timestamps).  This script reads the
same `metadata` file, decodes the packed event records itself, and prints

  1. every decoded event (--events FILE), and
  2. a response-time breakdown for each sensor event: when the event became
     ready, when the control thread really started, and which threads ran
     in between.

Usage:  decode_ctf.py TRACE_DIR [--events FILE] [--summary FILE]
        TRACE_DIR must contain `metadata` and `channel0_0`.
"""
import argparse
import re
import statistics
import sys
from pathlib import Path


# ----------------------------------------------------------------------------
# Metadata (TSDL) parsing - only what Zephyr's metadata file needs
# ----------------------------------------------------------------------------

def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def blocks(text, keyword):
    """Yield the body of every `keyword { ... }` block (nested braces ok)."""
    for m in re.finditer(r'\b%s\s*\{' % keyword, text):
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += (text[i] == '{') - (text[i] == '}')
            i += 1
        yield text[m.end():i - 1]


def parse_types(text):
    types = {}
    for m in re.finditer(r'typealias\s+integer\s*\{(.*?)\}\s*:=\s*(\w+)\s*;',
                         text, re.S):
        body, name = m.groups()
        size = re.search(r'size\s*=\s*(\d+)', body)
        if not size:
            continue
        signed = re.search(r'signed\s*=\s*(true|1)', body) is not None
        types[name] = (int(size.group(1)), signed)
    return types


FIELD_RE = re.compile(r'(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;')


def parse_fields(body):
    return [(t, n, int(c) if c else None) for t, n, c in FIELD_RE.findall(body)]


def load_metadata(path):
    text = strip_comments(Path(path).read_text(errors='replace'))
    types = parse_types(text)
    byte_order = '>' if re.search(r'byte_order\s*=\s*be', text) else '<'

    hdr = re.search(r'event\.header\s*:=\s*struct\s+(\w+)\s*;', text)
    if hdr:
        body = re.search(r'struct\s+%s\s*\{(.*?)\}' % hdr.group(1), text, re.S)
    else:
        body = re.search(r'event\.header\s*:=\s*struct\s*\{(.*?)\}', text, re.S)
    if not body:
        sys.exit('metadata: could not find the event header definition')
    header_fields = parse_fields(body.group(1))

    events = {}
    for blk in blocks(text, 'event'):
        nm = re.search(r'\bname\s*=\s*(\w+)\s*;', blk)
        ide = re.search(r'\bid\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;', blk)
        if not nm or not ide:
            continue
        fm = re.search(r'fields\s*:=\s*struct\s*\{(.*?)\}', blk, re.S)
        events[int(ide.group(1), 0)] = (
            nm.group(1), parse_fields(fm.group(1)) if fm else [])
    return types, byte_order, header_fields, events


# ----------------------------------------------------------------------------
# Binary decoding
# ----------------------------------------------------------------------------

def field_bytes(ftype, count, types):
    if ftype not in types:
        sys.exit("metadata: unsupported field type '%s'" % ftype)
    return types[ftype][0] // 8 * (count or 1)


def read_field(buf, off, ftype, count, types, order):
    bits, signed = types[ftype]
    nbytes = bits // 8
    if count is not None and bits == 8:            # fixed-size char array
        raw = buf[off:off + count]
        return raw.split(b'\0', 1)[0].decode('ascii', 'replace'), off + count
    vals = []
    for _ in range(count or 1):
        vals.append(int.from_bytes(buf[off:off + nbytes],
                                   'little' if order == '<' else 'big',
                                   signed=signed))
        off += nbytes
    return (vals if count is not None else vals[0]), off


def decode_stream(data, types, order, header_fields, events):
    hdr_size = sum(field_bytes(t, c, types) for t, _, c in header_fields)
    out, off = [], 0
    while off + hdr_size <= len(data):
        hdr, o = {}, off
        for t, n, c in header_fields:
            hdr[n], o = read_field(data, o, t, c, types, order)
        eid = hdr.get('id')
        if eid not in events:
            raise SystemExit(
                'unknown event id %r at byte offset %d\nnext bytes: %s\n'
                'decoded %d events before this point'
                % (eid, off, data[off:off + 24].hex(' '), len(out)))
        name, fields = events[eid]
        need = sum(field_bytes(t, c, types) for t, _, c in fields)
        if o + need > len(data):
            break                                   # truncated last event
        vals = {}
        for t, n, c in fields:
            vals[n], o = read_field(data, o, t, c, types, order)
        out.append((hdr['timestamp'], name, vals))
        off = o
    return out, off


def unwrap(raw_ts, bits):
    mod, base, prev, out = 1 << bits, 0, None, []
    for r in raw_ts:
        if prev is not None and r < prev:
            base += mod
        prev = r
        out.append(r + base)
    return out


# ----------------------------------------------------------------------------
# Analysis
# ----------------------------------------------------------------------------

def is_named(name, vals, label):
    return name == 'named_event' and vals.get('name') == label


def calibrate(evs, period_ms):
    """Raw timestamp units -> microseconds, using the known event period."""
    ready = [(t, v['arg0']) for t, n, v in evs if is_named(n, v, 'event_ready')]
    deltas = [b[0] - a[0] for a, b in zip(ready, ready[1:]) if b[1] - a[1] == 1]
    if not deltas:
        return None
    return period_ms * 1000.0 / statistics.median(deltas)


def breakdown(evs):
    """For each event_ready: time until control starts + who ran meanwhile."""
    rows = []
    for i, (t0, n0, v0) in enumerate(evs):
        if not is_named(n0, v0, 'event_ready'):
            continue
        seq, cur, ran, t_ctl, app_lat = v0['arg0'], None, [], None, None
        for t, n, w in evs[i + 1:]:
            if n == 'thread_switched_in':
                if w.get('name') == 'control':
                    t_ctl = t
                    break
                cur = (w.get('name'), t)
            elif n == 'thread_switched_out' and cur and w.get('name') == cur[0]:
                ran.append((cur[0], t - cur[1]))
                cur = None
        if t_ctl is not None:
            for t, n, w in evs[i + 1:]:
                if is_named(n, w, 'event_done') and w['arg0'] == seq:
                    app_lat = w['arg1']
                    break
        rows.append((seq, t0, t_ctl, ran, app_lat))
    return rows


def fmt_val(key, val):
    if isinstance(val, str):
        return '%s="%s"' % (key, val)
    if key == 'thread_id':
        return '%s=0x%x' % (key, val)
    return '%s=%s' % (key, val)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument('trace_dir')
    ap.add_argument('--events', help='write every decoded event to this file')
    ap.add_argument('--summary', help='also save the summary to this file')
    ap.add_argument('--period-ms', type=float, default=250.0,
                    help='event period used to calibrate timestamps (250)')
    ap.add_argument('--unit-us', type=float,
                    help='microseconds per raw timestamp unit (skip calibration)')
    ap.add_argument('--deadline-ms', type=float, default=10.0)
    ap.add_argument('--rows', type=int, default=8,
                    help='how many per-event rows to print (8)')
    a = ap.parse_args()

    d = Path(a.trace_dir)
    types, order, hdr_fields, events = load_metadata(d / 'metadata')
    data = (d / 'channel0_0').read_bytes()
    decoded, used = decode_stream(data, types, order, hdr_fields, events)
    if not decoded:
        sys.exit('no events decoded')

    ts_type = next(t for t, n, c in hdr_fields if n == 'timestamp')
    raw = unwrap([e[0] for e in decoded], types[ts_type][0])

    unit_us, how = a.unit_us, 'given on the command line'
    if unit_us is None:
        unit_us = calibrate([(r, e[1], e[2]) for r, e in zip(raw, decoded)],
                            a.period_ms)
        how = 'calibrated from the %g ms event_ready period' % a.period_ms
        if unit_us is None:
            unit_us, how = 1.0, 'ASSUMED (could not calibrate)'

    t0 = raw[0]
    evs = [((r - t0) * unit_us / 1000.0, e[1], e[2])
           for r, e in zip(raw, decoded)]

    if a.events:
        with open(a.events, 'w') as f:
            prev = 0.0
            for t, n, v in evs:
                f.write(('%12.3f ms  +%9.3f  %s: %s' % (
                    t, t - prev, n,
                    ' '.join(fmt_val(k, x) for k, x in v.items()))).rstrip(': ')
                    + '\n')
                prev = t

    lines = []
    lines.append('decoded %d events, %d of %d bytes'
                 % (len(decoded), used, len(data)))
    lines.append('timestamp unit: 1 raw tick = %.4f us (%s)' % (unit_us, how))
    lines.append('')
    rows = breakdown(evs)
    delays = []
    for seq, t_ready, t_ctl, ran, app_lat in rows:
        if t_ctl is None:
            continue
        delay = t_ctl - t_ready
        delays.append(delay)
        if len(delays) <= a.rows:
            between = ', '.join('%s %.3f ms' % (n, ms) for n, ms in ran) or 'nothing'
            lines.append('seq=%-3d ready@%9.3f ms  control started@%9.3f ms  '
                         'delay=%7.3f ms  ran in between: %s  (app log: %s ms)'
                         % (seq, t_ready, t_ctl, delay, between, app_lat))
    if delays:
        miss = sum(1 for x in delays if x > a.deadline_ms)
        lines.append('')
        lines.append('SUMMARY events=%d  median_delay=%.3f ms  max_delay=%.3f ms  '
                     'over_%gms=%d'
                     % (len(delays), statistics.median(delays), max(delays),
                        a.deadline_ms, miss))
    else:
        lines.append('no event_ready -> control start sequences found')
    text = '\n'.join(lines)
    print(text)
    if a.summary:
        Path(a.summary).write_text(text + '\n')


if __name__ == '__main__':
    main()
