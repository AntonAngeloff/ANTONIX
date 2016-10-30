/*
 * elf.c
 *
 *  Created on: 3.08.2016 ã.
 *      Author: Anton Angeloff
 */

#include "elf.h"
#include "scheduler.h"
#include "string.h"
#include "mm.h"
#include "hal.h"
#include "vga.h"

HRESULT elf_probe(K_STREAM *s)
{
	HRESULT hr = S_OK;

	/* Get current position, to restore it later. */
	uint32_t pos = k_ftell(s);

	/* Read header */
	K_ELF_HEADER_32	hdr;
	size_t			bytes;

	hr = k_fread(s, sizeof(K_ELF_HEADER_32), &hdr, &bytes);
	if (FAILED(hr) || bytes!=sizeof(K_ELF_HEADER_32)) {
		hr = E_FAIL;
		goto finally;
	}

	/* Elf magic */
	if (hdr.magic[0] != 0x7F || hdr.magic[1] != 'E' || hdr.magic[2] != 'L' || hdr.magic[3] != 'F') {
		hr = E_INVALIDDATA;
		goto finally;
	}

	/* Check class */
	if (hdr.class != ELF_CLASS_32BIT) {
		hr = E_FAIL;
		goto finally;
	}

	/* Check endiannes */
	if (hdr.little_endian != 1) {
		hr = E_FAIL;
		goto finally;
	}

	/* Check target arch */
	if (hdr.arch != ELF_ARCH_X86) {
		hr = E_FAIL;
		goto finally;
	}

	/* OK. We assume it's fine. */

finally:
	if (k_fseek(s, pos, KSTREAM_ORIGIN_BEGINNING) != pos) {
		return E_FAIL;
	}

	return hr;
}

HRESULT elf_load_from_memory(uint8_t *buff, uint32_t *pid_out)
{
	K_ELF_HEADER_32	*hdr = (void*)buff;
	HRESULT hr;

	/* Make sure it's executable elf */
	if (hdr->type != ELF_TYPE_EXECUTABLE) {
		return E_FAIL;
	}

	/* Create new process */
	K_PROCESS 	*proc;
	uint32_t	pid;

	hr = sched_create_process((void*)hdr->entry, PROCESS_PRIORITY_NORMAL, &pid);
	if (FAILED(hr)) return hr;

	hr = sched_find_proc(pid, &proc);
	if (FAILED(hr)) HalKernelPanic("elf_load_from_memory(): Failed to find process.");

	/* Map kernel code */
	hr = vmm_map_region(proc, 0x00000000, 0xC0000000, 4*1024*1024, USAGE_KERNEL, ACCESS_READWRITE, FALSE);
	if (FAILED(hr)) {
		HalKernelPanic("Failed to map region [0x0..0x00100000] to [0xC0000000..0xC0100000].");
	}

	/* Find first program header */
	K_ELF_PROGR_HDR_32 *ph = (K_ELF_PROGR_HDR_32*)(buff + hdr->prog_hdr_table);

	/* Parse program headers */
	for (unsigned int i=0; i<hdr->pht_num_entries; i++, ph++) {
		switch (ph->seg_type) {
		case ELF_PT_NULL:
			/* The array element is unused; other members’ values are undefined.
			 * This type lets the program header table have ignored entries.
			 */
			break;

		case ELF_PT_LOAD: {
			/* The array element specifies a loadable segment, described by
			 * p_filesz and p_memsz. The bytes from the file are mapped to the
			 * beginning of the memory segment. Extra bytes are set to zero.
			 */
			uint32_t usage  = (ph->flags & ELF_PF_W) == 0 ? USAGE_CODE : USAGE_DATA;
			uint32_t access = (ph->flags & ELF_PF_W) != 0 ? ACCESS_READWRITE : ACCESS_READ;

			/* p_vaddr and size should be page aligned. */
			uint32_t align_excess = ph->p_vaddr % VM_PAGE_FRAME_SIZE;
			uint32_t new_vaddr	  = ph->p_vaddr - align_excess;
			uint32_t size 		  = ph->size_mem + (ph->p_vaddr - new_vaddr);

			/* Make size be multiple of VM_PAGE_FRAME_SIZE */
			size += VM_PAGE_FRAME_SIZE - (size % VM_PAGE_FRAME_SIZE);

			hr = vmm_alloc_and_map(proc, new_vaddr, size, usage | USAGE_USER, access, 0);
			if (FAILED(hr)) HalKernelPanic("Failed to allocate and map ELF segment.");

			uintptr_t phys_addr;
			hr = vmm_get_region_phys_addr(proc, new_vaddr, &phys_addr);
			if (FAILED(hr)) HalKernelPanic("Failed to find region physical address.");

			/* We will need spinlock's interrupt disabling ability. Actually we don't
			 * need the very spinlock.
			 */
			K_SPINLOCK temp_lock;
			spinlock_create(&temp_lock);
			uint32_t iflag = spinlock_acquire(&temp_lock);

			/* Copy segment to memory. As we know from experience, we have to
			 * temporary map the region in order to copy to it.
			 */
			K_PROCESS *cur_proc;
			hr = sched_get_current_proc(&cur_proc);
			if (FAILED(hr)) HalKernelPanic("Failed to retrieve current process pcb.");

			uintptr_t temp_vaddr;
			hr = vmm_temp_map_region(cur_proc, phys_addr, size, &temp_vaddr);
			if (FAILED(hr)) HalKernelPanic("Failed to tempmap.");

			/* Copy segment */
			assert(ph->size_mem > 0);
			memcpy((void*)(temp_vaddr + align_excess), buff + ph->p_offset, ph->size_in_file);

			/* Initialize segment if needed */
			if (ph->size_in_file != ph->size_mem) {
				memset((void*)(temp_vaddr + align_excess + ph->size_in_file), 0, ph->size_mem-ph->size_in_file);
			}

			/* Unmap temp map */
			hr = vmm_unmap_region(cur_proc, temp_vaddr, 1);
			if (FAILED(hr)) HalKernelPanic("Failed to unmap temp region.");

			spinlock_release(&temp_lock, iflag);
			break;
		}

		case ELF_PT_PHDR:
			/* The array element, if present, specifies the location and size
			 * of the program header table itself, both in the file and in the
			 * memory image of the program.
			 */
			break;

		default:
			HalKernelPanic("Unhandled program header type.");
		}
	}

	if (pid_out != NULL) {
		*pid_out = pid;
	}

	/* Add main thread to scheduler's run queue */
	hr = sched_add_thread_to_run_queue(proc->threads[0]);

	return hr;
}

HRESULT elf_execute(K_STREAM *s, uint32_t *pid)
{
	uint32_t pos, endpos, size, bytes;
	HRESULT hr;

	/* Test if stream is valid ELF format */
	hr = elf_probe(s);
	if (FAILED(hr)) return hr;

	/* Get current position. */
	pos = k_ftell(s);

	/* Get end position */
	k_fseek(s, 0, KSTREAM_ORIGIN_END);
	endpos = k_ftell(s);
	k_fseek(s, pos, KSTREAM_ORIGIN_BEGINNING);

	size = endpos - pos;

	/* Allocate buffer to download the elf content from stream */
	uint8_t 		*buff = kmalloc(size);

	/* Read header */
	hr = k_fread(s, size, buff, &bytes);
	if (FAILED(hr) || bytes!=size) {
		hr = E_FAIL;
		goto finally;
	}

	hr = elf_load_from_memory(buff, pid);

finally:
	kfree(buff);
	return hr;
}
