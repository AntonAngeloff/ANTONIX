/*
 * iso9660.c
 *
 *  Created on: 12.11.2016 ã.
 *      Author: Anton Angelov
 */
#include "iso9660.h"
#include <string.h>
#include <kstdio.h>
#include <hal.h>
#include <mm.h>

/*
 * Prototypes
 */
static HRESULT iso9660_parse_superblock(ISO9660_DRV_CONTEXT *ctx);
static HRESULT iso9660_validate_vd_header(char *hdr);
static HRESULT iso9660_extract_filename(ISO9660_DIR_ENTRY *entry, char *target);

static HRESULT iso9660_find_subentry(K_FS_DRIVER *drv, ISO9660_DIR_ENTRY *dir, char *name, uint32_t flagmask, ISO9660_DIR_ENTRY *dst);
static HRESULT iso9660_parse_url(K_FS_DRIVER *drv, char *url, ISO9660_DIR_ENTRY *out_entry);
static HRESULT iso9660_file_read_block(K_FS_DRIVER *drv, ISO9660_DIR_ENTRY *entry, uint32_t start, uint32_t count, void *dst);
static HRESULT iso9660_file_read_buffer(K_STREAM *s, ISO9660_DIR_ENTRY *entry, uint32_t start_addr, uint32_t size, void *dst);
static HRESULT iso9660_cache_blocks(K_STREAM *s, ISO9660_DIR_ENTRY *entry, uint32_t start_addr, uint32_t block_cnt);
static uint32_t iso9660_get_subentry_count(void *content, uint32_t size);

/* Functions exported through the driver interface */
static HRESULT iso9660_init(K_FS_DRIVER *drv, char *storage_drv);
static HRESULT iso9660_fini(K_FS_DRIVER *drv);
static HRESULT iso9660_opendir(K_FS_DRIVER *drv, char *dirname, K_DIR_STREAM **out);
static HRESULT iso9660_mkdir(K_FS_DRIVER *drv, char *path, uint32_t mode);
static HRESULT iso9660_create(K_FS_DRIVER *drv, char *filename, uint32_t perm);
static HRESULT iso9660_open(K_FS_DRIVER *drv, char *filename, uint32_t flags, K_STREAM **out);
static HRESULT iso9660_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info);
static HRESULT iso9660_rewinddir(K_DIR_STREAM *dirstr);
static HRESULT iso9660_closedir(K_DIR_STREAM **dirstr);
static HRESULT iso9660_file_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);
static HRESULT iso9660_file_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
static HRESULT iso9660_file_ioctl(K_STREAM *s, uint32_t code, void *arg);
static uint32_t iso9660_file_seek(K_STREAM *s, int64_t pos, int8_t origin);
static uint32_t iso9660_file_tell(K_STREAM *s);
static HRESULT iso9660_file_close(K_STREAM **str);

#define FLAG_HIDDEN      0x01
#define FLAG_DIRECTORY   0x02
#define FLAG_ASSOCIATED  0x04
#define FLAG_EXTENDED    0x08
#define FLAG_PERMISSIONS 0x10
#define FLAG_CONTINUES   0x80

/*
 * Implementations
 */
static HRESULT
iso9660_init(K_FS_DRIVER *drv, char *storage_drv)
{
	ISO9660_DRV_CONTEXT	*c;
	HRESULT				hr;

	UNUSED_ARG(iso9660_get_subentry_count);

	if (drv->priv_data != NULL) {
		/* Driver already initialized, or uninitialized memory */
		return E_FAIL;
	}

	/* Create driver context */
	if (!(drv->priv_data = kcalloc(sizeof(ISO9660_DRV_CONTEXT)))) {
		return E_OUTOFMEM;
	}

	c = drv->priv_data;

	hr = k_fopen(storage_drv, FILE_OPEN_READ, &c->storage_drv);
	if (FAILED(hr)) return hr;

	hr = storage_get_block_size(c->storage_drv, &c->block_size);
	if (FAILED(hr)) return hr;

	if (!(c->pvd = kmalloc(c->block_size))) {
		return E_OUTOFMEM;
	}

	hr = iso9660_parse_superblock(c);
	if (FAILED(hr)) return hr;

	//todo

	/* Assign FS ops */
	drv->create 	= iso9660_create;
	drv->open 		= iso9660_open;
	drv->mkdir 		= iso9660_mkdir;
	drv->opendir 	= iso9660_opendir;
	drv->finalize 	= iso9660_fini;

	return S_OK;
}

