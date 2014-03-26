/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE display sub system driver
 *
 * Author: Marcus Lorentzon <marcus.xm.lorentzon@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <video/mcde_dss.h>

#define to_overlay(x) container_of(x, struct mcde_overlay, kobj)

void overlay_release(struct kobject *kobj)
{
	struct mcde_overlay *ovly = to_overlay(kobj);

	kfree(ovly);
}

struct kobj_type ovly_type = {
	.release = overlay_release,
};

static int apply_overlay(struct mcde_overlay *ovly,
				struct mcde_overlay_info *info, bool force)
{
	int ret = 0;

	if (ovly->info.paddr != info->paddr || force)
		mcde_ovly_set_source_buf(ovly->state, info->paddr, info->kaddr);

	if (ovly->info.stride != info->stride || ovly->info.fmt != info->fmt ||
									force)
		mcde_ovly_set_source_info(ovly->state, info->stride, info->fmt);
	if (ovly->info.src_x != info->src_x ||
					ovly->info.src_y != info->src_y ||
					ovly->info.w != info->w ||
					ovly->info.h != info->h || force)
		mcde_ovly_set_source_area(ovly->state,
				info->src_x, info->src_y, info->w, info->h);
	if (ovly->info.dst_x != info->dst_x || ovly->info.dst_y != info->dst_y
					|| ovly->info.dst_z != info->dst_z ||
					force)
		mcde_ovly_set_dest_pos(ovly->state,
					info->dst_x, info->dst_y, info->dst_z);

	mcde_ovly_apply(ovly->state);
	ovly->info = *info;

	return ret;
}

/* MCDE DSS operations */

int mcde_dss_open_channel(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct mcde_chnl_state *chnl;

	mutex_lock(&ddev->display_lock);
	/* Acquire MCDE resources */
	chnl = mcde_chnl_get(ddev->chnl_id, ddev->fifo, ddev->port);
	if (IS_ERR(chnl)) {
		ret = PTR_ERR(chnl);
		dev_warn(&ddev->dev, "Failed to acquire MCDE channel\n");
		goto chnl_get_failed;
	}
	ddev->chnl_state = chnl;
chnl_get_failed:
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_open_channel);

void mcde_dss_close_channel(struct mcde_display_device *ddev)
{
	mutex_lock(&ddev->display_lock);
	mcde_chnl_put(ddev->chnl_state);
	ddev->chnl_state = NULL;
	mutex_unlock(&ddev->display_lock);
}
EXPORT_SYMBOL(mcde_dss_close_channel);

int mcde_dss_enable_display(struct mcde_display_device *ddev)
{
	int ret;

	if (ddev->enabled)
		return 0;

	mutex_lock(&ddev->display_lock);
	mcde_chnl_enable(ddev->chnl_state);

	/* Initiate display communication */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0) {
		dev_warn(&ddev->dev, "Failed to initialize display\n");
		goto display_failed;
	}

	ret = ddev->set_rotation(ddev, ddev->get_rotation(ddev));
	if (ret < 0)
		dev_warn(&ddev->dev, "Failed to set rotation\n");

	dev_dbg(&ddev->dev, "Display enabled, chnl=%d\n",
					ddev->chnl_id);
	ddev->enabled = true;
	mutex_unlock(&ddev->display_lock);

	return 0;

display_failed:
	mcde_chnl_disable(ddev->chnl_state);
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_enable_display);

void mcde_dss_disable_display(struct mcde_display_device *ddev)
{
	if (!ddev->enabled)
		return;

	/* TODO: Disable overlays */
	mutex_lock(&ddev->display_lock);

	mcde_chnl_stop_flow(ddev->chnl_state);

	(void)ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);

	mcde_chnl_disable(ddev->chnl_state);

	ddev->enabled = false;
	mutex_unlock(&ddev->display_lock);

	dev_dbg(&ddev->dev, "Display disabled, chnl=%d\n", ddev->chnl_id);
}
EXPORT_SYMBOL(mcde_dss_disable_display);

int mcde_dss_apply_channel(struct mcde_display_device *ddev)
{
	int ret;
	if (!ddev->apply_config)
		return -EINVAL;
	mutex_lock(&ddev->display_lock);
	ret = ddev->apply_config(ddev);
	mutex_unlock(&ddev->display_lock);

	return ret;
}
EXPORT_SYMBOL(mcde_dss_apply_channel);

