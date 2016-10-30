/*
 * mm_skheap.c
 *
 *  Created on: 30.06.2016 ã.
 *      Author: Admin
 */

#include "include/mm_skheap.h"
#include "include/hal.h"
#include "include/string.h"
#include "include/vga.h"

/* Static kernel heap will have 1Mb capacity */
#define 	SKHEAP_SIZE	0x100000

/* Single block is 256b */
#define 	SKHEAP_BLOCK_SIZE	0x100

/* Number of existing sk-heap blocks */
#define SKHEAP_BLOCK_COUNT	SKHEAP_SIZE / SKHEAP_BLOCK_SIZE

/* Since we use single byte for bitmap array element, it holds 8 values/bits. */
#define SKHEAP_BLOCKS_PER_ELEMENT	8

/* Number of elements in the bitmap array. Each is 1 bytes and stores 8 bits */
#define SKHEAP_BITMAP_ELEMENTS	SKHEAP_BLOCK_COUNT / SKHEAP_BLOCKS_PER_ELEMENT

/* Reserve memory for heap */
uint8_t skheap_region[SKHEAP_SIZE] __attribute__((aligned(0x1000)));
uint8_t *skheap_region_start;

/* Block bitmap. Each bit marks the availability status of it's corresponding block.
 *
 * Bit set (1) = Block is allocated
 * Bit cleared (0) = Block is free
 */
uint8_t skheap_block_bitmap[SKHEAP_BITMAP_ELEMENTS];

/**
 * Counts number of free blocks
 */
uint64_t free_block_counter;

/*
 * Implementation
 */

/**
 * Returns total heap capacity
 */
int32_t skheap_get_capacity(void)
{
	return SKHEAP_SIZE;
}

HRESULT skheap_find_free_region(uint32_t size, void **addr)
{
	int i;

#ifdef debug
	if (size % SKHEAP_BLOCK_SIZE != 0) {
		HalKernelPanic("Invalid region size, it must be multiple of block size.");
	}
#endif

	uint32_t blocks = size / SKHEAP_BLOCK_SIZE;
	uint32_t bl_cntr = blocks;
	uint32_t start_bl_id = 0;

	for (i=0; i<SKHEAP_BITMAP_ELEMENTS; i++) {
		if (bl_cntr > SKHEAP_BLOCKS_PER_ELEMENT) {
			if (skheap_block_bitmap[i] != 0) {
				/* Less than 8 free blocks -> reset counter. */
				bl_cntr = blocks;
				start_bl_id = (i + 1) * SKHEAP_BLOCKS_PER_ELEMENT;
			} else {
				bl_cntr -= SKHEAP_BLOCKS_PER_ELEMENT;
			}

			continue;
		}

		if (skheap_block_bitmap[i] & ((1 << bl_cntr) - 1)) {
			/* Not enough blocks. Reset counter */
			bl_cntr = blocks;
			start_bl_id = (i + 1) * SKHEAP_BLOCKS_PER_ELEMENT;
			//vga_printf("continue. i=%d; bl_cntr=%d; start_bl_id=%d\n", i, bl_cntr, start_bl_id);
			continue;
		}

		/* Found */
		*addr = (void*)(skheap_region_start + start_bl_id * SKHEAP_BLOCK_SIZE);
		return S_OK;
	}

	/* Failed to find */
	return E_FAIL;
}

HRESULT skheap_find_free_region_aligned(uint32_t size, void **addr)
{
	/* Actually we need a block starting 256 bytes before 4kb-aligned address, because
	 * the control structure is located right before the usable memory region (which
	 * should be 4kb-aligned).
	 *
	 * 4-aligned block is every 16-th block since we use 256 (0x100) byte granularity.
	 * So when we search for free region, we will always start from a block index
	 * multiple of 15.
	 */
	int i;

#ifdef debug
	if (size % SKHEAP_BLOCK_SIZE != 0) {
		HalKernelPanic("Invalid region size, it must be multiple of block size.");
	}
#endif

	uint32_t blocks = size / SKHEAP_BLOCK_SIZE;
	uint32_t bl_cntr = blocks;
	uint32_t start_bl_id = 0;

	for (i=2; i<SKHEAP_BITMAP_ELEMENTS; i++) {
		if (bl_cntr == blocks) {
			if (i % 2 != 0) {
				/* Since i represents 8 blocks, and i+i represent 16 blocks.
				 * Every even i will represent 4kb aligned block.
				 */
				continue;
			}

			/* We are at 4kb aligned block, but we need 256 bytes from the previous block
			 * for control structure.
			 */
			if ((skheap_block_bitmap[i-1] & 0x80) != 0) {
				continue;
			}

			/* Seems like a good start */
			start_bl_id = (i * SKHEAP_BLOCKS_PER_ELEMENT) - 1;
			bl_cntr--;
		}

		if (bl_cntr > SKHEAP_BLOCKS_PER_ELEMENT) {
			if (skheap_block_bitmap[i] != 0) {
				/* Less than 8 free blocks -> reset counter. */
				bl_cntr = blocks;
			} else {
				bl_cntr -= SKHEAP_BLOCKS_PER_ELEMENT;
			}

			continue;
		}

		if (skheap_block_bitmap[i] & ((1 << bl_cntr) - 1)) {
			/* Not enough blocks. Reset counter */
			bl_cntr = blocks;
			continue;
		}

		/* Found */
		*addr = (void*)(skheap_region_start + start_bl_id * SKHEAP_BLOCK_SIZE);
		return S_OK;
	}

	/* Failed to find */
	return E_FAIL;
}


