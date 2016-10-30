/*
 * vfs.h
 *
 *  Created on: 17.05.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_VFS_H_
#define INCLUDE_VFS_H_

#include <stdio.h>
#include "types.h"
#include "devices.h"
#include "kstream.h"
#include "syncobjs.h"

#define VFS_PATH_DELIMITER		(PATH_DELIMITER)
#define VFS_MAX_FILENAME_LENGTH	(MAX_FILENAME_LENGTH)
#define VFS_MAX_DIRNAME_LENGTH	(MAX_DIRNAME_LENGTH)

/**
 * @brief ANTONIX Virtual File System.
 *
 * A very simple (at this point) hierarchical virtual file system, allowing (virtual) files/directories and
 * mount-points. It is intended to hold physical (such that exist on hard-disc/floppy-disc) file-systems and
 * device drivers.
 *
 * We will adopt the UNIX file-path convention, i.e "/path/to/location/filename.ext".
 */
#define NODE_TYPE_FILE        FS_NODE_TYPE_FILE
#define NODE_TYPE_DIRECTORY   FS_NODE_TYPE_DIRECTORY
#define NODE_TYPE_CHARDEVICE  FS_NODE_TYPE_CHARDEVICE
#define NODE_TYPE_BLOCKDEVICE FS_NODE_TYPE_BLOCKDEVICE
#define NODE_TYPE_PIPE        FS_NODE_TYPE_PIPE
#define NODE_TYPE_SYMLINK     FS_NODE_TYPE_SYMLINK
#define NODE_TYPE_MOUNTPOINT  FS_NODE_TYPE_MOUNTPOINT

/**
 * NTX Filesystem drivers should provide the following interface
 */
typedef struct K_FS_DRIVER K_FS_DRIVER;
struct K_FS_DRIVER {
	/** Creates new file */
	HRESULT (*create)(K_FS_DRIVER *self, char *filename, uint32_t perm);

	//fopen
	/** Open handle to a kernel file stream */
	HRESULT (*open)(K_FS_DRIVER *self, char *filename, uint32_t flags, K_STREAM **out);

	/** Open handle to a kernel directory stream */
	HRESULT (*opendir)(K_FS_DRIVER *self, char *dirname, K_DIR_STREAM **out);

	/** Create new directory */
	HRESULT (*mkdir)(K_FS_DRIVER *self, char *path, uint32_t mode);

	/** File system destructor (called when unmounting) */
	HRESULT (*finalize)(K_FS_DRIVER *self);

	void *priv_data;
};

/*
 * File system constructor function.
 */
typedef HRESULT (*K_FS_CONSTRUCTOR)(K_FS_DRIVER *self, char *storage_drv);

/**
 * Virtual file system node descriptor.
 */
typedef struct {
	/** Unique id of the node */
	int64_t uid;

	int8_t 	index;

	/** Name of the node */
	char	name[256];

	/** Type of the node - NODE_TYPE_* */
	int8_t 	type;

	/** Permission flags for accessing the node */
	uint32_t 	permission;

	/** Pointer to the file-system driver, governing this node */
	K_FS_DRIVER	*fs_driver;

	/** Size of the node (used only for files) */
	uint64_t 	size;
} K_VFS_NODE_DESC;

typedef struct K_VFS_NODE K_VFS_NODE;
struct K_VFS_NODE {
	/* A structure describing some characteristics of the node */
	K_VFS_NODE_DESC desc;

	/* Node children */
	K_VFS_NODE	**children;
	int32_t		child_cnt;
	int32_t		child_capacity;

	/* Content (for files only). For devices, this field holds
	 * pointer to K_DEVICE struct
	 */
	void *content;
	uint32_t content_capacity;

	/* Number of references. When a node is being opened, it increases the reference
	 * counter. And of course, ref. counter is decremented on close.
	 */
	uint32_t ref_count;

	/* Parent node */
	K_VFS_NODE	*parent;

	/* Mutex used to serialize access to node */
	K_MUTEX		lock;
};

typedef struct {
	K_VFS_NODE root;
} K_VFS_SYSTEM;

/*
 * Helper macro for extracting pointer to device specific data from
 * K_STREAM stream.
 */
static inline void* GET_DRV_CTX(K_STREAM *stream)
{
	K_VFS_NODE *n = stream->priv_data;
	return ((K_DEVICE*)(n->content))->opaque;
}

/**
 * Initializes the Virtual File System
 */
HRESULT vfs_init();
HRESULT vfs_opendir(K_FS_DRIVER *self, char *dirname, K_DIR_STREAM **out);
HRESULT vfs_mkdir(K_FS_DRIVER *self, char *path, uint32_t mode);
HRESULT vfs_create(K_FS_DRIVER *self, char *filename, uint32_t perm);
HRESULT vfs_open(K_FS_DRIVER *self, char *filename, uint32_t flags, K_STREAM **out);

/* Directory stream routines */
HRESULT vfs_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info);
HRESULT vfs_rewinddir(K_DIR_STREAM *dirstr);
HRESULT vfs_closedir(K_DIR_STREAM **dirstr);

/* File stream routine s*/
HRESULT vfs_file_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);
HRESULT vfs_file_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);
HRESULT vfs_file_ioctl(K_STREAM *s, uint32_t code, void *arg);
uint32_t vfs_file_seek(K_STREAM *s, int64_t pos, int8_t origin);
uint32_t vfs_file_tell(K_STREAM *s);
HRESULT vfs_close(K_STREAM **str);

/**
 * Mounts a device onto the VFS.
 */
HRESULT vfs_mount_device(K_DEVICE *dev, char *path);
HRESULT vfs_unmount_device(char *path);

/**
 * Mounts a file system onto the VFS on a specific path.
 */
HRESULT vfs_mount_fs(char *mount_path, K_FS_CONSTRUCTOR fs_ctor, char *storage_drv);
HRESULT vfs_unmount_fs(char *mount_path);

HRESULT vfs_parse_url(char *dirname, K_VFS_NODE *vfs_root, K_VFS_NODE **out, uint32_t *is_crossing_mountpoint, char *base_path, char *mounted_path);

K_FS_DRIVER *vfs_get_driver();
HRESULT vfs_selftest();

/**
 * Initializes the initial ram disc (initrd).
 * @note Routine is implemented in initrd.c
 */
HRESULT initrd_init();

#endif /* INCLUDE_VFS_H_ */
