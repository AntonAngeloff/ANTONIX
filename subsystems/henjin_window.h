/*
 * henjin_window.h
 *
 * 	Inheritance line:
 * 		HJ_OBJECT > HJ_CONTROL > HJ_WINDOW
 *
 *  Created on: 9.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_WINDOW_H_
#define SUBSYSTEMS_HENJIN_WINDOW_H_

#include "henjin_control.h"

typedef struct HJ_WINDOW HJ_WINDOW;
struct HJ_WINDOW {
	HJ_CONTROL	control;
	HJ_RECT		bounds_rect;

	/* Message handler, set by successor class */
	HJ_MESSAGE_HANDLER handler;


};

HJ_WINDOW	*hj_create_window(HJ_MESSAGE_HANDLER msg_handler);
VOID 		hj_destroy_window(HJ_WINDOW *wnd);


#endif /* SUBSYSTEMS_HENJIN_WINDOW_H_ */
