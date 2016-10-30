/*
 * mm_virt.c
 *
 *  Created on: 3.07.2016 ã.
 *      Author: Admin
 *
 *  Virtual memory usage convention:
 *  	0MB 		% 512MB 		=> User code and data
 *  	512			% 3GB			=> User heap and memory mapped resources
 *  	3GB 		% 3GB + 128MB	=> Kernel code and data
 *  	3GB + 128MB % 3GB + 950MB 	=> Kernel heap
 *  	3GB + 950MB % 4GB			=> Kernel temp usage
 */

#include "include/mm_virt.h"
#include "include/mm_phys.h"
#include "include/mm_skheap.h"
#include "include/hal.h"
#include "include/desctables.h"
#include "string.h"
#include "scheduler.h"
#include <kstdio.h>

/* Static array of memory map regions for kernel space usage */
K_VMM_REGION	kernel_regions[MAX_KERNEL_REGIONS];
uint32_t		kernel_region_cnt;

/* Statically declared page directory */
K_VMM_PAGE_DIR 	page_dir  __attribute__((aligned(0x1000)));

/* Symbols exported by the linker which points to the
 * memory address range, taken by the kernel image
 */
void kernel_virtual_start(void);
void kernel_physical_start(void);

static HRESULT vmm_find_region(void *proc_desc, uint_ptr_t virt_addr, K_VMM_REGION *dst);

/*
 * Implementation
 */
uint_ptr_t get_kernel_address_space_offset(void)
{
	return (uint_ptr_t)&kernel_virtual_start - (uint_ptr_t)&kernel_physical_start;
}

void vmm_lock() {
	/* TODO: implement when locking is supported */
}

void vmm_unlock() {
	/* TODO: implement */
}


/* Initializes the kernel virtual address space. As for this point, the paging should
 * be enabled by the assembly code in boot.s, which uses it's own static paging structures.
 *
 * Now we'll use the vmm_* API to reinitialize basic paging. So we'll have to map first 4MBs to
 * 0xC000000.
 */
void vmm_init_virtual_address_space() {
	HRESULT hr = vmm_map_region_ks(0, 0xC0000000, 4*1024*1024, USAGE_KERNEL, ACCESS_READWRITE);
	if (FAILED(hr)) {
		HalKernelPanic("Failed to map region [0x0..0x00100000] to [0xC0000000..0xC0100000].");
	}
//
//	hr = vmm_map_region_ks(0, 0, 4 * 1024 * 1024, USAGE_KERNEL, ACCESS_READWRITE);
//	if (FAILED(hr)) {
//		HalKernelPanic("Failed to map region [0x0..0x00100000] to [0xC0000000..0xC0100000].");
//	}
}

static VOID __cdecl vmm_page_fault_handler(K_REGISTERS regs)
{
	if (regs.err_code & 0x2) {
		k_printf("Page fault occurred by write at address %x\n", HalGetFaultingAddr());
	} else {
		k_printf("Page fault occurred by read of address %x\n", HalGetFaultingAddr());
	}

	k_printf("EIP=%x \tESP=%x \t*(ESP)=%x\n", regs.eip, regs.esp, *((uint32_t*)(regs.esp)));
	k_printf("Present: %d \tWrite\\read: %d \tUser\\kernel: %d \tInstr.fetch: %d\n\n",
					regs.err_code & 0x1 ? 1 : 0,
					regs.err_code & 0x2 ? 1 : 0,
					regs.err_code & 0x4 ? 1 : 0,
					regs.err_code & 0x10 ? 1 : 0
	);

	HalKernelPanic("Shutting down...");
}

HRESULT	vmm_init()
{
	/* Lock VMM mutex */
	vmm_lock();

	/* Initialize page directory */
	memset(&page_dir, 0, sizeof(page_dir));

	/* Make sure &page_dir is 4kb aligned */
	if ((uint_ptr_t)&page_dir % 4096 != 0) {
		HalKernelPanic("page_dir variable is not 4kb-aligned.");
	}

	/* The physical address of page_dir.phys_table is it's variable address decremented by 0xC0000000 */
	page_dir.phys_table_phys_addr = (uint_ptr_t)&page_dir.phys_table - get_kernel_address_space_offset();

	/* Initialize kernel region count */
	kernel_region_cnt = 0;

	/* Register page fault handler */
	register_isr_callback(14, vmm_page_fault_handler, NULL);

	/* Perform initial mapping, necessary for kernel */
	vmm_init_virtual_address_space();

	/* (Re)Enable paging */
	HalEnablePaging(skheap_get_phys_addr(&page_dir));

//success:
	vmm_unlock();
	return S_OK;
}

