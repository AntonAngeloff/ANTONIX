/*
 * ata.c
 *
 *  Created on: 4.11.2016 ã.
 *      Author: Anton Angelov
 */
#include <hal.h>
#include <timer.h>
#include <devices.h>
#include <kstream.h>
#include <kstdio.h>
#include <vfs.h>
#include <string.h>
#include <desctables.h>
#include <mm.h>
#include "ata.h"

/*
 * Prototypes
 */
static HRESULT	__nxapi ata_get_controller(uint32_t controller_id, ATA_CONTROLLER_CTX **ctrl);
static HRESULT	__nxapi ata_wait_status(uint32_t controller_id, uint32_t timeout);
static HRESULT	__nxapi ata_wait_ex(uint32_t controller_id, uint32_t timeout);
static void		__nxapi ata_wait_4us(uint32_t controller_id);
static uint8_t	__nxapi ata_read_reg(ATA_CONTROLLER_CTX *ctrl, uint8_t reg);
static void		__nxapi ata_write_reg(ATA_CONTROLLER_CTX *ctrl, uint8_t reg, uint8_t value);
static void		__nxapi ata_enable_interrupts(ATA_CONTROLLER_CTX *ctrl, BOOL enable);

static HRESULT	__nxapi ata_software_reset(uint32_t controller_id);
static HRESULT	__nxapi ata_select_drive(ATA_CONTROLLER_CTX *ctrl, uint32_t new_drive_id);
static HRESULT	__nxapi ata_select_drive_ex(ATA_CONTROLLER_CTX *ctrl, uint32_t new_drive_id, ATA_ADDRESSING_MODE addr_mode, uint8_t head_value);
static HRESULT	__nxapi ata_detect_device(uint32_t controller_id, uint32_t drive_id, ATA_DEVICE_CONTEXT *target_dev);
static HRESULT	__nxapi ata_identify_signature(ATA_DEVICE_CONTEXT *target_dev);
static HRESULT	__nxapi ata_identify(ATA_DEVICE_CONTEXT *target_dev, void *dst_buffer);
static HRESULT	__nxapi ata_issue_atapi_identify(ATA_DEVICE_CONTEXT *target_dev);
static BOOL		__nxapi ata_is_packet_interface(ATA_DEVICE_CONTEXT *dc);
static HRESULT	__nxapi ata_rw_sectors(ATA_DEVICE_CONTEXT *dev, ATA_OPERATION op, uint64_t start, size_t count, void *buffer);
static HRESULT	__nxapi ata_rw_sectors_pio(ATA_DEVICE_CONTEXT *dev, ATA_OPERATION op, uint64_t start, size_t count, void *buffer);
static HRESULT	__nxapi ata_rw_sectors_dma(ATA_DEVICE_CONTEXT *dev, ATA_OPERATION op, uint64_t start, size_t count, void *buffer);
static HRESULT	__nxapi atapi_read_sectors(ATA_DEVICE_CONTEXT *dev, uint64_t start, size_t count, void *buffer);
static HRESULT	__nxapi atapi_eject(ATA_DEVICE_CONTEXT *dev);

static HRESULT	__nxapi ata_setup_buses();
static HRESULT	__nxapi ata_finalize_buses();
static HRESULT	__nxapi ata_create_device(uint32_t ctrl_id, uint32_t drv_id, K_DEVICE **dev);
static HRESULT	__nxapi ata_destroy_device(K_DEVICE **dev);

static VOID __cdecl irq14_isr(K_REGISTERS regs);
static VOID __cdecl irq15_isr(K_REGISTERS regs);

static HRESULT	__nxapi atadev_generate_url(char *prefix, char *dst);
static uint32_t	__nxapi atadev_get_sector_size(K_STREAM *s);
static uint32_t	__nxapi atadev_get_sector_count(K_STREAM *s);
static HRESULT	__nxapi atadev_ioctl(K_STREAM *s, uint32_t code, void *arg);
static HRESULT	__nxapi atadev_read_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args);
static HRESULT	__nxapi atadev_write_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args);

/*
 * Macros for locking and unlocking ATA controller.
 */
#define ATA_LOCK(ctrl) mutex_lock(&ctrl->lock)
#define ATA_UNLOCK(ctrl) mutex_unlock(&ctrl->lock)

static ATA_CONTROLLER_CTX __ata_buses[2];
static K_DEVICE *__ata_devices[4] = {0};

/*
 * Implementation
 */
