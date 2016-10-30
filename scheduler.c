/*
 * scheduler.c
 *
 *	Important:
 *		- Scheduler have to use only the skheap memory manager, since it is only
 *			available in pre-scheduler environment.
 *
 *  Created on: 22.07.2016 ã.
 *      Author: Admin
 */

#include <stddef.h>
#include <string.h>
#include <kstdio.h>
#include "hal.h" //for assert()
#include "scheduler.h"
#include "mm_phys.h"
#include "mm_skheap.h"
#include "desctables.h"
#include "timer.h"
#include "vga.h" //temp

/*
 * List of all processes
 */
static K_PROCESS 	*process_array[MAX_PROCESSES];
static uint32_t 	process_count;

/* Spin-lock for locking process_array */
static K_SPINLOCK	process_array_lock;

/*
 * It's important to note that we define the kernel process descriptor
 * here as global var.
 */
static K_PROCESS	kernel_proc;

/*
 * Is the scheduler initialized.
 */
static uint32_t	initialized = 0;

/*
 * Free PID counter
 */
static uint32_t 	free_pid = 100;

/*
 * Scheduler state
 */
K_SCHEDULER_STATE sched_state;

/*
 * Signifies weather the task-switching
 * is enabled.
 */
uint32_t sched_enabled;

/*
 * Define a label in the irq handler (from isr.asm)
 */
VOID return_to_irq_handler(void);

static	void __nxapi kernel_idle_task()
{
	while (1) {
		;
	}
}

HRESULT	__nxapi sched_find_process(uint32_t pid, K_PROCESS **proc)
{
	uint32_t	i;

	/* Lock process list */
	uint32_t ifl = spinlock_acquire(&process_array_lock);

	for (i=0; i<process_count; i++) {
		if (process_array[i]->id == pid) {
			*proc = process_array[i];

			spinlock_release(&process_array_lock, ifl);
			return S_OK;
		}
	}

	spinlock_release(&process_array_lock, ifl);
	return E_NOTFOUND;
}

/**
 * Searches for a proper location inside the virtual address space
 * where a stack could be placed (mapped).
 */
static uintptr_t sched_find_proper_stack_location(K_PROCESS *proc, size_t stack_size)
{
	/* I come to conclusion it's best to leave unmapped space (barrier) between
	 * different thread stacks.
	 */
	#define unmapped_barrier	0x1000

	/* We try to place the stack at the end of VAS. */
	uintptr_t index = 0xC0000000 - 4096;

	for (uint32_t i=0; i<proc->region_count; i++) {
		if (proc->regions[i].usage | USAGE_STACK) {
			if (proc->regions[i].virt_addr < index) {
				index = proc->regions[i].virt_addr;
			}
		}
	}

	if (index < (stack_size + unmapped_barrier)) {
		/* Not able to find space for stack */
		HalKernelPanic("Failed to find place for stack.");
	}

	return index - stack_size - unmapped_barrier;
}

HRESULT __nxapi sched_create_process(void *entry_point, uint32_t priority, uint32_t *pid_out)
{
	HRESULT hr = S_OK;

	uint32_t iflag = spinlock_acquire(&process_array_lock);
	if (process_count >= MAX_PROCESSES) {
		hr = E_FAIL;
		goto finally;
	}

	K_PROCESS *p = skheap_calloc(sizeof(K_PROCESS));

	/* Populate it */
	p->id 		= free_pid++;
	p->mode		= PROCESS_MODE_USER;
	p->priority = priority;
	p->page_dir = skheap_calloc_a(sizeof(K_VMM_PAGE_DIR));
	spinlock_create(&p->lock);

	p->page_dir_phys = skheap_get_phys_addr(p->page_dir);

	/* Create main thread */
	hr = sched_create_thread(p, entry_point, NULL);
	if (FAILED(hr)) goto finally;

	if (pid_out != NULL) {
		*pid_out = p->id;
	}

	/* Add to proc array */
	process_array[process_count++] = p;

finally:
	spinlock_release(&process_array_lock, iflag);
	return hr;
}

HRESULT __nxapi sched_get_current_proc(K_PROCESS **proc)
{
	HRESULT hr;
	uint32_t iflag = spinlock_acquire(&sched_state.lock);

	if (sched_state.current == NULL) {
		hr = E_FAIL;
	} else {
		*proc = sched_state.current->process;
		hr = S_OK;
	}

	spinlock_release(&sched_state.lock, iflag);
	return hr;
}

