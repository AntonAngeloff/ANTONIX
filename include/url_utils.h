/*
 * url_utils.h
 *
 *  Created on: 7.07.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_URL_UTILS_H_
#define INCLUDE_URL_UTILS_H_

#include <types.h>

void url_get_basename(char *path, char *dst);
void url_get_dirname(char *path, char *dst);

/**
 * Renders a new URL where "." and ".." components are evaluated
 * to an absolute URL.
 */
HRESULT url_normalize(char *src, char *dst);

/**
 * Decomposes an URL to an array of components (node names).
 */
HRESULT url_decompose(char *url, char delim, char **components, uint32_t *count);

/**
 * Copies name of URL component to destination string.
 */
void url_extract_comp(char **components, uint32_t count, uint32_t id, char *dst);

void url_append_trailing_path_delimeter(char *url);
void url_exclude_trailing_path_delimeter(char *url);

#endif /* INCLUDE_URL_UTILS_H_ */
