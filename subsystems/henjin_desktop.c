/*
 * henjin_desktop.c
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <mm.h>
#include <scheduler.h>
#include <hal.h>
#include <timer.h>
#include <string.h>
#include <stdlib.h>
#include "henjin_desktop.h"
#include "henjin_taskpanel.h"

/*
 * Prototypes
 */
static HRESULT desktop_allocate_graphics_context(HJ_DESKTOP *this);
static HRESULT desktop_load_wallpaper1(HJ_DESKTOP *this, HJ_COLOR c1, HJ_COLOR c2, HJ_COLOR c3, HJ_COLOR c4);
static HRESULT desktop_resize(HJ_DESKTOP *this, uint32_t x, uint32_t y);
static HRESULT desktop_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg);
static HRESULT desktop_mouse_move(HJ_DESKTOP *d, HJ_POINT pos);
static HRESULT desktop_mouse_move_handle_dragging(HJ_DESKTOP *d, HJ_POINT pos);
static HRESULT desktop_mouse_down(HJ_DESKTOP *d, HJ_POINT pos, HJ_MOUSE_BUTTON button);
static HRESULT desktop_mouse_up(HJ_DESKTOP *d, HJ_POINT pos);
static HRESULT desktop_dispatch_message(HJ_OBJECT *control, HJ_MESSAGE *msg, BOOL *dispatched);
static HRESULT desktop_repaint(HJ_DESKTOP *d, HJ_RECT rect);
static HRESULT desktop_repaint_subcontrols(HJ_DESKTOP *d, HJ_RECT clip_rect);
static HRESULT desktop_blit_subcontrol(HJ_DESKTOP *d, HJ_CONTROL_META *wnd_meta, NXGI_RECT boundsrect, NXGI_RECT clientrect, HJ_RECT clip_rect);
static HRESULT desktop_add_window(HJ_DESKTOP *d, HJ_CONTROL *window);
static HRESULT desktop_receive_feedback(HJ_CONTROL *this, HJ_CONTROL *sender, HJ_FEEDBACK_MESSAGE *fb);
static HRESULT desktop_on_add_child(HJ_CONTROL *this, HJ_CONTROL *child);

/* Dispatch routines */
static HRESULT desktop_dispatch_mouse_messages(HJ_DESKTOP *d, HJ_MESSAGE *msg, BOOL *dispatched);


/*
 * Implementation
 */
HJ_DESKTOP*	hj_create_desktop()
{
	HJ_DESKTOP	*d;
	HRESULT 	hr;

	/* Allocate structure */
	if (!(d = kcalloc(sizeof(HJ_DESKTOP)))) {
		return NULL;
	}

	/* Set base object fields and populate instance table */
	HJ_OBJECT *bo = &d->control.base_object;
	bo->class = HJ_CLASS_DESKTOP;
	bo->size  = sizeof(HJ_DESKTOP);
	bo->inst_table[HJ_CLASS_OBJECT]	 = &d->control.base_object;
	bo->inst_table[HJ_CLASS_CONTROL] = &d->control;
	bo->inst_table[HJ_CLASS_DESKTOP] = d;

	/* Setup control class-related fields */
	hr = hj_control_init(&d->control, HJ_CONTROL_DEFERRED, desktop_message_handler, desktop_dispatch_message);
	if (FAILED(hr)) goto fail;

	/* This is used to synchronize desktop thread with server thread */
	event_create(&d->term_event, 0);

	/* Set default values */
	d->background_color = COLOR(0, 64, 128, 255);
	d->bounds_rect = RECT(0, 0, 0, 0);
	d->control.on_add_child = desktop_on_add_child;

	/* Create taskbar */
	d->task_panel = HJ_CAST(hj_create_taskpanel(), HJ_CLASS_CONTROL, HJ_CONTROL);
	hj_add_child(&d->control, d->task_panel);

	/* Success */
	return d;

fail:
	hj_destroy_desktop(&d);
	return NULL;
}

