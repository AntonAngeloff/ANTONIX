/*
 * vga.c
 *
 *	This file contains a driver for text-mode video output.
 *	Supports only 1 video mode - 80x25.
 *
 *  Created on: 10.10.2015 ã.
 *      Author: Anton Angelov (antonn.angelov@gmail.com)
 */

#include "vga.h"
#include "string.h"
#include "syncobjs.h"

/* Terminal internal data */
uint16_t 		*term_buff;
size_t 			term_pointer;
int32_t 		term_cursor_update_cntr;

static K_MUTEX	vga_lock;

uint16_t		term_buff_sw[80*25];

vga_color term_fg, term_bg;


void __weak vga_set_cursor_position(DWORD dwPos)
{
	if(term_cursor_update_cntr) {
		/* Not yet */
		return;
	}

	// Tell the VGA board we are setting the high cursor byte.
	WRITE_PORT_UCHAR(0x3D4, 14);

	// Send the high cursor byte.
	WRITE_PORT_UCHAR(0x3D5, dwPos >> 8);

	// Tell the VGA board we are setting the low cursor byte.
	WRITE_PORT_UCHAR(0x3D4, 15);

	// Send the low cursor byte.
	WRITE_PORT_UCHAR(0x3D5, dwPos);
}

/**
 * Increment terminal's cursor update flag
 */
static void vga_cursor_begin_update()
{
	term_cursor_update_cntr++;
}

/**
 * Decrement terminal's cursor update flag. When dropped to 0,
 * the function should update cursor's position.
 */
static void vga_cursor_end_update()
{
	term_cursor_update_cntr--;

	if(term_cursor_update_cntr == 0) {
		vga_set_cursor_position(term_pointer);
		return;
	}

	if(term_cursor_update_cntr < 0) {
		HalKernelPanic("VGA: Cursor update counter dropped below 0.");
	}
}

void vga_clear()
{
	//Reset all screen characters to whitespace
	for(size_t j=0; j<25; j++) {
		for(size_t i=0; i<80; i++) {
			uint32_t id = j * 80 + i;

			term_buff_sw[id] = vga_make_entry(' ', vga_make_color(term_fg, term_bg));
			term_buff[id] = term_buff_sw[id];
		}
	}

	term_pointer = 0;
	vga_set_cursor_position(0);
}

void vga_init_terminal()
{
	term_buff = (uint16_t*)0xC00B8000;
	term_pointer = 0;
	term_cursor_update_cntr = 0;

	//Initialize default colors
	term_fg = COLOR_LIGHT_GREY;
	term_bg = COLOR_BLACK;

//	mutex_create(&vga_lock);

	//Clear the screen
	vga_clear();
}

void vga_scroll_vert()
{
	int y;

	//Copy lines
	for(y=0; y<25-1; y++) {
		//this line = next line
		memmove(&term_buff[y*80], &term_buff_sw[(y+1)*80], 80*sizeof(uint16_t));
		memmove(&term_buff_sw[y*80], &term_buff_sw[(y+1)*80], 80*sizeof(uint16_t));
	}

	//Fill last line with whitespaces
	int32_t i;
	for(i=0; i<80; i++) {
		uint32_t id = 25*80 - i - 1;

		term_buff_sw[id] = vga_make_entry(' ', vga_make_color(term_fg, term_bg));
		term_buff[id] = term_buff_sw[id];
	}

	//move pointer
	term_pointer -= 80;
	vga_set_cursor_position(term_pointer);

	//clamp
	if(term_pointer / 80 >= 25) {
		HalKernelPanic("VGA: vga_scroll_vert(): failed.");
		//term_pointer = 24;
	}
}

static void vga_print_char(char c)
{
	if(term_pointer >= 80*25) {
		vga_scroll_vert(); //scroll one line upwards
		//kassert(term_pointer<80*25);
	}


	term_buff_sw[term_pointer] = vga_make_entry(c, vga_make_color(term_fg, term_bg));
	term_buff[term_pointer] = vga_make_entry(c, vga_make_color(term_fg, term_bg));

	term_pointer++;
	vga_set_cursor_position(term_pointer);
}

static void vga_new_line()
{
	//Move cursor to beginning of next line
	term_pointer = ((term_pointer + 80) / 80) * 80;

	//Scroll screen if we reached bottom
	if(term_pointer >= 80*25) {
		vga_scroll_vert(); //scroll one line upwards
	}else {
		vga_set_cursor_position(term_pointer);
	}
}

static void vga_apply_tab()
{
	//Force cursor to reside on 8-aligned position
	int cnt = term_pointer % 8;

	vga_cursor_begin_update();

	while(cnt) {
		vga_print_char(' ');
		cnt--;
	}

	vga_cursor_end_update();
}

static void vga_apply_carriage_return()
{
	term_pointer = term_pointer - (term_pointer % 80);
	vga_set_cursor_position(term_pointer);
}

void vga_set_color(vga_color fg, vga_color bg)
{
	term_fg = fg;
	term_bg = bg;
}

//todo: documentations
void __nxapi vga_print(char *str)
{
	K_SPINLOCK sl;
	spinlock_create(&sl);
	uint32_t intf = spinlock_acquire(&sl);

	/* This function prevents the cursor position from being updated for
	 * every char, but instead updates it at vga_cursor_begin_update()
	 */
	vga_cursor_begin_update();

//	mutex_lock(&vga_lock);

	while (*str != '\0') {
		/*
		 * Check current character, and parse it if is a special one
		 */
		switch (*str) {
		case '\n':
			vga_new_line();
			break;
		case '\t':
			vga_apply_tab();
			break;
		case '\r':
			vga_apply_carriage_return();
			break;
		default:
			vga_print_char(*str);
		}

		str++;
	}

//	mutex_unlock(&vga_lock);

	vga_cursor_end_update();

	spinlock_release(&sl, intf);
}

void __nxapi vga_printf(char *fmt, ...)
{
	char buffer[1024];

	va_list args;
	va_start(args, fmt);
	vsprintf(buffer, fmt, args);
	va_end(args);

	//Write formatted string with vga_print()
	vga_print(buffer);
}
//TODO: sprintf()
