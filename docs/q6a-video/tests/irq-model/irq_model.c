/*
 * User-space model of the Iris IRQ-thread / power-off interaction (NOT kernel code).
 * It models only the locking, to show three things deterministically:
 *   1. current tree, runtime-suspend path : the IRQ thread touches registers after the clocks went off
 *   2. upstream b9c2215bded on this tree  : disable_irq() under core->lock waits for a thread that waits for the lock
 *   3. this series (hw_lock + hw_powered) : neither happens
 * Build: cc -O1 -pthread irq_model.c -o irq_model
 */
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

enum mode { CURRENT, UPSTREAM_ONELINER, SERIES };

static pthread_mutex_t core_lock = PTHREAD_MUTEX_INITIALIZER;   /* core->lock (a mutex) */
static pthread_mutex_t hw_lock   = PTHREAD_MUTEX_INITIALIZER;   /* core->hw_lock (a spinlock in the kernel) */
static atomic_bool clocks_on, hw_powered, thread_active, thread_may_run, bus_error;
static enum mode mode;

static void *irq_thread(void *arg)             /* iris_hfi_isr_handler() */
{
	(void)arg;
	while (!atomic_load(&thread_may_run)) usleep(1000);   /* scheduling delay: woken but not yet running */
	if (mode == SERIES) {
		pthread_mutex_lock(&hw_lock);
		if (atomic_load(&hw_powered)) { if (!atomic_load(&clocks_on)) atomic_store(&bus_error, true); }
		pthread_mutex_unlock(&hw_lock);
	} else {
		pthread_mutex_lock(&core_lock);                 /* handler takes core->lock before the register access */
		if (!atomic_load(&clocks_on)) atomic_store(&bus_error, true);
		pthread_mutex_unlock(&core_lock);
	}
	atomic_store(&thread_active, false);
	return NULL;
}

static bool disable_irq_wait(int ms)                    /* disable_irq(): synchronize_irq() waits for the thread */
{
	for (int i = 0; i < ms; i++) { if (!atomic_load(&thread_active)) return true; usleep(1000); }
	return false;
}

static void power_off(bool *deadlock)
{
	if (mode == SERIES) {
		pthread_mutex_lock(&hw_lock); atomic_store(&hw_powered, false); pthread_mutex_unlock(&hw_lock);
	}
	atomic_store(&clocks_on, false);                       /* clocks / power domains off */
	if (mode == UPSTREAM_ONELINER && !disable_irq_wait(1000))
		*deadlock = true;
}

static void scenario(const char *name, enum mode m, bool under_core_lock)
{
	pthread_t t; bool deadlock = false;
	mode = m; atomic_store(&clocks_on, true); atomic_store(&hw_powered, true);
	atomic_store(&thread_active, true); atomic_store(&thread_may_run, false); atomic_store(&bus_error, false);
	pthread_create(&t, NULL, irq_thread, NULL);             /* interrupt fired, thread woken */
	if (under_core_lock) pthread_mutex_lock(&core_lock);    /* iris_core_deinit() holds core->lock */
	power_off(&deadlock);
	atomic_store(&thread_may_run, true);                    /* the thread is finally scheduled */
	if (deadlock) { /* the caller still holds core_lock, so the thread can never finish */ }
	if (under_core_lock) pthread_mutex_unlock(&core_lock);
	pthread_join(t, NULL);
	printf("%-58s deadlock=%-3s register-access-with-clocks-off=%s\n", name,
	       deadlock ? "YES" : "no", atomic_load(&bus_error) ? "YES" : "no");
}

int main(void)
{
	scenario("1 runtime suspend, current tree",              CURRENT,           false);
	scenario("2 core_deinit under core->lock, upstream 1-liner", UPSTREAM_ONELINER, true);
	scenario("3 runtime suspend, this series",               SERIES,            false);
	scenario("4 core_deinit under core->lock, this series",  SERIES,            true);
	return 0;
}