static HRESULT __nxapi ata_setup_buses()
{
	/* Setup primary bus */
	memset(&__ata_buses[0], 0, sizeof(ATA_CONTROLLER_CTX));
	__ata_buses[0].id			= 0;
	__ata_buses[0].cmd_base		= 0x1F0;
	__ata_buses[0].control_base	= 0x3F6;
	__ata_buses[0].drive		= 0xFF;
	__ata_buses[0].nIEN			= 0xFF;
	mutex_create(&__ata_buses[0].lock);
	event_create(&__ata_buses[0].irq_event, EVENT_FLAG_AUTORESET);

	/* Setup secondary bus */
	memset(&__ata_buses[1], 0, sizeof(ATA_CONTROLLER_CTX));
	__ata_buses[1].id			= 1;
	__ata_buses[1].cmd_base		= 0x170;
	__ata_buses[1].control_base	= 0x376;
	__ata_buses[1].drive		= 0xFF;
	__ata_buses[1].nIEN			= 0xFF;
	mutex_create(&__ata_buses[1].lock);
	event_create(&__ata_buses[1].irq_event, EVENT_FLAG_AUTORESET);

	/* Disable interrupts */
	ata_enable_interrupts(&__ata_buses[0], FALSE);
	ata_enable_interrupts(&__ata_buses[1], FALSE);

	/* Select default drive */
	ata_select_drive(&__ata_buses[0], 0);
	ata_select_drive(&__ata_buses[1], 0);

	/* Register ISRs */
	register_isr_callback(irq_to_intid(ATA_PRIMARY_IRQ), irq14_isr, &__ata_buses[0]);
	register_isr_callback(irq_to_intid(ATA_SECONADRY_IRQ), irq15_isr, &__ata_buses[1]);

	return S_OK;
}

static HRESULT __nxapi ata_finalize_buses()
{
	mutex_destroy(&__ata_buses[0].lock);
	mutex_destroy(&__ata_buses[1].lock);

	event_destroy(&__ata_buses[0].irq_event);
	event_destroy(&__ata_buses[1].irq_event);

	unregister_isr_callback(irq_to_intid(ATA_PRIMARY_IRQ));
	unregister_isr_callback(irq_to_intid(ATA_SECONADRY_IRQ));

	return S_OK;
}

/**
 * Interrupt service routine for IRQ14 (related to Primary ATA bus)
 */
static VOID __cdecl irq14_isr(K_REGISTERS regs)
{
	UNUSED_ARG(regs);
	event_signal(&__ata_buses[0].irq_event);
}

/**
 * Interrupt service routine for IRQ15 (related to Secondary ATA bus)
 */
static VOID __cdecl irq15_isr(K_REGISTERS regs)
{
	UNUSED_ARG(regs);
	event_signal(&__ata_buses[1].irq_event);
}

/**
 * Waits 4 microseconds (400 nano seconds) by reading the alternate status
 * register 4 times.
 *
 * Note: Reading from ctrl->control_base actually reads the alternate status,
 * 		 which has the same value as the original status register, except
 * 		 it reading it doesn't influence the interrupt states.
 */
static void __nxapi ata_wait_4us(uint32_t controller_id)
{
	ATA_CONTROLLER_CTX *ctrl;

	if (controller_id >= 2) {
		return;
	}

	ctrl = &__ata_buses[controller_id];

//	READ_PORT_UCHAR(ctrl->control_base);
//	READ_PORT_UCHAR(ctrl->control_base);
//	READ_PORT_UCHAR(ctrl->control_base);
//	READ_PORT_UCHAR(ctrl->control_base);
	ata_read_reg(ctrl, ATA_REG_ALTSTATUS);
	ata_read_reg(ctrl, ATA_REG_ALTSTATUS);
	ata_read_reg(ctrl, ATA_REG_ALTSTATUS);
	ata_read_reg(ctrl, ATA_REG_ALTSTATUS);
}

static HRESULT __nxapi ata_wait_status(uint32_t controller_id, uint32_t timeout)
{
	ATA_CONTROLLER_CTX	*ctrl;
	uint8_t 			status;

	if (FAILED(ata_get_controller(controller_id, &ctrl))) {
		return E_FAIL;
	}

	ata_wait_4us(controller_id);

	/* Set fake timeout if 0 is given. */
	timeout = timeout == 0 ? 1 : timeout;

	while (timeout > 0) {
		status = ata_read_reg(ctrl, ATA_REG_STATUS);

		if ((status & ATA_STATUS_BSY) == 0) {
			/* If BSY flag is dropped, waiting is successful */
			return S_OK;
		}

		/* Wait 10ms */
		timer_sleep(10);
		timeout -= timeout >= 10 ? 10 : timeout;
	}

	return E_TIMEDOUT;
}

/**
 * Same as @ata_wait_status, except it fails if ERR or DF bits are
 * lifted or DRQ bit is down.
 */
static HRESULT	__nxapi ata_wait_ex(uint32_t controller_id, uint32_t timeout)
{
	ATA_CONTROLLER_CTX	*ctrl;
	uint8_t 			status;
	HRESULT				hr;

	if (FAILED(ata_get_controller(controller_id, &ctrl))) {
		return E_FAIL;
	}

	ata_wait_4us(controller_id);

	hr = ata_wait_status(controller_id, timeout);
	if (FAILED(hr)) return hr;

	/* Read status again */
	status = ata_read_reg(ctrl, ATA_REG_STATUS);

	if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) || !(status & ATA_STATUS_DRQ)) {
		return E_FAIL;
	}

	return S_OK;
}

static HRESULT __nxapi ata_get_controller(uint32_t controller_id, ATA_CONTROLLER_CTX **ctrl)
{
	if (controller_id >= 2) {
		return E_INVALIDARG;
	}

	*ctrl = &__ata_buses[controller_id];
	return S_OK;
}

/**
 * Performs a software resets on a ATA controller/bus. This resets
 * both drives on the bus.
 *
 * This routine resets the selected drive to master, so the caller
 * have to reselect destination drive after calling this routine.
 */
