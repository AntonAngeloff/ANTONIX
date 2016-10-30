/*
 * nxgi.c
 *
 *	ANTONIX Graphics Interface (NXGI)
 *
 *  Created on: 3.10.2016 ã.
 *      Author: Anton Angelov
 */
#include <mm.h>
#include <kstdio.h>
#include "nxgi.h"
#include "nxgi_graphics.h"

static NXGI_CONTEXT nxgi_context;

/*
 * Prototypes
 */
static uint32_t nxgi_format_to_bpp(NXGI_FORMAT fmt);
HRESULT __nxapi nxgi_destroy_bitmap_impl(NXGI_BITMAP **ppBmp);
HRESULT __nxapi nxgi_destroy_screensurf_impl(NXGI_BITMAP **ppBmp);
K_VIDEO_DRIVER_IFACE __nxapi *get_graphics_iface();
/*
 * Implementation
 */
static uint32_t nxgi_format_to_bpp(NXGI_FORMAT fmt)
{
	return video_format_to_bpp((K_VIDEO_FORMAT)fmt);
}

HRESULT __nxapi nxgi_create_bitmap(uint32_t width, uint32_t height, NXGI_FORMAT format, NXGI_BITMAP **ppBmp)
{
	NXGI_BITMAP *bmp = NULL;
	HRESULT		hr;

	if (width == 0 || height == 0 || nxgi_format_to_bpp(format) == 0) {
		return E_INVALIDARG;
	}

	if (!(ppBmp)) {
		return E_POINTER;
	}

	/* Allocate struct */
	if (!(bmp = kcalloc(sizeof(NXGI_BITMAP)))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	bmp->width = width;
	bmp->height = height;
	bmp->bits_per_pixel = nxgi_format_to_bpp(format);
	bmp->format = format;
	bmp->stride = bmp->width * bmp->bits_per_pixel / 8;
	bmp->tag = NXGI_BITMAP_TAG_DEFAULT;
	bmp->destroy = nxgi_destroy_bitmap_impl;

	if (!(bmp->pBits = kmalloc(bmp->stride * bmp->height))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	/* Success */
	*ppBmp = bmp;
	return S_OK;

fail:
	if (bmp) {
		if (bmp && bmp->pBits) {
			kfree(bmp->pBits);
		}

		kfree(bmp);
	}

	return hr;
}

/**
 * Default implementation of bitmap destructor for software bitmaps.
 */
HRESULT __nxapi nxgi_destroy_bitmap_impl(NXGI_BITMAP **ppBmp)
{
	NXGI_BITMAP *bmp = *ppBmp;

	kfree(bmp->pBits);
	kfree(bmp);

	*ppBmp = NULL;
	return S_OK;
}

HRESULT __nxapi nxgi_destroy_screensurf_impl(NXGI_BITMAP **ppBmp)
{
	/* Do nothing */
	UNUSED_ARG(ppBmp);

	*ppBmp = NULL;
	return S_OK;
}

/*
 * Bitmap destructor dispatcher.
 */
HRESULT __nxapi nxgi_destroy_bitmap(NXGI_BITMAP **ppBmp)
{
	NXGI_BITMAP *bmp = *ppBmp;
	return bmp->destroy(ppBmp);
}

/*
 * NXGI Graphics Context dispatch methods
 */
HRESULT __nxapi nxgi_create_graphics_context(NXGI_GRAPHICS_CONTEXT **ppGC)
{
	return graphics_create_context(ppGC);
}

HRESULT __nxapi nxgi_set_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP *pTarget)
{
	return gc->set_target(gc, pTarget);
}

HRESULT __nxapi nxgi_get_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP **ppTarget)
{
	return gc->get_target(gc, ppTarget);
}

void 	__nxapi nxgi_destroy_graphics_context(NXGI_GRAPHICS_CONTEXT *gc)
{
	gc->destroy(gc);
}

HRESULT __nxapi nxgi_set_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR color)
{
	return gc->set_color(gc, color);
}

HRESULT __nxapi nxgi_get_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR *pColor)
{
	return gc->get_color(gc, pColor);
}

HRESULT __nxapi nxgi_draw_line(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
	return gc->draw_line(gc, x1, y1, x2, y2);
}

HRESULT __nxapi nxgi_draw_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect)
{
	return gc->draw_rect(gc, rect);
}

HRESULT __nxapi nxgi_fill_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect)
{
	return gc->fill_rect(gc, rect);
}

HRESULT __nxapi nxgi_bitblt(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect)
{
	return gc->bitblt(gc, dst_pos, pSrcBitmap, src_rect);
}

HRESULT __nxapi nxgi_stretchblt(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT dst_rect, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect)
{
	return gc->stretchblt(gc, dst_rect, pSrcBitmap, src_rect);
}

HRESULT __nxapi nxgi_alphablend(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect)
{
	return gc->alphablend(gc, dst_pos, pSrcBitmap, src_rect);
}

HRESULT __nxapi nxgi_set_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT offset)
{
	return gc->set_offset(gc, offset);
}

HRESULT __nxapi nxgi_get_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *pOffset)
{
	return gc->get_offset(gc, pOffset);
}

HRESULT __nxapi nxgi_set_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT clip_rect)
{
	return gc->set_clip_rect(gc, clip_rect);
}

HRESULT __nxapi nxgi_get_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT *clip_rect)
{
	return gc->get_clip_rect(gc, clip_rect);
}

HRESULT __nxapi nxgi_get_screen(NXGI_BITMAP **ppBmp)
{
	*ppBmp = nxgi_context.screen_surf;
	return S_OK;
}

