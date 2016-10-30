/*
 * henjin_button.h
 *
 *  Created on: 12.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_BUTTON_H_
#define SUBSYSTEMS_HENJIN_BUTTON_H_

#include "henjin_control.h"

typedef struct HJ_BUTTON HJ_BUTTON;
struct HJ_BUTTON {
	/* Control component */
	HJ_CONTROL 	control;

	/* Bounds rect */
	HJ_RECT		bounds_rect;
	BOOL		down;

	/* Caption */
	char		caption[256];
};

HJ_BUTTON *hj_create_button();
HRESULT hj_btn_set_caption(HJ_BUTTON *b, char *caption);
HRESULT hj_btn_get_caption(HJ_BUTTON *b, char *caption);
VOID	hj_btn_destroy(HJ_BUTTON *btn);

#endif /* SUBSYSTEMS_HENJIN_BUTTON_H_ */
