/*
 * graphics_graphics.c
 *
 *	Implementation of graphics context methods.
 *
 *  Created on: 3.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <stdlib.h>
#include <string.h>
#include <mm.h>
#include "nxgi_graphics.h"

/*
 * Prototypes
 */
static HRESULT 		__nxapi intrnl_draw_hline_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x1, uint32_t y, uint32_t x2);
static HRESULT		__nxapi intrnl_draw_vline_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x, uint32_t y1, uint32_t y2);
static void 		__nxapi intrnl_transform_point(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *p);
static void 		__nxapi intrnl_apply_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *p);
static inline void 	__nxapi intrnl_draw_char_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x, uint32_t y, NXGI_FONT *font, char c);
static inline void 	__nxapi intrnl_draw_char_segment_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x, uint32_t y, NXGI_RECT clip_rect, NXGI_FONT *font, char c);
static NXGI_SIZE 	__nxapi intrnl_get_char_size(NXGI_GRAPHICS_CONTEXT *gc, char c);

static inline void	intrnl_swap_ints(int32_t *i1, int32_t *i2);

/* System font dimensions */
#define SYS_FONT_WIDTH 		8
#define SYS_FONT_HEIGHT 	8

/*
 * System VGA font (1 bit per pixel). It takes 1 kilobyte of
 * memory.
 */