K_VIDEO_DRIVER_IFACE __nxapi *get_graphics_iface()
{
	K_VIDEO_DRIVER_IFACE *iface;

	if (FAILED(k_ioctl(nxgi_context.graphics_drv, IOCTL_GRAPHICS_GET_INTERFACE, &iface))) {
		return NULL;
	}

	return iface;
}

HRESULT __nxapi nxgi_init(uint32_t xres, uint32_t yres, NXGI_FORMAT fmt)
{
	NXGI_CONTEXT		*c = &nxgi_context;
	K_VIDEO_MODE_DESC 	desc;
	HRESULT				hr;

	/* Open handle to video driver */
	hr = k_fopen("/dev/video0", FILE_OPEN_READ, &c->graphics_drv);
	if (FAILED(hr)) {
		k_printf("Video driver not found.\n");
		return S_OK;
	}

	/* Set resolution */
	desc.width 	= xres;
	desc.height = yres;
	desc.format = fmt;

	hr = get_graphics_iface()->set_mode(get_graphics_iface(), &desc);
	if (FAILED(hr)) {
		k_printf("Failed to set resolution %dx%d.\n", desc.width, desc.height);
		return S_OK;
	}

	/* Get effective video mode */
	get_graphics_iface()->get_mode(get_graphics_iface(), &desc);

	/* Create screen surface */
	NXGI_BITMAP *ss;
	if (!(ss = kcalloc(sizeof(NXGI_BITMAP)))) {
		k_fclose(&nxgi_context.graphics_drv);
		return E_OUTOFMEM;
	}

	/* Populate fields */
	ss->width = desc.width;
	ss->height = desc.height;
	ss->format = desc.format;
	ss->bits_per_pixel = desc.bpp;
	ss->tag = NXGI_BITMAP_TAG_SCREEN_SURFACE;
	ss->destroy = nxgi_destroy_screensurf_impl;

	/* Get LFB pointer */
	hr = get_graphics_iface()->lock_fb(get_graphics_iface(), VIDEO_LOCK_WRITE | VIDEO_LOCK_READ, &ss->pBits, &ss->stride);
	if (FAILED(hr)) {
		kfree(ss);
		k_fclose(&nxgi_context.graphics_drv);
		return hr;
	}

	nxgi_context.screen_surf = ss;
	return S_OK;
}

HRESULT __nxapi nxgi_fini()
{
	if (nxgi_context.graphics_drv != NULL) {
		get_graphics_iface()->unlock_fb(get_graphics_iface());
	}

	if (nxgi_context.screen_surf != NULL) {
		/* Free screen surface */
		kfree(nxgi_context.screen_surf);
		nxgi_context.screen_surf = NULL;
	}

	if (nxgi_context.graphics_drv != NULL) {
		/* Close driver handle */
		k_fclose(&nxgi_context.graphics_drv);
	}

	return S_OK;
}

NXGI_RECT RECT(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
	NXGI_RECT r;

	r.x1 = x1;
	r.y1 = y1;
	r.x2 = x2;
	r.y2 = y2;

	return r;
}

NXGI_POINT POINT(int32_t x, int32_t y)
{
	NXGI_POINT p;

	p.x = x;
	p.y = y;

	return p;
}

NXGI_SIZE SIZE(int32_t w, int32_t h)
{
	return (NXGI_SIZE){w, h};
}

NXGI_FONT FONT_PARAMS(char *name, uint32_t size, uint32_t style, NXGI_COLOR color)
{
	NXGI_FONT f;

	/* TODO: check string overflow */

	strcpy(f.name, name);
	f.size = size;
	f.style = style;
	f.color = color;

	return f;
}

NXGI_COLOR COLOR(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	NXGI_COLOR res;

	res.r = r;
	res.b = b;
	res.g = g;
	res.a = a;

	return res;
}

HRESULT __nxapi nxgi_draw_text(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, char *text)
{
	return gc->draw_text(gc, pos, text);
}

HRESULT __nxapi nxgi_text_size(NXGI_GRAPHICS_CONTEXT *gc, char *text, NXGI_SIZE *size_out)
{
	return gc->text_size(gc, text, size_out);
}

HRESULT __nxapi nxgi_set_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT font_params)
{
	return gc->set_font(gc, font_params);
}

HRESULT __nxapi nxgi_get_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT *font_params_out)
{
	return gc->get_font(gc, font_params_out);
}

NXGI_FORMAT	__nxapi nxgi_internal_format()
{
	//TODO: use locks
	return nxgi_context.screen_surf->format;
}

HRESULT __nxapi nxgi_draw_aligned_text(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT box, NXGI_HALIGN halign, NXGI_VALIGN valign, char *text)
{
	NXGI_SIZE 	size;
	NXGI_POINT	pos;
	HRESULT		hr;

	hr = nxgi_text_size(gc, text, &size);
	if (FAILED(hr)) return hr;

	switch (halign) {
		case NXGI_HALIGN_LEFT:
			pos.x = box.x1;
			break;

		case NXGI_HALIGN_CENTER:
			pos.x = box.x1 + ((RECT_WIDTH(box) - size.width) / 2);
			break;

		case NXGI_HALIGN_RIGHT:
			pos.x = box.x2 - size.width;
			break;

		default:
			return E_INVALIDARG;
	}

	switch (valign) {
	case NXGI_VALIGN_TOP:
		pos.y = box.y1;
		break;

	case NXGI_VALIGN_MIDDLE:
		pos.y = box.y1 + ((RECT_HEIGHT(box) - size.height) / 2);
		break;

	case NXGI_VALIGN_BOTTOM:
		pos.y = box.y2 - size.height;
		break;

	default:
		return E_INVALIDARG;
	}

	hr = nxgi_draw_text(gc, pos, text);
	return hr;
}
