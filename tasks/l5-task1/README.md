
# L5 Task 1 — Reliability Under Pressure

## Goal

Study what happens to a producer/consumer pipeline when the consumer
gets stuck, and add the supervision needed to notice and report it:
a task watchdog on the consumer and a health-check thread on the queue.

## Design

| Component | Role |
|---|---|
| `pipe_q` | `k_msgq`, 8 slots of `struct pipe_msg { seq, tick }`. 75% = 6 messages. |
| `producer_thread` | Puts one message every 100 ms with `K_NO_WAIT`. If the queue is full the message is dropped (drop-newest) and counted. |
| `consumer_thread` | Registers a task watchdog channel (1000 ms timeout) and feeds it at the top of every loop. After 10 messages it calls `k_sleep(5000)` once, without feeding, to simulate a stuck consumer. |
| Task watchdog | `task_wdt_init(NULL)`: software-only, no hardware fallback. The callback logs which thread missed its deadline and the queue depth. |
| `health_thread` | Samples queue depth every 50 ms. Logs a warning when depth reaches 75%, an error at 100%, and a recovery message when it drops back below 75%. Edge-triggered, so there is no log spam. |

The watchdog callback only logs. Passing `NULL` as the callback would
make the task watchdog reboot the system instead.

## Observed timeline

Captured on the B-L4S5I-IOT01A, full output in
[`logs/watchdog-pipeline-result.txt`](logs/watchdog-pipeline-result.txt).

| Time | Event |
|---|---|
| 1.001 s | Consumer starts its 5000 ms sleep. Queue is empty. |
| 1.101 s | First message queues up (1/8). Depth grows by one every 100 ms. |
| 1.603 s | `[HEALTH] WARNING queue at 75% (6/8)` |
| 1.802 s | Queue reaches 8/8, `[HEALTH] queue 100% FULL` |
| 1.901 s | `[WDT] channel 0 EXPIRED - thread 'consumer_thread'` (callback triggered) |
| 1.902 s | First drop: `queue FULL - dropped seq=18` |
| 6.001 s | Consumer wakes up and drains the backlog (seq 10 to 17) |
| 6.012 s | `[HEALTH] queue back down to 0% - recovered` |

Final summary: `accepted=39 consumed=39 dropped=41 wdt_fires=1 max_queue_depth=8/8`.
The counts add up: 39 accepted + 41 dropped = 80 messages produced.

## What this shows

1. **Early warning comes before the failure.** The health warning at
   1.603 s fires about 200 ms before the queue is full and about 300 ms
   before the watchdog. A depth trend is the earliest signal that a
   consumer is falling behind.
2. **The watchdog measures time since the last feed, not since the
   stall began.** The consumer last fed the watchdog at about 0.901 s,
   before it was blocked waiting for seq 9, so the 1000 ms deadline
   expired at 1.901 s, not 2.001 s.
3. **The watchdog tells you which thread is stuck.** The callback
   receives the thread via `user_data` and logs its name. A hardware
   watchdog alone could only reset the whole chip.
4. **Drop-newest keeps old data and loses new data.** After recovery
   the consumer processed seq 10 to 17 with 4.2 to 4.9 seconds of
   queue latency, while the 41 messages that arrived during the stall
   were lost. That is acceptable when older data is still valid, but
   for latest-value-wins data (sensor readings) drop-oldest would be
   the better strategy.
5. **The system recovers on its own.** Once the consumer resumed it
   fed the watchdog again and the queue drained to 0 within the same
   millisecond tick.

## Possible next step

Slide 27 of the lecture recommends layering a hardware watchdog behind
the task watchdog (`CONFIG_TASK_WDT_HW_FALLBACK=y`), so the chip resets
if the task watchdog itself hangs. This task uses software-only mode.

## Files
