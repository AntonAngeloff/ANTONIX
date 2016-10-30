# Define tools
GAS = i686-elf-as
NASM = nasm
AS = $(GAS)
LD = i686-elf-ld
RM = rm

# GCC flags 
# Not including -fno-optimize-sibling-calls in flags, will cause kernel to fail on interrupts
CFLAGS = -std=gnu99 -ffreestanding -fleading-underscore -fno-optimize-sibling-calls -DKERNEL_MODE -O0 -g -Wall -Wextra

# Describe assembly source code files
ASM_FILES	=	main.s
ASM_OBJS	= $(ASM_FILES:.s=.o)

.PHONY: all clean helloworld

all : helloworld

helloworld:  
	@echo "Compiling asm files..."		
	@$(GAS) "main.s" -o "main.o"
	@echo "Linking..."
	@$(LD) -o helloworld.elf main.o
	
clean:	
	@$(RM) *.o 
	@$(RM) *.elf
	@echo "All clean..."
