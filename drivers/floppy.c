/*
 * floppy.c
 *
 *  Driver for Intel 82077AA Floppy Disk Controller (FDC) for ANTONIX OS.
 *	It is intended to handle Model 30 mode only using ISA DMA for data
 *	transfers, so PIO is not used.
 *
 *	This driver will not work in any way for USB floppy controllers.
 *
 *	References:
 *		- http://www.buchty.net/casio/files/82077.pdf
 *
 *  Created on: 16.09.2016 ã.
 *      Author: Anton Angelov
 */
#include <vfs.h>
#include <hal.h>
#include <timer.h>
#include <scheduler.h>
#include <mm.h>
#include <isa_dma.h>
#include <string.h>
#include <desctables.h>
#include <kstdio.h>
#include "floppy.h"

/* Base addresses for FDC 1 and 2 */
#define	FDC_BASE_1						0x3F0
#define FDC_BASE_2						0x370

/* Maximum number of supported floppy drives per controller */
#define FDC_MAX_DRIVE_COUNT				4
#define FDC_IRQ_ID						6

/* FDC Commands */
#define CMD_SPECIFY                     3
#define CMD_WRITE   	                0x45
#define CMD_READ	                    0x46
#define CMD_RECALIBRATE                 7      //Seeks to cylinder 0
#define CMD_SENSE_INTERRUPT_STATUS      8      //Acknowledge IRQ6 and get command status
#define CMD_FORMAT_TRACK                13
#define CMD_SEEK                        15     //Seeks heads to particular cylinder
#define CMD_VERSION                     16
#define CMD_PERPENDICULAR_MODE          18
#define CMD_CONFIGURE                   19     //Initializes controller-specific values
#define CMD_LOCK                        20

/**
 * Describes the working port addresses used with FDC.
 */
typedef struct FDC_PORTS FDC_PORTS;
struct FDC_PORTS {
	uint32_t	status_a;		//SRA
	uint32_t	status_b;		//SRB
	uint32_t	digital_output;
	uint32_t 	tape_driver;
	uint32_t 	main_status;	//MSR
	uint32_t	datarate_select;//DSR
	uint32_t	digital_input;	//DIR
	uint32_t	config_ctrl;	//CCR
	uint32_t	data_fifo;
};

/**
 * Floppy Disk Controller context
 */
typedef struct FDC_CTRL_CONTEXT FDC_CTRL_CONTEXT;
struct FDC_CTRL_CONTEXT {
	/* TRUE if controller is initialized */
	uint8_t		ready;

	/* Ports */
	FDC_PORTS	ports;

	/* Used for synchronization of the driver thread with the ISR */
	K_EVENT 	isr_event;

	/* Used to serialize access to floppy drive devices. Currently
	 * this mechanism is not implemented. */
	K_MUTEX		lock;
};

/**
 * A struct describing the state of a floppy disk drive.
 */
typedef struct FDC_DRIVE_CONTEXT FDC_DRIVE_CONTEXT;
struct FDC_DRIVE_CONTEXT {
	/* Floppy Disk Controller context */
	FDC_CTRL_CONTEXT	*ctrl;

	/* Id ranges in [0..3] */
	uint8_t				id;

	/* Signifies if the motor is turned on or off */
	BOOL				motor_state;

	/* Current head position */
	uint8_t 			cur_cylinder;

	/* Status from last operation */
	uint8_t 			cur_status;

	/* DMA buffer */
	uint8_t				*dma_buffer;
	uintptr_t			dma_buffer_phys;
};

/* FDC controller state */
static FDC_CTRL_CONTEXT controller = {0};

/* Prototypes */
static void fdc_delay_short();
static void lba_to_chs(uint32_t lba, uint8_t *c, uint8_t *h, uint8_t *s);

static HRESULT fdc_fifo_read(FDC_CTRL_CONTEXT *ctx, uint8_t *data);
static HRESULT fdc_fifo_write(FDC_CTRL_CONTEXT *ctx, uint8_t data);
static HRESULT fdc_issue_sis(FDC_CTRL_CONTEXT *ctx, uint8_t *out_st0, uint8_t *out_cc);