static char __sys_font[128][8] = {
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  { 0x00, 0x3E, 0x41, 0x55, 0x41, 0x55, 0x49, 0x3E },
	  { 0x00, 0x3E, 0x7F, 0x6B, 0x7F, 0x6B, 0x77, 0x3E },
	  { 0x00, 0x22, 0x77, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 },
	  { 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08 },
	  { 0x00, 0x08, 0x1C, 0x2A, 0x7F, 0x2A, 0x08, 0x1C },
	  { 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x08, 0x1C },
	  { 0x00, 0x00, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00 },
	  { 0xFF, 0xFF, 0xE3, 0xC1, 0xC1, 0xC1, 0xE3, 0xFF },
	  { 0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00 },
	  { 0xFF, 0xFF, 0xE3, 0xDD, 0xDD, 0xDD, 0xE3, 0xFF },
	  { 0x00, 0x0F, 0x03, 0x05, 0x39, 0x48, 0x48, 0x30 },
	  { 0x00, 0x08, 0x3E, 0x08, 0x1C, 0x22, 0x22, 0x1C },
	  { 0x00, 0x18, 0x14, 0x10, 0x10, 0x30, 0x70, 0x60 },
	  { 0x00, 0x0F, 0x19, 0x11, 0x13, 0x37, 0x76, 0x60 },
	  { 0x00, 0x08, 0x2A, 0x1C, 0x77, 0x1C, 0x2A, 0x08 },
	  { 0x00, 0x60, 0x78, 0x7E, 0x7F, 0x7E, 0x78, 0x60 },
	  { 0x00, 0x03, 0x0F, 0x3F, 0x7F, 0x3F, 0x0F, 0x03 },
	  { 0x00, 0x08, 0x1C, 0x2A, 0x08, 0x2A, 0x1C, 0x08 },
	  { 0x00, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x66 },
	  { 0x00, 0x3F, 0x65, 0x65, 0x3D, 0x05, 0x05, 0x05 },
	  { 0x00, 0x0C, 0x32, 0x48, 0x24, 0x12, 0x4C, 0x30 },
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x7F },
	  { 0x00, 0x08, 0x1C, 0x2A, 0x08, 0x2A, 0x1C, 0x3E },
	  { 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x1C, 0x1C, 0x1C },
	  { 0x00, 0x1C, 0x1C, 0x1C, 0x7F, 0x3E, 0x1C, 0x08 },
	  { 0x00, 0x08, 0x0C, 0x7E, 0x7F, 0x7E, 0x0C, 0x08 },
	  { 0x00, 0x08, 0x18, 0x3F, 0x7F, 0x3F, 0x18, 0x08 },
	  { 0x00, 0x00, 0x00, 0x70, 0x70, 0x70, 0x7F, 0x7F },
	  { 0x00, 0x00, 0x14, 0x22, 0x7F, 0x22, 0x14, 0x00 },
	  { 0x00, 0x08, 0x1C, 0x1C, 0x3E, 0x3E, 0x7F, 0x7F },
	  { 0x00, 0x7F, 0x7F, 0x3E, 0x3E, 0x1C, 0x1C, 0x08 },
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  { 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18 },
	  { 0x00, 0x36, 0x36, 0x14, 0x00, 0x00, 0x00, 0x00 },
	  { 0x00, 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36 },
	  { 0x00, 0x08, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x08 },
	  { 0x00, 0x60, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x06 },
	  { 0x00, 0x3C, 0x66, 0x3C, 0x28, 0x65, 0x66, 0x3F },
	  { 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00 },
	  { 0x00, 0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60 },
	  { 0x00, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06 },
	  { 0x00, 0x00, 0x36, 0x1C, 0x7F, 0x1C, 0x36, 0x00 },
	  { 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00 },
	  { 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x60 },
	  { 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00 },
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60 },
	  { 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00 },
	  { 0x00, 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C },
	  { 0x00, 0x18, 0x18, 0x38, 0x18, 0x18, 0x18, 0x7E },
	  { 0x00, 0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E },
	  { 0x00, 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C },
	  { 0x00, 0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C },
	  { 0x00, 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C },
	  { 0x00, 0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C },
	  { 0x00, 0x7E, 0x66, 0x0C, 0x0C, 0x18, 0x18, 0x18 },
	  { 0x00, 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C },
	  { 0x00, 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C },
	  { 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00 },
	  { 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30 },
	  { 0x00, 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06 },
	  { 0x00, 0x00, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x00 },
	  { 0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60 },
	  { 0x00, 0x3C, 0x66, 0x06, 0x1C, 0x18, 0x00, 0x18 },
	  { 0x00, 0x38, 0x44, 0x5C, 0x58, 0x42, 0x3C, 0x00 },
	  { 0x00, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66 },
	  { 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C },
	  { 0x00, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C },
	  { 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C },
	  { 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E },
	  { 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60 },
	  { 0x00, 0x3C, 0x66, 0x60, 0x60, 0x6E, 0x66, 0x3C },
	  { 0x00, 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66 },
	  { 0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C },
	  { 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38 },
	  { 0x00, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66 },
	  { 0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E },
	  { 0x00, 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63 },
	  { 0x00, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x63 },
	  { 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C },
	  { 0x00, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60 },
	  { 0x00, 0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x06 },
	  { 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66 },
	  { 0x00, 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C },
	  { 0x00, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x18 },
	  { 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E },
	  { 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18 },
	  { 0x00, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63 },
	  { 0x00, 0x63, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x63 },
	  { 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18 },
	  { 0x00, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E },
	  { 0x00, 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E },
	  { 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00 },
	  { 0x00, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78 },
	  { 0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00 },
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F },
	  { 0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00 },
	  { 0x00, 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E },
	  { 0x00, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x7C },
	  { 0x00, 0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C },
	  { 0x00, 0x06, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3E },
	  { 0x00, 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C },
	  { 0x00, 0x1C, 0x36, 0x30, 0x30, 0x7C, 0x30, 0x30 },
	  { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C },
	  { 0x00, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66 },
	  { 0x00, 0x00, 0x18, 0x00, 0x18, 0x18, 0x18, 0x3C },
	  { 0x00, 0x0C, 0x00, 0x0C, 0x0C, 0x6C, 0x6C, 0x38 },
	  { 0x00, 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66 },
	  { 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 },
	  { 0x00, 0x00, 0x00, 0x63, 0x77, 0x7F, 0x6B, 0x6B },
	  { 0x00, 0x00, 0x00, 0x7C, 0x7E, 0x66, 0x66, 0x66 },
	  { 0x00, 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C },
	  { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60 },
	  { 0x00, 0x00, 0x3C, 0x6C, 0x6C, 0x3C, 0x0D, 0x0F },
	  { 0x00, 0x00, 0x00, 0x7C, 0x66, 0x66, 0x60, 0x60 },
	  { 0x00, 0x00, 0x00, 0x3E, 0x40, 0x3C, 0x02, 0x7C },
	  { 0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18 },
	  { 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E },
	  { 0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x3C, 0x18 },
	  { 0x00, 0x00, 0x00, 0x63, 0x6B, 0x6B, 0x6B, 0x3E },
	  { 0x00, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66 },
	  { 0x00, 0x00, 0x00, 0x66, 0x66, 0x3E, 0x06, 0x3C },
	  { 0x00, 0x00, 0x00, 0x3C, 0x0C, 0x18, 0x30, 0x3C },
	  { 0x00, 0x0E, 0x18, 0x18, 0x30, 0x18, 0x18, 0x0E },
	  { 0x00, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18 },
	  { 0x00, 0x70, 0x18, 0x18, 0x0C, 0x18, 0x18, 0x70 },
	  { 0x00, 0x00, 0x00, 0x3A, 0x6C, 0x00, 0x00, 0x00 },
	  { 0x00, 0x08, 0x1C, 0x36, 0x63, 0x41, 0x41, 0x7F }
};

