/*
 * henjin_button.c
 *
 *  Created on: 12.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <string.h>
#include <mm.h>
#include "henjin_button.h"

/*
 * Prototypes
 */
static HRESULT button_repaint(HJ_BUTTON *this, NXGI_GRAPHICS_CONTEXT *gc, HJ_RECT clip_rect);
static HRESULT button_resize(HJ_BUTTON *this, uint32_t x, uint32_t y, NXGI_FORMAT fmt);
static HRESULT button_update_backbuffer(HJ_BUTTON *this);
static HRESULT button_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg);
static HRESULT button_mouse_down(HJ_BUTTON *this, HJ_POINT pos, HJ_MOUSE_BUTTON btn);
static HRESULT button_mouse_up(HJ_BUTTON *this, HJ_POINT pos, HJ_MOUSE_BUTTON btn);

/*
 * Implementation
 */
static HRESULT button_message_handler(HJ_OBJECT *this, HJ_MESSAGE *msg)
{
	HJ_BUTTON *b = HJ_CAST(this, HJ_CLASS_BUTTON, HJ_BUTTON);

	switch (msg->type) {
		case HJ_MESSAGE_REPAINT:
			return button_repaint(b, msg->repaint.gc, msg->repaint.rect);

		case HJ_MESSAGE_RESIZE:
			return button_resize(b, msg->resize.x, msg->resize.y, msg->resize.surface_fmt);

		case HJ_MESSAGE_MOUSE_DOWN:
			return button_mouse_down(b, POINT(msg->mouse.x, msg->mouse.y), msg->mouse.button);

		case HJ_MESSAGE_MOUSE_UP:
			return button_mouse_up(b, POINT(msg->mouse.x, msg->mouse.y), msg->mouse.button);

		default:
			return S_FALSE;
	}
}

static HRESULT button_repaint(HJ_BUTTON *this, NXGI_GRAPHICS_CONTEXT *gc, HJ_RECT clip_rect)
{
	HJ_RECT	r = this->bounds_rect;

	UNUSED_ARG(clip_rect);

	if (this->down) {
		r = nxgig_rect_offset(r, POINT(0, RECT_HEIGHT(r)));
	}

	mutex_lock(&this->control.surface_lock);
	nxgi_bitblt(gc, POINT(0, 0), this->control.surface, r);
	mutex_unlock(&this->control.surface_lock);

	return S_OK;
}

static HRESULT button_update_backbuffer(HJ_BUTTON *this)
{
	mutex_lock(&this->control.surface_lock);

	NXGI_GRAPHICS_CONTEXT *gc = hj_control_get_gc(&this->control);
	HJ_RECT	glyph_rect = this->bounds_rect;
	HRESULT	hr;

	if (!gc) {
		k_printf("button_update_backbuffer(): backbuffer not available.\n");
		goto unlock;
	}
	k_printf("button_update_backbuffer(): backbuffer available r=(%d %d %d %d).\n", glyph_rect.x1, glyph_rect.y1, glyph_rect.x2, glyph_rect.y2);

	/* Draw upper glyph (unpressed state) */
	nxgi_set_color(gc, this->control.theme.window_color[HJ_COLOR_MAIN]);
	nxgi_fill_rect(gc, nxgig_rect_inflate(glyph_rect, -1));

	nxgi_set_color(gc, this->control.theme.window_color[HJ_COLOR_LIGHT]);
	nxgi_draw_line(gc, glyph_rect.x1 + 1, glyph_rect.y1 + 1, glyph_rect.x2 - 1, glyph_rect.x1 + 1);
	nxgi_draw_line(gc, glyph_rect.x1 + 1, glyph_rect.y1 + 1, glyph_rect.x1 + 1, glyph_rect.y2 - 1);

	nxgi_set_color(gc, this->control.theme.window_color[HJ_COLOR_DARK]);
	nxgi_draw_rect(gc, glyph_rect);

	nxgi_set_font(gc, FONT_PARAMS("System", 1, 0, this->control.theme.text_color[HJ_COLOR_MAIN]));
	nxgi_draw_aligned_text(gc, nxgig_rect_inflate(glyph_rect, -3), NXGI_HALIGN_CENTER, NXGI_VALIGN_MIDDLE, this->caption);

	glyph_rect = nxgig_rect_offset(this->bounds_rect, POINT(0, RECT_HEIGHT(this->bounds_rect)));

	/* Draw lower glyph (pressed state) */
	nxgi_set_color(gc, this->control.theme.window_color[HJ_COLOR_MAIN]);
	nxgi_fill_rect(gc, nxgig_rect_inflate(glyph_rect, -1));

	nxgi_set_color(gc, this->control.theme.window_color[HJ_COLOR_LIGHT]);
	nxgi_draw_line(gc, glyph_rect.x2 - 2, glyph_rect.y1 + 1, glyph_rect.x2 - 2, glyph_rect.y2 - 2);
	nxgi_draw_line(gc, glyph_rect.x1 + 1, glyph_rect.y2 - 2, glyph_rect.x2 - 2, glyph_rect.y2 - 2);

	nxgi_set_color(gc, this->control.theme.window_color[HJ_COLOR_DARK]);
	nxgi_draw_rect(gc, glyph_rect);

	nxgi_set_font(gc, FONT_PARAMS("System", 1, 0, this->control.theme.text_color[HJ_COLOR_MAIN]));
	nxgi_draw_aligned_text(gc, nxgig_rect_offset(nxgig_rect_inflate(glyph_rect, -3), POINT(1,1)), NXGI_HALIGN_CENTER, NXGI_VALIGN_MIDDLE, this->caption);

unlock:
	mutex_unlock(&this->control.surface_lock);

	/* Request parent repaint */
	hr = hj_control_request_parent_repaint(&this->control, NULL);
	if (FAILED(hr)) return hr;

	return S_OK;
}