static HRESULT __nxapi ata_software_reset(uint32_t controller_id)
{
	ATA_CONTROLLER_CTX *ctrl;

	if (FAILED(ata_get_controller(controller_id, &ctrl))) {
		return E_FAIL;
	}

	ATA_LOCK(ctrl);

//	WRITE_PORT_UCHAR(ctrl->control_base, 0x04);
	ata_write_reg(ctrl, ATA_REG_CONTROL, 0x04);
	ata_wait_4us(ctrl->id);

//	WRITE_PORT_UCHAR(ctrl->control_base, 0x00);
	ata_write_reg(ctrl, ATA_REG_CONTROL, 0x00);
	ata_wait_4us(ctrl->id);

	ctrl->drive = 0;

	ATA_UNLOCK(ctrl);
	return S_OK;
}

/**
 * Selects active drive for a given ATA controller.
 */
static HRESULT __nxapi ata_select_drive(ATA_CONTROLLER_CTX *ctrl, uint32_t new_drive_id)
{
	HRESULT			hr = S_OK;

	/* Validate input args */
	if (new_drive_id >= 2) {
		return E_INVALIDARG;
	}

	/* Lock controller */
	ATA_LOCK(ctrl);

	if (ctrl->drive != new_drive_id) {
		WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_DRIVE, 0xA0 | new_drive_id << 4);
		ata_wait_4us(ctrl->id);

		/* Wait for BSY flag to drop in status register */
		hr = ata_wait_status(ctrl->id, 1000);

		if (SUCCEEDED(hr)) {
			/* Successfully selected */
			ctrl->drive = new_drive_id;
		}
	}

	/* Unlock controller */
	ATA_UNLOCK(ctrl);

	return hr;
}

/**
 * Same as @ata_select_drive, except it sets additional bits on the DRIVE register.
 */
static HRESULT	__nxapi ata_select_drive_ex(ATA_CONTROLLER_CTX *ctrl, uint32_t new_drive_id, ATA_ADDRESSING_MODE addr_mode, uint8_t head_value)
{
	HRESULT hr = S_OK;

	/* Validate input args */
	if (new_drive_id >= 2) {
		return E_INVALIDARG;
	}

	/* Lock controller */
	ATA_LOCK(ctrl);

	switch (addr_mode) {
		case ATA_LBA28:
		case ATA_LBA48:
			ata_write_reg(ctrl, ATA_REG_DRIVE, 0xA0 | (new_drive_id << 4) | head_value);
			break;

		case ATA_CHS:
			ata_write_reg(ctrl, ATA_REG_DRIVE, 0xE0 | (new_drive_id << 4) | head_value);
			break;

		default:
			hr = E_INVALIDARG;
			goto finally;
	}

	ata_wait_4us(ctrl->id);

	/* Wait for BSY flag to drop in status register */
	hr = ata_wait_status(ctrl->id, 10000);

	if (SUCCEEDED(hr)) {
		/* Successfully selected */
		ctrl->drive = new_drive_id;
	}

finally:
	ATA_UNLOCK(ctrl);
	return hr;
}

static HRESULT	__nxapi ata_detect_device(uint32_t controller_id, uint32_t drive_id, ATA_DEVICE_CONTEXT *target_dev)
{
	ATA_CONTROLLER_CTX	*ctrl;
	uint32_t			i;
	HRESULT				hr;
	char				*c;

	if (FAILED(ata_get_controller(controller_id, &ctrl))) {
		return E_INVALIDARG;
	}

	target_dev->controller	= ctrl;
	target_dev->drive_id	= drive_id;

	/* Perform software reset */
	hr = ata_software_reset(controller_id);
	if (FAILED(hr)) return hr;

	/* Select desired drive */
	hr = ata_select_drive(ctrl, drive_id);
	if (FAILED(hr)) return hr;

	/* Decide type of device by looking at it's signature */
	hr = ata_identify_signature(target_dev);
	if (FAILED(hr)) return hr;

	/* Identify */
	hr = ata_identify(target_dev, &target_dev->ident_space);
	if (FAILED(hr)) return hr;

	/* Extract valuable fields */
	target_dev->command_sets = *((uint32_t*)&target_dev->ident_space[ATA_IDENT_COMMANDSETS]);

	/* Get drive size */
	if (target_dev->command_sets & (1 << 27)) {
		/* 48-bit addressing */
		target_dev->size = *((uint32_t*)&target_dev->ident_space[ATA_IDENT_MAX_LBA_EXT]);
		target_dev->adr_mode = ATA_LBA48;
	} else {
		/* 28-bit addressing and CHS */
		target_dev->size = *((uint32_t*)&target_dev->ident_space[ATA_IDENT_MAX_LBA]);
		target_dev->adr_mode = ATA_LBA28;
	}

	target_dev->sector_size = ata_is_packet_interface(target_dev) ? ATAPI_SECTOR_SIZE : ATA_SECTOR_SIZE;

	/* Default to PIO mode, since we don't support DMA in driver */
	target_dev->mode = ATA_PIO;

	/* Get device model string. We have to reverse endianness */
	for (i=0; i<40; i+=2) {
		target_dev->model[i] = target_dev->ident_space[ATA_IDENT_MODEL + i + 1];
		target_dev->model[i+1] = target_dev->ident_space[ATA_IDENT_MODEL + i];
	}

	c = &target_dev->model[41];
	while (c > target_dev->model) {
		if (*(c-1) == ' ' || *(c-1) == '\0') {
			c--;
		} else {
			*c = '\0';
			break;
		}
	}

	return S_OK;
}