static HRESULT fetch_page_table(K_VMM_PAGE_DIR *dir, uint32_t id, int auto_create, int autocr_rw, int autocr_us, K_VMM_PAGE_TABLE **out)
{
	if (dir->table[id].page_table_addr) {
		*out = skheap_get_virt_addr((void*)(dir->table[id].page_table_addr << 12));
		return S_OK;
	}

	if (auto_create) {
		K_VMM_PAGE_TABLE *table = skheap_calloc_a(sizeof(K_VMM_PAGE_TABLE));
		dir->phys_table[id] = (uint_ptr_t)skheap_get_phys_addr(table);

		/* Initialize entry */
		K_VMM_PAGE_DIR_ENTRY *e = &dir->table[id];
		memset((void*)e, 0, sizeof(K_VMM_PAGE_DIR_ENTRY));

		/* Populate */
		e->f_readwrite = autocr_rw ? 1 : 0;
		e->f_allow_user_read = autocr_us ? 1 : 0;
		e->f_present = 1;
		e->page_table_addr = dir->phys_table[id] >> 12;

		/* Found */
		(*out) = table;
		return S_OK;
	}

	/* Not found */
	return E_INVALIDARG;
}

HRESULT vmm_map_region_ks(uint_ptr_t phys_addr, uint_ptr_t virt_addr, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access)
{
	/* We disallow mapping kernel space memory to addresses below 0xC0000000 */
	if (virt_addr < get_kernel_address_space_offset()) {
		HalKernelPanic("Requesting to map kernel virtual memory to address lower than 0xC0000000.");
	}

	/* Make sure we don't exceed maximum region count */
	if (kernel_region_cnt == MAX_KERNEL_REGIONS) {
		HalKernelPanic("Maximum count of virtual memory regions reached.");
	}

	if (size % VM_PAGE_FRAME_SIZE != 0) {
		HalKernelPanic("Memory region's length is not granular to page frame size.");
	}

	if (phys_addr % 0x1000 != 0) {
		HalKernelPanic("Physical address must be aligned at 4KiB.");
	}

	uint32_t i;
	uint_ptr_t range_start = virt_addr;
	uint_ptr_t range_end = virt_addr + size;

	/* Iterate all kernel memory regions to check if requested region
	 * overlaps with other, already mapped, regions
	 */
	for (i=0; i<kernel_region_cnt; i++) {
		K_VMM_REGION *r = &kernel_regions[i];

		if ((r->virt_addr >= range_start && r->virt_addr < range_end) ||
			(r->virt_addr+r->region_size > range_start && r->virt_addr+r->region_size <= range_end))
		{
			/* Requested region overlaps with other currently mapped region. */
			return E_FAIL;
		}
	}

	/* We don't allocate physical memory here (it's job of kpmm_* subsystem. We don't care
	 * if this region is free or not.
	 */
//	/* Test if the physical address range is available for allocation */
//	if (kpmm_test_region((void*)phys_addr, (size + (KPMM_BLOCK_SIZE-1)) / KPMM_BLOCK_SIZE) != S_OK) {
//		HalKernelPanic("Trying to map non-free pysical memory region.");
//	}

	/* Add new region entry */
	K_VMM_REGION *r = &kernel_regions[kernel_region_cnt];

	/* Populate entry */
	r->phys_addr = phys_addr;
	r->virt_addr = virt_addr;
	r->region_size = size;
	r->usage = usage;
	r->access = access;

	/* Number of page frames, required for describing the region */
	uint32_t page_cnt = size / VM_PAGE_FRAME_SIZE;
	uint_ptr_t phys_addr_idx = phys_addr;
	uint_ptr_t virt_addr_idx = virt_addr;
	uint8_t f_rw = access == ACCESS_READWRITE;
	uint8_t f_us = usage == USAGE_USER;

	for (i=0; i<page_cnt; i++) {
		/* Generate and submit page tables */
		uint32_t page_table_id = (virt_addr_idx / 0x1000) / 1024;
		uint32_t page_id = (virt_addr_idx / 0x1000) % 1024;
		K_VMM_PAGE_TABLE *table;

		/* Retrieve pointer to the page table, describing this page frame. */
		HRESULT hr = fetch_page_table(&page_dir, page_table_id, 1, f_rw, f_us, &table);
		if (FAILED(hr)) {
			HalKernelPanic("Failed to retrieve page table.");
		}

		K_VMM_PAGE_ENTRY *p = &table->pages[page_id];

		/* Populate page entry */
		memset(p, 0, sizeof(K_VMM_PAGE_ENTRY));
		p->frame_addr = phys_addr_idx >> 12;
		p->f_user = f_us;
		p->f_writable = f_rw;
		p->f_present = 1;

		/* Move to next page */
		phys_addr_idx += VM_PAGE_FRAME_SIZE;
		virt_addr_idx += VM_PAGE_FRAME_SIZE;
	}

	/* Invalidate TLB cache for modified pages */
	for (virt_addr_idx=virt_addr; virt_addr_idx<virt_addr+size; virt_addr_idx += VM_PAGE_FRAME_SIZE) {
		HalInvalidatePage((void*)virt_addr_idx);
	}

	/* Commit new region */
	kernel_region_cnt++;

	return S_OK;
}

