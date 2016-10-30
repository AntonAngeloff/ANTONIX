/*
 * vga.h
 *
 *  Created on: 10.10.2015 ã.
 *      Author: Admin
 */

#ifndef VGA_H_
#define VGA_H_

#include <stdint.h>
#include <stddef.h>
#include <hal.h>

/*
 * When using the struct in the previous example there is no
 * guarantee that the size of the struct will be exactly
 * 32 bits - the compiler can add some padding between elements
 * for various reasons, for example to speed up element access
 * or due to requirements set by the hardware and/or compiler.
 * When using a struct to represent configuration bytes, it is
 * very important that the compiler does not add any padding,
 * because the struct will eventually be treated as a 32 bit
 * unsigned integer by the hardware. The attribute packed can
 * be used to force GCC to not add any padding:
 */
struct test {
	unsigned char config;
	unsigned short adress;
	unsigned char index;
} __attribute__((packed));

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

/*
 * Function prototypes
 */
void __nxapi vga_init_terminal();
void __nxapi vga_scroll_vert();
void __nxapi vga_set_color(vga_color fg, vga_color bg);
void __nxapi vga_print(char *str);
void __nxapi vga_printf(char *fmt, ...);
void __nxapi vga_clear();

//TODO: printf
//void kprintf(char *mask, varargs)

#endif /* VGA_H_ */
