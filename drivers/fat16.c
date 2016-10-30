/*
 * fat16.c
 *
 *	FAT12/FAT16 file-system driver.
 *
 *	Yet to current date the driver supports only reading operations.
 *	Some of the routines does handle FAT32 as well, but it is not generally
 *	supported.
 *
 *	TODO:
 *		- Implement write support (creating/writing files, directories)
 *		- Support for FAT32
 *		- Format
 *		- Defragmentation
 *		- Clobbering

 *	References:
 *		- http://fileadmin.cs.lth.se/cs/Education/EDA385/HT09/student_doc/FinalReports/FAT12_overview.pdf
 *
 *  Created on: 23.09.2016 ã.
 *      Author: Anton Angelov
 */
#include "fat16.h"
#include <syncobjs.h>
#include <mm.h>
#include <string.h>
#include <hal.h>
#include <kstdio.h>

/* Prototypes of internal functions */
static HRESULT fat16_parse_bpb(K_FS_DRIVER *drv);
static HRESULT fat16_fat_lookup(K_FS_DRIVER *drv, uint32_t fat_index, uint32_t *value);
static HRESULT fat16_cache_fat_table(K_FS_DRIVER *drv);
static HRESULT fat16_read_file_content(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *entry, uint32_t start, uint32_t count, void *dst);
static HRESULT fat16_read_rootdir_content(K_FS_DRIVER *drv, void *dst);
static HRESULT fat16_find_subentry(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *dir, char *filename, uint32_t flagmask, BOOL skip_vlabel, FAT16_DIR_ENTRY *dst);
static HRESULT fat16_parse_url(K_FS_DRIVER *drv, char *url, FAT16_DIR_ENTRY *out_entry);

/* Helper functions.
 * Functions with "fat" prefix are usually usable for all FAT file-system types.
 */
static BOOL fat_is_end_of_chain(K_FS_DRIVER *drv, uint32_t cluster_id);
static void	fat_copy_short_filename(FAT16_DIR_ENTRY *entry, char *dst);
static uint32_t fat_get_type(K_FS_DRIVER *drv);
static uint32_t fat_get_rootdir_size(K_FS_DRIVER *drv);
static uint32_t fat_get_subitem_count(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *entry_arr, uint32_t entries_max);
static uint32_t fat_count_chain(K_FS_DRIVER *drv, uint32_t first_cluster);
static uint32_t	fat_get_first_cluster(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *entry);

/* Functions exported through the driver interface */
static HRESULT fat16_init(K_FS_DRIVER *drv, char *storage_drv);
static HRESULT fat16_fini(K_FS_DRIVER *drv);
static HRESULT fat16_opendir(K_FS_DRIVER *drv, char *dirname, K_DIR_STREAM **out);
static HRESULT fat16_mkdir(K_FS_DRIVER *drv, char *path, uint32_t mode);
static HRESULT fat16_create(K_FS_DRIVER *drv, char *filename, uint32_t perm);
static HRESULT fat16_open(K_FS_DRIVER *drv, char *filename, uint32_t flags, K_STREAM **out);

/* Directory stream functions */
static HRESULT fat16_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info);
static HRESULT fat16_rewinddir(K_DIR_STREAM *dirstr);
static HRESULT fat16_closedir(K_DIR_STREAM **dirstr);

/* File stream functions */
static HRESULT fat16_file_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);
static HRESULT fat16_file_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
static HRESULT fat16_file_ioctl(K_STREAM *s, uint32_t code, void *arg);
static uint32_t fat16_file_seek(K_STREAM *s, int64_t pos, int8_t origin);
static uint32_t fat16_file_tell(K_STREAM *s);
static HRESULT fat16_file_close(K_STREAM **str);

/* File stream helpers */
static HRESULT fat16_read_subblock(K_STREAM *s, FAT16_DIR_ENTRY *entry, uint32_t start_addr, uint32_t size, void *dst);
static HRESULT fat16_cache_blocks(K_STREAM *s, FAT16_DIR_ENTRY *entry, uint32_t start_addr, uint32_t block_cnt);

/*
 * Tests if a given cluster id is a symbolic constant for
 * denoting end of a cluster chain.
 */
