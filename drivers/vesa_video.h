/*
 * vesa_video.h
 *
 *	We will try to incorporate CORBA-like interfaces in this driver.
 *
 *  Created on: 1.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef DRIVERS_VESA_VIDEO_H_
#define DRIVERS_VESA_VIDEO_H_

#include <types.h>
#include <syncobjs.h>
#include <devices.h>
#include <devices_video.h>

typedef struct VESA_DRV_CONTEXT VESA_DRV_CONTEXT;
struct VESA_DRV_CONTEXT {
	/**
	 * Driver methods. By putting the virtual table at the beginning of the
	 * struct, we can just easily cast it to this struct, without offsetting.*/
	K_VIDEO_DRIVER_IFACE vtbl;

	/** Signifies if video card supports the Bochs Graphic Adapter (BGA)
	 * interface. This may be the case only when kernel is being emulated
	 * on virtual machines.
	 */
	BOOL				is_bga;

	/** Current video mode */
	K_VIDEO_MODE_DESC	mode;

	/** Mutex for framebuffer */
	K_MUTEX				lock;

	/**
	 * Physical address of linear frame buffer.
	 */
	uintptr_t			lfb_phys_addr;
	uintptr_t			lfb_addr;
	uint32_t			lfb_size;
};

HRESULT __nxapi vesa_install();

#endif /* DRIVERS_VESA_VIDEO_H_ */
