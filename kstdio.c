/*
 * kstdio.c
 *
 *  Created on: 10.07.2016 ã.
 *      Author: Anton Angelov
 */
#include "vfs.h"
#include <string.h>
#include <vga.h>
#include <kdbg.h>

HRESULT k_fcreate(char *filename, uint32_t perm)
{
	/* Forward call to VFS API */
	return vfs_create(vfs_get_driver(), filename, perm);
}

HRESULT k_fopen(char *filename, uint32_t flags, K_STREAM **out)
{
	/* Forward call to VFS API */
	return vfs_open(vfs_get_driver(), filename, flags, out);
}

HRESULT k_fclose(K_STREAM **str)
{
	/* Forward call to stream function, assigned by the file
	 * system driver. */
	K_STREAM *s = *str;
	return s->close(str);
}

HRESULT k_fread(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	return str->read(str, block_size, out_buf, bytes_read);
}

HRESULT k_fwrite(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	return str->write(str, block_size, in_buf, bytes_written);
}

uint32_t k_ftell(K_STREAM *str)
{
	return str->tell(str);
}

uint32_t k_fseek(K_STREAM *str, int64_t pos, int8_t origin)
{
	return str->seek(str, pos, origin);
}

HRESULT k_opendir(char *dirname, K_DIR_STREAM **out)
{
	return vfs_opendir(vfs_get_driver(), dirname, out);
}

HRESULT k_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info)
{
	//return vfs_readdir(dirstr, filename, info);
	return dirstr->readdir(dirstr, filename, info);
}

HRESULT k_rewinddir(K_DIR_STREAM *dirstr)
{
	//return vfs_rewinddir(dirstr);
	return dirstr->rewinddir(dirstr);
}

HRESULT k_closedir(K_DIR_STREAM **dirstr)
{
	//return vfs_closedir(dirstr);
	K_DIR_STREAM *d = *dirstr;
	return d->closedir(dirstr);
}

HRESULT k_mkdir(char *path, uint32_t mode)
{
	return vfs_mkdir(vfs_get_driver(), path, mode);
}

HRESULT k_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	return s->ioctl(s, code, arg);
}

void __nxapi k_print(char *str)
{
	dbg_print(str);
	vga_print(str);
}

void __nxapi k_printf(char *fmt, ...)
{
	char buffer[1024];

	va_list args;
	va_start(args, fmt);
	vsprintf(buffer, fmt, args);
	va_end(args);

	//Write formatted string with vga_print()
	dbg_print(buffer);
	vga_print(buffer);
}