static HRESULT
iso9660_fini(K_FS_DRIVER *drv)
{
	ISO9660_DRV_CONTEXT	*ctx;

	if (drv->priv_data == NULL) {
		return E_FAIL;
	}

	ctx = drv->priv_data;

	/* Close storage driver handle */
	if (ctx->storage_drv != NULL) {
		k_fclose(&ctx->storage_drv);
	}

	kfree(ctx->pvd);

	/* Free driver context */
	kfree(drv->priv_data);

	return S_OK;
}

static HRESULT
iso9660_parse_superblock(ISO9660_DRV_CONTEXT *ctx)
{
	uint32_t	i = ISO_VD_START_SECTOR;
	HRESULT 	hr;

	do {
		/* Read primary volume descriptor from storage */
		hr = storage_read_blocks(ctx->storage_drv, i, 1, ctx->pvd);
		if (FAILED(hr)) return hr;

		/* If it is not valid volume descriptor, then fail */
		hr = iso9660_validate_vd_header(&ctx->pvd->header.magic[0]);
		if (FAILED(hr)) return E_FAIL;

		/* End of volume descriptors */
		if (ctx->pvd->header.type == ISO_VD_SET_TERMINATOR) {
			return E_FAIL;
		}

		/* If it's not primary volume desc, try next one */
		if (ctx->pvd->header.type != ISO_VD_PRIMARY) {
			i++;
			continue;
		}
	} while (FALSE);

	return S_OK;
}

static HRESULT
iso9660_validate_vd_header(char *hdr)
{
	char *h = {"CD001"};
	return memcmp(hdr, h, 5) == 0 ? S_OK : E_FAIL;
}

static HRESULT
iso9660_find_subentry(K_FS_DRIVER *drv, ISO9660_DIR_ENTRY *dir, char *name, uint32_t flagmask, ISO9660_DIR_ENTRY *dst)
{
	ISO9660_DRV_CONTEXT *c = drv->priv_data;
	uint8_t				*content;
	uint32_t			content_size;
	uint32_t			offset = 0;
	ISO9660_DIR_ENTRY	*entry;
	HRESULT				hr = S_OK;

	/* If dir is 'NULL' we expected to search in root directory */
	if (dir == NULL) {
		dir = (ISO9660_DIR_ENTRY*)c->pvd->root_dir_entry;
	}

	/* Make sure this entry is a directory */
	if ((dir->flags & FLAG_DIRECTORY) == 0) {
		return E_INVALIDARG;
	}

	/* Round up to size multiple of sector size */
	content_size = dir->extent_size.lsb + (ISO9660_SECTOR_SIZE - dir->extent_size.lsb % ISO9660_SECTOR_SIZE);

	/* Allocate buffer to store directory's content */
	if (!(content = kmalloc(content_size))) {
		return E_OUTOFMEM;
	}

	/* Read directory content from storage */
	hr = storage_read_blocks(c->storage_drv, dir->extent_start.lsb, content_size / ISO9660_SECTOR_SIZE, content);
	if (FAILED(hr)) return hr;

	while (offset != content_size) {
		if (offset > content_size) {
			/* Totally unexpected */
			HalKernelPanic("iso9660_find_subentry(): unexpected case of reaching out of content buffer.");
		}

		/* ECMA-119 Directory Record format can hold composed names of up to 222 characters. */
		char filename[223];

		/* Read single subentry */
		entry = (ISO9660_DIR_ENTRY*)(content + offset);

		/* Skip hidden files */
		if (entry->flags & FLAG_HIDDEN) {
			goto next;
		}

		/* Extract current subentry name to local char string */
		hr = iso9660_extract_filename(entry, filename);
		if (FAILED(hr)) return hr;

		/* Compare names */
		if (strcmp(name, filename) == 0) {
			/* Enforce flag mask */
			if (flagmask != 0 && (entry->flags & flagmask) != flagmask) {
				/* Flags don't match */
				hr = E_NOTFOUND;
				goto finally;
			}

			/* Found */
			memcpy(dst, entry, sizeof(ISO9660_DIR_ENTRY));

			hr =  S_OK;
			goto finally;
		}

next:
		/* Next entry */
		offset += entry->length;

		/* Handle gaps */
		if (content[offset] == 0) {
			offset += ISO9660_SECTOR_SIZE - offset % ISO9660_SECTOR_SIZE;
		}
	}

	/* If we have reached here, then the requested entry is not found */
	hr = E_NOTFOUND;

finally:
	kfree(content);
	return hr;
}

