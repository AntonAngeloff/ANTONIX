/*
 * ata.h
 *
 * 	Parallel ATA/ATAPI driver for ANTONIX.
 *
 *	TODO:
 *		- Dynamically allocate ATA controller IO ports (currently
 *			we assume that they are set to standard values by the BIOS).
 *
 *		- Support DMA transfer (currently we support PIO only).
 *
 *		- Support for Serial ATA/ATAPI.
 *
 *  Created on: 4.11.2016 ã.
 *      Author: Anton Angelov
 */
#include <types.h>
#include <syncobjs.h>
#include <stddef.h>

#ifndef DRIVERS_ATA_H_
#define DRIVERS_ATA_H_

#define ATA_PRIMARY_IRQ			14
#define ATA_SECONADRY_IRQ		15

#define ATA_READY_TIMEOUT       500
#define ATA_IRQ_TIMEOUT         500

#define ATA_SECTOR_SIZE			512
#define ATAPI_SECTOR_SIZE		2048

/* Register offsets */
#define ATA_REG_DATA			0x0
#define ATA_REG_ERROR			0x1
#define ATA_REG_FEATURES		0x1
#define ATA_REG_SEC_CNT         0x2
#define ATA_REG_LBA_LOW			0x3
#define ATA_REG_LBA_MID			0x4
#define ATA_REG_LBA_HIGH		0x5
#define ATA_REG_DRIVE			0x6
#define ATA_REG_STATUS          0x7
#define ATA_REG_COMMAND         0x7

#define ATA_REG_SECCOUNT1   	0x8
#define ATA_REG_LBA3      		0x9
#define ATA_REG_LBA4      		0xA
#define ATA_REG_LBA5      		0xB
#define ATA_REG_CONTROL      	0xC
#define ATA_REG_ALTSTATUS   	0xC
#define ATA_REG_DEVADDRESS   	0xD

/* Control registers offset */
#define ATA_CTL_REG_CONTROL     0x10
#define ATA_CTL_REG_ALT_STATUS  0x10

/* Status byte bits */
#define ATA_STATUS_ERR          (1 << 0)
#define ATA_STATUS_DRQ          (1 << 3)
#define ATA_STATUS_DF           (1 << 5)
#define ATA_STATUS_DRDY         (1 << 6)
#define ATA_STATUS_BSY          (1 << 7)

/* Commands */
#define ATA_CMD_READ_PIO  	    0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_PACKET          0xA0
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_IDENTIFY        0xEC

#define ATAPI_CMD_READ			0xA8
#define ATAPI_CMD_EJECT			0x1B

/* Notable register offsets from the identification space */
#define ATA_IDENT_DEVICETYPE   	0
#define ATA_IDENT_CYLINDERS   	2
#define ATA_IDENT_HEADS      	6
#define ATA_IDENT_SECTORS      	12
#define ATA_IDENT_SERIAL   		20
#define ATA_IDENT_MODEL      	54
#define ATA_IDENT_CAPABILITIES  98
#define ATA_IDENT_FIELDVALID   	106
#define ATA_IDENT_MAX_LBA   	120
#define ATA_IDENT_COMMANDSETS   164
#define ATA_IDENT_MAX_LBA_EXT   200

typedef enum {
	ATA_READ,
	ATA_WRITE
} ATA_OPERATION;

typedef enum {
	ATA_UNKNOWN,
	ATA_PIO,
	ATA_DMA
} ATA_TRANSFER_MODE;

typedef enum {
	ATA_LBA28 = 1,
	ATA_LBA48,
	ATA_CHS
} ATA_ADDRESSING_MODE;

typedef enum {
	ATA_TYPE_UNKNOWN,
	ATA_TYPE_PATA,
	ATA_TYPE_SATA,
	ATA_TYPE_PATAPI,
	ATA_TYPE_SATAPI,
} ATA_DEVICE_TYPE;

typedef struct ATA_CONTROLLER_CTX ATA_CONTROLLER_CTX;
struct ATA_CONTROLLER_CTX {
	uint32_t	id;

	K_MUTEX		lock;
	K_EVENT		irq_event;
	uint32_t	cmd_base;
	uint32_t	control_base;
	uint32_t	bm_base;
	uint32_t	drive;
	uint8_t		nIEN;

	/* Used when in DMA mode */
	uint32_t	dma_buf_size;
	uintptr_t	dma_buf_phys;
	void*		dma_buf;
};

typedef struct ATA_DEVICE_CONTEXT ATA_DEVICE_CONTEXT;
struct ATA_DEVICE_CONTEXT {
	ATA_CONTROLLER_CTX	*controller;
	ATA_TRANSFER_MODE	mode;
	ATA_ADDRESSING_MODE	adr_mode;

	uint32_t			drive_id;
	ATA_DEVICE_TYPE		type;

	/** Size in sectors */
	size_t				size;
	size_t				sector_size;

	uint8_t				ident_space[256];
	char				model[41];

	uint32_t			command_sets;
};

typedef struct ATA_REQUEST ATA_REQUEST;
struct ATA_REQUEST {
	ATA_TRANSFER_MODE	mode;
	ATA_OPERATION		operation;
	BOOL				ata;
	//TODO
};

HRESULT __nxapi ata_driver_init();
HRESULT __nxapi ata_driver_fini();
HRESULT __nxapi ata_driver_test(char *dev);

#endif /* DRIVERS_ATA_H_ */
