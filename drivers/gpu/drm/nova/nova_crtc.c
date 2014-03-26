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

#include <linux/types.h>
#include <drm/drmP.h>
#include <drm/drm_mode.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <video/mcde.h>

#include "nova_drm_priv.h"

static char *tm[] = {
	"ON",
	"STANDBY",
	"SUSPEND",
	"OFF",
};

struct nova_crtc {
	struct drm_crtc base;
	int channel;
	struct mcde_chnl_state *chnl;
	struct mcde_ovly_state *ovly;
	int dpms;
};

static inline int crtc_id(struct nova_crtc *crtc)
{
	return crtc->base.base.id;
}

static inline void mcde_chnl_start_flow(struct nova_crtc *ncrtc)
{
	struct mcde_rectangle area = { 0, 0,
			ncrtc->base.mode.hdisplay, ncrtc->base.mode.vdisplay };
	mcde_chnl_update(ncrtc->chnl, &area, NULL);
}

static void
crtc_helper_dpms(struct drm_crtc *crtc, int mode)
{
	struct nova_crtc *ncrtc = to_nova_crtc(crtc);

	DRM_DEBUG_DRIVER("crtc=%d mode=%s->%s\n", crtc_id(ncrtc),
						tm[ncrtc->dpms], tm[mode]);

	if (ncrtc->dpms == mode)
		return;

	if (mode == DRM_MODE_DPMS_ON) {
		mcde_chnl_apply(ncrtc->chnl);
		mcde_chnl_start_flow(ncrtc);
	} else if (mode == DRM_MODE_DPMS_OFF) {
		mcde_chnl_stop_flow(ncrtc->chnl);
	}

	ncrtc->dpms = mode;
}

static bool
crtc_helper_mode_fixup(struct drm_crtc *crtc,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_DRIVER("\n");

	return true;
}

static void
crtc_helper_prepare(struct drm_crtc *crtc)
{
	struct nova_crtc *ncrtc = to_nova_crtc(crtc);

	DRM_DEBUG_DRIVER("\n");

	mcde_chnl_stop_flow(ncrtc->chnl);
}

static void
update_overlay(struct drm_crtc *crtc)
{
	struct nova_crtc *ncrtc = to_nova_crtc(crtc);
	struct drm_gem_object *bo = nova_fb_get_bo(crtc->fb);
	struct mcde_ovly_state *ovly = ncrtc->ovly;
	u32 paddr = 0, pitch = 0;

	if (bo) {
		pitch = crtc->fb->pitch;
		paddr = nova_gem_get_paddr(bo) + pitch * crtc->y;
	}
	mcde_ovly_set_source_buf(ovly, paddr);
	/* FIXME: Format below */
	mcde_ovly_set_source_info(ovly, pitch, MCDE_OVLYPIXFMT_RGBX8888);
	mcde_ovly_set_source_area(ovly, crtc->x, 0,
				crtc->hwmode.hdisplay, crtc->hwmode.vdisplay);
	mcde_ovly_set_dest_pos(ovly, 0, 0, 0);
	mcde_ovly_apply(ncrtc->ovly);
	DRM_DEBUG_DRIVER("crtc=%d: flip paddr=0x%.8x, pitch=0x%.8x (%d,%d)\n",
				crtc_id(ncrtc), paddr, pitch, crtc->x, crtc->y);
}

static int
crtc_helper_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode, int x, int y,
			struct drm_framebuffer *old_fb)
{
	struct nova_crtc *ncrtc = to_nova_crtc(crtc);
	struct mcde_video_mode vmode;

	DRM_DEBUG_DRIVER("\n");

	update_overlay(crtc);

	nova_mode_to_mcde(adjusted_mode, &vmode);
	WARN_ON(mcde_chnl_set_video_mode(ncrtc->chnl, &vmode));

	return MODE_OK;
}

static int
crtc_helper_mode_set_base(struct drm_crtc *crtc, int x, int y,
			  struct drm_framebuffer *old_fb)
{
	struct nova_crtc *ncrtc = to_nova_crtc(crtc);

	DRM_DEBUG_DRIVER("crtc=%d\n", crtc_id(ncrtc));

	update_overlay(crtc);
	if (ncrtc->dpms == DRM_MODE_DPMS_ON) {
		mcde_chnl_apply(ncrtc->chnl);
		mcde_chnl_start_flow(ncrtc);
	} else {
		DRM_DEBUG_DRIVER("Buffer swap with DPMS !ON\n");
	}

	return MODE_OK;
}

static void
crtc_helper_commit(struct drm_crtc *crtc)
{
	struct nova_crtc *ncrtc = to_nova_crtc(crtc);

	DRM_DEBUG_DRIVER("\n");

	if (ncrtc->dpms == DRM_MODE_DPMS_ON) {
		mcde_chnl_apply(ncrtc->chnl);
		mcde_chnl_start_flow(ncrtc);
	}
}

static void
crtc_helper_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER("\n");

	/* FIXME: */
}

static struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.dpms = crtc_helper_dpms,
	.prepare = crtc_helper_prepare,
	.commit = crtc_helper_commit,
	.mode_fixup = crtc_helper_mode_fixup,
	.mode_set = crtc_helper_mode_set,
	.mode_set_base = crtc_helper_mode_set_base,
	.load_lut = crtc_helper_load_lut,
};

static void
crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b, uint32_t start,
								uint32_t size)
{
	/* FIXME: */

	DRM_DEBUG_DRIVER("\n");
}

static void
crtc_destroy(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER("\n");

	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static int
crtc_set_config(struct drm_mode_set *set)
{
	DRM_DEBUG_DRIVER("\n");

	return drm_crtc_helper_set_config(set);
}

static int
crtc_page_flip(struct drm_crtc *crtc,
	       struct drm_framebuffer *fb,
	       struct drm_pending_vblank_event *event)
{
	DRM_DEBUG_DRIVER("\n");

	/* FIXME: */

	return 0;
}

static const struct drm_crtc_funcs crtc_funcs = {
	.gamma_set = crtc_gamma_set,
	.destroy = crtc_destroy,
	.set_config = crtc_set_config,
	.page_flip = crtc_page_flip,
};

int nova_crtc_create(struct nova_drm_device *ndrmdev, int channel_no)
{
	int channel = (1 << channel_no);
	struct nova_crtc *ncrtc;
	struct mcde_chnl_state *chnl;
	struct mcde_ovly_state *ovly;

	if (channel != NOVA_CRTC_A &&
	    channel != NOVA_CRTC_B)
		return -EINVAL;

	chnl = mcde_chnl_get(
		channel == NOVA_CRTC_A ? MCDE_CHNL_A : MCDE_CHNL_B, 0, NULL);
	if (IS_ERR(chnl))
		return PTR_ERR(chnl);

	ovly = mcde_ovly_get(chnl);
	if (IS_ERR(ovly))
		return PTR_ERR(ovly);

	ncrtc = kzalloc(sizeof(*ncrtc), GFP_KERNEL);
	if (!ncrtc)
		return -ENOMEM;

	ncrtc->channel = channel;
	ncrtc->ovly = ovly;
	ncrtc->chnl = chnl;
	ncrtc->dpms = DRM_MODE_DPMS_OFF;
	mcde_chnl_enable(chnl);
	drm_crtc_init(ndrmdev->drmdev, &ncrtc->base, &crtc_funcs);
	drm_crtc_helper_add(&ncrtc->base, &crtc_helper_funcs);

	DRM_LOG_DRIVER("CRTC created: chnl=%d, id=%d\n",
						channel_no, crtc_id(ncrtc));

	return 0;
}

