/*
 * kstream.h
 *
 *  Created on: 19.05.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_KSTREAM_H_
#define INCLUDE_KSTREAM_H_

#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "syncobjs.h"

/* Define values for seek origin */
#define	KSTREAM_ORIGIN_CURRENT		0x00
#define	KSTREAM_ORIGIN_BEGINNING	0x01
#define	KSTREAM_ORIGIN_END			0x02

#define PATH_DELIMITER	'/'
#define MAX_FILENAME_LENGTH	255
#define MAX_DIRNAME_LENGTH	255

#define FS_NODE_TYPE_FILE        0x01
#define FS_NODE_TYPE_DIRECTORY   0x02
#define FS_NODE_TYPE_CHARDEVICE  0x04
#define FS_NODE_TYPE_BLOCKDEVICE 0x08
#define FS_NODE_TYPE_PIPE        0x10
#define FS_NODE_TYPE_SYMLINK     0x20
#define FS_NODE_TYPE_MOUNTPOINT  0x40

#define FILE_OPEN_READ		0x01
#define FILE_OPEN_WRITE		0x02
#define	FILE_OPEN_READWRITE	(FILE_OPEN_READ | FILE_OPEN_WRITE)

#define NODE_MODE_ALL_RWE	  0xFFFF

/**
 * NTX base stream. It's purpose is to be used by the kernel for different kind
 * of data streaming, like accessing files, devices, pipes and probably other resources.
 */
typedef struct K_STREAM K_STREAM;
struct K_STREAM {
	/**
	 * Access mode
	 */
	uint32_t mode;

	/**
	 * Position marker
	 */
	uint32_t pos;

	/**
	 * Filename\URL used when opening this stream.
	 */
	char *filename;

	/**
	 * Reads 'block_size' bytes from the stream to the output buffer, pointed by 'out_buf'
	 * and moves the position marker accordingly.
	 *
	 * @return Number of bytes read.
	 */
	HRESULT (*read)(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read);

	/**
	 * Writes 'block_size' bytes to the stream from the input buffer, pointed by 'in_buf'.
	 * @return Number of bytes written.
	 */
	HRESULT (*write)(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written);

	/**
	 * Adjusts the stream position marker to an arbitrary position.
	 *
	 * @return Returns the new position on success, and -1 on failure.
	 */
	uint32_t (*seek)(K_STREAM *str, int64_t pos, int8_t origin);

	/**
	 * Retrieves the position of the stream's position marker. If the stream does not support
	 * positioning, then this function will return -1
	 *
	 * @return On success, returns the stream position.
	 */
	uint32_t (*tell)(K_STREAM *str);

	/**
	 * Performs device specific operations and it is supported only by devices.
	 * Call ioctrl() on files will return error code E_FAIL.
	 */
	HRESULT (*ioctl)(K_STREAM *s, uint32_t code, void *arg);

	/**
	 * Closes a kernel file stream handle
	 */
	HRESULT (*close)(K_STREAM **str);

	/**
	 * Private data, which is attached by an implementation-specific stream
	 * handler.
	 */
	void *priv_data;

	/**
	 * Pointer to a file system driver governing this stream. Can be NULL.
	 */
	void *fs_driver;
	void *opaque_data;

	K_MUTEX lock;
};

typedef struct {
	size_t size;
	uint32_t node_type;
} K_FS_NODE_INFO;

/**
 * Directory stream. Holds a collection of retrieved directory sub-items by opendir()
 */
typedef struct K_DIR_STREAM K_DIR_STREAM;
struct K_DIR_STREAM {
	/**  Number of sub-items */
	int32_t len;

	/** Position of the next sub-item which is about to be read by readdir() */
	int32_t pos;

	/** Array of all the items' names retrieved by opendir() */
	char **names;

	/** Array of additional info for each item */
	K_FS_NODE_INFO *info;

	/** Name of the directory being read */
	char *dirname;

	//readdir
	HRESULT (*readdir)(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info);

	//rewind
	HRESULT (*rewinddir)(K_DIR_STREAM *dirstr);

	//closedir
	HRESULT (*closedir)(K_DIR_STREAM **dirstr);

	/** Data assigned by FS driver */
	void *priv_data;
};

#endif /* INCLUDE_KSTREAM_H_ */
