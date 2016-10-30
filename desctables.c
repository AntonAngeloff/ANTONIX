/*
 * desctables.c
 *
 *	This file contains routines for
 *		- managing GDT and IDT tables
 *		- re-mapping PIC
 *		- setting custom interrupt handlers for arbitrary interrupt id's
 *
 *	The other part of GDT\IDT-handling routines is located in /isr.asm
 *
 *  Created on: 11.11.2015 ã.
 *      Author: Anton Angelov
 */

#include <desctables.h>
#include <stdint.h>
#include <string.h>
#include <hal.h>
#include <kstdio.h>

#define INVALID_IRQ_LINE	0xFFFFFFFF

/**
 * Declare Global Description Table vars
 */
K_GDTENTRY 		gdt_entries[5];
K_GDTPOINTER 	gdt_ptr;

/**
 * Declare Interrupt Description Table vars
 */
K_IDTENTRY		idt_entries[256];
K_IDTPOINTER	idt_ptr;

/**
 * TSS entry (as we see, its a global var)
 */
K_TSS_ENTRY		tss_entry;

/**
 * This structure describes a list of callback routines assigned to a
 * particular interrupt with their respective data pointers.
 *
 * There is a limit of MAX_ISR_ROUTINES which can be attached to an
 * interrupt id.
 */
typedef struct K_ISR_CALLBACK_DESC K_ISR_CALLBACK_DESC;
struct K_ISR_CALLBACK_DESC {
	K_INTERRUPTCALLBACK isr_list[MAX_ISR_ROUTINES];
	void	*isr_data[MAX_ISR_ROUTINES];
	uint8_t	isr_count;
};

/**
 * We have table with dynamically changeable callbacks used for handling interrupts.
 * Supporting callback lists instead of single callback is beneficial, since
 * we can avoid IRQ chaining.
 *
 * Also every ISR routine is allowed to be associated with an arbitrary pointer (data
 * pointer), pointing to a data structure relevant for the routine. The "data pointer"
 * is provided back to the callback on invocation as argument.
 */
K_INTERRUPTCALLBACK isr_callbacks[256]; //TODO: migrate to array of K_ISR_CALLBACK_DESC and modify register_isr* API

// Set the value of one GDT entry.
static void gdt_set_gate(int32_t num, DWORD base, DWORD limit, BYTE access, BYTE gran)
{
   gdt_entries[num].base_low    = (base & 0xFFFF);
   gdt_entries[num].base_middle = (base >> 16) & 0xFF;
   gdt_entries[num].base_high   = (base >> 24) & 0xFF;

   gdt_entries[num].limit_low   = (limit & 0xFFFF);
   gdt_entries[num].granularity = (limit >> 16) & 0x0F;

   gdt_entries[num].granularity |= gran & 0xF0;
   gdt_entries[num].access      = access;
}

/**
 * Declare ISR handlers as external symbols
 */
extern void isr_handler_0();
extern void isr_handler_1();
extern void isr_handler_2();
extern void isr_handler_3();
extern void isr_handler_4();
extern void isr_handler_5();
extern void isr_handler_6();
extern void isr_handler_7();
extern void isr_handler_8();
extern void isr_handler_9();
extern void isr_handler_10();
extern void isr_handler_11();
extern void isr_handler_12();
extern void isr_handler_13();
extern void isr_handler_14();
extern void isr_handler_15();
extern void isr_handler_16();
extern void isr_handler_17();
extern void isr_handler_18();
extern void isr_handler_19();
extern void isr_handler_20();
extern void isr_handler_21();
extern void isr_handler_22();
extern void isr_handler_23();
extern void isr_handler_24();
extern void isr_handler_25();
extern void isr_handler_26();
extern void isr_handler_27();
extern void isr_handler_28();
extern void isr_handler_29();
extern void isr_handler_30();
extern void isr_handler_31();
extern void isr_handler_128();
extern void isr_handler_129();

/*
 * Declare 16 IRQ handler external symbols
 */
extern void irq_handler_0();
extern void irq_handler_1();
extern void irq_handler_2();
extern void irq_handler_3();
extern void irq_handler_4();
extern void irq_handler_5();
extern void irq_handler_6();
extern void irq_handler_7();
extern void irq_handler_8();
extern void irq_handler_9();
extern void irq_handler_10();
extern void irq_handler_11();
extern void irq_handler_12();
extern void irq_handler_13();
extern void irq_handler_14();
extern void irq_handler_15();

static void idt_set_gate(BYTE num, DWORD base, WORD sel, BYTE flags)
{
   idt_entries[num].base_low = base & 0xFFFF;
   idt_entries[num].base_high = (base >> 16) & 0xFFFF;

   idt_entries[num].selector = sel;
   idt_entries[num].reserved = 0;

   // We must uncomment the OR below when we get to using user-mode.
   // It sets the interrupt gate's privilege level to 3.
   idt_entries[num].flags   = flags /* | 0x60 */;
}

