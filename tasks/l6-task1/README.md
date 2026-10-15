
# L6 Task 1 - Explain and Reduce a Scheduling Delay

## Goal

Find out why a Zephyr thread is late, prove it with runtime evidence,
fix it with one change, and repeat the measurement.

## The system

| Part | Behaviour |
|---|---|
| Timer ISR | Every 250 ms (first at 500 ms) queues an event for `control` and gives a semaphore to `maintenance`. Both threads become ready at the same instant. |
| `control` | Receives the event from a `k_msgq`. Starter priority 7. |
| `maintenance` | Takes the semaphore, then `k_busy_wait(45000)`: 45 ms of CPU, no blocking. Priority 4. |

In Zephyr a lower number is a higher priority.

## Response-time guarantee

Control work must **start within 10 ms** of the event becoming ready
(the timer interrupt that queued it).

Detection, in `control_fn`: latency = now - event.ready_ms. Every miss
is counted, warnings are rate-limited to one per second, each event logs
`event_done seq=... latency_ms=... deadline=ok|MISS`, and a summary is
printed every 20 events.

## Findings

### Baseline (control 7, maintenance 4)

- Application log: `deadline_misses` equals `events` at every summary
  (for example 1800 of 1800 after a 7.5 minute run).
- Kernel shell snapshot (`logs/baseline-shell.txt`): control priority 7,
  maintenance priority 4. A snapshot shows who outranks whom, not how
  long a thread waited.
- CTF trace (`logs/baseline-trace.txt`), first sensor event:

500.000 ms named_event: name="event_ready" arg0=0 arg1=500
500.000 ms thread_sched_ready: name="control"
500.000 ms thread_sched_ready: name="maintenance"
500.000 ms isr_exit
500.000 ms thread_switched_out: name="idle"
500.000 ms thread_switched_in: name="maintenance"
500.000 ms thread_busy_wait_enter: usec_to_wait=45000


  The decoder reports control starting at 545.000 ms: a delay of exactly
  45.000 ms, with only `maintenance` running in between. All 22 events
  in the 6 second capture missed the deadline.
- The application log said 40 ms, not 45, because `k_uptime_get_32()`
  advances in 10 ms kernel ticks on native_sim. The trace timestamps
  give the real figure.

### Root cause

Both threads become ready when the timer interrupt returns. The
scheduler picks the highest priority, and `maintenance` (4) outranks
`control` (7). `maintenance` busy-waits for 45 ms without blocking, so
`control` cannot run until it finishes. The timer, the queue and the
code in `control` are fine. The priority assignment is wrong: background
work with no deadline outranks the thread that has one.

### Correction (one change)

`CONTROL_PRIORITY` 7 -> 3, selected with `CONFIG_APP_FIX_PRIORITY=y`.
Nothing else changed, so `control` (3) now outranks `maintenance` (4).

### Result

| | Baseline | Fixed |
|---|---|---|
| control priority | 7 | 3 |
| delay before control starts (trace) | 45.000 ms | 0.000 ms |
| events over 10 ms (6 s capture) | 22 of 22 | 0 of 22 |
| ran in between | maintenance, 45 ms | nothing |
| application log, first 20 events | deadline_misses = events | deadline_misses=0, max_latency_ms=0 |

## Caveats

- Measured on `native_sim`, where time is simulated, so the numbers are
  idealised (0.000 ms has no interrupt or context-switch overhead). The
  scheduling-order conclusion carries over to hardware, but the figures
  should be re-measured on the real target.
- Logging and tracing are themselves overhead on a real system.
- Babeltrace 2.1.2 aborted on this CTF stream (see
  `logs/babeltrace2-error.txt`), so the capture was decoded with the
  included `decode_ctf.py`, which reads the same `metadata` file. It
  calibrates the timestamp unit from the known 250 ms event period
  (1 raw tick = 1 ns here).
- `maintenance` still uses 45 ms of every 250 ms (18% of the CPU). It now
  runs after `control` instead of before it, which is fine because it has
  no short deadline.

## Reproduce

```bash
# baseline
west build -p always -b native_sim -d build_l6-baseline tasks/l6-task1
# fixed (one correction)
west build -p always -b native_sim -d build_l6-fixed tasks/l6-task1 -- -DCONFIG_APP_FIX_PRIORITY=y

# capture 6 s of CTF, then decode
mkdir -p ctf/fixed && cp zephyr/subsys/tracing/ctf/tsdl/metadata ctf/fixed/
timeout -s INT 6 ./build_l6-fixed/zephyr/zephyr.exe -trace-file=ctf/fixed/channel0_0
python3 tasks/l6-task1/decode_ctf.py ctf/fixed
```

## Files

l6-task1/
|-- CMakeLists.txt, Kconfig, prj.conf
|-- src/main.c
|-- decode_ctf.py
|-- ctf/baseline, ctf/fixed raw CTF captures + metadata
-- logs/ |-- baseline-run.txt, baseline-shell.txt |-- baseline-trace.txt, baseline-trace-summary.txt |-- fixed-run.txt, fixed-trace.txt, fixed-trace-summary.txt -- babeltrace2-error.txt
