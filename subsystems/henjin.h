/*
 * henjin.h
 *
 *	Henjin is a compositing window manager / display server.
 *
 *	It is planned to support only server-side window decoration and
 *	client-side control drawing. The server may work in either buffered
 *	mode, or direct mode. In buffered mode the client will draw it's
 *	window's contents on a off-screen bitmap, residing in the shared memory.
 *	(NO! (delete) In direct mode, the server will require from the client to draw
 *	it's contents directly to the screen. !!!)
 *
 *	Henjin uses a pseudo-object-oriented approach and will be based on the
 *	NXGI graphical library, which will be used for server and client
 *	drawing.
 *
 *	As it is a display server, it is also responsible for input signal
 *	distribution (mouse and keyboard) across the applications.
 *
 *	Communication between the client and the server is performed by
 *	bi-drectional message exchange.
 *
 *	The name Henjin refers to a corporation from the Alien vs. Predator franchise
 *	called Henjin-Garcia Armament Co. which in its own refers to
 *	Weyland (Linux compositor) and Yutani (ToAruOS compositing server). :)
 *
 *	Road map:
 *		- Support server-side decoration
 *		- Support client-side control drawing
 *		- Implement widget toolkit
 *		- Implement message passing techniques
 *		- Implement dirty rect detection
 *		- Implement simple control (button and edit box)
 *		- Implement start menu with time bar
 *		- Desktop wallpaper
 *		- Desktop icons
 *
 *	What is not intended:
 *		- At this time, no multi-monitor support is intended.
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_H_
#define SUBSYSTEMS_HENJIN_H_

#include <types.h>
#include <ringbuffer.h>
#include <syncobjs.h>
#include <kstream.h>
#include <devices_pointing.h>
#include <../subsystems/nxgi.h>
#include <../subsystems/nxgi_geometry.h>

/*
 * Create aliases for some types
 */
typedef NXGI_RECT 	HJ_RECT;
typedef NXGI_POINT 	HJ_POINT;
typedef NXGI_SIZE 	HJ_SIZE;
typedef NXGI_COLOR	HJ_COLOR;

/*
 * Message types
 */
typedef enum {
	HJ_MESSAGE_MOUSE_MOVED,
	HJ_MESSAGE_MOUSE_CLICK,
	HJ_MESSAGE_MOUSE_DBL_CLICK,
	HJ_MESSAGE_MOUSE_DOWN,
	HJ_MESSAGE_MOUSE_UP,
	HJ_MESSAGE_KEY_DOWN,
	HJ_MESSAGE_KEY_UP,
	HJ_MESSAGE_RESIZE,
	HJ_MESSAGE_REPAINT,
	HJ_MESSAGE_DISABLE_DRAWING, //used when changing resolutions
	HJ_MESSAGE_ENABLE_DRAWING, //when drawing is reenabled, the object has to reallocate graphic context
	HJ_MESSAGE_QUIT,
	HJ_MESSAGE_MAX	= 0xFF
} HJ_MESSAGE_TYPE;

typedef enum {
	HJ_MOUSE_BUTTON_LEFT 	= MOUSE_BUTTON_LEFT,
	HJ_MOUSE_BUTTON_RIGHT	= MOUSE_BUTTON_RIGHT,
	HJ_MOUSE_BUTTON_MIDDLE	= MOUSE_BUTTON_MIDDLE,
	HJ_MOUSE_BUTTON_4		= MOUSE_BUTTON_4,
	HJ_MOUSE_BUTTON_5		= MOUSE_BUTTON_5

} HJ_MOUSE_BUTTON;

/*
 * Message data structure definition
 */
typedef struct HJ_MESSAGE HJ_MESSAGE;
struct HJ_MESSAGE {
	HJ_MESSAGE_TYPE type;

	/* Rest of the message depends on the
	 * type.
	 */
	union {
		struct {
			uint16_t x;
			uint16_t y;
			//BOOL	 buttons[5];
			HJ_MOUSE_BUTTON button;
		} mouse;

		struct {
			uint32_t shift_state;
			uint16_t vkey;
		} key;

		struct {
			uint16_t x;
			uint16_t y;
			NXGI_FORMAT surface_fmt;
		} resize;

		struct {
			HJ_RECT rect;
			NXGI_GRAPHICS_CONTEXT *gc;
		} repaint;

		/* Used with HJ_MESSAGE_DISABLE_DRAWING and HJ_MESSAGE_ENABLE_DRAWING */
		struct {
			K_EVENT *event;
		} lock;
	};
} __packed;

//...
typedef struct HJ_SERVER_CONTEXT HJ_SERVER_CONTEXT;
struct HJ_SERVER_CONTEXT {
	/* We receive mouse events through callback and store them
	 * here.
	 */
	RING_BUFFER *mouse_driver_events;

	/* Handle to mouse driver
	 */
	K_STREAM	*mouse_drv;

	/* Mouse aboslute position */
	HJ_POINT	mouse_pos;
	HJ_RECT		screen_rect;

	/* Desktop (also plays role of compositor).
	 */
	VOID		*desktop;

	/* Server is running */
	BOOL		running;

};

/*
 * Launches the display server.
 */
HRESULT henjin_start_server();
HRESULT henjin_shutdown_server();
HRESULT henjin_register_window(VOID *wnd, HJ_POINT position);
HRESULT henjin_unregister_window(VOID *wnd);

#endif /* SUBSYSTEMS_HENJIN_H_ */