HRESULT vmm_unmap_region_ks(uint_ptr_t virt_addr)
{
	//todo: we should disable interrupts here or lock a mutex

	/* Find the region */
	uint32_t i, id;
	K_VMM_REGION *r = NULL;

	for (i=0; i<kernel_region_cnt; i++) {
		if (kernel_regions[i].virt_addr == virt_addr) {
			r = &kernel_regions[i];
			id = i;
			break;
		}
	}

	if (!r) {
		/* Region not found */
		return E_INVALIDARG;
	}

	/* Make sure region is valid */
	if ((r->usage & USAGE_KERNEL) == 0) {
		return E_INVALIDARG;
	}

	uint32_t num_blocks = r->region_size / VM_PAGE_FRAME_SIZE;
	assert(num_blocks > 0);

	/* Unmap each block one by one */
	uint_ptr_t virt_addr_idx;
	for (virt_addr_idx=virt_addr; virt_addr_idx<virt_addr+r->region_size; virt_addr_idx+=VM_PAGE_FRAME_SIZE) {
		uint32_t page_table_id = (virt_addr_idx / 0x1000) / 1024;
		uint32_t page_id = (virt_addr_idx / 0x1000) % 1024;
		K_VMM_PAGE_TABLE *table;

		HRESULT hr = fetch_page_table(&page_dir, page_table_id, 0, 0, 0, &table);
		if (FAILED(hr)) {
			return hr;
		}

		table->pages[page_id].f_present = 0;
	}

	/* Invalidate TLB cache */
	for (virt_addr_idx=virt_addr; virt_addr_idx<virt_addr+r->region_size; virt_addr_idx+=VM_PAGE_FRAME_SIZE) {
		HalInvalidatePage((void*)virt_addr_idx);
	}

	/* Remove region from array */
	for (i=id; i<kernel_region_cnt-1; i++) {
		kernel_regions[i] = kernel_regions[i+1];
	}
	kernel_region_cnt--;

	return S_OK;
}

static HRESULT vmm_find_region(void *proc_desc, uint_ptr_t virt_addr, K_VMM_REGION *dst)
{
	K_PROCESS *proc = proc_desc;

	/* Finds a region by it's starting virtual address */
	vmm_lock();

	uint32_t i;
	HRESULT hr = S_OK;

	for (i=0; i<proc->region_count; i++) {
		if (proc->regions[i].virt_addr == virt_addr) {
			/* Found */
			*dst = proc->regions[i];
			goto finally;
		}
	}

	/* Not found */
	hr = E_FAIL;

finally:
	vmm_unlock();
	return hr;
}

static HRESULT vmm_find_region_ks(uint_ptr_t virt_addr, K_VMM_REGION *dst)
{
	/* Finds a region by it's starting virtual address */
	vmm_lock();

	uint32_t i;
	HRESULT hr = S_OK;

	for (i=0; i<kernel_region_cnt; i++) {
		if (kernel_regions[i].virt_addr == virt_addr) {
			/* Found */
			*dst = kernel_regions[i];
			goto finally;
		}
	}

	/* Not found */
	hr = E_FAIL;

finally:
	vmm_unlock();
	return hr;
}