static HRESULT	__nxapi ata_identify_signature(ATA_DEVICE_CONTEXT *target_dev)
{
	uint8_t			cl, ch;

	/* Read LBA_LO and LBA_HI registers, which are form a signature */
//	cl = READ_PORT_UCHAR(target_dev->controller->cmd_base + ATA_REG_LBA_LOW);
//	ch = READ_PORT_UCHAR(target_dev->controller->cmd_base + ATA_REG_LBA_HIGH);
	cl = ata_read_reg(target_dev->controller, ATA_REG_LBA_MID);
	ch = ata_read_reg(target_dev->controller, ATA_REG_LBA_HIGH);

	if (cl == 0xFF && ch == 0xFF) {
		/* No device attached */
		target_dev->type = ATA_TYPE_UNKNOWN;

	} else if (cl == 0x14 && ch == 0xEB) {
		/* Parallel ATAPI */
		target_dev->type = ATA_TYPE_PATAPI;

	} else if (cl == 0x69 && ch == 0x96) {
		/* Serial ATAPI */
		target_dev->type = ATA_TYPE_SATAPI;

	} else if (cl == 0x00 && ch == 0x00) {
		/* Parallel ATA */
		target_dev->type = ATA_TYPE_PATA;

	} else if (cl == 0x3C && ch == 0x3C) {
		/* Serial ATA */
		target_dev->type = ATA_TYPE_SATA;

	} else {
		/* Unknown signature */
		target_dev->type = ATA_TYPE_UNKNOWN;
		return E_FAIL;
	}

	return S_OK;
}

static HRESULT	__nxapi ata_identify(ATA_DEVICE_CONTEXT *target_dev, void *dst_buffer)
{
	ATA_CONTROLLER_CTX	*ctrl = target_dev->controller;
	uint16_t			*dst_ptr = dst_buffer;
	uint32_t			i;
	HRESULT				hr;

//	/* Suspend interrupts */
//	ata_enable_interrupts(ctrl, FALSE);

	/* Select drive */
	hr = ata_select_drive(ctrl, target_dev->drive_id);
	if (FAILED(hr)) return hr;

	/* Issue identify command */
	WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	if (FAILED(ata_wait_status(ctrl->id, 100000))) return E_FAIL;

	/* Set sector count and lba registers to 0 */
	WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_SEC_CNT, 0);
	WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_LBA_LOW, 0);
	WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_LBA_MID, 0);
	WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_LBA_HIGH, 0);
	WRITE_PORT_UCHAR(ctrl->cmd_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

	/* Sleep for 1 ms */
	timer_sleep(1);

	/* If status register immediately goes to 0, then no device
	 * is attached.
	 */
	if (READ_PORT_UCHAR(ctrl->cmd_base + ATA_REG_STATUS) == 0) {
		return E_FAIL;
	}

	/* Wait for BSY flag to drop */
	while (TRUE) {
		//uint8_t status = READ_PORT_UCHAR(ctrl->cmd_base + ATA_REG_STATUS);
		uint8_t status = ata_read_reg(ctrl, ATA_REG_STATUS);

		if (status & ATA_STATUS_ERR) {
			/* Not ATA device. Probably ATAPI */
			hr = ata_issue_atapi_identify(target_dev);
			if (FAILED(hr)) return hr;

			/* Ready */
			break;
		}

		if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
			/* Ready */
			break;
		}
	}

	/* Read data */
	for (i=0; i<128; i++) {
		dst_ptr[i] = READ_PORT_USHORT(ctrl->cmd_base + ATA_REG_DATA);
	}

	return S_OK;
}

/**
 * Returns TRUE if the device is PATAPI/SATAPI device and false otherwise.
 */
static BOOL	__nxapi ata_is_packet_interface(ATA_DEVICE_CONTEXT *dc)
{
	return dc->type == ATA_TYPE_PATAPI || dc->type == ATA_TYPE_SATAPI ? TRUE : FALSE;
}

static HRESULT	__nxapi ata_issue_atapi_identify(ATA_DEVICE_CONTEXT *target_dev)
{
	if (target_dev->type != ATA_TYPE_PATAPI && target_dev->type != ATA_TYPE_SATAPI) {
		return E_FAIL;
	}

	//WRITE_PORT_UCHAR(target_dev->controller->cmd_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
	ata_write_reg(target_dev->controller, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);

	ata_wait_4us(target_dev->controller->id);
	timer_sleep(1);

	return S_OK;
}

static HRESULT	__nxapi ata_rw_sectors(ATA_DEVICE_CONTEXT *dev, ATA_OPERATION op, uint64_t start, size_t count, void *buffer)
{
	HRESULT hr;

	/* Lock the controller while we perform this operation */
	ATA_LOCK(dev->controller);

	/* Select drive */
	hr = ata_select_drive(dev->controller, dev->drive_id);
	if (FAILED(hr)) goto finally;

	/* Perform operation depending ot the transfer mode */
	if (dev->mode == ATA_DMA) {
		/* DMA mode */
		hr = ata_rw_sectors_dma(dev, op, start, count, buffer);
		if (FAILED(hr)) goto finally;

	} else if (dev->mode == ATA_PIO) {
		/* PIO mode */
		hr = ata_rw_sectors_pio(dev, op, start, count, buffer);
		if (FAILED(hr)) goto finally;

	} else {
		/* Unexpected */
		HalKernelPanic("ata_rw_sectors(): Invalid transfer mode.");
		hr = E_FAIL;
		goto finally;
	}

finally:
	ATA_UNLOCK(dev->controller);
	return hr;
}

