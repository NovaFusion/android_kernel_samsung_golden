/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson Display device driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _DISPDEV_H_
#define _DISPDEV_H_

#if !defined(__KERNEL__) && !defined(_KERNEL)
#include <stdint.h>
#else
#include <linux/types.h>
#include <video/mcde.h>
#endif

#define DISPDEV_DEFAULT_DEVICE_PREFIX "disp"

enum dispdev_fmt {
	DISPDEV_FMT_RGB565,
	DISPDEV_FMT_RGB888,
	DISPDEV_FMT_RGBX8888,
	DISPDEV_FMT_RGBA8888,
	DISPDEV_FMT_YUV422,
};

struct dispdev_config {
	uint16_t format;
	uint16_t stride;
	uint16_t x;
	uint16_t y;
	uint16_t z;
	uint16_t width;
	uint16_t height;

	uint32_t user_flags;
};

struct dispdev_buffer_info {
	uint16_t buf_idx;
	uint16_t display_update;
	struct dispdev_config buf_cfg;
};

#define DISPDEV_SET_CONFIG_IOC        _IOW('D', 1, struct dispdev_config)
#define DISPDEV_GET_CONFIG_IOC        _IOR('D', 2, struct dispdev_config)
#define DISPDEV_REGISTER_BUFFER_IOC   _IO('D', 3)
#define DISPDEV_UNREGISTER_BUFFER_IOC _IO('D', 4)
#define DISPDEV_QUEUE_BUFFER_IOC      _IOW('D', 5, struct dispdev_buffer_info)
#define DISPDEV_DEQUEUE_BUFFER_IOC    _IO('D', 6)

#ifdef __KERNEL__

int dispdev_create(struct mcde_display_device *ddev, bool overlay,
					struct mcde_overlay *parent_ovly);
void dispdev_destroy(struct mcde_display_device *ddev);

#endif /* __KERNEL__ */

#endif /* _DISPDEV_H_ */

