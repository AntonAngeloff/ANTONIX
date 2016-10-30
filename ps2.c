/*
 * ps2.c
 *
 *  This file contains (partial) driver for PS\2 controller and a very simple keyboard driver.
 *  The kbd driver is actually a parody and should be rewritten soon... we hope..
 *
 *	Review on 6.10.2016: It really calls for reimplementation...
 *
 *  Created on: 13.11.2015 ã.
 *      Author: Anton Angelov
 */

#include <ps2.h>
#include <desctables.h>
#include <vga.h>
#include <hal.h>
#include <timer.h>
#include <keyboard.h>
#include <string.h>
#include <kdbg.h>
#include "include/devices.h"
#include "drivers/ps2mouse.h"

#define READ_TIMEOUT	200
#define WRITE_TIMEOUT	1000

void __nxapi mouse_irq_handler(K_REGISTERS regs);
void __nxapi keyboard_irq_handler(K_REGISTERS regs);

struct {
	BOOL initialized;
	BYTE ps2_channels;
} ps2_driver_state;

/**
 * TODO: probably move to stand-alone source file
 */
struct {
	BOOL ps_caps, ps_scroll, ps_num;
	BYTE keydown_map[128];
} keyboard_driver_state;

/**
 * We need a buffer to store symbols, received from keyboard IRQ
 */
char kbd_buffer[256];
int kbd_ptr;

//TODO: add spinlock for kbd_buffer

static BYTE ps2_get_status()
{
	return READ_PORT_UCHAR(PORT_PS2_CMD);
}

/**
 * Waits until ps2 status flag's 0 bit is set
 */
static HRESULT ps2_wait_read(DWORD dwTimeout)
{
	BYTE s = ps2_get_status();
	DWORD to_time = timer_gettickcount() + dwTimeout;

	//Wait, until bit 0 is set
	while ((s&1) == 0) {
		/* Check if we have timed out */
		if(timer_gettickcount() >= to_time) {
			return E_FAIL;
		}

		s = ps2_get_status();
	}

	return S_OK;
}

/**
 * Waits until ps2 status flag's 1 bit is set
 */
static void ps2_wait_write() {
	BYTE s = ps2_get_status();

	//Wait, until bit 1 is set
	while ((s&2) != 0) {
		s = ps2_get_status();
	}
}

/**
 * Set's configuration byte
 */
static void ps2_set_conf(BOOL ch1_int, BOOL ch2_int, BOOL transl)
{
	BYTE cb; //configuration byte

	/* Send command 0x20 */
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0x20);
	ps2_wait_read(READ_TIMEOUT);

	/* Read response */
	cb = READ_PORT_UCHAR(PORT_PS2_DATA);

	/* Set bits of interest
	 * Enable\disable interrupts of port#1
	 */
	if(ch1_int)
		cb |= 0x01;
	else
		cb &= ~0x01;

	/* Enable\disable interrupts of port#2 */
	if(ch2_int)
		cb |= 0x02;
	else
		cb &= ~0x02;

	/* Set translation flag */
	if(transl)
		cb |= 0x40;
	else
		cb &= ~0x40;

	/* Set new configuration byte to PS2 controller */
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0x60);
	ps2_wait_write();

	WRITE_PORT_UCHAR(PORT_PS2_DATA, cb);
}

/**
 * Writes a byte to a ps2 device.
 * dev_id stands for the channel (port) of PS\2 controller (i.e. [0..1])
 */
HRESULT __nxapi ps2_device_write(int dev_id, BYTE data)
{
	switch (dev_id) {
	case 0:
		break;
	case 1:
		if (ps2_driver_state.ps2_channels<2) {
			return E_FAIL;
		}

		/* We tell the PS2 ctl that the data we send is for second channel */
		WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xD4);
		break;
	default:
		return E_INVALIDARG;
	}

	/**
	 * TODO: implement timeout with timer
	 */
	ps2_wait_write();

	/* Write actual data */
	WRITE_PORT_UCHAR(PORT_PS2_DATA, data);

	return S_OK;
}

/**
 * Reads a byte from PS\2 ctl data port (explicitly)
 */
BYTE __nxapi ps2_device_read_ex(DWORD dwTimeout)
{
	if(dwTimeout) {
		ps2_wait_read(dwTimeout);
	}

	return READ_PORT_UCHAR(PORT_PS2_DATA);
}

/**
 * Reads data from PS\2 controller, but first
 * waits for the output flag to be set in status register
 */