static HRESULT fdc_select_drive(FDC_DRIVE_CONTEXT *drive);
static HRESULT fdc_specify(FDC_DRIVE_CONTEXT *drive);
static HRESULT fdc_reset(FDC_CTRL_CONTEXT *ctx);
static HRESULT fdc_recalibrate(FDC_DRIVE_CONTEXT *drive);
static HRESULT fdc_seek(FDC_DRIVE_CONTEXT *drive, uint8_t cylinder, uint8_t head);
static HRESULT fdc_set_motor_state(FDC_DRIVE_CONTEXT *drive, BOOL enabled, BOOL wait_to_spinup);

static HRESULT fdc_read_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args);
static HRESULT fdc_write_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args);

static HRESULT fdc_read_sector(FDC_DRIVE_CONTEXT *drive, uint32_t lba, void *dst, uint32_t retry_attempts);
static HRESULT fdc_write_sector(FDC_DRIVE_CONTEXT *drive, uint32_t lba, void *src, uint32_t retry_attempts);
static HRESULT fdc_driver_init(K_DEVICE *self);
static HRESULT fdc_driver_fini(K_DEVICE *self);
static HRESULT fdc_ioctl(K_STREAM *s, uint32_t code, void *arg);

static VOID __cdecl fdc_isr(K_REGISTERS regs);

/**
 * Short delays.
 */
static void fdc_delay_short()
{
	volatile uint32_t timeout = 100;

	while (timeout) {
		timeout--;
	}
}

/**
 * Converts logical block address to CHS (cylinder-head-sector)
 * representation.
 */
static void lba_to_chs(uint32_t lba, uint8_t *c, uint8_t *h, uint8_t *s)
{
    *c = lba / (2 * FLOPPY_144_SECTORS_PER_TRACK);
    *h = ((lba % (2 * FLOPPY_144_SECTORS_PER_TRACK)) / FLOPPY_144_SECTORS_PER_TRACK);
    *s = ((lba % (2 * FLOPPY_144_SECTORS_PER_TRACK)) % FLOPPY_144_SECTORS_PER_TRACK + 1);
}

/**
 * Implementation of the "Send_byte" routine from Intel manual.
 * It writes a byte to the FIFO, while first waiting for non-busy
 * state, which is signified by MSR
 */
static HRESULT fdc_fifo_write(FDC_CTRL_CONTEXT *ctx, uint8_t data)
{
	uint8_t		msr;
	uint32_t	timeout = timer_gettickcount() + FDC_FIFO_TIMEOUT;

	do {
		msr = READ_PORT_UCHAR(ctx->ports.main_status);

		/* Test RQM and DIO flags (mask is 10XXXXXXb) */
		if (msr >> 6 == 2) {
			WRITE_PORT_UCHAR(ctx->ports.data_fifo, data);
			return S_OK;
		}

		/* Yield time-slice to next thread */
		sched_yield();
	} while (timer_gettickcount() < timeout);

	HalKernelPanic("fdc_fifo_write(): timed out.");
	return E_TIMEDOUT;
}

/**
 * Implementation of "Read_byte" from Intel 82077 chip manual.
 */
static HRESULT fdc_fifo_read(FDC_CTRL_CONTEXT *ctx, uint8_t *data)
{
	uint8_t		msr;
	uint32_t	timeout = timer_gettickcount() + FDC_FIFO_TIMEOUT;

	do {
		msr = READ_PORT_UCHAR(ctx->ports.main_status);

		/* Test RQM and DIO flags (mask is 11XXXXXXb) */
		if (msr >> 6 == 3) {
			*data = READ_PORT_UCHAR(ctx->ports.data_fifo);
			return S_OK;
		}

		/* Yield time-slice to next thread */
		sched_yield();
	} while (timer_gettickcount() < timeout);

	return E_TIMEDOUT;
}

/**
 * Issues a SPECIFY command, which does set values of
 * the three internal timers - Head Unload Timer, Head Load
 * Timer and Step Rate Timer.
 *
 * These parameters are meant to optimize drive performance and
 * head lifetime.
 */