VOID hj_destroy_desktop(HJ_DESKTOP **d)
{
	HJ_DESKTOP *desktop = *d;

	/* Finalize control */
	hj_control_fini(&desktop->control);

	if (desktop->wallpaper) {
		nxgi_destroy_bitmap(&desktop->wallpaper);
	}

	/* Destroy NXGI graphics context */
	if (desktop->gc) {
		nxgi_destroy_graphics_context(desktop->gc);
	}

	event_destroy(&desktop->term_event);
	kfree(desktop);

	*d = NULL;
}

static HRESULT desktop_allocate_graphics_context(HJ_DESKTOP *this)
{
	HRESULT hr;

	/* We will draw directly to the frame-buffer */
	hr = nxgi_get_screen(&this->control.surface);
	if (FAILED(hr)) return hr;

	hr = nxgi_create_graphics_context(&this->control.gc);
	if (FAILED(hr)) return hr;

	hr = nxgi_set_target(this->control.gc, this->control.surface);
	if (FAILED(hr)) return hr;

	/* Keep it in the Desktop component for easier access */
	this->gc = this->control.gc;

	return S_OK;
}

static HRESULT desktop_resize(HJ_DESKTOP *this, uint32_t x, uint32_t y)
{
	HRESULT hr;

	/* We are not really allowed to have different size than the
	 * screen's framebuffer.
	 */
	this->bounds_rect = RECT(0, 0, x, y);
	this->control.size = (HJ_SIZE){x, y};

	/* Resize task bar according to resolution
	 */
	hr = hj_control_resize(this->task_panel, SIZE(x, 30));
	if (FAILED(hr)) return hr;

	/* Place it at the bottom
	 */
	hr = hj_control_set_child_position(&this->control, 0, POINT(0, y - 30));
	if (FAILED(hr)) return hr;

//	hr = desktop_load_wallpaper1(
//			this,
//			COLOR(0, 64, 128, 255),
//			COLOR(0+32, 64+32, 128+32, 255),
//			COLOR(0, 32, 64, 255),
//			COLOR(0, 128, 192, 255)
//	);
//	if (FAILED(hr)) return hr;

	return S_OK;
}

VOID hj_desktop_thread_proc()
{
	HJ_DESKTOP	*desktop;
	HJ_MESSAGE	msg;
	HRESULT 	hr;

	extern
	HJ_SERVER_CONTEXT __hj_server;

	/* Get desktop object. It will very good if we pass this pointer
	 * it in another way.
	 */
	desktop = __hj_server.desktop;

	/* Loops and processes incoming messages */
	while (TRUE) {
		hr = hj_get_message(&desktop->control, &msg);

		if (hr == E_BUFFERUNDERFLOW) {
			/* Queue is empty */
			sched_yield();
			continue;
		}else if (FAILED(hr)) {
			HalKernelPanic("hj_desktop_thread_proc(): failed to retrieve message.");
		}

		hr = hj_process_message(&desktop->control, &msg);
		if (FAILED(hr)) {
			k_printf("msg type=%x\n", msg.type);
			HalKernelPanic("hj_desktop_thread_proc(): failed to process message.");
		}
	}
}

