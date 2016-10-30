/*
 * henjin_taskpanel.h
 *
 *	Task Panel (also known as Taskbar) is a horizontally-spanned
 *	control on the top of the desktop.
 *
 *	It holds icons of the currently running applications as well as
 *	start menu button and clock.
 *
 *  Created on: 8.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_TASKPANEL_H_
#define SUBSYSTEMS_HENJIN_TASKPANEL_H_

#include "henjin_control.h"

#define GUI_BOXTHINGY_WIDTH	10
#define GUI_TIMEBOX_WIDTH	60

typedef struct HJ_TASKPANEL HJ_TASKPANEL;
struct HJ_TASKPANEL {
	/* Parent class */
	HJ_CONTROL	control;

	/* Bounds rect */
	HJ_RECT		bounds_rect;
};

HJ_TASKPANEL *hj_create_taskpanel();
VOID hj_destroy_taskpanel(HJ_TASKPANEL *tp);

#endif /* SUBSYSTEMS_HENJIN_TASKPANEL_H_ */