//HRESULT vmm_delete_region_entry_ks(uint_ptr_t virt_addr)
//{
//	/* Finds a region by it's starting virtual address */
//	vmm_lock();
//
//	HRESULT hr = S_OK;
//	if (kernel_region_cnt == 0) {
//		/* No entries */
//		hr = E_INVALIDARG;
//		goto finally;
//	}
//
//	uint32_t i, id=0;
//	for (i=1; i<kernel_region_cnt; i++) {
//		if (kernel_regions[i].virt_addr == virt_addr) {
//			/* Found */
//			id = i;
//			break;
//		}
//	}
//
//	for (i=id; i<kernel_region_cnt-1; i++) {
//		kernel_regions[i] = kernel_regions[i+1];
//	}
//	kernel_region_cnt--;
//
//	/* Success */
//	goto finally;
//
//	/* Not found */
//	hr = E_FAIL;
//
//finally:
//	vmm_unlock();
//	return hr;
//}

uint_ptr_t vmm_get_address_space_end_ks()
{
	/* Lock VMM mutex */
	vmm_lock();

	uint_ptr_t result = 0xC0000000;
	if (kernel_region_cnt == 0) {
		/* No regions */
		goto finally;
	}

	uint32_t i;
	K_VMM_REGION *r = &kernel_regions[0];

	for (i=1; i<kernel_region_cnt; i++) {
		/* Check for unexpected cases */
		assert_msg(kernel_regions[i].virt_addr != r->virt_addr, "Found two regions with same virtual address.");

		if (kernel_regions[i].virt_addr > r->virt_addr) {
			r = &kernel_regions[i];
		}
	}

	result = r->virt_addr + r->region_size;

finally:
	vmm_unlock();
	return result;
}

HRESULT vmm_create_heap_k(uint32_t size, K_VMM_REGION_USAGE usage, void **out)
{
	/* Heaps' size should be multiple of page frame size (4kb) */
	if (size % VM_PAGE_FRAME_SIZE != 0) {
		assert(0);
		return E_INVALIDARG;
	}

	/* Find size free physical memory and maps it at the end of the address space */
	uint32_t ph_blocks = (size + (KPMM_BLOCK_SIZE-1))/ KPMM_BLOCK_SIZE;
	void *ptr;

	HRESULT hr = kpmm_find_free_region(ph_blocks, &ptr);
	if (FAILED(hr)) {
		HalKernelPanic("Not enough physical memory");
	}

	if (usage == USAGE_KERNEL) {
		/* Allocate physical memory */
		hr = kpmm_mark_blocks(ptr, ph_blocks);
		if (FAILED(hr)) goto end;

		uint_ptr_t virt_addr = vmm_get_address_space_end_ks();
		hr = vmm_map_region_ks((uint_ptr_t)ptr, virt_addr, size, usage | USAGE_HEAP, ACCESS_READWRITE);

		*out = (void*)virt_addr;
	} else {
		/* Not implemented */
		return E_FAIL;
	}

end:
	return hr;
}

HRESULT vmm_create_heap(uint32_t pid, uint32_t size, K_VMM_REGION_USAGE usage, void **out)
{
	K_PROCESS	*proc;
	HRESULT		hr;

	/* Heaps' size should be multiple of page frame size (4kb) */
	if (size % VM_PAGE_FRAME_SIZE != 0) {
		assert(0);
		return E_INVALIDARG;
	}

	/* Get process descriptor */
	hr = sched_find_process(pid, &proc);
	if (FAILED(hr)) return hr;

	/* Find size free physical memory and maps it at the end of the address space */
	uint32_t ph_blocks = (size + (KPMM_BLOCK_SIZE-1))/ KPMM_BLOCK_SIZE;
	void *ptr;

	hr = kpmm_find_free_region(ph_blocks, &ptr);
	if (FAILED(hr)) {
		HalKernelPanic("vmm_create_heap(): Not enough physical memory.");
	}

	/* Allocate physical memory */
	hr = kpmm_mark_blocks(ptr, ph_blocks);
	if (FAILED(hr)) goto end;

	/* Map memory */
	uint_ptr_t virt_addr = vmm_get_address_space_end_ks();
	hr = vmm_map_region(proc, (uint_ptr_t)ptr, virt_addr, size, usage | USAGE_HEAP, ACCESS_READWRITE, TRUE);

	*out = (void*)virt_addr;

end:
	return hr;
}

