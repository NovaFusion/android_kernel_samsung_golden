/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson MCDE DRM/KMS driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_mode.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>

#include "nova_drm_priv.h"

#define to_nova_fbdev(__x) container_of(__x, struct nova_fbdev, helper)

struct nova_fbdev {
	struct drm_fb_helper helper;
	struct drm_framebuffer *fb;
	struct drm_gem_object *bo;
};

static struct fb_ops nova_fbdev_ops = {
	.owner          = THIS_MODULE,

	.fb_copyarea    = sys_copyarea,
	.fb_fillrect    = sys_fillrect,
	.fb_imageblit   = sys_imageblit,

	.fb_blank       = drm_fb_helper_blank,
	.fb_check_var   = drm_fb_helper_check_var,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_set_par     = drm_fb_helper_set_par,
	.fb_setcmap     = drm_fb_helper_setcmap,
};

static int
nova_fbdev_create(struct drm_fb_helper *helper,
				struct drm_fb_helper_surface_size *sizes)
{
	int ret, num_bufs = 2;
	struct nova_fbdev *fbdev = to_nova_fbdev(helper);
	struct drm_device *ddev = helper->dev;
	struct fb_info *fbi;
	struct drm_framebuffer *fb;
	struct drm_gem_object *bo;
	struct drm_mode_fb_cmd mode;
	u32 size;

	mode.width = sizes->surface_width;
	mode.height = sizes->surface_height;
	mode.bpp = sizes->surface_bpp;
	mode.depth = sizes->surface_depth;
	mode.pitch = ALIGN(mode.width * ((mode.bpp + 7) / 8), 64);

	size = PAGE_ALIGN(mode.height * mode.pitch * num_bufs);
	bo = nova_gem_create_object(ddev, size);
	if (IS_ERR(bo)) {
		ret = PTR_ERR(bo);
		goto fail_bo;
	}

	fb = nova_fb_create(ddev, &mode, bo);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto fail_fb;
	}

	fbi = framebuffer_alloc(0, ddev->dev);
	if (!fbi) {
		ret = -ENOMEM;
		goto fail_fbi;
	}

	fbdev->fb = fb;
	helper->fb = fb;
	helper->fbdev = fbi;
	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &nova_fbdev_ops;
	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret)
		goto fail_cmap;

	drm_fb_helper_fill_fix(fbi, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb->width, fb->height);

	fbi->var.yres_virtual *= num_bufs;
	fbi->screen_base = nova_gem_get_vaddr(bo);
	fbi->screen_size = bo->size;
	fbi->fix.smem_start = nova_gem_get_paddr(bo);
	fbi->fix.smem_len = bo->size;

	return 0;
fail_cmap:
	framebuffer_release(fbi);
fail_fbi:
	nova_fb_destroy(fb);
fail_fb:
	drm_gem_object_unreference_unlocked(bo);
fail_bo:
	return ret;
}

static int
nova_fbdev_probe(struct drm_fb_helper *helper,
				struct drm_fb_helper_surface_size *sizes)
{
	if (!helper->fb) {
		int ret = nova_fbdev_create(helper, sizes);
		return ret ? ret : 1;
	}

	return 0;
}

static struct drm_fb_helper_funcs nova_fb_helper_funcs = {
	.fb_probe = nova_fbdev_probe,
};

int nova_drm_fbdev_init(struct nova_drm_device *ndrmdev)
{
	int ret, max_channels;
	struct nova_fbdev *fbdev;
	struct drm_fb_helper *helper;
	struct drm_device *drmdev = ndrmdev->drmdev;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		DRM_ERROR("Failed to allocate fbdev\n");
		return -ENOMEM;
	}

	helper = &fbdev->helper;
	helper->funcs = &nova_fb_helper_funcs;
	max_channels = mcde_get_num_channels(ndrmdev->drmdev->platformdev);
	ret = drm_fb_helper_init(drmdev, helper, max_channels, max_channels);
	if (ret) {
		DRM_ERROR("Failed to init fbdev helper\n");
		goto init_fail;
	}
	drm_fb_helper_single_add_all_connectors(helper);
	drm_fb_helper_initial_config(helper, 32); /* FIXME: dynamic */

	ndrmdev->helper = helper;
	return 0;
init_fail:
	kfree(fbdev);
	return ret;
}

