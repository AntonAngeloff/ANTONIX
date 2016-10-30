/*
 * ringbuffer.h
 *
 *	Thread-safe ring buffer container implementation.
 *
 *	Working with the buffer is performed through the rb_() API.
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef LIBC_RINGBUFFER_H_
#define LIBC_RINGBUFFER_H_

#include <types.h>
#include <syncobjs.h>

typedef enum {
	RING_BUFFER_LOCK_NONE = 0,
	RING_BUFFER_LOCK_MUTEX,
	RING_BUFFER_LOCK_SPINLOCK
} RING_BUFFER_LOCK_TYPE;

typedef struct RING_BUFFER RING_BUFFER;
struct RING_BUFFER {
	RING_BUFFER_LOCK_TYPE lock_type;
	K_MUTEX		mutex;
	K_SPINLOCK 	spinlock;

	uint8_t		*buffer;
	uint32_t	buffer_capacity;

	uint32_t	read_pointer;
	uint32_t	write_pointer;
};

HRESULT rb_write(RING_BUFFER *rb, void *src, size_t len);
HRESULT rb_read(RING_BUFFER *rb, void *dst, size_t len);
HRESULT rb_read_upto(RING_BUFFER *rb, void *dst, size_t max, size_t *actual_bytes);
uint32_t rb_get_read_size(RING_BUFFER *rb);
uint32_t rb_get_write_size(RING_BUFFER *rb);

RING_BUFFER *create_ring_buffer(uint32_t buffer_size, RING_BUFFER_LOCK_TYPE lock_mechanism);
VOID destroy_ring_buffer(RING_BUFFER *rb);

#endif /* LIBC_RINGBUFFER_H_ */