static HRESULT	__nxapi atapi_eject(ATA_DEVICE_CONTEXT *dev)
{
	UNUSED_ARG(dev);
	UNUSED_ARG(atapi_eject);
	return E_NOTIMPL;
}

static HRESULT __nxapi atapi_read_sectors(ATA_DEVICE_CONTEXT *dev, uint64_t start, size_t count, void *buffer)
{
	HRESULT		hr = S_OK;
	uint32_t	i, j;
	uint16_t	*ptr = buffer;
	uint8_t		packet[12];

	if (count > 256) {
		return E_INVALIDARG;
	}

	ATA_LOCK(dev->controller);

	/* Only PIO mode is supported at this point */
	if (dev->mode != ATA_PIO) {
		hr = E_NOTIMPL;
		goto finally;
	}

	/* Enable interrupts */
	ata_enable_interrupts(dev->controller, TRUE);

	/* Construct SCSI packet */
	packet[0]  = ATAPI_CMD_READ;
	packet[1]  = 0;
	packet[2]  = (start >> 24) & 0xFF;
	packet[3]  = (start >> 16) & 0xFF;
	packet[4]  = (start >> 8) & 0xFF;
	packet[5]  = (start >> 0) & 0xFF;
	packet[6]  = 0x0;
	packet[7]  = 0x0;
	packet[8]  = 0x0;
	packet[9]  = count == 256 ? 0 : count;
	packet[10] = 0x0;
	packet[11] = 0x0;

	/* Select drive */
	hr = ata_select_drive(dev->controller, dev->drive_id);
	ata_write_reg(dev->controller, ATA_REG_DRIVE, dev->drive_id << 4);
	if (FAILED(hr)) goto finally;

	ata_wait_4us(dev->controller->id);

	if (dev->mode == ATA_PIO) {
		/* Switch to PIO mode */
		ata_write_reg(dev->controller, ATA_REG_FEATURES, 0);
	} else {
		//TODO
		HalKernelPanic("DMA mode not supported.");
	}

	/* Set buffer size */
	ata_write_reg(dev->controller, ATA_REG_LBA_MID, ATAPI_SECTOR_SIZE & 0xFF);
	ata_write_reg(dev->controller, ATA_REG_LBA_HIGH, ATAPI_SECTOR_SIZE >> 8);

	/* Send packet */
	ata_write_reg(dev->controller, ATA_REG_COMMAND, ATA_CMD_PACKET);

	hr = ata_wait_ex(dev->controller->id, 10000);
	if (FAILED(hr)) goto finally;

	for (i=0; i<6; i++) {
		WRITE_PORT_USHORT(dev->controller->cmd_base, ((uint16_t*)packet)[i]);
	}

	/* Receive data */
	for (i=0; i<count; i++) {
		/* Wait for IRQ to occur */
		hr = event_waitfor(&dev->controller->irq_event, 1000);
		if (FAILED(hr)) goto finally;

		/* Check status for error */
		hr = ata_wait_ex(dev->controller->id, 0);
		if (FAILED(hr)) goto finally;

		/* Read sector data */
		for (j=0; j<ATAPI_SECTOR_SIZE/2; j++) {
			*(ptr++) = READ_PORT_USHORT(dev->controller->cmd_base + ATA_REG_DATA);
		}
	}

	/* Wait for IRQ */
	hr = event_waitfor(&dev->controller->irq_event, 1000);
	if (FAILED(hr)) goto finally;

	/* Wait for BSY and DRQ flags to drop */
	while (ata_read_reg(dev->controller, ATA_REG_STATUS) & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) {
		;
	}

	/* Success */

finally:
	/* Disable IRQs for this ATA controller */
	ata_enable_interrupts(dev->controller, FALSE);

	ATA_UNLOCK(dev->controller);
	return hr;
}