static HRESULT
iso9660_extract_filename(ISO9660_DIR_ENTRY *entry, char *target)
{
	char *p = target;

	memcpy(target, entry->name, entry->name_length);

	/* Discard string content after '?' symbol */
	while (*p) {
		if (*p == ';') {
			*p = '\0';
			return S_OK;
		}

		p++;
	}

	target[entry->name_length] = '\0';
	return S_OK;
}

/*
 * This is a read-only driver, so creation of files is not allowed.
 */
static HRESULT
iso9660_create(K_FS_DRIVER *drv, char *filename, uint32_t perm)
{
	UNUSED_ARG(drv);
	UNUSED_ARG(filename);
	UNUSED_ARG(perm);

	return E_FAIL;
}

static HRESULT
iso9660_mkdir(K_FS_DRIVER *drv, char *path, uint32_t mode)
{
	UNUSED_ARG(drv);
	UNUSED_ARG(path);
	UNUSED_ARG(mode);

	return E_FAIL;

}

static HRESULT
iso9660_open(K_FS_DRIVER *drv, char *filename, uint32_t flags, K_STREAM **out)
{
	ISO9660_DIR_ENTRY	entry;
	ISO9660_STR_CONTEXT *ctx;
	K_STREAM			*stream;
	HRESULT				hr;

	/* Write operations are not supported */
	if ((flags & FILE_OPEN_WRITE) != 0) {
		return E_ACCESSDENIED;
	}

	/* Find target directory and read it's DIR ENTRY */
	hr = iso9660_parse_url(drv, filename, &entry);
	if (FAILED(hr)) return hr;

	/* Assert item is a file */
	if ((entry.flags & FLAG_DIRECTORY) != 0) {
		return E_FAIL;
	}

	/* Create new file stream */
	if (!(stream = kcalloc(sizeof(K_STREAM)))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	if (!(ctx = kcalloc(sizeof(ISO9660_STR_CONTEXT)))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	/* Setup stream. No need to set cache params since they
	 * are initially zeroed. */
	stream->mode = flags;
	stream->fs_driver = drv;
	stream->priv_data = ctx;

	memcpy(&ctx->entry, &entry, sizeof(ISO9660_DIR_ENTRY));

	/* Create mutex */
	mutex_create(&stream->lock);

	/* Assign file ops */
	stream->read 	= iso9660_file_read;
	stream->write 	= iso9660_file_write;
	stream->ioctl	= iso9660_file_ioctl;
	stream->seek	= iso9660_file_seek;
	stream->tell	= iso9660_file_tell;
	stream->close	= iso9660_file_close;

	/* Success */
	*out = stream;
	return S_OK;

fail:
	if (stream) kfree(stream);
	if (ctx) kfree(ctx);

	return hr;
}

static HRESULT
iso9660_parse_url(K_FS_DRIVER *drv, char *url, ISO9660_DIR_ENTRY *out_entry)
{
	ISO9660_DRV_CONTEXT	*ctx = drv->priv_data;
	uint32_t 			comp_count = 0;
	uint32_t 			url_len = strlen(url);
	ISO9660_DIR_ENTRY	curr_entry;
	uint32_t 			i, len, flagmask;
	HRESULT				hr;
	char				comp_name[256];

	if (strlen(url) == 0 ? TRUE : FALSE) {
		/* Caller is searching for the root directory */
		*out_entry = *(ISO9660_DIR_ENTRY*)ctx->pvd->root_dir_entry;
		return S_OK;
	}

	/* Count number of path delimiters */
	for (i=0; i<url_len; i++) {
		if (url[i] == VFS_PATH_DELIMITER) {
			comp_count++;
		}
	}

	/* Create array to hold the names of each component */
	uint32_t comp_id = 0;
	char *components[comp_count];

	/* Split URL to components */
	for (i=0; i<url_len; i++) {
		if (url[i] == VFS_PATH_DELIMITER) {
			components[comp_id++] = &url[i+1];
		}
	}

	/* Iterate components */
	for (i=0; i<comp_count; i++) {
		/* Find length of the component name */
		char *s = strchr(components[i], VFS_PATH_DELIMITER);
		len	= !s ? strlen(components[i]) : (uint32_t)(s - components[i]);

		/* Copy component name to local variable */
		memcpy(comp_name, components[i], len);
		comp_name[len] = '\0';

		/* If this is last component it could be either file or a folder,
		 * but otherwise it is mandatory to be a directory.
		 */
		flagmask = (comp_count - i == 1) ? 0x00 : FLAG_DIRECTORY;

		/* Search */
		if (i == 0) {
			hr = iso9660_find_subentry(drv, NULL, comp_name, flagmask, &curr_entry);
		} else {
			hr = iso9660_find_subentry(drv, &curr_entry, comp_name, flagmask, &curr_entry);
		}
		if (FAILED(hr)) goto finally;
	}

	/* Found */
	*out_entry = curr_entry;
	hr = S_OK;

finally:
	return hr;
}

/*
 * Reads `block_size` bytes from a file stream.
 */
static HRESULT
iso9660_file_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	ISO9660_STR_CONTEXT *strctx = str->priv_data;
	ISO9660_DIR_ENTRY	*entry = &strctx->entry;
	uint32_t			effective_size = block_size;
	HRESULT				hr;

	/* Validate input parameters */
	if (block_size == 0) {
		return E_INVALIDARG;
	}

	if (!out_buf) {
		return E_POINTER;
	}

	/* Lock stream mutex */
	mutex_lock(&str->lock);

	if (block_size + str->pos >= entry->extent_size.lsb) {
		effective_size = entry->extent_size.lsb - str->pos;
	}

	if (effective_size == 0) {
		/* End of file */
		if (bytes_read) *bytes_read = 0;
		hr = E_ENDOFSTR;

		goto finally;
	}

	/* Read content */
	hr = iso9660_file_read_buffer(str, entry, str->pos, effective_size, out_buf);
	if (FAILED(hr)) goto finally;

	/* Move position */
	str->pos += effective_size;

	/* Set output parameter */
	if (bytes_read) *bytes_read = effective_size;

finally:
	mutex_unlock(&str->lock);
	return hr;
}

/*
 * Reads sector-aligned file content.
 *
 * @param drv	File system driver
 * @param entry Directory entry of the file
 * @param start Source address of which to read in bytes (have to be sector-aligned).
 * @param count Number of bytes to read (have to be sector-aligned).
 * @param dst	Destination buffer which will receive the requested file data.
 */
static HRESULT
iso9660_file_read_block(K_FS_DRIVER *drv, ISO9660_DIR_ENTRY *entry, uint32_t start, uint32_t count, void *dst)
{
	ISO9660_DRV_CONTEXT *c = drv->priv_data;
	uint32_t			s_start = start / ISO9660_SECTOR_SIZE;
	uint32_t			s_count = count / ISO9660_SECTOR_SIZE;
	HRESULT				hr;

	/* Validate input args */
	if (start % ISO9660_SECTOR_SIZE != 0 || count % ISO9660_SECTOR_SIZE != 0) {
		return E_INVALIDARG;
	}

	/* File/directory extents are guaranteed to occupy sequential sectors */
	hr = storage_read_blocks(c->storage_drv, entry->extent_start.lsb + s_start, s_count, dst);
	return hr;
}

/**
 * Reads non sector aligned data from file.
 */
static HRESULT
iso9660_file_read_buffer(K_STREAM *s, ISO9660_DIR_ENTRY *entry, uint32_t start_addr, uint32_t size, void *dst)
{
	ISO9660_STR_CONTEXT *sc 		= s->priv_data;
	K_FS_DRIVER			*drv 		= s->fs_driver;
	uint32_t			write_index	= 0;
	uint32_t			read_index  = start_addr;
	uint32_t			block_size	= ISO9660_SECTOR_SIZE;
	uint8_t				*dst_buff 	= dst;
	uint32_t			block_count;
	HRESULT				hr;

	/* Check if requested sub-block is already cached */
	if (!sc->cache_invalid && start_addr >= sc->cache_start_addr && start_addr <= (sc->cache_start_addr + sc->cache_size)) {
		uint32_t cache_offs = start_addr - sc->cache_start_addr;

		if ((start_addr+size) <= (sc->cache_start_addr + sc->cache_size)) {
			/* Fully cached (cache hit) */
			memcpy(dst_buff, sc->cache_buff + cache_offs, size);
			return S_OK;
		} else {
			/* Partially cached (beginning part overlaps) */
			memcpy(dst_buff, sc->cache_buff + cache_offs, sc->cache_size - cache_offs);

			write_index = sc->cache_size - cache_offs;
			read_index	+= write_index;
			size 		-= write_index;
		}
	}

	/* Since we got here, this is a cache miss.
	 *
	 * If first block is not aligned read it in separate manner. So the
	 * remaining blocks (if any) will be block-aligned.
	 */
	if (read_index % block_size != 0) {
		uint32_t offs = read_index % block_size;

		hr = iso9660_cache_blocks(s, entry, read_index - offs, 1);
		if (FAILED(hr)) return hr;

		/* Copy sub-block to destination buffer */
		memcpy(dst_buff + write_index, sc->cache_buff + offs, block_size - offs);

		write_index += block_size - offs;
		read_index += block_size - offs;
		size -= block_size - offs;
	}

	/* Sanity check */
	if (read_index % block_size != 0) {
		HalKernelPanic("fat16_read_subblock(): unexpected.");
		return E_FAIL;
	}

	/* Calculate the remaining _aligned_ block count. */
	block_count = size / block_size;

	/* Read aligned blocks */
	hr = iso9660_file_read_block(drv, entry, read_index, block_count * block_size, dst_buff + write_index);
	if (FAILED(hr)) return hr;

	/* Move read and write indices */
	read_index += block_count * block_size;
	write_index += block_count * block_size;
	size -= block_count * block_size;

	/* Sanity check */
	if (size >= block_size) {
		HalKernelPanic("fat16_read_subblock(): unexpected(2).");
		return E_FAIL;
	}

	/* Read last _unaligned_ block (if remained). As first unaligned block,
	 * this too requires different treatment.
	 */
	if (size > 0) {
		/* Cache whole block */
		hr = iso9660_cache_blocks(s, entry, read_index, 1);
		if (FAILED(hr)) return hr;

		/* Write it to destination buffer */
		memcpy(dst_buff + write_index, sc->cache_buff, size);
	}

	/* Success */
	return S_OK;
}

static HRESULT
iso9660_cache_blocks(K_STREAM *s, ISO9660_DIR_ENTRY *entry, uint32_t start_addr, uint32_t block_cnt)
{
	ISO9660_STR_CONTEXT *sc			= s->priv_data;
	K_FS_DRIVER			*drv 		= s->fs_driver;
	uint32_t			block_size	= ISO9660_SECTOR_SIZE;
	HRESULT				hr;

	/* Validate input arguments */
	if (start_addr % block_size != 0 || block_cnt == 0) {
		return E_INVALIDARG;
	}

	/* Assert cache buffer is big enough */
	if (sc->cache_cap < block_cnt * block_size) {
		if (sc->cache_buff) {
			kfree(sc->cache_buff);
		}

		if (!(sc->cache_buff = kmalloc(block_cnt * block_size))) {
			return E_OUTOFMEM;
		}

		sc->cache_cap = block_cnt * block_size;
		sc->cache_invalid = TRUE;
	}

	/* Read blocks */
	hr = iso9660_file_read_block(drv, entry, start_addr, block_cnt * block_size, sc->cache_buff);
	if (FAILED(hr)) return hr;

	/* Update cache parameters */
	sc->cache_start_addr = start_addr;
	sc->cache_size = block_cnt * block_size;
	sc->cache_invalid = FALSE;

	/* Success */
	return S_OK;
}

static HRESULT
iso9660_file_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	UNUSED_ARG(str);
	UNUSED_ARG(block_size);
	UNUSED_ARG(in_buf);
	UNUSED_ARG(bytes_written);

	return E_FAIL;
}