HRESULT vmm_destroy_heap_k(void *heap_ptr)
{
	K_VMM_REGION	r;

	HRESULT hr = vmm_find_region_ks((uint_ptr_t)heap_ptr, &r);
	if (FAILED(hr)) {
		return hr;
	}

	if ((r.usage & USAGE_HEAP) == 0) {
		HalKernelPanic("Trying to free non-heap region");
	}

	if (r.usage & USAGE_KERNEL) {
		/* Un-map virtual memory region */
		hr = vmm_unmap_region_ks(r.virt_addr);
		if (FAILED(hr)) return hr;

		/* Free physical memory */
		assert(r.region_size % VM_PAGE_FRAME_SIZE == 0);
		hr = kpmm_unmark_blocks((void*)r.phys_addr, r.region_size / VM_PAGE_FRAME_SIZE);
		if (FAILED(hr)) return hr;
	} else {
		HalKernelPanic("Destroying user-space heap is not implemented.");
	}

	return S_OK;
}

HRESULT vmm_destroy_heap(uint32_t pid, void *heap_ptr)
{
	K_PROCESS 		*proc;
	HRESULT			hr;
	K_VMM_REGION	r;

	/* Get process descriptor */
	hr = sched_find_process(pid, &proc);
	if (FAILED(hr)) return hr;

	hr = vmm_find_region(proc, (uint_ptr_t)heap_ptr, &r);
	if (FAILED(hr)) return hr;

	if ((r.usage & USAGE_HEAP) == 0) {
		HalKernelPanic("Trying to free non-heap region");
	}

	/* Un-map virtual memory region */
	hr = vmm_unmap_region(proc, r.virt_addr, 1);
	if (FAILED(hr)) return hr;

	/* Free physical memory */
	assert(r.region_size % VM_PAGE_FRAME_SIZE == 0);
	hr = kpmm_unmark_blocks((void*)r.phys_addr, r.region_size / VM_PAGE_FRAME_SIZE);
	if (FAILED(hr)) return hr;

	return S_OK;

}

#define TEST_COUNT	3
void vmm_selftest()
{
	k_printf("Allocating %d x 16Mb heaps...\n", TEST_COUNT);
	void *heaps[TEST_COUNT];

	for (int i=0; i<TEST_COUNT; i++) {
		HRESULT hr;

		hr = vmm_create_heap_k(16*1024*1024, USAGE_KERNEL, &heaps[i]);
		if (FAILED(hr)) HalKernelPanic("Failed to create heap.");

		int32_t *p = (int32_t*)heaps[i];
		for (int j=0; j<16*1024*1024/4; j++) {
			/* Perform read/write on heap */
			volatile int q = *p;
			if (q) {
				*p = j;
			} else {
				*p = 0;
			}
			p++;
		}
	}

	for (int i=0; i<TEST_COUNT; i++) {
		HRESULT hr;

		hr = vmm_destroy_heap_k(heaps[i]);
		if (FAILED(hr)) HalKernelPanic("Failed.");
	}
}

HRESULT	vmm_get_region_count_ks(size_t *cnt) {
	vmm_lock();
	*cnt = kernel_region_cnt;
	vmm_unlock();

	return S_OK;
}

HRESULT	vmm_get_region_ks(size_t id, K_VMM_REGION *r) {
	HRESULT hr = S_OK;

	vmm_lock();
	if (id >= kernel_region_cnt) {
		hr = E_INVALIDARG;
		goto finally;
	}

	*r = kernel_regions[id];
finally:
	vmm_unlock();
	return hr;
}

HRESULT	vmm_get_region(void *proc_desc, size_t id, K_VMM_REGION *r)
{
	K_PROCESS 	*proc = proc_desc;
	HRESULT 	hr = S_OK;

	uint32_t ifl = spinlock_acquire(&proc->lock);

	if (id > proc->region_count) {
		hr = E_INVALIDARG;
		goto finally;
	}

	*r = proc->regions[id];

finally:
	spinlock_release(&proc->lock, ifl);
	return hr;
}

HRESULT	vmm_get_region_count(void *proc_desc, size_t *cnt)
{
	K_PROCESS *proc = proc_desc;

	uint32_t ifl = spinlock_acquire(&proc->lock);
	*cnt = proc->region_count;
	spinlock_release(&proc->lock, ifl);

	return S_OK;
}

