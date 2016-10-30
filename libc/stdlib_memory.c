/*
 * stdlib_memory.c
 *
 *	TODO:
 *		- Currently when a block is freed, it is merged with the next free blocks.
 *			It has to be made to merge with previous blocks also.
 *
 *		- mm_destroy_heap() is not properly implemented.
 *		- Implement logic to skip physical memory prior 16Mb, so it can be used for
 *			ISA devices for DMA transfer.
 *		- mm_fini() should be made destructor.
 *
 *  Created on: 10.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <stdlib.h>
#include <string.h>

#include <syncobjs.h>
#include <types.h>
#include <scheduler.h>
#include <mm_virt.h>
#include <kstdio.h>

/* Used for corruption prevention */
#define MM_MAGIC			0x4E584D4D
#define MAX_HEAPS			16
#define DEFAULT_HEAP_SIZE	8 * 1024 * 1024

typedef struct MM_CONTROL_BLOCK MM_CONTROL_BLOCK;
struct MM_CONTROL_BLOCK {
	uint32_t 	heap_id;
	uint32_t	magic;
	uint32_t	size;
	uint32_t	padding;
	BOOL		free;
};

typedef struct MM_HEAP_DESCRIPTOR MM_HEAP_DESCRIPTOR;
struct MM_HEAP_DESCRIPTOR{
	/* Used to serialize access to heap descriptor */
	K_MUTEX		lock;

	/* Heap start */
	void		*memory;
	uint32_t	size;
};

typedef struct MM_CONTEXT MM_CONTEXT;
struct MM_CONTEXT {
	uint32_t	initialized;
	uint32_t	pid;
	BOOL		kernel_mode;
	uint32_t	heap_count;

	K_MUTEX		lock;
	MM_HEAP_DESCRIPTOR heaps[MAX_HEAPS];
};

static MM_CONTEXT mm_contex = {
	.initialized = FALSE,
	.pid		 = 0,
	.heap_count	 = 0
};

/*
 * Prototypes
 */
static BOOL mm_create_heap(MM_CONTEXT *ctx);
static BOOL mm_free_heap(MM_CONTEXT *ctx, uint32_t id);
static BOOL mm_allocate_block_from_heap(MM_HEAP_DESCRIPTOR *d, size_t size, void **ptr);
static BOOL mm_free_block_from_heap(MM_HEAP_DESCRIPTOR *d, MM_CONTROL_BLOCK *mcb);
static BOOL mm_init();

void mm_fini();

/*
 * Implementation
 */
static BOOL mm_create_heap(MM_CONTEXT *ctx)
{
	HRESULT	hr = S_OK;

	/* Lock heap */
	mutex_lock(&ctx->lock);

	if (ctx->heap_count >= MAX_HEAPS) {
		hr = E_FAIL;
		goto unlock;
	}

	/* Initialize new heap descriptor. TODO: need syscal for createheap */
	MM_HEAP_DESCRIPTOR 	*d = &ctx->heaps[ctx->heap_count];

	d->size = DEFAULT_HEAP_SIZE;

	hr = vmm_create_heap(ctx->pid, d->size, USAGE_DATA | (ctx->kernel_mode ? USAGE_KERNEL : USAGE_USER), &d->memory);
	if (FAILED(hr)) goto unlock;

	/* Create initial mcb */
	MM_CONTROL_BLOCK	*mcb = d->memory;
	mcb->magic		= MM_MAGIC;
	mcb->heap_id	= ctx->heap_count;
	mcb->free		= TRUE;
	mcb->size		= d->size - sizeof(MM_CONTROL_BLOCK);
	mcb->padding	= 0;

	mutex_create(&d->lock);
	ctx->heap_count++;

unlock:
	/* Unlock */
	mutex_unlock(&ctx->lock);
	return SUCCEEDED(hr) ? TRUE : FALSE;
}

static BOOL mm_free_heap(MM_CONTEXT *ctx, uint32_t id)
{
	//HalKernelPanic("mm_free_heap(): not implemented");
	return FALSE;

	/* Lock heap */
	mutex_lock(&ctx->lock);

	MM_HEAP_DESCRIPTOR 	*d = &ctx->heaps[id];

	mutex_destroy(&d->lock);
	d->size = 0;
	vmm_destroy_heap(ctx->pid, d->memory);

	/* TODO: mark heap entry as unused */
//	for (uint32_t i=id; i<ctx->heap_count-1; i++) {
//		ctx->heaps[i] = ctx->heaps[i+1];
//	}

	ctx->heap_count--;

	/* Unlock */
	mutex_unlock(&ctx->lock);
	return TRUE;
}

