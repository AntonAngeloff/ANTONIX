/*
 * timer.h
 *
 *  Created on: 16.11.2015 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_TIMER_H_
#define INCLUDE_TIMER_H_

#include <types.h>

VOID __nxapi timer_initialize(DWORD rate);
VOID __nxapi timer_uninitialize();
VOID __nxapi timer_sleep(DWORD dwMilliseconds);
VOID __cdecl timer_enter_irq_handler(K_REGISTERS regs);
QWORD __nxapi timer_gettickcount();

#endif /* INCLUDE_TIMER_H_ */