static HRESULT desktop_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg)
{
	HJ_DESKTOP *d = HJ_SUPPORTS(this, HJ_CLASS_DESKTOP) ? HJ_CAST(this, HJ_CLASS_DESKTOP, HJ_DESKTOP) : NULL;

	if (!d) {
		/* Instance has to be desktop */
		return E_FAIL;
	}

	switch (msg->type) {
		/* Sent when display server changes resolution */
		case HJ_MESSAGE_DISABLE_DRAWING:
			if (d->draw_enabled) {
				nxgi_destroy_graphics_context(d->gc);
				nxgi_destroy_bitmap(&d->control.surface);

				d->draw_enabled	= FALSE;
			}

			if (msg->lock.event) {
				event_signal(msg->lock.event);
			}

			return S_OK;

		/* Sent when new resolution is set. This event is followed
		 * by resize message
		 */
		case HJ_MESSAGE_ENABLE_DRAWING:
			if (!d->draw_enabled) {
				/* Allocate graphic things */
				if (FAILED(desktop_allocate_graphics_context(d))) {
					return E_FAIL;
				}

				if (msg->lock.event) {
					k_printf("before signal event=%x; sizeof(msg) = %d\n", msg->lock.event, sizeof(HJ_MESSAGE));
					k_printf("&sl=%x; autoreset=%x, sl_state=%x; owner=%x\n", &msg->lock.event->lock, (uint32_t)msg->lock.event->autoreset, msg->lock.event->lock.lock, (uint32_t)msg->lock.event->owner_pid);
					event_signal(msg->lock.event);
					k_printf("after signal..");
				}

				d->draw_enabled = TRUE;
			}
			return S_OK;

		/* Only sent after resolution change */
		case HJ_MESSAGE_RESIZE:
			if (FAILED(desktop_resize(d, msg->resize.x, msg->resize.y))) {
				return E_FAIL;
			}

			return S_OK;

		/* Sent by display server on mouse move */
		case HJ_MESSAGE_MOUSE_MOVED:
			return desktop_mouse_move(d, POINT(msg->mouse.x, msg->mouse.y));

		case HJ_MESSAGE_MOUSE_DOWN:
			return desktop_mouse_down(d, POINT(msg->mouse.x, msg->mouse.y), msg->mouse.button);

		case HJ_MESSAGE_MOUSE_UP:
			return desktop_mouse_up(d, POINT(msg->mouse.x, msg->mouse.y));

		/* Paint request. Usually the desktop send itself this
		 * message when a screen region has to be updated.
		 */
		case HJ_MESSAGE_REPAINT:
			if (!d->draw_enabled) {
				return S_FALSE;
			}
			return desktop_repaint(d, msg->repaint.rect);

		default:
			/* We see it but we don't know what to do. */
			return S_FALSE;
	}
}

static HRESULT desktop_repaint(HJ_DESKTOP *d, HJ_RECT rect)
{
	HRESULT		hr;
	uint64_t 	draw_time = timer_gettickcount();

	nxgi_set_offset(d->gc, POINT(0, 0));
	nxgi_set_clip_rect(d->gc, rect);

	/* Repaint background */
//	if (d->wallpaper) {
//		nxgi_bitblt(d->gc, POINT(0, 0), d->wallpaper, RECT(0, 0, d->wallpaper->width, d->wallpaper->height));
//	} else {
		nxgi_set_color(d->gc, d->background_color);
		nxgi_fill_rect(d->gc, RECT(0, 0, d->bounds_rect.x2, d->bounds_rect.y2 - 30));
//	}

	/* Print build info */
	nxgi_set_font(d->gc, FONT_PARAMS("System", 1, 0, COLOR(255,255,255,255)));
	nxgi_draw_text(d->gc, POINT(25, 25), "ANTONIX kernel v0.1 BUILD-07102016");
	nxgi_draw_text(d->gc, POINT(25, 40), "Henjin Window Compositor");

	/* Repaint windows (taskbar is also a window) */
	hr = desktop_repaint_subcontrols(d, rect);
	if (FAILED(hr)) return hr;

	/* Print draw time */
	char draw_time_str[256];
	sprintf(draw_time_str, "Draw time: %d ms", (uint32_t)(timer_gettickcount()-draw_time));
	nxgi_set_clip_rect(d->gc, d->bounds_rect);
	nxgi_draw_text(d->gc, POINT(25, 70), draw_time_str);
	nxgi_set_clip_rect(d->gc, rect);

	/* Draw mouse pointer */
	NXGI_RECT mouse_rect = RECT(d->mouse_pos.x + 4, d->mouse_pos.y + 4, d->mouse_pos.x + 12, d->mouse_pos.y + 12);
	nxgi_set_color(d->gc, COLOR(255, 0, 0, 255));
	nxgi_fill_rect(d->gc, mouse_rect);

//	/* Test intersection */
//	nxgi_set_clip_rect(d->gc, RECT(0, 0, 640, 480));
//	NXGI_POINT l1 = POINT(d->mouse_pos.x - 25, d->mouse_pos.y);
//	NXGI_POINT l2 = POINT(d->mouse_pos.x + 25, d->mouse_pos.y);
//
//	NXGI_RECT r1 = RECT(200, 200, 300, 300);
//
//	nxgi_set_color(d->gc, COLOR(0, 64, 128, 255));
//		nxgi_fill_rect(d->gc, r1);
//
//	nxgi_set_color(d->gc, COLOR(255, 255, 0, 255));
//
//	if (nxgig_line_rect_intersect(l1, l2, r1)) {
//		nxgi_draw_rect(d->gc, r1);
//	}else {
//		nxgi_fill_rect(d->gc, r1);
//	}
//
//	nxgi_set_color(d->gc, COLOR(255, 0, 0, 255));
//	nxgi_draw_line(d->gc, l1.x, l1.y, l2.x, l2.y);


	/* Show clip rect */
//	nxgi_set_color(d->gc, COLOR(0,0,0,255));
//	nxgi_draw_rect(d->gc, rect);

	return S_OK;
}