static BOOL
fat_is_end_of_chain(K_FS_DRIVER *drv, uint32_t cluster_id)
{
	FAT16_DRV_CONTEXT *ctx = drv->priv_data;

	switch (ctx->fs_type) {
		case FS_TYPE_FAT12:
			if (cluster_id >= 0xFF7) {
				return TRUE;
			}
			break;

		case FS_TYPE_FAT16:
			if (cluster_id >= 0xFFF7) {
				return TRUE;
			}
			break;

		case FS_TYPE_FAT32:
			if ((cluster_id & 0x0FFFFFFF) >= 0x0FFFFFF7) {
				return TRUE;
			}
			break;

		default:
			HalKernelPanic("fat_is_end_of_chain(): Unhandled FS type.");
	}

	return FALSE;
}

/*
 * Looks up the successive cluster id in the cluster chain for given
 * cluster with id of `cluster_index`.
 */
static HRESULT
fat16_fat_lookup(K_FS_DRIVER *drv, uint32_t fat_index, uint32_t *value)
{
	FAT16_DRV_CONTEXT *ctx = drv->priv_data;
	uint32_t result;

	if (fat_is_end_of_chain(drv, fat_index) == TRUE) {
		return E_INVALIDARG;
	}

	switch (ctx->fs_type) {
		case FS_TYPE_FAT12:
			result = *(uint16_t*)&ctx->fat_cache[fat_index + fat_index / 2];
			result = (fat_index & 0x1) != 0 ? result >> 4 : result & 0x0FFF;
			break;

		case FS_TYPE_FAT16:
			result = *(uint16_t*)&ctx->fat_cache[fat_index * 2];
			break;

		case FS_TYPE_FAT32:
			result = *(uint32_t*)&ctx->fat_cache[fat_index * 4];
			break;

		default:
			HalKernelPanic("fat16_lookup(): Unhandled FS type.");
			return E_FAIL;
	}

	*value = result;
	return S_OK;
}

/*
 * Reads the content of the FAT and stores it in a memory buffer.
 */
static HRESULT fat16_cache_fat_table(K_FS_DRIVER *drv)
{
	FAT16_DRV_CONTEXT *ctx 		= drv->priv_data;
	uint32_t	fat_size		= ctx->bpb.sectors_per_FAT * ctx->bpb.bytes_per_sector;
	uint32_t	fat_start		= ctx->first_fat_sector;
	HRESULT		hr;

	/* Free old cache buffer */
	if (ctx->fat_cache) {
		kfree(ctx->fat_cache);
	}

	/* Allocate new one */
	if (!(ctx->fat_cache = kmalloc(fat_size))) {
		return E_OUTOFMEM;
	}

	/* Read sectors to memory buffer directly */
	hr = storage_read_blocks(ctx->storage_drv, fat_start, ctx->bpb.sectors_per_FAT, ctx->fat_cache);
	return hr;
}

/*
 * Reads `count` bytes from the content of a FAT12/16 file starting from the
 * `start`-th byte. Start address and count have to be multiple of the sector
 * size.
 *
 * Reading is done by iterating the cluster chain and copying
 * cluster's content to `dst` buffer.
 *
 * Note that directories are also considered files holding an array of
 * `FAT16_DIR_ENTRY` structs.
 */
static HRESULT fat16_read_file_content(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *entry, uint32_t start, uint32_t count, void *dst)
{
	FAT16_DRV_CONTEXT 	*ctx 			= drv->priv_data;
	HRESULT				hr;
	uint32_t			skip_sectors	= start / ctx->bpb.bytes_per_sector;
	uint32_t			count_sectors	= count / ctx->bpb.bytes_per_sector;
	uint32_t			count_clusters	= (count_sectors + ctx->bpb.sectors_per_cluster - 1) / ctx->bpb.sectors_per_cluster;
	uint32_t			cluster_id		= fat_get_first_cluster(drv, entry);
	uint8_t				*dst_buff		= dst;
	uint32_t			write_index		= 0;
	uint32_t			i, j, sector;

	/* Validate input arguments */
	if (start % ctx->bpb.bytes_per_sector != 0 || count % ctx->bpb.bytes_per_sector != 0) {
		return E_INVALIDARG;
	}

	/* Skip to the first cluster of interest */
	while (skip_sectors >= ctx->bpb.sectors_per_cluster) {
		/* Find ID of next cluster */
		hr = fat16_fat_lookup(drv, cluster_id, &cluster_id);
		if (FAILED(hr)) return E_FAIL;

		skip_sectors -= ctx->bpb.sectors_per_cluster;
	}

	/* Iterate clusters */
	for (i=0; i<count_clusters; i++) {
		sector = ctx->first_data_sector + (cluster_id - 2) * ctx->bpb.sectors_per_cluster;

		/* Iterate sectors */
		for (j=0; j<ctx->bpb.sectors_per_cluster; j++) {
			/* Do we have to skip more sectors? */
			if (skip_sectors > 0) {
				skip_sectors--;
				continue;
			}

			/* Read sector */
			hr = storage_read_blocks(ctx->storage_drv, sector+j, 1, dst_buff + write_index);
			if (FAILED(hr)) return hr;;
			write_index += ctx->bpb.bytes_per_sector;

			/* Did we read enough? */
			if (write_index == count) {
				break;
			}
		}

		/* Jump to next cluster */
		hr = fat16_fat_lookup(drv, cluster_id, &cluster_id);
		if (FAILED(hr)) return E_FAIL;
	}

	/* Sanity check */
	if (write_index != count) {
		k_printf("write_index=%d; count=%d\n", write_index, count);
		HalKernelPanic("fat16_read_file_content(): Unexpected.");
	}

	return S_OK;
}

