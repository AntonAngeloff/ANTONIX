/*
 * kdbg.h
 *
 *	ANTONIX kernel debugger.
 *
 *  Created on: 4.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_KDBG_H_
#define INCLUDE_KDBG_H_

#include <types.h>

HRESULT __nxapi kdbg_init();
HRESULT __nxapi kdbg_fini();
void __nxapi dbg_print(char *string);
void __nxapi dbg_printf(char *fmt, ...);
void __nxapi dbg_break();

#endif /* INCLUDE_KDBG_H_ */
