/*
 * devices_video.h
 *
 *  Created on: 3.10.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_DEVICES_VIDEO_H_
#define INCLUDE_DEVICES_VIDEO_H_

#include <devices.h>

/* Usage flags for K_VIDEO_DRIVER_VTBL->lock_fb() function */
#define VIDEO_LOCK_READ		0x01
#define VIDEO_LOCK_WRITE	0x02

/**
 * Pixel formats
 */
typedef enum {
	VIDEO_FORMAT_RGBA32,
	VIDEO_FORMAT_BGRA32
} K_VIDEO_FORMAT;

/**
 * Video mode descriptor.
 */
typedef struct K_VIDEO_MODE_DESC K_VIDEO_MODE_DESC;
struct K_VIDEO_MODE_DESC {
	uint32_t		width;
	uint32_t		height;
	uint32_t		bpp;
	uint32_t		stride;
	K_VIDEO_FORMAT	format;
};

/**
 * Functions exported by a graphics driver.
 */
typedef struct K_VIDEO_DRIVER_IFACE K_VIDEO_DRIVER_IFACE;
struct K_VIDEO_DRIVER_IFACE {
	/** Sets the video adapter in a particular mode. */
	HRESULT __nxapi (*set_mode)(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *mode_desc);

	/** Returns information about the current video mode. */
	HRESULT __nxapi (*get_mode)(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *mode_desc);

	/** Enumerates the supported video modes. */
	HRESULT __nxapi (*enum_modes)(K_VIDEO_DRIVER_IFACE *this, K_VIDEO_MODE_DESC *desc_arr, uint32_t *count);

	/**
	 * Locks the framebuffer, providing a memory pointer to the first
	 * scanline. Use VIDEO_LOCK_* symbolic constants for `lock_flags`.
	 */
	HRESULT __nxapi (*lock_fb)(K_VIDEO_DRIVER_IFACE *this, uint32_t lock_flags, void **ptr_out, uint32_t *stride_out);

	/** Unlocks the framebuffer. */
	HRESULT __nxapi (*unlock_fb)(K_VIDEO_DRIVER_IFACE *this);

	/**
	 * Switches to next framebuffer inside the framebuffer chain, which makes
	 * last modifications of the framebuffer visible.
	 */
	HRESULT __nxapi (*present)(K_VIDEO_DRIVER_IFACE *this, BOOL sync);
};

static inline uint32_t video_format_to_bpp(K_VIDEO_FORMAT fmt)
{
	switch (fmt) {
		case VIDEO_FORMAT_BGRA32:
		case VIDEO_FORMAT_RGBA32:
			return 32;

		default:
			return 0;
	}
}

#endif /* INCLUDE_DEVICES_VIDEO_H_ */
