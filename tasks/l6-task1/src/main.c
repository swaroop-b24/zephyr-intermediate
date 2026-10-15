
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE            2048
#define MAINTENANCE_PRIORITY     4
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000

/*
 * Response-time guarantee:
 *   control work must START within DEADLINE_MS of the event becoming
 *   ready (i.e. of the timer interrupt that queued it).
 */
#define DEADLINE_MS             10
#define SUMMARY_EVERY           20

/*
 * The ONE correction (selected at build time with CONFIG_APP_FIX_PRIORITY):
 * the thread that owns a deadline must outrank background work that has
 * no deadline. Lower number = higher priority in Zephyr.
 */
#ifdef CONFIG_APP_FIX_PRIORITY
#define CONTROL_PRIORITY         3   /* corrected: outranks maintenance (4) */
#else
#define CONTROL_PRIORITY         7   /* starter: maintenance (4) outranks it */
#endif

struct control_event {
    uint32_t seq;
    uint32_t ready_ms;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), 4, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);

/* Written only by the control thread, so no locking is needed. */
static uint32_t events_seen;
static uint32_t deadline_misses;
static uint32_t max_latency_ms;

/* ================================================================== */
/*  Timer expiry: creates one control event                           */
/* ================================================================== */

static void event_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    static uint32_t seq;
    struct control_event event = {
        .seq = seq++,
        .ready_ms = k_uptime_get_32(),
    };

    /* Emitted first so it precedes the thread_ready events in the trace. */
    sys_trace_named_event("event_ready", event.seq, event.ready_ms);

    /* Timer expiry runs in interrupt context, so never wait here. */
    int ret = k_msgq_put(&control_queue, &event, K_NO_WAIT);

    if (ret != 0) {
        sys_trace_named_event("event_dropped", event.seq, 0);
        return;
    }

    /* Both threads become ready when the timer interrupt returns. */
    k_sem_give(&maintenance_start);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

/* ================================================================== */
/*  Control thread                                                   */
/* ================================================================== */

static void control_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        struct control_event event;
        int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[CONTROL] receive failed: %d", ret);
            continue;
        }

        /* Time from "event ready" (timer ISR) to "control work starts". */
        uint32_t latency_ms = k_uptime_get_32() - event.ready_ms;
        bool missed = latency_ms > DEADLINE_MS;

        events_seen++;
        if (latency_ms > max_latency_ms) {
            max_latency_ms = latency_ms;
        }

        if (missed) {
            /* Count EVERY miss; rate-limit only the printing. */
            deadline_misses++;
            LOG_WRN_RATELIMIT("deadline_miss count=%u seq=%u latency_ms=%u limit_ms=%u",
                              deadline_misses, event.seq, latency_ms, DEADLINE_MS);
        }

        LOG_INF("event_done seq=%u latency_ms=%u deadline=%s misses=%u",
                event.seq, latency_ms, missed ? "MISS" : "ok", deadline_misses);

        sys_trace_named_event("event_done", event.seq, latency_ms);

        if ((events_seen % SUMMARY_EVERY) == 0) {
            LOG_INF("summary events=%u deadline_misses=%u max_latency_ms=%u",
                    events_seen, deadline_misses, max_latency_ms);
        }
    }
}

/* ================================================================== */
/*  Background maintenance thread                                    */
/* ================================================================== */

static void maintenance_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        k_sem_take(&maintenance_start, K_FOREVER);

        /* This work is important, but it has no short deadline. */
        k_busy_wait(MAINTENANCE_LOAD_US);
    }
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
                NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
                NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Homework: Runtime Investigation ===");
    LOG_INF("Control work must start within %d ms", DEADLINE_MS);
    LOG_INF("control priority=%d maintenance priority=%d",
            CONTROL_PRIORITY, MAINTENANCE_PRIORITY);

    k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

    return 0;
}