/*
 * Reads the content of the FAT12/16 "root dir", which holds an array
 * of DIR ENTRIES for files and directories residing in the root file system
 * directory.
 *
 * It is not possible to retrieve parts of the root dir content.
 */
static HRESULT fat16_read_rootdir_content(K_FS_DRIVER *drv, void *dst)
{
	FAT16_DRV_CONTEXT 	*ctx 		= drv->priv_data;
	uint32_t			root_bytes 	= fat_get_rootdir_size(drv);
	uint32_t			root_sectors= (root_bytes + ctx->bpb.bytes_per_sector - 1) / ctx->bpb.bytes_per_sector;
	uint32_t			write_index = 0;
	uint8_t				*dst_buff	= dst; //use uint8_t* for pointer arithmetics
	uint32_t			i;
	uint8_t				*buff;
	HRESULT				hr;

	for (i=0; i<root_sectors; i++) {
		if (root_bytes - write_index >= ctx->bpb.bytes_per_sector) {
			/* Read sector directly to destination buffer */
			hr = storage_read_blocks(ctx->storage_drv, ctx->first_root_sector + i, 1, dst_buff + write_index);
			if (FAILED(hr)) return hr;

			write_index += ctx->bpb.bytes_per_sector;
		} else {
			/* Read sector through temporary buffer, since we have no guarantee that
			 * destination buffer can accommodate more that `root_bytes` bytes.
			 */
			buff = kmalloc(ctx->bpb.bytes_per_sector);
			hr = storage_read_blocks(ctx->storage_drv, ctx->first_root_sector + i, 1, buff);

			if (SUCCEEDED(hr)) {
				memcpy(dst_buff + write_index, buff, root_bytes - write_index);
			}

			/* Clean up */
			kfree(buff);
			if (FAILED(hr)) return hr;
		}
	}

	return S_OK;
}


/*
 * Opens a handle to a directory
 */
