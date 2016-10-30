/*
 * types.h
 *
 *	Basic type definitions.
 *
 *  Created on: 10.11.2015 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_TYPES_H_
#define INCLUDE_TYPES_H_

#include <stdint.h>


/*
 * Define kernel name and version
 */
#define KERNEL_NAME	"ANTONIX"
#define KERNEL_VERSION_MAJ	0
#define KERNEL_VERSION_MIN	2

/**
 * Define __cdecl macro
 */
#define __cdecl __attribute__((__cdecl__))

/**
 * Define __stdcall macro
 */
#define __stdcall __attribute__((__stdcall__))

/**
 * Define __safecall macro
 */
#define __safecall __attribute__((__safecall__))

/**
 * Define __weak macro
 */
#define __weak __attribute__((__weak__))

/**
 * Define __packed macro
 */
#define __packed __attribute__((__packed__))

/**
 * __noinline macro
 */
#define __noinline __attribute__ ((noinline))

/**
 * Define default calling convention
 */
#define __nxapi 	__cdecl
#define __callback	__cdecl
#define __isr		__cdecl
//#define __ntapi

/*
 * Defining basic NT types
 */
typedef char * PCHAR;
typedef char CHAR;

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t QWORD;

typedef void VOID;
typedef void * PVOID;
typedef PVOID LPVOID;

typedef uint32_t uint_ptr_t;
typedef uint32_t size_t;

typedef unsigned char BOOL;

//Define HRESULT
typedef DWORD HRESULT;

#define S_OK			0x00
#define S_FALSE			0x01
#define E_INVALIDARG	0x10
#define E_POINTER		0x11
#define E_FAIL			0x12
#define E_NOTIMPL		0x13
#define E_ENDOFSTR		0x14
#define E_ACCESSDENIED	0x15
#define E_NOTFOUND		0x16
#define E_OUTOFMEM		0x17
#define E_INVALIDDATA	0x18
#define E_NOTSUPPORTED	0x19
#define E_INVALIDSTATE	0x1A
#define E_TIMEDOUT		0x1B
#define E_BUFFEROVERFLOW	0x1C
#define E_BUFFERUNDERFLOW	0x1D
#define E_UNEXPECTED	0x1E
#define E_TERMINATED	0x1F

#define hr_to_str(x) ("hr_to_str not impl.")

/**
 * Define SUCCEEDED and FAIL macros
 */
#define SUCCEEDED(x) (x==S_OK || x==S_FALSE)
#define FAILED(x) (x!=S_OK && x!=S_FALSE)

/**
 * Assertation macro
 */
#define debug
#ifdef debug
#define DPRINT2(s) \
	HalDisplayString("DPRINT("); \
	HalDisplayString(__FILE__); \
	HalDisplayString(":"); \
	k_printf("%d", __LINE__); \
	HalDisplayString("): "); \
	HalDisplayString(s);
#else
#define DPRINT2(s)
#endif


//#ifdef debug
	#define assert(x) if(!(x)) { DPRINT2("Assertation failed."); HalKernelPanic(""); }
	#define assert_msg(x, y) if(!(x)) { DPRINT(y); HalKernelPanic(""); }
//#else
//	#define assert(x) /* nothing in release */
//#endif

/**
 * This macro can be used on intentionally unused parameters
 * to avoid warning emission.
 */
#define UNUSED_ARG(x) (void)(x)

#define TRUE 1
#define FALSE 0

/**
 * Interrupt Description Table Entry
 */
typedef struct {
	//bits 0..15 of interrupt routine (ir) address
	WORD base_low;

	//code selector in gdt
	WORD selector;

	//reserved (should be 0)
	BYTE reserved;

	//bit flags. Set with flags above
	BYTE flags;

	//bits 16-31 of ir address
	WORD base_high;
} __packed K_IDTENTRY;

/**
 * IDT Pointer
 */
typedef struct __packed {
	//size of the interrupt descriptor table (idt)
	WORD limit;

	//base address of idt
	DWORD addr;
} K_IDTPOINTER;

/**
 * Local Description Table (GDT entry)
 * More info: https://en.wikipedia.org/wiki/Global_Descriptor_Table#Local_Descriptor_Table
 */
typedef struct _GDTENTRY
{
	//The lower 16 bits of the limit.
	WORD limit_low;

	// The lower 16 bits of the base.
	WORD base_low;

	// The next 8 bits of the base.
	BYTE base_middle;

	// Access flags, determine what ring this segment can be used in.
	BYTE access;
	BYTE granularity;

	// The last 8 bits of the base.
	BYTE base_high;
} __packed K_GDTENTRY;

/**
 * GDT Pointer
 */
typedef struct _GDTPOINTER
{
	// The upper 16 bits of all selector limits.
	WORD limit;

	// The address of the first K_GDTENTRY struct.
	DWORD base;
} __packed K_GDTPOINTER;

/**
 * K_REGISTERS holds all registers' state
 */
typedef struct _REGISTERS
{
	DWORD ds;                  // Data segment selector
	DWORD edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha.
	DWORD int_no, err_code;    // Interrupt number and error code (if applicable)
	DWORD eip, cs, eflags, useresp, ss; // Pushed by the processor automatically.
} K_REGISTERS;

/**
 * Globally unique identifier
 */
typedef struct _GUID {
	DWORD Data1;
	WORD  Data2;
	WORD  Data3;
	BYTE  Data4[8];
} GUID;

#endif /* INCLUDE_TYPES_H_ */
