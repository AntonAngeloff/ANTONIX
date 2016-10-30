/*
 * devices_pointing.h
 *
 *  Created on: 6.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_DEVICES_POINTING_H_
#define INCLUDE_DEVICES_POINTING_H_

#include <devices.h>

/*
 * Defines mouse event types. These events are generated
 * by the mouse driver and emitted towards a callback list.
 */
typedef enum {
	MOUSE_EVENT_MOVEMENT,
	MOUSE_EVENT_SCROLL,
	MOUSE_EVENT_BUTTON_DOWN,
	MOUSE_EVENT_BUTTON_UP
} K_MOUSE_EVENT_TYPE;

/*
 * Mouse button types
 */
typedef enum {
	MOUSE_BUTTON_LEFT	= 0,
	MOUSE_BUTTON_RIGHT	= 1,
	MOUSE_BUTTON_MIDDLE	= 2,
	MOUSE_BUTTON_4		= 3,
	MOUSE_BUTTON_5		= 4
} K_MOUSE_BUTTON;

/*
 * Mouse event structure
 */
typedef struct K_MOUSE_EVENT K_MOUSE_EVENT;
struct K_MOUSE_EVENT {
	K_MOUSE_EVENT_TYPE type;

	union {
		struct {
			int32_t x;
			int32_t y;
		} movement;

		struct {
			K_MOUSE_BUTTON type;
		} button;

		struct {
			int32_t amount;
		} scroll;
	};
};

/*
 * Callback routine, used to receive mouse events.
 */
typedef HRESULT __callback (*K_MOUSE_CALLBACK)(K_MOUSE_EVENT ev, void *user);

/*
 * Facilitating structure for IOCTL_POINTING_REGISTER
 */
typedef struct {
	K_MOUSE_CALLBACK	cb;
	void				*user;
} IOCTL_POINTING_REGISTER_STRUCT, *PIOCTL_POINTING_REGISTER_STRUCT;

#endif /* INCLUDE_DEVICES_POINTING_H_ */
