/*
 * fat16.h
 *
 *	Note:
 *		- This can potentially be expanded to support vFAT / FAT32, since there
 *		  are a lot of similarities.
 *
 *  Created on: 23.09.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef DRIVERS_FAT16_H_
#define DRIVERS_FAT16_H_

#include <types.h>
#include <vfs.h>
#include <kstream.h>

#define FS_TYPE_FAT12	0x01
#define FS_TYPE_FAT16	0x02
#define FS_TYPE_FAT32	0x03
#define FS_TYPE_EXFAT	0x04

/* Attribute flags for FAT16_DIR_ENTRY attribute field. */
#define FAT_ATTR_READONLY	0x01
#define FAT_ATTR_HIDDEN		0x02
#define FAT_ATTR_SYSTEM		0x04
#define FAT_ATTR_VOLUMEL	0x08
#define FAT_ATTR_DIRECTORY	0x10
#define FAT_ATTR_ARCHIVE	0x20
#define FAT_ATTR_DEVICE		0x60

/**
 * Describes a BIOS parameter block inside a FAT Boot
 * Sector.
 */
typedef struct FAT16_BPB FAT16_BPB;
struct FAT16_BPB {
	uint8_t		magic[3];
	uint8_t 	OEM_ID[8];
	uint16_t	bytes_per_sector;
	uint8_t		sectors_per_cluster;
	uint16_t	reserved_sectors;
	uint8_t		FAT_count;
	uint16_t	root_entries;
	uint16_t	num_sectors_16;
	uint8_t		media_descriptor;
	uint16_t	sectors_per_FAT;
	uint16_t	sectors_per_track;
	uint16_t	heads;
	uint32_t	hidden_sector;
	uint32_t	num_sectors_32;
} __packed;

/**
 * FAT12/16 Extended BIOS parameter block.
 */
typedef struct FAT16_EBPB FAT16_EBPB;
struct FAT16_EBPB {
	uint8_t		drive_number;
	uint8_t		flags;
	uint8_t		signature;
	uint32_t	volume_id;
	uint8_t		volume_label[11];
	uint8_t		fs_identifier[8];
} __packed;

/**
 * Stores metadata for files and directories.
 */
typedef struct FAT16_DIR_ENTRY FAT16_DIR_ENTRY;
struct FAT16_DIR_ENTRY {
	uint8_t		filename[8];
	uint8_t		ext[3];
	uint8_t		attributes;
	uint8_t		reserved;
	uint8_t		creation_time[5];
	uint16_t	last_access;
	uint16_t	first_clust_high;
	uint8_t		last_mod_time[4];
	uint16_t	first_clust_low;
	uint32_t	size;
} __packed;

typedef struct FAT16_DRV_CONTEXT FAT16_DRV_CONTEXT;
struct FAT16_DRV_CONTEXT {
	/** Handle to storage driver */
	K_STREAM 	*storage_drv;

	/* BPB / EBPB */
	FAT16_BPB	bpb;
	FAT16_EBPB	ebpb;

	/* File-system type denoted by FS_TYPE_* symbolic constant */
	uint32_t	fs_type;

	/* Memory cache of FAT table */
	uint8_t		*fat_cache;
	uint8_t		fat_cache_dirty;

	uint32_t	first_data_sector;
	uint32_t	first_fat_sector;
	uint32_t	first_root_sector;
	uint32_t	total_sectors;
	uint32_t	data_sector_cnt;
	uint32_t	total_cluster_cnt;
};


/**
 * Private structure assigned to kernel file streams.
 */
typedef struct FAT16_STR_CONTEXT FAT16_STR_CONTEXT;
struct FAT16_STR_CONTEXT {
	FAT16_DIR_ENTRY entry;

	/* Cache */
	uint32_t 	cache_cap;
	uint32_t 	cache_start_addr;
	uint32_t 	cache_size;
	uint8_t		*cache_buff;
	uint8_t		cache_invalid;
};

/**
 * Retrieves file system constructor for this driver.
 * It is used by the VFS when mounting a storage device.
 */
K_FS_CONSTRUCTOR fat16_get_constructor();

#endif /* DRIVERS_FAT16_H_ */