/*
 * At this point we'll abandon this implementation in favor of more naive one
 */
//HRESULT skheap_mark_region(void *start, uint32_t len, int8_t in_use)
//{
//	uint32_t start_block = ((uint32_t)start-(uint32_t)&skheap_region[0]) / SKHEAP_BLOCK_SIZE;
//	uint32_t block_cnt = len / SKHEAP_BLOCK_SIZE;
//
//	//
//	uint32_t marker = start_block;
//	uint32_t prealign_bits = start_block % SKHEAP_BLOCKS_PER_ELEMENT;
//
//	/* Set first bits before the first 8-aligned block */
//	if (prealign_bits != 0) {
//		uint32_t i = marker / SKHEAP_BLOCKS_PER_ELEMENT;
//
//		if (in_use) {
//			/* Lift flags */
//			skheap_block_bitmap[i] |= (0xFF & (8-prealign_bits));
//		}else {
//			/* Drop flags */
//			skheap_block_bitmap[i] &= (1 << (prealign_bits)) - 1;
//		}
//
//		marker += prealign_bits;
//		block_cnt -= prealign_bits;
//	}
//
//#ifdef debug
//	if((marker & 0xFF) != 0) {
//		vga_printf("start_block= %d; block_cnt=%d; prealign_bits= %d\n", start_block, block_cnt, prealign_bits);
//		vga_printf("SKHEAP start: %x\n", &skheap_region[0]);
//		vga_printf("skheap_mark_region(%x, %d, %d)\n", start, len, in_use);
//		HalKernelPanic("Assert failed.");
//	}
//#endif
//
//	/* Set remaining flags */
//	while (block_cnt > 0) {
//		if (block_cnt > 8) {
//			if(in_use) {
//				skheap_block_bitmap[marker / SKHEAP_BLOCKS_PER_ELEMENT] = 0xFF;
//			}else {
//				skheap_block_bitmap[marker / SKHEAP_BLOCKS_PER_ELEMENT] = 0x00;
//			}
//
//			marker += 8;
//			block_cnt -= 8;
//
//			continue;
//		}
//
//		/* Block cnt < 8 */
//		if (in_use) {
//			skheap_block_bitmap[marker / SKHEAP_BLOCKS_PER_ELEMENT] |= (1 << (block_cnt+1)) + 1;
//		}else {
//			skheap_block_bitmap[marker / SKHEAP_BLOCKS_PER_ELEMENT] &= !((1 << (block_cnt+1)) + 1);
//		}
//	}
//
//	return S_OK;
//}

HRESULT skheap_mark_region(void *start, uint32_t len, int8_t in_use)
{
	uint32_t offs_address = (uint32_t)start - (uint32_t)skheap_region_start;
	uint32_t block_id = offs_address / SKHEAP_BLOCK_SIZE;
	uint32_t num_blocks = len / SKHEAP_BLOCK_SIZE;

//	vga_printf("start_block= %d; block_cnt=%d\n", block_id, num_blocks);
//	vga_printf("SKHEAP start: %x\n", &skheap_region[0]);
//	vga_printf("skheap_mark_region(%x, %d, %d)\n", start, len, in_use);

	if (offs_address % SKHEAP_BLOCK_SIZE != 0) {
		HalKernelPanic("Trying to mark region which doesn't start on block boundary.");
	}

	if (len % SKHEAP_BLOCK_SIZE != 0) {
		HalKernelPanic("Trying to mark region which doesn't span across block boundary.");
	}

	if (block_id + num_blocks >= SKHEAP_BLOCK_COUNT) {
		HalKernelPanic("Trying to mark region outside of static kernel heap region.");
	}

	if (num_blocks == 0) {
		HalKernelPanic("Memory region size is zero or below SKHEAP_BLOCK_SIZE.");
	}

	while(num_blocks--) {
		uint8_t *e = &skheap_block_bitmap[block_id / SKHEAP_BLOCKS_PER_ELEMENT];

#ifdef debug
		if (in_use) {
			if(*e & (1 << (block_id % SKHEAP_BLOCKS_PER_ELEMENT))) {
				HalKernelPanic("Error: marking a block, already marked the same state (in use).");
			}
		}else {
			if((*e & (1 << (block_id % SKHEAP_BLOCKS_PER_ELEMENT))) == 0) {
				HalKernelPanic("Error: marking a block, already marked the same state (free).");
			}
		}
#endif

		/* Mark block */
		if (in_use) {
			*e |= 1 << (block_id % SKHEAP_BLOCKS_PER_ELEMENT);
		} else {
			*e &= (1 << (block_id % SKHEAP_BLOCKS_PER_ELEMENT)) ^ 0xFF;
		}

		/* Next block */
		block_id++;
	}

	return S_OK;
}