static HRESULT	__nxapi ata_rw_sectors_pio(ATA_DEVICE_CONTEXT *dev, ATA_OPERATION op, uint64_t start, size_t count, void *buffer)
{
	uint32_t	i, j;
	HRESULT 	hr = S_OK;
	uint16_t	*ptr = buffer;
	uint8_t 	cmd;
	uint8_t 	head;
	uint8_t		params[6];

	if (count > 256) {
		/* This routine can read up to 256 sectors at once */
		return E_INVALIDARG;
	}

	if (dev->mode != ATA_PIO) {
		/* This routine handles PIO only */
		return E_FAIL;
	}

	/* We don't support CHS addressing mode, since it is obsolete */
	if (dev->adr_mode == ATA_CHS) {
		return E_FAIL;
	}

	/* Decide command byte */
	switch (dev->mode) {
		case ATA_LBA48:
			params[0] = (start & 0x000000FF) >> 0;
			params[1] = (start & 0x0000FF00) >> 8;
			params[2] = (start & 0x00FF0000) >> 16;
			params[3] = (start & 0xFF000000) >> 24;
			params[4] = 0; //todo
			params[5] = 0; //todo
			head      = 0;
			cmd		  = op == ATA_READ ? ATA_CMD_READ_PIO_EXT : ATA_CMD_WRITE_PIO_EXT;

			break;

		case ATA_LBA28:
			params[0] = (start & 0x000000FF) >> 0;
			params[1] = (start & 0x0000FF00) >> 8;
			params[2] = (start & 0x00FF0000) >> 16;
			params[3] = 0;
			params[4] = 0;
			params[5] = 0;
			head 	  = (start & 0xF000000) >> 24;
			cmd		  = op == ATA_READ ? ATA_CMD_READ_PIO : ATA_CMD_WRITE_PIO;

			break;

		default:
			return E_INVALIDARG;
	}

	/* Lock controller */
	ATA_LOCK(dev->controller);

	/* Disable interrupts for current ATA bus */
	ata_enable_interrupts(dev->controller, FALSE);

	/* Wait the device if it is busy */
	hr = ata_wait_status(dev->controller->id, 10000);
	if (FAILED(hr)) goto finally;

	/* Select drive */
	hr = ata_select_drive_ex(dev->controller, dev->drive_id, dev->mode, head);
	if (FAILED(hr)) goto finally;

	/* Write LBA48-related parameters (must be written first) */
	if (dev->adr_mode == ATA_LBA48) {
		ata_write_reg(dev->controller, ATA_REG_SECCOUNT1, 0);
		ata_write_reg(dev->controller, ATA_REG_LBA3, params[3]);
		ata_write_reg(dev->controller, ATA_REG_LBA4, params[4]);
		ata_write_reg(dev->controller, ATA_REG_LBA5, params[5]);
	}

	/* Write generic parameters */
	ata_write_reg(dev->controller, ATA_REG_SEC_CNT, count == 256 ? 0 : count);
	ata_write_reg(dev->controller, ATA_REG_LBA_LOW, params[0]);
	ata_write_reg(dev->controller, ATA_REG_LBA_MID, params[1]);
	ata_write_reg(dev->controller, ATA_REG_LBA_HIGH, params[2]);
	ata_write_reg(dev->controller, ATA_REG_COMMAND, cmd);

	switch (op) {
		case ATA_READ:
			for (i=0; i<count; i++) {
				hr = ata_wait_ex(dev->controller->id, 10000);
				if (FAILED(hr)) goto finally;

				for (j=0; j<ATA_SECTOR_SIZE/2; j++) {
					*(ptr++) = READ_PORT_USHORT(dev->controller->cmd_base + ATA_REG_DATA);
				}
			}

			break;

		case ATA_WRITE:
			for (i=0; i<count; i++) {
				hr = ata_wait_status(dev->controller->id, 10000);
				if (FAILED(hr)) goto finally;

				for (j=0; j<ATA_SECTOR_SIZE/2; j++) {
					WRITE_PORT_USHORT(dev->controller->cmd_base + ATA_REG_DATA, *(ptr++));
				}
			}

			/* Flush the cache buffer. */
			cmd = dev->adr_mode == ATA_LBA48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH;
			ata_write_reg(dev->controller, ATA_REG_COMMAND, cmd);
			ata_wait_status(dev->controller->id, 10000);

			break;

		default:
			hr = E_UNEXPECTED;
			goto finally;
	}

	/* Success */

finally:
	//ata_enable_interrupts(dev->controller, TRUE);
	ATA_UNLOCK(dev->controller);

	return hr;
}

/**
 * This routine allows a unified way to access (write) all the ATA-related
 * registers which are enumerated by the ATA_REG_* constants.
 */
static void	__nxapi ata_write_reg(ATA_CONTROLLER_CTX *ctrl, uint8_t reg, uint8_t value)
{
	BOOL shift = reg >= 0x08 && reg <= 0x0B ? TRUE : FALSE;

	if (shift) {
		/* This applies to SecCount1, LBA3, LBA4, LBA5 */
		ata_write_reg(ctrl, ATA_REG_CONTROL, 0x80 | ctrl->nIEN);
	}

	if (reg < 0x08) {
		/* Command registers (0x00 - 0x07) */
		WRITE_PORT_UCHAR(ctrl->cmd_base + reg, value);
	} else if (reg < 0x0C) {
		/* SecCount1, LBA3, LBA4, LBA5 (0x08 - 0x0B) */
		WRITE_PORT_UCHAR(ctrl->cmd_base + reg - 0x06, value);
	} else if (reg < 0x0E) {
		/* AltStatus, DevAddress registers (0x0C - 0x0D) */
		WRITE_PORT_UCHAR(ctrl->control_base + reg - 0x0A, value);
	} else if (reg < 0x16) {
		/* Bus mastering register (0x0E - 0x15) */
		WRITE_PORT_UCHAR(ctrl->bm_base + reg - 0x0E, value);
	}

	if (shift) {
	  ata_write_reg(ctrl, ATA_REG_CONTROL, ctrl->nIEN);
	}
}

