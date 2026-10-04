/*
 * Lecture 3 - Homework Solution (BONUS: debounce)
 *
 * GOAL: sensor_sim fires a burst of 5 events within 20ms. Using
 * k_work_reschedule() with a 30ms delay means every new submission
 * pushes the handler's execution further out - so only the LAST
 * event in the burst actually results in a handler run, 30ms after
 * the burst settles. The other 4 "reschedules" are absorbed.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE       1024
#define BURST_EVENTS     5
#define BURST_SPACING_MS 4
#define DEBOUNCE_MS      30

static int total_events;
static int total_handler_runs;

static void sensor_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	total_handler_runs++;
	LOG_INF("[HANDLER] fired (debounced)  run=%d  tick=%u",
		total_handler_runs, k_uptime_get_32());
}

K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	k_msleep(200);
	LOG_INF("[SENSOR] burst starting  tick=%u", k_uptime_get_32());

	for (int i = 0; i < BURST_EVENTS; i++) {
		total_events++;
		LOG_INF("[SENSOR] event %d  tick=%u  -> reschedule +%dms",
			i, k_uptime_get_32(), DEBOUNCE_MS);

		k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));

		if (i < BURST_EVENTS - 1) {
			k_msleep(BURST_SPACING_MS);
		}
	}

	LOG_INF("[SENSOR] burst complete, %d events fired within ~%dms",
		BURST_EVENTS, (BURST_EVENTS - 1) * BURST_SPACING_MS);
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	LOG_INF("=== L3 Homework BONUS: Debounce via k_work_reschedule ===");
	LOG_INF("Firing %d events within ~%dms, debounce window=%dms",
		BURST_EVENTS, (BURST_EVENTS - 1) * BURST_SPACING_MS, DEBOUNCE_MS);

	k_msleep(500);

	LOG_INF("\n");
	LOG_INF("[SUMMARY] events_fired=%d  handler_runs=%d  "
		"(burst of %d collapsed to %d execution)",
		total_events, total_handler_runs, total_events, total_handler_runs);

	return 0;
}