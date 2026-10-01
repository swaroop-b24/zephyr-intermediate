

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE 1024
#define BUSY_LOOPS 500000

void t_low_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	while (1) {
		printk("[%u ms] T_LOW running\n", k_uptime_get_32());
		k_msleep(300);
	}
}

void t_med_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	while (1) {
		printk("[%u ms] T_MED running\n", k_uptime_get_32());
		k_msleep(200);
	}
}

void t_high_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	while (1) {
		printk("[%u ms] T_HIGH running\n", k_uptime_get_32());
		k_msleep(100);
	}
}

/* Cooperative thread (priority -1): once scheduled, it keeps the CPU
 * until it blocks or explicitly yields - it is never preempted by
 * lower-priority preemptive threads, only by higher-priority
 * cooperative threads or ISRs. */
void t_coop_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	volatile uint32_t busy;

	while (1) {
		for (int i = 0; i < 5; i++) {
			busy = 0;
			for (uint32_t j = 0; j < BUSY_LOOPS; j++) {
				busy++;
			}
			printk("[%u ms] T_COOP busy iteration %d/5\n",
			       k_uptime_get_32(), i + 1);
		}
		printk("[%u ms] T_COOP yielding\n", k_uptime_get_32());
		k_yield();
	}
}

K_THREAD_DEFINE(t_low, STACK_SIZE, t_low_fn, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(t_med, STACK_SIZE, t_med_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn, NULL, NULL, NULL, 3, 0, 0);
K_THREAD_DEFINE(t_coop, STACK_SIZE, t_coop_fn, NULL, NULL, NULL, -1, 0, 0);

int main(void)
{
	printk("=== Scheduler demo: Ready/Waiting, k_yield vs k_sleep ===\n");
	return 0;
}