static uint8_t __nxapi ata_read_reg(ATA_CONTROLLER_CTX *ctrl, uint8_t reg)
{
	uint8_t result;
	BOOL	shift = reg >= 0x08 && reg <= 0x0B ? TRUE : FALSE;

	if (shift) {
		ata_write_reg(ctrl, ATA_REG_CONTROL, 0x80 | ctrl->nIEN);
	}

	if (reg < 0x08) {
		result = READ_PORT_UCHAR(ctrl->cmd_base + reg - 0x00);
	} else if (reg < 0x0C) {
		result = READ_PORT_UCHAR(ctrl->cmd_base  + reg - 0x06);
	} else if (reg < 0x0E) {
		result = READ_PORT_UCHAR(ctrl->control_base + reg - 0x0A);
	} else if (reg < 0x16) {
		result = READ_PORT_UCHAR(ctrl->bm_base + reg - 0x0E);
	}

	if (shift) {
		ata_write_reg(ctrl, ATA_REG_CONTROL, ctrl->nIEN);
	}

	return result;
}

static void	__nxapi ata_enable_interrupts(ATA_CONTROLLER_CTX *ctrl, BOOL enable)
{
	uint8_t new_nIEN = enable ? 0x00 : 0x02;

	ATA_LOCK(ctrl);

	if (new_nIEN != ctrl->nIEN) {
		/* Put IRQ event to unsignaled state */
		event_reset(&ctrl->irq_event);
	}

	ata_write_reg(ctrl, ATA_REG_CONTROL, ctrl->nIEN = new_nIEN);

	ATA_UNLOCK(ctrl);
}

static HRESULT	__nxapi ata_rw_sectors_dma(ATA_DEVICE_CONTEXT *dev, ATA_OPERATION op, uint64_t start, size_t count, void *buffer)
{
	UNUSED_ARG(dev);
	UNUSED_ARG(op);
	UNUSED_ARG(start);
	UNUSED_ARG(count);
	UNUSED_ARG(buffer);

	HalKernelPanic("ata_rw_sectors(): DMA mode access routine not implemented.");
	return E_NOTIMPL;
}

static HRESULT __nxapi atadev_generate_url(char *prefix, char *dst)
{
	uint32_t	i;
	uint32_t	l = strlen(prefix);
	char		tmp[256];

	strcpy(tmp, prefix);
	tmp[l+1] = '\0';

	/* TODO: Expand to be able to test more than 10 ids */
	for (i=0; i<10; i++) {
		tmp[l] = '0' + i;

		if (!k_file_exists(tmp)) {
			strcpy(dst, tmp);
			return S_OK;
		}
	}

	return E_FAIL;
}

static uint32_t	__nxapi atadev_get_sector_size(K_STREAM *s)
{
	ATA_DEVICE_CONTEXT *dc = GET_DRV_CTX(s);
	return dc->sector_size;
}

static uint32_t	__nxapi atadev_get_sector_count(K_STREAM *s)
{
	ATA_DEVICE_CONTEXT *dc = GET_DRV_CTX(s);
	return dc->size;
}

static HRESULT	__nxapi atadev_read_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args)
{
	ATA_DEVICE_CONTEXT  *dc = GET_DRV_CTX(s);
	uint32_t			cnt = args->count;
	uint64_t			pos = args->start;
	uint32_t			effective_cnt;
	uint8_t				*ptr = args->buffer;
	HRESULT				hr;

	while (cnt > 0) {
		effective_cnt = cnt > 256 ? 256 : cnt;

		if (ata_is_packet_interface(dc)) {
			/* ATAPI */
			hr = atapi_read_sectors(dc, pos, effective_cnt, ptr);
			if (FAILED(hr)) return hr;
		} else {
			/* ATA */
			hr = ata_rw_sectors(dc, ATA_READ, pos, effective_cnt, ptr);
			if (FAILED(hr)) return hr;
		}

		pos += effective_cnt;
		ptr += effective_cnt * dc->sector_size;
		cnt -= effective_cnt;
	}

	return S_OK;
}

static HRESULT	__nxapi atadev_write_blocks(K_STREAM *s, IOCTL_STORAGE_READWRITE *args)
{
	ATA_DEVICE_CONTEXT  *dc = GET_DRV_CTX(s);
	uint32_t			cnt = args->count;
	uint64_t			pos = args->start;
	uint32_t			effective_cnt;
	uint8_t				*ptr;
	HRESULT				hr;

	while (cnt > 0) {
		effective_cnt = cnt > 256 ? 256 : cnt;

		if (ata_is_packet_interface(dc)) {
			/* ATAPI - writing to ATAPI drives is not supported by kernel. */
			return E_FAIL;
		} else {
			/* ATA */
			hr = ata_rw_sectors(dc, ATA_WRITE, pos, effective_cnt, ptr);
			if (FAILED(hr)) return hr;
		}

		pos += effective_cnt;
		ptr += effective_cnt * dc->sector_size;
		cnt -= effective_cnt;
	}

	return S_OK;
}

