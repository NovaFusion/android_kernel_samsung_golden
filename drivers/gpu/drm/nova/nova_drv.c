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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <video/mcde.h>

#include "nova_drm_priv.h"

static struct drm_framebuffer *
nova_drm_fb_create(struct drm_device *drmdev,
		   struct drm_file *filp,
		   struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *bo;
	struct drm_framebuffer *fb;

	DRM_DEBUG_DRIVER("\n");

	bo = drm_gem_object_lookup(drmdev, filp, mode_cmd->handle);
	if (!bo)
		return ERR_PTR(-ENOENT);

	fb = nova_fb_create(drmdev, mode_cmd, bo);
	if (IS_ERR(fb))
		drm_gem_object_unreference_unlocked(bo);

	return fb;
}

static /*const*/ struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = nova_drm_fb_create,
};

static int kms_init(struct nova_drm_device *ndrmdev)
{
	struct drm_device *drmdev = ndrmdev->drmdev;
	int i, n, ret;

	ret = drm_gem_init(drmdev);
	if (ret)
		return ret;

	drm_mode_config_init(drmdev);
	drmdev->mode_config.funcs = &mode_config_funcs;
	drmdev->mode_config.max_width  = MCDE_MAX_WIDTH;
	drmdev->mode_config.max_height = MCDE_MAX_HEIGHT;

	nova_drm_dss_register_devices(ndrmdev);

	n = mcde_get_num_channels(drmdev->platformdev);
	for (i = 0; i < n; i++) {
		ret = nova_crtc_create(ndrmdev, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int
nova_drm_load(struct drm_device *drmdev, unsigned long chipset)
{
	int ret;
	struct nova_drm_device *ndrmdev;

	DRM_DEBUG_DRIVER("\n");

	ndrmdev = kzalloc(sizeof(struct nova_drm_device), GFP_KERNEL);
	if (ndrmdev == NULL)
		return -ENOMEM;

	drmdev->dev_private = ndrmdev;
	ndrmdev->drmdev = drmdev;

	ret = kms_init(ndrmdev);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to initialize MCDE KMS\n");
		goto fail_kms;
	}

	ret = nova_drm_fbdev_init(ndrmdev);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to initialize fbdev\n");
		goto fail_fbdev;
	}

	return 0;
fail_fbdev:
	/* FIXME: kms_exit(mdev); */
fail_kms:
	kfree(ndrmdev);
	return ret;
}

static struct drm_driver mcde_drm_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_MODESET |
							DRIVER_BUS_PLATFORM,
	.load = nova_drm_load,
	.fops = {
		 .owner = THIS_MODULE,
		 .open = drm_open,
		 .release = drm_release,
		 .unlocked_ioctl = drm_ioctl,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
	},
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init nova_drm_init(void)
{
	int ret;
	struct device *mcde;

	DRM_DEBUG_DRIVER("\n");

	mcde = bus_find_device_by_name(&platform_bus_type, NULL, "mcde");
	if (!mcde) {
		DRM_ERROR("No mcde device probed\n");
		return -ENODEV;
	}
	mcde->coherent_dma_mask = DMA_BIT_MASK(32);
	nova_gem_init(&mcde_drm_driver);
	ret = drm_platform_init(&mcde_drm_driver, to_platform_device(mcde));
	if (ret)
		DRM_ERROR("drm platform init failed\n");
	return ret;
}
late_initcall(nova_drm_init);

MODULE_AUTHOR("Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson MCDE DRM/KMS driver");

