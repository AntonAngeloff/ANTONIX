/*
 * isr.asm
 *
 *  Created on: 12.11.2015 ã.
 *      Author: Anton
 */

.intel_syntax noprefix

/*
 * Define macros for building ISR routines with templates
 * for ISRs without error code (8, 10, 11, 12, 13, 14) and
 * such with error code
 */
.macro ISR_NOERRCODE id
.global _isr_handler_\id
_isr_handler_\id :
    cli
    push 0 //push fake error code
    push \id
    jmp isr_common_gateway
.endm

.macro ISR_ERRCODE id
.global _isr_handler_\id
_isr_handler_\id :
    cli
    push \id
    jmp isr_common_gateway
.endm

/*
 * Define real ISR routines
 */
ISR_NOERRCODE	0
ISR_NOERRCODE	1
ISR_NOERRCODE	2
ISR_NOERRCODE	3
ISR_NOERRCODE	4
ISR_NOERRCODE	5
ISR_NOERRCODE	6
ISR_NOERRCODE	7
ISR_ERRCODE		8
ISR_NOERRCODE	9
ISR_ERRCODE		10
ISR_ERRCODE		11
ISR_ERRCODE		12
ISR_ERRCODE		13
ISR_ERRCODE		14
ISR_NOERRCODE	15
ISR_NOERRCODE	16
ISR_NOERRCODE	17
ISR_NOERRCODE	18
ISR_NOERRCODE	19
ISR_NOERRCODE	20
ISR_NOERRCODE	21
ISR_NOERRCODE	22
ISR_NOERRCODE	23
ISR_NOERRCODE	24
ISR_NOERRCODE	25
ISR_NOERRCODE	26
ISR_NOERRCODE	27
ISR_NOERRCODE	28
ISR_NOERRCODE	29
ISR_NOERRCODE	30
ISR_NOERRCODE	31

ISR_NOERRCODE	128
ISR_NOERRCODE	129

//All ISR's pass through this routine
isr_common_gateway:
   pusha                    // Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax

   xor eax, eax
   mov ax, ds               // Lower 16-bits of eax = ds.
   push eax                 // save the data segment descriptor

   xor eax, eax
   mov eax, 0x10 			//load the kernel data segment descriptor
   mov ds, eax
   mov es, eax
   mov fs, eax
   mov gs, eax

   call _isr_handler_gateway

   pop eax        // reload the original data segment descriptor

//	mov eax, 0x10
   mov ds, eax
//   hlt //STOP! HAMMER TIME
   mov es, eax
   mov fs, eax
   mov gs, eax

   popa                     // Pops edi,esi,ebp...
   add esp, 8     // Cleans up the pushed error code and pushed ISR number
   sti
   iret           // pops 5 things at once: CS, EIP, EFLAGS, SS, and ESP

/**
 * As we did for isr, we will create similar macros and common gateway
 * function for IRQs
 */
.macro IRQ_HANDLER id intid
.global _irq_handler_\id
_irq_handler_\id :
    cli
    push 0
    push \intid
    jmp irq_common_gateway
.endm

/**
 * Now declare real instances of the irc handler routines,
 * using the macro
 */
IRQ_HANDLER  0, 32
IRQ_HANDLER  1, 33
IRQ_HANDLER  2, 34
IRQ_HANDLER  3, 35
IRQ_HANDLER  4, 36
IRQ_HANDLER  5, 37
IRQ_HANDLER  6, 38
IRQ_HANDLER  7, 39
IRQ_HANDLER  8, 40
IRQ_HANDLER  9, 41
IRQ_HANDLER 10, 42
IRQ_HANDLER 11, 43
IRQ_HANDLER 12, 44
IRQ_HANDLER 13, 45
IRQ_HANDLER 14, 46
IRQ_HANDLER 15, 47

/*
 * All IRQ calls will pass through this routine
 * which calls the C function irc_handler_gateway()
 */
 .global _return_to_irq_handler
irq_common_gateway:
  pusha                    // Pushes: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI

  xor eax, eax
  mov ax, ds               // Lower 16-bits of eax = ds.
  push eax                 // save the data segment descriptor

  xor eax, eax
  mov eax, 0x10  // load the kernel data segment descriptor
  mov ds, eax
  mov es, eax
  mov fs, eax
  mov gs, eax

  call _irq_handler_gateway

# This laber is used by the task switch routine to return here for newly created threads.
_return_to_irq_handler:
  pop ebx        // reload the original data segment descriptor
  mov ds, ebx
  mov es, ebx
  mov fs, ebx
  mov gs, ebx

  popa                     // Pops: EDI, ESI, EBP, ESP, EDX, ECX and EAX.
  add esp, 8     // Cleans up the pushed error code and pushed ISR number
  sti
  iret           // pops 5 things at once: CS, EIP, EFLAGS, SS, and ESP
