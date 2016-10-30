/*
 * vesa_bga.c
 *
 *	Support for Bochs's BGA
 *
 *  Created on: 1.10.2016 ã.
 *      Author: Admin
 */

#include <hal.h>
#include "vesa_bga.h"
#include "pci_bus.h"

/*
 * Prototypes
 */
static HRESULT __nxapi bga_set_mode_inner(uint32_t width, uint32_t height, uint32_t bpp, BOOL lfb_mode);

/*
 * Implementation
 */
void __nxapi bga_write(uint32_t reg, uint16_t value)
{
	/* Select register index and write it's value */
	WRITE_PORT_USHORT(VBE_DISPI_IOPORT_INDEX, reg);
	WRITE_PORT_USHORT(VBE_DISPI_IOPORT_DATA, value);
}

uint16_t __nxapi bga_read(uint32_t reg)
{
	/* Select register */
	WRITE_PORT_USHORT(VBE_DISPI_IOPORT_INDEX, reg);

	/* Read data */
	return READ_PORT_USHORT(VBE_DISPI_IOPORT_DATA);
}

void __nxapi bga_enable_vbe(BOOL enable, uint32_t extra_flags)
{
	if (enable) {
		bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | extra_flags);
	} else {
		bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
	}
}

BOOL __nxapi bga_check_present()
{
	uint32_t id = bga_read(VBE_DISPI_INDEX_ID);
	return id == VBE_DISPI_ID4 || id == VBE_DISPI_ID5 ? TRUE : FALSE;
}

static HRESULT __nxapi bga_set_mode_inner(uint32_t width, uint32_t height, uint32_t bpp, BOOL lfb_mode)
{
	HRESULT hr = S_OK;

	/* Setting a video mode on BGA is quite simple,
	 * First we have to disable the VBE.
	 */
	bga_enable_vbe(FALSE, 0);

	/* Set registers */
	bga_write(VBE_DISPI_INDEX_XRES, width);
	bga_write(VBE_DISPI_INDEX_YRES, height);
	bga_write(VBE_DISPI_INDEX_BPP, bpp);

	/* Check if mode is set correctly */
	if (bga_read(VBE_DISPI_INDEX_XRES) != width ||
		bga_read(VBE_DISPI_INDEX_YRES) != height ||
		bga_read(VBE_DISPI_INDEX_BPP) != bpp)
	{
		hr = E_FAIL;
	}

	/* Re-enable VBE */
	bga_enable_vbe(TRUE, (lfb_mode == TRUE ? VBE_DISPI_LFB_ENABLED : 0) | VBE_DISPI_NOCLEARMEM);
	return hr;
}

HRESULT __nxapi bga_set_mode(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc)
{
	VESA_DRV_CONTEXT *vc = (VESA_DRV_CONTEXT*)this;
	HRESULT	hr;

	/* The caller is not required to fill bpp field */
	desc->bpp = video_format_to_bpp(desc->format);
	desc->stride = desc->width * desc->bpp / 8;

	hr = bga_set_mode_inner(desc->width, desc->height, desc->bpp, TRUE);
	if (FAILED(hr)) return hr;

	vc->mode = *desc;
	return S_OK;
}

HRESULT __nxapi bga_get_mode(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc)
{
	VESA_DRV_CONTEXT *vc = (VESA_DRV_CONTEXT*)this;

	if (vc->mode.width == 0 || vc->mode.height == 0) {
		return E_FAIL;
	}

	*desc = vc->mode;
	return S_OK;
}

HRESULT __nxapi bga_get_lfb_addr(uintptr_t *start_addr)
{
	/* The LFB address has to be retrieved from BGA's PCI BAR0 register.
	 * In case of ISA video card, we wil guess the below address value.
	 */
	*start_addr = 0xE0000000;
	return S_OK;
}

HRESULT __nxapi bga_select_bank(uint32_t bank_id)
{
	bga_write(VBE_DISPI_INDEX_BANK, bank_id);
	return bga_read(VBE_DISPI_INDEX_BANK) == bank_id ? S_OK : E_FAIL;
}

HRESULT __nxapi bga_enum_modes(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc_arr, uint32_t *count)
{
	K_VIDEO_MODE_DESC	*d;

	UNUSED_ARG(this);

	/*
	 * TODO: Add more modes. Many modes are supported anyway.
	 */
	if (count) {
		*count = 1;
	}

	if (desc_arr) {
		d = desc_arr;

		d->width 	= 1024;
		d->height 	= 768;
		d->bpp 		= 32;
		d->stride 	= d->width * d->bpp / 8;
		d->format	= VIDEO_FORMAT_BGRA32;
	}

	return S_OK;
}
