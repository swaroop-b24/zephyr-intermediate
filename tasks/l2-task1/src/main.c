#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE   1024
#define THREAD_PRIO  5
#define ITERATIONS   500000

 #define USE_MUTEX 1

static int shared_counter;
static K_MUTEX_DEFINE(counter_mutex);

static void increment_unsafe(void)
{
	shared_counter++;
}

static void increment_safe(void)
{
	k_mutex_lock(&counter_mutex, K_FOREVER);
	shared_counter++;
	k_mutex_unlock(&counter_mutex);
}

void t_a_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	for (int i = 0; i < ITERATIONS; i++) {
		if (USE_MUTEX) {
			increment_safe();
		} else {
			increment_unsafe();
		}
	}
	printk("T_A done\n");
}

void t_b_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	for (int i = 0; i < ITERATIONS; i++) {
		if (USE_MUTEX) {
			increment_safe();
		} else {
			increment_unsafe();
		}
	}
	printk("T_B done\n");
}

K_THREAD_DEFINE(t_a, STACK_SIZE, t_a_fn, NULL, NULL, NULL, THREAD_PRIO, 0, 0);
K_THREAD_DEFINE(t_b, STACK_SIZE, t_b_fn, NULL, NULL, NULL, THREAD_PRIO, 0, 0);

int main(void)
{
	printk("=== Race condition demo (USE_MUTEX=%d) ===\n", USE_MUTEX);

	/* Give both threads time to finish before reporting the result. */
	k_sleep(K_SECONDS(5));

	int expected = ITERATIONS * 2;

	printk("Expected: %d\n", expected);
	printk("Actual:   %d\n", shared_counter);
	printk("Lost updates: %d\n", expected - shared_counter);

	return 0;
}
