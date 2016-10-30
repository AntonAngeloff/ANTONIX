/*
 * syscall.c
 *
 *  Created on: 1.08.2016 ã.
 *      Author: Admin
 */
#include "syscall.h"
#include "desctables.h"
#include "vga.h"
#include <stddef.h>
#include "syncobjs.h"
#include "kstdio.h"


typedef void __nxapi (*syscall_handler_t)(K_REGISTERS *r);


/* Array of syscall handlers */
static syscall_handler_t syscalls[] =
{
	sys_test,
	sys_exit,
	sys_fopen,
	sys_fclose,
	sys_fwrite,
	//todo: fread
	sys_mutex,
	NULL
};

uint32_t syscall_cnt = 0;

void __nxapi sys_test(K_REGISTERS *regs)
{
	UNUSED_ARG(regs);

	vga_printf("SYSCALL INVOKED!\n");
	return;
}

void __nxapi sys_mutex(K_REGISTERS *regs)
{
	switch(regs->ebx) {
	case 0:
		mutex_create((K_MUTEX*)regs->edx);
		break;
	case 1:
		mutex_lock((K_MUTEX*)regs->edx);
		break;
	case 2:
		mutex_unlock((K_MUTEX*)regs->edx);
		break;
	case 3:
		mutex_destroy((K_MUTEX*)regs->edx);
		break;
	default:
		regs->eax = E_FAIL;
		return;
	}

	regs->eax = S_OK;
}

void __nxapi sys_fopen(K_REGISTERS *regs)
{
	K_STREAM *s;
	HRESULT hr;

	hr = k_fopen((char*)regs->ebx, regs->edx, &s);
	if (FAILED(hr)) {
		regs->eax = 0;
		return;
	}

	regs->eax = (uintptr_t)s;
}

void __nxapi sys_fclose(K_REGISTERS *regs)
{
	K_STREAM *s = (K_STREAM*)regs->ebx;
	regs-> eax = k_fclose(&s);
}

static VOID __cdecl syscall_handler(K_REGISTERS regs)
{
	if (regs.eax >= syscall_cnt) {
		/* Invalid syscall id */
		return;
	}

	/* Invoke syscall */
	syscalls[regs.eax](&regs);
}

HRESULT syscall_init()
{
	while (syscalls[syscall_cnt] != NULL) {
		syscall_cnt++;
	}

	/* Attach syscall interrupt handler routine. Syscalls will be invoked
	 * by int 0x80.
	 */
	register_isr_callback(0x80, syscall_handler, NULL);
	return S_OK;
}

void __nxapi sys_fwrite(K_REGISTERS *regs)
{
	switch (regs->ebx) {
	case 1: {
		/* Write to STDOUT */
		char *msg = (char*)regs->ecx; //for now we ignore message len (edx).
		vga_print(msg);
		break;
	}

	default:
		HalKernelPanic("sys_fwrite(): Not implemented.");
	}
}

void __nxapi sys_exit(K_REGISTERS *regs)
{
	/* Terminate current process */
	UNUSED_ARG(regs);
	//TODO: implement
}