BYTE __nxapi ps2_device_read()
{
	ps2_wait_read(READ_TIMEOUT);
	return READ_PORT_UCHAR(PORT_PS2_DATA);
}

/**
 * Triggers CPU reset via the PS\2 controller
 */
HRESULT __nxapi ps2_cpu_reset()
{
	ps2_wait_write();
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xFE);

	//Actually we shouldn't reach here.
	return S_OK;
}

/**
 * Sends RESET command to a ps2 device
 */
HRESULT __nxapi ps2_device_reset(int dev_id)
{
	//Disable scanning
//	ps2_device_write(dev_id, PS2DEV_DISABLE_SCANNING);
//	if(ps2_device_read() != 0xFA) {
//		return E_FAIL;
//	}

	//Send RESET command
	ps2_device_write(dev_id, PS2DEV_RESET);
	if(ps2_device_read() != 0xFA) {
		return E_FAIL;
	}

	return S_OK;
}

/**
 * Sets IRQ callback for IRQ1
 * and also initalizes the keyboard driver state
 */
static HRESULT __nxapi keyboard_initialize() {
	register_isr_callback(IRQ1_INTID, keyboard_irq_handler, NULL);
	memset(&keyboard_driver_state, 0, sizeof(keyboard_driver_state));

	return S_OK;
}

void __nxapi mouse_irq_handler(K_REGISTERS regs)
{
	if(regs.eax) { } //avoid warnings

	/* TODO: Currently we just flush IO buffer for IRQ12. Later we need to
	 * read it's content and enqeueu it in a buffer.
	 */
	ps2_device_read();
	ps2_device_read_ex(0);
	ps2_device_read_ex(0);
}

/**
 * Loops while PS\2 controller sends a ACK byte
 */
void __nxapi keyboard_waitfor_ack()
{
	  while( !(READ_PORT_UCHAR(PORT_PS2_DATA)==0xFA) ) {
		  ;
	  }
}

void __nxapi keyboard_irq_handler(K_REGISTERS regs)
{
	BYTE b[3];
	BYTE *pB = b;

	if(kbd_ptr>=256) {
		HalKernelPanic("Keyboard buffer overflow!");
	}

	if(regs.eax) {
		//avoid warnings
	}

	*(pB++) = ps2_device_read_ex(0);
	//*(pB++) = ps2_device_read_ex();
	//*(pB++) = ps2_device_read_ex();

	if(b[0] & 0x80) {
		//Key is released
		keyboard_driver_state.keydown_map[b[0]&0x7F] = FALSE;
		//vga_printf("(rl: %x)(%x)\t", b[0], b[0]&0x7F);
		return;
	} else {
		keyboard_driver_state.keydown_map[b[0]] = TRUE;
	}

	WORD pseudoascii;
	if((keyboard_driver_state.keydown_map[0x2A] || keyboard_driver_state.keydown_map[0x36])) {
		pseudoascii = __keyboard_scs1_uc[b[0]];
	} else {
		pseudoascii = __keyboard_scs1[b[0]];
	}

	if(__is_ascii_sym(pseudoascii)) {
		/* Append to buffer */
		kbd_buffer[kbd_ptr++] = pseudoascii;
		return;
	}

	switch(pseudoascii) {
		case VKEY_CAPSLOCK:
			keyboard_driver_state.ps_caps ^= 1;
			break;
		case VKEY_SCROLLLOCK:
			keyboard_driver_state.ps_scroll ^= 1;
			break;
		case VKEY_NUMLOCK:
			keyboard_driver_state.ps_num^= 1;
			break;
	}

	return;
}

BYTE __nxapi getch()
{
	while(kbd_ptr == 0) {
		;
	}

	int i;
	BYTE res = kbd_buffer[0];

	for(i=0; i<kbd_ptr-1; i++) {
		kbd_buffer[i] = kbd_buffer[i+1];
	}
	kbd_ptr--;

	return res;
}

/* Reads a character from the kbd buffer (same as getch(), but doesn't block) */
HRESULT __nxapi readch(BYTE *c) {
	if (kbd_ptr == 0) {
		return E_ENDOFSTR;
	}

	int i;
	*c = kbd_buffer[0];
	kbd_ptr--;

	for(i=0; i<kbd_ptr; i++) {
		kbd_buffer[i] = kbd_buffer[i+1];
	}

	return S_OK;
}

