/*
 *  isa_dma.h
 *
 *	Defines routines for driving ISA DMA chip. We don't have
 *	unified interfaces for working with other types of DMA chips (like
 *	ACPI DMA, IDE, PCI and so on).
 *
 *	If we use ISA DMA, we will particularly refer to it using the interface
 *	defined here.
 *
 *  Created on: 3.09.2016 ã.
 *      Author: Admin
 */

#ifndef ISA_DMA_H_
#define ISA_DMA_H_

#include <stdint.h>
#include "types.h"

#define DMA_BUS_WIDTH_8BIT		0x08
#define DMA_BUS_WIDTH_16BIT		0x10
#define DMA_BUS_WIDTH_32BIT		0x20

#define DMA_TRANSFER_ON_DEMAND	0x00
#define DMA_TRANSFER_SINGLE		0x01
#define DMA_TRANSFER_BLOCK		0x02

#define DMA_TRANSFER_READ		0x00
#define DMA_TRANSFER_WRITE		0x01
#define DMA_TRANSFER_VERIFY		0x02

#define DMA_MODE_ON_DEMAND		0x00
#define DMA_MODE_SINGLE			0x01
#define DMA_MODE_BLOCK			0x02

/* Define ISA DMA channel descriptor */
typedef struct K_DMA_CHANNEL K_DMA_CHANNEL;
struct K_DMA_CHANNEL {
	uint32_t id;
	uint32_t bus_width;
	uint32_t in_use;
	uint32_t disabled;
};

/**
 * Initializes the isadma_ subsystem
 */
HRESULT isadma_initialize();

/**
 * Shuts down the subsystem
 */
HRESULT isadma_finalize();

/**
 * Configures and prepares particular channel for a data transfer.
 */
HRESULT isadma_open_channel(uint32_t channel, void *address, uint32_t count, uint32_t t_mode, uint32_t o_mode, uint32_t autoinit);

/**
 * Disables a channel.
 */
HRESULT isadma_close_channel(uint32_t channel);
//HRESULT isadm_alloc_buffer(size_t size);

#endif /* ISA_DMA_H_ */
