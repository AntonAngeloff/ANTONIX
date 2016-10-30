/*
 * syncobjs.h
 *
 *	Actually spinlocks are used only on multi-processor (SMP) environments. But
 *	we'll implement such tool anyway. Another good use of spinlocks is to temporary
 *	disable IRQs and later restore original state (be it enabled or disabled).
 *
 *	Our mutex implementation is of recursive mutices. It is possible to implement
 *	non-recursive ones in future, with some kind of flags.

 *  Created on: 15.11.2015 ã.
 *      Author: Anton Angelov
 *
 */

#ifndef INCLUDE_SYNCOBJS_H_
#define INCLUDE_SYNCOBJS_H_

#include <types.h>

#define DEFINE_SPINLOCK(name) (K_SPINLOCK ##name; spinlock_init(&##name);)

#define EVENT_STATE_SIGNALED	0x01
#define EVENT_STATE_UNSIGNALED	0x00

#define EVENT_FLAG_NONE			0x00
#define EVENT_FLAG_AUTORESET	0x01

#define TIMEOUT_INFINITE		0xFFFFFFFF

typedef struct SPINLOCK K_SPINLOCK;
struct SPINLOCK {
	uint32_t lock;
};

typedef struct MUTEX K_MUTEX;
struct MUTEX {
	//uint32_t	lock;
	K_SPINLOCK	inner_lock;

	/* Lock counter is used for recursinve locking */
	uint32_t	lock_count;

	/* Owner process and thread */
	uint32_t	pid;
	uint32_t	tid;
};

typedef struct K_EVENT K_EVENT;
struct K_EVENT {
	uint32_t 	owner_pid;
	uint32_t 	state;
	uint8_t		autoreset;
	K_SPINLOCK 	lock;
};

/* ANTONIX spinlock API */
void __nxapi spinlock_create(K_SPINLOCK *sl);
void __nxapi spinlock_destroy(K_SPINLOCK *sl);
uint32_t __nxapi spinlock_acquire(K_SPINLOCK *sl);
void __nxapi spinlock_release(K_SPINLOCK *sl, uint32_t if_state);

/* Mutex */
void __nxapi mutex_create(K_MUTEX *m);
void __nxapi mutex_destroy(K_MUTEX *m);
void __nxapi mutex_lock(K_MUTEX *m);
void __nxapi mutex_unlock(K_MUTEX *m);

/* Event routines */
void __nxapi event_create(K_EVENT *e, uint32_t flags);
void __nxapi event_destroy(K_EVENT *e);
void __nxapi event_signal(K_EVENT *e);
void __nxapi event_reset(K_EVENT *e);
HRESULT __nxapi event_waitfor(K_EVENT *e, uint32_t timeout);

#endif /* INCLUDE_SYNCOBJS_H_ */
