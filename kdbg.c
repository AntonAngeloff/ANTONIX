/*
 * kdbg.c
 *
 *  Created on: 4.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <kdbg.h>
#include <string.h>
#include <hal.h>

struct {
	/* Signifies that the OS is running under Bochs and E9
	 * port is enabled.
	 */
	BOOL is_bochs;
} KDBG_CONTEXT = {0};

HRESULT __nxapi kdbg_init()
{
	KDBG_CONTEXT.is_bochs = READ_PORT_UCHAR(0xE9) == 0xE9 ? TRUE : FALSE;
	return S_OK;
}

HRESULT __nxapi kdbg_fini()
{
	return S_OK;
}

void __nxapi dbg_print(char *string)
{
	char *s = string;

	if (KDBG_CONTEXT.is_bochs) {
		/* Use E9 hack */
		while (*s) {
			WRITE_PORT_UCHAR(0xE9, *s);
			s++;
		}
	} else {
		/* TODO: Implement writing through serial port. */
		return;
	}
}

void __nxapi dbg_printf(char *fmt, ...)
{
	char buffer[1024];

	va_list args;
	va_start(args, fmt);
	vsprintf(buffer, fmt, args);
	va_end(args);

	//Write formatted string with vga_print()
	dbg_print(buffer);
}

void __nxapi dbg_break()
{
	if (KDBG_CONTEXT.is_bochs) {
		WRITE_PORT_USHORT(0x8A00, 0x8A00);
		WRITE_PORT_USHORT(0x8A00, 0x8AE0);
	} else {
		//TODO
	}
}
