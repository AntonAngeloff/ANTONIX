/*
 * nxgi_graphics.h
 *
 *  Created on: 3.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_NXGI_GRAPHICS_H_
#define SUBSYSTEMS_NXGI_GRAPHICS_H_

#include <types.h>
#include "nxgi.h"

/*
 * Graphics Context methods. TODO: make static
 */
HRESULT __nxapi graphics_create_context(NXGI_GRAPHICS_CONTEXT **ppGC);
HRESULT __nxapi graphics_set_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP *pTarget);
HRESULT __nxapi graphics_get_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP **ppTarget);
HRESULT __nxapi graphics_set_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR color);
HRESULT __nxapi graphics_get_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR *pColor);
HRESULT __nxapi graphics_set_pixel_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, NXGI_COLOR color);
HRESULT __nxapi graphics_get_pixel_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, NXGI_COLOR *color);
HRESULT __nxapi graphics_draw_line_bgra32(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);
HRESULT __nxapi graphics_draw_rect_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect);
HRESULT __nxapi graphics_fill_rect_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect);
HRESULT __nxapi graphics_bitblt_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);
HRESULT __nxapi graphics_stretchblt_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT dst_rect, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);
HRESULT __nxapi graphics_alphablend_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);
HRESULT __nxapi graphics_set_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT offset);
HRESULT __nxapi graphics_get_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *pOffset);
HRESULT __nxapi graphics_set_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT clip_rect);
HRESULT __nxapi graphics_get_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT *clip_rect);
HRESULT __nxapi graphics_draw_text_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, char *text);
HRESULT __nxapi graphics_text_size(NXGI_GRAPHICS_CONTEXT *gc, char *text, NXGI_SIZE *size_out);
HRESULT __nxapi graphics_set_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT font_params);
HRESULT __nxapi graphics_get_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT *font_params_out);
void 	__nxapi graphics_destroy_context(NXGI_GRAPHICS_CONTEXT *gc);

//TODO
HRESULT __nxapi graphics_clear_bgra32(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR color);

#endif /* SUBSYSTEMS_NXGI_GRAPHICS_H_ */
