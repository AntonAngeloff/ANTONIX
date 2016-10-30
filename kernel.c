/*
 * ANTONIX OS kernel glue code.
 */

#include <stddef.h>
#include <stdint.h>
#include <vga.h>
#include <desctables.h>
#include <hal.h>
#include <ps2.h>
#include <types.h>
#include <timer.h>
#include <multiboot.h>
#include <kstdio.h>
#include <mm.h>
#include <kdbg.h>
#include <isa_dma.h>
#include "include/mm_skheap.h"
#include "include/mm_virt.h"
#include "include/mm_phys.h"
#include <vfs.h>
#include <string.h>
#include <kconsole.h>
#include "scheduler.h"
#include "syscall.h"
#include "include/devices.h"
#include "drivers/console.h"
#include "drivers/sound_blaster16.h"
#include "drivers/floppy.h"
#include "drivers/fat16.h"
#include "drivers/vesa_video.h"
#include "subsystems/nxa.h"
#include <stdlib.h>

void __nxapi kernel_initialize(multiboot_info_t* mbt)
{
	/* Initialize debugger */
	kdbg_init();

	/* Initialize terminal interface */
	vga_init_terminal();

	/* Initialize GDT */
	DPRINT("Initializing GDT...\n");
	gdt_initialize();

	/* Initialize IDT */
	DPRINT("Initializing IDT...\n");
	idt_initialize();

	/* Initialize PIT timer */
	DPRINT("Initializing PIT timer...\n");
	timer_initialize(20);

	/* Initialize memory manager subsystems */
	DPRINT("Initializing memory management system...\n");
	kpmm_init(mbt);
	skheap_init();
	vmm_init();
	//skheap_selftest();
	//vmm_selftest();

	DPRINT("Initializing syscall gateway...\n");
	syscall_init();

	DPRINT("Initializing (ISA)DMA driver...\n");
	isadma_initialize();
}

void __nxapi install_drivers()
{
	HRESULT hr;

	DPRINT("Initializing initrd...\n");
	initrd_init();
//	test_file_io();


	/* Initialize PS2 controller and keyboard. This will
	 * install the mouse driver, if mouse is found.
	 */
	DPRINT("Initializing PS\\2 controller...\n");
	if(FAILED(ps2_initialize())) HalKernelPanic("Fail!");

	DPRINT("Initializing /dev/console...\n");
	hr = con_initialize();
	if (FAILED(hr)) { vga_printf("%d", hr); HalKernelPanic("Failed."); }

	DPRINT("Initializing /dev/sb16...\n");
	hr = sb16_install();
	if (FAILED(hr)) { vga_printf("%d", hr); HalKernelPanic("Failed."); }

	DPRINT("Initializing /dev/fdd0...\n");
	hr = fdc_install();
	if (FAILED(hr)) { k_printf("Failed (hr=%d).", hr); }
	//hr = fdc_selftest();
	//if (FAILED(hr)) HalKernelPanic("failed test.");

	/* Graphics driver */
	DPRINT("Initializing VESA video driver (/dev/video0).\n")
	hr = vesa_install();
	if (FAILED(hr)) { k_printf("Failed (hr=%d).", hr); }

	/* Mount floppy to vfs */
//	DPRINT("Mounting /dev/fdd0 to /drives/a...\n");
//	hr = vfs_mount_fs("/drives/a", fat16_get_constructor(), "/dev/fdd0");
//	if (FAILED(hr)) { k_printf("Failed (hr=%x).", hr); }

	/* Initialize audio subsystem */
	hr = nxa_initialize(NULL);
	if (FAILED(hr)) { k_printf("%x", hr); HalKernelPanic("Failed to initialize audio subsystem."); }
}

void __nxapi kernel_shutdown()
{
	//Nothing so far
}

void __nxapi correct_mbt_info(multiboot_info_t* mbt)
{
	/* Since the mbt's field pointers point to memory in region [0mb..4mb]
	 * we need to offset them to point to the higher half region.
	 */
	mbt->mmap_addr += 0xC0000000;
}

static void __nxapi kernel_main_thread()
{
	/* Initialize virtual file system */
	DPRINT("Initializing virtual file system...\n");
	vfs_init();
//	vfs_selftest();

	install_drivers();

	/* Enter to kernel console */
	DPRINT("Initializing kconsole...\n");
	HalDisplayString("Hello from ANTONIX kernel! For available command type \"help\".\n\n");

	kcon_initialize();
	kcon_prompt();

	while(TRUE){
		char c = getch();
		kcon_process_char(c);
	}

	while (1) { ; }
}

/**
 * Kernel's entry point
 */
void kernel_main(multiboot_info_t* mbt, unsigned int magic) {
	//HalKernelPanic("panic!!!");

	correct_mbt_info(mbt);
	kernel_initialize(mbt);

	/* Dump reserved memory regions */
	UNUSED_ARG(magic);
//	int i=0;

	memory_map_t* mmap = (memory_map_t*)mbt->mmap_addr;
	while((unsigned long)mmap < mbt->mmap_addr + mbt->mmap_length) {
		//uint64_t base_addr = (uint64_t)(mmap->base_addr_high) << 32 | mmap->base_addr_low;
		//uint64_t length = (uint64_t)(mmap->length_high)<< 32 | mmap->length_low;
//		uint32_t base_addr = mmap->base_addr_low;
//		uint32_t length = mmap->length_low;
//
//		if (mmap->type == 1) {
//			vga_printf("%d. Usable memory region [%x..%x]", ++i, base_addr, base_addr + length);
//		}else if(mmap->type == 2) {
//			vga_printf("%d. Reserved memory region [%x..%x]", ++i, base_addr, base_addr + length);
//		}else {
//			vga_printf("%d. Unknown memory region [%x..%x]", ++i, base_addr, base_addr + length);
//		}
//		vga_printf("; size: %dKiB\n", length / 1024);

		/* Next */
		mmap = (memory_map_t*) ( (unsigned int)mmap + mmap->size + sizeof(unsigned int) );
	}

	/* We'll initialize multitasking here */
	DPRINT("Initializing scheduler...\n");

	HRESULT hr = sched_initialize(kernel_main_thread);
	if (FAILED(hr)) HalKernelPanic("Failed to initialize scheduler.");

	HalKernelPanic("we should not reach here.");
}