void __nxapi idt_initialize()
{
   idt_ptr.limit = sizeof(K_IDTENTRY) * 256 - 1;
   idt_ptr.addr= (DWORD)idt_entries;

   memset(&idt_entries[0], 0, sizeof(K_IDTENTRY) * 256);

   /**
    * Setup all 32 interrupt service routine handlers
    */
   idt_set_gate( 0, (DWORD)isr_handler_0 , 0x08, 0x8E);
   idt_set_gate( 1, (DWORD)isr_handler_1 , 0x08, 0x8E);
   idt_set_gate( 2, (DWORD)isr_handler_2 , 0x08, 0x8E);
   idt_set_gate( 3, (DWORD)isr_handler_3 , 0x08, 0x8E);
   idt_set_gate( 4, (DWORD)isr_handler_4 , 0x08, 0x8E);
   idt_set_gate( 5, (DWORD)isr_handler_5 , 0x08, 0x8E);
   idt_set_gate( 6, (DWORD)isr_handler_6 , 0x08, 0x8E);
   idt_set_gate( 7, (DWORD)isr_handler_7 , 0x08, 0x8E);
   idt_set_gate( 8, (DWORD)isr_handler_8 , 0x08, 0x8E);
   idt_set_gate( 9, (DWORD)isr_handler_9 , 0x08, 0x8E);
   idt_set_gate(10, (DWORD)isr_handler_10, 0x08, 0x8E);
   idt_set_gate(11, (DWORD)isr_handler_11, 0x08, 0x8E);
   idt_set_gate(12, (DWORD)isr_handler_12, 0x08, 0x8E);
   idt_set_gate(13, (DWORD)isr_handler_13, 0x08, 0x8E);
   idt_set_gate(14, (DWORD)isr_handler_14, 0x08, 0x8E);
   idt_set_gate(15, (DWORD)isr_handler_15, 0x08, 0x8E);
   idt_set_gate(16, (DWORD)isr_handler_16, 0x08, 0x8E);
   idt_set_gate(17, (DWORD)isr_handler_17, 0x08, 0x8E);
   idt_set_gate(18, (DWORD)isr_handler_18, 0x08, 0x8E);
   idt_set_gate(19, (DWORD)isr_handler_19, 0x08, 0x8E);
   idt_set_gate(20, (DWORD)isr_handler_20, 0x08, 0x8E);
   idt_set_gate(21, (DWORD)isr_handler_21, 0x08, 0x8E);
   idt_set_gate(22, (DWORD)isr_handler_22, 0x08, 0x8E);
   idt_set_gate(23, (DWORD)isr_handler_23, 0x08, 0x8E);
   idt_set_gate(24, (DWORD)isr_handler_24, 0x08, 0x8E);
   idt_set_gate(25, (DWORD)isr_handler_25, 0x08, 0x8E);
   idt_set_gate(26, (DWORD)isr_handler_26, 0x08, 0x8E);
   idt_set_gate(27, (DWORD)isr_handler_27, 0x08, 0x8E);
   idt_set_gate(28, (DWORD)isr_handler_28, 0x08, 0x8E);
   idt_set_gate(29, (DWORD)isr_handler_29, 0x08, 0x8E);
   idt_set_gate(30, (DWORD)isr_handler_30, 0x08, 0x8E);
   idt_set_gate(31, (DWORD)isr_handler_31, 0x08, 0x8E);
   idt_set_gate(128, (DWORD)isr_handler_128, 0x08, 0xEE);
   idt_set_gate(129, (DWORD)isr_handler_129, 0x08, 0xEE);

   /*
    * Re-map PIC
    * TODO: Implement and use HalIOWait
    */
   hal_cli();
   WRITE_PORT_UCHAR(PORT_PIC1_CMD, 0x11);
   WRITE_PORT_UCHAR(PORT_PIC2_CMD, 0x11);

   WRITE_PORT_UCHAR(PORT_PIC1_DATA, 0x20); //Master PIC's IRQs maps to interrupts [0x20..0x28]
   WRITE_PORT_UCHAR(PORT_PIC2_DATA, 0x28); //Slave IRQs map to [0x28..0x2F]
   WRITE_PORT_UCHAR(PORT_PIC1_DATA, 0x04);
   WRITE_PORT_UCHAR(PORT_PIC2_DATA, 0x02);
   WRITE_PORT_UCHAR(PORT_PIC1_DATA, 0x01);
   WRITE_PORT_UCHAR(PORT_PIC2_DATA, 0x01);

   WRITE_PORT_UCHAR(PORT_PIC1_DATA, 0x0);
   WRITE_PORT_UCHAR(PORT_PIC2_DATA, 0x0);
   hal_sti();

   /*
    * Register IRQ interrupt handlers
    */
   idt_set_gate(IRQ0_INTID, (DWORD)irq_handler_0 , 0x08, 0x8E);
   idt_set_gate(IRQ1_INTID, (DWORD)irq_handler_1 , 0x08, 0x8E);
   idt_set_gate(IRQ2_INTID, (DWORD)irq_handler_2 , 0x08, 0x8E);
   idt_set_gate(IRQ3_INTID, (DWORD)irq_handler_3 , 0x08, 0x8E);
   idt_set_gate(IRQ4_INTID, (DWORD)irq_handler_4 , 0x08, 0x8E);
   idt_set_gate(IRQ5_INTID, (DWORD)irq_handler_5 , 0x08, 0x8E);
   idt_set_gate(IRQ6_INTID, (DWORD)irq_handler_6 , 0x08, 0x8E);
   idt_set_gate(IRQ7_INTID, (DWORD)irq_handler_7 , 0x08, 0x8E);
   idt_set_gate(IRQ8_INTID, (DWORD)irq_handler_8 , 0x08, 0x8E);
   idt_set_gate(IRQ9_INTID, (DWORD)irq_handler_9 , 0x08, 0x8E);
   idt_set_gate(IRQ10_INTID, (DWORD)irq_handler_10, 0x08, 0x8E);
   idt_set_gate(IRQ11_INTID, (DWORD)irq_handler_11, 0x08, 0x8E);
   idt_set_gate(IRQ12_INTID, (DWORD)irq_handler_12, 0x08, 0x8E);
   idt_set_gate(IRQ13_INTID, (DWORD)irq_handler_13, 0x08, 0x8E);
   idt_set_gate(IRQ14_INTID, (DWORD)irq_handler_14, 0x08, 0x8E);
   idt_set_gate(IRQ15_INTID, (DWORD)irq_handler_15, 0x08, 0x8E);

   /* Disable all IRQs. Line 2 has to be re-enabled since it
    * is used to cascade requests to PIC2 */
   irq_disable_all();
   irq_enable(2);

   /* Initialize interrupt handler cb table. These are
    * invoked by the kernel when requested interrupt occur.
    * Now we fill table with NULLs
    */
   memset(isr_callbacks, 0, sizeof(LPVOID)*256);

   /* Submit new IDT to CPU */
   HalLoadIDT(&idt_ptr);

   /* Register some intrinsic exception handling routines */
   register_isr_callback(0xD, exception_handler_gpf, NULL);
}

