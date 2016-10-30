/*
 * console.h
 *
 *  Created on: 14.07.2016 ã.
 *      Author: Admin
 */

#ifndef DRIVERS_CONSOLE_H_
#define DRIVERS_CONSOLE_H_

#include "vfs.h"

#define CON_DRV_SIGNATURE	0xAABBCCDD

typedef struct {
	uint32_t magic_value;
} K_CON_CONTEXT;

HRESULT __nxapi con_initialize();
HRESULT __nxapi con_uninitialize();

#endif /* DRIVERS_CONSOLE_H_ */
