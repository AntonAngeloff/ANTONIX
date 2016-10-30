/*
 * ioctl_def.h
 *
 *	Definitions of IOCTL codes and related structs.
 *
 *  Created on: 19.09.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef INCLUDE_IOCTL_DEF_H_
#define INCLUDE_IOCTL_DEF_H_

/** DEV_OPEN command is issued to the driver when it is being opened by the VFS. */
#define DEVIO_OPEN			0x1
#define IOCTL_DEVICE_OPEN	0x01
/** DEVIO_CLOSE is issued by the VFS, right before closing the device stream */
#define DEVIO_CLOSE			0x2
#define IOCTL_DEVICE_CLOSE	0x02

/*
 * Beginning of IOCTL codes AUDIO drivers
 */
#define IOCTL_AUDIO						0x100

//Begins a playback session
#define	IOCTL_AUDIO_BEGIN_PLAYBACK		(IOCTL_AUDIO + 0x00)

//Stops playback
#define	IOCTL_AUDIO_STOP_PLAYBACK		(IOCTL_AUDIO + 0x01)

//Returns the free space in the buffer (works both for reading and writing). Arg is *uint32_t
#define	IOCTL_AUDIO_BUFFER_FREE_SIZE	(IOCTL_AUDIO + 0x02)

//Returns the state of the player (playing, stopped, recording, etc). Arg is *uint32_t
#define	IOCTL_AUDIO_GET_PLAYER_STATE	(IOCTL_AUDIO + 0x03)

//Sets input/output audio format (use K_AUDIO_FORMAT)
#define IOCTL_AUDIO_SET_FORMAT			(IOCTL_AUDIO + 0x04)

//Gets input/output audio format (arg uses K_AUDIO_FORMAT)
#define IOCTL_AUDIO_GET_FORMAT			(IOCTL_AUDIO + 0x05)

//Returns the size of buffered audio
#define	IOCTL_AUDIO_GET_BUFFERED_SIZE	(IOCTL_AUDIO + 0x06)

/*
 * Beginning of IOCTL codes STORAGE drivers
 */
#define IOCTL_STORAGE 					0x120

//Read blocks (uses IOCTL_STORAGE_READWRITE)
#define IOCTL_STORAGE_READ_BLOCKS		(IOCTL_STORAGE + 0x00)
//Write blocks (uses IOCTL_STORAGE_READWRITE)
#define IOCTL_STORAGE_WRITE_BLOCKS		(IOCTL_STORAGE + 0x01)
//Returns size in bytes of a single block (use uint32_t for arg)
#define IOCTL_STORAGE_GET_BLOCK_SIZE	(IOCTL_STORAGE + 0x02)
//Returns capacity of device in blocks (use uint32_t for arg)
#define IOCTL_STORAGE_GET_BLOCK_COUNT	(IOCTL_STORAGE + 0x03)

/*
 * IOCTL codes for GRAPHICS drivers
 */
#define IOCTL_GRAPHICS 					0x200
#define IOCTL_GRAPHICS_GET_INTERFACE	(IOCTL_GRAPHICS + 0x01)

/*
 * IOCTL codes for POINTING devices (mice and touchpads)
 */
#define IOCTL_POINTING					0x300
#define IOCTL_POINTING_REGISTER			(IOCTL_POINTING + 0x01)
#define IOCTL_POINTING_UNREGISTER		(IOCTL_POINTING + 0x02)
#define IOCTL_POINTING_SET_SENSITIVITY	(IOCTL_POINTING + 0x03)
#define IOCTL_POINTING_GET_SENSITIVITY	(IOCTL_POINTING + 0x04)

/**
 * Device-specific IOCTL calls should range from DEVIO_CUSTOM up
 */
#define IOCTL_PECULIAR	0x500

/**
 * Used for read/write block operations of STORAGE drivers.
 */
typedef struct {
	uint32_t	start;
	uint32_t	count;
	void		*buffer;
} IOCTL_STORAGE_READWRITE, *PIOCTL_STORAGE_READWRITE;

#endif /* INCLUDE_IOCTL_DEF_H_ */
