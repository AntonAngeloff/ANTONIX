/*
 * keyboard.h
 *
 *  Created on: 13.11.2015 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_PS2_H_
#define INCLUDE_PS2_H_

#include <types.h>
#include <hal.h>

/**
 * Define PS\2 related ports
 */
#define PORT_PS2_DATA	0x60
#define PORT_PS2_STATUS	0x64
#define PORT_PS2_CMD	0x64

/*
 * Define PS\2 device commands
 */
#define PS2DEV_RESET 			0xFF
#define PS2DEV_DISABLE_SCANNING	0xF5
#define PS2DEV_IDENTIFY			0xFA

#define PS2_SUCCESS				0xFA
#define PS2_FAILURE				0xFC


HRESULT __nxapi ps2_initialize();
BYTE __nxapi getch();
HRESULT __nxapi readch(BYTE *c);

#endif /* INCLUDE_PS2_H_ */