void* skheap_get_virt_addr(void *physical_addr)
{
	/* Converts physical address to virtual one */
	return (void*)((uint8_t*)physical_addr + 0xC0000000);
}

void* skheap_get_phys_addr(void *virtual_addr)
{
	/* This should be fairly straightforward. The static heap
	 * is located inside the kernel image, and it is mapped +3GB over
	 * it's physical location in memory, so to find it, we
	 * just subtract 3GB.
	 */
	return (void*)((uint8_t*)virtual_addr - 0xC0000000);
}

void* skheap_malloc(int32_t size)
{
	if (size <= 0) {
		/* Invalid size */
		return NULL;
	}

	/* We will prepend a control structure before the allocated memory region */
	int32_t ext_size = size + sizeof(SKH_CONTROL_STRUCT);
	uint8_t *p = NULL;

	/* Make the size fall on block size boundary */
	ext_size += SKHEAP_BLOCK_SIZE - ext_size % SKHEAP_BLOCK_SIZE;

	/* Find a free region with this particular size */
	if(FAILED(skheap_find_free_region(ext_size, (void**)&p))) {
		/* Most likely, out of memory */
		return NULL;
	}

	/* Mark region as "in use" */
	skheap_mark_region((void*)p, ext_size, 1);

	/* Setup the control structure */
	SKH_CONTROL_STRUCT *cs = (SKH_CONTROL_STRUCT*)p;
	cs->size = size;
	cs->ext_size = ext_size;
	cs->crc_size = (size + ext_size) ^ 0xFFFFFFFF;
	cs->padding = 0;

	/* Return the address right after the control struct */
	return (void*)(p + sizeof(SKH_CONTROL_STRUCT));
}

void* skheap_calloc(int32_t size)
{
	/* We simply allocate and call memset() */
	void *p = skheap_malloc(size);
	if (!p) {
		return NULL;
	}

	memset(p, 0, size);
	return p;
}

void* skheap_realloc(void *p, int32_t new_size)
{
	/* At this point, we'll do only naive implementation of realloc
	 * maybe later on in time, it could be optimized.
	 */
	if(new_size<=0 || p==NULL) {
		return NULL;
	}

	/* Allocate new region */
	void *new = skheap_malloc(new_size);

	/* Retrieve region's old size and copy contents to new region */
	SKH_CONTROL_STRUCT *cs = (SKH_CONTROL_STRUCT*)((uint8_t*)p - sizeof(SKH_CONTROL_STRUCT));
	memcpy(new, p, cs->size);

	/* Release old region */
	skheap_free(p);
	return p;
}

void* skheap_malloc_a(int32_t size)
{
	/* Allocate 4KB-aligned memory region */
	if (size <= 0) {
		/* Invalid size */
		return NULL;
	}

	/* We will prepend a control structure before the allocated memory region */
	int32_t ext_size = size + sizeof(SKH_CONTROL_STRUCT);
	uint8_t *p = NULL;

	/* Make the size fall on block size boundary */
	ext_size += SKHEAP_BLOCK_SIZE - ext_size % SKHEAP_BLOCK_SIZE;

	/* Find a free region with this particular size */
	if(FAILED(skheap_find_free_region_aligned(ext_size, (void**)&p))) {
		/* Most likely, out of memory */
		return NULL;
	}

	/* Mark region as "in use" */
	skheap_mark_region((void*)p, ext_size, 1);

	/* Setup the control structure */
	SKH_CONTROL_STRUCT *cs = (SKH_CONTROL_STRUCT*)(p + SKHEAP_BLOCK_SIZE - sizeof(SKH_CONTROL_STRUCT));
	cs->size = size;
	cs->ext_size = ext_size;
	cs->crc_size = (size + ext_size) ^ 0xFFFFFFFF;
	cs->padding = SKHEAP_BLOCK_SIZE - sizeof(SKH_CONTROL_STRUCT);

	/* Return the address right after the control struct */
	return (void*)(p + SKHEAP_BLOCK_SIZE);
}

