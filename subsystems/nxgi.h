/*
 * nxgi.h
 *
 *  ANTONIX Graphics Interface (NXGI)
 *
 *  Created on: 3.10.2016 ã.
 *      Author: Anton Angelov
 */

#include <types.h>
#include <kstream.h>
#include <devices_video.h>
#include "nxgi_geometry.h"

#ifndef SUBSYSTEMS_NXGI_H_
#define SUBSYSTEMS_NXGI_H_

/**
 * NXGI_FORMAT is alias for K_VIDEO_FORMAT.
 */
typedef enum {
	NXGI_FORMAT_BGRA32 = VIDEO_FORMAT_BGRA32,
	NXGI_FORMAT_RGBA32 = VIDEO_FORMAT_RGBA32,
} NXGI_FORMAT;

typedef enum {
	NXGI_BITMAP_TAG_DEFAULT	= 0x00,
	NXGI_BITMAP_TAG_SCREEN_SURFACE
} NXGI_BITMAP_TAG;

/* Alignment */
typedef enum {
	NXGI_HALIGN_LEFT,
	NXGI_HALIGN_CENTER,
	NXGI_HALIGN_RIGHT
} NXGI_HALIGN;

typedef enum {
	NXGI_VALIGN_TOP,
	NXGI_VALIGN_MIDDLE,
	NXGI_VALIGN_BOTTOM
} NXGI_VALIGN;

/**
 * Color
 */
typedef struct NXGI_COLOR NXGI_COLOR;
struct NXGI_COLOR {
	uint8_t b;
	uint8_t g;
	uint8_t r;
	uint8_t a;
} __packed;

/**
 * 2-D point.
 */
typedef struct NXGI_POINT NXGI_POINT;
struct NXGI_POINT {
	int32_t x;
	int32_t y;
};

/**
 * Simple struct for representing 2-D size.
 */
typedef struct NXGI_SIZE NXGI_SIZE;
struct NXGI_SIZE {
	int32_t width;
	int32_t height;
};

/**
 * Axis-aligned two-dimensional rectangle descriptor.
 */
typedef struct NXGI_RECT NXGI_RECT;
struct NXGI_RECT {
	union {
		struct {
			int32_t x1;
			int32_t y1;
		};
		NXGI_POINT p1;
	};

	union {
		struct {
			int32_t x2;
			int32_t y2;
		};
		NXGI_POINT p2;
	};
};

/**
 * Two-dimensional raster bitmap (surface).
 */
typedef struct NXGI_BITMAP NXGI_BITMAP;
struct NXGI_BITMAP {
	/* Bitmap characteristics */
	uint32_t	width;
	uint32_t	stride;
	uint32_t	height;
	uint32_t	bits_per_pixel;
	NXGI_FORMAT	format;

	/** Reference counter. Used by graphics context */
	uint32_t	ref_count;

	/**
	 * Used to differentiate different bitmap implementations
	 */
	NXGI_BITMAP_TAG	tag;

	/**
	 * Pointer to pixel data
	 */
	void		*pBits;

	/**
	 * Destructor
	 */
	HRESULT __nxapi (*destroy)(NXGI_BITMAP **ppBmp);
};

/**
 * Describes font parameters.
 */
typedef struct NXGI_FONT NXGI_FONT;
struct NXGI_FONT {
	char 		name[64];
	uint32_t	size;
	uint32_t	style;
	NXGI_COLOR 	color;
	uint32_t	reserved;
};

/**
 * Think of this as some sort of a canvas.
 */
typedef struct NXGI_GRAPHICS_CONTEXT NXGI_GRAPHICS_CONTEXT;
struct NXGI_GRAPHICS_CONTEXT {
	//settarget
	HRESULT __nxapi (*set_target)(NXGI_GRAPHICS_CONTEXT *this, NXGI_BITMAP *pTarget);

	//gettarget
	HRESULT __nxapi (*get_target)(NXGI_GRAPHICS_CONTEXT *this, NXGI_BITMAP **ppTarget);

	//setcolor
	HRESULT __nxapi (*set_color)(NXGI_GRAPHICS_CONTEXT *this, NXGI_COLOR color);

	//getcolor
	HRESULT __nxapi (*get_color)(NXGI_GRAPHICS_CONTEXT *this, NXGI_COLOR *pColor);

	//setpixel
	HRESULT __nxapi (*set_pixel)(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, NXGI_COLOR color);

	//getpixel
	HRESULT __nxapi (*get_pixel)(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, NXGI_COLOR *color);

	//drawline
	HRESULT __nxapi (*draw_line)(NXGI_GRAPHICS_CONTEXT *this, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);

	//drawrect
	HRESULT __nxapi (*draw_rect)(NXGI_GRAPHICS_CONTEXT *this, NXGI_RECT rect);

	//fillrect
	HRESULT __nxapi (*fill_rect)(NXGI_GRAPHICS_CONTEXT *this, NXGI_RECT rect);

	//bitblt
	HRESULT __nxapi (*bitblt)(NXGI_GRAPHICS_CONTEXT *this, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);

	//stretchblt
	HRESULT __nxapi (*stretchblt)(NXGI_GRAPHICS_CONTEXT *this, NXGI_RECT dst_rect, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);