static HRESULT
iso9660_file_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	UNUSED_ARG(s);
	UNUSED_ARG(code);
	UNUSED_ARG(arg);

	return E_UNEXPECTED;
}

static uint32_t
iso9660_file_seek(K_STREAM *s, int64_t pos, int8_t origin)
{
	ISO9660_STR_CONTEXT *strctx = s->priv_data;
	ISO9660_DIR_ENTRY	*entry 	= &strctx->entry;
	HRESULT 			hr 		= S_OK;
	int32_t 			new_pos;

	mutex_lock(&s->lock);

	switch (origin) {
	case KSTREAM_ORIGIN_BEGINNING:
		new_pos = pos;
		break;

	case KSTREAM_ORIGIN_CURRENT:
		new_pos =  s->pos + pos;
		break;

	case KSTREAM_ORIGIN_END:
		new_pos = entry->extent_size.lsb - pos;
		break;

	default:
		hr = E_INVALIDARG;
		goto finally;
	}

	if (new_pos < 0 || new_pos > (int32_t)entry->extent_size.lsb) {
		hr = E_INVALIDARG;
		goto finally;
	}

	s->pos = new_pos;

finally:
	mutex_unlock(&s->lock);
	return hr;
}

static uint32_t
iso9660_file_tell(K_STREAM *s)
{
	uint32_t current_pos;

	mutex_lock(&s->lock);
	current_pos = s->pos;
	mutex_unlock(&s->lock);

	return current_pos;
}

