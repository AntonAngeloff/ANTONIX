/*
 * stdlib.h
 *
 *  Created on: 3.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_STDLIB_H_
#define INCLUDE_STDLIB_H_

#include <stddef.h>

int abs(int n);

/*
 * Memory management.
 */
void* malloc(size_t size);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);

#endif /* INCLUDE_STDLIB_H_ */