static HRESULT
fat16_opendir(K_FS_DRIVER *drv, char *dirname, K_DIR_STREAM **out)
{
	FAT16_DRV_CONTEXT 	*ctx = drv->priv_data;
	FAT16_DIR_ENTRY		entry;
	FAT16_DIR_ENTRY		*content = NULL;
	uint32_t			max_entry_cnt;
	uint32_t			content_size;
	BOOL				is_root_dir;
	HRESULT				hr;

	is_root_dir = strlen(dirname) == 0 ? TRUE : FALSE;

	if (!is_root_dir) {
		/* Find target directory and read it's DIR ENTRY */
		hr = fat16_parse_url(drv, dirname, &entry);
		if (FAILED(hr)) return hr;

		/* Assert item is directory */
		if ((entry.attributes & FAT_ATTR_DIRECTORY) == 0) {
			//k_printf("not a directory\n");
			return E_FAIL;
		}

		content_size = fat_count_chain(drv, fat_get_first_cluster(drv, &entry)) * ctx->bpb.bytes_per_sector * ctx->bpb.sectors_per_cluster;
		if (content_size == 0) {
			/* Invalid starting cluster */
			return E_FAIL;
		}

		/* Allocate content buffer */
		if (!(content = kmalloc(content_size))) {
			return E_OUTOFMEM;
		}

		/* Read content */
		hr = fat16_read_file_content(drv, &entry, 0, content_size, content);
		if (FAILED(hr)) goto fail;

		/* Max entry count */
		max_entry_cnt = content_size / sizeof(FAT16_DIR_ENTRY);
	} else {
		/* Allocate content buffer */
		if (!(content = kmalloc(fat_get_rootdir_size(drv)))) {
			return E_OUTOFMEM;
		}

		hr = fat16_read_rootdir_content(drv, content);
		if (FAILED(hr)) goto fail;

		/* Max entry count */
		max_entry_cnt = ctx->bpb.root_entries;
	}

	/* Create new kernel directory stream */
	K_DIR_STREAM *ds = kmalloc(sizeof(K_DIR_STREAM));

	/* Allocate string to hold name of the directory being opened */
	if (!(ds->dirname = kmalloc(VFS_MAX_DIRNAME_LENGTH))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	if (is_root_dir) {
		ds->dirname[0] = '\0';
	} else {
		strcpy(ds->dirname, dirname);
	}

	ds->priv_data = content;
	ds->len = max_entry_cnt;
	ds->pos = 0;

	/* Assign ops */
	ds->readdir 	= fat16_readdir;
	ds->rewinddir 	= fat16_rewinddir;
	ds->closedir 	= fat16_closedir;

	/* Success */
	*out = ds;

	return S_OK;

fail:
	if (content != NULL) {
		kfree(content);
	}

	return hr;
}

/*
 * Reads the sub-item currently pointed by `dirstr`'s position. Then moves position
 * to the next one.
 */
static HRESULT fat16_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info)
{
	FAT16_DIR_ENTRY *entries = dirstr->priv_data;
	FAT16_DIR_ENTRY *e;

	while (dirstr->pos < dirstr->len) {
		e = &entries[dirstr->pos];

		if (e->filename[0] == 0xE5) {
			/* Deleted sub-item. Skip */
			goto skip;
		}

		if (e->filename[0] == 0x00) {
			/* End of entry list */
			dirstr->pos = dirstr->len;
			break;
		}

		/* Skip volume labels */
		if ((e->attributes & FAT_ATTR_VOLUMEL) != 0) {
			goto skip;
		}

		/* Skip long filename entries */
		if (e->attributes == 0x0F) {
			goto skip;
		}

		/* Skip "." and ".." entries */
		if (e->filename[0] == '.') {
			if (e->filename[1] == ' ' || (e->filename[1] == '.' && e->filename[2] == ' ')) {
				goto skip;
			}
		}

		/* Found */
		fat_copy_short_filename(e, filename);
		info->node_type = (e->attributes & FAT_ATTR_DIRECTORY) == 0 ? NODE_TYPE_FILE : NODE_TYPE_DIRECTORY;
		info->size = e->size;

		/* Move position */
		dirstr->pos++;

		return S_OK;

skip:
		dirstr->pos++;
		continue;
	}

	return E_ENDOFSTR;
}

/*
 * Rewinds the directory stream position.
 */
static HRESULT fat16_rewinddir(K_DIR_STREAM *dirstr)
{
	dirstr->pos = 0;
	return S_OK;
}

/*
 * Closes directory stream and deallocates it's related resources.
 */
static HRESULT fat16_closedir(K_DIR_STREAM **dirstr)
{
	K_DIR_STREAM *d = *dirstr;

	kfree(d->dirname);

	if (d->priv_data) {
		kfree(d->priv_data);
	}

	kfree(d);

	*dirstr = NULL;
	return S_OK;
}

/*
 * Creates a directory.
 */
static HRESULT fat16_mkdir(K_FS_DRIVER *drv, char *path, uint32_t mode)
{
	UNUSED_ARG(drv);
	UNUSED_ARG(path);
	UNUSED_ARG(mode);

	return E_NOTIMPL;
}

/*
 * Creates an empty file.
 */
static HRESULT fat16_create(K_FS_DRIVER *drv, char *filename, uint32_t perm)
{
	UNUSED_ARG(drv);
	UNUSED_ARG(filename);
	UNUSED_ARG(perm);

	return E_NOTIMPL;
}

/*
 * Opens handle to a file stream.
 */