static HRESULT desktop_mouse_move(HJ_DESKTOP *d, HJ_POINT pos)
{
	HJ_RECT old_area = RECT(d->mouse_pos.x, d->mouse_pos.y, d->mouse_pos.x + 16, d->mouse_pos.y + 16);
	HJ_RECT new_area = RECT(pos.x, pos.y, pos.x + 16, pos.y + 16);

	d->mouse_pos = pos;

	/* Unify both dirty rects */
	NXGI_RECT unified_dirty_rect = nxgig_rects_union(old_area, new_area);

	if (nxgig_rect_area(unified_dirty_rect) < nxgig_rect_area(old_area) + nxgig_rect_area(new_area)) {
		/* It's more feasible to draw the unified area as a whole, since this
		 * will modify less square pixels.
		 */
		hj_control_invalidate_rect(&d->control, unified_dirty_rect);
	} else {
		/* Repaint both areas separately */
		hj_control_invalidate_rect(&d->control, old_area);
		hj_control_invalidate_rect(&d->control, new_area);
	}

	/* Move windows, if dragged */
	desktop_mouse_move_handle_dragging(d, pos);

	return S_OK;
}

/*
 * Dispatches (redistributes) a given message either to the children controls or the
 * desktop itself, depending on the type of the message.
 */
static HRESULT desktop_dispatch_message(HJ_OBJECT *control, HJ_MESSAGE *msg, BOOL *dispatched)
{
	HJ_DESKTOP	*d = HJ_CAST(control, HJ_CLASS_DESKTOP, HJ_DESKTOP);
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
			hr = desktop_dispatch_mouse_messages(d, msg, dispatched);

			/* Desktop has to receive these messages too. */
			*dispatched = msg->type == HJ_MESSAGE_MOUSE_MOVED ? FALSE : *dispatched;
			*dispatched = msg->type == HJ_MESSAGE_MOUSE_DOWN ? FALSE : *dispatched;
			*dispatched = msg->type == HJ_MESSAGE_MOUSE_UP ? FALSE : *dispatched;
			return hr;

//		case HJ_MESSAGE_REPAINT:
//			return E_NOTIMPL;

		default:
			break;
	}

	return S_OK;
}