// This gets called from our ASM interrupt handler stub.
void __nxapi isr_handler_gateway(K_REGISTERS regs)
{
	/* Try to handle interrupt */
	if(isr_callbacks[regs.int_no] != NULL) {
		//Interrupt handler found. Invoke it.
		return isr_callbacks[regs.int_no](regs);
	}

	k_printf("Unhandled interrupt: %x\n", regs.int_no);
	k_printf("Dumping register states...\n");
	k_printf("EAX= %x\t EBX= %x\t ECX= %x\t EDX= %x\n", regs.eax, regs.ebx, regs.ecx, regs.edx);
	k_printf("CS = %x\t DS = %x\t SS = %x\t EIP= %x\n", regs.cs, regs.ds, regs.ss, regs.eip);
	k_printf("RETCODE= %x\t INT_NUM= %x\n", regs.err_code, regs.int_no);
}

HRESULT __nxapi register_isr_callback(DWORD int_id, K_INTERRUPTCALLBACK pCB, void* data_ptr)
{
//  HalDisableInterrupt();
	UNUSED_ARG(data_ptr);

	if(int_id >= 256) {
	  return E_INVALIDARG;
	}

	isr_callbacks[int_id] = pCB;

	/* If this interrupt is used by an IRQ line, enable that line */
	if (intid_to_irq(int_id) != INVALID_IRQ_LINE) {
		/* Enable IRQ */
		irq_enable(intid_to_irq(int_id));
	}

	//  HalEnableInterrupt();
	return S_OK;
}

HRESULT __nxapi unregister_isr_callback(DWORD int_id)
{
	/* If this interrupt is used by an IRQ line, disable the line */
	if (intid_to_irq(int_id)) {
		/* Disable */
		irq_disable(intid_to_irq(int_id) != INVALID_IRQ_LINE);
	}

	return register_isr_callback(int_id, NULL, NULL);
}

/*
 * Sends end-of-interrupt command to PIC port
 */
static void __nxapi pic_send_eoi(BYTE irq)
{
	if(irq >= 8) {
		WRITE_PORT_UCHAR(PORT_PIC2_CMD, PIC_EOI);
	}

	WRITE_PORT_UCHAR(PORT_PIC1_CMD, PIC_EOI);
}

/**
 * This function is called by it's ASM counterpart (irq_common_gateway)
 */