static HRESULT
iso9660_file_close(K_STREAM **str)
{
	K_STREAM 			*s = *str;
	ISO9660_STR_CONTEXT *strctx = s->priv_data;

	/* Free mutex */
	mutex_destroy(&s->lock);

	/* Free cache buffer*/
	if (strctx->cache_buff) {
		kfree(strctx->cache_buff);
	}

	/* Free private data and stream itself */
	kfree(s->priv_data);
	kfree(s);

	*str = NULL;
	return S_OK;
}

static HRESULT
iso9660_opendir(K_FS_DRIVER *drv, char *dirname, K_DIR_STREAM **out)
{
	ISO9660_DRV_CONTEXT	*c = drv->priv_data;
	ISO9660_DIR_ENTRY	entry;
	uint32_t			size;
	void				*dir_content;
	HRESULT				hr;

	hr = iso9660_parse_url(drv, dirname, &entry);
	if (FAILED(hr)) return hr;

	/* Make sure this is a directory */
	if ((entry.flags & FLAG_DIRECTORY) == 0) {
		return E_FAIL;
	}

	/* Allocate a memory buffer to load the directory content */
	size = entry.extent_size.lsb + ISO9660_SECTOR_SIZE - entry.extent_size.lsb % ISO9660_SECTOR_SIZE;

	if (!(dir_content = kmalloc(size))) {
		return E_OUTOFMEM;
	}

	/* Read directory content */
	hr = storage_read_blocks(c->storage_drv, entry.extent_start.lsb, (size + ISO9660_SECTOR_SIZE - 1) / ISO9660_SECTOR_SIZE, dir_content);
	if (FAILED(hr)) goto finally;

	/* Create new kernel directory stream */
	K_DIR_STREAM *ds;

	if (!(ds = kmalloc(sizeof(K_DIR_STREAM)))) {
		hr = E_OUTOFMEM;
		goto finally;
	}

	/* Allocate string to hold name of the directory being opened */
	if (!(ds->dirname = kcalloc(VFS_MAX_DIRNAME_LENGTH))) {
		hr = E_OUTOFMEM;
		goto finally;
	}

	mutex_create(&ds->lock);
	strcpy(ds->dirname, dirname);

	ds->priv_data = dir_content;
	ds->len = entry.extent_size.lsb; //iso9660_get_subentry_count(dir_content, entry->extent_size.lsb);
	ds->pos = 0;

	/* Assign ops */
	ds->readdir 	= iso9660_readdir;
	ds->rewinddir 	= iso9660_rewinddir;
	ds->closedir 	= iso9660_closedir;

	/* Success */
	*out = ds;

finally:
	if (FAILED(hr)) {
		kfree(dir_content);
	}

	return hr;
}

