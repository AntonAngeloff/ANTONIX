/*
 * isa_dma.c
 *
 *	Where to find info:
 *		- http://www.intel-assembler.it/PORTALE/4/231466_8237A_DMA.pdf
 *		- http://wiki.osdev.org/ISA_DMA
 *
 *	ISA DMA controller chip (similar to other DMAs) performs data transfer
 *	on behalf of the CPU, which off loads the CPU in order to perform
 *	different (perhaps more important) tasks. The types of data transfers are:
 *		- peripheral device -> memory
 *		- memory -> peripheral device
 *		- memory -> memory (we don't support this)
 *
 *	Particularly the ISA bus is by far obsolete and is used only in very old
 *	hardware. Theoretically it supports speed of 4.77 MB/s but in practice the
 *	transfer is limited to 400 KB/s (according to OS dev wiki).
 *
 *	The DMA uses mechanisms to recognize when the memory bus is not used and
 *	uses it to perform the transfer. There are mechanism for applying privilege
 *	control, but they are not incorporated here.
 *
 *  Created on: 3.09.2016 ã.
 *      Author: Anton Angelov
 */
#include <isa_dma.h>
#include <syncobjs.h>
#include <hal.h>

/* Used for serializing access to the DMA controller registers'
 * flip-flop. Since all registers use a single flip flop.
 */
static K_SPINLOCK flipflop_lock;

/* Used to serialize access to channel state array
 */
static K_SPINLOCK inner_lock;

/**
 * Register map describing DMA controller ports/registers.
 */
static struct {
	uint8_t status;
	uint8_t command;
	uint8_t request;
	uint8_t single_channel_mask;
	uint8_t mode;
	uint8_t clear_flip_flop;
	uint8_t intermediate;
	uint8_t master_reset;
	uint8_t mask_reset;
	uint8_t multi_channel_mask;
} dma_ports[] = {
	{ 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0D, 0x0E, 0x0F },
	{ 0xD0, 0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDA, 0xDC, 0xDE }
};

/**
 * Register map describing channel-specific DMA ports/registers
 */
static struct {
	uint8_t start_addr;
	uint8_t count;
	uint8_t page_addr;
} dma_ch_ports[] = {
	/* Channel 0. It is virtually unusable, since it is reserved for legacy features */
	{ 0x00, 0x01, 0x87 },
	/* Channel 1 */
	{ 0x02, 0x03, 0x83 },
	/* Channel 2 */
	{ 0x04, 0x05, 0x81 },
	/* Channel 3 */
	{ 0x06, 0x07, 0x82 },
	/* Channel 4. Unusable due to cascading */
	{ 0xC0, 0xC2, 0x8F },
	/* Channel 5 */
	{ 0xC4, 0xC6, 0x8B },
	/* Channel 6 */
	{ 0xC8, 0xCA, 0x89 },
	/* Channel 7 */
	{ 0xCC, 0xCE, 0x8A }
};

/**
 * Performs useless operations so to occupy the processor for few cycles
 * while waiting for a peripheral device for respond.
 */
static void dma_delay()
{
	volatile int i = 20;

	while (i--) {
		;
	}
}

/**
 * Since ISA DMA uses flip flops for it's 16 bit registers, this routine provides
 * and abstract way to write 16 bit value directly to a register.
 */
static void dma_write_word(uint32_t port, uint16_t data)
{
	uint8_t		b;
	uint32_t 	inf;

	/*
	 * Lock flip-flop lock, so other DMA devices doesn't mess it's state
	 */
	inf = spinlock_acquire(&flipflop_lock);

	/*
	 * Write most significant 8 bits
	 */
	b = (uint8_t)(data & 0xFF);
	WRITE_PORT_UCHAR(port, b);
	dma_delay();

	/*
	 * Write least significant bits
	 */
	b = (uint8_t)((data >> 8) & 0xFF);
	WRITE_PORT_UCHAR(port, b);
	dma_delay();

	/* Unlock spin-lock */
	spinlock_release(&flipflop_lock, inf);
}

/**
 * Disables a controller by given id
 */
static void dma_disable_ctrl(uint32_t ctrl_id)
{
	WRITE_PORT_UCHAR(dma_ports[ctrl_id].command, 0x04);
	dma_delay();
}

/**
 * Enables a controller by given id
 */
static void dma_enable_ctrl(uint32_t ctrl_id)
{
	WRITE_PORT_UCHAR(dma_ports[ctrl_id].command, 0x00);
	dma_delay();
}

/**
 * Masks in or out a channel
 */
static HRESULT dma_mask_channel(uint32_t ch_id, uint8_t enabled)
{
	/* Chennels 0 and 4 mask is not allowed to be changed */
	if (ch_id == 0 || ch_id == 4) {
		return E_FAIL;
	}

	if (ch_id > 7) {
		return E_INVALIDARG;
	}

	/* Find out controller id.
	 */
	uint32_t ctrl_id = ch_id > 3 ? 1 : 0;
	uint32_t mask_flag = enabled ? 0x00 : (0x01 << 2);

	/* Set new mask */
	WRITE_PORT_UCHAR(dma_ports[ctrl_id].single_channel_mask, (ch_id & 0x03) | mask_flag);
	dma_delay();

	return S_OK;
}

/**
 * Sets particular mode to a channel via Mode register
 */