static HRESULT desktop_dispatch_mouse_messages(HJ_DESKTOP *d, HJ_MESSAGE *msg, BOOL *dispatched)
{
	HJ_CONTROL_META child_meta; //child control's meta-data
	HRESULT			hr;
	uint32_t		i, k = 0;

	*dispatched = FALSE;

	for (i=1; i<hj_get_children_count(&d->control); i++) {
		/* Get child control meta-data */
		hr = hj_get_child_at(&d->control, i, &child_meta);
		if (FAILED(hr)) return hr;

		/* Get child control's bounds rectangle */
		HJ_RECT rect = hj_get_child_boundsrect(&d->control, i);

		/* Make mouse position point */
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
		hr = hj_get_child_at(&d->control, k, &child_meta);
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

static HRESULT desktop_repaint_subcontrols(HJ_DESKTOP *d, HJ_RECT clip_rect)
{
	HJ_CONTROL_META meta;
	HRESULT		hr;
	uint32_t 	i;

	/* Iterate each child controls and repaint (blit) to screen
	 * those who intersect the clipping rect.
	 */
	for (i=0; i<hj_get_children_count(&d->control); i++) {
		hr = hj_get_child_at(&d->control, i, &meta);
		if (FAILED(hr)) return hr;

		HJ_RECT boundsrect = hj_get_child_boundsrect(&d->control, i);
		HJ_RECT clientrect = hj_get_child_clientrect(&d->control, i);

		/* TODO: Draw only unoccluded topmost windows */
		if (nxgig_rect_intersect(boundsrect, clip_rect)) {
			hr = desktop_blit_subcontrol(d, &meta, boundsrect, clientrect, clip_rect);
			if (FAILED(hr)) return hr;
		}
	}

	return S_OK;
}

/*
 * Blits (from Bit-Block-Transfer) window's back-buffer or part of it
 * to screen. The caller is supposed to have set up the clipping rect
 * properly before calling this routine.
 */
static HRESULT desktop_blit_subcontrol(HJ_DESKTOP *d, HJ_CONTROL_META *wnd_meta, NXGI_RECT boundsrect, NXGI_RECT clientrect, HJ_RECT clip_rect)
{
	NXGI_GRAPHICS_CONTEXT *gc = hj_control_get_gc(&d->control);
	NXGI_SIZE 	wnd_size = hj_control_get_size(wnd_meta->control);
	HRESULT 	hr = S_OK;

	if (nxgig_rect_intersect(clientrect, clientrect)) {
		/* TODO: Use trylock instead of lock */
		mutex_lock(&wnd_meta->control->surface_lock);

		if (wnd_meta->control->surface) {
			hr = nxgi_bitblt(gc, clientrect.p1, wnd_meta->control->surface, RECT(0, 0, wnd_size.width, wnd_size.height));
		}else {
			k_printf("surface not aviailable.\n");
		}

		mutex_unlock(&wnd_meta->control->surface_lock);
		if (FAILED(hr)) return hr;
	}

	/* Draw decoration */
	if (wnd_meta->decor.type == HJ_DECORATION_NORMAL) {
		NXGI_RECT titlebox_rect = boundsrect;
		titlebox_rect.y2 = titlebox_rect.y1 + GUI_TITLEBOX_HEIGHT;

		/* Check if the dirty rect intersects with the decoration area */
		if (nxgig_rect_intersect(titlebox_rect, clip_rect)) {
			/* Draw titlebox background */
			nxgi_set_color(gc, d->control.theme.window_color[HJ_COLOR_DARKER]);
			nxgi_fill_rect(gc, titlebox_rect);

			nxgi_set_color(gc, d->control.theme.window_color[HJ_COLOR_DARK]);
			//nxgi_draw_rect(gc, nxgig_rect_inflate(titlebox_rect, -1));
			nxgi_draw_line(gc, titlebox_rect.x1 + 1, titlebox_rect.y2 - 1, titlebox_rect.x1 + 1, titlebox_rect.y1 + 1);
			nxgi_draw_line(gc, titlebox_rect.x1 + 1, titlebox_rect.y1 + 1, titlebox_rect.x2 - 1, titlebox_rect.y1 + 1);

			/* Draw icon */
			NXGI_RECT icon_rect = RECT(clientrect.x1 + 4, boundsrect.y1 + 6, clientrect.x1 + 4 + 16, boundsrect.y1 + 6 + 16);
			nxgi_set_color(gc, COLOR(255, 255, 255, 255));
			nxgi_fill_rect(gc, icon_rect);

			/* Draw title */
			NXGI_RECT title_rect = RECT(icon_rect.x2 + 5, icon_rect.y1, icon_rect.x2 + 140, icon_rect.y2);
			nxgi_draw_aligned_text(gc, title_rect, NXGI_HALIGN_CENTER, NXGI_VALIGN_MIDDLE, "Demo application");
		}

		/* Repaint borders */
		NXGI_RECT border_rect = nxgig_rect_inflate(clientrect, wnd_meta->decor.border_width);

		nxgi_set_color(gc, d->control.theme.window_color[HJ_COLOR_DARKER]);
		nxgi_draw_rect(gc, border_rect);

		if (wnd_meta->decor.border_width > 1) {
			nxgi_set_color(gc, d->control.theme.window_color[HJ_COLOR_DARK]);
			nxgi_draw_rect(gc, nxgig_rect_inflate(border_rect, -1));
		}
	}

//	/* Show rects */
//	nxgi_set_offset(gc, POINT(0,0)); //todo: temp
////	nxgi_set_color(gc, COLOR(0, 255, 0, 255));
////	nxgi_draw_rect(gc, clientrect);
//	nxgi_set_color(gc, COLOR(255, 0, 0, 255));
//	boundsrect.y1 += 25;
//	nxgi_fill_rect(gc, boundsrect);
//		nxgi_set_color(gc, COLOR(0, 255, 0, 255));
//	boundsrect.y1 += 10;
//	nxgi_draw_rect(gc, boundsrect);
//
//	nxgi_set_color(gc, COLOR(255, 0, 0, 255));
//	nxgi_fill_rect(gc, RECT(300, 100, 350, 150));
//
//	nxgi_set_color(gc, COLOR(0, 255, 0, 255));
//	nxgi_draw_rect(gc, RECT(300, 110, 350, 149));


	return S_OK;
}

static HRESULT desktop_add_window(HJ_DESKTOP *d, HJ_CONTROL *window)
{
	//TODO:
}

static HRESULT desktop_mouse_down(HJ_DESKTOP *d, HJ_POINT pos, HJ_MOUSE_BUTTON button)
{
	HJ_CONTROL	*c = &d->control;
	uint32_t	cnt;
	BOOL		found = FALSE;
	uint32_t	i, k;

	if (button != HJ_MOUSE_BUTTON_LEFT) {
		return S_OK;
	}

	mutex_lock(&c->children_lock);

	cnt = dlist_get_count(c->children);

	/* Handle window dragging */
	for (i=0; i<cnt; i++) {
		HJ_RECT			br, cr, tr;

		br = hj_get_child_boundsrect(c, i);
		cr = hj_get_child_clientrect(c, i);

		if (!nxgig_point_in_rect(pos, br) || nxgig_point_in_rect(pos, cr)) {
			/* Point is outside bounds rect, or inside client rect */
			continue;
		}

		/* Find taskbar rect */
		tr = RECT(br.x1, br.y1, br.x2, br.y1 + GUI_TITLEBOX_HEIGHT);

		if (nxgig_point_in_rect(pos, tr)) {
			k = i;

			/* TODO: check if point is on visible region */
			found = TRUE;
		}
	}

	if (found) {
		HJ_CONTROL_META *m;

		m = dlist_get_at(c->children, k);

		m->drag_source = pos;
		m->drag_wnd_pos = m->pos;

		m->drag_mode = TRUE;
	}

	mutex_unlock(&c->children_lock);
	return S_OK;
}

static HRESULT desktop_mouse_move_handle_dragging(HJ_DESKTOP *d, HJ_POINT pos)
{
	HJ_CONTROL_META	*m;
	HJ_CONTROL		*c = &d->control;
	uint32_t		cnt;
	uint32_t		i, k;
	BOOL			found = FALSE;

	mutex_lock(&c->children_lock);

	cnt = dlist_get_count(c->children);

	/* Handle window dragging */
	for (i=0; i<cnt; i++) {
		m = dlist_get_at(c->children, i);

		if (m->drag_mode) {
			found = TRUE;
			k = i;

			break;
		}
	}

	if (found) {
		HJ_RECT br = hj_get_child_boundsrect(c, k);
		HJ_RECT new_br;

		/* Move window */
		int32_t delta_x = pos.x - m->drag_source.x;
		int32_t delta_y = pos.y - m->drag_source.y;

		/* Don't move if distance is less than 2 pixels */
		HJ_POINT new_pos = POINT(m->drag_wnd_pos.x + delta_x, m->drag_wnd_pos.y + delta_y);

		if (abs(new_pos.x - br.x1) < 2 && abs(new_pos.y - br.y1) < 2) {
			goto unlock;
		}

		hj_control_set_child_position(c, k, new_pos);

		new_br = hj_get_child_boundsrect(c, k);
		hj_control_invalidate_rect(c, nxgig_rects_union(br, new_br));
	}

unlock:
	mutex_unlock(&c->children_lock);
	return S_OK;
}

static HRESULT desktop_mouse_up(HJ_DESKTOP *d, HJ_POINT pos)
{
	HJ_CONTROL_META	*m;
	HJ_CONTROL		*c = &d->control;
	uint32_t		cnt;
	uint32_t		i;

	UNUSED_ARG(pos);

	mutex_lock(&c->children_lock);

	cnt = dlist_get_count(c->children);

	/* Handle window dragging */
	for (i=0; i<cnt; i++) {
		m = dlist_get_at(c->children, i);

		if (m->drag_mode) {
			m->drag_mode = FALSE;
		}
	}

	mutex_unlock(&c->children_lock);
	return S_OK;
}

static HRESULT desktop_load_wallpaper1(HJ_DESKTOP *this, HJ_COLOR c1, HJ_COLOR c2, HJ_COLOR c3, HJ_COLOR c4)
{
//	NXGI_GRAPHICS_CONTEXT	*temp_gc;
	NXGI_BITMAP				*b;
	uint8_t					*ptr;
	uint32_t				i, j;
	HRESULT 				hr;

	if (RECT_WIDTH(this->bounds_rect) == 0 || RECT_HEIGHT(this->bounds_rect) == 0) {
		return S_FALSE;
	}

	if (this->wallpaper) {
		nxgi_destroy_bitmap(&this->wallpaper);
	}

	hr = nxgi_create_bitmap(RECT_WIDTH(this->bounds_rect), RECT_HEIGHT(this->bounds_rect), nxgi_internal_format(), &this->wallpaper);
	if (FAILED(hr)) return hr;

//	hr = nxgi_create_graphics_context(&temp_gc);
//	if (FAILED(hr)) return hr;

	b = this->wallpaper;
//
//	hr = nxgi_set_target(temp_gc, this->wallpaper);
//	if (FAILED(hr)) goto finally;

	for (j=0; j<b->height; j++) {
		NXGI_COLOR cl, cr, c;

		ptr = (uint8_t*)b->pBits + b->stride * j;

		cl.r = (int32_t)c1.r + ((int32_t)c3.r - c1.r) * ((float)j / b->height);
		cl.g = (int32_t)c1.g + ((int32_t)c3.g - c1.g) * ((float)j / b->height);
		cl.b = (int32_t)c1.b + ((int32_t)c3.b - c1.b) * ((float)j / b->height);
		cl.a = (int32_t)c1.a + ((int32_t)c3.a - c1.a) * ((float)j / b->height);

		cr.r = (int32_t)c2.r + ((int32_t)c4.r - c2.r) * ((float)j / b->height);
		cr.g = (int32_t)c2.g + ((int32_t)c4.g - c2.g) * ((float)j / b->height);
		cr.b = (int32_t)c2.b + ((int32_t)c4.b - c2.b) * ((float)j / b->height);
		cr.a = (int32_t)c2.a + ((int32_t)c4.a - c2.a) * ((float)j / b->height);

		for (i=0; i<b->width; i++) {
			c.r = (int32_t)cl.r + ((int32_t)cr.r - cl.r) * ((float)i/b->width);
			c.g = (int32_t)cl.g + ((int32_t)cr.g - cl.g) * ((float)i/b->width);
			c.b = (int32_t)cl.b + ((int32_t)cr.b - cl.b) * ((float)i/b->width);
			c.a = (int32_t)cl.a + ((int32_t)cr.a - cl.a) * ((float)i/b->width);

			*(NXGI_COLOR*)ptr = c;
			ptr += b->bits_per_pixel / 4;
		}
	}

finally:
//	nxgi_destroy_graphics_context(temp_gc);
	return hr;
}

static HRESULT desktop_receive_feedback(HJ_CONTROL *this, HJ_CONTROL *sender, HJ_FEEDBACK_MESSAGE *fb)
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

			return hj_control_invalidate_rect(this, cr);

		default:
			return S_FALSE;
	}
}

static HRESULT desktop_on_add_child(HJ_CONTROL *this, HJ_CONTROL *child)
{
	UNUSED_ARG(this);

	child->parent_feedback = desktop_receive_feedback;
	child->parent = this;

	return S_OK;
}