/**
 * Reads bytes from PS\2 device 1
 */
int32_t ps2_device1_read(BYTE *buff, int32_t size)
{
	int bytes_read = 0;

	while (size-- > 0) {
	  *(buff++) = ps2_device_read();
	  bytes_read++;
	}

	return bytes_read;
}

int32_t ps2_device1_write(BYTE *buff, int32_t size)
{
	int bytes_written = 0;

	while (size-- > 0) {
	  if (SUCCEEDED(ps2_device_write(0, *(buff++)))) {
		  bytes_written++;
	  }
	}

	return bytes_written;
}

HRESULT __nxapi ps2_initialize()
{
	HRESULT hres;

	/* Disable interrupts */
	hal_cli();

	/* Assume dual-channel ctrlr. */
	ps2_driver_state.ps2_channels = 2;

	/* Disable both PS2 ports during initialization */
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xAD);
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xA7);

	/* Flush PS2 ctl output buffer */
	READ_PORT_UCHAR(PORT_PS2_DATA);

	/* Setup PS2 ctl configuration byte */
	ps2_set_conf(FALSE, FALSE, FALSE);

	/* Test PS2 controller */
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xAA);
	ps2_wait_read(READ_TIMEOUT);

	/* Check result */
	BYTE res = READ_PORT_UCHAR(PORT_PS2_DATA);

	switch(res) {
	case 0x55:
		//Test passed successfully!
		break;
	case 0xFC:
		//Enable interrupts before exiting this routine
		hres = E_FAIL;
		goto exit;
	default:
		HalKernelPanic("Unexpected return code from PS2 controller test...");
	}

	/* Test port#1 */
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xAB);
	ps2_wait_read(READ_TIMEOUT);

	res = READ_PORT_UCHAR(PORT_PS2_DATA);
	if(res != 0x00) {
		hres = E_FAIL;
		goto exit;
	}

	if (ps2_driver_state.ps2_channels > 1) {
		/* Test port#2 */
//		WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xA9);
//		ps2_wait_read(READ_TIMEOUT);
//
//		res = READ_PORT_UCHAR(PORT_PS2_DATA);
//		if(res != 0x00) {
//			//Problem with port#2
//			ps2_driver_state.ps2_channels--;
//		}
	}

	/* Enable ports */
	WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xAE);

	if (ps2_driver_state.ps2_channels>1) {
//		WRITE_PORT_UCHAR(PORT_PS2_CMD, 0xA8);
	}

	/* Enable interrupts (IRQ) for ports and enable translating */
//	ps2_set_conf(ps2_driver_state.ps2_channels>0, ps2_driver_state.ps2_channels>1, TRUE);
	ps2_set_conf(TRUE, /*ps2_driver_state.ps2_channels>1*/FALSE, TRUE);

	/**
	 * NOTE: We don't support Hot Plug for PS\2 devices
	 */

	/* Reset first device (port) */
	hres = ps2_device_reset(0);
	if (FAILED(hres)) goto exit;

	/* Reset second device */
	if(ps2_driver_state.ps2_channels>1) {
//		hres = ps2_device_reset(1);
//
//		/* If we failed to reset PS\2 port#2, then mark it as unavailable */
//		if(FAILED(hres)) {
//			ps2_driver_state.ps2_channels--;
//			dbg_printf("PS/2 Port #2 is not available.\n");
//		}
	}


	/* Enable IRQ1 and assign interrupt callback for keyboard */
//	irq_enable(1);
//	irq_enable(12);
	//register_isr_callback(IRQ1_INTID, keyboard_irq_handler);
	keyboard_initialize();
	hal_sti();

	/* Attach dummy handler */
//	register_isr_callback(IRQ12_INTID, mouse_irq_handler, NULL);

	/* Install PS/2 Mouse driver */
	hres = ps2mouse_install();
	if (FAILED(hres)) {
		ps2_driver_state.ps2_channels = 1;
		hres = S_OK;
	}

	/* Init kbd buffer */
	kbd_ptr = 0;

	/* PS2 driver initialized successfully */
	ps2_driver_state.initialized = TRUE;

	/* So far so good */
	return S_OK;

exit:
	hal_sti();
	return hres;
}

/*
//Define PS\2 device
K_DEVICE nx_ps2_port1 = {
		.name = "PS\2 Port1",
		.read = ps2_device1_read,
		.write = ps2_device1_write,
		.seek = NULL,
};

*/