struct mcde_overlay *mcde_dss_create_overlay(struct mcde_display_device *ddev,
	struct mcde_overlay_info *info)
{
	struct mcde_overlay *ovly;

	ovly = kzalloc(sizeof(struct mcde_overlay), GFP_KERNEL);
	if (!ovly)
		return NULL;

	kobject_init(&ovly->kobj, &ovly_type); /* Local ref */
	kobject_get(&ovly->kobj); /* Creator ref */
	INIT_LIST_HEAD(&ovly->list);
	mutex_lock(&ddev->display_lock);
	list_add(&ddev->ovlys, &ovly->list);
	mutex_unlock(&ddev->display_lock);
	ovly->info = *info;
	ovly->ddev = ddev;

	return ovly;
}
EXPORT_SYMBOL(mcde_dss_create_overlay);

void mcde_dss_destroy_overlay(struct mcde_overlay *ovly)
{
	list_del(&ovly->list);
	if (ovly->state)
		mcde_dss_disable_overlay(ovly);
	kobject_put(&ovly->kobj);
}
EXPORT_SYMBOL(mcde_dss_destroy_overlay);

int mcde_dss_enable_overlay(struct mcde_overlay *ovly)
{
	int ret;

	if (!ovly->ddev->chnl_state)
		return -EINVAL;

	if (!ovly->state) {
		struct mcde_ovly_state *state;
		state = mcde_ovly_get(ovly->ddev->chnl_state);
		if (IS_ERR(state)) {
			ret = PTR_ERR(state);
			dev_warn(&ovly->ddev->dev,
				"Failed to acquire overlay\n");
			return ret;
		}
		ovly->state = state;
	}

	apply_overlay(ovly, &ovly->info, true);

	dev_vdbg(&ovly->ddev->dev, "Overlay enabled, chnl=%d\n",
							ovly->ddev->chnl_id);
	return 0;
}
EXPORT_SYMBOL(mcde_dss_enable_overlay);

int mcde_dss_apply_overlay(struct mcde_overlay *ovly,
						struct mcde_overlay_info *info)
{
	if (info == NULL)
		info = &ovly->info;
	return apply_overlay(ovly, info, false);
}
EXPORT_SYMBOL(mcde_dss_apply_overlay);

void mcde_dss_disable_overlay(struct mcde_overlay *ovly)
{
	if (!ovly->state)
		return;

	mcde_ovly_put(ovly->state);

	dev_dbg(&ovly->ddev->dev, "Overlay disabled, chnl=%d\n",
							ovly->ddev->chnl_id);

	ovly->state = NULL;
}
EXPORT_SYMBOL(mcde_dss_disable_overlay);