	//alphablend
	HRESULT __nxapi (*alphablend)(NXGI_GRAPHICS_CONTEXT *this, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);

	//setoffset
	HRESULT __nxapi (*set_offset)(NXGI_GRAPHICS_CONTEXT *this, NXGI_POINT offset);

	//getoffset
	HRESULT __nxapi (*get_offset)(NXGI_GRAPHICS_CONTEXT *this, NXGI_POINT *pOffset);

	//setcliprect
	HRESULT __nxapi (*set_clip_rect)(NXGI_GRAPHICS_CONTEXT *this, NXGI_RECT clip_rect);

	//getcliprect
	HRESULT __nxapi (*get_clip_rect)(NXGI_GRAPHICS_CONTEXT *this, NXGI_RECT *clip_rect);

	//drawtext
	HRESULT __nxapi (*draw_text)(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, char *text);

	//textsize
	HRESULT __nxapi (*text_size)(NXGI_GRAPHICS_CONTEXT *gc, char *text, NXGI_SIZE *size_out);

	//setfont
	HRESULT __nxapi (*set_font)(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT font_params);

	//getfont
	HRESULT __nxapi (*get_font)(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT *font_params_out);

	//destructor
	void __nxapi (*destroy)(NXGI_GRAPHICS_CONTEXT *this);

	/* Fields */
	NXGI_BITMAP	*target;
	NXGI_FONT	font;
	NXGI_POINT	offset;
	NXGI_COLOR	color;
	NXGI_RECT	clip_rect;
};

/**
 * NXGI sub-system context.
 */
typedef struct NXGI_CONTEXT NXGI_CONTEXT;
struct NXGI_CONTEXT {
	/** File stream handle to video driver */
	K_STREAM				*graphics_drv;

	/** Screen surface */
	NXGI_BITMAP				*screen_surf;
};

/*
 * Subsystem initialization and uninitialization.
 */
HRESULT __nxapi nxgi_init(uint32_t xres, uint32_t yres, NXGI_FORMAT fmt);
HRESULT __nxapi nxgi_fini();
NXGI_FORMAT	__nxapi nxgi_internal_format();

/**
 * Constructors
 */
HRESULT __nxapi nxgi_create_bitmap(uint32_t width, uint32_t height, NXGI_FORMAT format, NXGI_BITMAP **ppBmp);
HRESULT __nxapi nxgi_create_graphics_context(NXGI_GRAPHICS_CONTEXT **ppGC);
HRESULT __nxapi nxgi_get_screen(NXGI_BITMAP **ppBmp);

/*
 * Graphics Context methods
 */
HRESULT __nxapi nxgi_set_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP *pTarget);
HRESULT __nxapi nxgi_get_target(NXGI_GRAPHICS_CONTEXT *gc, NXGI_BITMAP **ppTarget);
HRESULT __nxapi nxgi_set_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR color);
HRESULT __nxapi nxgi_get_color(NXGI_GRAPHICS_CONTEXT *gc, NXGI_COLOR *pColor);
HRESULT __nxapi nxgi_draw_line(NXGI_GRAPHICS_CONTEXT *gc, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);
HRESULT __nxapi nxgi_draw_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect);
HRESULT __nxapi nxgi_fill_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT rect);
HRESULT __nxapi nxgi_bitblt(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);
HRESULT __nxapi nxgi_stretchblt(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT dst_rect, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);
HRESULT __nxapi nxgi_alphablend(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT dst_pos, NXGI_BITMAP *pSrcBitmap, NXGI_RECT src_rect);
HRESULT __nxapi nxgi_set_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT offset);
HRESULT __nxapi nxgi_get_offset(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT *pOffset);
HRESULT __nxapi nxgi_set_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT clip_rect);
HRESULT __nxapi nxgi_get_clip_rect(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT *clip_rect);
HRESULT __nxapi nxgi_draw_text(NXGI_GRAPHICS_CONTEXT *gc, NXGI_POINT pos, char *text);
HRESULT __nxapi nxgi_text_size(NXGI_GRAPHICS_CONTEXT *gc, char *text, NXGI_SIZE *size_out);
HRESULT __nxapi nxgi_set_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT font_params);
HRESULT __nxapi nxgi_get_font(NXGI_GRAPHICS_CONTEXT *gc, NXGI_FONT *font_params_out);
void 	__nxapi nxgi_destroy_graphics_context(NXGI_GRAPHICS_CONTEXT *gc);

HRESULT __nxapi nxgi_draw_aligned_text(NXGI_GRAPHICS_CONTEXT *gc, NXGI_RECT box, NXGI_HALIGN halign, NXGI_VALIGN valign, char *text);

/*
 * Bitmap methods
 */
HRESULT __nxapi nxgi_destroy_bitmap(NXGI_BITMAP **ppBmp);

/* Helpers (todo: move to nxgi_geometry.h */
NXGI_RECT RECT(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
NXGI_POINT POINT(int32_t x, int32_t y);
NXGI_SIZE SIZE(int32_t w, int32_t h);
NXGI_COLOR COLOR(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
NXGI_FONT FONT_PARAMS(char *name, uint32_t size, uint32_t style, NXGI_COLOR color);

#endif /* SUBSYSTEMS_NXGI_H_ */
