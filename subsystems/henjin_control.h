/*
 * henjin_control.h
 *
 *	Definitions of facilitating the "control" concept. The control
 *	is the common logic that is shared between windows and the
 *	desktop.
 *
 *	Each control has basic/common properties, as for now, it is only one
 *	property which is the size (i.e. the area which the control takes
 *	on the screen).
 *
 *	Common properties are locked by a mutex called `property_lock`.
 *
 *	The mutex `children_lock` is used to serialize the access to the
 *	child control list, while the `message_lock` is used to serialize
 *	such to the message queue.
 *
 *	Allowed locking order:
 *		property_lock > children_lock === allowed
 *		children_lock > property_lock === disallowed
 *
 *	Inheritance line:
 *		- HJ_OBJECT > HJ_CONTROL
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_HENJIN_CONTROL_H_
#define SUBSYSTEMS_HENJIN_CONTROL_H_

#include <types.h>
#include <linkedlist.h>
#include <ringbuffer.h>
#include "henjin.h"
#include "henjin_object.h"

#define MESSAGE_QUEUE_SIZE	512 * sizeof(HJ_MESSAGE)
#define GUI_TITLEBOX_HEIGHT	26

/*
 * Message handler. Different control successors have
 * different implementations of this routine.
 */
typedef HRESULT (*HJ_MESSAGE_HANDLER)(HJ_OBJECT *ctrl, HJ_MESSAGE *msg);
typedef HRESULT (*HJ_MESSAGE_DISPATCHER)(HJ_OBJECT *ctrl, HJ_MESSAGE *msg, BOOL *dispatched);

typedef enum {
	HJ_COLOR_LIGHTER,
	HJ_COLOR_LIGHT,
	HJ_COLOR_MAIN,
	HJ_COLOR_DARK,
	HJ_COLOR_DARKER,
	HJ_COLOR_MAX
} HJ_COLOR_TONE;

typedef struct HJ_COLOR_PRESET HJ_COLOR_PRESET;
struct HJ_COLOR_PRESET {
	HJ_COLOR window_color[HJ_COLOR_MAX];
	HJ_COLOR text_color[HJ_COLOR_MAX];
};

typedef enum {
	HJ_FEEDBACK_SURFACE_CHANGED
} HJ_FEEDBACK_TYPE;

typedef struct HJ_FEEDBACK_MESSAGE HJ_FEEDBACK_MESSAGE;
struct HJ_FEEDBACK_MESSAGE {
	HJ_FEEDBACK_TYPE type;
	HJ_RECT rect;
};

/**
 * Control type refers to the way in which message processing will
 * be performed.
 *
 * In deferred mode the control will receive messages and process them
 * in different thread (it's own thread), while in immediate mode, the control
 * don't have it's own thread and message processing takes place in the
 * sender's thread just when the message is received.
 */
typedef enum {
	HJ_CONTROL_DEFERRED,
	HJ_CONTROL_IMMEDIATE
} HJ_CONTROL_TYPE;

typedef struct HJ_CONTROL HJ_CONTROL;
struct HJ_CONTROL {
	/* Parent class */
	HJ_OBJECT		base_object;

	/* Child controls */
	DOUBLE_LIST		*children;
	K_MUTEX			children_lock;

	/* Deferred or immediate */
	HJ_CONTROL_TYPE type;

	/* Messages get stored here */
	RING_BUFFER 	*message_queue;
	K_MUTEX			message_lock;

	/* Message processing routine */
	HJ_MESSAGE_HANDLER message_handler;
	HJ_MESSAGE_DISPATCHER message_dispatcher;

	K_MUTEX			property_lock;

	/* Common control properties */
	HJ_SIZE			size;

	/* Control's surface. This is where it draws
	 * itself.
	 */
	NXGI_BITMAP 	*surface;
	NXGI_GRAPHICS_CONTEXT *gc;
	K_MUTEX			surface_lock;

	/* Color preset
	 */
	HJ_COLOR_PRESET	theme;

	HJ_CONTROL		*parent;

	/* This routine is used to communicate with parent
	 * control
	 */
	HRESULT (*parent_feedback)(HJ_CONTROL *this, HJ_CONTROL *sender, HJ_FEEDBACK_MESSAGE *msg);
	HRESULT (*on_add_child)(HJ_CONTROL *this, HJ_CONTROL *child);
	HRESULT (*on_remove_child)(HJ_CONTROL *this, HJ_CONTROL *child);
};

typedef enum {
	HJ_DECORATION_NORMAL,
	HJ_DECORATION_NONE
} HJ_DECORATION_TYPE;

/**
 * Meta-data about the control, which is available only to the
 * parent and not the control itself.
 */
typedef struct HJ_CONTROL_META HJ_CONTROL_META;
struct HJ_CONTROL_META {
	/* Position of the left-top-most pixel of the
	 * decorated control.
	 */
	HJ_POINT 	pos;