static BOOL mm_allocate_block_from_heap(MM_HEAP_DESCRIPTOR *d, size_t size, void **ptr)
{
	MM_CONTROL_BLOCK	*mcb, *mcb_next;
	uint8_t				*addr = d->memory;
	void				*result = NULL;
	BOOL				success = TRUE;

	mutex_lock(&d->lock);

	while (addr != (uint8_t*)d->memory + d->size) {
//		k_printf("iteration: addr=%x; end_addr=%x\n", addr, (uint8_t*)d->memory + d->size);
		mcb = (MM_CONTROL_BLOCK*)addr;

		if (mcb->magic != MM_MAGIC) {
			/* Memory layout has corrupted */
			k_printf("Memory layout corrupted.\n");
			result	= NULL;
			success	= FALSE;

			goto unlock;
		}

		if (mcb->free && mcb->size >= size + sizeof(MM_CONTROL_BLOCK)) {
			if (mcb->size == size) {
				/* Free block found */
				mcb->free = FALSE;
			} else {
				/* Split into two blocks */
				mcb_next = (MM_CONTROL_BLOCK*)(addr + sizeof(MM_CONTROL_BLOCK) + size);
				mcb_next->magic		= MM_MAGIC;
				mcb_next->heap_id	= mcb->heap_id;
				mcb_next->free		= TRUE;
				mcb_next->size		= mcb->size - size - sizeof(MM_CONTROL_BLOCK);
				mcb_next->padding	= 0;

				/* Update current block info */
				mcb->free = FALSE;
				mcb->size = size;
			}

			result = addr + sizeof(MM_CONTROL_BLOCK);
			goto unlock;
		}

		/* Block not available or not sufficient */
		addr += mcb->size + sizeof(MM_CONTROL_BLOCK);
	}

unlock:
	mutex_unlock(&d->lock);

	*ptr = result;
	return success;
}

static BOOL mm_free_block_from_heap(MM_HEAP_DESCRIPTOR *d, MM_CONTROL_BLOCK *mcb)
{
	MM_CONTROL_BLOCK 	*next_mcb 	= NULL;
	void				*end_address= (uint8_t*)d->memory + d->size;
	uint32_t			expand_size = 0;
	BOOL				result 		= TRUE;

	mutex_lock(&d->lock);

	next_mcb = (MM_CONTROL_BLOCK*)((uint8_t*)mcb + sizeof(MM_CONTROL_BLOCK) + mcb->size + mcb->padding);

	/* Try to merge with next blocks if they are also free */
	while ((uintptr_t)next_mcb != (uintptr_t)end_address) {
		/* Validate block */
		if ((uintptr_t)next_mcb > (uintptr_t)end_address || ((uintptr_t)next_mcb <= (uintptr_t)end_address && next_mcb->magic != MM_MAGIC)) {
			result = FALSE;
			goto unlock;
		}

		if (!next_mcb->free) {
			break;
		}

		expand_size += next_mcb->size + sizeof(MM_CONTROL_BLOCK);

		/* Go to next block */
		next_mcb = (MM_CONTROL_BLOCK*)((uint8_t*)next_mcb + sizeof(MM_CONTROL_BLOCK) + next_mcb->size + next_mcb->padding);
	}

	/* Expand current block to merge with next free blocks */
	mcb->size += expand_size;
	mcb->free = TRUE;

unlock:
	mutex_unlock(&d->lock);
	return result;
}

static BOOL mm_init()
{
	HRESULT hr;

	if (mm_contex.initialized) {
		return FALSE;
	}

	/* TODO: implement used-space mapping for getpid() */
	hr = sched_get_current_pid(&mm_contex.pid);
	if (FAILED(hr)) return FALSE;

	mutex_create(&mm_contex.lock);

	/* TODO: Check kernel or user mode */
	mm_contex.kernel_mode	= TRUE;
	mm_contex.heap_count	= 0;
	mm_contex.initialized 	= TRUE;

	return TRUE;
}

void mm_fini()
{
	MM_CONTEXT *ctx = &mm_contex;

	if (!ctx->initialized) {
		return;
	}

	/* TODO:
	 * Report memory leaks
	 */

	mutex_lock(&ctx->lock);
	while (ctx->heap_count > 0) {
		mm_free_heap(ctx, 0);
	}
	mutex_unlock(&ctx->lock);

	mutex_destroy(&ctx->lock);

	/* TODO: use atomic set */
	ctx->initialized = FALSE;
}

