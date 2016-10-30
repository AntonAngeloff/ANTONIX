/*
 * string.h
 *
 *  Created on: 10.11.2015 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_STRING_H_
#define INCLUDE_STRING_H_

#include <types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

void* __nxapi memcpy (void *dst, const void *src, size_t count);
void* __nxapi memmove (void *dst, const void *src, size_t count);
void* __nxapi memset (void *dst, int c, size_t size);

int __nxapi vsprintf(PCHAR target, PCHAR fmt, va_list args);
int __nxapi sprintf(PCHAR target, PCHAR fmt, ...);

size_t __nxapi strlen(const char *str);
int32_t __nxapi strcmp(const char *s1, const char *s2);
int32_t __nxapi stricmp(const char *s1, const char *s2);
char __nxapi *strcpy(char *restrict to, char *restrict from);
char __nxapi *strchr(char *str, int character);
char __nxapi *strrchr(char *str, int character);

/* Interprets an integer value in a byte string pointed to by str.
 */
long __nxapi strtol(const char *str, char **str_end, int base);

//TODO: move to ctype.h
int isspace (int c);

/* Additional functions, available only for ANTONIX */
void __nxapi str_explode(char *src, char delim, char ***target, int *cnt);
void __nxapi str_explode_cleanup(char ***target, int cnt);

#endif /* INCLUDE_STRING_H_ */