HJ_BUTTON *hj_create_button()
{
	HJ_BUTTON 	*btn;
	HRESULT		hr;

	if (!(btn = kcalloc(sizeof(HJ_BUTTON)))) {
		/* Out of memory */
		return NULL;
	}

	/* Initialize base object and control components */
	HJ_OBJECT *bo = &btn->control.base_object;
	bo->class = HJ_CLASS_BUTTON;
	bo->size  = sizeof(HJ_BUTTON);
	bo->inst_table[HJ_CLASS_OBJECT]	 = &btn->control.base_object;
	bo->inst_table[HJ_CLASS_CONTROL] = &btn->control;
	bo->inst_table[HJ_CLASS_BUTTON]  = btn;

	/* Setup control subcomponent fields.
	 */
	hr = hj_control_init(&btn->control, HJ_CONTROL_IMMEDIATE, button_message_handler, NULL);
	if (FAILED(hr)) goto fail;

	/* Populate */
	strcpy(btn->caption, "Button");

	return btn;

fail:
	/* Clean up */
	hj_btn_destroy(btn);
	return NULL;

}

HRESULT hj_btn_set_caption(HJ_BUTTON *b, char *caption)
{
	HRESULT hr;

	strcpy(b->caption, caption);

	hr = button_update_backbuffer(b);
	return hr;
}

HRESULT hj_btn_get_caption(HJ_BUTTON *b, char *caption)
{
	strcpy(caption, b->caption);
	return S_OK;
}

VOID	hj_btn_destroy(HJ_BUTTON *btn)
{
	//TODO
}

static HRESULT button_resize(HJ_BUTTON *this, uint32_t x, uint32_t y, NXGI_FORMAT fmt)
{
	HRESULT hr;

	mutex_lock(&this->control.surface_lock);

	hr = hj_control_realloc_surface(&this->control, SIZE(x, y*2), fmt);
	if (FAILED(hr)) goto unlock;

	/* Update control size */
	mutex_lock(&this->control.property_lock);
	this->control.size = SIZE(x, y);
	mutex_unlock(&this->control.property_lock);

	this->bounds_rect = RECT(0, 0, x, y);

	hr = button_update_backbuffer(this);
	if (FAILED(hr)) goto unlock;

unlock:
	mutex_unlock(&this->control.surface_lock);

	return hr;
}

static HRESULT button_mouse_down(HJ_BUTTON *this, HJ_POINT pos, HJ_MOUSE_BUTTON btn)
{
	UNUSED_ARG(pos);

	k_printf("button mouse down!\n");
	if (btn != HJ_MOUSE_BUTTON_LEFT) {
		return S_OK;
	}

	/* Change button down state */
	this->down = TRUE;

	/* Request repaint */
	return hj_control_request_parent_repaint(&this->control, NULL);
}

static HRESULT button_mouse_up(HJ_BUTTON *this, HJ_POINT pos, HJ_MOUSE_BUTTON btn)
{
	UNUSED_ARG(pos);

	if (btn != HJ_MOUSE_BUTTON_LEFT) {
		return S_OK;
	}

	/* Change button down state */
	this->down = FALSE;

	/* Request repaint */
	return hj_control_request_parent_repaint(&this->control, NULL);
}