HRESULT	__nxapi	sched_get_current_pid(uint32_t *pid)
{
	HRESULT 	hr;
	K_PROCESS	*p;

	hr = sched_get_current_proc(&p);
	if (FAILED(hr)) return hr;

	*pid = p->id;
	return S_OK;
}

HRESULT	__nxapi	sched_get_current_tid(uint32_t *tid)
{
	HRESULT hr;
	uint32_t iflag = spinlock_acquire(&sched_state.lock);

	if (sched_state.current == NULL) {
		hr = E_FAIL;
	} else {
		*tid = sched_state.current->id;
		hr = S_OK;
	}

	spinlock_release(&sched_state.lock, iflag);
	return hr;
}

HRESULT __nxapi	sched_enter_process_addr_space(K_PROCESS *new_proc)
{
	/* Switch virtual address space to new_proc's one. Which is
	 * done by setting cr3 register.
	 */
	hal_flush_pagedir((void*)new_proc->page_dir_phys);

	return S_OK;
}

HRESULT __nxapi sched_find_proc(uint32_t pid, K_PROCESS **out)
{
	HRESULT hr = E_NOTFOUND;
	uint32_t ifl = spinlock_acquire(&process_array_lock);

	for (uint32_t i=0; i<process_count; i++) {
		if (process_array[i]->id == pid) {
			*out = process_array[i];

			hr = S_OK;
			break;
		}
	}

	spinlock_release(&process_array_lock, ifl);
	return hr;
}

HRESULT	__nxapi sched_setup_thread_stack(K_THREAD *t)
{
	/* We should always setup the thread's kernel stack.
	 *
	 * Even for user-space threads (which has 2 stacks: for CPL0 and CPL3), the entrance
	 * is done from the kernel stack. On the bottom of which we push user stack's SS/ESP
	 */
	uint32_t is_user_thread = ((K_PROCESS*)t->process)->mode == PROCESS_MODE_USER ? 1 : 0;
	HRESULT hr;

	/* Get stack physical location */
	uintptr_t stack_phys_location;

	hr = vmm_get_region_phys_addr(t->process, t->kernel_stack, &stack_phys_location);
	if (FAILED(hr)) return hr;

	/* At this point, we have allocated the stack somewhere in the physical memory, but it likely
	 * that this memory is not mapped, and thus we cannot access it. So we have to map it to
	 * temporary virtual address, then set it up, and of course then unmap it.
	 */
	uintptr_t stack_temp_ptr;
	K_PROCESS *current_proc = sched_state.current == NULL ? &kernel_proc : sched_state.current->process;

	hr = vmm_temp_map_region(current_proc, stack_phys_location, t->stack_size, &stack_temp_ptr);
	//k_printf("temp_map: hr=%x\n", hr);
	if (FAILED(hr)) return hr;

	uint32_t *stack = (uint32_t*)(stack_temp_ptr + t->stack_size);
	uintptr_t stack_ptr_offset = stack_temp_ptr - t->kernel_stack;

	assert(stack_temp_ptr > t->kernel_stack);

	/* Setup initial stack */
	/* Add pointer to sched_exit_thread(), which will be invoked by RET instruction of
	 * the threads proc. Also we have to add _t_ pointer to the stack, since it is the
	 * argument of sched_exit_thread().
	 */
	*--stack = (uintptr_t)t; 					//t
	*--stack = (uintptr_t)0; 					//EIP
	*--stack = (uintptr_t)sched_exit_thread; 	//sched_exit_thread

	/* If IRET causes privilege switch, it will also pop ESP and SS for the user stack,
	 * in addition to EIP, CS, EFLAGS.
	 *
	 * Intel IA32 manuals: 6.4.1: Call and Return Operation for Interrupt or Exception
	 * Handling Procedures (page: 156).
	 */
	if (is_user_thread) {
		*--stack = 0x20|3; 					//SS (SS = DS actually)
		*--stack = (uint32_t)(t->user_stack + t->stack_size); //ESP
	}

	*--stack = 0x00000202; 						 //EFLAGS
	*--stack = is_user_thread ? (0x18|3) : 0x08; //CS
	*--stack = t->eip; 							 //EIP

	/* Values, which get discarded by isr_handler */
	*--stack = 0; // Error code
	*--stack = 0; // ISR number

	/* PUSHA */
	*--stack = 0; // EAX
	*--stack = 0; // ECX
	*--stack = 0; // EDX
	*--stack = 0; // EBX

	uint32_t curr_esp = (uint32_t)stack - stack_ptr_offset; //remember that stack is temporary remapped
	*--stack = curr_esp + 4 * sizeof(uintptr_t); // ESP (initial value before EAX is pushed)

	*--stack = 0; // EBP
	*--stack = 0; // ESI
	*--stack = 0; // EDI

	/* Segments */
	*--stack = is_user_thread ? (0x20|3) : 0x10; //value for: DS, FS, ES, GS (see isr.asm)

	/* Update ESP */
	t->esp = (uintptr_t)stack - stack_ptr_offset;

	/* We finished writing to thread's kernel stack, so remove the temporary mapping */
	hr = vmm_unmap_region(current_proc, stack_temp_ptr, 1);
//	vga_printf("vmm_unmap_region(current_proc=%x, stack_temp_ptr=%x, 1); hr=%x\n", current_proc, stack_temp_ptr, hr);
	assert(SUCCEEDED(hr));

	/* This should be all fine and enough to just IRET to the new thread, later
	 * by the scheduler.
	 */
	return S_OK;
}

