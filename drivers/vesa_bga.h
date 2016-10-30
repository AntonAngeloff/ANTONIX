/*
 * vesa_bga.h
 *
 *  Created on: 1.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef DRIVERS_VESA_BGA_H_
#define DRIVERS_VESA_BGA_H_

#include <types.h>
#include "vesa_video.h"

/* BGA has two 16-bit registers */
#define VBE_DISPI_IOPORT_INDEX 		0x1CE
#define VBE_DISPI_IOPORT_DATA		0x1CF

/* BGA registers */
#define VBE_DISPI_INDEX_ID 			0
#define VBE_DISPI_INDEX_XRES 		1
#define VBE_DISPI_INDEX_YRES 		2
#define VBE_DISPI_INDEX_BPP 		3
#define VBE_DISPI_INDEX_ENABLE 		4
#define VBE_DISPI_INDEX_BANK 		5
#define VBE_DISPI_INDEX_VIRT_WIDTH 	6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET 	8
#define VBE_DISPI_INDEX_Y_OFFSET 	9

/* BGA versions */
#define VBE_DISPI_ID0				0xB0C0
#define VBE_DISPI_ID1				0xB0C1
#define VBE_DISPI_ID2				0xB0C2
#define VBE_DISPI_ID3				0xB0C3
#define VBE_DISPI_ID4				0xB0C4
#define VBE_DISPI_ID5				0xB0C5

/* Bits per pixel (BPP) values */
#define VBE_DISPI_BPP_4 			0x04
#define VBE_DISPI_BPP_8 			0x08
#define VBE_DISPI_BPP_15 			0x0F
#define VBE_DISPI_BPP_16 			0x10
#define VBE_DISPI_BPP_24 			0x18
#define VBE_DISPI_BPP_32 			0x20

/* ENABLE register's options */
#define VBE_DISPI_DISABLED			0x00
#define VBE_DISPI_ENABLED			0x01
#define VBE_DISPI_GETCAPS			0x02
#define VBE_DISPI_8BIT_DAC			0x20
#define VBE_DISPI_LFB_ENABLED		0x40
#define VBE_DISPI_NOCLEARMEM		0x80

/**
 * Writes 16-bit value to an enumerated BGA register
 */
void __nxapi bga_write(uint32_t reg, uint16_t value);

/**
 * Reads 16-bit value from a BGA register
 */
uint16_t __nxapi bga_read(uint32_t reg);

/**
 * Enables or disables the VESA Bios Extension (VBE)
 */
void __nxapi bga_enable_vbe(BOOL enable, uint32_t extra_flags);

/**
 * Checks if BGA is present. Returns TRUE if it is.
 */
BOOL __nxapi bga_check_present();

/**
 * Retrieves the start address of the LFB
 */
HRESULT __nxapi bga_get_lfb_addr(uintptr_t *start_addr);

/**
 * Selects a bank (64kb page) which is accessed via address
 * 0x0xA0000
 */
HRESULT __nxapi bga_select_bank(uint32_t bank_id);

/**
 * Sets a particular video mode
 */
HRESULT __nxapi bga_set_mode(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc);
HRESULT __nxapi bga_get_mode(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc);
HRESULT __nxapi bga_enum_modes(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc_arr, uint32_t *count);

#endif /* DRIVERS_VESA_BGA_H_ */
