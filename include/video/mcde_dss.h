/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * ST-Ericsson MCDE display sub system driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __MCDE_DSS__H__
#define __MCDE_DSS__H__

#include <linux/kobject.h>
#include <linux/notifier.h>

#include "mcde.h"
#include "mcde_display.h"

/* Public MCDE dss (Used by MCDE fb ioctl & MCDE display sysfs) */
int mcde_dss_open_channel(struct mcde_display_device *ddev);
void mcde_dss_close_channel(struct mcde_display_device *ddev);
int mcde_dss_enable_display(struct mcde_display_device *ddev);
int mcde_dss_restart_display(struct mcde_display_device *ddev);
void mcde_dss_disable_display(struct mcde_display_device *ddev);
int mcde_dss_apply_channel(struct mcde_display_device *ddev);
struct mcde_overlay *mcde_dss_create_overlay(struct mcde_display_device *ddev,
	struct mcde_overlay_info *info);
void mcde_dss_destroy_overlay(struct mcde_overlay *ovl);
int mcde_dss_enable_overlay(struct mcde_overlay *ovl);
void mcde_dss_disable_overlay(struct mcde_overlay *ovl);
int mcde_dss_apply_overlay(struct mcde_overlay *ovl,
						struct mcde_overlay_info *info);
void mcde_dss_get_overlay_info(struct mcde_overlay *ovly,
				struct mcde_overlay_info *info);
int mcde_dss_update_overlay(struct mcde_overlay *ovl, bool tripple_buffer);

void mcde_dss_get_native_resolution(struct mcde_display_device *ddev,
	u16 *x_res, u16 *y_res);
enum mcde_ovl_pix_fmt mcde_dss_get_default_color_format(
	struct mcde_display_device *ddev);
void mcde_dss_get_physical_size(struct mcde_display_device *ddev,
	u16 *x_size, u16 *y_size); /* mm */

int mcde_dss_try_video_mode(struct mcde_display_device *ddev,
	struct mcde_video_mode *video_mode);
int mcde_dss_set_video_mode(struct mcde_display_device *ddev,
	struct mcde_video_mode *video_mode);
void mcde_dss_get_video_mode(struct mcde_display_device *ddev,
	struct mcde_video_mode *video_mode);

int mcde_dss_set_pixel_format(struct mcde_display_device *ddev,
	enum mcde_ovly_pix_fmt pix_fmt);
int mcde_dss_get_pixel_format(struct mcde_display_device *ddev);

int mcde_dss_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation);
enum mcde_display_rotation mcde_dss_get_rotation(
	struct mcde_display_device *ddev);

int mcde_dss_wait_for_vsync(struct mcde_display_device *ddev, s64 *timestamp);

bool mcde_dss_secure_output(struct mcde_display_device *ddev);

/* MCDE dss events */

/*      A display device and driver has been loaded, probed and bound */
#define MCDE_DSS_EVENT_DISPLAY_REGISTERED    1
/*      A display device has been removed */
#define MCDE_DSS_EVENT_DISPLAY_UNREGISTERED  2

/*      Note! Notifier callback will be called holding the dev sem */
int mcde_dss_register_notifier(struct notifier_block *nb);
int mcde_dss_unregister_notifier(struct notifier_block *nb);

/* MCDE dss driver */

int mcde_dss_init(void);
void mcde_dss_exit(void);

#endif /* __MCDE_DSS__H__ */