static HRESULT fat16_open(K_FS_DRIVER *drv, char *filename, uint32_t flags, K_STREAM **out)
{
	FAT16_DIR_ENTRY			entry;
	FAT16_STR_CONTEXT 		*strctx = NULL;
	K_STREAM 				*stream = NULL;
	HRESULT					hr;

	/* Find target directory and read it's DIR ENTRY */
	hr = fat16_parse_url(drv, filename, &entry);
	if (FAILED(hr)) return hr;

	/* Assert item is a file */
	if ((entry.attributes & (FAT_ATTR_DIRECTORY | FAT_ATTR_VOLUMEL)) != 0) {
		return E_FAIL;
	}

	/* Create new file stream */
	if (!(stream = kcalloc(sizeof(K_STREAM)))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	if (!(strctx = kcalloc(sizeof(FAT16_STR_CONTEXT)))) {
		hr = E_OUTOFMEM;
		goto fail;
	}

	/* Setup stream. No need to set cache params since they
	 * are initially zeroed. */
	stream->mode = flags;
	stream->fs_driver = drv;
	stream->priv_data = strctx;

	memcpy(&strctx->entry, &entry, sizeof(FAT16_DIR_ENTRY));

	/* Create mutex */
	mutex_create(&stream->lock);

	/* Assign file ops */
	stream->read 	= fat16_file_read;
	stream->write 	= fat16_file_write;
	stream->ioctl	= fat16_file_ioctl;
	stream->seek	= fat16_file_seek;
	stream->tell	= fat16_file_tell;
	stream->close	= fat16_file_close;

	/* Success */
	*out = stream;
	return S_OK;

fail:
	if (stream) kfree(stream);
	if (strctx) kfree(strctx);

	return hr;
}

/*
 * Closes file stream and frees it's related resources.
 */
static HRESULT fat16_file_close(K_STREAM **str)
{
	K_STREAM 			*s = *str;
	FAT16_STR_CONTEXT 	*strctx = s->priv_data;

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

/*
 * Reads non-sector-aligned memory blocks from a file stream.
 */
static HRESULT fat16_read_subblock(K_STREAM *s, FAT16_DIR_ENTRY *entry, uint32_t start_addr, uint32_t size, void *dst)
{
	FAT16_STR_CONTEXT 	*sc 		= s->priv_data;
	K_FS_DRIVER			*drv 		= s->fs_driver;
	FAT16_DRV_CONTEXT 	*ctx 		= drv->priv_data;
	uint32_t			write_index	= 0;
	uint32_t			read_index  = start_addr;
	uint32_t			block_size	= ctx->bpb.bytes_per_sector;
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
		}
		else {
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
		uint32_t offs = read_index % ctx->bpb.bytes_per_sector;

		hr = fat16_cache_blocks(s, entry, read_index - offs, 1);
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
	hr = fat16_read_file_content(drv, entry, read_index, block_count * block_size, dst_buff + write_index);
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
		hr = fat16_cache_blocks(s, entry, read_index, 1);
		if (FAILED(hr)) return hr;

		/* Write it to destination buffer */
		memcpy(dst_buff + write_index, sc->cache_buff, size);
	}

	/* Success */
	return S_OK;
}

/*
 * Reads `block_cnt` blocks from the content of a file/directory and stores them
 * in the cache buffer.
 */
static HRESULT fat16_cache_blocks(K_STREAM *s, FAT16_DIR_ENTRY *entry, uint32_t start_addr, uint32_t block_cnt)
{
	FAT16_STR_CONTEXT 	*sc			= s->priv_data;
	K_FS_DRIVER			*drv 		= s->fs_driver;
	FAT16_DRV_CONTEXT 	*ctx 		= drv->priv_data;
	uint32_t			block_size	= ctx->bpb.bytes_per_sector;
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
	hr = fat16_read_file_content(drv, entry, start_addr, block_cnt * block_size, sc->cache_buff);
	if (FAILED(hr)) return hr;

	/* Update cache parameters */
	sc->cache_start_addr = start_addr;
	sc->cache_size = block_cnt * block_size;
	sc->cache_invalid = FALSE;

	/* Success */
	return S_OK;
}

/*
 * Reads `block_size` bytes from an opened file stream.
 */
static HRESULT fat16_file_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	FAT16_STR_CONTEXT 	*strctx = str->priv_data;
	FAT16_DIR_ENTRY		*entry = &strctx->entry;
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

	if (block_size + str->pos >= entry->size) {
		effective_size = entry->size - str->pos;
	}

	if (effective_size == 0) {
		/* End of file */
		if (bytes_read) *bytes_read = 0;
		hr = E_ENDOFSTR;

		goto finally;
	}

	/* Read content */
	//hr = fat16_read_file_content_DEPRECATED(drv, entry, str->pos, effective_size, out_buf);
	hr = fat16_read_subblock(str, entry, str->pos, effective_size, out_buf);
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
 * Writes `block_size` bytes to file.
 */
static HRESULT fat16_file_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	UNUSED_ARG(str);
	UNUSED_ARG(block_size);
	UNUSED_ARG(in_buf);
	UNUSED_ARG(bytes_written);

	return E_NOTIMPL;
}