/*
 * Implementation
 */
HRESULT __nxapi graphics_set_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP *pTarget)
{
	/* Unreference and detach old target */
	if (gc->target) {
		gc->target->ref_count--;
		gc->target = NULL;
	}

	if (pTarget == NULL) {
		return S_OK;
	}

	/* This graphical engine can handle on software bitmaps */
	if (pTarget->tag != NXGI_BITMAP_TAG_DEFAULT && pTarget->tag != NXGI_BITMAP_TAG_SCREEN_SURFACE) {
		return E_FAIL;
	}

	/* Attach new one */
	pTarget->ref_count++;
	gc->target = pTarget;

	/* Reset clip rect */
	NXGI_RECT cr;
	cr.x1 = 0;
	cr.y1 = 0;
	cr.x2 = (int32_t)pTarget->width;
	cr.y2 = (int32_t)pTarget->height;

	gc->set_clip_rect(gc, cr);

	/* Assign new drawing ops, depending of bitmap format */
	switch (gc->target->format) {
		case NXGI_FORMAT_BGRA32:
			gc->set_pixel = graphics_set_pixel_bgra32;
			gc->get_pixel = graphics_get_pixel_bgra32;
			gc->draw_line = graphics_draw_line_bgra32;
			gc->draw_rect = graphics_draw_rect_bgra32;
			gc->fill_rect = graphics_fill_rect_bgra32;
			gc->bitblt	= graphics_bitblt_bgra32;
			gc->stretchblt = graphics_stretchblt_bgra32;
			gc->alphablend = graphics_alphablend_bgra32;
			gc->draw_text = graphics_draw_text_bgra32;
			break;

		default:
			/* Unsupported pixel format */
			return E_FAIL;
	}

	return S_OK;
}

HRESULT __nxapi graphics_get_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP **ppTarget)
{
	*ppTarget = gc->target;
	return S_OK;
}

HRESULT __nxapi graphics_set_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR color)
{
	gc->color = color;
	return S_OK;
}

HRESULT __nxapi graphics_get_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR *pColor)
{
	*pColor = gc->color;
	return S_OK;
}

HRESULT __nxapi graphics_set_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT offset)
{
	gc->offset = offset;
	return S_OK;
}

HRESULT __nxapi graphics_get_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *pOffset)
{
	*pOffset = gc->offset;
	return S_OK;
}

