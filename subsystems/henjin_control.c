/*
 * henjin_control.c
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <mm.h>
#include "henjin_control.h"

/*
 * Initializes the control.
 *
 * Massage handling routine is always required, but the dispatching
 * routine `dispatcher` is optional.
 */
HRESULT __nxapi hj_control_init(HJ_CONTROL *ctrl, HJ_CONTROL_TYPE type, HJ_MESSAGE_HANDLER msg_handler, HJ_MESSAGE_DISPATCHER dispatcher)
{
	ctrl->children = dlist_create();
	ctrl->type = type;
	ctrl->message_handler = msg_handler;
	ctrl->message_dispatcher = dispatcher;

	if (ctrl->type == HJ_CONTROL_DEFERRED) {
		/* Queue is needed only in deferred mode */
		ctrl->message_queue = create_ring_buffer(MESSAGE_QUEUE_SIZE, RING_BUFFER_LOCK_NONE);
		if (!ctrl->message_queue) return E_OUTOFMEM;
	}

	/* Initialize main theme */
	ctrl->theme.window_color[HJ_COLOR_MAIN] = COLOR(0, 64, 128, 255);
	ctrl->theme.window_color[HJ_COLOR_LIGHTER] = COLOR(0, 128, 255, 255);
	ctrl->theme.window_color[HJ_COLOR_LIGHT] = COLOR(0, 96, 192, 255);
	ctrl->theme.window_color[HJ_COLOR_DARK] = COLOR(0, 32, 64, 255);
	ctrl->theme.window_color[HJ_COLOR_DARKER] = COLOR(0, 16, 32, 255);
	ctrl->theme.text_color[HJ_COLOR_MAIN] = COLOR(255, 255, 255, 255);
	ctrl->theme.text_color[HJ_COLOR_LIGHTER] = COLOR(255, 255, 255, 255);
	ctrl->theme.text_color[HJ_COLOR_LIGHT] = COLOR(255, 255, 255, 255);
	ctrl->theme.text_color[HJ_COLOR_DARK] = COLOR(192, 192, 192, 255);
	ctrl->theme.text_color[HJ_COLOR_DARKER] = COLOR(128, 128, 128, 255);

	mutex_create(&ctrl->message_lock);
	mutex_create(&ctrl->children_lock);
	mutex_create(&ctrl->property_lock);
	mutex_create(&ctrl->surface_lock);

	return S_OK;
}

HRESULT __nxapi hj_control_fini(HJ_CONTROL *ctrl)
{
	mutex_destroy(&ctrl->message_lock);
	mutex_destroy(&ctrl->children_lock);
	mutex_destroy(&ctrl->property_lock);
	mutex_destroy(&ctrl->surface_lock);

	//TODO:
	return E_NOTIMPL;
}

HRESULT __nxapi hj_queue_message(HJ_CONTROL *control, HJ_MESSAGE *msg)
{
	HRESULT hr;

	mutex_lock(&control->message_lock);

	/* For immediate mode, process message directly */
	if (control->type == HJ_CONTROL_IMMEDIATE) {
		hr = hj_process_message(control, msg);
		goto finally;
	}

	/* Deferred mode */
	if (rb_get_write_size(control->message_queue) < sizeof(HJ_MESSAGE)) {
		hr = E_BUFFEROVERFLOW;
		goto finally;
	}

	hr = rb_write(control->message_queue, msg, sizeof(HJ_MESSAGE));

finally:
	mutex_unlock(&control->message_lock);
	return hr;
}

HRESULT __nxapi hj_get_message(HJ_CONTROL *control, HJ_MESSAGE *msg)
{
	HRESULT hr;

	mutex_lock(&control->message_lock);

	/* We are not expected to get called in immediate mode */
	if (control->type == HJ_CONTROL_IMMEDIATE) {
		hr = E_FAIL;
		goto finally;
	}

	/* Is there something in the pipe?? */
	if (rb_get_read_size(control->message_queue) < sizeof(HJ_MESSAGE)) {
		hr = E_BUFFERUNDERFLOW;
		goto finally;
	}

	hr = rb_read(control->message_queue, msg, sizeof(HJ_MESSAGE));

finally:
	mutex_unlock(&control->message_lock);
	return hr;
}