/*
 * IOCTL stub. Doesn't do anything, since IOCTL is used only for
 * devices/drivers.
 */
static HRESULT fat16_file_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	UNUSED_ARG(s);
	UNUSED_ARG(code);
	UNUSED_ARG(arg);

	return E_UNEXPECTED;
}

/*
 * Moves the file stream position according to the given arguments.
 */
static uint32_t fat16_file_seek(K_STREAM *s, int64_t pos, int8_t origin)
{
	FAT16_STR_CONTEXT 	*strctx = s->priv_data;
	FAT16_DIR_ENTRY		*entry 	= &strctx->entry;
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
		new_pos = entry->size - pos;
		break;

	default:
		hr = E_INVALIDARG;
		goto finally;
	}

	if (new_pos < 0 || new_pos > (int32_t)entry->size) {
		hr = E_INVALIDARG;
		goto finally;
	}

	s->pos = new_pos;

finally:
	mutex_unlock(&s->lock);
	return hr;
}

/*
 * Retrieves the current position of the file stream.
 */
static uint32_t fat16_file_tell(K_STREAM *s)
{
	uint32_t current_pos;

	mutex_lock(&s->lock);
	current_pos = s->pos;
	mutex_unlock(&s->lock);

	return current_pos;
}

/*
 * Concatenates the filename with the file extension putting
 * a dot in between.
 */
static void	fat_copy_short_filename(FAT16_DIR_ENTRY *entry, char *dst)
{
	uint32_t i, dst_index = 0;

	/* Copy name */
	for (i=0; i<8; i++) {
		if (entry->filename[i] != ' ') {
			dst[dst_index++] = entry->filename[i];
		} else {
			break;
		}
	}

	/* Insert dot */
	if (entry->ext[0] != ' ')
	dst[dst_index++] = '.';

	/* Copy extension */
	for (i=0; i<3; i++) {
		if (entry->ext[i] != ' ') {
			dst[dst_index++] = entry->ext[i];
		} else {
			break;
		}
	}

	dst[dst_index++] = '\0';
}

/*
 * Figures out the type of the FAT file system based on few parameters.
 */
static uint32_t
fat_get_type(K_FS_DRIVER *drv)
{
	FAT16_DRV_CONTEXT 	*ctx = drv->priv_data;

	if (ctx->total_cluster_cnt < 4085) {
		return FS_TYPE_FAT12;
	} else if (ctx->total_cluster_cnt < 65525) {
		return FS_TYPE_FAT16;
	} else if (ctx->total_cluster_cnt < 268435445) {
		return FS_TYPE_FAT32;
	}

	return FS_TYPE_EXFAT;
}

/*
 * Returns size of the root dir content in bytes.
 */
static uint32_t fat_get_rootdir_size(K_FS_DRIVER *drv)
{
	FAT16_DRV_CONTEXT 	*ctx = drv->priv_data;
	return ctx->bpb.root_entries * sizeof(FAT16_DIR_ENTRY);
}

/*
 * Iterates all directory entries and count the actual number
 * of sub-items (files and directories) for the given directory.
 */
static uint32_t fat_get_subitem_count(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *entry_arr, uint32_t entries_max)
{
	uint32_t			i, cnt=0;

	UNUSED_ARG(drv);

	for (i=0; i<entries_max; i++) {
		if (entry_arr[i].filename[0] == 0xE5) {
			/* Sub-item is deleted */
			continue;
		}

		if (entry_arr[i].filename[0] == 0x00) {
			/* End of list */
			break;
		}

		cnt++;
	}

	return cnt;
}

/*
 * Counts clusters in a chain starting from the given cluster id.
 */
static uint32_t fat_count_chain(K_FS_DRIVER *drv, uint32_t first_cluster)
{
	FAT16_DRV_CONTEXT 	*ctx 	= drv->priv_data;
	uint32_t			current = first_cluster;
	uint32_t			count 	= 0;
	HRESULT				hr;

	if (first_cluster == 0x00) {
		/* Unused cluster */
		return 0;
	}

	while (!fat_is_end_of_chain(drv, current)) {
		hr = fat16_fat_lookup(drv, current, &current);
		if (FAILED(hr)) return 0;

		count++;

		/* Break out of defective (cyclic) chains */
		if (count == ctx->total_cluster_cnt) {
			count = 0;
			break;
		}
	}

	return count;
}

/*
 * Returns the first data cluster of a DIR entry.
 */
