/*
 * kstdio.h
 *
 *  Created on: 10.07.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_KSTDIO_H_
#define INCLUDE_KSTDIO_H_

#include "kstream.h"
#include "stdio.h"

/* File routines */
HRESULT k_fcreate(char *filename, uint32_t perm);
HRESULT k_fopen(char *filename, uint32_t flags, K_STREAM **out);
HRESULT k_fclose(K_STREAM **str);
HRESULT k_fread(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);
HRESULT k_fwrite(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
uint32_t k_ftell(K_STREAM *str);
uint32_t k_fseek(K_STREAM *str, int64_t pos, int8_t origin);
HRESULT k_ioctl(K_STREAM *s, uint32_t code, void *arg);

HRESULT k_opendir(char *dirname, K_DIR_STREAM **out);
HRESULT k_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info);
HRESULT k_rewinddir(K_DIR_STREAM *dirstr);
HRESULT k_closedir(K_DIR_STREAM **dirstr);
HRESULT k_mkdir(char *path, uint32_t mode);

void __nxapi k_print(char *str);
void __nxapi k_printf(char *fmt, ...);

#endif /* INCLUDE_KSTDIO_H_ */