HRESULT __nxapi hj_process_message(HJ_CONTROL *control, HJ_MESSAGE *msg)
{
	HRESULT hr;
	BOOL	dispatched = FALSE;

	/*
	 * Invoke message dispatcher to redistribute the message to the
	 * child controls if necessary.
	 */
	if (control->message_dispatcher != NULL) {
		hr = control->message_dispatcher(&control->base_object, msg, &dispatched);
		if (FAILED(hr)) return hr;
	}

	/*
	 * The message is not distributed out, so we can send it to the current
	 * control for processing.
	 */
	if (!dispatched) {
		/*
		 * Process
		 */
		hr = control->message_handler(&control->base_object, msg);
		if (FAILED(hr)) return hr;
	}

	return hr;
}

HRESULT __nxapi hj_control_invalidate_rect(HJ_CONTROL *control, HJ_RECT rect)
{
	HJ_MESSAGE msg = { 0 };

	msg.type = HJ_MESSAGE_REPAINT;
	msg.repaint.rect = rect;

	return hj_queue_message(control, &msg);
}

HRESULT __nxapi hj_control_invalidate_rect2(HJ_CONTROL *control, HJ_RECT rect, NXGI_GRAPHICS_CONTEXT *gc)
{
	HJ_MESSAGE msg = { 0 };

	msg.type = HJ_MESSAGE_REPAINT;
	msg.repaint.rect = rect;
	msg.repaint.gc = gc;

	return hj_queue_message(control, &msg);
}

HRESULT __nxapi hj_control_resize(HJ_CONTROL *control, HJ_SIZE new_size)
{
	HJ_MESSAGE msg = { 0 };

	msg.type = HJ_MESSAGE_RESIZE;
	msg.resize.x = new_size.width;
	msg.resize.y = new_size.height;
	msg.resize.surface_fmt = nxgi_internal_format();

	return hj_queue_message(control, &msg);
}

HRESULT __nxapi hj_add_child(HJ_CONTROL *target, HJ_CONTROL *child)
{
	HJ_CONTROL_META	*meta;
	HRESULT hr = E_FAIL;

	mutex_lock(&target->children_lock);

	/* Create meta info */
	if (!(meta = kcalloc(sizeof(HJ_CONTROL_META)))) {
		hr = E_OUTOFMEM;
		goto finally;
	}

	/* All control defaults to non-decoration */
	meta->decor.type = HJ_DECORATION_NONE;
	meta->decor.border_width = 0;
	meta->decor.offset = POINT(0, 0);

	meta->control = child;

	/* Add to child list */
	if (dlist_add(target->children, meta) != 0) {
		hr = S_OK;
	}
	if (FAILED(hr)) goto finally;

	if (target->on_add_child) {
		hr = target->on_add_child(target, child);
		if (FAILED(hr)) goto finally;
	}

finally:
	mutex_unlock(&target->children_lock);
	return hr;
}

HRESULT __nxapi hj_remove_child(HJ_CONTROL *target, uint32_t index, HJ_CONTROL *child_out)
{
	HJ_CONTROL_META	*meta;
	HRESULT			hr = S_OK;

	mutex_lock(&target->children_lock);

	if (index >= dlist_get_count(target->children)) {
		hr = E_INVALIDARG;
		goto finally;
	}

	/* Remove from list */
	if ((meta = dlist_remove_at(target->children, index)) == NULL) {
		hr = E_FAIL;
	}

	if ((child_out = meta->control) == NULL) {
		hr = E_FAIL;
	}

	if (target->on_remove_child) {
		target->on_remove_child(target, meta->control);
	}

	kfree(meta);

finally:
	mutex_unlock(&target->children_lock);
	return hr;
}

uint32_t __nxapi hj_get_children_count(HJ_CONTROL *target)
{
	uint32_t count;

	mutex_lock(&target->children_lock);
	count = dlist_get_count(target->children);
	mutex_unlock(&target->children_lock);

	return count;
}

