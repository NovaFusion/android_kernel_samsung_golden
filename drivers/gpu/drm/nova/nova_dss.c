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

#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <drm/drmP.h>
#include <video/mcde_display.h>

#include "nova_drm_priv.h"

static char *tm[] = {
	"ON",
	"STANDBY",
	"SUSPEND",
	"OFF",
};

struct dss_device {
	struct mcde_display_device *dispdev;
	struct nova_drm_device *ndrmdev;
	struct drm_connector con;
	struct drm_encoder enc;

	int xres;
	int yres;
};

static inline int dssdev_id(struct dss_device *dssdev)
{
	return dssdev->dispdev->id;
}

#define drmcon2ddev(__con) container_of(__con, struct dss_device, con)
#define drmenc2ddev(__enc) container_of(__enc, struct dss_device, enc)

static inline void dump_mcde_mode(struct mcde_video_mode *m)
{
	DRM_DEBUG_DRIVER("res:%dx%d clock:%d intl:%s hs:%d,%d,%d vs:%d,%d,%d\n",
		m->xres, m->yres, m->pixclock, m->interlaced ? "y" : "n",
		m->hbp, m->hsw, m->hfp, m->vbp, m->vsw, m->vfp);
}

static inline void dump_mode(struct drm_display_mode *m)
{
	DRM_DEBUG_DRIVER("res:%dx%d clock:%d tot:%dx%d intl:%s hs:%d,%d"
								" vs:%d,%d\n",
		m->hdisplay, m->vdisplay, m->clock, m->htotal, m->vtotal,
		(m->flags & DRM_MODE_FLAG_INTERLACE) ? "y" : "n",
		m->hsync_start, m->hsync_end, m->vsync_start, m->vsync_end);
}

/* Connector */

static int
connector_helper_get_modes(struct drm_connector *connector)
{
	struct dss_device *dssdev = drmcon2ddev(connector);
	struct drm_display_mode *mode = drm_mode_create(connector->dev);

	DRM_DEBUG_DRIVER("x=%d y=%d\n", dssdev->xres, dssdev->yres);

	mode->hdisplay = dssdev->xres;
	mode->vdisplay = dssdev->yres;
	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
	mode->clock = 1000000;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static int
connector_helper_mode_valid(struct drm_connector *connector,
						struct drm_display_mode *mode)
{
	struct dss_device *dssdev = drmcon2ddev(connector);

	DRM_DEBUG_DRIVER("\n");

	if (mode->hdisplay != dssdev->xres ||
	    mode->vdisplay != dssdev->yres)
		return MODE_BAD;

	return MODE_OK;
}

static struct drm_encoder *
connector_helper_best_encoder(struct drm_connector *connector)
{
	struct dss_device *dssdev = drmcon2ddev(connector);

	DRM_DEBUG_DRIVER("\n");

	return &dssdev->enc;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = connector_helper_get_modes,
	.mode_valid = connector_helper_mode_valid,
	.best_encoder = connector_helper_best_encoder,
};

static enum mcde_display_power_mode dpms2pm(int dpms)
{
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		return MCDE_DISPLAY_PM_ON;
	case DRM_MODE_DPMS_STANDBY:
		return MCDE_DISPLAY_PM_STANDBY;
	default:
		return MCDE_DISPLAY_PM_OFF;
	}
}

static void
connector_dpms(struct drm_connector *connector, int mode)
{
	struct dss_device *dssdev = drmcon2ddev(connector);
	int old_mode = connector->dpms;

	DRM_DEBUG_DRIVER("dev=%d mode=%s encoder=%p crtc=%p\n",
			dssdev_id(dssdev), tm[mode], connector->encoder,
			connector->encoder ? connector->encoder->crtc : NULL);

	/* off/suspend -> on via standby */
	if (mode == DRM_MODE_DPMS_ON && old_mode > DRM_MODE_DPMS_STANDBY)
		drm_helper_connector_dpms(connector, DRM_MODE_DPMS_STANDBY);
	drm_helper_connector_dpms(connector, mode);
}

static enum drm_connector_status
connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static int
connector_fill_modes(struct drm_connector *connector,
					uint32_t max_width, uint32_t max_height)
{
	DRM_DEBUG_DRIVER("\n");
	return drm_helper_probe_single_connector_modes(connector, max_width,
								max_height);
}

