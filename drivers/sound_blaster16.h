/*
 * sound_blaster16.h
 *
 *  Created on: 8.09.2016 ã.
 *      Author: Admin
 */

#ifndef DRIVERS_SOUND_BLASTER16_H_
#define DRIVERS_SOUND_BLASTER16_H_

#include <devices.h>
#include <../subsystems/nxa.h>

/** Alias for K_AUDIO_FORMAT */
typedef K_AUDIO_FORMAT SB16_AUDIO_FORMAT;

/* Installs driver */
HRESULT __nxapi sb16_install();

/* Uninstall driver */
HRESULT __nxapi sb16_uninstall();

#endif /* DRIVERS_SOUND_BLASTER16_H_ */