static HRESULT fdc_specify(FDC_DRIVE_CONTEXT *drive)
{
	uint8_t	hut;
	uint8_t hlt;
	uint8_t srt;

	/* This values does not really matter on an emulator. */
	hut = 0x0F;
	hlt = 0x0A;
	srt = 0x08;

	/* Send command */
	fdc_fifo_write(drive->ctrl, CMD_SPECIFY);
	fdc_fifo_write(drive->ctrl, (srt << 4) | (hut & 0xF));
	fdc_fifo_write(drive->ctrl, (hlt << 1)); //ND bit is kept 0 for DMA mode.

	return S_OK;
}

/**
 * Resets the FDC and introduces it to a default state.
 */
static HRESULT fdc_reset(FDC_CTRL_CONTEXT *ctx)
{
	HRESULT 	hr;
	uint8_t 	dor_state, b;
	uint32_t	i;

	/* Read current DOR state */
	dor_state = READ_PORT_UCHAR(ctx->ports.digital_output);
	fdc_delay_short();

	/* Clear IRQ-synchronization event */
	event_reset(&ctx->isr_event);

	/* Clear NORESET flag */
	dor_state &= ~0x04;
	WRITE_PORT_UCHAR(ctx->ports.digital_output, dor_state);
	fdc_delay_short();

	/* Lift NORESET flag and force DMA-mode */
	dor_state |= 0x04;
	dor_state |= 0x08;
	WRITE_PORT_UCHAR(ctx->ports.digital_output, dor_state);
	fdc_delay_short();

	/* Set CCR to 0x00, for data rate of 500 kb/s. Set
	 * DSR to same value for better compatibility. */
	WRITE_PORT_UCHAR(ctx->ports.config_ctrl, 0x00);
	fdc_delay_short();
	WRITE_PORT_UCHAR(ctx->ports.datarate_select, 0x00);
	fdc_delay_short();

	/* Wait for IRQ */
	hr = event_waitfor(&ctx->isr_event, FDC_RESET_TIMEOUT);
	if (FAILED(hr)) return hr;

	/*
	 * Issue 4 sense interrupt status commands (as described in
	 * the manual).
	 */
	for (i=0; i<4; i++) {
		uint8_t	st0;
		uint8_t	cc;

		/* Read and discard st0 and cc */
		hr = fdc_issue_sis(ctx, &st0, &cc);
		if (FAILED(hr)) return hr;
	}

	/*
	 * Get controller version
	 */
	hr = fdc_fifo_write(ctx, CMD_VERSION);
	if (FAILED(hr)) return hr;

	hr = fdc_fifo_read(ctx, &b);
	if (FAILED(hr)) return hr;

	if (b != 0x90) {
		/* The FDC is a legacy controller and we can't
		 * support it
		 */
		k_printf("b=%x ", (uint32_t)b);
		HalKernelPanic("Unknown FDC type.");
		return E_FAIL;
	}

	return S_OK;
}

/**
 * Issues a Sense Interrupt Status command.
 * It is used to retrieve return code from SEEK, RELATIVE SEEK and
 * RECALIBRATE commands. It is mandatory to issue a sense interrupt
 * status after one of these three commands above.
 */
static HRESULT fdc_issue_sis(FDC_CTRL_CONTEXT *ctx, uint8_t *out_st0, uint8_t *out_cc)
{
	HRESULT hr;

	/* Send SIS command */
	hr = fdc_fifo_write(ctx, CMD_SENSE_INTERRUPT_STATUS);
	if (FAILED(hr)) return hr;

	/* Read st0 and current cylinder */
	hr = fdc_fifo_read(ctx, out_st0);
	if (FAILED(hr)) return hr;

	hr = fdc_fifo_read(ctx, out_cc);
	if (FAILED(hr)) return hr;

	/* Success */
	return S_OK;
}

/**
 * Selects a drive for target of a following operations. E.g. you call fdc_select_drive()
 * and then you perform some other operation.
 */
static HRESULT fdc_select_drive(FDC_DRIVE_CONTEXT *drive)
{
	uint8_t	dor_state;

	/* Read DOR state */
	dor_state = READ_PORT_UCHAR(drive->ctrl->ports.digital_output);
	fdc_delay_short();

	/* Assert IRQ and (NOT-IN-)RESET bits are set */
	if ((dor_state & 0x0C) != 0xC) {
		HalKernelPanic("fdc_select_drive(): controller not initialized or in wrong state.");
		return E_FAIL;
	}

	/* Set new selection bits */
	dor_state = dor_state & ~0x03;
	dor_state = dor_state | (drive->id & 0x03);

	/* Set new state */
	WRITE_PORT_UCHAR(drive->ctrl->ports.digital_output, dor_state);
	fdc_delay_short();

	return S_OK;
}