static int
connector_set_property(struct drm_connector *connector,
				struct drm_property *property, uint64_t val)
{
	DRM_DEBUG_DRIVER("\n");
	return 0;
}

static void
connector_destroy(struct drm_connector *connector)
{
	DRM_DEBUG_DRIVER("\n");
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}

static void
connector_force(struct drm_connector *connector)
{
	DRM_DEBUG_DRIVER("\n");
}

static const struct drm_connector_funcs connector_funcs = {
	.dpms = connector_dpms,
	.detect = connector_detect,
	.fill_modes = connector_fill_modes,
	.set_property = connector_set_property,
	.destroy = connector_destroy,
	.force = connector_force,
};

/* Encoder */

static void
encoder_helper_dpms(struct drm_encoder *encoder, int mode)
{
	struct dss_device *dssdev = drmenc2ddev(encoder);
	struct mcde_display_device *dispdev = dssdev->dispdev;

	DRM_DEBUG_DRIVER("mode=%s\n", tm[mode]);

	dispdev->set_power_mode(dispdev, dpms2pm(mode));
	if (mode == DRM_MODE_DPMS_ON)
		dispdev->on_first_update(dispdev);
}

static void
encoder_helper_save(struct drm_encoder *encoder)
{
	DRM_DEBUG_DRIVER("\n");
}

static void
encoder_helper_restore(struct drm_encoder *encoder)
{
	DRM_DEBUG_DRIVER("\n");
}

static bool
encoder_helper_mode_fixup(struct drm_encoder *encoder,
			  struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode)
{
	int ret;
	struct dss_device *dssdev = drmenc2ddev(encoder);
	struct mcde_display_device *dispdev = dssdev->dispdev;
	struct mcde_video_mode vmode;

	DRM_DEBUG_DRIVER("\n");

	nova_mode_to_mcde(adjusted_mode, &vmode);
	ret = dispdev->try_video_mode(dispdev, &vmode);
	if (!ret) {
		nova_mode_from_mcde(adjusted_mode, &vmode);
		dump_mcde_mode(&vmode);
		dump_mode(adjusted_mode);
		return true;
	} else {
		return false;
	}
}

static void
encoder_helper_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG_DRIVER("\n");
}

static void
encoder_helper_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct dss_device *dssdev = drmenc2ddev(encoder);
	struct mcde_display_device *dispdev = dssdev->dispdev;
	struct mcde_video_mode vmode;

	DRM_DEBUG_DRIVER("%dx%d\n", adjusted_mode->hdisplay,
						adjusted_mode->vdisplay);

	nova_mode_to_mcde(adjusted_mode, &vmode);
	dump_mode(adjusted_mode);
	dump_mcde_mode(&vmode);
	WARN_ON(dispdev->set_video_mode(dispdev, &vmode));
	WARN_ON(dispdev->invalidate_area(dispdev, NULL));
}

static void
encoder_helper_commit(struct drm_encoder *encoder)
{
	struct dss_device *dssdev = drmenc2ddev(encoder);
	struct mcde_display_device *dispdev = dssdev->dispdev;

	DRM_DEBUG_DRIVER("\n");

	if (dispdev->get_power_mode(dispdev) == MCDE_DISPLAY_PM_ON)
		dispdev->on_first_update(dispdev);
}

static struct drm_crtc *
encoder_helper_get_crtc(struct drm_encoder *encoder)
{
	DRM_DEBUG_DRIVER("\n");
	return encoder->crtc;
}

static void
encoder_helper_disable(struct drm_encoder *encoder)
{
	DRM_DEBUG_DRIVER("\n");
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.dpms = encoder_helper_dpms,
	.save = encoder_helper_save,
	.restore = encoder_helper_restore,
	.mode_fixup = encoder_helper_mode_fixup,
	.prepare = encoder_helper_prepare,
	.commit = encoder_helper_commit,
	.mode_set = encoder_helper_mode_set,
	.get_crtc = encoder_helper_get_crtc,
	.disable = encoder_helper_disable,
};