HJ_SIZE __nxapi hj_control_get_size(HJ_CONTROL *target)
{
	HJ_SIZE s;

	mutex_lock(&target->property_lock);
	s = target->size;
	mutex_unlock(&target->property_lock);

	return s;
}

HJ_RECT __nxapi hj_get_child_boundsrect(HJ_CONTROL *target, uint32_t index)
{
	HJ_CONTROL_META	child_meta;
	HJ_SIZE			child_screen_size;
	HJ_RECT			br;
	HRESULT 		hr;

	hr = hj_get_child_at(target, index, &child_meta);
	if (FAILED(hr)) return RECT(0, 0, 0, 0);

	child_screen_size = hj_control_get_size(child_meta.control);
	child_screen_size.width += child_meta.decor.border_width * 2;
	child_screen_size.height += child_meta.decor.border_width * 2;

	br.p1 = child_meta.pos;
	br.p2 = POINT(br.x1 + child_screen_size.width, br.y1 + child_screen_size.height);

	/* Apply decoration modifications */
	if (child_meta.decor.type == HJ_DECORATION_NORMAL) {
		br.y2 += GUI_TITLEBOX_HEIGHT;
	}

	return br;
}

HJ_RECT __nxapi hj_get_child_clientrect(HJ_CONTROL *target, uint32_t index)
{
	HJ_CONTROL_META	child_meta;
	HJ_SIZE			child_size;
	HJ_RECT			br;
	HRESULT 		hr;

	hr = hj_get_child_at(target, index, &child_meta);
	if (FAILED(hr)) return RECT(0, 0, 0, 0);

	child_size = hj_control_get_size(child_meta.control);

	br.p1 = nxgig_point_add(child_meta.pos, child_meta.decor.offset);
	br.p2 = POINT(br.x1 + child_size.width, br.y1 + child_size.height);

	return br;
}

HRESULT __nxapi hj_get_child_at(HJ_CONTROL *target, uint32_t index, HJ_CONTROL_META *child)
{
	HRESULT		hr;

	mutex_lock(&target->children_lock);

	if (index >= dlist_get_count(target->children)) {
		hr = E_INVALIDARG;
		goto finally;
	}

	*child	= *(HJ_CONTROL_META*)dlist_get_at(target->children, index);
	hr		= S_OK;

finally:
	mutex_unlock(&target->children_lock);
	return hr;
}

NXGI_GRAPHICS_CONTEXT __nxapi *hj_control_get_gc(HJ_CONTROL *control)
{
	NXGI_GRAPHICS_CONTEXT *gc;

	/* Lock surface */
	mutex_lock(&control->surface_lock);
	gc = control->gc;
	mutex_unlock(&control->surface_lock);

	return gc;
}

HRESULT __nxapi hj_control_set_child_position(HJ_CONTROL *parent, uint32_t index, HJ_POINT pos)
{
	HJ_CONTROL_META *info;
	HRESULT			hr = S_OK;

	mutex_lock(&parent->children_lock);

	if ((info = dlist_get_at(parent->children, index)) == NULL) {
		hr = E_FAIL;
		goto finally;
	}

	/* TODO: property lock for control metadata */
	info->pos.x = pos.x;
	info->pos.y = pos.y;

	/* TODO: moving control should invoke repainting */

finally:
	mutex_unlock(&parent->children_lock);

	return hr;
}

HRESULT __nxapi hj_control_get_theme(HJ_CONTROL *control, HJ_COLOR_PRESET *theme)
{
	mutex_lock(&control->property_lock);
	*theme = control->theme;
	mutex_unlock(&control->property_lock);

	return S_OK;
}

