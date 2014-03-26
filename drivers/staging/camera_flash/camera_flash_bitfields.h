/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation. 
 */
/**
* \file    camera_flash_bitfields.h
* \brief   Define some constants for the flash drivers API.
* \author  ST-Ericsson
*/
#ifndef __CAMERA_FLASH_BITFIELDS_H__
#define __CAMERA_FLASH_BITFIELDS_H__

/* Flash Mode definitions */
/* All Operating Modes are off (shutdown low power state)*/
#define FLASH_MODE_NONE				(0x000)
/* Enables the xenon driver. Strobe is managed by the flash driver itself.
Charges the xenon. Automatic periodic recharge is abstracted by the driver */
#define FLASH_MODE_XENON			(0x001)
/* Enables the xenon driver. Strobe is managed externally to the driver */
#define FLASH_MODE_XENON_EXTERNAL_STROBE	(0x002)
/* Enables the video led driver. Strobing is managed by the driver */
#define FLASH_MODE_VIDEO_LED			(0x004)
/* Enables the video led driver. Strobing is managed externally to driver */
#define FLASH_MODE_VIDEO_LED_EXTERNAL_STROBE	(0x008)
/* Enables the still LED driver. Strobing is managed by the driver itself */
#define FLASH_MODE_STILL_LED			(0x010)
/* Enables the still LED driver. Strobe is managed externally to the driver */
#define FLASH_MODE_STILL_LED_EXTERNAL_STROBE	(0x020)
/* Enables the AF assistant driver. Strobe is managed by the driver */
#define FLASH_MODE_AF_ASSISTANT			(0x040)
/* Enable the driver. Strobe is managed by the driver */
#define FLASH_MODE_INDICATOR			(0x080)
/* Enables the still HP LED driver. Strobing is managed by the driver itself */
#define FLASH_MODE_STILL_HPLED			(0x100)
/* Enables the still HP LED driver. Strobe is managed externally to the
driver */
#define FLASH_MODE_STILL_HPLED_EXTERNAL_STROBE	(0x200)


/* The flash is not usable anymore */
#define FLASH_STATUS_BROKEN     (0x00)
/* The flash is ready to be fired and unlit */
#define FLASH_STATUS_READY      (0x01)
/* The flash is discharged and by construction, charging; usually an
application shall not try to fire it in that state (although possible
typically in sport mode flash) */
#define FLASH_STATUS_NOT_READY  (0x02)
/* The flash is in shutdown state */
#define FLASH_STATUS_SHUTDOWN   (0x04)
/* Intermediate state that may exist where I2C registers can be programmed */
#define FLASH_STATUS_STANDBY    (0x08)
/* The flash is already strobing */
#define FLASH_STATUS_LIT        (0x10)

#define FLASH_SELFTEST_NONE              0x000
/* tests connections to flash driver ICs */
#define FLASH_SELFTEST_CONNECTION        0x001
/* tests capture flash without using strobe signal from camera */
#define FLASH_SELFTEST_FLASH             0x002
/* tests capture flash using strobe signal from camera: ONLY this one needs to
be done in idle state from flash tests cases */
#define FLASH_SELFTEST_FLASH_WITH_STROBE 0x004
/* tests video light */
#define FLASH_SELFTEST_VIDEO_LIGHT       0x008
/* tests AF assistance light */
#define FLASH_SELFTEST_AF_LIGHT          0x010
/* tests capture indicator light */
#define FLASH_SELFTEST_INDICATOR         0x020
/* tests flash in torch mode */
#define FLASH_SELFTEST_TORCH_LIGHT       0x040

/** \brief Flash Error */
enum TFlashError {
	FLASH_ERR_NONE		, /* None */
	FLASH_ERR_OVER_CHARGE	, /* Error happened during the charge */
	FLASH_ERR_OVER_HEAT	, /* Over temperature */
	FLASH_ERR_SHORT_CIRCUIT	, /* Short circuit */
	FLASH_ERR_TIMEOUT	, /* Timeout */
	FLASH_ERR_OVER_VOLTAGE	/* Over voltage */
} ;

#endif
