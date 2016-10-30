/*
 * mm.c
 *
 *	Memory management and virtual memory mapping.
 *
 *  Created on: 20.05.2016 ã.
 *      Author: Admin
 */

#define USE_STDLIB_MEMORY_MANAGER

#include "include/types.h"
#include "include/mm.h"
#include "include/hal.h"
#include "include/mm_skheap.h" //todo:delete

#ifdef USE_STDLIB_MEMORY_MANAGER
#include <stdlib.h>
#else
#include <mm_skheap.h>
#endif

#define MAX_HEAPS	64

/* Array of pointers to allocated heaps */
uint8_t *heaps[MAX_HEAPS];
uint32_t heap_cnt;

/* Symbols exported by the linker which points to the
 * memory address range, taken by the kernel image
 */
void kernel_virtual_start(void);
void kernel_physical_start(void);
void kernel_virtual_end(void);
void kernel_physical_end(void);

HRESULT mm_get_kernel_physical_location(uint32_t *start, uint32_t *end)
{
	*start = (uint_ptr_t)&kernel_physical_start;
	*end = (uint_ptr_t)&kernel_physical_end;

	return S_OK;
}

HRESULT mm_get_kernel_virtual_location(uint32_t *start, uint32_t *end)
{
	*start = (uint_ptr_t)&kernel_virtual_start;
	*end = (uint_ptr_t)&kernel_virtual_end;

	return S_OK;
}

#ifdef USE_STDLIB_MEMORY_MANAGER
	void *kmalloc(size_t size)
	{
		/* for now, we'll redirect this to skheap_ API */
		return malloc(size);
	}

	void *kcalloc(size_t size)
	{
		return calloc(1, size);
	}

	void kfree(void *ptr)
	{
		free(ptr);
	}

	void *krealloc(void *ptr, size_t size)
	{
		return realloc(ptr, size);
	}
#else
	void *kmalloc(size_t size)
	{
		/* for now, we'll redirect this to skheap_ API */
		return skheap_malloc(size);
	}

	void *kcalloc(size_t size)
	{
		return skheap_calloc(size);
	}

	void kfree(void *ptr)
	{
		skheap_free(ptr);
	}

	void *krealloc(void *ptr, size_t size)
	{
		return skheap_realloc(ptr, size);
	}
#endif
