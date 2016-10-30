/*
 * syncobjs.c
 *
 *  Created on: 15.11.2015 ã.
 *      Author: Anton Angelov
 */

#include <string.h>
#include <hal.h>
#include "include/syncobjs.h"
#include "scheduler.h"
#include <timer.h>

uint32_t __nxapi spinlock_acquire(K_SPINLOCK *sl)
{
	/* Retrieve EFLAGS and particularly IF */
	uint32_t if_state = hal_get_eflags() & 0x200 ? 1 : 0;
	hal_cli();

	/* Constantly update sl->lock value to 1, until
	 * it's old value turn out to be 0.
	 */
	while (atomic_update_int(&sl->lock, 1) != 0) {
		//do nothing
		;
	}

	/* Spinlock is acquired at this point */
	return if_state;
}

void __nxapi spinlock_release(K_SPINLOCK *sl, uint32_t if_state)
{
	/* Release spinlock */
	if (atomic_update_int(&sl->lock, 0) != 1) {
		HalKernelPanic("spinlock_realase(): Trying to release a non-locked spinlock.");
	}

	/* Restore previous IF state */
	if (if_state) {
		hal_sti();
	}
}

void __nxapi spinlock_create(K_SPINLOCK *sl)
{
	memset(sl, 0, sizeof(K_SPINLOCK));
}

void __nxapi spinlock_destroy(K_SPINLOCK *sl)
{
	UNUSED_ARG(sl);
	//Does nothing right now;
}

void __nxapi mutex_create(K_MUTEX *m)
{
	memset(m, 0, sizeof(K_MUTEX));
	spinlock_create(&m->inner_lock);
}

static uint32_t mutex_get_lock_count(K_MUTEX *m)
{
	uint32_t c;

	uint32_t intr_status = spinlock_acquire(&m->inner_lock);
	c = m->lock_count;
	spinlock_release(&m->inner_lock, intr_status);

	return c;
}

void __nxapi mutex_destroy(K_MUTEX *m)
{
	/* If mutex is still held, wait until it's released totally */
	while (mutex_get_lock_count(m) != 0) {
		sched_yield();
	}

	spinlock_destroy(&m->inner_lock);
	UNUSED_ARG(m);
}

/*
 * Recursive mutex lock.
 * The inner state of the mutex is guarded by a spinlock.
 */
void __nxapi mutex_lock(K_MUTEX *m)
{
	HRESULT		hr;
	uint32_t	intr_status;
	uint32_t	curr_pid;
	uint32_t	curr_tid;
	BOOL		pass;

//	/* Try to lock. If failed, switch to next thread */
//	while (atomic_update_int(&m->lock, 1) != 0) {
//		sched_update_sw();
//	}

retry:

	/* Acquire inner spinlock */
	intr_status = spinlock_acquire(&m->inner_lock);

	/* We made it */
	hr = sched_get_current_pid(&curr_pid);
	if (FAILED(hr)) {
		HalKernelPanic("mutex_lock(): Failed to retrieve current process id.");
	}

	hr = sched_get_current_tid(&curr_tid);
	if (FAILED(hr)) {
		HalKernelPanic("mutex_lock(): Failed to retrieve current thread id.");
	}

	/* Assume we will pass */
	pass = TRUE;

	/* If mutex is already locked, we can only pass if it
	 * is locked by the current process/thread.
	 */
	if (m->lock_count > 0) {
		if (m->pid != curr_pid || m->tid != curr_tid) {
			pass = FALSE;
			HalKernelPanic("...");
		}
	}

	if (pass) {
		if (m->lock_count == 0) {
			/* Set new owner */
			m->pid = curr_pid;
			m->tid = curr_tid;
		}

		m->lock_count++;
	}

	/* Unlock spinlock */
	spinlock_release(&m->inner_lock, intr_status);

	if (!pass) {
		//sched_yield();
		goto retry;
	}
}

/*
 * Idiot-proof mutex unlocking routine.
 */
void __nxapi mutex_unlock(K_MUTEX *m)
{
	HRESULT		hr;
	uint32_t	intr_status;
	uint32_t	curr_pid;
	uint32_t	curr_tid;

//	/* Release lock and go to next thread. */
//	atomic_update_int(&m->lock, 0);
//	//sched_update_sw();

	/* Acquire inner spinlock */
	intr_status = spinlock_acquire(&m->inner_lock);

	/* Get current pid/tid */
	hr = sched_get_current_pid(&curr_pid);
	if (FAILED(hr)) {
		HalKernelPanic("mutex_lock(): Failed to retrieve current process id.");
	}

	hr = sched_get_current_tid(&curr_tid);
	if (FAILED(hr)) {
		HalKernelPanic("mutex_lock(): Failed to retrieve current thread id.");
	}

	if (m->pid != curr_pid || m->tid != curr_tid) {
		HalKernelPanic("mutex_unlock(): Trying to unlock a non-owned mutex.");
	}

	if (m->lock_count <= 0) {
		HalKernelPanic("mutex_unlock(): Trying to unlock non-locked mutex or counter dropped below zero.");
	}

	m->lock_count--;
	spinlock_release(&m->inner_lock, intr_status);
}

void __nxapi event_create(K_EVENT *e, uint32_t flags)
{
	/* Initialize event. This will reset state
	 * to unsignaled state.
	 */
	memset(e, 0, sizeof(K_EVENT));
	spinlock_create(&e->lock);

	switch (flags) {
		case EVENT_FLAG_AUTORESET:
			e->autoreset = 1;
			break;
	}
}

void __nxapi event_destroy(K_EVENT *e)
{
	spinlock_destroy(&e->lock);
}

void __nxapi event_signal(K_EVENT *e)
{
	uint32_t intf = spinlock_acquire(&e->lock);
	e->state++;
	spinlock_release(&e->lock, intf);
}

void __nxapi event_reset(K_EVENT *e)
{
	uint32_t intf = spinlock_acquire(&e->lock);
	e->state = 0;
	spinlock_release(&e->lock, intf);
}

HRESULT __nxapi event_waitfor(K_EVENT *e, uint32_t timeout)
{
	uint32_t state;
	uint32_t intf;
	uint32_t initial_time;

	/* Get current tick count */
	initial_time = timer_gettickcount();

	do {
		/*
		 * Lock event spinlock to get state
		 */
		intf = spinlock_acquire(&e->lock);
		state = e->state;

		/*
		 * Apply auto-reset
		 */
		if (state != EVENT_STATE_UNSIGNALED && e->autoreset) {
			e->state = EVENT_STATE_UNSIGNALED;
		}
		spinlock_release(&e->lock, intf);

		/* If timeout has elapsed, terminate waiting cycle
		 * and issue an error code.
		 */
		if (state == EVENT_STATE_UNSIGNALED && timer_gettickcount() - initial_time >= timeout) {
			return E_TIMEDOUT;
		}

		/*
		 * If event is not yet signaled yield time slice to next thread
		 * and try again later.
		 */
		if (state == EVENT_STATE_UNSIGNALED) {
			sched_update_sw();
		}
	} while (state == EVENT_STATE_UNSIGNALED);

	return S_OK;
}
