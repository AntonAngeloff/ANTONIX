/*
 * nxa.c
 *
 *	ANTONIX Audio Subsystem.
 *
 *	Since at this time we haven't implemented a kernel mixer, only one audio
 *	session could be played at the same time.
 *
 *	Also we don't support multiple audio cards at this time.
 *
 *  Created on: 11.09.2016 ã.
 *      Author: Anton Angelov
 */
#include "nxa.h"
#include <kstdio.h>
#include <devices.h>
#include <string.h>
#include <mm.h>

static K_STREAM *aud_driver = NULL;

HRESULT __nxapi nxa_initialize(char *audio_driver)
{
	char drv_name[64];

	if (aud_driver != NULL) {
		/* Already opened */
		return E_FAIL;
	}

	if (audio_driver == NULL) {
		/* Auto-detect audio driver. We could iterate all installed sound drivers
		 * to select a proper candidate. Though we don't have such kind of functionalities
		 * we will hardcode sound blaster 16 driver.
		 */
		strcpy(drv_name, "/dev/sb16");
	}else {
		strcpy(drv_name, audio_driver);
	}

	/* Open the driver */
	HRESULT hr = k_fopen(drv_name, FILE_OPEN_READ, &aud_driver);
	return hr;
}

HRESULT __nxapi nxa_finalize()
{
	if (aud_driver == NULL) {
		/* Already opened */
		return E_FAIL;
	}

	HRESULT hr = k_fclose(&aud_driver);
	return hr;
}

HRESULT __nxapi nxa_open_session(K_AUDIO_FORMAT *fmt, void *preroll_buff, uint32_t preroll_size, K_AUDIO_SESSION **session)
{
	K_AUDIO_SESSION *ses;
	HRESULT 		hr;

	/* Set fmt as native format on the driver. Changing the
	 * native format causes buffer flush.
	 *
	 * Notice that we don't have a mechanism to prevent multiple session
	 * opening.
	 */
	hr = k_ioctl(aud_driver, IOCTL_AUDIO_SET_FORMAT, fmt);
	if (FAILED(hr)) return hr;

	/* Preroll */
	if (preroll_size > 0) {
		hr = k_fwrite(aud_driver, preroll_size, preroll_buff, NULL);
		if (FAILED(hr)) return hr;
	}

	/* Begin playback */
	hr = k_ioctl(aud_driver, IOCTL_AUDIO_BEGIN_PLAYBACK, NULL);
	if (FAILED(hr)) return hr;

	/* Allocate and populate session struct */
	ses = kcalloc(sizeof(K_AUDIO_SESSION));

	ses->driver = aud_driver;
	ses->fmt = *fmt;
	*session = ses;

	/* Retrieve back format. We do this because the driver might
	 * modify some struct fields.
	 */
	hr = k_ioctl(aud_driver, IOCTL_AUDIO_GET_FORMAT, &ses->fmt);
	return hr;
}

HRESULT __nxapi nxa_queue_audio(K_AUDIO_SESSION *session, void *buffer, uint32_t buff_size)
{
	if (buff_size == 0 || buffer == NULL) {
		return E_INVALIDARG;
	}

	return k_fwrite(session->driver, buff_size, buffer, NULL);
}

HRESULT __nxapi nxa_get_buffered_audio_size(K_AUDIO_SESSION *session, uint32_t *size)
{
	if (size == NULL) {
		return E_POINTER;
	}

	return k_ioctl(session->driver, IOCTL_AUDIO_GET_BUFFERED_SIZE, (void*)size);
}

HRESULT __nxapi nxa_get_free_buffer_space(K_AUDIO_SESSION *session, uint32_t *size)
{
	if (size == NULL) {
		return E_POINTER;
	}

	return k_ioctl(session->driver, IOCTL_AUDIO_BUFFER_FREE_SIZE, (void*)size);
}

HRESULT __nxapi nxa_get_format(K_AUDIO_SESSION *session, K_AUDIO_FORMAT *fmt)
{
	*fmt = session->fmt;
	return S_OK;
}

HRESULT __nxapi nxa_set_volume(K_AUDIO_SESSION *session, uint32_t flags, float volume)
{
	UNUSED_ARG(session);
	UNUSED_ARG(flags);
	UNUSED_ARG(volume);

	return E_NOTIMPL;
}

HRESULT __nxapi nxa_get_volume(K_AUDIO_SESSION *session, uint32_t flags, float *volume)
{
	UNUSED_ARG(session);
	UNUSED_ARG(flags);
	UNUSED_ARG(volume);

	return E_NOTIMPL;
}

HRESULT __nxapi nxa_samples_to_bytes(K_AUDIO_SESSION *session, uint32_t sample_cnt, uint32_t *bytes)
{
	*bytes = sample_cnt * session->fmt.block_size;
	return S_OK;
}

HRESULT __nxapi nxa_close_session(K_AUDIO_SESSION **session, uint32_t wait_buffer_exhaust)
{
	HRESULT 	hr;
	uint32_t	size;

	/* Wait for audio buffer to exhaust */
	if (wait_buffer_exhaust) {
		do {
			hr = nxa_get_buffered_audio_size(*session, &size);
			if (FAILED(hr)) return hr;
		} while (size > 0);
	}

	/* Stop playback */
	hr = k_ioctl((*session)->driver, IOCTL_AUDIO_STOP_PLAYBACK, NULL);
	if (FAILED(hr)) return hr;

	/* Free session struct */
	kfree(*session);

	*session = NULL;
	return S_OK;
}
