/*
 * iso9660.h
 *
 *	ISO 9660 file system driver for ANTONIX
 *
 *  Created on: 12.11.2016 ã.
 *      Author: Anton Angelov
 */
#include <kstream.h>
#include <vfs.h>

#ifndef DRIVERS_ISO9660_H_
#define DRIVERS_ISO9660_H_

#define ISO_VD_BOOT_RECORD		0x00
#define ISO_VD_PRIMARY			0x01
#define ISO_VD_SUPPLEMENTARY	0x02
#define ISO_VD_PARTITION_TABLE	0x03
#define ISO_VD_SET_TERMINATOR	0xFF

#define ISO_VD_START_SECTOR		0x10

#define ISO9660_SECTOR_SIZE		2048

/* ISO9660-specific data types */
typedef struct {
	uint16_t lsb;
	uint16_t msb;
} __packed uint16_dual_t;

typedef struct {
	int16_t lsb;
	int16_t msb;
} __packed int16_dual_t;

typedef struct {
	uint32_t lsb;
	uint32_t msb;
} __packed uint32_dual_t;

typedef struct {
	int32_t lsb;
	int32_t msb;
} __packed int32_dual_t;

typedef struct ISO9660_VOLUME_DESC_HEADER ISO9660_VOLUME_DESC_HEADER;
struct ISO9660_VOLUME_DESC_HEADER {
	uint8_t	type;
	char magic[5]; //strA
	uint8_t	version;
} __packed;

typedef struct ISO9660_DATETIME ISO9660_DATETIME;
struct ISO9660_DATETIME {
	uint8_t	year[4]; //strD
	uint8_t	month[2]; //strD
	uint8_t	day[2]; //strD
	uint8_t	hour[2]; //strD
	uint8_t	minute[2]; //strD
	uint8_t	second[2]; //strD
	uint8_t	hundreth[2]; //strD

	/* Time offset from GMT in 15-minute intervals */
	uint8_t	time_zone_offs;
} __packed;

typedef struct ISO9660_DATE ISO9660_DATE;
struct ISO9660_DATE {
	uint8_t	year;
	uint8_t	month;
	uint8_t	day;
	uint8_t	hour;
	uint8_t	minute;
	uint8_t	second;
	int8_t	timezone;
} __packed;

typedef struct ISO9660_DATETIME_BIN ISO9660_DATETIME_BIN;
struct ISO9660_DATETIME_BIN {
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	int8_t timezone;
} __packed;

typedef struct ISO_PRIMARY_VOLUME_DESC ISO_PRIMARY_VOLUME_DESC;
struct ISO_PRIMARY_VOLUME_DESC {
	ISO9660_VOLUME_DESC_HEADER header;

	uint8_t			reserved0;
	uint8_t			system_id[32]; //strA
	uint8_t			volume_id[32]; //strD
	uint8_t			reserved1[8];
	uint32_dual_t	volume_space_size;
	uint8_t			reserved2[32];
	uint16_dual_t	volume_set_size;
	uint16_dual_t	volume_sequence_size;
	uint16_dual_t	logical_block_size;
	uint32_dual_t	path_table_size;

	uint32_t		path_table_lsb;
	uint32_t		path_table_optional_lsb;
	uint32_t		path_table_msb;
	uint32_t		path_table_optional_msb;

	uint8_t			root_dir_entry[34];

	uint8_t			volume_set_id[128]; //strD
	uint8_t			publisher_id[128]; //strA
	uint8_t			data_prepare_id[128]; //strA
	uint8_t			application_id[128]; //strA

	uint8_t			copyright_file_id[38]; //strD
	uint8_t			abstract_file_id[36]; //strD
	uint8_t			bibliographic_file_id[37]; //strD

	ISO9660_DATETIME	volume_creation_time;
	ISO9660_DATETIME	volume_mod_time;
	ISO9660_DATETIME	volume_expiration_time;
	ISO9660_DATETIME	volume_effective_time;

	uint8_t			file_struct_version;
	uint8_t			reserved3;
} __packed;

typedef struct ISO9660_DRV_CONTEXT ISO9660_DRV_CONTEXT ;
struct ISO9660_DRV_CONTEXT {
	/** Handle to storage driver */
	K_STREAM 	*storage_drv;

	/** Size of single sector on target storage device */
	uint32_t	block_size;

	/** Primary volume descriptor */
	ISO_PRIMARY_VOLUME_DESC *pvd;
} __packed;

typedef struct ISO9660_DIR_ENTRY ISO9660_DIR_ENTRY;
struct ISO9660_DIR_ENTRY {
	uint8_t			length;
	uint8_t			extended_length;
	uint32_dual_t	extent_start;
	uint32_dual_t	extent_size;

	ISO9660_DATE	recording_time;
	uint8_t			flags;

	uint8_t			il_units;
	uint8_t			il_gap;

	uint16_dual_t	volume_seq;

	uint8_t 		name_length;
	char 			name[];
} __packed;

typedef struct ISO9660_STR_CONTEXT ISO9660_STR_CONTEXT;
struct ISO9660_STR_CONTEXT {
	ISO9660_DIR_ENTRY entry;

	/* Cache fields */
	uint32_t 	cache_cap;
	uint32_t 	cache_start_addr;
	uint32_t 	cache_size;
	uint8_t		*cache_buff;
	uint8_t		cache_invalid;
};

K_FS_CONSTRUCTOR iso9660_get_constructor();

#endif /* DRIVERS_ISO9660_H_ */