	struct {
		/* Offsetting `pos` with this point will give
		 * the top-left-most pixel of the screen where
		 * the control surface has to be blitted.
		 */
		HJ_POINT			offset;

		/* Size of the control after decoration is applied
		 */
		HJ_SIZE				total_size;

		/* Type of the decoration */
		HJ_DECORATION_TYPE	type;

		/* Border width in pixels. */
		uint32_t 			border_width;
	} decor;

	BOOL	 	drag_mode; //e.g. dragging, minimizing, maximizing
	HJ_POINT	drag_source;
	HJ_POINT	drag_wnd_pos;

	HJ_CONTROL	*control;
};

HRESULT __nxapi hj_control_init(HJ_CONTROL *ctrl, HJ_CONTROL_TYPE type, HJ_MESSAGE_HANDLER msg_handler, HJ_MESSAGE_DISPATCHER dispatcher);
HRESULT __nxapi hj_control_fini(HJ_CONTROL *ctrl);

/**
 * Enqueue message to the end of the control's message queue
 */
HRESULT __nxapi hj_queue_message(HJ_CONTROL *control, HJ_MESSAGE *msg);

/**
 * Extracts a message from the top of the queue
 */
HRESULT __nxapi hj_get_message(HJ_CONTROL *control, HJ_MESSAGE *msg);

/**
 * Invokes message handler to process the message
 */
HRESULT __nxapi hj_process_message(HJ_CONTROL *control, HJ_MESSAGE *msg);

/**
 * Adds child control to control.
 */
HRESULT __nxapi hj_add_child(HJ_CONTROL *target, HJ_CONTROL *child);
HRESULT __nxapi hj_remove_child(HJ_CONTROL *target, uint32_t index, HJ_CONTROL *child_out);
HRESULT __nxapi hj_get_child_at(HJ_CONTROL *target, uint32_t index, HJ_CONTROL_META *child);
uint32_t __nxapi hj_get_children_count(HJ_CONTROL *target);

/**
 * Returns the bounds rect of a child control, i.e. a a rect describing
 * the exact position of the control in screen space coordinates, including
 * the decoration graphics.
 *
 * For non-decorated controls, boundsrect and clientrect are equal.
 */
HJ_RECT __nxapi hj_get_child_boundsrect(HJ_CONTROL *target, uint32_t index);

/**
 * Same as boundsrect, except the client rect describes the coordinates of the
 * actual control surface (i.e. decoration is excluded).
 */
HJ_RECT __nxapi hj_get_child_clientrect(HJ_CONTROL *target, uint32_t index);
HJ_SIZE __nxapi hj_control_get_size(HJ_CONTROL *target);
int32_t __nxapi hj_child_find(HJ_CONTROL *target, HJ_CONTROL *child);

/**
 * Used when a region of a control is no longer visually valid.
 * The routine sends REPAINT message to the given control which causes
 * it to redraw the invalid area.
 */
HRESULT __nxapi hj_control_invalidate_rect(HJ_CONTROL *control, HJ_RECT rect);
HRESULT __nxapi hj_control_invalidate_rect2(HJ_CONTROL *control, HJ_RECT rect, NXGI_GRAPHICS_CONTEXT *gc);
HRESULT __nxapi hj_control_resize(HJ_CONTROL *control, HJ_SIZE new_size);
NXGI_GRAPHICS_CONTEXT __nxapi *hj_control_get_gc(HJ_CONTROL *control);
HRESULT __nxapi hj_control_get_theme(HJ_CONTROL *control, HJ_COLOR_PRESET *theme);
HRESULT __nxapi hj_control_set_child_position(HJ_CONTROL *parent, uint32_t index, HJ_POINT pos);
HRESULT __nxapi hj_control_set_child_decoration(HJ_CONTROL *parent, uint32_t index, HJ_DECORATION_TYPE d);

/**
 * Reallocates graphics context and surface.
 */
HRESULT __nxapi hj_control_realloc_surface(HJ_CONTROL *control, HJ_SIZE new_size, NXGI_FORMAT pixel_format);

/**
 * Frees the graphics context and surface, associated with this
 * control.
 */
HRESULT __nxapi hj_control_free_surface(HJ_CONTROL *control);

/* Finds the region of the control's surface, which is not occluded
 * by any child control. The region is described in local (client)
 * space coordinates.
 *
 * Region is described as list of axis-aligned rectangles.
 */
HRESULT __nxapi hj_control_get_unoccluded_region(HJ_CONTROL *ctrl, DOUBLE_LIST *target);

HRESULT __nxapi hj_control_request_parent_repaint(HJ_CONTROL *this, HJ_RECT *dirty_rect);

#endif /* SUBSYSTEMS_HENJIN_CONTROL_H_ */
