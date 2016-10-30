/*
 * mm_virt.h
 *
 *  Created on: 3.07.2016 ã.
 *      Author: Admin
 */

#ifndef MM_VIRT_H_
#define MM_VIRT_H_

/**
 * 	@brief
 * 	Virtual memory management (paging) subsystem.
 *
 * 	Note that we will use x86 paging feature without PSE or PAE, so we'll use
 * 	page directories, page tables and 4kb pages.
 *
 * 	Memory mapping and unmapping is done only in supervisor mode and should lock spinlocks
 * 	(not implemented at current moment).
 */

#include "types.h"

/**
 *  Defines the maximum of kernel regions allowed to be mapped. It is assumed that
 *  the heap manager will allocate large memory regions, most likely larger than
 *  8Mbs, so 256 regions are enough to allocate at least 1GB.
 */
#define MAX_KERNEL_REGIONS	128

/**
 * 4KiB page frame size
 */
#define VM_PAGE_FRAME_SIZE	0x1000

/**
 * Defines the start address of the kernel code section
 */
#define KERNEL_CODE_START	0xC0100000

/**
 * Defines starting address of kernel heap
 */
#define KERNEL_HEAP_START	0xC8000000

#define USER_HEAP_START		0x08000000

/**
 * Defines the starting address of the kernel temp region,
 * which is used to temporary map different memory regions for processing.
 */
#define KERNEL_TEMP_START	0xFE000000

#define USER_CODE_START		0x00100000

typedef enum {
	USAGE_RESERVED	= 	0x1,
	USAGE_KERNEL	= 	0x2,
	USAGE_USER		= 	0x4,
	USAGE_HEAP		= 	0x8,
	USAGE_STACK		= 	0x10,
	USAGE_AUTOFREE	= 	0x20,
	USAGE_TEMP		= 	0x40,
	USAGE_CODE		=	0x80,
	USAGE_DATA		=	0x100,
	USAGE_KERNELHEAP = 	USAGE_KERNEL | USAGE_HEAP,
	USAGE_KERNELSTACK = USAGE_KERNEL | USAGE_STACK,
	USAGE_USERHEAP	=	USAGE_USER | USAGE_HEAP,
	USAGE_USERSTACK	= 	USAGE_USER | USAGE_STACK,
} K_VMM_REGION_USAGE;

typedef enum {
	ACCESS_READ	=		0x01,
	ACCESS_WRITE =		0x02,
	ACCESS_READWRITE = 	ACCESS_READ | ACCESS_WRITE
} K_VMM_ACCESS_FLAG;

typedef struct {
	DWORD f_present: 1;
	DWORD f_writable: 1;
	DWORD f_user: 1;
	DWORD f_reserved: 2;
	DWORD f_accessed: 1;
	DWORD f_dirty: 1;
	DWORD f_reserved2: 1;
	DWORD f_global: 1;
	DWORD f_custom: 3;
	DWORD frame_addr: 20;
} K_VMM_PAGE_ENTRY;

typedef struct {
	K_VMM_PAGE_ENTRY pages[1024];
} K_VMM_PAGE_TABLE;

typedef struct {
	/**
	 * If set, the page is available inside the physical memory and vice versa.
	 * When accessing a non-present page, the CPU will fire a page fault exception, which
	 * have to be handled by other routines.
	 */
	uint8_t f_present: 1;

	/**
	 * If flag is set, allows both read and write access. If not set - only read access.
	 */
	uint8_t f_readwrite: 1;

	/**
	 * If flag is set, allows user-space to access this page directory's derived pages.
	 * This will require setting f_user bit to each page table entry as well as here in
	 * page directory entry.
	 *
	 * If flag is not set, only kernel-space (supervisor) can access the pages.
	 */
	uint8_t f_allow_user_read: 1;

	/**
	 * Toggle between "write-through" and "write-back" caching for 1 and 0 respectively.
	 */
	uint8_t f_write_through_caching: 1;

	/**
	 * Disables caching.
	 */
	uint8_t f_cache_disable: 1;

	/**
	 * Signifies if the page has been accessed, since it is mapped.
	 */
	uint8_t f_accessed: 1;

	/**
	 * Reserved, must be 0
	 */
	uint8_t f_reserved0: 1;

	/**
	 * Stores the size of the page for this entry. If flag is set, the page is 4MiB
	 * in size, otherwise it's 4KiB. In order to use this flag, PSE must be enabled.
	 */
	uint8_t	f_pagesize: 1;

	/**
	 * Reserved (1bit)
	 */
	uint8_t f_reserved1: 1;

	/**
	 * These three bits are not used by the CPU and thus are available for our own usage.
	 */
	uint8_t f_opaque: 3;

	/**
	 * The physical address of the page table. It must be 4kb aligned.
	 */
	uint32_t page_table_addr: 20;
} K_VMM_PAGE_DIR_ENTRY;