static HRESULT __nxapi atadev_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	switch (code) {
		case IOCTL_STORAGE_READ_BLOCKS:
			return atadev_read_blocks(s, arg);

		case IOCTL_STORAGE_WRITE_BLOCKS:
			return atadev_write_blocks(s, arg);

		case IOCTL_STORAGE_GET_BLOCK_SIZE:
			*(size_t*)arg = atadev_get_sector_size(s);
			break;

		case IOCTL_STORAGE_GET_BLOCK_COUNT:
			*(size_t*)arg = atadev_get_sector_count(s);
			break;

		case IOCTL_STORAGE_EJECT:
			return atapi_eject(GET_DRV_CTX(s));

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

static HRESULT	__nxapi ata_create_device(uint32_t ctrl_id, uint32_t drv_id, K_DEVICE **dev)
{
	HRESULT				hr;
	ATA_DEVICE_CONTEXT	*dc;
	K_DEVICE			*kd;

	/* Allocate kernel device struct */
	if (!(dc = kcalloc(sizeof(ATA_DEVICE_CONTEXT)))) {
		hr = E_OUTOFMEM;
		goto finally;
	}

	/* Allocate device context struct */
	if (!(kd = kcalloc(sizeof(K_DEVICE)))) {
		hr = E_OUTOFMEM;
		goto finally;
	}

	hr = ata_detect_device(ctrl_id, drv_id, dc);
	if (FAILED(hr)) goto finally;

	/* Populate kernel device struct */
	kd->class		= DEVICE_CLASS_STORAGE;
	kd->subclass	= ata_is_packet_interface(dc) ? DEVICE_SUBCLASS_CDROM : DEVICE_SUBCLASS_HARDDISK;
	kd->type		= DEVICE_TYPE_BLOCK;
	kd->opaque		= dc;
	kd->ioctl		= atadev_ioctl;
	kd->default_url = ata_is_packet_interface(dc) ? "/dev/cdrom" : "/dev/hd";

	hr = atadev_generate_url(kd->default_url, kd->default_url);

finally:
	if (FAILED(hr)) {
		if (dc) {
			kfree(dc);
			dc = NULL;
		}

		if (kd) {
			kfree(kd);
			kd = NULL;
		}
	}

	*dev = kd;
	return hr;
}

static HRESULT	__nxapi ata_destroy_device(K_DEVICE **dev)
{
	K_DEVICE *d = *dev;

	/* Unmount */
	vfs_unmount_device(d->default_url);

	/* Free */
	kfree(d->opaque);
	kfree(dev);

	*dev = NULL;
	return S_OK;
}

HRESULT __nxapi ata_driver_init()
{
	uint32_t	i, j;
	HRESULT		hr;
	char		*bus_names[2] = {"Primary", "Secondary"};
	char 		*drive_names[2] = {"master", "slave"};
	char		*proto_name[2] = {"ATA", "ATAPI"};
	K_DEVICE	*dev;

	hr = ata_setup_buses();
	if (FAILED(hr)) return hr;

	/* Detect drives and create devices */
	for (i=0; i<2; i++) {
		for (j=0; j<2; j++) {
			k_printf("%s bus/%s: ", bus_names[i], drive_names[j]);

			hr = ata_create_device(i, j, &dev);
			if (SUCCEEDED(hr)) {
				/* Mount device */
				hr = vfs_mount_device(dev, dev->default_url);
				if (FAILED(hr)) return hr;

				ATA_DEVICE_CONTEXT *dc = dev->opaque;

				/* Print device info */
				if (!ata_is_packet_interface(dc)) {
					k_printf("%s - %dmb; %s (mounted at '%s')\n", dc->model, dc->size * dc->sector_size / (1024*1024), proto_name[ata_is_packet_interface(dc) ? 1:0], dev->default_url);
				} else {
					k_printf("%s; %s (mounted at '%s')\n", dc->model, proto_name[ata_is_packet_interface(dc) ? 1:0], dev->default_url);
				}

				/* Keep reference to the device */
				__ata_devices[i*2 + j] = dev;
			} else {
				k_printf("none\n");
			}
		}
	}

	return S_OK;
}

HRESULT __nxapi ata_driver_fini()
{
	uint32_t	i, j;

	/* Unmount and free devices */
	for (i=0; i<2; i++) {
		for (j=0; j<2; j++) {
			if (__ata_devices[i*2 + j] == NULL) {
				continue;
			}

			ata_destroy_device(&__ata_devices[i*2 + j]);
		}
	}

	/* Finalize buses */
	ata_finalize_buses();

	return S_OK;
}

HRESULT __nxapi ata_driver_test(char *dev)
{
	IOCTL_STORAGE_READWRITE rw_desc;
	uint8_t					*buff = NULL;
	K_STREAM 				*hdrv;
	HRESULT					hr;
	uint32_t				i;
	uint32_t				bs;

	hr = k_fopen(dev, FILE_OPEN_READ, &hdrv);
	if (FAILED(hr)) {
		k_printf("ata_driver_test(): failed to open driver handle.");
		return hr;
	}

	/* Get block size */
	hr = storage_get_block_size(hdrv, &bs);
	if (FAILED(hr)) {
		k_printf("ata_driver_test(): failed to retrieve block size.");
		return hr;
	}

	k_printf("Block size: %d bytes\n", bs);

	buff = kmalloc(bs);

	/* Configure rw_desc */
	rw_desc.start = 0x148800 / bs;
	rw_desc.count = 1;
	rw_desc.buffer= buff;

	/* Perform block read - read the boot sector */
	hr = k_ioctl(hdrv, IOCTL_STORAGE_READ_BLOCKS, &rw_desc);
	if (FAILED(hr))	{
		k_printf("ata_driver_test(): failed to read block via ioctl (hr=%x).", hr);
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
