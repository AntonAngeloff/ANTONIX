/*
 * timer.c
 *
 *  Created on: 16.11.2015 ã.
 *      Author: Anton Angelov
 */

#include <timer.h>
#include <stddef.h>
#include <hal.h>
#include <desctables.h>
#include <scheduler.h>

uint64_t __timer_ticks;
DWORD __timer_rate;

static VOID __cdecl timer_irq_handler(K_REGISTERS regs)
{
	UNUSED_ARG(regs);

	/* Increment timer tick counter */
	__timer_ticks = (__timer_ticks + 1) % 0xFFFFFFFFFFFFFFFF;
}

VOID __cdecl timer_enter_irq_handler(K_REGISTERS regs)
{
	timer_irq_handler(regs);
}

VOID __nxapi timer_initialize(DWORD rate)
{
	DWORD divisor = 1193180 / rate;

	//irq_enable(0);
	register_isr_callback(IRQ0_INTID, timer_irq_handler, NULL); //PROBABLY REMOVE THIS? IRQ0 is used by scheduler.

	/* Initialize PIT */
	WRITE_PORT_UCHAR(0x43, 0x36);

	WRITE_PORT_UCHAR(0x40, divisor);
	WRITE_PORT_UCHAR(0x40, divisor >> 8);

	/* Init global vars */
	__timer_rate = rate;
	__timer_ticks = 0;
}

VOID __nxapi timer_uninitialize()
{
	irq_disable(0);
	unregister_isr_callback(IRQ0_INTID);

	__timer_ticks = 0;
}

QWORD __nxapi timer_gettickcount()
{
	/* TODO: Use 64bit var */
	return (uint32_t)(1000 * __timer_ticks) / (__timer_rate);
}

VOID __nxapi timer_sleep(DWORD dwMilliseconds)
{
	DWORD end = timer_gettickcount() + dwMilliseconds;

	while(TRUE) {
		DWORD now = timer_gettickcount();

		if (end >= now) {
			break;
		}

		//TODO: use NOP instruction or something similar (for lower power consumption)
		if (end - now > 5) {
			sched_yield();
		}
	}
}
