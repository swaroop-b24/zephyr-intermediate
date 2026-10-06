/*
 * L4 Task 1 - Event-driven system with zbus
 *
 * One channel carries simulated sensor samples, published every 100ms.
 *
 * - display_listener: a zbus LISTENER. Its callback runs SYNCHRONOUSLY,
 *   inline, in the publisher thread's own context, the instant
 *   zbus_chan_pub() is called. No extra thread, no delay - this is the
 *   "fast path" for things like updating a display that must react
 *   immediately.
 *
 * - logger_subscriber: a zbus SUBSCRIBER. It has its OWN thread that
 *   blocks on zbus_sub_wait(), decoupled from the publisher. We add an
 *   artificial delay inside it to simulate slower work (e.g. writing
 *   to flash), proving the subscriber's pace doesn't block or depend
 *   on the publisher's 100ms cadence.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zbus_demo, LOG_LEVEL_DBG);

#define PUBLISH_PERIOD_MS   100
#define LOGGER_WORK_MS      40   /* simulated "slow" logging work */
#define SAMPLE_COUNT        10

struct sensor_msg {
	uint32_t value;
	uint32_t timestamp;
};

/* ------------------------------------------------------------------ */
/*  Channel definition                                                */
/* ------------------------------------------------------------------ */

ZBUS_CHAN_DEFINE(sensor_chan,
		  struct sensor_msg,
		  NULL,
		  NULL,
		  ZBUS_OBSERVERS(display_listener, logger_subscriber),
		  ZBUS_MSG_INIT(0)
);

/* ------------------------------------------------------------------ */
/*  Listener - fast path, runs inline in the publisher's context      */
/* ------------------------------------------------------------------ */

static void display_listener_cb(const struct zbus_channel *chan)
{
	const struct sensor_msg *msg = zbus_chan_const_msg(chan);

	LOG_INF("[DISPLAY ] (listener, inline) value=%u  tick=%u",
		msg->value, k_uptime_get_32());
}

ZBUS_LISTENER_DEFINE(display_listener, display_listener_cb);

/* ------------------------------------------------------------------ */
/*  Subscriber - own thread, slower consumer, decoupled from publish  */
/* ------------------------------------------------------------------ */

ZBUS_SUBSCRIBER_DEFINE(logger_subscriber, 4);

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	const struct zbus_channel *chan;

	while (!zbus_sub_wait(&logger_subscriber, &chan, K_FOREVER)) {
		struct sensor_msg msg;

		if (zbus_chan_read(chan, &msg, K_MSEC(100)) == 0) {
			LOG_INF("[LOGGER  ] (subscriber) received value=%u  "
				"recv_tick=%u, now logging (slow)...",
				msg.value, k_uptime_get_32());

			/* Simulated slower work - writing to storage, etc. */
			k_msleep(LOGGER_WORK_MS);

			LOG_INF("[LOGGER  ] (subscriber) done logging value=%u  "
				"tick=%u", msg.value, k_uptime_get_32());
		}
	}
}

K_THREAD_DEFINE(logger_thread, 1024, logger_thread_fn, NULL, NULL, NULL, 6, 0, 0);

/* ------------------------------------------------------------------ */
/*  Publisher - simulated sensor, publishes every 100ms               */
/* ------------------------------------------------------------------ */

static void publisher_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < SAMPLE_COUNT; i++) {
		k_msleep(PUBLISH_PERIOD_MS);

		struct sensor_msg msg = {
			.value = 100 + i * 5,   /* fake sensor reading */
			.timestamp = k_uptime_get_32(),
		};

		LOG_INF("[SENSOR  ] publishing value=%u  tick=%u",
			msg.value, msg.timestamp);

		zbus_chan_pub(&sensor_chan, &msg, K_MSEC(50));
	}

	LOG_INF("[SENSOR  ] all samples published");
}

K_THREAD_DEFINE(publisher_thread, 1024, publisher_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	LOG_INF("=== L4 Task 1: zbus event-driven sensor pipeline ===");
	LOG_INF("Publishing every %dms, %d samples total",
		PUBLISH_PERIOD_MS, SAMPLE_COUNT);

	k_msleep((SAMPLE_COUNT + 2) * PUBLISH_PERIOD_MS + 500);

	LOG_INF("\n[SUMMARY] Done. Compare DISPLAY (inline, immediate) vs "
		"LOGGER (own thread, %dms slower per message) timestamps above.",
		LOGGER_WORK_MS);

	return 0;
}