void* malloc(size_t size)
{
	MM_CONTEXT 	*ctx = &mm_contex;
	void		*result = NULL;
	uint32_t	i;

	if (!mm_contex.initialized) {
		/* Initialize memory manager */
		if (!mm_init()) {
			return NULL;
		}
	}

	/* Validate size */
	if (size == 0) {
		return NULL;
	}

	/* Lock mm context */
	mutex_lock(&ctx->lock);

	for (i=0; i<ctx->heap_count; i++) {
		if (!mm_allocate_block_from_heap(&ctx->heaps[i], size, &result)) {
			/* Error */
			goto unlock;
		}

		if (result != NULL) {
			/* Allocated */
			goto unlock;
		}
	}

	/* Failed to allocate a block from existing heaps.
	 * Create new one.
	 */
	if (ctx->heap_count < MAX_HEAPS-1) {
		if (!mm_create_heap(&mm_contex)) {
			goto unlock;
		}

		/* Allocate from new heap */
		mm_allocate_block_from_heap(&ctx->heaps[ctx->heap_count-1], size, &result);
	}

unlock:
	mutex_unlock(&ctx->lock);
	return result;
}

void free(void* ptr)
{
	MM_CONTROL_BLOCK	*mcb = (MM_CONTROL_BLOCK*)((uint8_t*)ptr - sizeof(MM_CONTROL_BLOCK));
	MM_CONTEXT			*ctx = &mm_contex;

	/* Validate block */
	if (mcb->magic != MM_MAGIC || mcb->size == 0 || mcb->heap_id >= MAX_HEAPS) {
		/* Memory is not allocated by this memory manager or
		 * layout is corrupted.
		 */
		k_printf("[MM] free(): invalid mcb.");
		return;
	}

	/* Lock context */
	mutex_lock(&ctx->lock);
	mm_free_block_from_heap(&ctx->heaps[mcb->heap_id], mcb);
	mutex_unlock(&ctx->lock);

}

void* calloc(size_t num, size_t size)
{
	void *mem = malloc(num * size);

	if (mem) {
		memset(mem, 0, num * size);
	}

	return mem;
}

void* realloc(void* ptr, size_t size)
{
	/* TODO: We can implement block growing (if following block(s)
	 * are free, instead of relocating the memory.
	 */
	if (size == 0) {
		/* If free block */
		if (ptr) {
			free(ptr);
		}

		return NULL;
	}

	void *old = ptr;
	void *new = malloc(size);

	if (old != NULL) {
		if (new != NULL) {
			MM_CONTROL_BLOCK *mcb = (MM_CONTROL_BLOCK*)((uint8_t*)old - sizeof(MM_CONTROL_BLOCK));

			memcpy(new, old, size > mcb->size ? mcb->size : size);
		}

		free(old);
	}

	return new;
}

/*
 * Tests the routines
 */
BOOL mm_test()
{
	const int N = 10;

	void	*blocks[N];
	int		size;
	int 	i;

	size = 128;
	k_printf("Allocate %d blocks from %d bytes...\n", N, size);
	for (i=0; i<N; i++) {
		blocks[i] = malloc(size);
		if (!blocks[i]) {
			k_printf("Failed at block %d.\n", i);
			return FALSE;
		}

		k_printf("%x; ", blocks[i]);
	}

	k_printf("\nReallocing...\n");
	/* Reallocate */
	for (i=0; i<N; i++) {
		blocks[i] = realloc(blocks[i], size * 2);
		k_printf("%x; ", blocks[i]);
	}

	k_printf("\nReallocing (2)...\n");
	/* Reallocate */
	for (i=0; i<N; i++) {
		blocks[i] = realloc(blocks[i], size);
		k_printf("%x; ", blocks[i]);
	}

	k_printf("\nFreeing...\n\n");
	for (i=N-1; i>=0; i--) {
		free(blocks[i]);
	}

	size = 1024 * 4;
	k_printf("Allocate %d blocks from %d bytes...\n", N, size);
	for (i=0; i<N; i++) {
		blocks[i] = malloc(size);
		if (!blocks[i]) {
			k_printf("Failed at block %d.\n", i);
			return FALSE;
		}

		k_printf("%x; ", blocks[i]);
	}

	k_printf("\nFreeing...\n\n");
	for (i=0; i<N; i++) {
		free(blocks[i]);
	}

	size = 1024 * 1024 * 4;
	k_printf("Allocate big blocks...\n");
	for (i=0; i<1000; i++) {
		blocks[0] = malloc(size);

		if (!blocks[0]) {
			k_printf("Failed at block %d.\n", i);
			return FALSE;
		}

		free(blocks[0]);
	}

	return TRUE;
}
