/*
 * pipe.h
 *
 *  Created on: 4.08.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_PIPE_H_
#define INCLUDE_PIPE_H_

#include "types.h"
#include "syncobjs.h"

#define PIPE_FLAG_NONE				0x00
#define PIPE_FLAG_DELETE_ON_CLOSE	0x01

typedef struct PIPE_DESC K_PIPE_DESC;
struct PIPE_DESC {
	/* Ring buffers as usual has two position indices */
	uint32_t	read_pos;
	uint32_t	write_pos;

	/** Total size/capacity of the buffer */
	size_t		buffer_size;

	/** Pointer to buffer itself */
	uint8_t		*ring_buffer;

	/** Count of open handles to this pipe */
	uint32_t	ref_cnt;

	/**
	 * Probably we can switch to semaphore when we
	 * implement one.
	 */
	K_MUTEX		lock;

	uint32_t	flags;
};

HRESULT pipe_create(char *name, uint32_t flags, size_t buff_size);

#endif /* INCLUDE_PIPE_H_ */
