/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson Display overlay compositer device driver
 *
 * Author: Per-Daniel Olsson <per-daniel.olsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _CLONEDEV_H_
#define _CLONEDEV_H_

#if !defined(__KERNEL__) && !defined(_KERNEL)
#include <stdint.h>
#else
#include <linux/types.h>
#include <video/mcde.h>
#endif

#if defined(__KERNEL__) || defined(_KERNEL)
#include <linux/mm_types.h>
#include <linux/bitops.h>
#else
#define BIT(nr)			(1UL << (nr))
#endif

#define CLONEDEV_DEFAULT_DEVICE_PREFIX "clone"

/* Cloning mode */
enum clonedev_mode {
	CLONEDEV_CLONE_NONE,
	CLONEDEV_CLONE_VIDEO_OR_UI,
	CLONEDEV_CLONE_VIDEO_AND_UI,
	CLONEDEV_CLONE_VIDEO,
	CLONEDEV_CLONE_UI,
};

#define CLONEDEV_SET_MODE_IOC       _IOW('D', 1, __u32*)
#define CLONEDEV_SET_CROP_RATIO_IOC _IOW('D', 2, __u32*)

#ifdef __KERNEL__

int clonedev_create(void);
void clonedev_destroy(void);

#endif /* __KERNEL__ */

#endif /* _CLONEDEV_H_ */

