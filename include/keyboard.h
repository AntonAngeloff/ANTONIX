/*
 * keyboard.h
 *
 *  Created on: 16.11.2015 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_KEYBOARD_H_
#define INCLUDE_KEYBOARD_H_

#include <types.h>

#define VKEY_ESC		0xFF01
#define VKEY_LEFTCTRL	0xFF0D
#define VKEY_BACKSP		0xFF0E
#define VKEY_TAB		0xFF0F
#define VKEY_LEFTSHIFT	0xFF2A
#define VKEY_RIGHTSHIFT	0xFF36
#define VKEY_LEFTALT	0xFF38
#define VKEY_CAPSLOCK	0xFF3A
#define VKEY_NUMLOCK	0xFF45
#define VKEY_SCROLLLOCK	0xFF46

#define VKEY_F1			0xFF3B
#define VKEY_F2			0xFF3C
#define VKEY_F3			0xFF3D
#define VKEY_F4			0xFF3E
#define VKEY_F5			0xFF3F
#define VKEY_F6			0xFF40
#define VKEY_F7			0xFF41
#define VKEY_F8			0xFF42
#define VKEY_F9			0xFF43
#define VKEY_F10		0xFF44
#define VKEY_F11		0xFF57
#define VKEY_F12		0xFF58

/**
 * Define scan code set 1
 */
WORD __keyboard_scs1[128] = {
		'\0',	//0x00
		VKEY_ESC,
		'1',
		'2',
		'3',
		'4',
		'5',
		'6',
		'7',
		'8',
		'9',
		'0',
		'-',
		'=',
		VKEY_BACKSP,
		VKEY_TAB,
		'q',	//0x10
		'w',
		'e',
		'r',
		't',
		'y',
		'u',
		'i',
		'o',
		'p',
		'[',
		']',
		'\n',
		VKEY_LEFTCTRL,
		'a',
		's',
		'd',	//0x20
		'f',
		'g',
		'h',
		'j',
		'k',
		'l',
		';',
		'\'',
		'`',
		VKEY_LEFTSHIFT,
		'\\',
		'z',
		'x',
		'c',
		'v',
		'b',	//0x30
		'n',
		'm',
		',',
		'.',
		'/',
		VKEY_RIGHTSHIFT,
		'*',
		VKEY_LEFTALT,
		' ',
		VKEY_CAPSLOCK,
		VKEY_F1,
		VKEY_F2,
		VKEY_F3,
		VKEY_F4,
		VKEY_F5,
		VKEY_F6,	//0x40
		VKEY_F7,
		VKEY_F8,
		VKEY_F9,
		VKEY_F10,
		VKEY_F11,
		VKEY_F12,
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
};

/**
 * Scan code set #1 / upper case scancode table
 */
WORD __keyboard_scs1_uc[128] = {
		'\0',	//0x00
		VKEY_ESC,
		'!',
		'@',
		'#',
		'$',
		'%',
		'^',
		'&',
		'*',
		'(',
		')',
		'_',
		'+',
		VKEY_BACKSP,
		VKEY_TAB,
		'Q',	//0x10
		'W',
		'E',
		'R',
		'T',
		'Y',
		'U',
		'I',
		'O',
		'P',
		'{',
		'}',
		'\n',
		VKEY_LEFTCTRL,
		'A',
		'S',
		'D',	//0x20
		'F',
		'G',
		'H',
		'J',
		'K',
		'L',
		':',
		'"',
		'~',
		VKEY_LEFTSHIFT,
		'|',
		'Z',
		'X',
		'C',
		'V',
		'B',	//0x30
		'N',
		'M',
		'<',
		'>',
		'?',
		VKEY_RIGHTSHIFT,
		'*',
		VKEY_LEFTALT,
		' ',
		VKEY_CAPSLOCK,
		VKEY_F1,
		VKEY_F2,
		VKEY_F3,
		VKEY_F4,
		VKEY_F5,
		VKEY_F6,	//0x40
		VKEY_F7,
		VKEY_F8,
		VKEY_F9,
		VKEY_F10,
		VKEY_F11,
		VKEY_F12,
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
		'_',
};

BOOL __nxapi __is_ascii_sym(WORD scancode)
{
	return (scancode & 0xFF00) != 0xFF00;
}

#endif /* INCLUDE_KEYBOARD_H_ */
