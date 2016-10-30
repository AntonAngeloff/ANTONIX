#Enable intel syntax
.intel_syntax noprefix

# Declare constants used for creating a multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

# Declare a header as in the Multiboot Standard. We put this into a special
# section so we can force the header to be in the start of the final program.
# You don't need to understand all these details as it is just magic values that
# is documented in the multiboot standard. The bootloader will search for this
# magic sequence and recognize us as a multiboot kernel.
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# This is the virtual base address of kernel space. It must be used to convert virtual
# addresses into physical addresses until paging is enabled. Note that this is not
# the virtual address where the kernel image itself is loaded -- just the amount that must
# be subtracted from a virtual address to get a physical address.
.set KERNEL_VIRTUAL_BASE, 0xC0000000                  # 3GB
.set KERNEL_PAGE_NUMBER, (KERNEL_VIRTUAL_BASE >> 22)  # Page directory index of kernel's 4MB PTE.

# reserve initial kernel stack space -- that's 16k.
.set STACKSIZE, 0x4000

# The linker script specifies _start as the entry point to the kernel and the
# bootloader will jump to this position once the kernel has been loaded. It
# doesn't make sense to return from this function as the bootloader is gone.
.section .text

# Setting up entry point for linker
.set start_phys, (_start - 0xC0000000)
.global start_phys

.global _start
.type _start, @function
_start:
    # NOTE: Until paging is set up, the code must be position-independent and use physical
    # addresses, not virtual ones!
    #NOT WORKING -> mov ecx, (boot_page_directory - KERNEL_VIRTUAL_BASE)
    lea ecx, boot_page_directory - KERNEL_VIRTUAL_BASE

    # Load Page Directory Base Register.
    mov cr3, ecx

    # Set PSE bit in CR4 to enable 4MB pages.
    mov ecx, cr4
    or 	ecx, 0x00000010
    mov cr4, ecx

    # Set PG bit in CR0 to enable paging.
    mov ecx, cr0
    or 	ecx, 0x80000000
    mov cr0, ecx

    # Start fetching instructions in kernel space.
    # Since eip at this point holds the physical address of this command (approximately 0x00100000)
    # we need to do a long jump to the correct virtual address of StartInHigherHalf which is
    # approximately 0xC0100000.
    lea ecx, [_start_in_higher_half]
    jmp ecx

_start_in_higher_half:
    # Unmap the identity-mapped first 4MB of physical address space. It should not be needed
    # anymore.
    #mov dword ptr [boot_page_directory], 0
    #invlpg [0]

	# To set up a stack, we simply set the esp register to point to the top of
	# our stack (as it grows downwards).
	lea	esp, stack_top

	# Pass multiboot magic number
	push eax

    # Pass Multiboot info structure -- WARNING: This is a physical address and may not be
    # in the first 4MB!
    add	ebx, KERNEL_VIRTUAL_BASE
	push ebx

	# We are now ready to actually execute C code. We cannot embed that in an
	# assembly file, so we'll create a kernel.c file in a moment. In that file,
	# we'll create a C entry point called kernel_main and call it here.
	call _kernel_main

	# In case the function returns, we'll want to put the computer into an
	# infinite loop. To do that, we use the clear interrupt ('cli') instruction
	# to disable interrupts, the halt instruction ('hlt') to stop the CPU until
	# the next interrupt arrives, and jumping to the halt instruction if it ever
	# continues execution, just to be safe. We will create a local label rather
	# than real symbol and jump to there endlessly.
	cli
	hlt

.inf_loop:
	jmp .inf_loop

# Set the size of the _start symbol to the current location '.' minus its start.
# This is useful when debugging or when you implement call tracing.
.size _start, . - _start

.section .data
.align 0x1000
boot_page_directory:
    # This page directory entry identity-maps the first 4MB of the 32-bit physical address space.
    # All bits are clear except the following:
    # bit 7: PS The kernel page is 4MB.
    # bit 1: RW The kernel page is read/write.
    # bit 0: P  The kernel page is present.
    # This entry must be here -- otherwise the kernel will crash immediately after paging is
    # enabled because it can't fetch the next instruction! It's ok to unmap this page later.
    .long 0x00000083
    .rept (KERNEL_PAGE_NUMBER - 1)
    	# Pages before kernel space.
    	.long 0
    .endr

    # This page directory entry defines a 4MB page containing the kernel.
    .long 0x00000083
    .rept (1024 - KERNEL_PAGE_NUMBER - 1)
    	.long 0
    .endr

# Currently the stack pointer register (esp) points at anything and using it may
# cause massive harm. Instead, we'll provide our own stack. We will allocate
# room for a small temporary stack by creating a symbol at the bottom of it,
# then allocating 16384 bytes for it, and finally creating a symbol at the top.
.section .bootstrap_stack, "aw", @nobits
.align 32
stack_bottom:
	.skip STACKSIZE
stack_top:
