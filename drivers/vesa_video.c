/*
 * vesa_video.c
 *
 *  Created on: 1.10.2016 ã.
 *      Author: Anton Angelov
 */

#include <hal.h>
#include <devices.h>
#include <vfs.h>
#include <string.h>
#include <mm_virt.h>
#include <mm.h>
#include "vesa_video.h"
#include "vesa_bga.h"
#include "pci_bus.h"

/*
 * Prototypes
 */
static HRESULT vesa_lock_fb(K_VIDEO_DRIVER_IFACE *this, uint32_t lock_flags, void **ptr_out, uint32_t *stride_out);
static HRESULT vesa_unlock_fb(K_VIDEO_DRIVER_IFACE *this);
static HRESULT __nxapi vesa_present(K_VIDEO_DRIVER_IFACE *this, BOOL sync);

static HRESULT vesa_init(K_DEVICE *device);
static HRESULT vesa_fini(K_DEVICE *dev);
static HRESULT vesa_ioctl(K_STREAM *s, uint32_t code, void *arg);

/*
 * Implementations
 */
static HRESULT vesa_pci_scan_callback(K_PCI_CONFIG_ADDRESS addr, uint32_t vendor_id, uint32_t device_id, void *user)
{
	/*
	 * Here we assume there is only one BGA/VBGA adapter attached.
	 * Which is most likely the case.
	 */
	if ((vendor_id == 0x1234 && device_id == 0x1111) ||
		(vendor_id == 0x80EE && device_id == 0xBEEF))
	{
		/* BGA or VirtualBox Graphics Device */
		*(K_PCI_CONFIG_ADDRESS*)user = addr;
	}

	return S_OK;
}

static HRESULT vesa_init(K_DEVICE *device)
{
	VESA_DRV_CONTEXT 	*drv;
	HRESULT				hr;

	if (!(drv = kcalloc(sizeof(VESA_DRV_CONTEXT)))) {
		return E_OUTOFMEM;
	}
	device->opaque = drv;

	drv->is_bga = bga_check_present();

	if (!drv->is_bga) {
		/* Assign pure VBE-based implementation */
		HalKernelPanic("Pure VESA not supported.");
	} else {
		/* Assign BGA implementation */
		drv->vtbl.set_mode 	= bga_set_mode;
		drv->vtbl.get_mode 	= bga_get_mode;
		drv->vtbl.enum_modes= bga_enum_modes;

		/* Scan PCI bus to find BAR0 register, which is where
		 * the LFB address is stored.
		 */
		K_PCI_SCAN_PARAMS scan_params;
		K_PCI_CONFIG_ADDRESS addr = { 0 };

		scan_params.cb = vesa_pci_scan_callback;
		scan_params.user = &addr;
		scan_params.class_selector = PCI_CLASS_SELECTOR_ALL;
		scan_params.subclass_selector = PCI_SUBCLASS_SELECTOR_ALL;

		/*
		 * Modify addr to know later if it has been changed (i.e. device found).
		 * If PCI device is found, it is guaranteed that zero_bits will be zeroed.
		 */
		addr.zero_bits = 0x3;

		hr = pci_scan(&scan_params);

		if (FAILED(hr) || addr.zero_bits == 0x3) {
			/* Probably ISA video card on Bochs */
			bga_get_lfb_addr(&drv->lfb_phys_addr);
			drv->lfb_size = 0xFF0000;
		} else {
			/* Read BAR0 */
			pci_read_config_u32(MAKE_CONFIG_ADDRESS(addr.bus_id, addr.device_id, addr.function_id, PCI_BAR0), &drv->lfb_phys_addr);
			drv->lfb_phys_addr &= 0xFFFFFFF0;
			drv->lfb_size = 0xFF0000;
		}
	}

	drv->vtbl.lock_fb	= vesa_lock_fb;
	drv->vtbl.unlock_fb	= vesa_unlock_fb;
	drv->vtbl.present	= vesa_present;

	/* Create mutex */
	mutex_create(&drv->lock);

	/* Map framebuffer to virtual memory */
	drv->lfb_addr = vmm_get_address_space_end(NULL);

	hr = vmm_map_region(NULL, drv->lfb_phys_addr, drv->lfb_addr, drv->lfb_size, USAGE_DATA, ACCESS_READWRITE, 1);
	if (FAILED(hr)) return hr;

	return S_OK;
}

static HRESULT vesa_fini(K_DEVICE *dev)
{
	VESA_DRV_CONTEXT *ctx = dev->opaque;
	HRESULT hr;

	/* Unmap LFB */
	hr = vmm_unmap_region(NULL, (uintptr_t)ctx->lfb_addr, 0);
	kfree(dev->opaque);

	return hr;
}

static HRESULT vesa_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	VESA_DRV_CONTEXT *ctx = GET_DRV_CTX(s);

	switch (code) {
		/*
		 * We have to handle these, since they are sent from
		 * VFS when device is being opened.
		 */
		case IOCTL_DEVICE_OPEN:
		case IOCTL_DEVICE_CLOSE:
			return S_OK;

		/*
		 * Return pointer to video driver methods
		 */
		case IOCTL_GRAPHICS_GET_INTERFACE:
			*(K_VIDEO_DRIVER_IFACE**)arg = &ctx->vtbl;
			return S_OK;
	}

	return E_NOTSUPPORTED;
}

static HRESULT vesa_lock_fb(K_VIDEO_DRIVER_IFACE *this, uint32_t lock_flags, void **ptr_out, uint32_t *stride_out)
{
	VESA_DRV_CONTEXT 	*vc = (VESA_DRV_CONTEXT*)this;

	UNUSED_ARG(lock_flags);
	mutex_lock(&vc->lock);

	*ptr_out = (void*)vc->lfb_addr;
	*stride_out = vc->mode.stride;

	return S_OK;
}

static HRESULT vesa_unlock_fb(K_VIDEO_DRIVER_IFACE *this)
{
	VESA_DRV_CONTEXT 	*vc = (VESA_DRV_CONTEXT*)this;

	mutex_unlock(&vc->lock);
	return S_OK;
}

static HRESULT __nxapi vesa_present(K_VIDEO_DRIVER_IFACE *this, BOOL sync)
{
	/*
	 * We can implement this later if we use double buffering (framebuffer chain).
	 */
	UNUSED_ARG(this);
	UNUSED_ARG(sync);

	return S_OK;
}

HRESULT __nxapi vesa_install()
{
	K_DEVICE 	dev;
	HRESULT		hr;

	memset(&dev, 0, sizeof(dev));

	dev.default_url = "/dev/video0";
	dev.type		= DEVICE_TYPE_BLOCK;
	dev.class		= DEVICE_CLASS_GRAPHICS;
	dev.subclass	= DEVICE_SUBCLASS_NONE;
	dev.initialize 	= vesa_init;
	dev.finalize 	= vesa_fini;
	dev.ioctl		= vesa_ioctl;

	/* Mount device */
	hr = vfs_mount_device(&dev, dev.default_url);
	return hr;
}
