/*
 * mm_skheap.h
 *
 *  Created on: 30.06.2016 ã.
 *      Author: Anton Angeloff
 */

/**
 *  @brief Static kernel heap (skheap) API
 *
 *	Static kernel heap sub-system provides a dynamically allocatable memory inside a fixed memory
 *	region inside the .bss section of the kernel. The size of the fixed pool could be adjusted in size,
 *	but will probably be between 1 and 2 Mb.
 *
 *	The need of SK heap arises from the need of other memory sub-systems to manage their selves using
 *	dynamic structures like linked lists and page tables, which require dynamic memory allocation.
 *
 *	This subsystem should be used only by virtual memory (paging) manager. For other cases, the kernel heap
 *	should be used.
 */

#ifndef MM_SKHEAP_H_
#define MM_SKHEAP_H_

#include "types.h"

#define SKHEAP_MEM_REGION_SIGNATURE	0x12345678

/**
 * This control structure is prepended before each allocated memory region.
 */
typedef struct {
	uint32_t size;
	uint32_t ext_size;
	uint32_t padding;
	uint32_t crc_size;
} SKH_CONTROL_STRUCT;

/**
 * Returns the total capacity of the static kernel heap in bytes.
 */
int32_t skheap_get_capacity(void);

/**
 * Allocates a memory region of size bytes
 */
void* skheap_malloc(int32_t size);

/**
 * Allocates a memory region, initializing it with zeroes afterwards.
 */
void* skheap_calloc(int32_t size);

/**
 *  Allocates a memory region of size bytes (4kb-aligned)
 */
void* skheap_malloc_a(int32_t size);

/**
 * Allocates a memory region (4kb-aligned), initializing it with zeroes afterwards.
 */
void* skheap_calloc_a(int32_t size);

/**
 * Reallocates a memory region with bigger/smaller one
 */
void* skheap_realloc(void *p, int32_t new_size);

/**
 * Frees a memory region.
 */
void skheap_free(void *ptr);

/**
 * Returns the physical address of virtual address, allocated by
 * skheap_*() routines.
 */
void* skheap_get_phys_addr(void *virtual_addr);

/**
 * Returns virtual address from physical. It should be used only for
 * memory regions allocated by skheap_*() routines.
 */
void* skheap_get_virt_addr(void *physical_addr);

/**
 * Initializes the static kernel heap (SKHEAP) sub-system.
 */
HRESULT skheap_init();

/**
 * Performs minor sub-system test.
 */
void skheap_selftest();

#endif /* MM_SKHEAP_H_ */
