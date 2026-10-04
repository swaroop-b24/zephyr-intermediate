/*
 * Lecture 3 - Homework Solution (Task 2 + Task 3)
 *
 * GOAL: Event-driven workqueue architecture instead of polling.
 *
 * sensor_sim fires every 100ms and submits a k_work item directly -
 * no shared flag, no polling thread waking up every 10ms to check it.
 * The system workqueue's own worker thread runs sensor_handler()
 * only when there's actually work to do.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE    1024
#define SENSOR_MS     100    /* sensor fires every 100ms */
#define EVENT_COUNT   10     /* total sensor events to produce */

/* Statistics */
static int total_events;
static int total_processed;

/* ------------------------------------------------------------------ */
/*  Task 2: work handler - runs ONLY when submitted, no polling        */
/* ------------------------------------------------------------------ */

static void sensor_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	total_processed++;

	/* Task 3: timestamp confirms this only runs on real events,
	 * at ~100ms intervals matching sensor_sim - not every 10ms. */
	LOG_INF("[HANDLER] processed event %d  tick=%u",
		total_processed, k_uptime_get_32());
}

K_WORK_DEFINE(sensor_work, sensor_handler);

/* ------------------------------------------------------------------ */
/*  sensor_sim - fires EVENT_COUNT events, 100ms apart                */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < EVENT_COUNT; i++) {
		k_msleep(SENSOR_MS);

		total_events++;
		LOG_INF("[SENSOR] event %d  tick=%u", i, k_uptime_get_32());

		int ret = k_work_submit(&sensor_work);

		if (ret < 0) {
			LOG_ERR("submit failed: %d", ret);
		}
	}

	LOG_INF("[SENSOR] all events produced");
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	LOG_INF("=== L3 Homework: Workqueue solution ===");
	LOG_INF("sensor fires every %dms, handler runs only on real events",
		SENSOR_MS);

	/* Wait long enough for all events to complete */
	k_msleep((EVENT_COUNT + 2) * SENSOR_MS + 500);

	LOG_INF("\n");
	LOG_INF("[SUMMARY] events=%d  processed=%d  (no wasted wakeups - "
		"handler only runs on real events)",
		total_events, total_processed);

	return 0;
}