HRESULT __nxapi vmm_map_region(void *proc_desc, uintptr_t phys_addr, uintptr_t virt_addr, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access, uint8_t commit)
{
	/* We don't allocate physical memory here (it's job of kpmm_* subsystem). We don't care
	 * if this physical region is free or not.
	 */
	K_PROCESS 	*proc = proc_desc;
	HRESULT		hr;

	/* If proc_desc is NULL, then kernel main process is requested */
	if (proc_desc == NULL) {
		/* Fetch kernel process desc */
		hr = sched_get_process_by_id(0, (K_PROCESS**)&proc);
		if (FAILED(hr)) return hr;
	}

	/* Make sure we don't exceed maximum region count */
	if (proc->region_count == MAX_VM_REGIONS) {
		HalKernelPanic("Maximum count of virtual memory regions reached.");
	}

	/* Assert memory region size is multiple of VM_PAGE_FRAME_SIZE */
	if (size % VM_PAGE_FRAME_SIZE != 0) {
		HalKernelPanic("Memory region's length is not granular to page frame size.");
	}

	/* Assert physical address starts at 4kb aligned address */
	if (phys_addr % 0x1000 != 0) {
		k_printf("phys_addr=%x  virt_addr=%x\n", phys_addr, virt_addr);
		HalKernelPanic("Physical address must be aligned at 4KiB.");
	}

	uint32_t i;
	uint_ptr_t range_start = virt_addr;
	uint_ptr_t range_end = virt_addr + size;

	/* Iterate all memory regions to check if requested region
	 * overlaps with other, already mapped, regions
	 */
	for (i=0; i<proc->region_count; i++) {
		K_VMM_REGION *r = &proc->regions[i];

		if ((r->virt_addr >= range_start && r->virt_addr < range_end) ||
			(r->virt_addr+r->region_size > range_start && r->virt_addr+r->region_size <= range_end))
		{
			/* Requested region overlaps with other currently mapped region. */
			return E_FAIL;
		}
	}

	/* Add new region entry */
	K_VMM_REGION *r = &proc->regions[proc->region_count];

	/* Populate entry */
	r->phys_addr = phys_addr;
	r->virt_addr = virt_addr;
	r->region_size = size;
	r->usage = usage;
	r->access = access;

	/* Number of page frames, required for describing the region */
	uint32_t page_cnt = size / VM_PAGE_FRAME_SIZE;
	uint_ptr_t phys_addr_idx = phys_addr;
	uint_ptr_t virt_addr_idx = virt_addr;
	uint8_t f_rw = access == ACCESS_READWRITE;
	uint8_t f_us = (usage | USAGE_USER) != 0;

	/* Modify page directory ang page tables */
	for (i=0; i<page_cnt; i++) {
		/* Generate and submit page tables */
		uint32_t page_table_id = (virt_addr_idx / 0x1000) / 1024;
		uint32_t page_id = (virt_addr_idx / 0x1000) % 1024;
		K_VMM_PAGE_TABLE *table;

		/* Retrieve pointer to the page table, describing this page frame. */
		HRESULT hr = fetch_page_table(proc->page_dir, page_table_id, 1, f_rw, f_us, &table);
		if (FAILED(hr)) {
			HalKernelPanic("Failed to retrieve page table.");
		}

		K_VMM_PAGE_ENTRY *p = &table->pages[page_id];

		/* Populate page entry */
		memset(p, 0, sizeof(K_VMM_PAGE_ENTRY));
		p->frame_addr = phys_addr_idx >> 12;
		p->f_user = f_us;
		p->f_writable = f_rw;
		p->f_present = 1;

		/* Move to next page */
		phys_addr_idx += VM_PAGE_FRAME_SIZE;
		virt_addr_idx += VM_PAGE_FRAME_SIZE;
	}

	/* If we have updated the kernel process page directory, the changes have to be made
	 * to all other processes' virtual address spaces. Since kernel is mapped to all user
	 * processes at address 3GB.
	 */
	if (proc->mode == PROCESS_MODE_KERNEL) {
		/* TODO */
		//return E_NOTIMPL;
	}

	/* Invalidate TLB cache for modified pages */
	if (commit) {
		for (virt_addr_idx=virt_addr; virt_addr_idx<virt_addr+size; virt_addr_idx += VM_PAGE_FRAME_SIZE) {
			HalInvalidatePage((void*)virt_addr_idx);
		}
	}

	/* Increment region counter */
	proc->region_count++;

	return S_OK;
}

