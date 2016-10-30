/*
 * henjin_window.c
 *
 *  Created on: 9.10.2016 ã.
 *      Author: Anton Angelov
 */
#include "henjin_window.h"
#include <mm.h>

/*
 * Prototypes
 */
static HRESULT window_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg);
static HRESULT window_message_dispatcher(HJ_OBJECT *control, HJ_MESSAGE *msg, BOOL *dispatched);
static HRESULT window_dispatch_mouse_message(HJ_WINDOW *wnd, HJ_MESSAGE *msg, BOOL *dispatched);
static HRESULT window_repaint(HJ_WINDOW *this, HJ_RECT clip_rect);
static HRESULT window_resize(HJ_WINDOW *this, HJ_SIZE new_size, NXGI_FORMAT pix_fmt);
static HRESULT window_on_add_child(HJ_CONTROL *this, HJ_CONTROL *child);
static HRESULT window_receive_feedback(HJ_CONTROL *this, HJ_CONTROL *sender, HJ_FEEDBACK_MESSAGE *fb);

/*
 * Implementation
 */
static HRESULT window_repaint(HJ_WINDOW *this, HJ_RECT clip_rect)
{
	NXGI_GRAPHICS_CONTEXT *gc = hj_control_get_gc(&this->control);
	HRESULT hr;

	/* Set up offset and clip rect */
	nxgi_set_offset(gc, POINT(0, 0));
	nxgi_set_clip_rect(gc, clip_rect);

	/* Repaint background */
	nxgi_set_color(gc, COLOR(220, 220, 220, 255));
	nxgi_fill_rect(gc, this->bounds_rect);

	/* Repaint subscontrols */
	for (uint32_t i=0; i<hj_get_children_count(&this->control); i++) {
		HJ_CONTROL_META c;
		HJ_RECT			br;

		hr = hj_get_child_at(&this->control, i, &c);
		if (FAILED(hr)) return hr;

		br = hj_get_child_boundsrect(&this->control, i);

		if (nxgig_rect_intersect(br, clip_rect)) {
			HJ_RECT cr = nxgig_rect_offset(br, POINT(-br.x1, -br.y1));

			/* If dirty rect overlaps with control's bounds rect, paint
			 * it at position br1.p1.
			 */
			nxgi_set_offset(gc, br.p1);
			nxgi_set_clip_rect(gc, br);

			hr = hj_control_invalidate_rect2(c.control, cr, gc);
			if (FAILED(hr)) return hr;
		}
	}

	hj_control_request_parent_repaint(&this->control, &clip_rect);

	return S_OK;
}

static HRESULT window_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg)
{
	HJ_WINDOW 	*wnd = HJ_CAST(this, HJ_CLASS_WINDOW, HJ_WINDOW);
	HRESULT		hr;

	/* Preprocess message */
	switch (msg->type) {
		case HJ_MESSAGE_REPAINT:
			hr = window_repaint(wnd, msg->repaint.rect);
			if (FAILED(hr)) return hr;

			break;

		case HJ_MESSAGE_RESIZE:
			hr = window_resize(wnd, SIZE(msg->resize.x, msg->resize.y), msg->resize.surface_fmt);
			if (FAILED(hr)) return hr;

			break;

		default:
			hr = S_FALSE;
	}

	/* Pass message to successor's message handler */
	if (wnd->handler) {
		hr = wnd->handler(this, msg);
	}

	return hr;
}

static HRESULT window_message_dispatcher(HJ_OBJECT *control, HJ_MESSAGE *msg, BOOL *dispatched)
{
	HJ_WINDOW	*wnd = HJ_CAST(control, HJ_CLASS_WINDOW, HJ_WINDOW);
	HRESULT		hr;

	/* Assume the message will not be able to be dispatched */
	*dispatched = FALSE;

	switch (msg->type) {
		/* Mouse messages have to be rerouted to the topmost intersecting child control.
		 * The desktop have to always receive the mouse move message, since it's
		 * responsible for updating the mouse position and redrawing the mouse
		 * pointer.
		 */
		case HJ_MESSAGE_MOUSE_MOVED:
		case HJ_MESSAGE_MOUSE_DOWN:
		case HJ_MESSAGE_MOUSE_UP:
		case HJ_MESSAGE_MOUSE_CLICK:
		case HJ_MESSAGE_MOUSE_DBL_CLICK:
			hr = window_dispatch_mouse_message(wnd, msg, dispatched);
			return hr;

		default:
			return S_OK;
	}
}