static uint32_t	fat_get_first_cluster(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *entry)
{
	FAT16_DRV_CONTEXT 	*ctx = drv->priv_data;
	uint32_t			result = 0;

	switch (ctx->fs_type) {
		case FS_TYPE_FAT12:
		case FS_TYPE_FAT16:
			result = entry->first_clust_low;
			break;

		case FS_TYPE_FAT32:
			result = (entry->first_clust_high << 16) | (entry->first_clust_low);
			break;

		default:
			HalKernelPanic("fat_get_entry_start_cluster(): unsupported fs.");
	}

	return result;
}

/*
 * Parses the BIOS Parameter Block from the Boot Record.
 */
static HRESULT
fat16_parse_bpb(K_FS_DRIVER *drv)
{
	FAT16_DRV_CONTEXT 	*ctx = drv->priv_data;
	HRESULT 			hr;
	size_t				block_size;
	uint8_t				*buff;
	uint32_t			root_dir_sectors;

	hr = storage_get_block_size(ctx->storage_drv, &block_size);
	if (FAILED(hr)) return hr;

	/* Allocate temporary buffer */
	if (!(buff = kmalloc(block_size))) {
		return E_OUTOFMEM;
	}

	/* Read boot record */
	hr = storage_read_blocks(ctx->storage_drv, 0, 1, buff);
	if (FAILED(hr)) goto finally;

	/* Copy appropriate data to BPB buffer */
	memcpy(&ctx->bpb, buff, sizeof(FAT16_BPB));
	memcpy(&ctx->ebpb, buff+sizeof(FAT16_BPB), sizeof(FAT16_EBPB));

	/* Initialize remaining fields */
	ctx->total_sectors 		= ctx->bpb.num_sectors_16 > 0 ? ctx->bpb.num_sectors_16 : ctx->bpb.num_sectors_32;

	root_dir_sectors 		= (fat_get_rootdir_size(drv) + (ctx->bpb.bytes_per_sector - 1)) / ctx->bpb.bytes_per_sector;
	ctx->first_data_sector 	= ctx->bpb.reserved_sectors + (ctx->bpb.FAT_count * ctx->bpb.sectors_per_FAT) + root_dir_sectors;
	ctx->first_fat_sector  	= ctx->bpb.reserved_sectors;
	ctx->first_root_sector	= ctx->first_fat_sector + ctx->bpb.FAT_count * ctx->bpb.sectors_per_FAT; //sector

	ctx->data_sector_cnt 	= ctx->total_sectors - ctx->bpb.reserved_sectors - ctx->bpb.FAT_count * ctx->bpb.sectors_per_FAT - root_dir_sectors;
	ctx->total_cluster_cnt	= ctx->data_sector_cnt / ctx->bpb.sectors_per_cluster;

	/* fat_get_type() uses `ctx->total_cluster_cnt` field, so it has to be invoked
	 * after it is properly evaluated.
	 */
	ctx->fs_type 			= fat_get_type(drv);

finally:
	kfree(buff);
	return hr;
}

/*
 * Retrieves directory content for `dir` and iterates it's sub-items to find a specific
 * one with given filename and flags.
 *
 * If a match is found, it is returned copied to `dst`.
 */
