/*
 * floppy.h
 *
 *  Created on: 16.09.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef DRIVERS_FLOPPY_H_
#define DRIVERS_FLOPPY_H_

#include <types.h>
#include <stdint.h>
#include <devices.h>

#define FLOPPY_144_SECTORS_PER_TRACK 18

/* Time out for FIFO operations in milliseconds */
#define FDC_FIFO_TIMEOUT				200
#define FDC_RESET_TIMEOUT				200
#define FDC_SPINUP_TIMEOUT				500
#define FDC_SPINDOWN_TIMEOUT			500
#define FDC_SEEK_TIMEOUT				3000
#define FDC_DEFAULT_TIMEOUT				200
#define FDC_IRQ_TIMEOUT					500

#define FDC_SEEK_DELAY					15
#define FDC_DEFAULT_COMMAND_RETRIES		2

#define FDC_DMA_CHANNEL					2
#define FDC_SECTOR_SIZE					512
#define FDC_SECTOR_COUNT				2880

/* Due to the fact that we can't map memory less than 4kb size,
 * we have to use bigger DMA buffer size than the actual sector size.
 */
#define FDC_DMA_BUFFER_SIZE				4096

/* Define symbolic constants for registers */
#define REG_STATUS_A					0x03F0 //SRA (read-only)
#define REG_STATUS_B					0x03F1 //SRB (read-only)
#define REG_DIGITAL_OUTPUT				0x03F2
#define REG_TAPE_DRIVE					0x03F3
#define REG_MAIN_STATUS					0x03F4 //MSR (read-only)
#define REG_DATARATE_SELECT				0x03F4 //DSR (write-only)
#define REG_DIGITAL_INPUT				0x03F7 //DIR (read-only)
#define REG_CONFIGURATION_CONTROL		0x03F7 //CCR (write-only)
#define DATA_FIFO                       0x03F5

/* Installs the FDC driver */
HRESULT __nxapi fdc_install();

/* Uninstalls driver */
HRESULT __nxapi fdc_uninstall();

/* Performs simple self test */
HRESULT fdc_selftest();

#endif /* DRIVERS_FLOPPY_H_ */
