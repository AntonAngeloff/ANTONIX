# Define tools
GCC = i686-elf-gcc
GAS = i686-elf-as
NASM = nasm
AS = $(GAS)
LD = i686-elf-ld
RM = rm

# GCC flags 
# Not including -fno-optimize-sibling-calls in flags, will cause kernel to fail on interrupts
CFLAGS = -std=gnu99 -ffreestanding -fleading-underscore -fno-optimize-sibling-calls -DKERNEL_MODE -O0 -g -Wall -Wextra

SHELL	= /bin/sh

.SILENT :