int mcde_dss_update_overlay(struct mcde_overlay *ovly, bool tripple_buffer)
{
	int ret;
	dev_vdbg(&ovly->ddev->dev, "Overlay update, chnl=%d\n",
							ovly->ddev->chnl_id);

	if (!ovly->state || !ovly->ddev->update)
		return -EINVAL;

	mutex_lock(&ovly->ddev->display_lock);
	/* Do not perform an update if power mode is off */
	if (ovly->ddev->get_power_mode(ovly->ddev) == MCDE_DISPLAY_PM_OFF) {
		ret = 0;
		goto power_mode_off;
	}

	ret = ovly->ddev->update(ovly->ddev, tripple_buffer);
	if (ret)
		goto update_failed;

power_mode_off:
update_failed:
	mutex_unlock(&ovly->ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_update_overlay);

void mcde_dss_get_overlay_info(struct mcde_overlay *ovly,
				struct mcde_overlay_info *info) {
	if (info)
		*info = ovly->info;
}
EXPORT_SYMBOL(mcde_dss_get_overlay_info);

void mcde_dss_get_native_resolution(struct mcde_display_device *ddev,
	u16 *x_res, u16 *y_res)
{
	mutex_lock(&ddev->display_lock);
	ddev->get_native_resolution(ddev, x_res, y_res);
	mutex_unlock(&ddev->display_lock);
}
EXPORT_SYMBOL(mcde_dss_get_native_resolution);

enum mcde_ovly_pix_fmt mcde_dss_get_default_pixel_format(
	struct mcde_display_device *ddev)
{
	int ret;
	mutex_lock(&ddev->display_lock);
	ret = ddev->get_default_pixel_format(ddev);
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_get_default_pixel_format);

void mcde_dss_get_physical_size(struct mcde_display_device *ddev,
	u16 *physical_width, u16 *physical_height)
{
	mutex_lock(&ddev->display_lock);
	ddev->get_physical_size(ddev, physical_width, physical_height);
	mutex_unlock(&ddev->display_lock);
}
EXPORT_SYMBOL(mcde_dss_get_physical_size);

int mcde_dss_try_video_mode(struct mcde_display_device *ddev,
	struct mcde_video_mode *video_mode)
{
	int ret;
	mutex_lock(&ddev->display_lock);
	ret = ddev->try_video_mode(ddev, video_mode);
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_try_video_mode);

int mcde_dss_set_video_mode(struct mcde_display_device *ddev,
	struct mcde_video_mode *vmode)
{
	int ret = 0;
	struct mcde_video_mode old_vmode;

	mutex_lock(&ddev->display_lock);
	/* Do not perform set_video_mode if power mode is off */
	if (ddev->get_power_mode(ddev) == MCDE_DISPLAY_PM_OFF)
		goto power_mode_off;

	ddev->get_video_mode(ddev, &old_vmode);
	if (memcmp(vmode, &old_vmode, sizeof(old_vmode)) == 0)
		goto same_video_mode;

	ret = ddev->set_video_mode(ddev, vmode);
	if (ret)
		goto set_video_mode_failed;

power_mode_off:
same_video_mode:
set_video_mode_failed:
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_set_video_mode);

void mcde_dss_get_video_mode(struct mcde_display_device *ddev,
	struct mcde_video_mode *video_mode)
{
	mutex_lock(&ddev->display_lock);
	ddev->get_video_mode(ddev, video_mode);
	mutex_unlock(&ddev->display_lock);
}
EXPORT_SYMBOL(mcde_dss_get_video_mode);

int mcde_dss_set_pixel_format(struct mcde_display_device *ddev,
	enum mcde_ovly_pix_fmt pix_fmt)
{
	enum mcde_ovly_pix_fmt old_pix_fmt;
	int ret;

	mutex_lock(&ddev->display_lock);
	old_pix_fmt = ddev->get_pixel_format(ddev);
	if (old_pix_fmt == pix_fmt) {
		ret = 0;
		goto same_pixel_format;
	}

	ret = ddev->set_pixel_format(ddev, pix_fmt);

same_pixel_format:
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_set_pixel_format);

int mcde_dss_get_pixel_format(struct mcde_display_device *ddev)
{
	int ret;
	mutex_lock(&ddev->display_lock);
	ret = ddev->get_pixel_format(ddev);
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_get_pixel_format);

int mcde_dss_set_rotation(struct mcde_display_device *ddev,
	enum mcde_display_rotation rotation)
{
	int ret;
	enum mcde_display_rotation old_rotation;

	mutex_lock(&ddev->display_lock);
	old_rotation = ddev->get_rotation(ddev);
	if (old_rotation == rotation) {
		ret = 0;
		goto same_rotation;
	}

	ret = ddev->set_rotation(ddev, rotation);
same_rotation:
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_set_rotation);

enum mcde_display_rotation mcde_dss_get_rotation(
	struct mcde_display_device *ddev)
{
	int ret;
	mutex_lock(&ddev->display_lock);
	ret = ddev->get_rotation(ddev);
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_get_rotation);

int mcde_dss_wait_for_vsync(struct mcde_display_device *ddev, s64 *timestamp)
{
	int ret;

	/*
	 * Do not take the display_lock so that other dss functions can
	 * be called from another thread.
	 */
	mutex_lock(&ddev->vsync_lock); /* Protect against multi-thread usage */
	ret = ddev->wait_for_vsync(ddev, timestamp);
	mutex_unlock(&ddev->vsync_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_wait_for_vsync);

bool mcde_dss_secure_output(struct mcde_display_device *ddev)
{
	bool ret;
	mutex_lock(&ddev->display_lock);
	ret = ddev->secure_output();
	mutex_unlock(&ddev->display_lock);
	return ret;
}
EXPORT_SYMBOL(mcde_dss_secure_output);

int __init mcde_dss_init(void)
{
	return 0;
}

void mcde_dss_exit(void)
{
}

