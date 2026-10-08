
/*
 * L5 Task 1 - Reliability under pressure
 *
 * Producer/consumer pipeline over a k_msgq, protected by:
 *   - a task watchdog on the consumer (callback logs the expiry)
 *   - a health-check thread that warns at 75% queue fill
 *
 * The consumer deliberately gets "stuck" (long k_sleep, no watchdog
 * feed) after STUCK_AFTER_MSGS messages, so we can observe:
 *   queue fills -> health warning -> queue full/drops -> watchdog fires
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(l5, LOG_LEVEL_DBG);

#define STACK_SIZE        1536
#define QUEUE_CAPACITY    8
#define WARN_PERCENT      75
#define WARN_THRESHOLD    ((QUEUE_CAPACITY * WARN_PERCENT) / 100)  /* 6 */

#define PRODUCE_MS        100
#define TOTAL_MESSAGES    80
#define STUCK_AFTER_MSGS  10
#define STUCK_MS          5000
#define WDT_TIMEOUT_MS    1000
#define HEALTH_PERIOD_MS  50
#define HEALTH_REPORT_MS  500

struct pipe_msg {
	uint32_t seq;
	uint32_t tick;
};

K_MSGQ_DEFINE(pipe_q, sizeof(struct pipe_msg), QUEUE_CAPACITY, 4);

static atomic_t produced;
static atomic_t consumed;
static atomic_t dropped;
static atomic_t wdt_fires;
static volatile uint32_t max_depth;

/* ------------------------------------------------------------------ */
/*  Task watchdog callback - runs in ISR context, keep it minimal      */
/* ------------------------------------------------------------------ */

static void wdt_callback(int channel_id, void *user_data)
{
	const char *name = k_thread_name_get((k_tid_t)user_data);

	atomic_inc(&wdt_fires);
	LOG_ERR("[WDT     ] channel %d EXPIRED - thread '%s' missed its %d ms "
		"deadline (queue=%u/%u)",
		channel_id, name ? name : "?", WDT_TIMEOUT_MS,
		k_msgq_num_used_get(&pipe_q), QUEUE_CAPACITY);
}

/* ------------------------------------------------------------------ */
/*  Consumer - feeds the watchdog, gets stuck once on purpose          */
/* ------------------------------------------------------------------ */