/**
 * Sets motor on or off.
 */
static HRESULT fdc_set_motor_state(FDC_DRIVE_CONTEXT *drive, BOOL enabled, BOOL wait_to_spinup)
{
	uint8_t 	dor_state;
	uint32_t 	delay;

	/* Read DOR's state */
	dor_state = READ_PORT_UCHAR(drive->ctrl->ports.digital_output);
	fdc_delay_short();

	/* Calculate new DOR state */
	switch (enabled) {
		/* Turn on */
		case TRUE:
			dor_state |= 0x10 << drive->id;
			delay = wait_to_spinup ? FDC_SPINUP_TIMEOUT : 0;
			break;

		/* Turn off */
		case FALSE:
			dor_state &= ~(0x10 << drive->id);
			delay = FDC_SPINDOWN_TIMEOUT;
			break;

		default:
			HalKernelPanic("fdc_set_motor_state(): unexpected.");
	}

	/* Set new state */
	WRITE_PORT_UCHAR(drive->ctrl->ports.digital_output, dor_state);
	fdc_delay_short();

	/* Wait for motor */
	timer_sleep(delay);

	/* Update motor state in driver context */
	drive->motor_state = enabled;
	drive->cur_cylinder = 255; //invalidate

	return S_OK;
}

/**
 * Recalibrate causes the read/write head to retract to
 * track position 0. Sometimes more than one recalibrate is
 * required.
 */
static HRESULT fdc_recalibrate(FDC_DRIVE_CONTEXT *drive)
{
	uint32_t 	i;
	HRESULT		hr;

	for (i=0; i<FDC_DEFAULT_COMMAND_RETRIES; i++) {
		event_reset(&drive->ctrl->isr_event);

		/* Issue RECALIBRATE command */
		fdc_fifo_write(drive->ctrl, CMD_RECALIBRATE);
		fdc_fifo_write(drive->ctrl, drive->id);

		/* Wait for IRQ */
		hr = event_waitfor(&drive->ctrl->isr_event, FDC_FIFO_TIMEOUT);
		if (FAILED(hr)) {
			/* Time out */
			continue;
		}

		/* Issue SIS */
		hr = fdc_issue_sis(drive->ctrl, &drive->cur_status, &drive->cur_cylinder);
		if (FAILED(hr)) return hr;

		/* Check if read/write head is really moved to cylinder 0 */
		if (drive->cur_cylinder == 0) {
			/* Success */
			return S_OK;
		}
	}

	return E_FAIL;
}

/**
 * Moves the read/write head for specific drive.
 */
static HRESULT fdc_seek(FDC_DRIVE_CONTEXT *drive, uint8_t cylinder, uint8_t head)
{
	#define CHECK(X) if (FAILED(X)) return hr;

	HRESULT		hr;
	uint32_t	i;
	uint8_t		msr_state;

	/* If drive head is located already at `cylinder` cylinder, we don't need to
	 * move it
	 */
	if (drive->cur_cylinder == cylinder) {
		return S_OK;
	}

	/* Reset ISR event */
	event_reset(&drive->ctrl->isr_event);

	/* Issue a SEEK command */
	hr = fdc_fifo_write(drive->ctrl, CMD_SEEK);
	CHECK(hr);

	fdc_fifo_write(drive->ctrl, (head << 2) | (drive->id & 0x3));
	CHECK(hr);

	fdc_fifo_write(drive->ctrl, cylinder);
	CHECK(hr);

	/* Wait for IRQ to fire */
	hr = event_waitfor(&drive->ctrl->isr_event, FDC_IRQ_TIMEOUT);
	if (FAILED(hr)) return hr;

	/* Issue a Sense Interrupt Status command */
	hr = fdc_issue_sis(drive->ctrl, &drive->cur_status, &drive->cur_cylinder);
	if (FAILED(hr)) return hr;

	/* Wait BUSY flags to drop in MSR */
	for (i=0; i<5; i++) {
		timer_sleep(FDC_SEEK_DELAY);

		/* Read MSR state */
		msr_state = READ_PORT_UCHAR(drive->ctrl->ports.main_status);

		/* If busy flag is dropped, break */
		if ((msr_state & ((1 << drive->id) | 0x10)) == 0) {
			goto msr_check_success;
		}
	}

	/* Time out */
	return E_TIMEDOUT;

msr_check_success:
	/* Check if we succeeded in seeking to new cylinder */
	if (drive->cur_cylinder != cylinder) {
		return E_FAIL;
	}

	/* Check st0 (bit 5) */
	if ((drive->cur_status & 0x20) == 0) {
		return E_FAIL;
	}

	/* Success */
	return S_OK;
}

