include Makefile.inc

KERNEL_IMAGE= antonixkrnl.bin
LD_SCRIPT	= linker.ld

# Describe C source code files
C_FILES		=	desctables.c \
				kernel.c \
				ps2.c \
				timer.c \
				vfs.c \
				url_utils.c \
				initrd.c \
				kconsole.c \
				kstdio.c \
				mm.c \
				mm_skheap.c \
				mm_phys.c \
				mm_virt.c \
				syncobjs.c \
				vga.c \
				scheduler.c \
				elf.c \
				syscall.c \
				isa_dma.c \
				devices.c \
				kdbg.c

# Describe assembly source code files
ASM_FILES	=	boot.s \
				hal.s \
				isr.s

C_OBJS		= $(C_FILES:.c=.o)
ASM_OBJS	= $(ASM_FILES:.s=.o)
BUILD_DIR	= bin/

# Object files for "drivers" subdirectory
DRIVERS_OBJ_BD := `cd drivers; make obj-list; cd ..`
LIBC_OBJ_BD := `cd libc; make obj-list; cd ..`
SUBSYSTEMS_OBJ_BD := `cd subsystems; make obj-list; cd ..`

C_OBJS_BD	=	$(addprefix $(BUILD_DIR),$(C_OBJS))
ASM_OBJS_BD =	$(addprefix $(BUILD_DIR),$(ASM_OBJS))

INCLUDES = -I"include/" -I"include/std/"

.PHONY: all clean drivers libc subsystems install
all : ntonix

ntonix: $(C_OBJS_BD) build-asm drivers libc subsystems
	@echo "Linking..."
	$(GCC) -T $(LD_SCRIPT) -o "$(BUILD_DIR)$(KERNEL_IMAGE)" -ffreestanding -nostdlib $(ASM_OBJS_BD) $(C_OBJS_BD) $(DRIVERS_OBJ_BD) $(LIBC_OBJ_BD) $(SUBSYSTEMS_OBJ_BD) -lgcc 	
	
build-asm: 
	@echo "Compiling asm files..."		
	@$(GAS) "boot.asm" -o "bin/boot.o"
	@$(GAS) "hal.asm" -o "bin/hal.o"
	@$(GAS) "isr.asm" -o "bin/isr.o"
	
$(BUILD_DIR)%.o: %.c
	@echo Compiling file \"$<\"...
	@$(GCC) $(INCLUDES) -c "$<" -o "$@" $(CFLAGS)
	
drivers:
	@echo Building files for directory \"$@/\"
	@mkdir -p $(BUILD_DIR)$@
	cd drivers; make; 
	
libc:
	@echo Building files for directory \"$@/\"
	@mkdir -p $(BUILD_DIR)$@
	cd libc; make; 
	
subsystems:
	@echo Building files for directory \"$@/\"
	@mkdir -p $(BUILD_DIR)$@
	cd subsystems; make; 
	
install: ntonix
	@echo "Copying kernel image to floppy disk..."
	cp "bin/antonixkrnl.bin" "a:\\boot\\"
	
install-dbg: install
	@echo "Recreating dump file..."
	@rm "dump.txt"
	@objdump  -M intel -D "bin/antonixkrnl.bin" >> dump.txt
	
clean:
	@$(RM) -f bin/subsystems/*.o
	@$(RM) -f bin/drivers/*.o
	@$(RM) -f bin/libc/*.o
	@$(RM) -f bin/*.o
	@$(RM) -f bin/*.bin
	@echo The coast is clear!