void* skheap_calloc_a(int32_t size)
{
	/* We simply allocate and call memset() */
	void *p = skheap_malloc_a(size);
	if (!p) {
		return NULL;
	}

	memset(p, 0, size);
	return p;
}

void skheap_free(void *ptr)
{
#ifdef debug
	if (ptr == NULL) {
		HalKernelPanic("SKHEAP: Trying to free null pointer.");
	}

	if ((uint32_t)ptr < (uint32_t)skheap_region_start || (uint32_t)ptr >= ((uint32_t)skheap_region_start + SKHEAP_SIZE)) {
		HalKernelPanic("SKHEAP: Freeing a memory outside the static kernel heap pool.");
	}
#endif

	/* Locate the region's control structure */
	SKH_CONTROL_STRUCT *cs = (SKH_CONTROL_STRUCT*)((uint8_t*)ptr - sizeof(SKH_CONTROL_STRUCT));
	if ((cs->crc_size ^ 0xFFFFFFFF) != (cs->size + cs->ext_size)) {
		/* Invalid pointer */
		HalKernelPanic("Freeing invalid memory block. CRC value doesn't comply.");
		return;
	}

	/* Find real start position of the memory region */
	uint8_t *p = (uint8_t*)cs;
	p -= cs->padding;

	/* Mark memory region as free */
	skheap_mark_region((void*)p, cs->ext_size, 0);
}

HRESULT skheap_init()
{
	memset(&skheap_block_bitmap[0], 0, sizeof(skheap_block_bitmap[0]) * SKHEAP_BITMAP_ELEMENTS);
	free_block_counter = 0;

	/* We need the memory pool to start at 4kb-aligned address, so we'll move the start pointer
	 * and reserve the blocks before the 4kb-aligned heap start address.
	 */
	skheap_region_start = &skheap_region[0];
	uint32_t padding = 4096 - (uint_ptr_t)skheap_region_start % 4096;

	if(padding > 0 && padding != 4096) {
		skheap_region_start += padding;
#ifdef debug
		if ((uint_ptr_t)skheap_region_start % 4096 != 0) {
			HalKernelPanic("Unexpected error.");
		}
#endif

		/* We will reserve blocks at the end of the heap */
		uint32_t reserve_size = padding + (SKHEAP_BLOCK_SIZE - padding % SKHEAP_BLOCK_SIZE);
		uint8_t *reserve_addr = skheap_region_start + SKHEAP_SIZE - reserve_size;
		reserve_addr -= SKHEAP_BLOCK_SIZE - (uint_ptr_t)reserve_addr % SKHEAP_BLOCK_SIZE;

		/* Reserve */
		skheap_mark_region(reserve_addr, reserve_size, 1);
	}

	return S_OK;
}

#define TEST_COUNT	3

void skheap_selftest()
{
	int i;
	void *p[TEST_COUNT];

	/* Allocate and free small blocks (512 b) */
	vga_printf("Allocating %d blocks of 512 bytes...\n", TEST_COUNT);
	for (i=0;i<TEST_COUNT;i++) {
		p[i] = skheap_malloc(512);
		if (p[i]) {
			vga_printf("%d. Pointer: %x\n", i, p[i]);
		}else {
			vga_printf("%d. Out of mem\n", i);
		}
	}

	vga_printf("Free'ing the %d blocks...\n", TEST_COUNT);
	for (i=0;i<TEST_COUNT;i++) {
		skheap_free(p[i]);
	}

	/* Allocate and free big blocks (200000 b) */
	vga_printf("Allocating %d blocks of 200000 bytes...\n", TEST_COUNT);
	for (i=0;i<TEST_COUNT;i++) {
		p[i] = skheap_malloc(200000);
		if (p[i]) {
			vga_printf("%d. Pointer: %x\n", i, p[i]);
		}else {
			vga_printf("%d. Out of mem\n", i);
		}
	}

	vga_printf("Free'ing the %d blocks...\n", TEST_COUNT);
	for (i=0;i<TEST_COUNT;i++) {
		if (p[i]) {
			skheap_free(p[i]);
		}
	}

	/* Allocate and free 4kb aligned small blocks (200 b) */
	vga_printf("Allocating %d 4kb-aligned blocks of 200000 bytes...\n", TEST_COUNT);
	for (i=0;i<TEST_COUNT;i++) {
		p[i] = skheap_malloc_a(200000);
		if (p[i]) {
			vga_printf("%d. Pointer: %x\n", i, p[i]);
		}else {
			vga_printf("%d. Out of mem\n", i);
		}
	}

	vga_printf("Free'ing the %d blocks...\n", TEST_COUNT);
	for (i=0;i<TEST_COUNT;i++) {
		if (p[i]) {
			skheap_free(p[i]);
		}
	}
}
