ANTONIX OS is a hobby operating system kernel built from scratch, partly as a university project at University of Rousse "Angel Kanchev", Bulgaria.

It is written in C entirely and is designed for IA-32 architecture group. Following are the major features which the kernel supports:
 - Higher half memory model - address beyond 3GB mark is used for kernel and below is for user space.
 - Three-layer memory manager - physical memory manager, heap manager (responsible for mapping and memory heaps to processes) and end-user memory manager (scatters heaps into smaller memory blocks, and provides those by malloc() and friends API)
 - Virtual file system - supports memory files, mounting of devices (by drivers) and mounting of "real" file systems to mountpoint inside the virtual file system.
 - Initial ram disc (INITRD) - the kernel stores important files inside the kernel binary image and deploys them onto the VFS during the boot process.
 - Preemptive multitasking - supports multiple processes with multiple threads. Round-robin algorithm is used currently so thread priority is not supported at this point.
 - Synchronization mechanism - mutices (mutexes), signals and spinlocks.
 - Built-in containers - linked lists and thread-safe ring buffers.
 - Device driver API
 - Timer API - which is PIT-based.
 - ELF loader - parses and loads ELF executables (no dynamic linking is supported at this time).
 - Syscall API
 
Drivers:
 - Text mode VGA minidriver
 - PS\2 Keyboard 
 - PS\2 Mouse (no support for scrolling)
 - Built-in ISA DMA minidriver.
 - Creative SoundBlaster 16 (ISA) - audio card driver.
 - Floppy Disc Controller (Intel 82077AA FDC) - Model 30
 - FAT12 and FAT16 filesystem read-only drivers.
 - PCI bus minidriver - currently supports only scanning the bus. IRQ and memory remapping for PCI devices is currently not available.
 - VESA video driver - BGA-based initialization of video mode is only supported currently, this means it is not available on real hardware, but only some emulators.
 
Subsystems:
 - NXS (ANTONIX Sound interface) - a simple API for playing sound, which is used as abstract layer around the sound card driver interface.
 - NXGI (ANTONIX Graphics Interface) - supports common 2d graphical operations
 
Desirable long-term features (up to come):
 - Native USB drivers
 - UDI (Unified Driver Interface) host implementation.
 - FUSE host implementation.
 - ACPI support
 - SMP (symmetric multi-processor) support.
 - ELF dynamic linking.
 - Own LIBC implementation.
 - Expand Henjin CWS
 - Relative POSIX compilance
 - Port GCC
 - Port SDL
 
Henjin
Henjin is a work-in-progress window compositing manager and display server. It uses NXGI underneath to draw windows and controls and so on.
The display server part is responsible for changing video resolution and translating mouse driver and keyboard events to Henjin messages (HJ_MESSAGE).

 