/**
 * Reads a single sector from a floppy disk device and writes it to
 * memory location pointed by `dst`.
 */
static HRESULT fdc_read_sector(FDC_DRIVE_CONTEXT *drive, uint32_t lba, void *dst, uint32_t retry_attempts)
{
	HRESULT 	hr;
	uint8_t		cylinder;
	uint8_t		head;
	uint8_t		sector;
	uint32_t	force_retry = FALSE;

	/*
	 * Select drive and turn motor on.
	 */
	hr = fdc_select_drive(drive);
	if (FAILED(hr)) return hr;

	/* Seems for read operations we don't need to wait for
	 * the motor to spin up.
	 */
	hr = fdc_set_motor_state(drive, TRUE, FALSE); //FALSE is also good
	if (FAILED(hr)) return hr;

	/* Convert LBA to CHS */
	lba_to_chs(lba, &cylinder, &head, &sector);

	/*
	 * Seek to position
	 */
	hr = fdc_seek(drive, cylinder, head);
	if (FAILED(hr)) {
		/* We will try to recalibrate and seek again */
		hr = fdc_recalibrate(drive);
		if (FAILED(hr)) return hr;

		hr = fdc_seek(drive, cylinder, head);
		if (FAILED(hr)) return hr;
	}

	/*
	 * Open DMA channel
	 */
	hr = isadma_open_channel(
			FDC_DMA_CHANNEL,
			(void*)drive->dma_buffer_phys,
			FDC_SECTOR_SIZE,
			DMA_TRANSFER_WRITE,
			DMA_MODE_SINGLE,
			FALSE
	);
	if (FAILED(hr)) return hr;

	/* Reset ISR event */
	event_reset(&drive->ctrl->isr_event);

	/*
	 * Issue READ command
	 */
	fdc_fifo_write(drive->ctrl, CMD_READ);
	fdc_fifo_write(drive->ctrl, (head << 2) | (drive->id & 0x3));
	fdc_fifo_write(drive->ctrl, cylinder);
	fdc_fifo_write(drive->ctrl, head);
	fdc_fifo_write(drive->ctrl, sector);
	fdc_fifo_write(drive->ctrl, 2); //512 bytes/sector
	fdc_fifo_write(drive->ctrl, FLOPPY_144_SECTORS_PER_TRACK);
	fdc_fifo_write(drive->ctrl, 0x1B); //GAP1 default size
	fdc_fifo_write(drive->ctrl, 0xFF);

	/* Wait for IRQ */
	hr = event_waitfor(&drive->ctrl->isr_event, FDC_DEFAULT_TIMEOUT + 1000);
	if (FAILED(hr)) {
		goto finally;
	}

	uint8_t	result[7];

	/* Read back 7 result bytes */
	for (uint32_t i=0; i<7; i++) {
		hr = fdc_fifo_read(drive->ctrl, &result[i]);
		if (FAILED(hr)) goto finally;
	}

	/* Validate st0. If top 2 bits are cleared, then operation
	 * was successful. */
	if (result[0] & 0xC0) {
		/* Fail. For real hardware we have to retry most operations
		 * since it's common for FDC hardware to fail. Retrial is performed
		 * later using recursive call to this routine.  */
		force_retry = TRUE;
		hr = E_FAIL;
		goto finally;
	}

	/* Copy DMA buffer to target buffer */
	memcpy(dst, drive->dma_buffer, FDC_SECTOR_SIZE);

	/* Success */

finally:
	/* Close DMA channel */
	isadma_close_channel(FDC_DMA_CHANNEL);

	/* Retry, if retry counter is non-null */
	if (force_retry && (retry_attempts > 0)) {
		hr = fdc_read_sector(drive, lba, dst, retry_attempts-1);
	}

	return hr;
}

/**
 * Writes a sector (512 bytes) from memory to disk drive medium.
 * The code in this routine is mostly similar to `fdc_read_sector()`
 */
static HRESULT fdc_write_sector(FDC_DRIVE_CONTEXT *drive, uint32_t lba, void *src, uint32_t retry_attempts)
{
	HRESULT 	hr;
	uint8_t		cylinder;
	uint8_t		head;
	uint8_t		sector;
	uint32_t	force_retry = FALSE;

	/*
	 * Select drive and turn motor on.
	 */
	hr = fdc_select_drive(drive);
	if (FAILED(hr)) return hr;

	hr = fdc_set_motor_state(drive, TRUE, TRUE);
	if (FAILED(hr)) return hr;

	/* Convert LBA to CHS */
	lba_to_chs(lba, &cylinder, &head, &sector);

	/*
	 * Seek to position
	 */
	hr = fdc_seek(drive, cylinder, head);
	if (FAILED(hr)) {
		/* We will try to recalibrate and seek again */
		hr = fdc_recalibrate(drive);
		if (FAILED(hr)) return hr;

		hr = fdc_seek(drive, cylinder, head);
		if (FAILED(hr)) return hr;
	}

	/*
	 * Open DMA channel
	 */
	hr = isadma_open_channel(
			FDC_DMA_CHANNEL,
			(void*)drive->dma_buffer_phys,
			FDC_SECTOR_SIZE,
			DMA_TRANSFER_READ,
			DMA_MODE_SINGLE,
			FALSE
	);
	if (FAILED(hr)) return hr;

	/*
	 * Copy `src` buffer to DMA memory.
	 */
	memcpy(drive->dma_buffer, src, FDC_SECTOR_SIZE);

	/* Reset ISR event */
	event_reset(&drive->ctrl->isr_event);

	/*
	 * Issue WRITE command
	 */
	fdc_fifo_write(drive->ctrl, CMD_WRITE);
	fdc_fifo_write(drive->ctrl, (head << 2) | (drive->id & 0x3));
	fdc_fifo_write(drive->ctrl, cylinder);
	fdc_fifo_write(drive->ctrl, head);
	fdc_fifo_write(drive->ctrl, sector);
	fdc_fifo_write(drive->ctrl, 2); //512 bytes/sector
	fdc_fifo_write(drive->ctrl, FLOPPY_144_SECTORS_PER_TRACK);
	fdc_fifo_write(drive->ctrl, 0x1B); //GAP1 default size
	fdc_fifo_write(drive->ctrl, 0xFF);

	/* Wait for IRQ */
	hr = event_waitfor(&drive->ctrl->isr_event, FDC_DEFAULT_TIMEOUT);
	if (FAILED(hr)) {
		goto finally;
	}

	uint8_t	result[7];

	/* Read back 7 result bytes */
	for (uint32_t i=0; i<7; i++) {
		hr = fdc_fifo_read(drive->ctrl, &result[i]);
		if (FAILED(hr)) goto finally;
	}

	/* Validate st0. If top 2 bits are cleared, then operation
	 * was successful. */
	if (result[0] & 0xC0) {
		/* Fail. For real hardware we have to retry most operations
		 * since it's common for FDC hardware to fail. Retrial is performed
		 * later using recursive call to this routine.  */
		force_retry = TRUE;
		hr = E_FAIL;

		/* TODO: If the operation has failed due to write protection there is
		 * no need for additional retry attempts.
		 */
		goto finally;
	}

finally:
	/* Close DMA channel */
	isadma_close_channel(FDC_DMA_CHANNEL);

	/* Retry, if retry counter is non-null */
	if (force_retry && (retry_attempts > 0)) {
		hr = fdc_write_sector(drive, lba, src, retry_attempts-1);
	}

	return hr;
}

/**
 * Initializes the driver (called when mounting the driver to VFS)
 */
