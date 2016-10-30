/*
 * vga.c
 *
 *  Created on: 12.08.2016 ã.
 *      Author: Admin
 */
#include "vga.h"
#include <hal.h>
#include <vfs.h>
#include <mm.h>
#include <string.h>

static inline K_VGA_DRIVER_CONTEXT *get_drv_ctx(K_STREAM *s)
{
	K_VFS_NODE *n = s->priv_data;
	return n->content;
}

static HRESULT vga_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	HRESULT hr = S_OK;
	mutex_lock(&str->lock);

	/* Retrieve driver's context to lock driver's mutex */
	K_VGA_DRIVER_CONTEXT *dc = get_drv_ctx(str);
	K_VGA_STREAM_CONTEXT *vga_ctx = str->opaque_data;

	mutex_lock(&vga_ctx->lock);
	mutex_lock(&dc->lock);

	/* If we potentially exceed vga buffer upper limit, we should constrain to just writing to the limit */
	size_t effective_size = (str->pos + block_size) > dc->vga_buff_size ? dc->vga_buff_size - (str->pos - block_size) : block_size;
	if (effective_size == 0) {
		hr = E_ENDOFSTR;
		goto finally;
	}

	/* Write to vga memory software mirror buffer */
	memcpy(dc->vga_buff_sw, in_buf, effective_size);

	/* Write to vga memory itself */
	memcpy(dc->vga_buff, in_buf, effective_size);

	/* Move stream position */
	str->pos += effective_size;

	/* Set *bytes_written */
	if (bytes_written) *bytes_written = effective_size;

finally:
	mutex_unlock(&dc->lock);
	mutex_unlock(&vga_ctx->lock);
	mutex_unlock(&str->lock);
	return hr;
}

static HRESULT vga_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	HRESULT hr = S_OK;
	mutex_lock(&str->lock);

	/* Retrieve driver's context to lock driver's mutex */
	K_VGA_DRIVER_CONTEXT *dc = get_drv_ctx(str);
	K_VGA_STREAM_CONTEXT *vga_ctx = str->opaque_data;

	mutex_lock(&vga_ctx->lock);
	mutex_lock(&dc->lock);

	/* If we potentially exceed vga buffer upper limit, we should constrain to just reading up until the limit */
	size_t effective_size = (str->pos + block_size) > dc->vga_buff_size ? dc->vga_buff_size - (str->pos - block_size) : block_size;
	if (effective_size == 0) {
		hr = E_ENDOFSTR;
		goto finally;
	}

	/* Read back from VGA's memory mirror buffer */
	memcpy(out_buf, dc->vga_buff_sw, effective_size);

	/* Move stream pos */
	str->pos += effective_size;

	/* Set *bytes_read */
	if (bytes_read) *bytes_read = effective_size;

finally:
	mutex_unlock(&dc->lock);
	mutex_unlock(&vga_ctx->lock);
	mutex_unlock(&str->lock);
	return hr;
}

static uint32_t vga_tell(K_STREAM *str)
{
	mutex_lock(&str->lock);
	uint32_t pos = str->pos;
	mutex_unlock(&str->lock);

	return pos;
}

static HRESULT vga_seek(K_STREAM *str, int64_t pos, int8_t origin)
{
	HRESULT hr = S_OK;
	mutex_lock(&str->lock);

	int32_t		new_pos;
	uint32_t	size;
	K_VGA_DRIVER_CONTEXT *ctx = get_drv_ctx(str);

	/* Lock for accessing vga_buff_size */
	mutex_lock(&ctx->lock);
	size = ctx->vga_buff_size;
	mutex_unlock(&ctx->lock);

	switch (origin) {
	case KSTREAM_ORIGIN_BEGINNING:
		new_pos = pos;
		break;

	case KSTREAM_ORIGIN_CURRENT:
		new_pos =  str->pos + pos;
		break;

	case KSTREAM_ORIGIN_END:
		new_pos = size - pos;
		break;

	default:
		hr = E_INVALIDARG;
		goto finally;
	}

	if (new_pos < 0 || new_pos > (int32_t)size) {
		hr = E_INVALIDARG;
		goto finally;
	}

finally:
	mutex_unlock(&str->lock);
	return hr;
}


static HRESULT vga_device_init(K_DEVICE *self)
{
	if (self->opaque) {
			return E_FAIL;
	}

	K_VGA_DRIVER_CONTEXT *c = kcalloc(sizeof(K_VGA_DRIVER_CONTEXT));

	self->opaque = c;
	mutex_create(&c->lock);

	return S_OK;
}

static HRESULT vga_device_fini(K_DEVICE *self)
{
	if (!self->opaque) {
		return E_FAIL;
	}

	K_VGA_DRIVER_CONTEXT *c = self->opaque;

	mutex_destroy(&c->lock);
	kfree(self->opaque);

	return S_OK;
}

static HRESULT vga_set_cursor_position(K_STREAM *s, VGA_CURSOR_POS *cp)
{
	K_VGA_DRIVER_CONTEXT *dc = get_drv_ctx(s);

	mutex_lock(&s->lock);

	if(dc->vga_cursor_update_cntr) {
		/* Not yet */
		mutex_unlock(&s->lock);
		return S_FALSE;
	}
	uint32_t pos = cp->y * dc->width + cp->x;

	/* Tell the VGA board we are setting the high cursor byte. */
	WRITE_PORT_UCHAR(0x3D4, 14);

	/* Send the high cursor byte. */
	WRITE_PORT_UCHAR(0x3D5, pos >> 8);

	/* Tell the VGA board we are setting the low cursor byte. */
	WRITE_PORT_UCHAR(0x3D4, 15);

	/* Send the low cursor byte. */
	WRITE_PORT_UCHAR(0x3D5, pos);

	mutex_unlock(&s->lock);
	return S_OK;
}