HRESULT __nxapi graphics_set_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT clip_rect)
{
	/* Transparently sanitize rect */
	if (clip_rect.x1 < 0) clip_rect.x1 = 0;
	if (clip_rect.y1 < 0) clip_rect.y1 = 0;
	if (clip_rect.x2 < 0) clip_rect.x2 = 0;
	if (clip_rect.y2 < 0) clip_rect.y2 = 0;
	if (clip_rect.x1 > (int32_t)gc->target->width) clip_rect.x1 = gc->target->width;
	if (clip_rect.y1 > (int32_t)gc->target->height) clip_rect.y1 = gc->target->height;
	if (clip_rect.x2 > (int32_t)gc->target->width) clip_rect.x2 = gc->target->width;
	if (clip_rect.y2 > (int32_t)gc->target->height) clip_rect.y2 = gc->target->height;

	gc->clip_rect = clip_rect;
	return S_OK;
}

HRESULT __nxapi graphics_get_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT *clip_rect)
{
	*clip_rect = gc->clip_rect;
	return S_OK;
}

HRESULT __nxapi graphics_draw_rect_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect)
{
	if (!gc->target) {
		return E_INVALIDSTATE;
	}

	/* Check if rectangle is outside of the clipping rect */
	if (rect.x1 == rect.x2 || rect.y1 == rect.y2) {
		/* Nothing to do here */
		return S_FALSE;
	}

	rect.x2 -= 1;
	rect.y2 -= 1;

	if (rect.x1 == rect.x2 || rect.y1 == rect.y2) {
		/* Nothing to do here */
		return S_FALSE;
	}

	/* Offseting/clipping is applied by draw line routine */
	graphics_draw_line_bgra32(gc, rect.x1, rect.y1, rect.x2, rect.y1);
	graphics_draw_line_bgra32(gc, rect.x2, rect.y1, rect.x2, rect.y2);
	graphics_draw_line_bgra32(gc, rect.x2, rect.y2, rect.x1, rect.y2);
	graphics_draw_line_bgra32(gc, rect.x1, rect.y2, rect.x1, rect.y1);

	return S_OK;
}

HRESULT __nxapi graphics_stretchblt_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT dst_rect, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect)
{
	UNUSED_ARG(gc);
	UNUSED_ARG(dst_rect);
	UNUSED_ARG(pSrcBitmap);
	UNUSED_ARG(src_rect);

	return E_NOTIMPL;
}

HRESULT __nxapi graphics_alphablend_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect)
{
	UNUSED_ARG(gc);
	UNUSED_ARG(dst_pos);
	UNUSED_ARG(pSrcBitmap);
	UNUSED_ARG(src_rect);

	return E_NOTIMPL;
}

static HRESULT __nxapi intrnl_draw_hline_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x1, uint32_t y, uint32_t x2)
{
	/* No need to assert anythings, since this is an internal function
	 * and input arguments can be trusted.
	 */
	uint32_t tmp;
	if (x1 > x2) {
		tmp = x1;
		x1 = x2;
		x2 = tmp;
	}

	uint32_t width = x2 - x1;
	NXGI_COLOR *pixel = (NXGI_COLOR*)((uint8_t*)gc->target->pBits + (y * gc->target->stride) + (x1 * gc->target->bits_per_pixel / 8));

	while (width--) {
		*pixel = gc->color;
		pixel++;
	}

	return S_OK;
}

static HRESULT __nxapi intrnl_draw_vline_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x, uint32_t y1, uint32_t y2)
{
	uint32_t tmp;
	if (y1 > y2) {
		tmp = y1;
		y1 = y2;
		y2 = tmp;
	}

	uint32_t height = y2 - y1;
	uint8_t *pixel = (uint8_t*)gc->target->pBits + (y1 * gc->target->stride) + (x * gc->target->bits_per_pixel / 8);

	while (height--) {
		*(NXGI_COLOR*)pixel = gc->color;
		pixel += gc->target->stride;
	}

	return S_OK;
}

/*
 * Applies offset and clipping to point.
 */
