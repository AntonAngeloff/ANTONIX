/*
 * devices.h
 *
 *	Contains definitions related to ANOTNIX devices and device drivers.
 *
 *  Created on: 13.11.2015 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_DEVICES_H_
#define INCLUDE_DEVICES_H_

#include "types.h"
#include "kstream.h"
#include <ioctl_def.h>

/* Define device types */
#define DEVICE_TYPE_CHAR	FS_NODE_TYPE_CHARDEVICE
#define DEVICE_TYPE_BLOCK	FS_NODE_TYPE_BLOCKDEVICE

/* Device classes */
typedef enum {
	DEVUCE_CLASS_UNKNOWN = 0,
	DEVICE_CLASS_AUDIO,
	DEVICE_CLASS_GRAPHICS,
	DEVICE_CLASS_STORAGE,
	DEVICE_CLASS_POINTING
} K_DEVICE_CLASS;

typedef enum {
	DEVICE_SUBCLASS_NONE = 0,
	DEVICE_SUBCLASS_FLOPPY,
	DEVICE_SUBCLASS_MOUSE
} K_DEVICE_SUBCLASS;

/**
 * Describes a device driver. This structure is also used when mounting a driver
 * to the virtual file system.
 *
 * Routine pointers duplicate those contained in K_STREAM structure.
 */
typedef struct K_DEVICE K_DEVICE;
struct K_DEVICE {
	char 				*default_url;
	uint32_t 			type;

	K_DEVICE_CLASS 		class;
	K_DEVICE_SUBCLASS	subclass;

	void 				*opaque;

	/* Following routines should be implemented on all device drivers */
	HRESULT (*read)(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);
	HRESULT (*write)(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
	HRESULT (*ioctl)(K_STREAM *s, uint32_t code, void *arg);

	/* seek an tell are implemented only for block devices */
	uint32_t (*seek)(K_STREAM *str, int64_t pos, int8_t origin);
	uint32_t (*tell)(K_STREAM *str);

	HRESULT (*initialize)(K_DEVICE *self);
	HRESULT (*finalize)(K_DEVICE *self);
	HRESULT (*open)(K_STREAM *str);
	HRESULT (*close)(K_STREAM *str);
};

/* Storage driver helper functions */
HRESULT storage_read_blocks(K_STREAM *drv, uint32_t start, uint32_t count, void *buffer);
HRESULT storage_write_blocks(K_STREAM *drv, uint32_t start, uint32_t count, void *buffer);
HRESULT storage_get_block_size(K_STREAM *drv, size_t *size);
HRESULT storage_get_block_count(K_STREAM *drv, size_t *count);

#endif /* INCLUDE_DEVICES_H_ */