static HRESULT fdc_driver_init(K_DEVICE *self)
{
	FDC_DRIVE_CONTEXT 	*drive;
	HRESULT				hr;

	/* Make sure the driver is not initialized */
	if (self->opaque) {
			return E_FAIL;
	}

	/* Create drive context */
	drive = kcalloc(sizeof(FDC_DRIVE_CONTEXT));
	if (!drive) return E_OUTOFMEM;

	drive->cur_cylinder = 255;
	drive->ctrl = &controller;

	/* Allocate DMA buffer */
	drive->dma_buffer = (void*)vmm_get_address_space_end(NULL); //find end of address space to map DMA buffer

	hr = vmm_alloc_and_map_limited(NULL, (uintptr_t)drive->dma_buffer, 0xFFFFFF, FDC_DMA_BUFFER_SIZE, USAGE_HEAP, ACCESS_READWRITE, 1);
	if (FAILED(hr)) goto fail;

	/* Get physical address */
	hr = vmm_get_region_phys_addr(NULL, (uintptr_t)drive->dma_buffer, &drive->dma_buffer_phys);
	if (FAILED(hr)) goto fail;

	/* Make sure physical memory is aligned at 64kb boundary. */
	if ((drive->dma_buffer_phys & 0xFFFF) != 0) {
		HalKernelPanic("FDC: DMA buffer is not on 64k physical boundary.");
	}

	hr = fdc_select_drive(drive);
	if (FAILED(hr)) return hr;

	/* Specify (set) FDC internal timers */
	hr = fdc_specify(drive);
	if (FAILED(hr)) return hr;

	/* Success */
	self->opaque = drive;
	return S_OK;

fail:
	return E_FAIL;
}

static HRESULT fdc_driver_fini(K_DEVICE *self)
{
	FDC_DRIVE_CONTEXT 	*drive = self->opaque;

	vmm_unmap_region(NULL, (uintptr_t)drive->dma_buffer, 1);
	self->opaque = NULL;
	kfree(drive);

	return S_OK;
}

/**
 * IRQ handler
 */
static VOID __cdecl fdc_isr(K_REGISTERS regs)
{
	/* We don't do anything here but signal the ISR event to
	 * let the main thread synchronize with the ISR callback.
	 */
	UNUSED_ARG(regs);
	event_signal(&controller.isr_event);
}

/**
 * Multiple sector (block) reading routine.
 */
static HRESULT fdc_read_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args)
{
	FDC_DRIVE_CONTEXT 	*ctx = GET_DRV_CTX(s);
	uint32_t 			i;
	HRESULT				hr;
	uint8_t				*dst = args->buffer;

	/* Validate arguments */
	if (args->count == 0 || args->buffer == NULL) {
		return E_INVALIDARG;
	}

	/* Perform sequential read */
	for (i=0; i<args->count; i++) {
		hr = fdc_read_sector(ctx, args->start + i, dst, FDC_DEFAULT_COMMAND_RETRIES);
		if (FAILED(hr)) return hr;

		dst += FDC_SECTOR_SIZE;
	}

	return S_OK;
}

/**
 * Multiple sector writing routine.
 */
static HRESULT fdc_write_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args)
{
	FDC_DRIVE_CONTEXT 	*ctx = GET_DRV_CTX(s);
	uint32_t 			i;
	HRESULT				hr;
	uint8_t				*src = args->buffer;

	/* Validate arguments */
	if (args->count == 0 || args->buffer == NULL) {
		return E_INVALIDARG;
	}

	/* Perform sequential read */
	for (i=0; i<args->count; i++) {
		hr = fdc_write_sector(ctx, args->start + i, src, FDC_DEFAULT_COMMAND_RETRIES);
		if (FAILED(hr)) return hr;

		src += FDC_SECTOR_SIZE;
	}

	return S_OK;
}

/**
 * IOCTL handler
 */
static HRESULT fdc_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	switch (code) {
		case IOCTL_STORAGE_READ_BLOCKS:
			return fdc_read_blocks(s, arg);

		case IOCTL_STORAGE_WRITE_BLOCKS:
			return fdc_write_blocks(s, arg);

		case IOCTL_STORAGE_GET_BLOCK_SIZE:
			*(size_t*)arg = FDC_SECTOR_SIZE;
			break;

		case IOCTL_STORAGE_GET_BLOCK_COUNT:
			*(size_t*)arg = FDC_SECTOR_COUNT;
			break;

		/*
		 * Handle open and close signals.
		 */
		case DEVIO_OPEN:
		case DEVIO_CLOSE:
			return S_OK;

		default:
			return E_NOTSUPPORTED;
	}

	return S_OK;
}

