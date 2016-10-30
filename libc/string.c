/*
 * string.c
 *
 *  Created on: 31.10.2015 ã.
 *      Author: Anton
 */

#include "string.h"
#include "mm.h"
#include "hal.h"

void* memcpy(void *dst, const void *src, size_t count)
{
	if(count == 0) {
		return dst;
	}

	//TODO: We can optimize this routine by transfering DWORDS instead of BYTES.
//	switch (count % 4) {
//		/* Size is granular to 4 bytes */
//		case 0: {
//			count /= 4;
//
//			uint32_t *d = (uint32_t*)dst, *s = (uint32_t*)src;
//			while(count > 0) {
//				*(d++) = *(s++);
//				count--;
//			}
//
//			break;
//		}
//
//		/* Copy byte by byte */
//		default: {
			uint8_t *d = (uint8_t*)dst, *s = (uint8_t*)src;
			while(count > 0) {
				*(d++) = *(s++);
				count--;
			}
//		}
//	}

	return dst;
}

void* memmove(void *dst, const void *src, size_t count)
{
	//TODO: perform check
	return memcpy(dst, src, count);
}

void* memset(void *dst, int c, size_t size)
{
	if(size<=0) {
		return dst;
	}

	uint8_t byte = (uint8_t)c, *d = (uint8_t*)dst;

	while(size>0) {
		*(d++) = byte;
		size--;
	}

	return dst;
}

static PCHAR __sprint_hex(PCHAR target, int src)
{
	char hexmap[16] = "0123456789ABCDEF";
	uint32_t usrc = (uint32_t)src;

	/* Add minus sign if number is negative */
	/*
	if (src<0) {
		*(target++) = '-';
		src *= -1;
	}
	*/

	/* Count the number of digits in the number */
	uint32_t digits = 0, num = (uint32_t)src;
	do {
		digits++;
		num /= 16;
	} while(num>0);

	/* Add 0x prefix */
	*(target++) = '0';
	*(target++) = 'x';
	target += digits;

	do {
		*(--target) = hexmap[usrc % 16];
		usrc /= 16;
	} while(usrc>0);

	return target + digits;
}

static PCHAR __sprint_dec(PCHAR target, int src)
{
	/* Add minus sign if number is negative */
	if (src<0) {
		*(target++) = '-';
		src *= -1;
	}

	/* First we have to reverse the order of the digits */
	int reversed = 0;
	int cnt = 0;

	while(src>0) {
		reversed *= 10;
		reversed += src % 10;
		src /= 10;

		cnt++;
	}

	do {
		*(target++) = '0' + (reversed % 10);
		reversed /= 10;
	} while(--cnt > 0);

	return target;
}

static PCHAR __sprint_str(PCHAR target, PCHAR src, BOOL inc_null)
{
	/* A very good idea is to handle NULL strings. Useful for debugging */
	if (src == NULL) {
		*target++ = 'N';
		*target++ = 'U';
		*target++ = 'L';
		*target++ = 'L';
	}

	while(*src != '\0') {
		*(target++) = *(src++);
	}

	if(inc_null) {
		*target = '\0';
	}

	return target;
}

/**
 * Function: vsprintf()
 * Composes a string with the same text that would be printed if format was
 * used on printf, but using the elements in the variable argument list identified
 * by arg instead of additional function arguments and storing the resulting
 * content as a C string in the buffer pointed by target.
 *
 * Parameters:
 * @target
 *    Pointer to a buffer where the resulting C-string is stored.
 *    The buffer should be large enough to contain the resulting string.
 * @fmt
 *    C string that contains a format string that follows the same specifications as format in printf (see printf for details).
 * @args
 *    A value identifying a variable arguments list initialized with va_start.
 *    va_list is a special type defined in <cstdarg>.
 *
 * Return value:
 * 	  On success, the total number of characters written is returned.
 *    On failure, a negative number is returned.
 */
int __nxapi vsprintf(PCHAR target, PCHAR fmt, va_list args)
{
	PCHAR pc = fmt;
	PCHAR dst = target;

	while (*pc != '\0') {
		if(*pc == '%') {
			//this might be a place holder
			if(pc != fmt && *(pc-1) == '\\') {
				//the % symbol is actually escaped, so
				//TODO: implement
			}


			//assume this is place holder
			BOOL ph_handled = FALSE;
			PCHAR str;
			int number;

			//move to next char to read identifier
			pc++;
			char id = *pc;

			switch (id) {
			case 'd':
				//Extract decimal parameter
				number = va_arg(args, int);

				//Write decimal to string
				dst = __sprint_dec(dst, number);

				ph_handled = TRUE;
				break;
			case 'x':
				//Extract hex parameter
				number = va_arg(args, int);

				//Write decimal to string
				dst = __sprint_hex(dst, number);

				ph_handled = TRUE;
				break;
			case 's':
				//Extract string pointer
				str = va_arg(args, char*);

				//Write string to target
				dst = __sprint_str(dst, str, FALSE);

				ph_handled = TRUE;
				break;
			}

			if(ph_handled) {
				pc++;
				continue;
			}
		}

		/* Write to target string */
		*(dst++) = *pc;

		/* Go to next char */
		pc++;
	}

	/* Write null terminator */
	*dst = '\0';

	/* Return number of written chars */
	return dst - target;
}

