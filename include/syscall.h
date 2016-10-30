/*
 * syscall.h
 *
 *  Created on: 1.08.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_SYSCALL_H_
#define INCLUDE_SYSCALL_H_

#include "types.h"

#define SYSCALL_ID_FOPEN	0x02
#define SYSCALL_ID_FCLOSE	0x03
#define SYSCALL_ID_MUTEX	0x04

HRESULT syscall_init();

/**
 * Tests the SYSCALL subsystem.
 */
void __nxapi sys_test(K_REGISTERS *regs);

/**
 * Mutex syscall - it contains 4 sub-routines denoted by EBX as follows:
 * 		0 - create mutex
 * 		1 - lock mutex
 * 		2 - unlock mutex
 * 		3 - destroy mutex
 *
 * 	@param regs->ebx ID of the subroutine.
 * 	@param regs->edx Pointer to K_MUTEX structure, which is argument to the subroutine.
 * 	@return Returns S_OK on success, E_FAIL otherwise.
 */
void __nxapi sys_mutex(K_REGISTERS *regs);

/**
 * Opens a FILE stream.
 *
 * @param regs->ebx Pointer to filename/url null-terminated string.
 * @param regs->edx Flags
 * @return On success regs->eax holds pointer to FILE stream. On failure, regs->eax is 0.
 */
void __nxapi sys_fopen(K_REGISTERS *regs);

/**
 * Closes a FILE stream.
 *
 * @param regs->ebx Pointer to FILE stream.
 * @returns S_OK on success.
 */
void __nxapi sys_fclose(K_REGISTERS *regs);

void __nxapi sys_fwrite(K_REGISTERS *regs);
void __nxapi sys_exit(K_REGISTERS *regs);

#endif /* INCLUDE_SYSCALL_H_ */
