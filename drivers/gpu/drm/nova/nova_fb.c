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

#include "nova_drm_priv.h"

#define to_nova_fb(x) container_of(x, struct nova_framebuffer, base)

struct nova_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *bo;
};

void
nova_fb_destroy(struct drm_framebuffer *framebuffer)
{
	struct nova_framebuffer *nfb = to_nova_fb(framebuffer);

	DRM_DEBUG_DRIVER("\n");

	drm_framebuffer_cleanup(framebuffer);
	drm_gem_object_unreference_unlocked(nfb->bo);

	kfree(nfb);
}

static int
fb_create_handle(struct drm_framebuffer *fb,
		 struct drm_file *file_priv,
		 unsigned int *handle)
{
	struct nova_framebuffer *nfb = to_nova_fb(fb);

	DRM_DEBUG_DRIVER("\n");

	return drm_gem_handle_create(file_priv, nfb->bo, handle);
}

static int
fb_dirty(struct drm_framebuffer *framebuffer, struct drm_file *file_priv,
	 unsigned flags, unsigned color, struct drm_clip_rect *clips,
	 unsigned num_clips)
{
	DRM_DEBUG_DRIVER("\n");

	return 0;
}

static const struct drm_framebuffer_funcs fb_funcs = {
	.destroy = nova_fb_destroy,
	.create_handle = fb_create_handle,
	.dirty = fb_dirty,
};

struct drm_gem_object *nova_fb_get_bo(struct drm_framebuffer *framebuffer)
{
	struct nova_framebuffer *nfb;

	if (!framebuffer)
		return NULL;
	nfb = to_nova_fb(framebuffer);

	return nfb->bo;
}

struct drm_framebuffer *
nova_fb_create(struct drm_device *drmdev, struct drm_mode_fb_cmd *mode,
						struct drm_gem_object *bo)
{
	int ret;
	struct nova_framebuffer *nfb;

	nfb = kzalloc(sizeof(*nfb), GFP_KERNEL);
	if (!nfb) {
		ret = -ENOMEM;
		goto fail_alloc;
	}

	ret = drm_framebuffer_init(drmdev, &nfb->base, &fb_funcs);
	if (ret)
		goto fail_init;

	drm_helper_mode_fill_fb_struct(&nfb->base, mode);
	nfb->bo = bo; /* Reference increased by caller */

	return &nfb->base;
fail_init:
	kfree(nfb);
fail_alloc:
	return ERR_PTR(ret);
}