int __nxapi sprintf(PCHAR target, PCHAR fmt, ...)
{
	int retcode;

	va_list args;
	va_start (args, fmt);

	retcode = vsprintf (target, fmt, args);

	//perror (target);
	va_end (args);

	return retcode;
}

size_t __nxapi strlen(const char *str)
{
	int32_t len = 0;

	while(*(str++) != '\0') {
		len ++;
	}

	return len;
}

int32_t __nxapi strcmp(const char *s1, const char *s2)
{
	while (1) {
		if (*s1 > *s2) {
			return 1;
		}else if (*s1 < *s2) {
			return -1;
		}else if (*s1 == '\0'){
			//this logically implies that *s2 is also == to '\0'
			return 0;
		}else {
			s1++;
			s2++;
		}
	}
}

char __nxapi toupper(const char c)
{
	return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

int32_t __nxapi stricmp(const char *s1, const char *s2)
{
	while (1) {
		if (toupper(*s1) > toupper(*s2)) {
			return 1;
		}else if (toupper(*s1) < toupper(*s2)) {
			return -1;
		}else if (*s1 == '\0'){
			//this logically implies that *s2 is also == to '\0'
			return 0;
		}else {
			s1++;
			s2++;
		}
	}
}

char __nxapi *strcpy(char *restrict to, char *restrict from)
{
	char *dst = to;

	while (*from) {
		*(to++) = *(from++);
	}
	*to = '\0';

	return dst;
}

char __nxapi *strchr(char *str, int character)
{
	while (*str) {
		if (*str == (uint8_t)character) {
			/* Found */
			return str;
		}
		str++;
	}

	if (character == 0) {
		return str;
	}

	return NULL;
}

char __nxapi *strrchr(char *str, int character)
{
	char *result = NULL;

	while (*str) {
		if (*str == (uint8_t)character) {
			/* Found */
			result = str;
		}

		str++;
	}

	if (character == 0) {
		return str;
	}

	return result;
}

void __nxapi str_explode(char *src, char delim, char ***target, int *cnt)
{
	int target_cnt = 1;
	char *s = src;

	/* First count the substrings to know how much memory to allocate */
	while (*s) {
		if (*(s++) == delim) {
			target_cnt++;
		}
	}

	/* Alloc */
	char **target_arr = kmalloc(target_cnt * sizeof(void*));

	/* Start searching and allocating memory for resulted substrings */
	char *d = src;

	for (int i=0; i<target_cnt; i++) {
		char *d2 = strchr(d, delim);
		uint32_t k;

		if (d2 == NULL) {
			k = strlen(d);
		} else {
			k = (uint32_t)(d2 - d);
		}

		/* Allocate and copy substring */
		target_arr[i] = kmalloc(k + 1);

		if (k>0) {
			memcpy(target_arr[i], d, k);
		}
		target_arr[i][k] = '\0';

		/* Jump to first character after delimiter (for last iteration, this will be NULL+1 */
		d = d2 + 1;
	}

	/* Populate output arguments */
	*cnt = target_cnt;
	*target = target_arr;
}

void __nxapi str_explode_cleanup(char ***target, int cnt)
{
	char **arr = *target;

	for (int i=0; i<cnt; i++) {
		kfree(arr[i]);
	}

	kfree(arr);
	*target = NULL;
}

long __nxapi strtol(const char *str, char **str_end, int base)
{
	const char *c = str;

	/* Check if character is space. If it is - skip it. */
	while (isspace(*c)) {
		c++;
	}

	uint8_t negative = *c == '-' ? TRUE : FALSE;

	if (*c == '-' || *c == '+') {
		c++;
	}

	/* Check if it is octal number */
	if (*c == '0' && *(c+1) == '0') {
		HalKernelPanic("strtol(): octal support not implemented.");
	}

	/* Check if hexadecimal */
	if (*c == '0' && ((*(c+1) == 'x') || *(c+1) == 'X')) {
		HalKernelPanic("strtol(): hex support not implemented.");
	}

	if (base == 0) {
		base = 10;
	}

	if (base != 10) {
		HalKernelPanic("strtol(): only base 10 is supported.");
	}

	long k = 0;
	while (*c != '\0') {
		k = (k<<3) + (k<<1) + (*c) - '0';
	}

	if (str_end != NULL) *str_end = (char *)c;
	return negative ? -k : k;

//fail:
//	if (str_end != NULL) *str_end = str;
//	return 0;
}

int isspace (int c)
{
	switch (c) {
	case ' ':
	case '\t':
	case '\n':
	case '\v':
	case '\f':
	case '\r':
		return TRUE;

	default:
		return FALSE;
	}
}