void __nxapi irq_handler_gateway(K_REGISTERS regs)
{
	/* Get IRQ id by interrupt ID.
	 * If we begin to use non-contiguous IRQ id's, we might need
	 * to do more precise IRQ->INT unmapping, like checking each
	 * IRQ id separately.
	 */
	//int irqid = regs.int_no - IRQ0_INTID;
	int irqid = intid_to_irq(regs.int_no);

	/* Handle spurious IRQ7 */
	if(regs.int_no == 0x27) {
		WRITE_PORT_UCHAR(0x20, 0x0B);
		BYTE irr = READ_PORT_UCHAR(0x20);

		if ((irr & 0x80) == 0) {
			//k_printf("[spurious IRQ received]");
			return;
		}
	}

	pic_send_eoi(irqid);

	/* Try to handle interrupt */
	if(isr_callbacks[regs.int_no] != NULL) {
		//Interrupt handler found. Invoke it.
		isr_callbacks[regs.int_no](regs);
		return;
	};

	char msg[256];

	/* It seems we weren't able to handle this */
	sprintf(msg, "Unhandled IRQ (int_id= %x; dec= %d)\n", regs.int_no, regs.int_no );
	HalKernelPanic(msg);
}

/**
 * Enables a given IRQ, by changing the mask
 */
HRESULT __nxapi irq_enable(BYTE irq_id)
{
	WORD port;
	BYTE value;

	if(irq_id>15) {
		return E_INVALIDARG;
	}

	if(irq_id < 8) {
		port = PORT_PIC1_DATA;
	} else {
		port = PORT_PIC2_DATA;
		irq_id -= 8;
	}

	value = READ_PORT_UCHAR(port) & ~(1 << irq_id);
	WRITE_PORT_UCHAR(port, value);

	return S_OK;
}

/**
 * Disables given IRQ
 */
HRESULT __nxapi irq_disable(BYTE irq_id)
{
	WORD port;
	BYTE value;

	if(irq_id>15) {
		return E_INVALIDARG;
	}

	if(irq_id < 8) {
		port = PORT_PIC1_DATA;
	} else {
		port = PORT_PIC2_DATA;
		irq_id -= 8;
	}

	value = READ_PORT_UCHAR(port) | (1 << irq_id);
	WRITE_PORT_UCHAR(port, value);

	return S_OK;
}

/**
 * Disables all IRQs
 */
HRESULT __nxapi irq_disable_all()
{
	WRITE_PORT_UCHAR(PORT_PIC1_DATA, 0xFF);
	WRITE_PORT_UCHAR(PORT_PIC2_DATA, 0xFF);

	return S_OK;
}

void __nxapi gdt_initialize() {
	gdt_ptr.limit = (sizeof(K_GDTENTRY) * 6) - 1;
	gdt_ptr.base = (DWORD) &gdt_entries[0];

	gdt_set_gate(0, 0, 0, 0, 0); // Null segment
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code segment
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment
	gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
	gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment

	/* Add GDT entry for TSS */
	uint32_t	tss_base = (uint32_t)&tss_entry;
	gdt_set_gate(5, tss_base, tss_base + sizeof(K_TSS_ENTRY), 0xE9, 0x00); // Task state segment

	/* Initialize and populate tss_entry.
	 * We set last two bits of segment selectors to enable them to be
	 * switched to from CPL=3
	 */
	memset(&tss_entry, 0, sizeof(tss_entry));
	tss_entry.cs = 0x08 | 2;
	tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x10 | 2;
	tss_entry.ss0 = 0x10;

	/* Load GDT to CPU */
	HalLoadGDT(&gdt_ptr);

	/* Load TSR using "ltr" instruction. */
	hal_tss_flush(5 * sizeof(K_GDTENTRY));
}

void __nxapi gdt_update_tss(uint16_t ss_kernel, uint32_t esp_kernel)
{
	tss_entry.ss0 = ss_kernel;
	tss_entry.esp0 = esp_kernel;
}

void __nxapi exception_handler_gpf(K_REGISTERS regs)
{
	k_printf("General protection fault at address %x. (errcode: %x)\n", regs.eip, regs.err_code);
	HalKernelPanic("");
}

uint32_t __nxapi irq_to_intid(uint32_t irq_id)
{
	if (irq_id > 15) {
		/* Curretly we don't support APIC, so we have only 16 IRQ lines */
		return 0xFFFFFFFF;
	}

	if (irq_id < 8) {
		return IRQ0_INTID + irq_id;
	}else {
		return IRQ8_INTID + (irq_id-8);
	}
}

uint32_t __nxapi intid_to_irq(uint32_t int_id)
{
	if (int_id >= IRQ0_INTID && int_id <= IRQ15_INTID) {
		return int_id - IRQ0_INTID;
	}

	return INVALID_IRQ_LINE;
}