static HRESULT vga_update_rect(K_STREAM *str, VGA_UPDATE_RECT_DESC *desc)
{
	/* Updates a region on the screen. This updates both the characters and the
	 * color attributes which are extracted from two different buffers.
	 */
	HRESULT hr = S_OK;
	mutex_lock(&str->lock);

	K_VGA_DRIVER_CONTEXT *dc = get_drv_ctx(str);
	mutex_lock(&dc->lock);

	/* Validate input */
	if ((desc->x + desc->w) >= dc->width || (desc->y + desc->h) >= dc->height) {
		hr = E_INVALIDARG;
		goto finally;
	}

	if (!desc->char_buffer && !desc->col_buffer) {
		hr = E_INVALIDARG;
		goto finally;
	}

	if (desc->w == 0 || desc->h == 0) {
		hr = E_INVALIDARG;
		goto finally;
	}

	/* Begin update */
	for (uint32_t i=0; i<desc->w; i++) {
		for (uint32_t j=0; j<desc->h; j++) {
			uint32_t src_key = j * desc->w + i;
			uint32_t dst_key = (j+desc->y) * dc->width + (i+desc->x);

			if (desc->col_buffer == NULL) {
				/* Change only character */
				dc->vga_buff_sw[dst_key] = (dc->vga_buff_sw[dst_key] & 0xF0) | ((uint16_t)desc->char_buffer[src_key]);
			} else if (desc->char_buffer == NULL) {
				/* Update only color */
				dc->vga_buff_sw[dst_key] = (dc->vga_buff_sw[dst_key] & 0x0F) | ((uint16_t)desc->char_buffer[src_key] << 8);
			} else {
				/* Update both */
				dc->vga_buff_sw[dst_key] = vga_make_entry(desc->char_buffer[src_key], desc->col_buffer[src_key]);
			}

			/* Copy to video memory */
			dc->vga_buff[dst_key] = dc->vga_buff_sw[dst_key];
		}
	}

finally:
	mutex_unlock(&dc->lock);
	mutex_unlock(&str->lock);

	return hr;
}

HRESULT vga_clear_screen(K_STREAM *str)
{
	K_VGA_DRIVER_CONTEXT *dc = get_drv_ctx(str);

	mutex_lock(&dc->lock);

	uint32_t w = dc->width;
	uint32_t h = dc->height;

	//Reset all screen characters to whitespace
	for(size_t j=0; j<w; j++) {
		for(size_t i=0; i<h; i++) {
			uint32_t id = j * w + i;

			term_buff_sw[id] = vga_make_entry(' ', vga_make_color(dc->color_fg, dc->color_bg));
			term_buff[id] = term_buff_sw[id];
		}
	}

	dc->vga_pointer = 0;
	mutex_unlock(&dc->lock);

	return S_OK;
}

HRESULT vga_get_state(K_STREAM *stream, VGA_DEVICE_STATE *s)
{
	K_VGA_DRIVER_CONTEXT *dc = get_drv_ctx(stream);

	mutex_lock(&dc->lock);

	s->width = dc->width;
	s->height = dc->height;

	mutex_unlock(&dc->lock);

	return S_OK;
}

HRESULT vga_on_open(K_STREAM *s)
{
	/* Prevent sequential calls to DEVIO_OPEN */
	if (s->opaque_data != NULL) {
		return E_FAIL;
	}

	/* Handle device opening */
	s->opaque_data = kmalloc(sizeof(K_VGA_STREAM_CONTEXT));

	K_VGA_STREAM_CONTEXT *c = s->opaque_data;
	mutex_create(&c->lock);

	return S_OK;
}

HRESULT vga_on_close(K_STREAM *s)
{
	K_VGA_STREAM_CONTEXT *c = s->opaque_data;
	mutex_destroy(&c->lock);

	kfree(s->opaque_data);
	s->opaque_data = NULL;

	return S_OK;
}

/*
 * Executes driver-specific commands.
 */
static HRESULT vga_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	UNUSED_ARG(arg);

	switch (code) {
	case DEVIO_OPEN:
		return vga_on_open(s);

	case DEVIO_CLOSE:
		return vga_on_close(s);

	case IOCTL_VGA_CLEAR_SCREEN:
		return vga_clear_screen(s);

	case IOCTL_VGA_SET_CURSOR_POS:
		return vga_set_cursor_position(s, arg);

	case IOCTL_VGA_UPDATE_RECT:
		return vga_update_rect(s, arg);

	case IOCTL_VGA_GET_STATE:
		return vga_get_state(s, arg);

	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

/*
 * Describe the console driver in K_DEVICE structure
 */
K_DEVICE vga_device = {
		.default_url = "/dev/vga",
		.type = DEVICE_TYPE_BLOCK,
		.read = vga_read,
		.write = vga_write,
		.ioctl = vga_ioctl,
		.seek = vga_seek,
		.tell = vga_tell,
		.initialize = vga_device_init, //TODO: modify VFS to make it invoke those two cb's
		.finalize = vga_device_fini,
		.open = vga_on_open,
		.close = vga_on_close,
};

HRESULT __nxapi vga_initialize()
{
	return vfs_mount_device(&vga_device, vga_device.default_url);
}

HRESULT __nxapi vga_uninitialize()
{
	return vfs_unmount_device(vga_device.default_url);
}
