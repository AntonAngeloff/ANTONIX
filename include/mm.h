/*
 * mm.h
 *
 *	Memory paging and management for Ntonix
 *
 *  Created on: 19.11.2015 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_MM_H_
#define INCLUDE_MM_H_

#include <types.h>

/*
 * Generic memory manager interface
 */
typedef struct K_MEMORY_MANAGER K_MEMORY_MANAGER;
struct K_MEMORY_MANAGER {
	void __nxapi (*malloc)(size_t size);
	void __nxapi (*calloc)(size_t size);
	void __nxapi (*realloc)(size_t size);
	void __nxapi (*free)(size_t size);
	void __nxapi (*set_tag)(uint32_t tag);
};

/*
 * ANTONIX memory manager context
 */
struct K_MEMORY_MANAGER_CONTEXT {
	/* Methods */
	K_MEMORY_MANAGER	iface;

	/* Process to which the mm is attached */
	void 				*proc_desc;

	/* Process is operating only in kernel mode */
	BOOL 				is_kernel_mode;

	/* Control structure */
	void 				*main_heap;
};

/**
 * Retrieves the start of the kernel image in physical memory.
 */
HRESULT mm_get_kernel_physical_location(uint32_t *start, uint32_t *end);

/**
 * Retrieves the start of the kernel image in virtual memory.
 */
HRESULT mm_get_kernel_virtual_location(uint32_t *start, uint32_t *end);

void *kmalloc(size_t size);
void *kcalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

#endif /* INCLUDE_MM_H_ */
