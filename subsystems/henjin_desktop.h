/*
 * henjin_desktop.h
 *
 *	Desktop inheritance line:
 *		- HJ_OBJECT > HJ_CONTROL > HJ_DESKTOP
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_DESKTOP_H_
#define SUBSYSTEMS_HENJIN_DESKTOP_H_

#include "henjin_object.h"
#include "henjin_control.h"

typedef struct HJ_DESKTOP HJ_DESKTOP;
struct HJ_DESKTOP {
	/* Parent class */
	HJ_CONTROL	control;

	/* Desktop fields */
	NXGI_BITMAP		*wallpaper;
	HJ_COLOR		background_color;
	HJ_RECT			bounds_rect;

	/* TODO: Use image control for mouse pointer */
	//NXGI_BITMAP *mouse_pointer;
	HJ_POINT		mouse_pos;

	/* We use this to draw. It points to
	 * this.control.gc
	 */
	NXGI_GRAPHICS_CONTEXT *gc;

	/* If set, we can draw */
	BOOL			draw_enabled;

	/* Termination event */
	K_EVENT			term_event;

	/* Pointer to task panel */
	HJ_CONTROL		*task_panel;
};

HJ_DESKTOP*	hj_create_desktop();
VOID		hj_destroy_desktop(HJ_DESKTOP **d);
VOID		hj_desktop_thread_proc();

#endif /* SUBSYSTEMS_HENJIN_DESKTOP_H_ */
