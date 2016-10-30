/*
 * henjin_taskpanel.c
 *
 *  Created on: 8.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <mm.h>
#include "henjin_taskpanel.h"

/*
 * Prototypes
 */
static HRESULT taskpan_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg);
static HRESULT taskpan_resize(HJ_TASKPANEL *this, HJ_SIZE new_size, NXGI_FORMAT pixel_format);
static HRESULT taskpan_repaint(HJ_TASKPANEL *this, HJ_RECT clip_rect);
static HRESULT taskpane_set_text_color(HJ_TASKPANEL *this, HJ_COLOR color);

/*
 * Implementation
 */
static HRESULT taskpan_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg)
{
	HJ_TASKPANEL *p = HJ_CAST(this, HJ_CLASS_TASKPANEL, HJ_TASKPANEL);

	switch (msg->type) {
		case HJ_MESSAGE_REPAINT:
			return taskpan_repaint(p, msg->repaint.rect);

		case HJ_MESSAGE_RESIZE:
			return taskpan_resize(p, SIZE(msg->resize.x, msg->resize.y), msg->resize.surface_fmt);

		default:
			return S_FALSE;
	}
}

HJ_TASKPANEL *hj_create_taskpanel()
{
	HJ_TASKPANEL	*p;
	HRESULT			hr;

	if (!(p = kcalloc(sizeof(HJ_TASKPANEL)))) {
		/* Out of memory */
		return NULL;
	}

	/* Initialize base object and control components */
	HJ_OBJECT *bo = &p->control.base_object;
	bo->class = HJ_CLASS_DESKTOP;
	bo->size  = sizeof(HJ_TASKPANEL);
	bo->inst_table[HJ_CLASS_OBJECT]	 = &p->control.base_object;
	bo->inst_table[HJ_CLASS_CONTROL] = &p->control;
	bo->inst_table[HJ_CLASS_TASKPANEL] = p;

	/* Setup control class-related fields.
	 * The task panel is an immediate control, which means it doesn't
	 * have it's own thread.
	 */
	hr = hj_control_init(&p->control, HJ_CONTROL_IMMEDIATE, taskpan_message_handler, NULL);
	if (FAILED(hr)) goto fail;

	return p;

fail:
	/* Clean up */
	hj_destroy_taskpanel(p);
	return NULL;
}

VOID hj_destroy_taskpanel(HJ_TASKPANEL *tp)
{
	//TODO
}

static HRESULT taskpan_resize(HJ_TASKPANEL *this, HJ_SIZE new_size, NXGI_FORMAT pixel_format)
{
	HRESULT hr;

//	/* TODO: This logic may be relocated at HJ_CONTROL class and made invisible for
//	 * successor classes.
//	 */
//
//	if (new_size.width == this->control.size.width && new_size.height == this->control.size.height && pixel_format == this->control.surface->format) {
//		/* Nothing to worry about. Anything at all... */
//		return S_OK;
//	}
//	k_printf("0: hr=%x\n", 0);
//
//
//	if (this->control.gc) {
//		k_printf("before d.\n");
//		nxgi_destroy_graphics_context(this->control.gc);
//		this->control.gc = NULL;
//		k_printf("after d.\n");
//	}
//
//	/* TODO: think of mutex for the surface bitmap */
//	if (this->control.surface != NULL) {
//		nxgi_destroy_bitmap(&this->control.surface);
//		this->control.surface = NULL;
//	}
//
//	if (new_size.width == 0 || new_size.height == 0) {
//		return E_FAIL;
//	}
//
//	/* Allocate */
//	hr = nxgi_create_bitmap(new_size.width, new_size.height, pixel_format, &this->control.surface);
//	k_printf("1: hr=%x\n", hr);
//	if (FAILED(hr)) return hr;
//
//	hr = nxgi_create_graphics_context(&this->control.gc);
//	k_printf("2: hr=%x\n", hr);
//	if (FAILED(hr)) return hr;
//
//	hr = nxgi_set_target(this->control.gc, this->control.surface);
//	k_printf("3: hr=%x\n", hr);
//	if (FAILED(hr)) return hr;
//
//	/* Update size */
//	mutex_lock(&this->control.property_lock);
//	this->control.size = new_size;
//	mutex_unlock(&this->control.property_lock);

	hr = hj_control_realloc_surface(&this->control, new_size, pixel_format);
	if (FAILED(hr)) return hr;

	this->bounds_rect = RECT(0, 0, new_size.width, new_size.height);

	/* This has to issue a full redraw */
	hr = hj_control_invalidate_rect(&this->control, RECT(0, 0, new_size.width, new_size.height));
	if (FAILED(hr)) return hr;

	return S_OK;
}

