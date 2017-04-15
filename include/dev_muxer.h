/*
 * dev_muxer.h
 *
 *	Device Multiplexor
 *
 *  Created on: 13.04.2017 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_DEV_MUXER_H_
#define INCLUDE_DEV_MUXER_H_

#include <types.h>
#include <kstdio.h>

#define DEV_MUXER_MAX_DEVICES 	16
#define DEV_MUXER_INVALID_SLOT	DEV_MUXER_MAX_DEVICES

typedef enum {
	DEVMUX_DIRECTION_INPUT,
	DEVMUX_DIRECTION_OUTPUT,
	DEVMUX_DIRECTION_BIDIRECTION,
} K_DEVMUX_DIRECTION;

/**
 * Device descriptor
 */
typedef struct K_DEV_DESC K_DEV_DESC;
struct K_DEV_DESC {
	uint32_t		ref_count;
	BOOL			initialized;
	K_STREAM		*stream;
	K_MUTEX			lock;
	K_EVENT			activation_ev;
	K_EVENT			destruction_ev;
	void			*muxer;
};

/**
 * Device multiplexer
 */
typedef struct K_DEV_MUXER K_DEV_MUXER;
struct K_DEV_MUXER {
	K_DEVMUX_DIRECTION	direction;
	uint32_t		active_slot;
	K_DEV_DESC		prim_device;
	BOOL			prim_auto_close;
	K_DEV_DESC		sec_devices[DEV_MUXER_MAX_DEVICES];
	uint32_t		sec_device_count;
	K_MUTEX			sec_devices_lock;
};

/**
 * Virtual stream used to forward stream calls.
 */
typedef struct K_DEV_STREAM K_DEV_STREAM;
struct K_DEV_STREAM {
	uint32_t	slot;
	K_STREAM	stream;
	K_DEV_MUXER	*dm;
};

/**
 * Creates new device multiplexer object using primary device from the virtual
 * file system, denoted by `primary_device`. For it's internal use, this
 * constructor opens a stream to the primary device and closes it upon it's
 * destruction.
 */
K_DEV_MUXER	__nxapi	*devmux_create(K_DEVMUX_DIRECTION dir, char	*primary_device);

/**
 * Creates new device multiplexer object using opened stream.
 */
K_DEV_MUXER	__nxapi	*devmux_create2(K_DEVMUX_DIRECTION dir, K_STREAM *stream);

/**
 * Device multiplexer destructor.
 */
VOID		__nxapi	devmux_destroy(K_DEV_MUXER *dm);

/**
 * Adds new secondary device. Passing `slot_id` of -1 will choose a slot automatically.
 */
HRESULT		__nxapi devmux_add_device(K_DEV_MUXER *dm, int32_t slot_id, uint32_t *slot_id_out, K_STREAM **stream_out);

/**
 * Removes a secondary device. In order this function to succeed, the device at that
 * particular slot must be inactive.
 */
HRESULT		__nxapi devmux_remove_device(K_DEV_MUXER *dm, uint32_t slot_id);

/**
 * Retrieves pointer to a secondary device's stream descriptor.
 */
HRESULT		__nxapi devmux_get_device_stream(K_DEV_MUXER *dm, uint32_t slot_id, K_STREAM **stream_out);

/**
 * Switches the active secondary device to the one pointer by `slot`.
 */
HRESULT		__nxapi devmux_switch_to(K_DEV_MUXER *dm, uint32_t slot);

/**
 * Performs basic testing on the device multiplexer object.
 */
HRESULT		__nxapi devmux_test();

#endif /* INCLUDE_DEV_MUXER_H_ */
