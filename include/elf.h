/*
 * elf.h
 *
 *  Created on: 3.08.2016 ã.
 *      Author: Anton Angeloff
 */

/**
 * @brief ELF (Executable and Linkable Format) file loader for ANTONIX.
 */

#ifndef INCLUDE_ELF_H_
#define INCLUDE_ELF_H_

#include "kstdio.h"

#define ELF_MAGIC				0x7F

#define ELF_CLASS_NONE			0x00
#define ELF_CLASS_32BIT			0x01
#define ELF_CLASS_64BIT			0x02

#define ELF_TYPE_NONe			0x00
#define ELF_TYPE_RELOCATABLE	0x01
#define ELF_TYPE_EXECUTABLE		0x02
#define ELF_TYPE_SHARED			0x03
#define ELF_TYPE_CORE			0x04

#define ELF_ARCH_SPARC			0x02
#define ELF_ARCH_X86			0x03
#define ELF_ARCH_MIPS			0x08
#define ELF_ARCH_POWERPC		0x14
#define ELF_ARCH_ARM			0x28
#define ELF_ARCH_SUPERH			0x2A
#define ELF_ARCH_IA64			0x32
#define ELF_ARCH_X86_64			0x3E
#define ELF_ARCH_AARCH64		0xB7

/**
 * The array element is unused; other members’ values are undefined.
 * This type lets the program header table have ignored entries.
 */
#define ELF_PT_NULL		0x00
#define ELF_PT_LOAD		0x01
#define ELF_PT_DYNAMIC	0x02
#define ELF_PT_INTERP	0x03
#define ELF_PT_NOTE		0x04
#define ELF_PT_SHLIB	0x05
#define ELF_PT_PHDR		0x06

/* Symbolic constants for value of K_ELF_PROGR_HDR_32.flags */
#define ELF_PF_X		0x01
#define ELF_PF_W		0x02
#define ELF_PF_R		0x04
#define ELF_PF_MASKRPOC	0xF0000000

typedef struct ELF_HEADER_32 K_ELF_HEADER_32;
struct ELF_HEADER_32 {
	uint8_t		magic[4];
	uint8_t		class;
	uint8_t		little_endian;
	uint8_t		version;
	uint8_t		os_abi;
	uint8_t		padding[8];

	uint16_t	type;
	uint16_t	arch;
	uint32_t	version2;

	uint32_t	entry;
	uint32_t	prog_hdr_table;
	uint32_t	sect_hdr_table;
	uint32_t	flags;
	uint16_t	hdr_size;

	uint16_t	pht_entry_size;
	uint16_t	pht_num_entries;
	uint16_t	sht_entry_size;
	uint16_t	sht_num_entries;
	uint16_t	sht_index;
};

typedef struct ELF_PROGR_HDR_32 K_ELF_PROGR_HDR_32;
struct ELF_PROGR_HDR_32 {
	uint32_t	seg_type;
	uint32_t	p_offset;
	uint32_t	p_vaddr;
	uint32_t	reserved;
	uint32_t	size_in_file;
	uint32_t	size_mem;
	uint32_t	flags;
	uint32_t	align;
};

/**
 *  Tests a given stream if it is a valid ELF file/stream
 */
HRESULT elf_probe(K_STREAM *s);

/**
 * Creates a process and executes an ELF program residing in the given
 * stream.
 */
HRESULT elf_execute(K_STREAM *s, uint32_t *pid);

#endif /* INCLUDE_ELF_H_ */