static void __nxapi intrnl_transform_point(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *p)
{
	p->x += gc->offset.x;
	p->y += gc->offset.y;

	/* Apply clipping for X */
	if (p->x > gc->clip_rect.x2) {
		p->x = gc->clip_rect.x2;
	} else if (p->x < gc->clip_rect.x1) {
		p->x = gc->clip_rect.x1;
	}

	/* Same for Y */
	if (p->y > gc->clip_rect.y2) {
		p->y = gc->clip_rect.y2;
	} else if (p->y < gc->clip_rect.y1) {
		p->y = gc->clip_rect.y1;
	}
}

static void __nxapi intrnl_apply_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *p)
{
	p->x += gc->offset.x;
	p->y += gc->offset.y;
}

static inline void intrnl_swap_ints(int32_t *i1, int32_t *i2)
{
	int32_t t = *i1;
	*i1 = *i2;
	*i2 = t;
}

/*
 * Draws a line using Bresenham's line algorithm. Clipping is performed
 * before drawing take places.
 */
HRESULT __nxapi graphics_draw_line_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
	/* Assert that a target is assigned */
	if (!gc->target) {
		return E_INVALIDSTATE;
	}

	NXGI_RECT 	r = gc->clip_rect;
	NXGI_POINT 	p1 = {x1, y1};
	NXGI_POINT 	p2 = {x2, y2};

	/* For lines, we need additional clipping logic. If the line
	 * is totally outside of the clipping rect, just do nothing.
	 */
	NXGI_RECT unoffs_r = nxgig_rect_offset(r, POINT(-gc->offset.x, -gc->offset.y));
	if (!nxgig_line_rect_intersect(POINT(x1, y1), POINT(x2, y2), unoffs_r)) {
		/* Out of clipping rect */
		return S_OK;
	}

	/* Clip points */
	intrnl_transform_point(gc, &p1);
	intrnl_transform_point(gc, &p2);

	/* Handle axis-aligned lines. */
	if (p1.x == p2.x) {
		return intrnl_draw_vline_bgra32(gc, p1.x, p1.y, p2.y);
	}

	if (p1.y == p2.y) {
		return intrnl_draw_hline_bgra32(gc, p1.x, p1.y, p2.x);
	}

	/* Draw non-axis-aligned line */
	int32_t dx = abs(p2.x - p1.x);
	int32_t	sx = p1.x<p2.x ? 1 : -1;
	int32_t dy = abs(p2.y - p1.y);
	int32_t	sy = p1.y < p2.y ? 1 : -1;
	int32_t err = (dx>dy ? dx : -dy) / 2;
	int32_t e2;

	while (TRUE) {
		NXGI_COLOR *pixel = (NXGI_COLOR*)((uint8_t*)gc->target->pBits + (p1.x * gc->target->bits_per_pixel / 8) + (p1.y * gc->target->stride));
		*pixel = gc->color;

		if (p1.x == p2.x && p1.y == p2.y) {
			break;
		}

		e2 = err;
		if (e2 > -dx) {
			err -= dy;
			p1.x += sx;
		}

		if (e2 <  dy) {
			err += dx;
			p1.y += sy;
		}
	}

	return S_OK;
}

HRESULT __nxapi graphics_set_pixel_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, NXGI_COLOR color)
{
	/* Assert that a target is assigned */
	if (!gc->target) {
		return E_INVALIDSTATE;
	}

	/*
	 * Offset and clipping doesn't apply for get/set pixel API!
	 */
	uint8_t *dst = gc->target->pBits;
	dst += pos.x * gc->target->bits_per_pixel / 8;
	dst += pos.y * gc->target->stride;

	*(NXGI_COLOR*)dst = color;
	return S_OK;
}

HRESULT __nxapi graphics_get_pixel_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, NXGI_COLOR *color)
{
	/* Assert that a target is assigned */
	if (!gc->target) {
		return E_INVALIDSTATE;
	}

	/*
	 * Offset and clipping doesn't apply for get/set pixel API!
	 */
	uint8_t *src = gc->target->pBits;
	src += pos.x * gc->target->bits_per_pixel / 8;
	src += pos.y * gc->target->stride;

	*color = *(NXGI_COLOR*)src;
	return S_OK;
}

