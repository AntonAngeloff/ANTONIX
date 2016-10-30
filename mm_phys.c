/*
 * mm_phys.c
 *
 *  Created on: 31.05.2016 ã.
 *      Author: Anton Angeloff
 */

#include <stdint.h>
#include "include/mm_phys.h"
#include "include/string.h"
#include "vga.h"

#define MEMORY_BITMAP_LENGTH	128*1024
#define BITS_PER_ELEMENT	(sizeof(int32_t) * 8)

/* Bitmap (128*4 kb in size) */
uint32_t kpmm_memory_bitmap[MEMORY_BITMAP_LENGTH];

HRESULT kpmm_init(multiboot_info_t* mbt)
{
	UNUSED_ARG(mbt);
	memset(&kpmm_memory_bitmap[0], 0, MEMORY_BITMAP_LENGTH * sizeof(uint32_t));

	/* TODO: mark memory mapped io regions */
	kpmm_mark_blocks((void*)0x000000, 0x100000 / KPMM_BLOCK_SIZE); //mark range [0mb..1mb] as in use
	kpmm_mark_blocks((void*)0x00100000, 4*1024*1024 / KPMM_BLOCK_SIZE); //mark range [1mb..4mb] for kernel image

	return S_OK;
}

HRESULT kpmm_mark_blocks(const void *addr, int32_t num_blocks)
{
	uint32_t block_id = (uint32_t)addr / KPMM_BLOCK_SIZE;

	while(num_blocks--) {
		uint32_t *e = &kpmm_memory_bitmap[block_id / BITS_PER_ELEMENT];

		/* Try to mark 32 blocks at once */
		if((num_blocks+1) >= 32 && block_id % 32 == 0) {
#ifdef debug
			if(*e != 0) {
				return E_FAIL;
			}
#endif

			*e = 0xFFFFFFFF;

			num_blocks -= 31;
			block_id += 32;
			continue;
		}

		/* Mark one block (bit) per iteration */
#ifdef debug
		if(*e & (1 << (block_id % BITS_PER_ELEMENT))) {
			return E_FAIL;
		}
#endif

		/* Mark block */
		*e |= 1 << (block_id % BITS_PER_ELEMENT);

		/* Next block */
		block_id++;
	}

	return S_OK;
}

HRESULT kpmm_unmark_blocks(const void *addr, int32_t num_blocks)
{
	uint32_t block_id = (uint_ptr_t)addr / KPMM_BLOCK_SIZE;

	while(num_blocks--) {
		uint32_t *e = &kpmm_memory_bitmap[block_id / BITS_PER_ELEMENT];

#ifdef debug
		if(!(*e >> (block_id % BITS_PER_ELEMENT)) & 1) {
			return E_FAIL;
		}
#endif

		/* Un-mark block */
		*e &= ~(1 << (block_id % BITS_PER_ELEMENT));

		/* Next block */
		block_id++;
	}

	return S_OK;
}

HRESULT kpmm_find_free_region(int32_t num_blocks, PVOID *area)
{
	volatile int32_t i;
	int32_t icnt=num_blocks;
	int32_t i_start = 0;

	for (i=0; i<MEMORY_BITMAP_LENGTH; i++) {
		if(icnt >= 32) {
			if(kpmm_memory_bitmap[i] != 0) {
				/* More than (or) 32 blocks are yet needed,
				 * but the current element has less that 32 blocks free.
				 * So reset the counter. */
				icnt = num_blocks;
				i_start = i + 1;
			} else {
				/* All blocks in current element are free */
				icnt -= 32;
			}

			continue;
		}

		if ((kpmm_memory_bitmap[i] & ((1 << icnt) - 1)) != 0) {
			/* Not enough blocks. Reset counter */
			icnt = num_blocks;
			i_start = i + 1;

			continue;
		}

		goto found;
	}

	/* Failed to find */
	return E_FAIL;

found:
	*area = (void*)(i_start * BITS_PER_ELEMENT * KPMM_BLOCK_SIZE);
	return S_OK;
}

HRESULT kpmm_alloc(int32_t num_blocks, PVOID *area)
{
	HRESULT hr;

	hr = kpmm_find_free_region(num_blocks, area);
	if(FAILED(hr)) return hr;

	return kpmm_mark_blocks(*area, num_blocks);
}

HRESULT kpmm_test_region(const void *addr, int32_t num_blocks)
{
	UNUSED_ARG(addr);
	UNUSED_ARG(num_blocks);

	/* TODO: implement */
	return E_NOTIMPL;
}

HRESULT kpmm_self_test()
{
	/* TODO: implement */
	return E_NOTIMPL;
}