HRESULT	__nxapi sched_create_thread(K_PROCESS *proc, void *entry_point, uint32_t *thread_id)
{
	HRESULT 	hr;

	/* If process is not specified, use current process */
	if (proc == NULL) {
		hr = sched_get_current_proc(&proc);
		if (FAILED(hr)) return hr;
	}

	/* Assert thread limit is not reached */
	if (proc->thread_count >= MAX_THREADS) {
		return E_FAIL;
	}

	/* Lock process' lock */
	uint32_t iflag	= spinlock_acquire(&proc->lock);

	/* Setup thread descriptor struct */
	K_THREAD *t = skheap_malloc(sizeof(K_THREAD));
	proc->threads[proc->thread_count] = t;

	memset(t, 0, sizeof(K_THREAD));

	t->id		= proc->thread_id_counter++;
	t->process 	= proc;
	t->priority = proc->priority;
	t->quanta 	= DEFAULT_THREAD_QUANTA;
	t->state 	= THREAD_STATE_READY;
	t->eip 		= (uintptr_t)entry_point;
	t->running	= FALSE;

	/* Allocate kernel space stack, and map it to process virtual address space */
	uint32_t 	size = STACK_SIZE_KERNEL;
	uintptr_t	virt_addr = sched_find_proper_stack_location(proc, size);

	t->stack_size = size;

//	vga_printf("t->id=%d\n", t->id);
	hr = vmm_alloc_and_map(proc, virt_addr, size, USAGE_KERNELSTACK, ACCESS_READWRITE, 0);
	//vga_printf("virt_addr=%x, size=%x  hr=%x\n", virt_addr, size, hr);
	if (FAILED(hr)) {
		HalKernelPanic("Failed to allocate kernel space stack.");
	}

	/* Assign kernel stack pointer */
	t->kernel_stack = virt_addr;

	/* Allocate user space stack */
	if (proc->mode == PROCESS_MODE_USER) {
		uint32_t 	size = STACK_SIZE_USER;
		uintptr_t	virt_addr = sched_find_proper_stack_location(proc, size);

		hr = vmm_alloc_and_map(proc, virt_addr, size, USAGE_USERSTACK, ACCESS_READWRITE, 0);
		if (FAILED(hr)) {
			HalKernelPanic("Failed to allocate user space stack.");
		}

		/* Set stack pointer */
		t->user_stack = virt_addr;
	}

	/* Now setup the stack to it's initial state */
	hr = sched_setup_thread_stack(t);
	if (FAILED(hr)) {
		HalKernelPanic("Failed to setup new thread stack.");
	}

	/* Set thread_id output var and update thread count. */
	if (thread_id != NULL) {
		*thread_id = t->id;
	}
	proc->thread_count++;

	/* Unlock process */
	spinlock_release(&proc->lock, iflag);

	/* If this thread is created as part of the running process, the process
	 * should refresh it's page directory.
	 *
	 * Also current thread has to be added to running queue.
	 */
	iflag = spinlock_acquire(&sched_state.lock);

	if (sched_state.current != NULL && sched_state.current->process == proc) {
		sched_enter_process_addr_space(proc);
	}

//	K_THREAD *last = sched_state.run_queue;
//	if (last == NULL) {
//		sched_state.run_queue = t;
//	}else {
//		while (last->next != NULL) { last=last->next; }
//		last->next = t;
//		last->next->next = NULL;
//	}

	spinlock_release(&sched_state.lock, iflag);

	/* Add to thread queue */
	sched_add_thread_to_run_queue(t);

	return S_OK;
}

