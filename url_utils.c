/*
 * url_utils.c
 *
 *  Created on: 7.07.2016 ã.
 *      Author: Admin
 */

#include "url_utils.h"
#include "string.h"
#include "vfs.h"

void url_get_basename(char *path, char *dst)
{
	char *last_del = strrchr(path, VFS_PATH_DELIMITER);
	if (last_del == NULL) {
		strcpy(dst, path);
		return;
	}

	strcpy(dst, last_del + 1);
}

void url_get_dirname(char *path, char *dst)
{
	const char *last_del = strrchr(path, VFS_PATH_DELIMITER);
	if (last_del == NULL) {
		strcpy(dst, ".");
		return;
	}

	uint32_t len = (uint_ptr_t)last_del - (uint_ptr_t)path;

	if (len == 0) {
		strcpy(dst, "/");
		return;
	}

	memcpy(dst, path, len);
	dst[len] = '\0';
}

HRESULT url_decompose(char *url, char delim, char **components, uint32_t *count)
{
	uint32_t	i;
	uint32_t 	len 		= strlen(url);
	uint32_t	comp_count	= 0;
	uint32_t 	comp_id 	= 0;


	/* Count components */
	for (i=0; i<len; i++) {
		if (url[i] == delim) {
			comp_count ++;
		}
	}

	if (comp_count == 0 || components == NULL) {
		goto finally;
	}

	/* Allocate and populate component array */
	char **comp_arr = components;

	for (i=0; i<len; i++) {
		if (url[i] == delim) {
			comp_arr[comp_id++] = &url[i+1];
		}
	}

finally:
	if (count) *count = comp_count;
	return S_OK;
}

void url_extract_comp(char **components, uint32_t count, uint32_t id, char *dst)
{
	if (id >= count) {
		dst[0] = 0;
		return;
	}

	if (id == count-1) {
		strcpy(dst, components[id]);
		return;
	}

	uint32_t offs = components[id+1] - components[id];
	memcpy(dst, components[id], offs-1);

	/* Place NULL terminator */
	dst[offs-1] = 0;
}

HRESULT url_normalize(char *src, char *dst)
{
	uint32_t	comp_cnt;
	uint32_t	i, j;
	char 		*s = dst;
	HRESULT		hr;

	hr = url_decompose(src, VFS_PATH_DELIMITER, NULL, &comp_cnt);
	if (FAILED(hr)) return hr;

	if (comp_cnt == 0) {
		dst[0] = VFS_PATH_DELIMITER;
		dst[1] = '\0';

		return S_OK;
	}

	char *comp_arr[comp_cnt];
	BOOL skip_mask[comp_cnt];

	/* Decompose URL to components */
	hr = url_decompose(src, VFS_PATH_DELIMITER, comp_arr, NULL);
	if (FAILED(hr)) return hr;

	/* Initialize skip mask */
	memset(skip_mask, 0, sizeof(BOOL) * comp_cnt);

	/* Parse each component */
	for (i=0; i<comp_cnt; i++) {
		char name[VFS_MAX_FILENAME_LENGTH];

		/* Extract name of i-th component */
		url_extract_comp(comp_arr, comp_cnt, i, name);

		if (strcmp(name, ".") == 0) {
			skip_mask[i] = TRUE;
			continue;
		}

		if (strcmp(name, "..") == 0) {
			/* We can't have .. at beginning of URL */
			if (i == 0) {
				return E_INVALIDDATA;
			}

			skip_mask[i] = TRUE;
			skip_mask[i-1] = TRUE;

			continue;
		}

		if (strlen(name) == 0) {
			/* Empty name, means we have double path delimiter in the URL.
			 * This has to be treated like "discard all previous components".
			 */
			for (j=0; j<i+1; j++) {
				skip_mask[j] = TRUE;
			}

			continue;
		}
	}

	/* Compose back */
	for (i=0; i<comp_cnt; i++) {
		char name[VFS_MAX_FILENAME_LENGTH];

		/* Skip flagged components */
		if (skip_mask[i]) {
			continue;
		}

		/* Extract name of i-th component */
		url_extract_comp(comp_arr, comp_cnt, i, name);

		/* Place delimiter */
		*(s++) = VFS_PATH_DELIMITER;

		strcpy(s, name);
		s += strlen(name);
	}

	/* If all components are skipped, place a path delimiter */
	if (s == dst) {
		*(s++) = VFS_PATH_DELIMITER;
	}

	/* Place NUL terminator */
	*s = '\0';

	return S_OK;
}

void url_append_trailing_path_delimeter(char *url) {
	uint32_t	len = strlen(url);

	if (len == 0 || url[len-1] != VFS_PATH_DELIMITER) {
		url[len] = VFS_PATH_DELIMITER;
		url[len+1] = 0;
	}
}

void url_exclude_trailing_path_delimeter(char *url)
{
	uint32_t	len = strlen(url);

	if (len > 0 && url[len-1] == VFS_PATH_DELIMITER) {
		url[len-1] = 0;
	}
}
