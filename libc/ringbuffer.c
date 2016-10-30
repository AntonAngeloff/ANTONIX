/*
 * ringbuffer.c
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */
#include "ringbuffer.h"
#include <stddef.h>
#include <string.h>
#include <mm.h>

/* Generic lock macro */
#define RB_LOCK(x) \
	uint32_t __rb_sl; \
	if (x->lock_type == RING_BUFFER_LOCK_MUTEX) mutex_lock(&x->mutex); \
		else if (x->lock_type == RING_BUFFER_LOCK_SPINLOCK) __rb_sl = spinlock_acquire(&x->spinlock);

/* Generic unlock */
#define RB_UNLOCK(x) \
	if (x->lock_type == RING_BUFFER_LOCK_MUTEX) mutex_unlock(&x->mutex); \
		else if (x->lock_type == RING_BUFFER_LOCK_SPINLOCK) spinlock_release(&x->spinlock, __rb_sl);

/*
 * Implementation
 */
RING_BUFFER *create_ring_buffer(uint32_t buffer_size, RING_BUFFER_LOCK_TYPE lock_mechanism)
{
	RING_BUFFER *rb;

	if (!(rb = kcalloc(sizeof(RING_BUFFER)))) {
		/* Out of memory */
		return NULL;
	}

	if (!(rb->buffer = kmalloc(buffer_size))) {
		/* Out of memory */
		kfree(rb);
		return NULL;
	}

	switch (lock_mechanism) {
		case RING_BUFFER_LOCK_MUTEX:
			mutex_create(&rb->mutex);
			break;

		case RING_BUFFER_LOCK_SPINLOCK:
			spinlock_create(&rb->spinlock);
			break;

		default:
			break;
	}

	rb->lock_type = lock_mechanism;
	rb->buffer_capacity = buffer_size;

	return rb;
}

VOID destroy_ring_buffer(RING_BUFFER *rb)
{
	RB_LOCK(rb);
	RB_UNLOCK(rb);

	kfree(rb->buffer);

	switch (rb->lock_type) {
		case RING_BUFFER_LOCK_MUTEX:
			mutex_destroy(&rb->mutex);
			break;

		case RING_BUFFER_LOCK_SPINLOCK:
			spinlock_destroy(&rb->spinlock);
			break;

		default:
			break;
	}

	kfree(rb);
}

HRESULT rb_write(RING_BUFFER *rb, void *src, size_t len)
{
	RB_LOCK(rb);

	/* Do we have enough space? Find free segment (section) size. */
	uint32_t free_seg_size = rb->write_pointer >= rb->read_pointer ?
					rb->buffer_capacity - (rb->write_pointer - rb->read_pointer) :
					rb->read_pointer - rb->write_pointer;

	/* We should always keep 1 byte difference between two
	 * positions.
	 */
	free_seg_size--;

	/* Make sure we have enough space to accommodate source buffer */
	if (free_seg_size < len) {
		RB_UNLOCK(rb);
		return E_BUFFEROVERFLOW;
	}

	/* Decide if we have to wrap the ring */
	uint8_t wrap = len > (rb->buffer_capacity - rb->write_pointer) ? TRUE : FALSE;

	if (wrap) {
		/* Copy in two iterations */
		uint32_t 	s1 = rb->buffer_capacity - rb->write_pointer;
		uint32_t 	s2 = len - s1;
		uint8_t		*in = src;

		memcpy(rb->buffer + rb->write_pointer, in, s1);
		memcpy(rb->buffer, in + s1, s2);

		/* Update write index */
		rb->write_pointer = s2;
	} else {
		/* Copy at once */
		uint32_t 	s1 = len;

		memcpy(rb->buffer + rb->write_pointer, src, s1);

		/* Update write index */
		rb->write_pointer += s1;
	}

	RB_UNLOCK(rb);
	return S_OK;
}

HRESULT rb_read(RING_BUFFER *rb, void *dst, size_t len)
{
	RB_LOCK(rb);

	/* Do we have enough data to read? */
	uint32_t avail_bytes = rb->read_pointer > rb->write_pointer ?
					rb->buffer_capacity - (rb->read_pointer - rb->write_pointer) :
					rb->write_pointer - rb->read_pointer;

	if (avail_bytes < len) {
		/* Not enough bytes available. */
		RB_UNLOCK(rb);
		return E_BUFFERUNDERFLOW;
	}

	uint8_t wrap = rb->read_pointer + len > rb->buffer_capacity ? TRUE : FALSE;

	if (wrap) {
		uint32_t	s1 = rb->buffer_capacity - rb->read_pointer;
		uint32_t	s2 = len - s1;
		uint8_t		*out = dst;

		memcpy(out, rb->buffer + rb->read_pointer, s1);
		memcpy(out + s1, rb->buffer, s2);

		/* Update read index */
		rb->read_pointer = s2;
	} else {
		memcpy(dst, rb->buffer + rb->read_pointer, len);
		rb->read_pointer += len;
	}

	RB_UNLOCK(rb);
	return S_OK;
}

HRESULT rb_read_upto(RING_BUFFER *rb, void *dst, size_t max, size_t *actual_bytes)
{
	uint32_t size = rb_get_read_size(rb);

	size = size > max ? max : size;
	if (actual_bytes) *actual_bytes = size;

	if (size == 0) {
		return E_BUFFERUNDERFLOW;
	}

	return rb_read(rb, dst, size);
}

uint32_t rb_get_read_size(RING_BUFFER *rb)
{
	RB_LOCK(rb);

	uint32_t avail_bytes = rb->read_pointer > rb->write_pointer ?
					rb->buffer_capacity - (rb->read_pointer - rb->write_pointer) :
					rb->write_pointer - rb->read_pointer;

	RB_UNLOCK(rb);
	return avail_bytes;
}

uint32_t rb_get_write_size(RING_BUFFER *rb)
{
	RB_LOCK(rb);

	/* Do we have enough space? Find free segment (section) size. */
	uint32_t free_seg_size = rb->write_pointer >= rb->read_pointer ?
					rb->buffer_capacity - (rb->write_pointer - rb->read_pointer) :
					rb->read_pointer - rb->write_pointer;

	RB_UNLOCK(rb);

	/* We should always keep 1 byte difference between two
	 * positions.
	 */
	free_seg_size--;

	return free_seg_size;
}