HRESULT __nxapi destroy_thread_struct(K_THREAD **thread)
{
	K_THREAD *t = *thread;

	/* Remove from process' thread list */
	K_PROCESS *p = t->process;
	int id = -1;

	for (unsigned int i=0; i<p->thread_count; i++) {
		if (p->threads[i] == t) {
			id = i;
			break;
		}
	}

	/* Thread not found? */
	if (id == -1) {
		HalKernelPanic("destroy_thread_struct(): thread not found.");
		return E_FAIL;
	}

	/* Delete thread from list */
	for (uint32_t i=id; i<p->thread_count-1; i++) {
		p->threads[i] = p->threads[i+1];
	}
	p->thread_count--;

	/* We don't commit unmapped address ranges, since we cant. They will
	 * be enforced on next address space switch.
	 */
	assert(t->kernel_stack != 0);
	HRESULT hr;

//	hr = vmm_unmap_region(p, t->kernel_stack, 0);
//	if (FAILED(hr)) return hr;
	//TODO: Mark kernel stack for later deallocation. Since if we
	//deallocate it now, IRQ0 will use it and it can't switch to next task.

	if (p->mode == PROCESS_MODE_USER) {
		hr = vmm_unmap_region(p, t->user_stack, 0);
		if (FAILED(hr)) return hr;
	}

	skheap_free(t);
	*thread = NULL;

	return S_OK;
}

HRESULT	__nxapi	sched_exit_thread(K_THREAD *t)
{
	HRESULT hr = E_NOTIMPL;
	uint32_t iflag = spinlock_acquire(&sched_state.lock);

	/* This routine is called by the thread which is requesting to be exited.
	 * So this implies that it is the current executed thread.
	 */
	if (sched_state.current != t) {
		hr = E_FAIL;
		HalKernelPanic("sched_exit_thread(): t is not current thread.");
		goto finally;
	}

	sched_state.current = NULL;
	hr = destroy_thread_struct(&t);

finally:
	spinlock_release(&sched_state.lock, iflag);

	/* Wait until we get preempted */
	/* TODO: OR -> yield cpu time to next thread */
	if (SUCCEEDED(hr)) {
		while (TRUE) { ; }
	}

	return hr;
}

HRESULT __nxapi sched_switch_to_thread(K_THREAD *t)
{
	/* Before modifying scheduler state at all, we should
	 * lock it.
	 */
	HRESULT hr = S_OK;
//	uint32_t iflag = spinlock_acquire(&sched_state.lock);

	/* Validate _t_ */
	if (t == NULL) {
		hr = E_POINTER;
		goto finally;
	}

	/* Check if we are asked to switch to current task */
	if (t == sched_state.current) {
		//hr = S_FALSE;
		HalKernelPanic("Trying to switch to same task?");
		goto finally;
	}

	//TODO: THIS DEFINATELY WON'T WORK!!
	/* If new thread is from different process, we have to make VAS switch. */
	if (sched_state.current == NULL || t->process != sched_state.current->process) {
//		hr = sched_enter_process_addr_space(t->process);
//		if (FAILED(hr)) goto finally;
	}

	/* Set current thread */
	sched_state.current = t;

	/* Release spinlock, but keep interrupts disabled, since
	 * they will be re-enabled by the IRET instruction.
	 */
//	spinlock_release(&sched_state.lock, iflag);

	if (((K_PROCESS*)t->process)->mode == PROCESS_MODE_USER) {
		/* For user-space threads we also need to update
		 * SS0 and ESP0 in the TSR.
		 *
		 * For kernel process (and thus kernel threads) the SS0/ESP0
		 * value in TSR is not used.
		 */
		//gdt_update_tss(0x10, t->esp); //!>!>!>!>!!<!<!<!<!<?!?!?!?!??!?! :?
		hal_cli();
		gdt_update_tss(0x10, t->kernel_stack + t->stack_size);
	}

	if (!t->running) {
		void *new_cr3 = ((K_PROCESS*)t->process)->page_dir_phys;

		/* Enter thread for first time.
		 */
		t->running = TRUE;
		asm volatile("         \
				mov		%3, %%cr3;		 \
				mov		%0, %%edi;       \
				mov 	%1, %%esp;       \
				mov 	%2, %%ebp;       \
				jmp 	*%%edi       "
					: : "r"((uintptr_t)return_to_irq_handler), "r"(t->esp), "r"(t->ebp), "r"(new_cr3)
		);
	}

finally:
//	spinlock_release(&sched_state.lock, iflag);
	return hr;
}

