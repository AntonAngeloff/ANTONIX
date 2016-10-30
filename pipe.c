/*
 * pipe.c
 *
 *  Created on: 4.08.2016 ã.
 *      Author: Anton Angeloff
 */

#include "pipe.h"
#include "vfs.h"

static HRESULT pipe_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	K_VFS_NODE 	*node = str->priv_data;
	K_DEVICE 	*dev  = node->content;
	K_PIPE_DESC *desc = dev->opaque;

	/* Lock pipe's mutex */
	mutex_lock(&desc->lock);

	/* Do we have enough data to read? */
	uint32_t avail_bytes = desc->read_pos > desc->write_pos ?
					desc->buffer_size - (desc->read_pos - desc->write_pos) :
					desc->write_pos - desc->read_pos;

	if (avail_bytes < block_size) {
		/* Not enough bytes available. */
		if (bytes_read) *bytes_read = 0;
		mutex_unlock(&desc->lock);

		return E_FAIL;
	}

	uint8_t overlap = desc->read_pos + block_size > desc->buffer_size ? TRUE : FALSE;

	if (overlap) {
		uint32_t	s1 = desc->buffer_size - desc->read_pos;
		uint32_t	s2 = block_size - s1;
		uint8_t		*out = out_buf;

		memcpy(out, desc->ring_buffer + desc->read_pos, s1);
		memcpy(out + s1, desc->ring_buffer, s2);

		/* Update read index */
		desc->read_pos = s2;
	} else {
		memcpy(out_buf, desc->ring_buffer + desc->read_pos, block_size);
		desc->read_pos += block_size;
	}

	mutex_unlock(&desc->lock);
	if (bytes_read) *bytes_read = block_size;

	return S_OK;
}

/*
 * Tries to write _block_size_ bytes to the pipe. If the buffer cannot accommodate them
 * then the function fails while writing 0 bytes.
 */
static HRESULT pipe_write(K_STREAM *str, const size_t block_size, void *in_buf, size_t *bytes_written)
{
	K_VFS_NODE 	*node = str->priv_data;
	K_DEVICE 	*dev  = node->content;
	K_PIPE_DESC *desc = dev->opaque;

	/* Lock pipe's mutex */
	mutex_lock(&desc->lock);

	/* Do we have enough space? */
	uint32_t free_size = desc->write_pos >= desc->read_pos ?
					desc->buffer_size - (desc->write_pos - desc->read_pos) :
					desc->read_pos - desc->write_pos;

	/* We should always keep 1 byte difference between two
	 * positions.
	 */
	free_size--;

	if (free_size < block_size) {
		/* No space to write. */
		if (bytes_written) *bytes_written = 0;
		mutex_unlock(&desc->lock);

		return E_FAIL;
	}

	uint8_t overlap = block_size > (desc->buffer_size - desc->write_pos) ? TRUE : FALSE;

	if (overlap) {
		/* Copy in two iterations */
		uint32_t 	s1 = desc->buffer_size - desc->write_pos;
		uint32_t 	s2 = block_size - s1;
		uint8_t		*in = in_buf;

		memcpy(desc->ring_buffer + desc->write_pos, in, s1);
		memcpy(desc->ring_buffer, in + s1, s2);

		/* Update write index */
		desc->write_pos = s2;
	} else {
		/* Copy at once */
		uint32_t 	s1 = block_size;

		memcpy(desc->ring_buffer + desc->write_pos, in_buf, s1);

		/* Update write index */
		desc->write_pos += s1;
	}

	mutex_unlock(&desc->lock);
	if (bytes_written) *bytes_written = block_size;

	return S_OK;
}

static HRESULT destroy_pipe_desc(K_PIPE_DESC **desc)
{
	K_PIPE_DESC *d = *desc;

	kfree(d->ring_buffer);
	mutex_destroy(&d->lock);

	kfree(d);
	*desc = NULL;

	return S_OK;
}

static HRESULT pipe_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	K_VFS_NODE 	*node = s->priv_data;
	K_DEVICE 	*dev  = node->content;
	K_PIPE_DESC *desc = dev->opaque;

	UNUSED_ARG(arg);

	switch (code) {
	case DEVIO_OPEN:
		desc->ref_cnt++;
		break;

	case DEVIO_CLOSE:
		if ((desc->ref_cnt--) == 0) {
			if (desc->flags == PIPE_FLAG_DELETE_ON_CLOSE) {
				/* Unmount pipe */
				destroy_pipe_desc(&dev->opaque);
				return vfs_unmount_device(s->filename);
			}
		}

		break;

	default:
		/* Unsupported code */
		return E_INVALIDARG;
	}

	return S_OK;
}

HRESULT pipe_create(char *name, uint32_t flags, size_t buff_size)
{
	K_DEVICE *dev = kcalloc(sizeof(K_DEVICE));
	if (!dev) {
		return E_OUTOFMEM;
	}

	/* Create pipe descriptor struct */
	K_PIPE_DESC *pipe_desc 	= kmalloc(sizeof(K_PIPE_DESC));
	pipe_desc->buffer_size 	= buff_size;
	pipe_desc->ring_buffer	= kmalloc(buff_size);
	pipe_desc->flags 		= flags;

	mutex_create(&pipe_desc->lock);

	/* Set pipe url */
	dev->default_url = kmalloc(1024);
	sprintf(dev->default_url, "/ipc/%s", name);

	/* Populate */
	dev->type = DEVICE_TYPE_CHAR;
	dev->read = pipe_read;
	dev->write = pipe_write;
	dev->ioctl = pipe_ioctl;
	dev->opaque = pipe_desc;

	return vfs_mount_device(dev, dev->default_url);
}

#define CHECK(x, y) if (x != S_OK) { HalKernelPanic(y); }

void pipe_test()
{
	/* Tests the pipe system */
	pipe_create("pipe1", PIPE_FLAG_NONE, 1024*16);

	K_STREAM 	*s_read, s_write;
	uint8_t 	*buff = kmalloc(1024);
	uint8_t		*buff_2 = kmalloc(1024);
	uint32_t	bytes;

	CHECK(k_fopen("/ipc/pipe1", FILE_OPEN_WRITE, s_write), "Failed to open pipe for writing.");
	CHECK(k_fwrite(s_write, 1024, buff, &bytes), "Failed to write to pipe.");
	if (bytes != 1024) { HalKernelPanic("bytes != 1024."); }

	CHECK(k_fopen("/ipc/pipe1", FILE_OPEN_READ, s_read), "Failed to open pipe for reading.");
	CHECK(k_fread(s_read, 1024, buff_2, &bytes), "Failed to write to pipe.");
	if (bytes != 1024) { HalKernelPanic("bytes != 1024."); }

	for (int i=0; i<1024; i++) {
		if (buff[i] != buff_2[i]) {
			HalKernelPanic("Written and read bytes are different.");
		}
	}

	CHECK(fclose(s_read), "Failed to close reading handle.");
	CHECK(fclose(s_write), "Failed to close writing handle.");
}
