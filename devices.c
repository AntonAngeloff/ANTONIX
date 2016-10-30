/*
 * devices.c
 *
 *  Created on: 23.09.2016 ã.
 *      Author: Anton Angelov
 */
#include <devices.h>
#include <kstdio.h>

HRESULT storage_read_blocks(K_STREAM *drv, uint32_t start, uint32_t count, void *buffer)
{
	IOCTL_STORAGE_READWRITE rw;

	rw.buffer 	= buffer;
	rw.start 	= start;
	rw.count	= count;

	return k_ioctl(drv, IOCTL_STORAGE_READ_BLOCKS, &rw);
}

HRESULT storage_write_blocks(K_STREAM *drv, uint32_t start, uint32_t count, void *buffer)
{
	IOCTL_STORAGE_READWRITE rw;

	rw.buffer 	= buffer;
	rw.start 	= start;
	rw.count	= count;

	return k_ioctl(drv, IOCTL_STORAGE_WRITE_BLOCKS, &rw);
}

HRESULT storage_get_block_size(K_STREAM *drv, size_t *size)
{
	return k_ioctl(drv, IOCTL_STORAGE_GET_BLOCK_SIZE, size);
}

HRESULT storage_get_block_count(K_STREAM *drv, size_t *count)
{
	return k_ioctl(drv, IOCTL_STORAGE_GET_BLOCK_COUNT, count);
}