HRESULT __nxapi sched_add_thread_to_run_queue(K_THREAD *t)
{
	uint32_t iflag = spinlock_acquire(&sched_state.lock);

	K_THREAD *last = sched_state.run_queue;
	if (last == NULL) {
		sched_state.run_queue = t;
	}else {
		while (last->next != NULL) { last=last->next; }
		last->next = t;
		last->next->next = NULL;
	}

	spinlock_release(&sched_state.lock, iflag);
	return S_OK;
}

HRESULT __nxapi sched_update()
{
	/* If scheduling is disabled, return */
	if (!sched_enabled) {
		return S_FALSE;
	}

	/* Retrieve the current kernel stack and instruction pointers */
	uintptr_t esp, ebp, eip;
	asm volatile ("mov %%esp, %0" : "=r" (esp));
	asm volatile ("mov %%ebp, %0" : "=r" (ebp));
	eip = HalRetrieveEIP(); //this actually returns pointer to the _next_ instruction's addr (probably assignment)

//	vga_printf("esp=%x ebp=%x eip=%x return_irq_ptr=%x\n", esp, ebp, eip, return_to_irq_handler);

	/* HERE IT'S PROPER TO CITE PonyOS's author:
	 * 			"Kernels are magic!"
	 */
	if (eip == 0x12345) {
		/* Just switched? So now we are residing in the new task.
		 * Pretty weird.
		 */
		return S_OK;
	}

	if (sched_state.run_queue == NULL) {
		/* No task to switch to */
		HalKernelPanic("No tasks in run queue.\n");
		return S_OK;
	}

	/* Save current task state */
	if (sched_state.current != NULL) {
		sched_state.current->eip = eip;
		sched_state.current->esp = esp;
		sched_state.current->ebp = ebp;

		/* Assign current thread as last in run queue */
		K_THREAD *last = sched_state.run_queue;
		while (last->next != NULL) last = last->next;

		/* Append current thread at end of list */
		last->next = (K_THREAD*)sched_state.current;
		last->next->next = NULL;
	}

	/* Doing Round Robin (at this point). Extract first thread from list.
	 */
	K_THREAD *new = sched_state.run_queue;
	sched_state.run_queue = sched_state.run_queue->next;
	//assert(new != NULL);

	/* Switch to new thread */
	HRESULT hr = sched_switch_to_thread(new);

	/* NOTE (very important): When we assign new->eip to %ecx, GCC later clobbers
	 * it and assigns a bogus value to %ebp. So as a solution I made it used %ebx
	 * instead of %ecx. Now it seems fine.
	 */
	if (SUCCEEDED(hr)) {
		asm volatile("         \
			mov		%3, %%cr3;		 \
			mov		%0, %%edi;       \
			mov 	%1, %%esp;       \
			mov 	%2, %%ebp;       \
			mov 	$0x12345, %%eax; \
			jmp 	*%%edi       "
				: : "r"(new->eip), "r"(new->esp), "r"(new->ebp), "r"(((K_PROCESS*)new->process)->page_dir_phys)
		);

	}

	HalKernelPanic("sched_update(): failed...");
	return S_OK;
}

/*
 * Creates the initial kernel process and thread.
 */