HRESULT __nxapi hj_control_set_child_decoration(HJ_CONTROL *parent, uint32_t index, HJ_DECORATION_TYPE d)
{
	HJ_CONTROL_META *info;
	HRESULT			hr = S_OK;

	mutex_lock(&parent->children_lock);

	if ((info = dlist_get_at(parent->children, index)) == NULL) {
		hr = E_FAIL;
		goto finally;
	}

	info->decor.type = d;

	switch (d) {
		case HJ_DECORATION_NONE:
			info->decor.border_width = 0;
			info->decor.offset = POINT(0, 0);

			break;

		case HJ_DECORATION_NORMAL:
			info->decor.border_width = 2;
			info->decor.offset = POINT(info->decor.border_width, GUI_TITLEBOX_HEIGHT + info->decor.border_width);

			break;

		default:
			hr = E_INVALIDARG;
			goto finally;
	}

finally:
	mutex_unlock(&parent->children_lock);
	return hr;
}

HRESULT __nxapi hj_control_realloc_surface(HJ_CONTROL *control, HJ_SIZE new_size, NXGI_FORMAT pixel_format)
{
	HRESULT hr;

	mutex_lock(&control->surface_lock);

	/* If new size and format as same as current, no need to reallocate */
	if (control->surface && control->surface->width == (uint32_t)new_size.width &&
		control->surface->height == (uint32_t)new_size.height && control->surface->format == pixel_format)
	{
		hr = S_OK;
		goto unlock;
	}

	hr = hj_control_free_surface(control);
	if (FAILED(hr)) goto unlock;

	if (new_size.width == 0 || new_size.height == 0) {
		hr = S_FALSE;
		goto unlock;
	}

	/* Reallocate */
	hr = nxgi_create_bitmap(new_size.width, new_size.height, pixel_format, &control->surface);
	if (FAILED(hr)) goto unlock;

	hr = nxgi_create_graphics_context(&control->gc);
	if (FAILED(hr)) goto unlock;

	hr = nxgi_set_target(control->gc, control->surface);
	if (FAILED(hr)) goto unlock;

	/* Update control size */
	mutex_lock(&control->property_lock);
	control->size = new_size;
	mutex_unlock(&control->property_lock);

unlock:
	mutex_unlock(&control->surface_lock);
	return hr;
}

HRESULT __nxapi hj_control_free_surface(HJ_CONTROL *control)
{
	mutex_lock(&control->surface_lock);

	if (control->gc) {
		nxgi_destroy_graphics_context(control->gc);
		control->gc = NULL;
	}

	if (control->surface) {
		nxgi_destroy_bitmap(&control->surface);
		control->surface = NULL;
	}

	mutex_unlock(&control->surface_lock);
	return S_OK;
}

HRESULT __nxapi hj_control_request_parent_repaint(HJ_CONTROL *this, HJ_RECT *dirty_rect)
{
	HJ_FEEDBACK_MESSAGE msg;
	HRESULT				hr;

	if (!this->parent_feedback) {
		k_printf("no parent feedback func\n");
		return E_NOTSUPPORTED;
	}

	msg.type = HJ_FEEDBACK_SURFACE_CHANGED;

	if (dirty_rect != NULL) {
		msg.rect = *dirty_rect;
	} else {
		HJ_SIZE s = hj_control_get_size(this);
		msg.rect = RECT(0, 0, s.width, s.height);
	}

	hr = this->parent_feedback(this->parent, this, &msg);
	k_printf("parent feedback func: hr=%x\n", hr);
	return hr;
}

int32_t __nxapi hj_child_find(HJ_CONTROL *target, HJ_CONTROL *child)
{
	uint32_t	cnt;
	uint32_t	i;
	uint32_t	index = -1;

	mutex_lock(&target->children_lock);

	cnt = dlist_get_count(target->children);

	for (i=0; i<cnt; i++) {
		HJ_CONTROL_META *m;

		m = dlist_get_at(target->children, i);
		if (!m) {
			/* Unexpected */
			goto finally;
		}

		if (m->control == child) {
			/* Found */
			index = i;
			goto finally;
		}
	}

finally:
	mutex_unlock(&target->children_lock);
	return index;
}
HRESULT __nxapi hj_control_get_unoccluded_region(HJ_CONTROL *ctrl, DOUBLE_LIST *target)
{
	//todo
}