HRESULT __nxapi graphics_fill_rect_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect)
{
	int32_t y;

	if (!gc->target) {
		return E_INVALIDSTATE;
	}

	/* Apply offset and clipping */
	intrnl_transform_point(gc, &rect.p1);
	intrnl_transform_point(gc, &rect.p2);

	/* Check if rectangle is outside of the clipping rect */
	if (rect.x1 == rect.x2 || rect.y1 == rect.y2) {
		/* Nothing to do here */
		return S_FALSE;
	}

	if (rect.y1 > rect.y2) {
		intrnl_swap_ints(&rect.y1, &rect.y2);
	}

	for (y=rect.y1; y<rect.y2; y++) {
		intrnl_draw_hline_bgra32(gc, rect.x1, y, rect.x2);
	}

	return S_OK;
}

HRESULT __nxapi graphics_bitblt_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect)
{
	/* Check if target is attached */
	if (!gc->target) {
		return E_INVALIDSTATE;
	}

	/* We don't support format conversion at this point. */
	if (pSrcBitmap->format != gc->target->format) {
		return E_NOTIMPL;
	}

	/* Make sure source position is in bounds */
	if (!nxgig_rect_contains_rect(src_rect, RECT(0, 0, pSrcBitmap->width, pSrcBitmap->height))) {
		return E_INVALIDARG;
	}

	NXGI_RECT dst_rect, dst_clipped_rect;
	dst_rect.p1 = dst_pos;
	dst_rect.p2 = POINT(dst_pos.x + RECT_WIDTH(src_rect), dst_pos.y + RECT_HEIGHT(src_rect));

	/* Copy dst_rect to dst_clipped_rect */
	memcpy(&dst_clipped_rect, &dst_rect, sizeof(NXGI_RECT));

	/* Apply offset for unclipped rect (dst_rect) */
	intrnl_apply_offset(gc, &dst_rect.p1);
	intrnl_apply_offset(gc, &dst_rect.p2);

	/* Apply offset and clipping */
	intrnl_transform_point(gc, &dst_clipped_rect.p1);
	intrnl_transform_point(gc, &dst_clipped_rect.p2);

	/* Hold the difference between the clipped and non-clipped
	 * dst rect.
	 */
	NXGI_POINT clip_offset = POINT(dst_clipped_rect.x1 - dst_rect.x1, dst_clipped_rect.y1 - dst_rect.y1);

	/* Test if destination rectangle is clipped out */
	if (dst_rect.x1 == dst_rect.x2 || dst_rect.y1 == dst_rect.y2) {
		/* Nothing to do here */
		return S_FALSE;
	}

	int32_t 	h = RECT_HEIGHT(dst_clipped_rect);
	uint32_t 	line_size = (uint32_t)RECT_WIDTH(dst_clipped_rect) * pSrcBitmap->bits_per_pixel / 8;
	uint32_t 	horiz_offs = dst_clipped_rect.x1 * gc->target->bits_per_pixel / 8;
	uint32_t 	src_horiz_offs = (src_rect.x1 + clip_offset.x) * gc->target->bits_per_pixel / 8;
	uint8_t 	*psrc, *pdst;
	int32_t 	j;
	uint32_t	byte_cntr = 0;

	for (j=0; j<h; j++) {
		pdst = (uint8_t*)gc->target->pBits + (dst_clipped_rect.y1 + j) * gc->target->stride + horiz_offs;
		psrc = (uint8_t*)pSrcBitmap->pBits + (src_rect.y1 + clip_offset.y + j) * pSrcBitmap->stride + src_horiz_offs;

		memcpy(pdst, psrc, line_size);
		byte_cntr += line_size;
	}

	return S_OK;
}