HRESULT __nxapi fdc_install()
{
	HRESULT				hr;
	FDC_CTRL_CONTEXT 	*ctrl = &controller;
	uint32_t 			base_port;
	K_DEVICE			dev;

	if (ctrl->ready) {
		HalKernelPanic("fdc_install(): controller already intialized.");
		return E_FAIL;
	}

	/* Initialize FDC context */
	memset(&controller, 0, sizeof(controller));

	/* Hardcode the driver to handle the first FDC only. If we decide to implement support for
	 * the second FDC, we will just assign FDC_BASE_2 for base port address.
	 */
	base_port = FDC_BASE_1;

	/* Setup ports */
	ctrl->ports.status_a 		= base_port + 0;
	ctrl->ports.status_b   		= base_port + 1;
	ctrl->ports.digital_output 	= base_port + 2;
	ctrl->ports.tape_driver		= base_port + 3;
	ctrl->ports.main_status		= base_port + 4;
	ctrl->ports.datarate_select	= base_port + 4;
	ctrl->ports.data_fifo		= base_port + 5;
	ctrl->ports.digital_input	= base_port + 7;
	ctrl->ports.config_ctrl		= base_port + 7;

	/* Register IRQ */
	register_isr_callback(irq_to_intid(FDC_IRQ_ID), fdc_isr, ctrl);

	/* Create synch objects */
	event_create(&ctrl->isr_event, EVENT_FLAG_AUTORESET);
	mutex_create(&ctrl->lock);
	ctrl->ready = TRUE;

	/* Reset controller */
	hr = fdc_reset(ctrl);
	if (FAILED(hr)) return hr;

	/* Populate device struct */
	memset(&dev, 0, sizeof(dev));
	dev.default_url = "/dev/fdd0";
	dev.type 		= DEVICE_TYPE_BLOCK;
	dev.ioctl 		= fdc_ioctl;
	dev.initialize 	= fdc_driver_init;
	dev.finalize 	= fdc_driver_fini;

	/* Mount driver */
	hr = vfs_mount_device(&dev, dev.default_url);
	if (FAILED(hr)) return hr;

	return S_OK;
}

HRESULT __nxapi fdc_uninstall()
{
	FDC_CTRL_CONTEXT 	*ctrl = &controller;
	HRESULT 			hr;

	if (!ctrl->ready) {
		return E_FAIL;
	}

	event_destroy(&ctrl->isr_event);
	mutex_destroy(&ctrl->lock);
	ctrl->ready = FALSE;

	/* Unregister IRQ handler */
	unregister_isr_callback(irq_to_intid(FDC_IRQ_ID));

	hr = vfs_unmount_device("/dev/fdd0");
	if (FAILED(hr)) return hr;

	return S_OK;
}

/*
 * Performs simple self test
 */
HRESULT fdc_selftest()
{
	IOCTL_STORAGE_READWRITE rw_desc;
	uint8_t					*buff = kmalloc(FDC_SECTOR_SIZE);
	K_STREAM 				*hdrv;
	HRESULT					hr;
	uint32_t				i;

	hr = k_fopen("/dev/fdd0", FILE_OPEN_READ, &hdrv);
	if (FAILED(hr)) {
		k_printf("fdc_selftest(): failed to open driver handle.");
		return hr;
	}

	/* Configure rw_desc */
	rw_desc.start = 0;
	rw_desc.count = 1;
	rw_desc.buffer= buff;

	/* Perform block read - read the boot sector */
	hr = k_ioctl(hdrv, IOCTL_STORAGE_READ_BLOCKS, &rw_desc);
	if (FAILED(hr))	{
		k_printf("fdc_selftest(): failed to read block via ioctl (hr=%x).", hr);
		goto finally;
	}

	/* Print first 128 bytes of boot sector */
	for (i=0; i<128; i++) {
		k_printf("%x ", (uint32_t)buff[i]);

		if (buff[i] <= 0xF) {
			k_printf(" ");
		}

		if (i%8 == 7) {
			k_printf("\n");
		}
	}

finally:
	kfree(buff);
	hr = k_fclose(&hdrv);

	return hr;
}