static void consumer_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	struct pipe_msg msg;
	bool stuck_done = false;

	int ret = task_wdt_init(NULL);   /* software-only task watchdog */

	if (ret != 0) {
		LOG_ERR("task_wdt_init failed: %d", ret);
		return;
	}

	int ch = task_wdt_add(WDT_TIMEOUT_MS, wdt_callback, (void *)k_current_get());

	if (ch < 0) {
		LOG_ERR("task_wdt_add failed: %d", ch);
		return;
	}

	LOG_INF("[CONSUMER] started, wdt channel=%d timeout=%d ms",
		ch, WDT_TIMEOUT_MS);

	while (1) {
		task_wdt_feed(ch);

		/* Timeout keeps the loop (and the feed) alive when idle */
		if (k_msgq_get(&pipe_q, &msg, K_MSEC(200)) != 0) {
			continue;
		}

		atomic_inc(&consumed);
		LOG_INF("[CONSUMER] got seq=%u  queue_latency=%u ms  queue=%u/%u",
			msg.seq, k_uptime_get_32() - msg.tick,
			k_msgq_num_used_get(&pipe_q), QUEUE_CAPACITY);

		if (!stuck_done && (int)atomic_get(&consumed) == STUCK_AFTER_MSGS) {
			stuck_done = true;
			LOG_WRN("[CONSUMER] *** SIMULATING STUCK: k_sleep(%d ms), "
				"no watchdog feed ***", STUCK_MS);
			k_sleep(K_MSEC(STUCK_MS));
			LOG_INF("[CONSUMER] recovered, resuming (queue=%u/%u)",
				k_msgq_num_used_get(&pipe_q), QUEUE_CAPACITY);
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Producer - one message every 100 ms, never blocks                  */
/* ------------------------------------------------------------------ */

static void producer_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (uint32_t i = 0; i < TOTAL_MESSAGES; i++) {
		k_msleep(PRODUCE_MS);

		struct pipe_msg m = { .seq = i, .tick = k_uptime_get_32() };

		if (k_msgq_put(&pipe_q, &m, K_NO_WAIT) == 0) {
			atomic_inc(&produced);
			LOG_INF("[PRODUCER] put seq=%u  queue=%u/%u",
				i, k_msgq_num_used_get(&pipe_q), QUEUE_CAPACITY);
		} else {
			int n = (int)atomic_inc(&dropped) + 1;

			/* Don't spam: log the first drop, then every 10th */
			if (n == 1 || (n % 10) == 0) {
				LOG_WRN("[PRODUCER] queue FULL - dropped seq=%u "
					"(total dropped=%d)", i, n);
			}
		}
	}

	LOG_INF("[PRODUCER] finished producing %d messages", TOTAL_MESSAGES);
}

/* ------------------------------------------------------------------ */
/*  Health check - warns at 75% fill, edge-triggered to avoid spam     */
/* ------------------------------------------------------------------ */

static void health_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	bool warned = false;
	bool full_reported = false;
	uint32_t last_report = 0;

	while (1) {
		k_msleep(HEALTH_PERIOD_MS);

		uint32_t used = k_msgq_num_used_get(&pipe_q);
		uint32_t pct = (used * 100U) / QUEUE_CAPACITY;
		uint32_t now = k_uptime_get_32();

		if (used > max_depth) {
			max_depth = used;
		}

		if (used >= WARN_THRESHOLD && !warned) {
			warned = true;
			LOG_WRN("[HEALTH  ] WARNING queue at %u%% (%u/%u) - reached "
				"%d%% threshold", pct, used, QUEUE_CAPACITY,
				WARN_PERCENT);
		} else if (used < WARN_THRESHOLD && warned) {
			warned = false;
			LOG_INF("[HEALTH  ] queue back down to %u%% (%u/%u) - recovered",
				pct, used, QUEUE_CAPACITY);
		}

		if (used == QUEUE_CAPACITY && !full_reported) {
			full_reported = true;
			LOG_ERR("[HEALTH  ] queue 100%% FULL - producer will start dropping");
		} else if (used < QUEUE_CAPACITY) {
			full_reported = false;
		}

		if (now - last_report >= HEALTH_REPORT_MS) {
			last_report = now;
			LOG_INF("[HEALTH  ] depth %u/%u (%u%%)",
				used, QUEUE_CAPACITY, pct);
		}
	}
}

K_THREAD_DEFINE(consumer_thread, STACK_SIZE, consumer_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(producer_thread, STACK_SIZE, producer_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(health_thread,   STACK_SIZE, health_fn,   NULL, NULL, NULL, 4, 0, 0);

int main(void)
{
	LOG_INF("=== L5 Task 1: reliability under pressure ===");
	LOG_INF("queue=%d slots, warn at %d%% (%d msgs), wdt timeout=%d ms, "
		"consumer stuck for %d ms after msg %d",
		QUEUE_CAPACITY, WARN_PERCENT, WARN_THRESHOLD, WDT_TIMEOUT_MS,
		STUCK_MS, STUCK_AFTER_MSGS);

	k_msleep((TOTAL_MESSAGES * PRODUCE_MS) + 1500);

	LOG_INF("[SUMMARY] accepted=%d consumed=%d dropped=%d wdt_fires=%d "
		"max_queue_depth=%u/%u",
		(int)atomic_get(&produced), (int)atomic_get(&consumed),
		(int)atomic_get(&dropped), (int)atomic_get(&wdt_fires),
		max_depth, QUEUE_CAPACITY);

	return 0;
}
