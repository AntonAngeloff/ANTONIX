/*
 * console.c
 *
 *  Created on: 14.07.2016 ã.
 *      Author: Admin
 */

/**
 * @brief - This console driver (/dev/console) is an experimental driver to
 * provide early access to the console for user-space applications at first
 * stage of kernel development.
 *
 * Probably it will be rewritten or removed later.
 *
 * Supports following function:
 * 		read - reads characters from the keyboard
 * 		write - writes characters to the screen
 */

#include "console.h"
#include "kstream.h"
#include "devices.h"
#include "../vga.h"
#include "ps2.h"
#include "mm.h"

/*
 * Writes a sequence of characters to the screen using the VGA's driver
 * vga_print() routine.
 */
static HRESULT con_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	char *buff = in_buf;
	K_CON_CONTEXT *ctx = str->opaque_data;
	UNUSED_ARG(ctx);

	/* Validate block size */
	if (block_size == 0) {
		return E_INVALIDARG;
	}

	if (buff[block_size-1] != '\0') {
		int i;
		char s[2] = {0, 0};

		for (i=0; i<(int32_t)block_size; i++) {
			s[0] = buff[i];
			vga_print(s);
		}

		if (bytes_written) *bytes_written = block_size;
		return S_OK;
	}

	vga_printf(buff);
	if (bytes_written) *bytes_written = block_size;

	return S_OK;
}

/*
 * Tries to read a sequence of characters from the keyboard buffer.
 * If the kbd buffer's length is less than block_size, it reads only
 * the available characters.
 */
static HRESULT con_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	K_CON_CONTEXT *ctx = str->opaque_data;
	UNUSED_ARG(ctx);

	uint8_t *b = out_buf;

	size_t i;
	for (i=0; i<block_size; i++) {
		HRESULT hr = readch(b++);

		if (FAILED(hr)) {
			/* Keyboard buffer exhausted */
			if (bytes_read) *bytes_read = i;
			return S_FALSE;
		}
	}

	if (bytes_read) *bytes_read = block_size;
	return S_OK;
}

/*
 * Executes driver-spcific commands.
 */
static HRESULT con_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	UNUSED_ARG(arg);

	switch (code) {
	case DEVIO_OPEN:
		/* Prevent sequental calls to DEVIO_OPEN */
		if (s->opaque_data != NULL) {
			return E_FAIL;
		}

		/* Handle device opening */
		s->opaque_data = kmalloc(sizeof(K_CON_CONTEXT));

		K_CON_CONTEXT *c = s->opaque_data;
		c->magic_value = CON_DRV_SIGNATURE;

		break;

	case DEVIO_CLOSE:
		/* Handle device closing */
		c = s->opaque_data;

		/* Validate signature */
		if (!c || c->magic_value != CON_DRV_SIGNATURE) {
			return E_FAIL;
		}

		kfree(s->opaque_data);
		s->opaque_data = NULL;

		break;

	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

/*
 * Describe the console driver in K_DEVICE structure
 */
K_DEVICE con_device = {
		.default_url = "/dev/console",
		.type = DEVICE_TYPE_CHAR,
		.read = con_read,
		.write = con_write,
		.ioctl = con_ioctl,
		.seek = NULL,
		.tell = NULL
};

HRESULT __nxapi con_initialize()
{
	return vfs_mount_device(&con_device, con_device.default_url);
}

HRESULT __nxapi con_uninitialize()
{
	return vfs_unmount_device(con_device.default_url);
}
