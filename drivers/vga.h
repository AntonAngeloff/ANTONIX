/*
 * vga.h
 *
 *  Created on: 12.08.2016 ã.
 *      Author: Admin
 */

#ifndef DRIVERS_VGA_H_
#define DRIVERS_VGA_H_

#include <types.h>
#include <devices.h>
#include <syncobjs.h>

/* Define IOCTL command ids */
#define		IOCTL_VGA_CLEAR_SCREEN		(IOCTL_PECULIAR + 0x00)
#define		IOCTL_VGA_GET_STATE			(IOCTL_PECULIAR + 0x01)
#define		IOCTL_VGA_SET_CURSOR_POS	(IOCTL_PECULIAR + 0x02)
#define		IOCTL_VGA_UPDATE_RECT		(IOCTL_PECULIAR + 0x03)

/* Hardware text mode color constants. */
typedef enum vga_color {
	COLOR_BLACK = 0,
	COLOR_BLUE = 1,
	COLOR_GREEN = 2,
	COLOR_CYAN = 3,
	COLOR_RED = 4,
	COLOR_MAGENTA = 5,
	COLOR_BROWN = 6,
	COLOR_LIGHT_GREY = 7,
	COLOR_DARK_GREY = 8,
	COLOR_LIGHT_BLUE = 9,
	COLOR_LIGHT_GREEN = 10,
	COLOR_LIGHT_CYAN = 11,
	COLOR_LIGHT_RED = 12,
	COLOR_LIGHT_MAGENTA = 13,
	COLOR_LIGHT_BROWN = 14,
	COLOR_WHITE = 15,
} vga_color;

static inline uint8_t vga_make_color(vga_color fg, vga_color bg)
{
	return (bg) | (fg & 0xF);
}

static inline uint16_t vga_make_entry(char c, uint8_t color)
{
	return (uint16_t)c | ((uint16_t)color << 8);
}

#define CON_DRV_SIGNATURE	0xAABBCCDD

typedef struct K_VGA_DRIVER_CONTEXT K_VGA_DRIVER_CONTEXT;
struct K_VGA_DRIVER_CONTEXT {
	uint32_t 	magic_value;
	K_MUTEX		lock;

	/* Screen internal data */
	uint16_t 		*vga_buff;
	uint16_t 		*vga_buff_sw;
	uint32_t		vga_buff_size;
	size_t 			vga_pointer;
	int32_t 		vga_cursor_update_cntr;
	uint32_t		vga_initiailized;

	uint8_t			color_fg;
	uint8_t			color_bg;

	uint32_t		width;
	uint32_t		height;
};

typedef struct {
	uint32_t 	magic_value;
	K_MUTEX		lock;
} K_VGA_STREAM_CONTEXT;

/** Carrier struct for IOCTL_VGA_GET_STATE IOCTL invoke. */
typedef struct VGA_DEVICE_STATE VGA_DEVICE_STATE;
struct VGA_DEVICE_STATE {
	uint32_t	width;
	uint32_t	height;
};

typedef struct VGA_CURSOR_POS VGA_CURSOR_POS;
struct VGA_CURSOR_POS {
	uint32_t	x;
	uint32_t	y;
};

typedef struct VGA_UPDATE_RECT_DESC VGA_UPDATE_RECT_DESC;
struct VGA_UPDATE_RECT_DESC {
	uint32_t	x;
	uint32_t	y;
	uint32_t	w;
	uint32_t	h;
	uint8_t		*char_buffer;
	uint8_t		*col_buffer;
};

/** Mounts and initializes the VGA device driver */
HRESULT __nxapi vga_initialize();

/** Unmounts VGA device driver */
HRESULT __nxapi vga_uninitialize();

#endif /* DRIVERS_VGA_H_ */
