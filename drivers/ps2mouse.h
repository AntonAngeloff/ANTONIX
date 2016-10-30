/*
 * ps2mouse.h
 *
 *  Created on: 5.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef DRIVERS_PS2MOUSE_H_
#define DRIVERS_PS2MOUSE_H_

#include <types.h>
#include <ps2.h>
#include <syncobjs.h>
#include <devices_pointing.h>

#define MOUSE_DATA_PORT		PORT_PS2_DATA
#define MOUSE_STATUS_PORT	PORT_PS2_STATUS
#define MOUSE_COMMAND_PORT	PORT_PS2_CMD

#define MOUSE_IRQ_ID		12

#define MOUSE_CMD_RESET		0xFF
#define MOUSE_CMD_RESEND	0xFE
#define MOUSE_CMD_READ_DATA	0xEB

/* Buttons */
#define MOUSE_MAX_BUTTONS	5

/* Return codes */
#define MOUSE_SUCCESS		PS2_SUCCESS
#define MOUSE_FAILURE		PS2_FAILURE

#define MOUSE_MAX_CB_COUNT	32

/*
 * Wait reason
 */
typedef enum {
	WAIT_READ,
	WAIT_WRITE
} K_MOUSE_WAIT_REASON;

/*
 * Driver context (attached to device's private data field).
 */
typedef struct PS2MOUSE_DRV_CONTEXT PS2MOUSE_DRV_CONTEXT;
struct PS2MOUSE_DRV_CONTEXT {
	uint32_t 	read_counter;
	uint8_t		packet[4];
	uint32_t	button_cnt;
	BOOL		button_state[5];

	/* Callback list */
	K_MOUSE_CALLBACK 	cb_list[MOUSE_MAX_CB_COUNT];
	void				*cb_user_list[MOUSE_MAX_CB_COUNT];
	uint32_t			cb_count;
	K_SPINLOCK			cb_lock;
};

/*
 * Install driver
 */
HRESULT __nxapi ps2mouse_install();
HRESULT __nxapi ps2mouse_uninstall();


#endif /* DRIVERS_PS2MOUSE_H_ */