uintptr_t __nxapi vmm_get_address_space_end(void *proc_desc)
{
	HRESULT 	hr;
	K_PROCESS	*proc = proc_desc;
	//uintptr_t	result = proc->mode == PROCESS_MODE_KERNEL ? KERNEL_HEAP_START : USER_HEAP_START;
	uintptr_t	result = KERNEL_HEAP_START; //we have to also define heap end address and follow it's range
	uint32_t 	intf;
	uint32_t 	i;
	K_VMM_REGION *r;

	if (proc_desc == NULL) {
		/* Fetch kernel process desc */
		hr = sched_get_process_by_id(0, (K_PROCESS**)&proc);
		if (FAILED(hr)) return hr;
	}

	if (proc->region_count == 0) {
		/* No regions */
		goto finally;
	}

	intf = spinlock_acquire(&proc->lock);
	r = &proc->regions[0];

	for (i=1; i<proc->region_count; i++) {
		/* Check for unexpected cases */
		assert_msg(proc->regions[i].virt_addr != r->virt_addr, "Found two regions with same virtual address.");

		if (proc->regions[i].virt_addr > r->virt_addr) {
			r = &proc->regions[i];
		}
	}

	result = r->virt_addr + r->region_size;
	spinlock_release(&proc->lock, intf);

finally:
	vmm_unlock();
	return result;
}

HRESULT	__nxapi	vmm_alloc_and_map(void *proc_desc, uintptr_t virt_addr, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access, uint8_t commit)
{
	HRESULT 	hr;
	void 		*phys_addr;
	K_PROCESS	*proc = proc_desc;

	/* Allocate _size_ bytes of physical memory */
	hr = kpmm_alloc(size / KPMM_BLOCK_SIZE, &phys_addr);
	if (FAILED(hr)) return hr;

	/* Marking region with usage flag AUTOFREE will cause the region to be freed via the
	 * physical memory manager when region is being unmapped.
	 */
	return vmm_map_region(proc, (uintptr_t)phys_addr, virt_addr, size, usage | USAGE_AUTOFREE, access, commit);
}

HRESULT __nxapi vmm_alloc_and_map_limited(void *proc_desc, uintptr_t virt_addr, uintptr_t limit, size_t size, K_VMM_REGION_USAGE usage, K_VMM_ACCESS_FLAG access, uint8_t commit)
{
	HRESULT 	hr;
	void 		*phys_addr;
	K_PROCESS	*proc = proc_desc;

	/* Allocate _size_ bytes of physical memory */
	hr = kpmm_alloc(size / KPMM_BLOCK_SIZE, &phys_addr);
	if (FAILED(hr)) return hr;

	/* Enforce limit */
	if (((uintptr_t)phys_addr + size) > limit) {
		kpmm_unmark_blocks(phys_addr, size / KPMM_BLOCK_SIZE);
		return E_FAIL;
	}

	if (proc == NULL) {
		/* Fetch kernel process desc */
		hr = sched_get_process_by_id(0, (K_PROCESS**)&proc);
		//vga_printf("sched_get_proc(): hr=%x. proc=%x\n", hr, proc->name);
		if (FAILED(hr)) return hr;
	}

	/* Marking region with usage flag AUTOFREE will cause the region to be freed via the
	 * physical memory manager when region is being unmapped.
	 */
	return vmm_map_region(proc, (uintptr_t)phys_addr, virt_addr, size, usage | USAGE_AUTOFREE, access, commit);
}