/**
 * Parses directory content to find the number of subentries.
 */
static uint32_t
iso9660_get_subentry_count(void *content, uint32_t size)
{
	ISO9660_DIR_ENTRY	*entry;
	uint8_t				*content_ptr = content;
	uint32_t			offset = 0;
	uint32_t			count = 0;

	while (offset != size) {
		if (offset > size) {
			/* Totally unexpected */
			HalKernelPanic("iso9660_get_subentry_count(): unexpected case of reaching out of content buffer.");
		}

		/* Read single subentry */
		entry = (ISO9660_DIR_ENTRY*)(content + offset);

		/* Skip hidden subentries */
		if (entry->flags & FLAG_HIDDEN) {
			goto next;
		}

		count++;

next:
		/* Next entry */
		offset += entry->length;

		/* Handle gaps */
		if (content_ptr[offset] == 0) {
			offset += ISO9660_SECTOR_SIZE - offset % ISO9660_SECTOR_SIZE;
		}
	}

	return count;
}

static HRESULT
iso9660_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info)
{
	ISO9660_DIR_ENTRY	*entry;
	uint32_t			new_offs = dirstr->pos;
	uint8_t				*content = dirstr->priv_data;
	HRESULT				hr;

	mutex_lock(&dirstr->lock);

	while (TRUE) {
		/* We will use dirstr->pos as offset */
		entry = (ISO9660_DIR_ENTRY*)(content + new_offs);

		/* Handle gaps */
		if (content[new_offs] == 0) {
			new_offs += ISO9660_SECTOR_SIZE - new_offs % ISO9660_SECTOR_SIZE;
			continue;
		}

		if (new_offs >= (uint32_t)dirstr->len) {
			hr = E_ENDOFSTR;
			goto finally;
		}

		/* Skip "." and ".." entries */
		if (entry->name_length == 1 && (entry->name[0] == '\0' || entry->name[0] == '\1')) {
			/* Move to next one */
			new_offs += entry->length;
			continue;
		}

		/* Skip hiddne subentries */
		if ((entry->flags & FLAG_HIDDEN) != 0) {
			/* Move to next one */
			new_offs += entry->length;
			continue;
		}

		break;
	}

	info->node_type	= (entry->flags & FLAG_DIRECTORY) == 0 ? NODE_TYPE_FILE : NODE_TYPE_DIRECTORY;
	info->size 		= entry->extent_size.lsb;
	iso9660_extract_filename(entry, filename);

	dirstr->pos = new_offs += entry->length;
	hr 			= S_OK;

finally:
	mutex_unlock(&dirstr->lock);
	return hr;
}

static HRESULT
iso9660_rewinddir(K_DIR_STREAM *dirstr)
{
	mutex_lock(&dirstr->lock);
	dirstr->pos = 0;
	mutex_unlock(&dirstr->lock);

	return S_OK;
}

static HRESULT
iso9660_closedir(K_DIR_STREAM **dirstr)
{
	K_DIR_STREAM *d = *dirstr;

	kfree(d->dirname);

	/* Free content */
	if (d->priv_data) {
		kfree(d->priv_data);
	}

	mutex_destroy(&d->lock);
	kfree(d);

	*dirstr = NULL;
	return S_OK;
}

K_FS_CONSTRUCTOR
iso9660_get_constructor()
{
	return iso9660_init;
}
