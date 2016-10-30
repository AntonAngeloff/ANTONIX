/*
 * mm_phys.h
 *
 *  Created on: 31.05.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_MM_PHYS_H_
#define INCLUDE_MM_PHYS_H_

/**
 * @brief Kernel physical memory management (kpmm) API
 *
 * We will be using a bit map to keep track of which memory blocks are allocated.
 * Each memory block is consisted of 4096 bytes.
 *
 * On startup, the initialization routine should parse the data passed by GRUB,
 * describing which regions of the physical memory are reserved for memory mapped
 * device i/o and mark those regions as not-available.
 *
 * The physical MM will keep a 128 kb bit map in a reserved physical memory location.
 * 128 kb bit map is capable of keeping track of 16GB physical memory.
 */

#include "types.h"
#include "multiboot.h"


/* Block size is 4096 bytes, as we mentioned earlier */
#define KPMM_BLOCK_SIZE	4096

/** Initializes the _kpmm_ sub-system
 * @return S_OK on success, error otherwise
 */
HRESULT kpmm_init(multiboot_info_t* mbt);

/** Allocates a sequence of physical memory blocks. Each block is consisted of 4096 bytes.
 * @param addr Pointer to the beginning of the memory block, which will be allocated.
 * @param num_blocks Number of sequential blocks to be allocated
 * @return S_OK on success, error otherwise
 */
HRESULT kpmm_mark_blocks(const void *addr, int32_t num_blocks);

/** Frees a sequence of memory blocks.
 * @return S_OK on success, error otherwise.
 */
HRESULT kpmm_unmark_blocks(const void *addr, int32_t num_blocks);

/** Finds a free memory region consisted of _num_ blocks.
 * @param num_blocks Number of blocks which had to be contained in the free region.
 * @param area Pointer to variable to receive the address of the free region
 * @return S_OK on success, error otherwise
 */
HRESULT kpmm_find_free_region(int32_t num_blocks, PVOID *area);

/**
 * Tests weather a given region in physical memory is available
 * @return S_OK if region is available, error otherwise
 */
HRESULT kpmm_test_region(const void *addr, int32_t num_blocks);

/** Finds and allocates _num_blocks_ free memory blocks.
 * @return S_OK on success, error otherwise.
 */
HRESULT kpmm_alloc(int32_t num_blocks, PVOID *area);

/**
 * Performs unit test on the kpmm sub-system.
 */
HRESULT kpmm_self_test();

#endif /* INCLUDE_MM_PHYS_H_ */