HRESULT __nxapi vmm_unmap_region(void *proc_desc, uint_ptr_t virt_addr, int commit)
{
	//todo: we should disable interrupts here or lock using mutex

	K_PROCESS *proc = proc_desc;

	uint32_t i, id;
	K_VMM_REGION *r = NULL;
	HRESULT hr;

	if (proc_desc == NULL) {
		/* Fetch kernel process desc */
		hr = sched_get_process_by_id(0, (K_PROCESS**)&proc);
		if (FAILED(hr)) return hr;
	}

	/* Find the region, by iterating the region array */
	for (i=0; i<proc->region_count; i++) {
		if (proc->regions[i].virt_addr == virt_addr) {
			/* Found */
			r = &proc->regions[i];
			id = i;
			break;
		}
	}

	if (!r) {
		/* Region not found */
		return E_INVALIDARG;
	}

	/* Calculate number of page frames of region */
	uint32_t num_blocks = r->region_size / VM_PAGE_FRAME_SIZE;
	assert(num_blocks > 0);

	uintptr_t virt_addr_idx;

	/* Unmap each block one by one */
	for (virt_addr_idx=virt_addr; virt_addr_idx<virt_addr+r->region_size; virt_addr_idx+=VM_PAGE_FRAME_SIZE) {
		uint32_t page_table_id = (virt_addr_idx / 0x1000) / 1024;
		uint32_t page_id = (virt_addr_idx / 0x1000) % 1024;
		K_VMM_PAGE_TABLE *table;

		HRESULT hr = fetch_page_table(proc->page_dir, page_table_id, 0, 0, 0, &table);
		if (FAILED(hr)) {
			/* Failed to fetch table with _page_table_id_ id. */
			return hr;
		}

		/* Mark page as non-present */
		table->pages[page_id].f_present = 0;
	}

	if (commit) {
		/* Invalidate TLB cache */
		for (virt_addr_idx=virt_addr; virt_addr_idx<virt_addr+r->region_size; virt_addr_idx+=VM_PAGE_FRAME_SIZE) {
			HalInvalidatePage((void*)virt_addr_idx);
		}
	}

	/* Free physical memory, if region is market with AUTOFREE usage flag */
	if ((r->usage & USAGE_AUTOFREE) != 0) {
		assert(r->region_size % VM_PAGE_FRAME_SIZE == 0);

		HRESULT hr = kpmm_unmark_blocks((void*)r->phys_addr, r->region_size / VM_PAGE_FRAME_SIZE);
		if (FAILED(hr)) return hr;
	}

	/* Remove region from array */
	for (i=id; i<proc->region_count-1; i++) {
		proc->regions[i] = proc->regions[i+1];
	}
	proc->region_count--;

	return S_OK;
}

HRESULT __nxapi vmm_temp_map_region(void *proc_desc, uintptr_t phys_addr, uint32_t region_size, uintptr_t *virt_addr)
{
	K_PROCESS *proc = proc_desc;
	uint32_t addr;

	//TODO: Should we lock??

	for (addr=KERNEL_TEMP_START; addr<(0xFFFFFFFF-region_size); addr+=KPMM_BLOCK_SIZE) {
		uint32_t range_start = addr, range_end = addr + region_size;
		BOOL	 avail = TRUE;

		/* Check if current address is available */
		for (uint32_t i=0; i<proc->region_count; i++) {
			K_VMM_REGION *r = &proc->regions[i];

			//k_printf("region (va=%x, pa=%x, size=%x)\n", r->virt_addr, r->phys_addr, r->region_size);

			uint8_t overlap =
				((r->virt_addr >= range_start && r->virt_addr < range_end) ||
				(r->virt_addr+r->region_size > range_start && r->virt_addr+r->region_size <= range_end)) ? 1 : 0;

			if (overlap) {
				/* Not available */
				assert((r->virt_addr + r->region_size) % KPMM_BLOCK_SIZE == 0)
				addr = r->virt_addr + r->region_size - KPMM_BLOCK_SIZE;

				avail = FALSE;
				break;
			}
		}

		//k_printf("testing range[%x..%x]: %s\n", addr, addr+region_size, avail ? "available" : "not available");

		if (!avail) {
			continue;
		}

		/* Available */
		HRESULT hr = vmm_map_region(proc_desc, phys_addr, addr, region_size, USAGE_TEMP, ACCESS_READWRITE, 1);
		if (FAILED(hr)) return hr;

		/* Assign output parameter */
		*virt_addr = addr;

		return S_OK;
	}

	return E_FAIL;
}

HRESULT __nxapi vmm_get_region_phys_addr(void *proc_desc, uintptr_t virt_addr, uintptr_t *phys_addr)
{
	if (proc_desc == NULL) {
		HRESULT hr = sched_get_process_by_id(0, (K_PROCESS**)&proc_desc);
		if (FAILED(hr)) return hr;
	}

	K_PROCESS *proc = proc_desc;

	for (uint32_t i=0; i<proc->region_count; i++) {
		if (proc->regions[i].virt_addr == virt_addr) {
			/* Found */
			*phys_addr = proc->regions[i].phys_addr;
			return S_OK;
		}
	}

	return E_NOTFOUND;
}
