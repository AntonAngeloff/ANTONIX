/*
 * nxa.h
 *
 *	ANTONIX Audio Subsystem
 *
 *  Created on: 11.09.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_NXA_H_
#define SUBSYSTEMS_NXA_H_

#include <types.h>
#include <stdint.h>
#include <kstream.h>

/**
 * Describes audio format for all audio drivers and APIs.
 */
typedef struct K_AUDIO_FORMAT K_AUDIO_FORMAT;
struct K_AUDIO_FORMAT {
	/**
	 * Size of a single audio sample in bits.
	 */
	uint32_t bit_depth;

	/**
	 * Size of interleaved audio block (in bytes). We don't support
	 * planar audio. This value is ignored when setting format to the
	 * driver (set it to 0).
	 */
	uint32_t block_size;

	/**
	 * Number of samples per second
	 */
	uint32_t sample_rate;

	/**
	 * Number of audio channels
	 */
	uint32_t channels;
};

typedef struct K_AUDIO_SESSION K_AUDIO_SESSION;
struct K_AUDIO_SESSION {
	K_AUDIO_FORMAT fmt;
	K_STREAM 	*driver;
};

/**
 * Opens a playback session. When opened, the caller have to periodically
 * call nxa_queue_audio() to provide new audio samples. The samples have to
 * match the format specified by `fmt`.
 *
 * Prerolling is optional. If not used, `NULL` and `0` have to be assigned
 * to `preroll_buff` and `preroll_size` respectively.
 */
HRESULT __nxapi nxa_open_session(K_AUDIO_FORMAT *fmt, void *preroll_buff, uint32_t preroll_size, K_AUDIO_SESSION **session);

/**
 * Queues samples to the audio buffer.
 */
HRESULT __nxapi nxa_queue_audio(K_AUDIO_SESSION *session, void *buffer, uint32_t buff_size);

/**
 * Returns amount of pending audio data (in bytes) present in the buffer.
 */
HRESULT __nxapi nxa_get_buffered_audio_size(K_AUDIO_SESSION *session, uint32_t *size);

/**
 * Returns the size of the free section of the audio buffer.
 */
HRESULT __nxapi nxa_get_free_buffer_space(K_AUDIO_SESSION *session, uint32_t *size);

/**
 * Returns the format by which the session has been opened.
 */
HRESULT __nxapi nxa_get_format(K_AUDIO_SESSION *session, K_AUDIO_FORMAT *fmt);

/**
 * Sets volume for session.
 */
HRESULT __nxapi nxa_set_volume(K_AUDIO_SESSION *session, uint32_t flags, float volume);

/**
 * Gets current volume for session.
 */
HRESULT __nxapi nxa_get_volume(K_AUDIO_SESSION *session, uint32_t flags, float *volume);

/**
 * Converts a number of samples to number of bytes (based on the audio format
 * of the session).
 */
HRESULT __nxapi nxa_samples_to_bytes(K_AUDIO_SESSION *session, uint32_t sample_cnt, uint32_t *bytes);

/**
 * Closes a playback session.
 */
HRESULT __nxapi nxa_close_session(K_AUDIO_SESSION **session, uint32_t wait_buffer_exhaust);


/**
 * Initializes the audio subsystem
 */
HRESULT __nxapi nxa_initialize(char *audio_driver);

/**
 * Shuts down the audio subsystem
 */
HRESULT __nxapi nxa_finalize();

#endif /* SUBSYSTEMS_NXA_H_ */