HRESULT	__nxapi sched_create_initial_proc()
{
	K_PROCESS *p = &kernel_proc;

	/* Initialize process descriptor */
	memset(p, 0, sizeof(p));

	/* Set process name */
	strcpy(p->name, "init");

	/* Populate it */
	p->id 		= free_pid++;
	p->mode		= PROCESS_MODE_KERNEL;
	p->priority = PROCESS_PRIORITY_NORMAL;
	p->page_dir = skheap_calloc_a(sizeof(K_VMM_PAGE_DIR));

	p->page_dir_phys = skheap_get_phys_addr(p->page_dir);

	/* Make sure page dir is properly aligned */
	assert(((uintptr_t)p->page_dir % 0x1000) == 0);

	/* Create process spinlock */
	spinlock_create(&p->lock);

	/* Map initial memory regions */
	HRESULT hr = vmm_map_region(p, 0x00000000, 0xC0000000, 4*1024*1024, USAGE_KERNEL, ACCESS_READWRITE, FALSE);
	if (FAILED(hr)) {
		HalKernelPanic("Failed to map region [0x0..0x00100000] to [0xC0000000..0xC0100000].");
	}

	/* Add to process list */
	uint32_t if_flag = spinlock_acquire(&process_array_lock);
	process_array[process_count++] = p;
	spinlock_release(&process_array_lock, if_flag);

	return S_OK;
}

static VOID __cdecl timer_irq_handler(K_REGISTERS regs)
{
	UNUSED_ARG(regs);

	/* First call PIT's irq handler */
	timer_enter_irq_handler(regs);

	/* Update scheduler */
	sched_update();
}

/* Same as above, but doesn't call timer */
static VOID __cdecl scheduler_isr_handler(K_REGISTERS regs)
{
	UNUSED_ARG(regs);

	/* Update scheduler */
	sched_update();
}

HRESULT __nxapi sched_initialize(void *kernel_thread_entry)
{
	/* Initialize process array */
	process_count = 0;
	spinlock_create(&process_array_lock);

	/* Initialize scheduler state */
	memset(&sched_state, 0, sizeof(sched_state));
	spinlock_create(&sched_state.lock);

	/* Create process descriptor for main kernel process */
	HRESULT hr = sched_create_initial_proc();
	if (FAILED(hr)) HalKernelPanic("Failed to create kernel process descriptor.");

//	/* Create few test threads */
	sched_enter_process_addr_space(&kernel_proc); //not needed, this is done in scheduler

	/* Create main thred */
	hr = sched_create_thread(&kernel_proc, kernel_thread_entry, NULL);
	if (FAILED(hr)) {
		HalKernelPanic("Failed to create kernel main thread.");
	}

	/* Create two test threads */
//	sched_create_thread(&kernel_proc, kernel_task2, NULL);
//	sched_create_thread(&kernel_proc, kernel_task3, NULL);
	sched_create_thread(&kernel_proc, kernel_idle_task, NULL);

//	sched_add_thread_to_run_queue(kernel_proc.threads[0]);
//	sched_add_thread_to_run_queue(kernel_proc.threads[1]);
//	sched_add_thread_to_run_queue(kernel_proc.threads[2]);

	/* Set initialized flag */
	initialized = TRUE;
	sched_enabled = TRUE;

	/* Attach PIT handler */
	register_isr_callback(IRQ0_INTID, timer_irq_handler, NULL);
	register_isr_callback(RESCHEDULE_INTID, scheduler_isr_handler, NULL);

	/* Wait until we get preempted in favor of the next thread. Also we'll never
	 * return back here anymore.
	 */
	while (TRUE) { ; }
	HalKernelPanic("We should not return here...");
	return S_OK;
}

HRESULT __nxapi sched_update_sw()
{
	if(!initialized)
		return E_FAIL;

	asm volatile("int $0x81");
	return S_OK;
}

HRESULT __nxapi sched_yield()
{
	return sched_update_sw();
}

uint32_t __nxapi sched_get_process_count(void)
{
	uint32_t ifl = spinlock_acquire(&process_array_lock);
	uint32_t res = process_count;
	spinlock_release(&process_array_lock, ifl);

	return res;
}

HRESULT __nxapi sched_get_process_by_id(uint32_t id, K_PROCESS **proc)
{
	HRESULT hr = E_NOTFOUND;
	uint32_t ifl = spinlock_acquire(&process_array_lock);

	if (id < process_count) {
		*proc = process_array[id];
		hr = S_OK;
	}

	spinlock_release(&process_array_lock, ifl);
	return hr;
}

HRESULT __nxapi sched_enable(uint32_t bool)
{
	atomic_update_int(&sched_enabled, bool);
	return S_OK;
}
