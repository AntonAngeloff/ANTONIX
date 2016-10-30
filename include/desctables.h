/*
 * desctables.h
 *
 *  Created on: 11.11.2015 ã.
 *      Author: Anton
 */

#ifndef INCLUDE_DESCTABLES_H_
#define INCLUDE_DESCTABLES_H_

#include <types.h>

/**
 * Define interrupt id's for IRQs
 */
#define IRQ0_INTID 	32
#define IRQ1_INTID 	33
#define IRQ2_INTID 	34
#define IRQ3_INTID 	35
#define IRQ4_INTID 	36
#define IRQ5_INTID 	37
#define IRQ6_INTID 	38
#define IRQ7_INTID 	39
#define IRQ8_INTID 	40
#define IRQ9_INTID 	41
#define IRQ10_INTID	42
#define IRQ11_INTID	43
#define IRQ12_INTID	44
#define IRQ13_INTID	45
#define IRQ14_INTID	46
#define IRQ15_INTID 47

#define RESCHEDULE_INTID	0x81

/**
 * Define PIC's port id's
 */
#define PORT_PIC1		0x20		/* IO base address for master PIC */
#define PORT_PIC2		0xA0		/* IO base address for slave PIC */
#define PORT_PIC1_CMD	PORT_PIC1
#define PORT_PIC1_DATA	(PORT_PIC1+1)
#define PORT_PIC2_CMD	PORT_PIC2
#define PORT_PIC2_DATA	(PORT_PIC2+1)

/**
 * Define Initialization Control Words (ICW) for PIC.
 * These are used as parameters to be sent via in/out.
 */
#define ICW1_ICW4	0x01		/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04	/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */

#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08	/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C	/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */

/**
 * Number of maximum callback routines which can be attached
 * to individual interrupt number.
 */
#define MAX_ISR_ROUTINES 8

/**
 * End-of-interrupt command
 */
#define PIC_EOI	0x20

typedef struct tss_entry {
	uint32_t prevTss;
	uint32_t esp0;
	uint32_t ss0;
	uint32_t esp1;
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	uint32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldt;
	uint16_t trap;
	uint16_t iomap;
} __packed K_TSS_ENTRY;

/**
 * Define interrupt handler delegated function
 */
typedef void __nxapi (*K_INTERRUPTCALLBACK)(K_REGISTERS regs);

void __nxapi gdt_initialize();
void __nxapi gdt_update_tss(uint16_t ss_kernel, uint32_t esp_kernel);
void __nxapi idt_initialize();

HRESULT __nxapi irq_enable(BYTE irq_id);
HRESULT __nxapi irq_disable(BYTE irq_id);
HRESULT __nxapi irq_disable_all();
HRESULT __nxapi register_isr_callback(DWORD int_id, K_INTERRUPTCALLBACK pCB, void *data_ptr);
HRESULT __nxapi unregister_isr_callback(DWORD int_id);

uint32_t __nxapi irq_to_intid(uint32_t irq_id);
uint32_t __nxapi intid_to_irq(uint32_t int_id);

void __nxapi exception_handler_gpf(K_REGISTERS regs);

#endif /* INCLUDE_DESCTABLES_H_ */
