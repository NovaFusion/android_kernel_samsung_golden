/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * ST-Ericsson MCDE DRM/KMS driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __NOVA_DRM_PRIV_H__
#define __NOVA_DRM_PRIV_H__

#include <drm/drmP.h>
#include <drm/drm_mode.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <video/mcde.h>

#define DRIVER_NAME		"nova"
#define DRIVER_DESC		"ST-Ericsson MCDE DRM/KMS driver"
#define DRIVER_DATE		"20120202"
#define DRIVER_MAJOR		0
#define DRIVER_MINOR		1
#define DRIVER_PATCHLEVEL	0
#define DRIVER_VERSION		__stringify(DRIVER_MAJOR) "." \
				__stringify(DRIVER_MINOR)

#define to_nova_crtc(x) container_of(x, struct nova_crtc, base)

enum nova_crtc_id {
	NOVA_CRTC_A   = 0x1,
	NOVA_CRTC_B   = 0x2,
	NOVA_CRTC_C0  = 0x4,
	NOVA_CRTC_C1  = 0x8,
	NOVA_CRTC_ANY = 0xF,
};

struct nova_drm_device {
	struct drm_device *drmdev;
	struct drm_fb_helper *helper;
};

/* crtc */
int nova_crtc_create(struct nova_drm_device *mdev, int i);

/* framebuffer */
struct drm_framebuffer *nova_fb_create(struct drm_device *drmdev,
		struct drm_mode_fb_cmd *mode, struct drm_gem_object *bo);
void nova_fb_destroy(struct drm_framebuffer *fb);
struct drm_gem_object *nova_fb_get_bo(struct drm_framebuffer *fb);

/* gem */
void nova_gem_init(struct drm_driver *drv);
struct drm_gem_object *nova_gem_create_object(struct drm_device *dev, u32 size);
phys_addr_t nova_gem_get_paddr(struct drm_gem_object *bo);
void *nova_gem_get_vaddr(struct drm_gem_object *bo);

/* fbdev */
int nova_drm_fbdev_init(struct nova_drm_device *ddev);

/* dss */
void nova_drm_dss_register_devices(struct nova_drm_device *ndrmdev);

/* display modes */
void nova_mode_to_mcde(struct drm_display_mode *m,
						struct mcde_video_mode *vmode);
void nova_mode_from_mcde(struct drm_display_mode *m,
						struct mcde_video_mode *vmode);

/* FIXME: Move to MCDE driver */
static inline int mcde_get_num_channels(struct platform_device *dev)
{
	return 2;
}
extern struct bus_type mcde_bus_type;

#endif /* __NOVA_DRM_PRIV_H__ */