HRESULT __nxapi graphics_create_context(NXGI_GRAPHICS_CONTEXT **ppGC)
{
	NXGI_GRAPHICS_CONTEXT *gc;

	if (!(gc = kcalloc(sizeof(NXGI_GRAPHICS_CONTEXT)))) {
		return E_OUTOFMEM;
	}

	/* Set format-agnostic methods. The remaining ones
	 * are set in gc->set_target() method.
	 */
	gc->set_target = graphics_set_target;
	gc->get_target = graphics_get_target;
	gc->set_color = graphics_set_color;
	gc->get_color = graphics_get_color;
	gc->set_offset = graphics_set_offset;
	gc->get_offset = graphics_get_offset;
	gc->set_clip_rect = graphics_set_clip_rect;
	gc->get_clip_rect = graphics_get_clip_rect;
	gc->text_size = graphics_text_size;
	gc->set_font = graphics_set_font;
	gc->get_font = graphics_get_font;
	gc->destroy = graphics_destroy_context;

	/* Set default font */
	NXGI_FONT default_font;

	strcpy(default_font.name, "System");
	default_font.size = 1;
	default_font.style = 0;
	default_font.color = (NXGI_COLOR){255, 255, 255, 255};
	default_font.reserved = 0;

	gc->set_font(gc, default_font);

	/* Success */
	*ppGC = gc;
	return S_OK;
}

void 	__nxapi graphics_destroy_context(NXGI_GRAPHICS_CONTEXT *gc)
{
	/* Detach target */
	gc->set_target(gc, NULL);

	/* Free context */
	kfree(gc);
}

static inline void __nxapi intrnl_draw_char_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x, uint32_t y, NXGI_FONT *font, char c)
{
	uint32_t	i, j;
	NXGI_COLOR  *pix_ptr;
	char		glyph, glyph_offs;

	/* We don't check input arguments here. `c` is exception. */
	if (c < 0) {
		return;
	}

	/* Iterate rows */
	for (j=0; j<SYS_FONT_HEIGHT; j++) {
		glyph = __sys_font[(uint8_t)c][j];

		if (glyph == 0) {
			/* Empty line */
			continue;
		}

		pix_ptr = (NXGI_COLOR*)((uint8_t*)gc->target->pBits + x * gc->target->bits_per_pixel / 8 + (y+j) * gc->target->stride);

		for (i=0; i<SYS_FONT_WIDTH; i++) {
			glyph_offs = glyph >> i;

			if (glyph_offs & 1) {
				pix_ptr[SYS_FONT_WIDTH - i] = font->color;
			}

			if (glyph_offs <= 1) {
				/* No pixels remain in row */
				break;
			}
		}
	}
}

static inline void 	__nxapi intrnl_draw_char_segment_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x, uint32_t y, NXGI_RECT clip_rect, NXGI_FONT *font, char c)
{
	uint32_t	i, j;
	NXGI_COLOR  *pix_ptr;
	char		glyph, glyph_offs;

	/* We don't check input arguments here. `c` is exception. */
	if (c < 0) {
		return;
	}

	/* Iterate rows */
	for (j=0; j<SYS_FONT_HEIGHT; j++) {
		int32_t ly = y + (int32_t)j;
		if (ly < clip_rect.y1 || ly > clip_rect.y2) {
			continue;
		}

		glyph = __sys_font[(uint8_t)c][j];

		if (glyph == 0) {
			/* Empty line */
			continue;
		}

		pix_ptr = (NXGI_COLOR*)((uint8_t*)gc->target->pBits + x * gc->target->bits_per_pixel / 8 + (y+j) * gc->target->stride);

		for (i=0; i<SYS_FONT_WIDTH; i++) {
			int32_t lx = x + SYS_FONT_WIDTH - (int32_t)i;
			if (lx < clip_rect.x1 || lx > clip_rect.x2) {
				continue;
			}

			glyph_offs = glyph >> i;

			if (glyph_offs & 1) {
				pix_ptr[SYS_FONT_WIDTH - i] = font->color;
			}

			if (glyph_offs <= 1) {
				/* No pixels remain in row */
				break;
			}
		}
	}
}

