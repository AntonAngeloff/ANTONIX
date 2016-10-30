/*
 * hal.h
 *
 *  Created on: 10.11.2015 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_HAL_H_
#define INCLUDE_HAL_H_

/**
 * Define debug-mode printing macro
 */
#define debug
#ifdef debug
#define DPRINT(s) \
	HalDisplayString("DPRINT("); \
	HalDisplayString(__FILE__); \
	HalDisplayString("): "); \
	HalDisplayString(s);
#else
#define DPRINT(s)
#endif

#include "types.h"

void __nxapi HalEnableInterrupt(void);
void __nxapi HalDisableInterrupt(void);
void __nxapi HalDisplayString(PCHAR text);
void __nxapi HalKernelPanic(PCHAR text);
void __nxapi WRITE_PORT_UCHAR(WORD port, BYTE data);
void __nxapi WRITE_PORT_USHORT(WORD port, WORD data);
void __nxapi WRITE_PORT_ULONG(WORD port, DWORD data);
BYTE __nxapi READ_PORT_UCHAR(WORD port);
WORD __nxapi READ_PORT_USHORT(WORD port);
DWORD __nxapi READ_PORT_ULONG(WORD port);
uint32_t __nxapi HalRetrieveEIP(void);
uint32_t __nxapi HalRetrieveESP(void);
void __nxapi HalEnablePaging(void *page_dir);
void __nxapi HalInvalidatePage(void *virt_addr);
uint_ptr_t __nxapi HalGetFaultingAddr();

void __nxapi	hal_tss_flush(uint32_t gdt_index);

/* Atomic operations (perhaps we can move those to atomic.h and atomic.s */
int32_t __nxapi	atomic_update_int(uint32_t *target, uint32_t new_value);

void __nxapi hal_cli();
void __nxapi hal_sti();
void __nxapi hal_flush_pagedir(void *page_dir);
uint32_t __nxapi	hal_get_eflags(void);

/* Enters userspace (ring3) */
void __nxapi hal_enter_userspace(uintptr_t location, uintptr_t stack);

void __nxapi HalLoadGDT(K_GDTPOINTER* pGDTPointer);
void __nxapi HalLoadIDT(K_IDTPOINTER* pIDTPointer);

#endif /* INCLUDE_HAL_H_ */