static void
encoder_destroy(struct drm_encoder *encoder)
{
	struct dss_device *dssdev = drmenc2ddev(encoder);

	DRM_DEBUG_DRIVER("\n");

	mcde_chnl_put(dssdev->dispdev->chnl_state);
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs encoder_funcs = {
	.destroy = encoder_destroy,
};

static int nova_dss_register_device(struct nova_drm_device *ndrmdev,
					struct mcde_display_device *dispdev)
{
	int ret;
	struct mcde_chnl_state *chnl;
	struct dss_device *dssdev;
	struct drm_connector *con;
	struct drm_encoder *enc;
	u16 xres, yres;

	DRM_DEBUG_DRIVER("\n");

	chnl = mcde_chnl_get(dispdev->chnl_id, dispdev->fifo, dispdev->port);
	if (IS_ERR(chnl)) {
		DRM_ERROR("Failed to open dss channel\n");
		return PTR_ERR(chnl);
	}

	dssdev = kzalloc(sizeof(*dssdev), GFP_KERNEL);
	if (!dssdev) {
		ret = -ENOMEM;
		goto fail_alloc;
	}
	con = &dssdev->con;
	con->dpms = DRM_MODE_DPMS_OFF;
	enc = &dssdev->enc;
	dssdev->ndrmdev = ndrmdev;
	dssdev->dispdev = dispdev;
	dispdev->chnl_state = chnl;
	dispdev->get_native_resolution(dispdev, &xres, &yres);
	dssdev->xres = xres;
	dssdev->yres = yres;

	drm_encoder_init(ndrmdev->drmdev, enc, &encoder_funcs,
							DRM_MODE_ENCODER_LVDS);
	enc->possible_crtcs = (1 << dispdev->chnl_id);
	drm_encoder_helper_add(enc, &encoder_helper_funcs);
	drm_connector_init(ndrmdev->drmdev, con, &connector_funcs,
						DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(con, &connector_helper_funcs);
	drm_mode_connector_attach_encoder(con, enc);
	drm_mode_group_init_legacy_group(ndrmdev->drmdev,
					&ndrmdev->drmdev->primary->mode_group);
	drm_sysfs_connector_add(con);

	DRM_LOG_DRIVER("DSS device initialized (%s)\n",
						dev_name(&dispdev->dev));

	return 0;

fail_alloc:
	mcde_chnl_put(chnl);
	return ret;
}

static int dss_dev_registered(struct device *dev, void *data)
{
	struct nova_drm_device *ndrmdev = data;

	return nova_dss_register_device(ndrmdev, to_mcde_display_device(dev));
}

void nova_drm_dss_register_devices(struct nova_drm_device *ndrmdev)
{
	bus_for_each_dev(&mcde_bus_type, NULL, ndrmdev, dss_dev_registered);
}

void nova_mode_to_mcde(struct drm_display_mode *m,
						struct mcde_video_mode *vmode)
{
	vmode->pixclock = m->clock;
	vmode->xres = m->hdisplay;
	vmode->hbp = m->hsync_start - m->hdisplay;
	vmode->hsw = m->hsync_end - m->hsync_start;
	vmode->hfp = m->htotal - m->hsync_end;
	vmode->yres = m->vdisplay;
	vmode->vbp = m->vsync_start - m->vdisplay;
	vmode->vsw = m->vsync_end - m->vsync_start;
	vmode->vfp = m->vtotal - m->vsync_end;
	vmode->interlaced = (m->flags & DRM_MODE_FLAG_INTERLACE) != 0;
}

void nova_mode_from_mcde(struct drm_display_mode *m,
						struct mcde_video_mode *vmode)
{
	m->clock = vmode->pixclock;
	m->hdisplay = vmode->xres;
	m->hsync_start = m->hdisplay + vmode->hbp;
	m->hsync_end = m->hsync_start + vmode->hsw;
	m->htotal = m->hsync_end + vmode->hfp;
	m->vdisplay = vmode->yres;
	m->vsync_start = m->vdisplay + vmode->vbp;
	m->vsync_end = m->vsync_start + vmode->vsw;
	m->vtotal = m->vsync_end + vmode->vfp;
	m->flags = vmode->interlaced ? DRM_MODE_FLAG_INTERLACE : 0;
}