HRESULT __nxapi graphics_set_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT font_params)
{
	/* Validate */
	if (strcmp(font_params.name, "System") != 0) {
		/* We don't support other fonts at this time */
		return E_INVALIDARG;
	}

	if (font_params.size != 1) {
		/* We don't support scaling */
		return E_INVALIDARG;
	}

	gc->font = font_params;
	return S_OK;
}

HRESULT __nxapi graphics_get_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT *font_params_out)
{
	*font_params_out = gc->font;
	return S_OK;
}

HRESULT __nxapi graphics_text_size(NXGI_GRAPHICS_CONTEXT *gc, char *text, NXGI_SIZE *size_out)
{
	char *s = text;

	size_out->width = 0;

	while (*s) {
		//size_out->width += intrnl_get_char_size(gc, *s).width;
		size_out->width++;
		s++;
	}

	size_out->width *= intrnl_get_char_size(gc, ' ').width;

	/* Supposedly, every symbol has sample height */
	size_out->height = intrnl_get_char_size(gc, ' ').height;

	return S_OK;
}

static NXGI_SIZE 	__nxapi intrnl_get_char_size(NXGI_GRAPHICS_CONTEXT *gc, char c)
{
	NXGI_SIZE p = {SYS_FONT_WIDTH, SYS_FONT_HEIGHT};
	UNUSED_ARG(c);

	if (gc->font.size != 1) {
		p = (NXGI_SIZE){0, 0};
	}

	return p;
}


HRESULT __nxapi graphics_draw_text_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, char *text)
{
	NXGI_BITMAP *t = gc->target;
	NXGI_RECT	bounds; //bounds rect
	NXGI_SIZE	text_size;
	uint32_t	len = strlen(text);
	uint32_t	draw_cntr = 0;
	uint32_t	i;
	HRESULT		hr;

	if (!t) {
		return E_INVALIDSTATE;
	}

	/* Find text bounds rect */
	hr = graphics_text_size(gc, text, &text_size);
	if (FAILED(hr)) return hr;

	bounds.p1 = pos;
	bounds.p2 = (NXGI_POINT){pos.x + text_size.width, pos.y + text_size.height};

	/* Clip bounds rect */
	intrnl_transform_point(gc, &bounds.p1);
	intrnl_transform_point(gc, &bounds.p2);

	/* If the whole text is clipped out, then do nothing */
	if (bounds.p2.x - bounds.p1.x == 0 || bounds.p2.y - bounds.p1.y == 0) {
		/* Fully clipped out */
		return S_OK;
	}

	for (i=0; i<len; i++) {
		NXGI_SIZE char_size = intrnl_get_char_size(gc, text[i]);
		NXGI_RECT char_rect = RECT(pos.x, pos.y, pos.x + char_size.width, pos.y + char_size.height);

		/* Offset and clip char rect */
		intrnl_transform_point(gc, &char_rect.p1);
		intrnl_transform_point(gc, &char_rect.p2);

		/* Clip test */
		if (char_rect.p2.x - char_rect.p1.x >= char_size.width && char_rect.p2.y - char_rect.p1.y >= char_size.height) {
			/* Visible */
			intrnl_draw_char_bgra32(gc, char_rect.x1, char_rect.y1, &gc->font, text[i]);

			/* One more character.. Phuu */
			draw_cntr++;
		} else if (char_rect.p2.x - char_rect.p1.x > 0 && char_rect.p2.y - char_rect.p1.y > 0) {
			/* Partially clipped */
			intrnl_draw_char_segment_bgra32(gc, pos.x + gc->offset.x, pos.y + gc->offset.y, char_rect, &gc->font, text[i]);

			/* One more character.. Phuu */
			draw_cntr++;
		} else {
			/* Clipped out */
			if (draw_cntr > 0) {
				/* If the current character is clipped, while the previous was visible,
				 * this means we have left the clip rect, and none of the remaining
				 * characters will be visible.
				 */
				break;
			}
		}

		/* Move position */
		pos.x += char_size.width;
	}

	return S_OK;
}