static HRESULT window_dispatch_mouse_message(HJ_WINDOW *wnd, HJ_MESSAGE *msg, BOOL *dispatched)
{
	HJ_CONTROL_META child_meta; //child control's meta-data
	HRESULT			hr;
	uint32_t		i, k = 0;

	*dispatched = FALSE;

	for (i=1; i<hj_get_children_count(&wnd->control); i++) {
		/* Get child control meta-data */
		hr = hj_get_child_at(&wnd->control, i, &child_meta);
		if (FAILED(hr)) return hr;

		/* Get child control's bounds rectangle */
		HJ_RECT rect = hj_get_child_boundsrect(&wnd->control, i);

		/* Calculate mouse position point */
		HJ_POINT p = POINT(msg->mouse.x, msg->mouse.y);

		/* Check if mouse is inside the bounds rectangle */
		if (nxgig_point_in_rect(p, rect)) {
			k = i;
			*dispatched = TRUE;
		}
	}

	HJ_MESSAGE new_msg = *msg;

	if (*dispatched) {
		/* Get recipient control meta-data */
		hr = hj_get_child_at(&wnd->control, k, &child_meta);
		if (FAILED(hr)) return hr;

		/* Transform mouse position to recipient control
		 * space.
		 */
		new_msg.mouse.x -= child_meta.pos.x;
		new_msg.mouse.y -= child_meta.pos.y;

		/* Reroute message to control `k` */
		hr = hj_queue_message(child_meta.control, &new_msg);
		if (FAILED(hr)) return hr;
	}

	return S_OK;
}

static HRESULT window_resize(HJ_WINDOW *this, HJ_SIZE new_size, NXGI_FORMAT pix_fmt)
{
	HRESULT hr;

	hr = hj_control_realloc_surface(&this->control, new_size, pix_fmt);
	if (FAILED(hr)) return hr;

	this->bounds_rect = RECT(0, 0, new_size.width, new_size.height);
	return S_OK;
}

HJ_WINDOW *hj_create_window(HJ_MESSAGE_HANDLER msg_handler)
{
	HJ_WINDOW	*wnd;
	HRESULT		hr;

	if (!(wnd = kcalloc(sizeof(HJ_WINDOW)))) {
		/* Out of memory */
		return NULL;
	}

	/* Initialize base object and control components */
	HJ_OBJECT *bo = &wnd->control.base_object;
	bo->class = HJ_CLASS_WINDOW;
	bo->size  = sizeof(HJ_WINDOW);
	bo->inst_table[HJ_CLASS_OBJECT]	 = &wnd->control.base_object;
	bo->inst_table[HJ_CLASS_CONTROL] = &wnd->control;
	bo->inst_table[HJ_CLASS_WINDOW]  = wnd;

	/* Setup control subcomponent fields.
	 */
	hr = hj_control_init(&wnd->control, HJ_CONTROL_DEFERRED, window_message_handler, window_message_dispatcher);
	if (FAILED(hr)) goto fail;

	/* Populate */
	wnd->handler = msg_handler;
	wnd->control.on_add_child = window_on_add_child;

	return wnd;

fail:
	/* Clean up */
	hj_destroy_window(wnd);
	return NULL;
}

VOID hj_destroy_window(HJ_WINDOW *wnd)
{
	//TODO:
}

static HRESULT window_on_add_child(HJ_CONTROL *this, HJ_CONTROL *child)
{
	UNUSED_ARG(this);

	child->parent_feedback = window_receive_feedback;
	child->parent = this;

	return S_OK;
}

static HRESULT window_receive_feedback(HJ_CONTROL *this, HJ_CONTROL *sender, HJ_FEEDBACK_MESSAGE *fb)
{
	int32_t 	i;
	HJ_RECT		cr;

	switch (fb->type) {
		/* Child control surface has change, and we need to repaint it
		 * at the window surface.
		 */
		case HJ_FEEDBACK_SURFACE_CHANGED:
			i = hj_child_find(this, sender);
			if (i == -1) {
				return E_FAIL;
			}

			cr = hj_get_child_clientrect(this, i);
			cr = nxgig_rect_offset(fb->rect, cr.p1);
			k_printf("index=%d. cr=(%d %d %d %d)\n", i, cr.x1, cr.y1, cr.x2, cr.y2);

			return hj_control_invalidate_rect(this, cr);

		default:
			return S_FALSE;
	}
}