static void dma_set_channel_mode(uint32_t ch_id, uint32_t transf_mode, uint32_t op_mode, uint32_t autoinit, uint32_t reverse)
{
	uint32_t ctrl_id = ch_id > 3 ? 1 : 0;
	uint32_t transfer_type;

	/* Convert symbolic constant to DMA specific bits */
	switch (transf_mode) {
		case DMA_TRANSFER_READ:
			transfer_type = 2; //0b10 - peripheral is reading from memory
			break;

		case DMA_TRANSFER_WRITE:
			transfer_type = 1; //0b01 - peripheral is writing to memory
			break;

		case DMA_TRANSFER_VERIFY:
			transfer_type = 0; //Run self test on controller
			break;

		default:
			HalKernelPanic("dma_set_channel_mode(): Invalid channel mode.");
			break;
	}

	if (op_mode > 2) {
		HalKernelPanic("dma_set_channel(): invalid operation mode");
	}

	/* Reduce booleans to single bit */
	autoinit = autoinit ? 1 : 0;
	reverse = reverse ? 1 : 0;

	/* Compile final data byte */
	uint8_t data = (ch_id & 0x03) | (transfer_type << 2) | (autoinit << 4) | (reverse << 5) | (op_mode << 6);

	/* Write to mode register */
	WRITE_PORT_UCHAR(dma_ports[ctrl_id].mode, data);
	dma_delay();
}

/**
 * Resets the 8237A flip flop state by writing to Clear First/Last Flip-Flop
 * register.
 */
static void dma_reset_flipflop(uint32_t ctrl_id)
{
	uint32_t inf = spinlock_acquire(&flipflop_lock);
	WRITE_PORT_UCHAR(dma_ports[ctrl_id].clear_flip_flop, 0x01);
	spinlock_release(&flipflop_lock, inf);

	dma_delay();
}

/**
 * Opens a DMA channel for arbitrary use case
 */
HRESULT isadma_open_channel(uint32_t channel, void *address, uint32_t count, uint32_t t_mode, uint32_t o_mode, uint32_t autoinit)
{
	HRESULT 	hr = S_OK;
	uint32_t	inf;
	uint16_t	addr_low;
	uint8_t		addr_page;

	/* Validate args */
	if (channel > 7 || count == 0) {
		return E_INVALIDARG;
	}

	if (t_mode != DMA_TRANSFER_READ && t_mode != DMA_TRANSFER_WRITE && t_mode != DMA_TRANSFER_VERIFY) {
		return E_INVALIDARG;
	}

	/* Make sure address is below 16m physical range */
	if ((uintptr_t)address > 0xFFFFFF) {
		HalKernelPanic("isadma_open_channel(): receiving buffer above 16mb boundary.");
	}

	/* Verification transfer type is not yet implemented */
	if (t_mode == DMA_TRANSFER_VERIFY) {
		return E_NOTIMPL;
	}

	/* Decide which controller the channel belongs to */
	uint32_t ctrl_id = channel > 3 ? 1 : 0;

	/* Lock */
	inf = spinlock_acquire(&inner_lock);

	/* Disable contoller while modifying registers */
	dma_disable_ctrl(ctrl_id);

	/* Disable channel */
	hr = dma_mask_channel(channel, FALSE);
	if (FAILED(hr)) goto fail;

	/* Set channel mode */
	dma_set_channel_mode(channel, t_mode, o_mode, autoinit, 0);

	/* Set address and count. For 16bit transfers, start
	 * address should be in words. */
	addr_low = (uintptr_t)address & 0xFFFF;
	addr_page = ((uintptr_t)address >> 16) & 0xFF;

	/*
	 * Write base (start) address
	 */
	dma_reset_flipflop(ctrl_id);
	dma_write_word(dma_ch_ports[channel].start_addr, addr_low);
	dma_delay();

	/*
	 * Write transfer length
	 */
	dma_reset_flipflop(ctrl_id);
	if (ctrl_id == 1) { count /= 2; } //16bit channels require number of words, not bytes
	dma_write_word(dma_ch_ports[channel].count, count-1);
	dma_delay();

	/*
	 * Write page (second half of the higher 16 bits of the address)
	 */
	dma_reset_flipflop(ctrl_id);
	WRITE_PORT_UCHAR(dma_ch_ports[channel].page_addr, addr_page);
	dma_delay();

	/* Enable the channel */
	dma_mask_channel(channel, TRUE);

	/* Success */
	goto finally;

fail:
	hr = E_FAIL;

finally:
	/* Re-enable controller */
	dma_enable_ctrl(ctrl_id);
	spinlock_release(&inner_lock, inf);

	return hr;
}

HRESULT isadma_close_channel(uint32_t channel)
{
	if (channel > 7) {
		return E_INVALIDARG;
	}

	if (channel == 0 || channel == 4) {
		/* Not allowed */
		return E_FAIL;
	}

	uint32_t ctrl_id = channel > 3 ? 1 : 0;
	uint32_t inf;

	inf = spinlock_acquire(&inner_lock);

	/* Disable controller */
	dma_disable_ctrl(ctrl_id);

	/* Mask channel */
	dma_mask_channel(channel, FALSE);

	/* Re-enable controller */
	dma_enable_ctrl(ctrl_id);

	spinlock_release(&inner_lock, inf);

	return S_OK;
}

HRESULT isadma_initialize()
{
	/* We will do not detection logic, we assume 8237A chip is
	 * present.
	 */

	/* Disable all channels */
	for (int i=0; i<8; i++) {
		dma_mask_channel(i, FALSE);
	}

	/* Enable controllers */
	dma_enable_ctrl(0);
	dma_enable_ctrl(1);

	spinlock_create(&flipflop_lock);
	spinlock_create(&inner_lock);

	return S_OK;
}

HRESULT isadma_finalize()
{
	spinlock_destroy(&flipflop_lock);
	spinlock_destroy(&inner_lock);

	return S_OK;
}
