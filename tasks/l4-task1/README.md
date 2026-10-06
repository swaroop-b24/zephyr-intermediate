
# L4 Task 1 — Event-Driven Sensor Pipeline with zbus

## Goal

Build a small event-driven system using Zephyr's **zbus** message bus:
one channel carrying simulated sensor data, with two different kinds
of consumers reacting to it in two different ways.

## Design

| Component | Type | Behavior |
|---|---|---|
| `publisher_thread` | Zephyr thread | Simulates a sensor. Publishes a new `sensor_msg` on `sensor_chan` every 100ms, 10 samples total. |
| `sensor_chan` | zbus channel | Carries `struct sensor_msg { uint32_t value; uint32_t timestamp; }`. Has two observers attached: a listener and a subscriber. |
| `display_listener` | zbus **listener** | A plain callback. Runs **synchronously, inline, in the publisher's own thread context** the instant `zbus_chan_pub()` is called — no extra thread, no latency. Represents a "must react immediately" consumer, like a display update. |
| `logger_subscriber` | zbus **subscriber** | Has its **own dedicated thread** that blocks on `zbus_sub_wait()`. Adds a deliberate 40ms delay per message to simulate slower work (e.g. writing to flash). Represents a consumer that's decoupled from the publisher's timing. |

## Why a listener *and* a subscriber

zbus gives you both for a reason — they trade off latency against
decoupling:

- **Listener** — zero extra latency, but runs on the *publisher's*
  thread/priority. A slow listener would stall the publisher. Use it
  for small, fast, must-not-miss reactions.
- **Subscriber** — fully decoupled via its own thread and an internal
  message queue, so it can take as long as it needs without ever
  blocking the publisher. Use it for anything non-trivial: logging,
  storage, network I/O, etc.

This demo puts one of each on the same channel to make that contrast
directly visible in the log timestamps.

## What the log proves

See [`logs/zbus-pipeline-result.txt`](logs/zbus-pipeline-result.txt)
for a full captured run on real hardware (B-L4S5I-IOT01A). Key things
to look for:

1. **Publish cadence is steady** — `[SENSOR]` lines land at tick 100,
   200, 300 ... 1001, exactly 100ms apart, unaffected by either
   consumer.
2. **Listener has zero latency** — every `[DISPLAY]` line shares the
   *exact same tick* as its corresponding `[SENSOR]` line (e.g. both
   at `tick=901`), since it runs inline in the same call stack.
3. **Subscriber is slower but never blocks the publisher** — every
   `[LOGGER]` entry takes ~40ms between "received" and "done logging"
   (e.g. `tick=100` → `tick=140`), yet the *next* `[SENSOR]` publish
   still lands exactly 100ms after the previous one. The subscriber's
   own thread absorbs the delay instead of the publisher waiting on it.

## Files
