/*
 * hal.asm
 *
 *  Created on: 7.11.2015 ã.
 *      Author: Anton
 */
#Enable intel syntax
.intel_syntax noprefix

.data
hello:
	.string "no data yet"

.section .text
/*
 * Routine: void __ntapi _HalEnableInterrupt(void);
 * Input: No parameters
 * Output: No
*/
.globl _HalEnableInterrupt
_HalEnableInterrupt:
	sti
	ret

/*
 * Routine - HalDisableInterrupt(void); cdecl;
 * Input: No parameters
 * Output: No
*/
.globl _HalDisableInterrupt
_HalDisableInterrupt:
	cli
	ret

.global _HalGetInterruptVector
_HalGetInterruptVector:
	//TODO..
	ret

.extern _vga_print

// HalDisplayString - displays a string to the terminal (cdecl)
// stack:
//        [esp + 4] pointer to null-terminated string
//        [esp    ] return addr
.global _HalDisplayString
_HalDisplayString:
	jmp _k_print

/**
 * Routine: HalPanic (TODO: convert to stdcall)
 * Enters the kernel in panic mode, writes a text to the screen and halts the CPU
 */
.global _HalKernelPanic
_HalKernelPanic:
	// Parameter (the string) is already loaded in stack
	push DWORD PTR [esp + 4]
	call _HalDisplayString
	pop eax

loop:
	hlt
	jmp loop

/*
 * Routine - _HalOutb; TODO: refactor it to stdcall convention
 * Input: 1 DWORD and 1 byte
 * Output: No
*/
.global _WRITE_PORT_UCHAR
_WRITE_PORT_UCHAR:
// _HalOutb - send a byte to an IO port (cdecl)
// stack: [esp + 8] data byte (BYTE)
//        [esp + 4] cmd byte (WORD)
//        [esp    ] return addr
	mov al, [esp + 8]       // move data byte
	mov dx, [esp + 4]       // move port byte
	out dx, al
	ret

/*
 * Routine - _HalInb; TODO: refactor it to stdcall convention
 * Input: 1 DWORD
 * Output: 1 BYTE
*/
.global _READ_PORT_UCHAR
_READ_PORT_UCHAR:
	mov dx,	[esp + 4]
	inb al,	dx
	ret

/*
 * Routine - READ_PORT_USHORT
 * Input: 1 DWORD
 * Output: 1 WORD
*/
.global _READ_PORT_USHORT
_READ_PORT_USHORT:
	mov dx,	[esp + 4]
	in 	ax,	dx
	ret

/*
 * Routine - READ_PORT_ULONG
 * Input: 1 DWORD
 * Output: 1 DWORD
*/
.global _READ_PORT_ULONG
_READ_PORT_ULONG:
	mov dx,	[esp + 4]
	in 	eax,dx
	ret

/**
 * Routine: WRITE_PORT_USHORT - writes 16bit uint to IO port
 */
.global _WRITE_PORT_USHORT
_WRITE_PORT_USHORT:
	mov ax, [esp + 8]       // move data byte
	mov dx, [esp + 4]       // move port byte
	out dx, ax
	ret

/**
 * Routine: WRITE_PORT_ULONG - writes 32bit uint to IO port
 */
.global _WRITE_PORT_ULONG
_WRITE_PORT_ULONG:
	mov eax, 	[esp + 8]       // move data byte
	mov dx, 	[esp + 4]       // move port byte
	out dx, 	eax
	ret

.global _HalLoadIDT //(cdecl)
_HalLoadIDT:
	mov eax, [esp + 4]
	lidt [eax] //Pointer to IDT is stored in the first (and only) arg
	ret

/**
 * Sets new GDT and reloads the segment selectors
 */
.global _HalLoadGDT //(cdecl)
_HalLoadGDT:
	mov eax, [esp + 4]
	lgdt [eax]

	//0x10 is the offset in the GDT to our data segment
	mov eax, 0x10

	mov ds, eax
	mov es, eax
	mov fs, eax
	mov gs, eax
	mov ss, eax

	//0x08 is the offset to our code segment: Far jump!
	jmp 0x08:.reload_cs
.reload_cs:
	ret

.global _hal_cli
_hal_cli :
	cli
	ret

.global _hal_sti
_hal_sti :
	sti
	ret

.global _HalTestInt
_HalTestInt:
	int 0x3
	ret

subroutine:
	mov		eax,	[esp]
	ret

.global _HalRetrieveEIP
_HalRetrieveEIP:
#	call	subroutine
#	ret
	pop		eax
	jmp		eax

.global _HalRetrieveESP
_HalRetrieveESP:
	mov	eax,	esp
	ret

.global _HalInvalidatePage
_HalInvalidatePage:
	mov 	eax, [esp + 4]
	invlpg	[eax]
	ret

.global _HalEnablePaging
_HalEnablePaging:
	cli

	# Load page directory address to cr3
	mov	eax, [esp + 4]
	mov cr3, eax

	# Enable paging through cr0
	mov eax, cr0
	or	eax, 0x80000000
	mov cr0, eax

	# Disable PSE
	mov	eax, cr4
	and	eax, 0xFFFFFFEF
	mov	cr4, eax

	sti
	ret

#
# Same as above, just doesn't disable PSE, since it is supposed
# to be disabled.
.global _hal_flush_pagedir
_hal_flush_pagedir:
	cli

	# Load page directory address to cr3
	mov	eax, [esp + 4]
	mov cr3, eax

	# Enable paging through cr0
	mov eax, cr0
	or	eax, 0x80000000
	mov cr0, eax

	sti
	ret

.global _HalGetFaultingAddr
_HalGetFaultingAddr:
	mov eax, cr2
	ret

.global _hal_tss_flush
_hal_tss_flush:
	mov eax, [esp + 4]

	# Lift the two most significant bits, to allow CPL=3 to access TSS
	or	ax,  2

	# Load task register
	ltr ax

	ret

#
# Updates dword [[esp+4]] with value of [esp+8] and
# returns old value in eax.
#
.global _atomic_update_int
_atomic_update_int:
	mov	eax, [esp + 8]
	mov edx, [esp + 4]

	lock
	xchg eax, [edx]

	ret

.global _hal_get_eflags
_hal_get_eflags:
	pushf
	pop eax

	ret

.set MAGIC, 0xB001B001

.global _hal_enter_userspace
_hal_enter_userspace:
    push	ebp
    mov		ebp, esp
    mov		edx, [ebp + 0xC]
    mov		esp, edx
    push	MAGIC

    /* Segement selector */
    mov		ax, 0x23

    /* Save segement registers */
    mov		ds, eax
    mov		es, eax
    mov		fs, eax
    mov		gs, eax
    /* Stack segment is handled by iret */

    /* Store stack address in %eax */
    mov		eax, esp

    /* Data segmenet with bottom 2 bits set for ring3 */
    push	0x23

    /* Push the stack address */
    push	eax

    /* Push flags and fix interrupt flag */
    pushf
    pop 	eax

    /* Request ring3 */
    or		eax, 0x200
    push 	eax
    push 	0x1B

    /* Push entry point */
    push	[ebp + 0x8]

    iret
    pop		ebp

    ret