typedef struct {
	/** Array of page directory entries */
	K_VMM_PAGE_DIR_ENTRY	table[1024];

	/**
	 * Array of pointers to the physical address
	 * of the table above.
	 */
	uint32_t	phys_table[1024];

	/**
	 * Physical address of &phys_table
	 */
	uint32_t	phys_table_phys_addr;
} K_VMM_PAGE_DIR;

typedef struct {
	uint_ptr_t phys_addr;
	uint_ptr_t virt_addr;
	size_t	region_size;
	K_VMM_REGION_USAGE usage;
	K_VMM_ACCESS_FLAG access;
} K_VMM_REGION;

/**
 * Initializes the virtual memory managmenet sub-system.
 */
HRESULT	vmm_init();

/**
 * Maps a physical memory region to virtual address space for kernel space usage.
 */
HRESULT vmm_map_region_ks(uint_ptr_t phys_addr, uint_ptr_t virt_addr, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access);

/**
 * Unmaps memory region from virtual address space.
 */
HRESULT vmm_unmap_region_ks(uint_ptr_t virt_addr);

/**
 * Returns the end (as virtual address) of the greatest mapped virtual memory
 * region of the kernel space
 */
uint_ptr_t vmm_get_address_space_end_ks();

/**
 * Creates a heap and appends it to the end of the address space (can be used
 * both for kernel and user-space). Creating means it is allocating physical memory
 * and mapping it to the end of the virtual address space.
 *
 * @param size Requested size for the new heap
 */
HRESULT vmm_create_heap_k(uint32_t size, K_VMM_REGION_USAGE usage, void **out);
HRESULT vmm_create_heap(uint32_t pid, uint32_t size, K_VMM_REGION_USAGE usage, void **out);

/**
 * Destroys a heap
 */
HRESULT vmm_destroy_heap_k(void *heap_ptr);
HRESULT vmm_destroy_heap(uint32_t pid, void *heap_ptr);

/** Used for debugging */
HRESULT	vmm_get_region_count_ks(size_t *cnt);
HRESULT	vmm_get_region_count(void *proc_desc, size_t *cnt);

/** Used for debugging */
HRESULT	vmm_get_region_ks(size_t id, K_VMM_REGION *r);
HRESULT	vmm_get_region(void *proc_desc, size_t id, K_VMM_REGION *r);

/**
 * Maps a physical memory region to virtual address space for given process
 *
 * @param 	proc_desc	Pointer to process descriptor (K_PROCESS struct)
 * @param	phys_addr	Physical address of the memory being mapped
 * @param	virt_addr	Virtual address
 * @param 	size		Size of the memory region which will be mapped
 * @param	commit		If set, the function will clear the TLB cache
 */
HRESULT __nxapi vmm_map_region(void *proc_desc, uintptr_t phys_addr, uintptr_t virt_addr, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access, uint8_t commit);

/**
 * Unmaps virtual memory region (starting at address _virt_addr_) for
 * given process.
 *
 * @param 	proc_desc	Pointer to process descriptor (K_PROCESS struct).
 * @param	virt_addr	Pointer to virtual address of memory region
 * @param	commit		If set, the function will invalidate the unmapped pages with INVLPG
 */
HRESULT __nxapi vmm_unmap_region(void *proc_desc, uint_ptr_t virt_addr, int commit);

HRESULT	__nxapi	vmm_alloc_and_map(void *proc_desc, uintptr_t virt_addr, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access, uint8_t commit);
HRESULT __nxapi vmm_alloc_and_map_limited(void *proc_desc, uintptr_t virt_addr, uintptr_t limit, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access, uint8_t commit);
uintptr_t __nxapi vmm_get_address_space_end(void *proc_desc);

/**
 * Finds a place in the temporary kernel map and maps a physical memory region.
 * Region is unmapped, as usual, by calling vmm_unmap_region()
 */
HRESULT __nxapi vmm_temp_map_region(void *proc_desc, uintptr_t phys_addr, uint32_t region_size, uintptr_t *virt_addr);

/**
 * Find's memory region's physical address location from a given virtual address.
 */
HRESULT __nxapi vmm_get_region_phys_addr(void *proc_desc, uintptr_t virt_addr, uintptr_t *phys_addr);

void vmm_selftest();

#endif /* MM_VIRT_H_ */
