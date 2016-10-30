/*
 * scheduler.h
 *
 *  Created on: 22.07.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_SCHEDULER_H_
#define INCLUDE_SCHEDULER_H_

#include <stdint.h>
#include "mm_virt.h"
#include "types.h"
#include "syncobjs.h"

#define	MAX_THREADS		32
#define MAX_PROCESSES	32
#define MAX_VM_REGIONS	32

/* Defines the default CPU time for a thread */
#define DEFAULT_THREAD_QUANTA		20

#define THREAD_STATE_READY			0x01
#define THREAD_STATE_RUNNING		0x02
#define THREAD_STATE_BLOCKED		0x03
#define THREAD_STATE_TERMINATED		0x04

#define PROCESS_PRIORITY_HIGHEST	0x00
#define PROCESS_PRIORITY_HIGH		0x01
#define PROCESS_PRIORITY_NORMAL		0x02
#define PROCESS_PRIORITY_LOW		0x03

#define PROCESS_MODE_KERNEL			0x00
#define PROCESS_MODE_USER			0x01

/* Standard stack sizes will be 8kb */
#define STACK_SIZE_KERNEL			0x2000
#define STACK_SIZE_USER				0x2000
#define STACK_GAP_SIZE				0x1000

/** Defines the number of different priority levels */
#define PROCESS_PRIORITY_LEVELS		0x04

#define	process_lock(proc) (spinlock_acquire(proc->spinlock);)
#define process_unlock(proc) (spinlock_release(proc->spinlock);)

typedef struct {
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;
	uint32_t esp;
	uint32_t ebp;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;
} K_STACKFRAME;

/**
 * Defines an ANTONIX thread
 */
typedef struct THREAD K_THREAD;
struct THREAD {
	void		*process;

	uint32_t	id;

	uint32_t	stack_size;
	uintptr_t	user_stack;
	uintptr_t	kernel_stack;

	uint32_t	eip;
	/** esp holds the esp value of the thread at the moment when it was paused,
	 * 	regardless in kernel or user mode. */
	uint32_t	esp;
	uint32_t	ebp;

	uint32_t 	state;
	uint32_t 	quanta;
	uint32_t	priority;

	/* Set to TRUE when the thread is first entered by
	 * the scheduler.
	 */
	uint8_t		running;

	K_THREAD	*next;
};

/* Since we will be using K_THREAD inside a queue, we want to
 * decouple the next pointer from it.
 * Edit: we don't decouple it, it was too much effort.
 */
typedef struct THREAD_NODE K_THREAD_NODE;
struct THREAD_NODE {
	K_THREAD	node;
	int32_t		ticks_remaining;

	K_THREAD_NODE	*next;
};

/**
 * Defines an ANTONIX process
 */
typedef struct PROCESS K_PROCESS;
struct PROCESS {
	/** Process name */
	char			name[32];

	/** ID of the process (pid). */
	uint32_t 		id;
	uint32_t		priority;
	uint32_t		mode;
	uint32_t		thread_id_counter;

	/** Each process can hold up to MAX_THREADS threads. */
	K_THREAD 		*threads[MAX_THREADS];
	uint32_t 		thread_count;

	/** Virtual memory region descriptors. Used for heap */
	K_VMM_REGION 	regions[32];
	uint32_t 	 	region_count;

	/** Process' page directory */
	K_VMM_PAGE_DIR	*page_dir;
	void			*page_dir_phys;

	/** A mutex which has to be locked when modifying the structure */
	K_SPINLOCK		lock;
};

/**
 * Describes the complete compound state of the scheduler
 */
typedef struct {
	/** Queue (actually 4 queues) of running tasks (threads) */
	K_THREAD		*run_queue;

	/** Queue of blocked tasks (threads). */
	K_THREAD_NODE	*blocked_queue;

	/** Pointer to current task */
	volatile K_THREAD *current;

	/** Spinlock for owning the state */
	K_SPINLOCK	lock;
} K_SCHEDULER_STATE;

/**
 * Initializes the scheduler subsystem
 */
HRESULT __nxapi sched_initialize(void *kernel_thread_entry);

/**
 * Finds a process by a given ID and retrieves it's process descriptor.
 */
HRESULT	__nxapi sched_find_process(uint32_t pid, K_PROCESS **proc);

/**
 * Creates a thread for particular process.
 */
HRESULT	__nxapi sched_create_thread(K_PROCESS *proc, void *entry_point, uint32_t *thread_id);

/**
 * Creates a new process with primary thread and position thread's
 * EIP at _entry_point_.
 */
HRESULT __nxapi sched_create_process(void *entry_point, uint32_t priority, uint32_t *pid_out);

/**
 * Switches to the virtual address space of process _new_proc_. This is done
 * by updating CR3 register with new_proc's page directory. Which automatically
 * flushes the whole TLB.
 */
HRESULT __nxapi	sched_enter_process_addr_space(K_PROCESS *new_proc);

/** Retrieves process descriptor for given pid */
HRESULT __nxapi sched_find_proc(uint32_t pid, K_PROCESS **out); //TODO: delete, since there is the same function (sched_find_process)

/**
 * Returns PID of current running process.
 */
HRESULT	__nxapi	sched_get_current_pid(uint32_t *pid);
HRESULT	__nxapi	sched_get_current_tid(uint32_t *tid);

/**
 * Returns pointer of current process descriptor struct.
 */
HRESULT __nxapi sched_get_current_proc(K_PROCESS **proc);

/**
 * This is called only by sched_update() and the prototype should be removed.
 */
HRESULT __nxapi sched_switch_to_thread(K_THREAD *t);

/**
 * Adds the thread argument to the scheduler's run queue.
 */
HRESULT __nxapi sched_add_thread_to_run_queue(K_THREAD *t);

/**
 * Called by threads when reaching their thread proc's end.
 */
HRESULT	__nxapi	sched_exit_thread(K_THREAD *t);

/**
 * Switches to the next following task (thread) in the task list. This function is called
 * only by  IRQ0.
 */
HRESULT __nxapi sched_update();

/**
 * Triggers IRQ0 interrupt vector to perform task switch.
 */
HRESULT __nxapi sched_update_sw();
HRESULT __nxapi sched_yield(); //alias for sched_update_sw();

/**
 * Enables and disables the scheduler. When scheduling is disabled
 * when an IRQ0 is invoked, sched_update() will just return without
 * doing nothing.
 */
HRESULT __nxapi sched_enable(uint32_t bool);

uint32_t __nxapi sched_get_process_count(void);
HRESULT __nxapi sched_get_process_by_id(uint32_t id, K_PROCESS **proc);

#endif /* INCLUDE_SCHEDULER_H_ */