static HRESULT fat16_find_subentry(K_FS_DRIVER *drv, FAT16_DIR_ENTRY *dir, char *filename, uint32_t flagmask, BOOL skip_vlabel, FAT16_DIR_ENTRY *dst)
{
	FAT16_DRV_CONTEXT 	*ctx = drv->priv_data;
	FAT16_DIR_ENTRY		*buf;
	uint32_t			entry_cnt;
	uint32_t			content_size;
	char				fn_buff[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint32_t			i;
	HRESULT				hr;

	/* If `dir` is NULL, we have to list the root directory */
	if (dir == NULL) {
		/* Allocate temporary buffer to store directory content */
		if (!(buf = kmalloc(ctx->bpb.root_entries & sizeof(FAT16_DIR_ENTRY)))) {
			return E_OUTOFMEM;
		}

		/* Read content */
		hr = fat16_read_rootdir_content(drv, buf);
		if (FAILED(hr)) goto finally;

		entry_cnt = ctx->bpb.root_entries;
	} else {
		content_size = fat_count_chain(drv, fat_get_first_cluster(drv, dir)) * ctx->bpb.bytes_per_sector * ctx->bpb.sectors_per_cluster;

		/* Only directories can be listed for subentries */
		if ((dir->attributes & FAT_ATTR_DIRECTORY) == 0) {
			return E_INVALIDARG;
		}

		/* Allocate temporary buffer to store directory content */
		if (!(buf = kmalloc(content_size))) {
			return E_OUTOFMEM;
		}

		/* Read content */
		hr = fat16_read_file_content(drv, dir, 0, content_size, buf);
		if (FAILED(hr)) goto finally;

		entry_cnt = content_size / sizeof(FAT16_DIR_ENTRY);
	}

	/* Iterate sub-entries */
	for (i=0; i<entry_cnt; i++) {
		/* Skip entries for deleted files/directories */
		if (buf[i].filename[0] == 0xE5) {
			continue;
		}

		/* Character 0x05 is escape token for 0xE5 */
		if (buf[i].filename[0] == 0x05) {
			buf[i].filename[0] = 0xE5;
		}

		/* Handle end of directory */
		if (buf[i].filename[0] == 0x00) {
			break;
		}

		/* Skip volume label entries */
		if (skip_vlabel && (buf[i].attributes & FAT_ATTR_VOLUMEL) != 0) {
			continue;
		}

		/* Match flags */
		if (flagmask != 0 && (buf[i].attributes & flagmask) != flagmask) {
			/* Flags does not match */
			continue;
		}

		fat_copy_short_filename(&buf[i], fn_buff);

		/* Match file names */
		if (stricmp(fn_buff, filename) != 0) {
			continue;
		}

		/* Found */
		*dst = buf[i];
		goto finally;
	}

	/* Not found */
	hr = E_NOTFOUND;

finally:
	kfree(buf);
	return hr;
}

/*
 * Descends the file system according to the given URL and tries to find and retrieve
 * FAT16_DIR_ENTRY data for the specified file.
 */
static HRESULT fat16_parse_url(K_FS_DRIVER *drv, char *url, FAT16_DIR_ENTRY *out_entry)
{
	uint32_t 			comp_count = 0;
	uint32_t 			url_len = strlen(url);
	FAT16_DIR_ENTRY		curr_entry;
	uint32_t 			i, len, flagmask;
	HRESULT				hr;
	char				comp_name[256];

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
		flagmask = (comp_count - i == 1) ? 0x00 : FAT_ATTR_DIRECTORY;

		/* Search */
		if (i == 0) {
			hr = fat16_find_subentry(drv, NULL, comp_name, flagmask, TRUE, &curr_entry);
		} else {
			hr = fat16_find_subentry(drv, &curr_entry, comp_name, flagmask, TRUE, &curr_entry);
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
 * Creates new instance of the FAT12/16 driver.
 */
static HRESULT
fat16_init(K_FS_DRIVER *drv, char *storage_drv)
{
	FAT16_DRV_CONTEXT 	*c;
	HRESULT				hr;

	if (drv->priv_data != NULL) {
		/* Driver already initialized, or uninitialized memory */
		return E_FAIL;
	}

	/* Create driver context */
	if (!(drv->priv_data = kcalloc(sizeof(FAT16_DRV_CONTEXT)))) {
		return E_OUTOFMEM;
	}

	c = drv->priv_data;

	hr = k_fopen(storage_drv, FILE_OPEN_READ, &c->storage_drv);
	if (FAILED(hr)) return hr;

	/* Read BPB info */
	hr = fat16_parse_bpb(drv);
	if (FAILED(hr)) return E_FAIL;

	/* Cache FAT table */
	hr = fat16_cache_fat_table(drv);
	if (FAILED(hr)) return hr;

	/* Assign FS ops */
	drv->create 	= fat16_create;
	drv->open 		= fat16_open;
	drv->mkdir 		= fat16_mkdir;
	drv->opendir 	= fat16_opendir;
	drv->finalize 	= fat16_fini;

	/* Avoid warning for unused function */
	UNUSED_ARG(fat_get_subitem_count); //TODO: remove if used.

	return S_OK;
}

/*
 * Cleans up before the FS driver is being destroyed.
 */
static HRESULT
fat16_fini(K_FS_DRIVER *drv)
{
	FAT16_DRV_CONTEXT 	*ctx;

	if (drv->priv_data == NULL) {
		return E_FAIL;
	}

	ctx = drv->priv_data;

	/* Free FAT cache */
	if (ctx->fat_cache) {
		kfree(ctx->fat_cache);
	}

	/* Close storage driver handle */
	if (ctx->storage_drv != NULL) {
		k_fclose(&ctx->storage_drv);
	}

	/* Free driver context */
	kfree(drv->priv_data);

	return S_OK;
}

K_FS_CONSTRUCTOR fat16_get_constructor()
{
	return fat16_init;
}