static HRESULT taskpan_repaint(HJ_TASKPANEL *this, HJ_RECT clip_rect)
{
	NXGI_GRAPHICS_CONTEXT *gc = hj_control_get_gc(HJ_CAST(this, HJ_CLASS_CONTROL, HJ_CONTROL));
	NXGI_RECT		br = this->bounds_rect;
	HJ_COLOR_PRESET theme;

	/* Get color preset */
	hj_control_get_theme(&this->control, &theme);

	/* Setup clipping */
	nxgi_set_clip_rect(gc, clip_rect);

	/* Draw background */
	nxgi_set_color(gc, theme.window_color[HJ_COLOR_DARK]);
	nxgi_fill_rect(gc, this->bounds_rect);

	/* Draw upper border */
	nxgi_set_color(gc, theme.window_color[HJ_COLOR_MAIN]);
	nxgi_draw_line(gc, br.x1, br.y1 + 1, br.x2, br.y1 + 1);

	nxgi_set_color(gc, theme.window_color[HJ_COLOR_DARKER]);
	nxgi_draw_line(gc, br.x1, br.y1 + 0, br.x2, br.y1 + 0);

	/* Draw box thingy on the right end */
	NXGI_RECT	box_rect = RECT(br.x2 - GUI_BOXTHINGY_WIDTH, br.y1, br.x2, br.y2);

	nxgi_set_color(gc, theme.window_color[HJ_COLOR_MAIN]);
	nxgi_draw_line(gc, box_rect.x1 + 1, box_rect.y1 + 1, box_rect.x1 + 1, box_rect.y2 - 1);

	nxgi_set_color(gc, theme.window_color[HJ_COLOR_DARKER]);
	nxgi_draw_line(gc, box_rect.x1, box_rect.y1 + 1, box_rect.x1, box_rect.y2 - 1);

	/* Draw current time */
	NXGI_RECT	time_box = RECT(box_rect.x1 - GUI_TIMEBOX_WIDTH, br.y1, box_rect.x2 - GUI_BOXTHINGY_WIDTH, br.y2);

	taskpane_set_text_color(this, theme.text_color[HJ_COLOR_DARK]);
	nxgi_draw_aligned_text(gc, time_box, NXGI_HALIGN_CENTER, NXGI_VALIGN_MIDDLE, "12:00");

	/* Draw mini-border on the left side of the time box */
	nxgi_set_color(gc, theme.window_color[HJ_COLOR_MAIN]);
	nxgi_draw_line(gc, time_box.x1 + 1, time_box.y1 + 10, time_box.x1 + 1, time_box.y2 - 5);

	nxgi_set_color(gc, theme.window_color[HJ_COLOR_DARKER]);
	nxgi_draw_line(gc, time_box.x1, time_box.y1 + 10, time_box.x1, time_box.y2 - 5);

	/* Draw start button */
	nxgi_set_font(gc, FONT_PARAMS("System", 1, 0, theme.text_color[HJ_COLOR_MAIN]));
	nxgi_draw_text(gc, POINT(10, 10), "START");

	return S_OK;
}

static HRESULT taskpane_set_text_color(HJ_TASKPANEL *this, HJ_COLOR color)
{
	NXGI_FONT f = FONT_PARAMS("System", 1, 0, color);
	return nxgi_set_font(this->control.gc, f);
}
