/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Display overlay compositer device driver
 *
 * Author: Anders Bauer <anders.bauer@stericsson.com>
 * for ST-Ericsson.
 *
 * Modified: Per-Daniel Olsson <per-daniel.olsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/compdev.h>
#include <linux/compdev_util.h>
#include <linux/hwmem.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/kobject.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <video/mcde_dss.h>
#include <video/mcde.h>
#include <video/b2r2_blt.h>

#define NUM_COMPDEV_BUFS 2

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_list_lock);
static int dev_counter;

struct compdev_buffer {
	struct hwmem_alloc *alloc;
	enum compdev_ptr_type type;
	u32 size;
	u32 paddr; /* if pinned */
};

struct compdev_display_work {
	struct work_struct work;
	struct dss_context *dss_ctx;
	int img_count;
	struct compdev_img img1;
	struct compdev_img img2;
	struct hwmem_alloc *img1_alloc;
	struct hwmem_alloc *img2_alloc;
	int blt_handle;
	int b2r2_req_id;
	enum compdev_transform  mcde_transform;
};

struct dss_context {
	struct device *dev;
	struct mcde_display_device *ddev;
	struct mcde_overlay *ovly[NUM_COMPDEV_BUFS];
	struct compdev_buffer ovly_buffer[NUM_COMPDEV_BUFS];
	struct compdev_buffer prev_ovly_buffer[NUM_COMPDEV_BUFS];
	enum compdev_transform current_buffer_transform;
	int blt_handle;
	struct buffer_cache_context cache_ctx;
};

struct compdev {
	struct mutex lock;
	struct mutex si_lock;
	struct miscdevice mdev;
	struct device *dev;
	struct list_head list;
	struct dss_context dss_ctx;
	struct kref ref_count;
	struct workqueue_struct *display_worker_thread;
	int dev_index;
	char name[10];
	post_buffer_callback pb_cb;
	post_scene_info_callback si_cb;
	size_changed_callback sc_cb;
	struct compdev_scene_info s_info;
	u8 sync_count;
	u8 image_count;
	struct compdev_img images[NUM_COMPDEV_BUFS];
	struct compdev_img *pimages[NUM_COMPDEV_BUFS];
	enum compdev_transform  mcde_transform;
	enum compdev_transform  saved_reuse_fb_transform;
	struct compdev_display_work *display_work;
	struct completion fence;
	void *cb_data;
	bool mcde_rotation;
	bool using_fb_overlay;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	struct compdev_img fb_image;
	struct hwmem_alloc *fb_image_alloc;
	bool blanked;
};

static struct compdev *compdevs[MAX_NBR_OF_COMPDEVS];

static int release_prev_frame(struct dss_context *dss_ctx);
static int compdev_clear_screen_locked(struct compdev *cd);

/* Parameter used by wait_for_vsync to avoid having to lock
 * the mutex for this call */
static struct mcde_display_device *vsync_ddev = NULL;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void early_suspend(struct early_suspend *data)
{
	struct compdev *cd =
		container_of(data, struct compdev, early_suspend);

	if (cd->dss_ctx.ovly[0] && cd->dss_ctx.ovly[0]->ddev &&
		(cd->dss_ctx.ovly[0]->ddev->stay_alive == false))
		mcde_dss_disable_display(cd->dss_ctx.ovly[0]->ddev);
}

static void late_resume(struct early_suspend *data)
{
	struct compdev *cd =
		container_of(data, struct compdev, early_suspend);

	if (cd->dss_ctx.ovly[0])
		(void) mcde_dss_enable_display(cd->dss_ctx.ovly[0]->ddev);
}
#endif

static void compdev_device_release(struct kref *ref)
{
	int i;
	struct compdev *cd =
		container_of(ref, struct compdev, ref_count);

	mutex_lock(&cd->lock);

	/* Sync last refresh */
	if (cd->display_work != NULL) {
		flush_work_sync(&cd->display_work->work);
		kfree(cd->display_work);
		cd->display_work = NULL;
	}
	flush_workqueue(cd->display_worker_thread);

#ifdef CONFIG_COMPDEV_JANITOR
	cancel_delayed_work_sync(&cd->dss_ctx.cache_ctx.free_buffers_work);
	flush_workqueue(cd->dss_ctx.cache_ctx.janitor_thread);
#endif

	mcde_dss_disable_display(cd->dss_ctx.ddev);

	if (cd->fb_image_alloc != NULL) {
		hwmem_release(cd->fb_image_alloc);
		cd->fb_image_alloc = NULL;
	}

	if (!cd->using_fb_overlay) {
		mcde_dss_close_channel(cd->dss_ctx.ddev);
		i = 0;
	} else {
		i = 1;
	}

	for (; i < NUM_COMPDEV_BUFS; i++)
		mcde_dss_destroy_overlay(cd->dss_ctx.ovly[i]);

	if (cd->dss_ctx.blt_handle >= 0)
		b2r2_blt_close(cd->dss_ctx.blt_handle);

	/* Release previous 2 frames that are still reference counted */
	release_prev_frame(&cd->dss_ctx);
	release_prev_frame(&cd->dss_ctx);

	/* Free potential temp buffers */
	for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
		if (cd->dss_ctx.cache_ctx.img[i] != NULL) {
			kref_put(&cd->dss_ctx.cache_ctx.img[i]->ref_count,
				compdev_image_release);
			cd->dss_ctx.cache_ctx.img[i] = NULL;
		}
	}

#ifdef CONFIG_COMPDEV_JANITOR
	mutex_destroy(&cd->dss_ctx.cache_ctx.janitor_lock);
	destroy_workqueue(cd->dss_ctx.cache_ctx.janitor_thread);
#endif
	destroy_workqueue(cd->display_worker_thread);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&cd->early_suspend);
#endif

	compdevs[cd->dev_index] = NULL;

	mutex_unlock(&cd->lock);

	mutex_destroy(&cd->si_lock);
	mutex_destroy(&cd->lock);

	kfree(cd);
}

static int compdev_open(struct inode *inode, struct file *file)
{
	struct compdev *cd = NULL;

	mutex_lock(&dev_list_lock);
	list_for_each_entry(cd, &dev_list, list)
		if (cd->mdev.minor == iminor(inode))
			break;

	if (&cd->list == &dev_list) {
		mutex_unlock(&dev_list_lock);
		return -ENODEV;
	}
	mutex_unlock(&dev_list_lock);
	file->private_data = cd;
	return 0;
}

static int disable_overlay(struct mcde_overlay *ovly)
{
	struct mcde_overlay_info info;

	mcde_dss_get_overlay_info(ovly, &info);
	if (info.paddr != 0) {
		/* Set the pointer to zero to disable the overlay */
		info.paddr = 0;
		info.kaddr = 0;
		mcde_dss_apply_overlay(ovly, &info);
	}
	return 0;
}

static int compdev_release(struct inode *inode, struct file *file)
{
	struct compdev *cd = NULL;
	int i;

	mutex_lock(&dev_list_lock);
	list_for_each_entry(cd, &dev_list, list)
		if (cd->mdev.minor == iminor(inode))
			break;
	mutex_unlock(&dev_list_lock);

	if (&cd->list == &dev_list)
		return -ENODEV;

	for (i = 0; i < NUM_COMPDEV_BUFS; i++)
		disable_overlay(cd->dss_ctx.ovly[i]);

	return 0;
}

static enum mcde_ovly_pix_fmt get_ovly_fmt(enum compdev_fmt fmt)
{
	switch (fmt) {
	default:
	case COMPDEV_FMT_RGB565:
		return MCDE_OVLYPIXFMT_RGB565;
	case COMPDEV_FMT_RGB888:
		return MCDE_OVLYPIXFMT_RGB888;
	case COMPDEV_FMT_RGBA8888:
		return MCDE_OVLYPIXFMT_RGBA8888;
	case COMPDEV_FMT_RGBX8888:
		return MCDE_OVLYPIXFMT_RGBX8888;
	case COMPDEV_FMT_YUV422:
		return MCDE_OVLYPIXFMT_YCbCr422;
	}
}

static int to_degree(enum compdev_transform transform)
{
	switch (transform) {
	case COMPDEV_TRANSFORM_ROT_270_CW:
		return 270;
	case COMPDEV_TRANSFORM_ROT_180:
		return 180;
	case COMPDEV_TRANSFORM_ROT_90_CW:
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_H:
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_V:
		return 90;
	case COMPDEV_TRANSFORM_ROT_0:
	default:
		return 0;
	}
}

static enum mcde_display_rotation to_mcde_rotation(int degrees)
{
	switch (degrees) {
	case 90:
		return MCDE_DISPLAY_ROT_90_CW;
	case 180:
		return MCDE_DISPLAY_ROT_180;
	case 270:
		return MCDE_DISPLAY_ROT_270_CW;
	default:
		return MCDE_DISPLAY_ROT_0;
	}
}

static enum compdev_transform to_transform(int degrees)
{
	switch (degrees) {
	case 270:
		return COMPDEV_TRANSFORM_ROT_270_CW;
	case 180:
		return COMPDEV_TRANSFORM_ROT_180;
	case 90:
		return COMPDEV_TRANSFORM_ROT_90_CW;
	default:
		return COMPDEV_TRANSFORM_ROT_0;
	}
}

enum compdev_transform extract_rotation(enum compdev_transform transform)
{
	switch (transform) {
	case COMPDEV_TRANSFORM_ROT_90_CW:
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_H:
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_V:
		return COMPDEV_TRANSFORM_ROT_90_CW;
	case COMPDEV_TRANSFORM_ROT_90_CCW:
		return COMPDEV_TRANSFORM_ROT_90_CCW;
	default:
		return COMPDEV_TRANSFORM_ROT_0;
	}
}

enum compdev_transform extract_transform(enum compdev_transform transform)
{
	switch (transform) {
	case COMPDEV_TRANSFORM_FLIP_H:
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_H:
		return COMPDEV_TRANSFORM_FLIP_H;
	case COMPDEV_TRANSFORM_FLIP_V:
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_V:
		return COMPDEV_TRANSFORM_FLIP_V;
	default:
		return COMPDEV_TRANSFORM_ROT_0;
	}
}

static int compdev_setup_ovly(struct compdev_img *img,
		struct compdev_buffer *buffer,
		struct mcde_overlay *ovly,
		int z_order,
		struct dss_context *dss_ctx,
		enum compdev_transform mcde_transform)
{
	int ret = 0;
	enum hwmem_mem_type memtype;
	enum hwmem_access access;
	struct hwmem_mem_chunk mem_chunk;
	size_t mem_chunk_length = 1;
	struct hwmem_region rgn = { .offset = 0, .count = 1, .start = 0 };
	struct mcde_overlay_info info;
	enum compdev_transform tot_trans;

	if (img->buf.type == COMPDEV_PTR_HWMEM_BUF_NAME_OFFSET) {
		buffer->paddr = 0;
		buffer->type = COMPDEV_PTR_HWMEM_BUF_NAME_OFFSET;
		buffer->alloc = hwmem_resolve_by_name(img->buf.hwmem_buf_name);
		if (IS_ERR(buffer->alloc)) {
			ret = PTR_ERR(buffer->alloc);
			dev_warn(dss_ctx->dev,
				"HWMEM resolve failed, %d\n", ret);
			goto resolve_failed;
		}

		hwmem_get_info(buffer->alloc, &buffer->size, &memtype,
				&access);

		if (!(access & HWMEM_ACCESS_READ) ||
				memtype == HWMEM_MEM_SCATTERED_SYS) {
			ret = -EACCES;
			dev_warn(dss_ctx->dev,
				"Invalid_mem overlay, %d\n", ret);
			goto invalid_mem;
		}
		ret = hwmem_pin(buffer->alloc, &mem_chunk, &mem_chunk_length);
		if (ret) {
			dev_warn(dss_ctx->dev,
				"Pin failed, %d\n", ret);
			goto pin_failed;
		}

		rgn.size = rgn.end = buffer->size;
		ret = hwmem_set_domain(buffer->alloc, access,
			HWMEM_DOMAIN_SYNC, &rgn);
		if (ret)
			dev_warn(dss_ctx->dev,
				"Set domain failed, %d\n", ret);

		buffer->paddr = mem_chunk.paddr;
	} else if (img->buf.type == COMPDEV_PTR_PHYSICAL) {
		buffer->type = COMPDEV_PTR_PHYSICAL;
		buffer->alloc = NULL;
		buffer->size = img->buf.len;
		buffer->paddr = img->buf.offset;
	}

	info.stride = img->pitch;
	info.fmt = get_ovly_fmt(img->fmt);
	info.dst_z = z_order;

	tot_trans = to_transform((to_degree(mcde_transform)
			+ to_degree(img->transform)) % 360);

	if (tot_trans & COMPDEV_TRANSFORM_ROT_90_CW) {
		info.dst_x = img->dst_rect.y;
		info.dst_y = img->dst_rect.x;
		info.w = img->dst_rect.height;
		info.h = img->dst_rect.width;
	} else {
		info.dst_x = img->dst_rect.x;
		info.dst_y = img->dst_rect.y;
		info.w = img->dst_rect.width;
		info.h = img->dst_rect.height;
	}

	/*
	 * Start coordinate is always 0,0.
	 * The paddr is increased accordingly instead.
	 */
	info.src_x = 0;
	info.src_y = 0;
	info.paddr = buffer->paddr + img->pitch * img->src_rect.y +
		img->src_rect.x * (compdev_get_bpp(img->fmt) >> 3);

	if (img->buf.type == COMPDEV_PTR_HWMEM_BUF_NAME_OFFSET)
		info.kaddr = hwmem_kmap(buffer->alloc);
	else
		info.kaddr = 0; /* requires an ioremap at dump time */

	mcde_dss_apply_overlay(ovly, &info);
	return ret;

pin_failed:
invalid_mem:
	hwmem_release(buffer->alloc);
	buffer->alloc = NULL;
	buffer->size = 0;
	buffer->paddr = 0;

resolve_failed:
	return ret;
}

static int compdev_update_rotation(struct dss_context *dss_ctx,
		enum mcde_display_rotation rotation)
{
	int ret = 0;

	/* Set rotation */
	ret = mcde_dss_set_rotation(dss_ctx->ddev, rotation);
	if (ret != 0)
		goto exit;

	/* Apply */
	ret = mcde_dss_apply_channel(dss_ctx->ddev);
exit:
	return ret;
}

static int release_prev_frame(struct dss_context *dss_ctx)
{
	int ret = 0;
	int i;
	struct compdev_buffer *prev_frame;

	/* Handle unpin of previous buffers */
	for (i = 0; i < NUM_COMPDEV_BUFS; i++) {
		prev_frame = &dss_ctx->prev_ovly_buffer[i];
		if (prev_frame->type ==
				COMPDEV_PTR_HWMEM_BUF_NAME_OFFSET) {
			if (!IS_ERR_OR_NULL(prev_frame->alloc)) {
				hwmem_release(prev_frame->alloc);
				if (prev_frame->paddr != 0)
					hwmem_unpin(prev_frame->alloc);
			}
		}
		*prev_frame = dss_ctx->ovly_buffer[i];

		dss_ctx->ovly_buffer[i].alloc = NULL;
		dss_ctx->ovly_buffer[i].size = 0;
		dss_ctx->ovly_buffer[i].paddr = 0;
	}
	return ret;

}

static int compdev_blt(struct compdev *cd,
		int blt_handle,
		struct compdev_img *src_img,
		struct compdev_img *dst_img)
{

	struct b2r2_blt_req req;
	int req_id;

	dev_dbg(cd->dev, "%s\n", __func__);

	memset(&req, 0, sizeof(req));
	req.size = sizeof(req);

	if (src_img->buf.type == COMPDEV_PTR_PHYSICAL) {
		req.src_img.buf.type = B2R2_BLT_PTR_PHYSICAL;
		req.src_img.buf.fd = src_img->buf.fd;
	} else {
		struct hwmem_alloc *alloc;

		req.src_img.buf.type = B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET;
		req.src_img.buf.hwmem_buf_name = src_img->buf.hwmem_buf_name;

		alloc = hwmem_resolve_by_name(src_img->buf.hwmem_buf_name);
		if (IS_ERR_OR_NULL(alloc)) {
			dev_warn(cd->dev,
				"HWMEM resolve failed\n");
		} else {
			hwmem_set_access(alloc,
					HWMEM_ACCESS_READ | HWMEM_ACCESS_IMPORT,
					task_tgid_nr(current));
			hwmem_release(alloc);
		}
	}
	req.src_img.pitch = src_img->pitch;
	req.src_img.buf.offset = src_img->buf.offset;
	req.src_img.buf.len = src_img->buf.len;
	req.src_img.fmt = compdev_to_blt_format(src_img->fmt);
	req.src_img.width = src_img->width;
	req.src_img.height = src_img->height;

	req.src_rect.x = src_img->src_rect.x;
	req.src_rect.y = src_img->src_rect.y;
	req.src_rect.width = src_img->src_rect.width;
	req.src_rect.height = src_img->src_rect.height;

	if (dst_img->buf.type == COMPDEV_PTR_PHYSICAL) {
		req.dst_img.buf.type = B2R2_BLT_PTR_PHYSICAL;
		req.dst_img.buf.fd = dst_img->buf.fd;
	} else {
		req.dst_img.buf.type = B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET;
		req.dst_img.buf.hwmem_buf_name = dst_img->buf.hwmem_buf_name;
	}
	req.dst_img.pitch = dst_img->pitch;
	req.dst_img.buf.offset = dst_img->buf.offset;
	req.dst_img.buf.len = dst_img->buf.len;
	req.dst_img.fmt = compdev_to_blt_format(dst_img->fmt);
	req.dst_img.width = dst_img->width;
	req.dst_img.height = dst_img->height;


	req.transform = compdev_to_blt_transform(src_img->transform);
	req.dst_rect.x = 0;
	req.dst_rect.y = 0;
	req.dst_rect.width = dst_img->width;
	req.dst_rect.height = dst_img->height;

	req.global_alpha = 0xff;
	req.flags = B2R2_BLT_FLAG_DITHER | B2R2_BLT_FLAG_ASYNCH;

	dev_dbg(cd->dev, "%s: src_rect: x %d, y %d, w %d h %d\n",
		__func__, req.src_rect.x, req.src_rect.y,
		req.src_rect.width, req.src_rect.height);
	dev_dbg(cd->dev, "%s: dst_rect: x %d, y %d, w %d h %d\n",
		__func__, req.dst_rect.x, req.dst_rect.y,
		req.dst_rect.width, req.dst_rect.height);
	dev_dbg(cd->dev, "%s: img_trans 0x%02x, mcde_trans %d\n",
		__func__, src_img->transform, cd->mcde_transform);

	req_id = b2r2_blt_request(blt_handle, &req);
	if (req_id < 0) {
		dev_err(cd->dev,
			"%s: Failed b2r2_blt_request (%d), blt_handle %d\n",
			__func__, req_id, blt_handle);
	}

	return req_id;
}

static int compdev_post_buffers_dss(struct dss_context *dss_ctx,
		struct compdev_img *img1, struct compdev_img *img2,
		bool tripple_buffer, enum compdev_transform mcde_transform)
{
	int ret = 0;
	int i = 0;

	struct compdev_img *fb_img = NULL;
	struct compdev_img *ovly_img = NULL;
	int curr_rot = to_degree(dss_ctx->current_buffer_transform);
	int img_rot = to_degree(mcde_transform);
	bool update_ovly[] = {false, false};

	/* Unpin the previous frame */
	release_prev_frame(dss_ctx);

	/* Set channel rotation */
	if ((curr_rot != img_rot)) {
		if (compdev_update_rotation(dss_ctx,
				to_mcde_rotation(img_rot)) == 0)
			dss_ctx->current_buffer_transform = mcde_transform;
		else
			dev_warn(dss_ctx->dev,
				"Failed to update MCDE rotation "
				"(image rotation = %d), %d\n",
				img_rot, ret);
	}

	if ((img1 != NULL) && (img1->flags & COMPDEV_OVERLAY_FLAG))
		ovly_img = img1;
	else if (img1 != NULL)
		fb_img = img1;

	if ((img2 != NULL) && (img2->flags & COMPDEV_OVERLAY_FLAG))
		ovly_img = img2;
	else if (img2 != NULL)
		fb_img = img2;

	/* Handle buffers */
	if (fb_img != NULL) {
		if ((fb_img->flags & COMPDEV_PROTECTED_FLAG) &&
			(mcde_dss_secure_output(dss_ctx->ddev) == false)) {
			disable_overlay(dss_ctx->ovly[i]);
		} else {
			ret = compdev_setup_ovly(fb_img,
					&dss_ctx->ovly_buffer[i],
					dss_ctx->ovly[i], 1, dss_ctx,
					mcde_transform);
			if (ret)
				dev_warn(dss_ctx->dev,
					"Failed to setup overlay[%d],"
					"%d\n", i, ret);
			else
				update_ovly[i] = true;
		}
	} else {
		disable_overlay(dss_ctx->ovly[i]);
	}
	i++;

	if (ovly_img != NULL) {
		if ((ovly_img->flags & COMPDEV_PROTECTED_FLAG) &&
			(mcde_dss_secure_output(dss_ctx->ddev) == false)) {
			disable_overlay(dss_ctx->ovly[i]);
		} else {
			ret = compdev_setup_ovly(ovly_img,
					&dss_ctx->ovly_buffer[i],
					dss_ctx->ovly[i], 0, dss_ctx,
					mcde_transform);
			if (ret)
				dev_warn(dss_ctx->dev,
						"Failed to setup overlay[%d],"
						"%d\n", i, ret);
			else
				update_ovly[i] = true;
		}
	} else {
		disable_overlay(dss_ctx->ovly[i]);
	}

	/* Do the display update */
	for (i = 0; i < 2; i++) {
		if (update_ovly[i]) {
			mcde_dss_update_overlay(dss_ctx->ovly[i],
					tripple_buffer);
			break;
		}
	}

	return ret;
}

static void compdev_display_worker_function(struct work_struct *w)
{
	struct compdev_display_work *dw =
		container_of(w, struct compdev_display_work, work);

	if (dw->blt_handle >= 0 && dw->b2r2_req_id >= 0) {
		if (b2r2_blt_synch(dw->blt_handle,
				dw->b2r2_req_id) < 0) {
			dev_err(dw->dss_ctx->dev,
				"%s: Could not perform b2r2_blt_synch, "
				"handle %d, req_id %d",
				__func__, dw->blt_handle,
				dw->b2r2_req_id);
		}
	}

	if (dw->img_count == 1)
		compdev_post_buffers_dss(dw->dss_ctx,
				&dw->img1, NULL, false,
				dw->mcde_transform);
	else if (dw->img_count == 2)
		compdev_post_buffers_dss(dw->dss_ctx,
				&dw->img1, &dw->img2, false,
				dw->mcde_transform);

	if (dw->img1_alloc != NULL) {
		hwmem_release(dw->img1_alloc);
		dw->img1_alloc = NULL;
	}

	if (dw->img2_alloc != NULL) {
		hwmem_release(dw->img2_alloc);
		dw->img2_alloc = NULL;
	}
}

int compdev_add_display_work(struct compdev *cd,
		struct dss_context *dss_ctx,
		struct compdev_img *img1,
		struct compdev_img *img2,
		struct compdev_display_work *dw)
{
	INIT_WORK(&dw->work, compdev_display_worker_function);

	if (img1) {
		dw->img_count++;
		dw->img1 = *img1;
		if (img2) {
			dw->img_count++;
			dw->img2 = *img2;
		}
	}

	dw->img1_alloc = NULL;
	if (dw->img1.buf.hwmem_buf_name > 0) {
		/* Hog the img1 buffer */
		struct hwmem_alloc *alloc = hwmem_resolve_by_name(
				dw->img1.buf.hwmem_buf_name);
		if (IS_ERR_OR_NULL(alloc))
			dev_err(cd->dev,
				"%s: Failed to resolve hwmem (%d)\n",
				__func__, (uint32_t) alloc);
		else
			dw->img1_alloc = alloc;
	}

	dw->img2_alloc = NULL;
	if (dw->img2.buf.hwmem_buf_name > 0) {
		/* Hog the img2 buffer */
		struct hwmem_alloc *alloc = hwmem_resolve_by_name(
				dw->img2.buf.hwmem_buf_name);
		if (IS_ERR_OR_NULL(alloc))
			dev_err(cd->dev,
				"%s: Failed to resolve hwmem (%d)\n",
				__func__, (uint32_t) alloc);
		else
			dw->img2_alloc = alloc;
	}

	dw->dss_ctx = dss_ctx;
	dw->mcde_transform = cd->mcde_transform;
	queue_work(cd->display_worker_thread, &dw->work);

	return 0;
}

/* Determine if transform is needed */
static bool transform_needed(struct compdev_img *src_img,
		enum compdev_transform mcde_transform)
{
	/* Any transform left for b2r2? */
	if (src_img->transform != COMPDEV_TRANSFORM_ROT_0)
		return true;

	/*
	 * Check scaling, notice that dst_rect
	 * is defined after mcde_transform
	 */

	if ((mcde_transform == COMPDEV_TRANSFORM_ROT_0 ||
		mcde_transform == COMPDEV_TRANSFORM_ROT_180) &&
		(src_img->src_rect.width != src_img->dst_rect.width ||
		src_img->src_rect.height != src_img->dst_rect.height))
		return true;
	if ((mcde_transform & COMPDEV_TRANSFORM_ROT_90_CW) &&
		(src_img->src_rect.width != src_img->dst_rect.height ||
		src_img->src_rect.height != src_img->dst_rect.width))
		return true;

	/* Check color conversion */
	if (check_hw_format(src_img->fmt) == false)
		return true;

	return false;
}

/* Remove GPU transform if using MCDE rotation */
static void update_transform(struct compdev *cd,
		struct compdev_img *src_img)
{
	if (cd->mcde_rotation) {
		/* Remove the rotation from the src */
		if (!(src_img->flags & COMPDEV_OVERLAY_FLAG))
			src_img->transform =
				extract_transform(src_img->transform);
	}
}

static void compdev_set_background_fb(
	struct compdev *cd,
	struct compdev_img *image1,
	struct compdev_img *image2)
{
	struct compdev_img *img[2] = {image1, image2};
	int i;

	for (i = 0; i < 2; i++) {

		int cur = i;
		int next = (i + 1) & 1;

		if ((img[cur]->flags & COMPDEV_OVERLAY_FLAG) &&
			(img[next]->flags & COMPDEV_FRAMEBUFFER_FLAG)) {
			/* Save img[next] as the framebuffer */
			dev_dbg(cd->dev, "%s: Save img%i=0x%x for reuse\n",
				__func__, next + 1,
				(uint32_t)img[next]);
			/* Make sure memory will remain */
			if (img[next]->buf.type ==
					COMPDEV_PTR_HWMEM_BUF_NAME_OFFSET) {
				cd->fb_image_alloc = hwmem_resolve_by_name(
					img[next]->buf.hwmem_buf_name);
				if (IS_ERR_OR_NULL(cd->fb_image_alloc)) {
					dev_err(cd->dev,
						"%s: HWMEM resolve failed\n",
						__func__);
				}
			}
			cd->fb_image = *img[next];
			break;
		}
	}
}

static int compdev_reuse_fb(struct compdev *cd, struct compdev_img **image1,
		struct compdev_img **image2)
{
	/*
	 * Both img1 and img2 must be set in order to have a fully
	 * composited framebuffer together with an overlay
	 */
	if (!cd->s_info.reuse_fb_img) {

		if (cd->fb_image_alloc != NULL) {
			hwmem_release(cd->fb_image_alloc);
			cd->fb_image_alloc = NULL;
		}

		if ((*image1 != NULL) && (*image2 != NULL))
			compdev_set_background_fb(
				cd,
				*image1,
				*image2);

	} else if ((*image1)->flags & COMPDEV_OVERLAY_FLAG) {
		/* Let's reuse the previously stored image */
		dev_dbg(cd->dev, "%s: Reuse fb_img\n", __func__);
		*image2 = &cd->fb_image;
		if (cd->pb_cb) {
			/*
			 * Temporarily reset the transform flag to the original
			 * image rotation. Clonedev needs this transform.
			 */
			enum compdev_transform tmp = cd->fb_image.transform;

			cd->fb_image.transform = cd->saved_reuse_fb_transform;
			cd->pb_cb(cd->cb_data, &cd->fb_image);
			cd->fb_image.transform = tmp;
		}
	}

	return 0;
}

static int compdev_post_buffer_locked(struct compdev *cd,
		struct compdev_img *src_img)
{
	int ret = 0;
	struct compdev_img *resulting_img;
	struct compdev_img_internal *tmp_img = NULL;
	bool bypass_case = false;
	int b2r2_req_id;

	dev_dbg(cd->dev, "%s\n", __func__);

	/* Check for bypass images */
	if (src_img->flags & COMPDEV_BYPASS_FLAG)
		bypass_case = true;

	/* Start new callback work */
	if (cd->pb_cb != NULL)
		cd->pb_cb(cd->cb_data, src_img);

	if (src_img->flags & COMPDEV_FRAMEBUFFER_FLAG)
		cd->saved_reuse_fb_transform = src_img->transform;

	update_transform(cd, src_img);

	if (!bypass_case) {
		if (transform_needed(src_img, cd->mcde_transform)) {
			u16 width = 0;
			u16 height = 0;
			bool protected = false;
			enum compdev_fmt fmt;

			if (cd->dss_ctx.blt_handle < 0) {
				dev_dbg(cd->dev, "%s: Opening B2R2\n",
					__func__);
				cd->dss_ctx.blt_handle = b2r2_blt_open();
				if (cd->dss_ctx.blt_handle < 0) {
					dev_warn(cd->dev,
						"%s(%d): Failed to "
						"open b2r2 device\n",
						__func__, __LINE__);
				}
			}

			if (cd->mcde_transform & COMPDEV_TRANSFORM_ROT_90_CW) {
				width = src_img->dst_rect.height;
				height = src_img->dst_rect.width;
			} else {
				width = src_img->dst_rect.width;
				height = src_img->dst_rect.height;
			}

			fmt = find_compatible_fmt(src_img->fmt,
					src_img->transform &
					COMPDEV_TRANSFORM_ROT_90_CW);

			if (src_img->flags & COMPDEV_PROTECTED_FLAG)
				protected = true;

			tmp_img = compdev_buffer_cache_get_image
					(&cd->dss_ctx.cache_ctx,
							fmt, width, height,
							protected);

			if (tmp_img != NULL) {
				tmp_img->img.flags = src_img->flags |
						COMPDEV_INTERNAL_TEMP_FLAG;
				tmp_img->img.dst_rect =
						src_img->dst_rect;
				tmp_img->img.src_rect.x = 0;
				tmp_img->img.src_rect.y = 0;
				tmp_img->img.src_rect.width =
						tmp_img->img.width;
				tmp_img->img.src_rect.height =
						tmp_img->img.height;
				tmp_img->img.z_position =
						src_img->z_position;
				tmp_img->img.transform =
						src_img->transform;

				resulting_img = &tmp_img->img;

				b2r2_req_id = compdev_blt(cd,
						cd->dss_ctx.blt_handle,
						src_img, resulting_img);

				if (cd->dss_ctx.blt_handle >= 0 &&
						b2r2_req_id >= 0) {
					if (b2r2_blt_synch(
							cd->dss_ctx.blt_handle,
							b2r2_req_id) < 0) {
						dev_err(cd->dev,
							"%s: Could not perform "
							"b2r2_blt_synch,"
							"handle %d"
							", req_id %d",
							__func__,
							cd->dss_ctx.blt_handle,
							b2r2_req_id);
					}
				}
			} else {
				cd->images[cd->image_count] = *src_img;
				resulting_img = &cd->images[cd->image_count];
				dev_err(cd->dev, "%s: Could not allocate hwmem "
						"temporary buffer\n", __func__);
			}
		} else {
			cd->images[cd->image_count] = *src_img;
			resulting_img = &cd->images[cd->image_count];
		}

		/*
		 * After this point all rotation will be done by mcde
		 * All B2R2 rotation is already performed
		 */
		resulting_img->transform = COMPDEV_TRANSFORM_ROT_0;

		cd->pimages[cd->image_count] = resulting_img;
		cd->image_count++;

		if ((cd->image_count < NUM_COMPDEV_BUFS) &&
				(cd->sync_count > 1)) {
			cd->sync_count--;
		} else {
			struct compdev_img *img[2] = {NULL, NULL};
			int i;
			struct compdev_img_internal *tmp_handle;

			/* Unblank if blanked */
			if (cd->blanked)
				cd->blanked = false;

			if (cd->sync_count)
				cd->sync_count--;

			img[0] = cd->pimages[0];
			if (cd->image_count > 1)
				img[1] = cd->pimages[1];

			compdev_reuse_fb(cd, &img[0], &img[1]);

			/* Do the refresh */
			compdev_post_buffers_dss(&cd->dss_ctx,
					img[0], img[1],
					true, cd->mcde_transform);

			/*
			 * Free references to the temp buffers,
			 * dss worker now "owns" the hwmem handles.
			 */
			for (i = 0; i < 2; i++)
				if ((img[i] != NULL) &&
					(img[i]->flags &
					COMPDEV_INTERNAL_TEMP_FLAG)) {
					tmp_handle = container_of(img[i],
						struct compdev_img_internal,
						img);

					/* don't free the background image */
					if ((void *)tmp_handle !=
						(void *)&cd->fb_image)
						compdev_free_img(
							&cd->dss_ctx.cache_ctx,
							tmp_handle);
				}

			cd->sync_count = 0;
			cd->image_count = 0;
			cd->pimages[0] = NULL;
			cd->pimages[1] = NULL;
		}
	} else {
		if (cd->sync_count && !cd->s_info.reuse_fb_img)
			cd->sync_count--;
		/* Do a blanking of main display But only once to clear */
		if (!cd->blanked && cd->s_info.img_count == 1) {
			compdev_clear_screen_locked(cd);
			cd->blanked = true;
		}
	}

	if (cd->sync_count == 0)
		complete(&cd->fence);

	return ret;
}

static int compdev_post_single_buffer_asynch_locked(struct compdev *cd,
		struct compdev_img *src_img, int b2r2_handle,
		int b2r2_req_id)
{
	dev_dbg(cd->dev, "%s\n", __func__);

	/* Add asynch work for b2r2 synch and dss */
	if (cd->display_work != NULL) {
		flush_work_sync(&cd->display_work->work);
		kfree(cd->display_work);
		cd->display_work = NULL;
	}

	cd->display_work = kzalloc(sizeof(*cd->display_work),
			GFP_KERNEL);
	if (cd->display_work != NULL) {
		cd->display_work->blt_handle = b2r2_handle;
		cd->display_work->b2r2_req_id = b2r2_req_id;
		compdev_add_display_work(cd, &cd->dss_ctx,
				src_img, NULL, cd->display_work);
	}

	cd->sync_count = 0;
	cd->image_count = 0;

	return 0;
}

static int compdev_post_scene_info_locked(struct compdev *cd,
				struct compdev_scene_info *s_info)
{
	int ret = 0;

	dev_dbg(cd->dev, "%s\n", __func__);

	if (cd->dev_index == 0)	{
		mutex_unlock(&cd->lock);
		wait_for_completion_interruptible_timeout(&cd->fence, HZ/10);
		mutex_lock(&cd->lock);
		init_completion(&cd->fence);
	}

	cd->s_info = *s_info;
	cd->sync_count = cd->s_info.img_count;

	if (cd->mcde_rotation) {
		if (cd->sync_count >= 1 || cd->s_info.reuse_fb_img) {
			cd->mcde_transform = s_info->hw_transform;
		}
	}

	/* Handle callback */
	if (cd->si_cb != NULL)
		cd->si_cb(cd->cb_data, s_info);
	return ret;
}


static int compdev_get_size_locked(struct dss_context *dss_ctx,
					struct compdev_size *size)
{
	mcde_dss_get_native_resolution(dss_ctx->ddev,
			&(size->width), &(size->height));

	return 0;
}

static int compdev_set_video_mode_locked(struct compdev *cd,
				struct compdev_video_mode *video_mode)
{
	/* Set video mode */
	struct mcde_video_mode vmode;
	struct compdev_size size;
	int ret;

	memset(&vmode, 0, sizeof(struct mcde_video_mode));

	vmode.xres = video_mode->xres;
	vmode.yres = video_mode->yres;
	vmode.pixclock = video_mode->pixclock;
	vmode.hbp = video_mode->hbp;
	vmode.hfp = video_mode->hfp;
	vmode.hsw = video_mode->hsw;
	vmode.vbp = video_mode->vbp;
	vmode.vfp = video_mode->vfp;
	vmode.vsw = video_mode->vsw;
	vmode.interlaced = video_mode->interlaced;
	vmode.force_update = video_mode->force_update;

	ret = mcde_dss_set_video_mode(cd->dss_ctx.ddev, &vmode);
	if (ret != 0)
		goto exit;

	/* Apply */
	ret = mcde_dss_apply_channel(cd->dss_ctx.ddev);
	if (ret != 0)
		goto exit;

	/* Handle callback */
	if (cd->sc_cb != NULL) {
		size.width = video_mode->xres;
		size.height = video_mode->yres;
		mutex_unlock(&cd->lock);
		cd->sc_cb(cd->cb_data, &size);
		mutex_lock(&cd->lock);
	}
exit:
	return ret;
}

static int compdev_clear_screen_locked(struct compdev *cd)
{
	/* disable all overlays and refresh update screen */
	disable_overlay(cd->dss_ctx.ovly[0]);
	disable_overlay(cd->dss_ctx.ovly[1]);
	mcde_dss_update_overlay(cd->dss_ctx.ovly[0], false);

	return 0;
}

static int compdev_get_listener_state_locked(struct compdev *cd,
				enum compdev_listener_state *state)
{
	int ret = 0;

	*state = COMPDEV_LISTENER_OFF;
	if (cd->pb_cb != NULL)
		*state = COMPDEV_LISTENER_ON;
	return ret;
}

static int compdev_wait_for_vsync_unlocked(s64 *timestamp)
{
	int ret;

	ret = mcde_dss_wait_for_vsync(vsync_ddev, timestamp);

	return ret;
}

static long compdev_ioctl(struct file *file,
		unsigned int cmd,
		unsigned long arg)
{
	int ret;
	struct compdev *cd = (struct compdev *)file->private_data;
	struct compdev_img img;
	struct compdev_scene_info s_info;
	struct compdev_video_mode video_mode;

	switch (cmd) {
	case COMPDEV_GET_SIZE_IOC:
	{
		struct compdev_size tmp;
		mutex_lock(&cd->lock);

		ret = compdev_get_size_locked(&(cd->dss_ctx), &tmp);
		if (ret) {
			mutex_unlock(&cd->lock);
			return -EFAULT;
		}
		ret = copy_to_user((void __user *)arg, &tmp,
							sizeof(tmp));
		if (ret)
			ret = -EFAULT;

		mutex_unlock(&cd->lock);
		break;
	}
	case COMPDEV_GET_LISTENER_STATE_IOC:
	{
		enum compdev_listener_state state;
		mutex_lock(&cd->lock);

		compdev_get_listener_state_locked(cd, &state);
		ret = copy_to_user((void __user *)arg, &state,
				sizeof(state));
		if (ret)
			ret = -EFAULT;
		mutex_unlock(&cd->lock);
		break;
	}
	case COMPDEV_POST_BUFFER_IOC:
		mutex_lock(&cd->lock);
		/* Get the user data */
		if (copy_from_user(&img, (void *)arg, sizeof(img))) {
			dev_warn(cd->dev,
				"%s: copy_from_user failed\n",
				__func__);
			mutex_unlock(&cd->lock);
			return -EFAULT;
		}
		ret = compdev_post_buffer_locked(cd, &img);
		mutex_unlock(&cd->lock);
		break;
	case COMPDEV_POST_SCENE_INFO_IOC:
		mutex_lock(&cd->lock);
		/* Get the user data */
		if (copy_from_user(&s_info, (void *)arg, sizeof(s_info))) {
			dev_warn(cd->dev,
				"%s: copy_from_user failed\n",
				__func__);
			mutex_unlock(&cd->lock);
			return -EFAULT;
		}
		mutex_lock(&cd->si_lock);
		ret = compdev_post_scene_info_locked(cd, &s_info);
		mutex_unlock(&cd->si_lock);
		mutex_unlock(&cd->lock);
		break;
	case COMPDEV_SET_VIDEO_MODE_IOC:
		mutex_lock(&cd->lock);
		/* Get the user data */
		if (copy_from_user(&video_mode, (void *)arg,
					sizeof(video_mode))) {
			dev_warn(cd->dev,
				"%s: copy_from_user failed\n",
				__func__);
			mutex_unlock(&cd->lock);
			return -EFAULT;
		}
		ret = compdev_set_video_mode_locked(cd, &video_mode);

		mutex_unlock(&cd->lock);
		break;
	case COMPDEV_WAIT_FOR_VSYNC_IOC:
	{
		s64 timestamp;

		ret = compdev_wait_for_vsync_unlocked(&timestamp);

		if (copy_to_user((void __user *)arg, &timestamp,
				sizeof(timestamp))) {
			dev_warn(cd->dev,
					"%s: copy_to_user failed\n", __func__);
			return -EFAULT;
		}
		if (ret)
			ret = -EFAULT;

		break;
	}
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static const struct file_operations compdev_fops = {
	.open = compdev_open,
	.release = compdev_release,
	.unlocked_ioctl = compdev_ioctl,
};

static void init_compdev(struct compdev *cd)
{
	mutex_init(&cd->lock);
	mutex_init(&cd->si_lock);
	kref_init(&cd->ref_count);
	INIT_LIST_HEAD(&cd->list);
	init_completion(&cd->fence);

	complete(&cd->fence);

	cd->mdev.minor = MISC_DYNAMIC_MINOR;
	cd->mdev.name = cd->name;
	cd->mdev.fops = &compdev_fops;
	cd->dev = cd->mdev.this_device;
	cd->fb_image_alloc = NULL;
	cd->blanked = false;
}

static int init_dss_context(struct dss_context *dss_ctx,
		struct mcde_display_device *ddev, struct compdev *cd,
		const char *name)
{
#ifdef CONFIG_COMPDEV_JANITOR
	char wq_name[20];
#endif

	dss_ctx->ddev = ddev;
	vsync_ddev = ddev;
	memset(&dss_ctx->cache_ctx, 0, sizeof(dss_ctx->cache_ctx));
	dss_ctx->blt_handle = -1;

#ifdef CONFIG_COMPDEV_JANITOR
	snprintf(wq_name, sizeof(wq_name), "%s_janitor", name);

	mutex_init(&dss_ctx->cache_ctx.janitor_lock);
	dss_ctx->cache_ctx.janitor_thread = create_workqueue(wq_name);
	if (!dss_ctx->cache_ctx.janitor_thread) {
		mutex_destroy(&dss_ctx->cache_ctx.janitor_lock);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK_DEFERRABLE(&dss_ctx->cache_ctx.free_buffers_work,
		compdev_free_cache_context_buffers);
#endif

	return 0;
}

int compdev_create(struct mcde_display_device *ddev,
		struct mcde_overlay *parent_ovly, bool mcde_rotation,
		struct compdev **cd_pp)
{
	int ret = 0;
	int i;
	struct compdev *cd;
	struct mcde_overlay_info info;

	if (cd_pp != NULL)
		*cd_pp = NULL;

	mutex_lock(&dev_list_lock);

	if (dev_counter == 0) {
		for (i = 0; i < MAX_NBR_OF_COMPDEVS; i++)
			compdevs[i] = NULL;
	}

	if (dev_counter > MAX_NBR_OF_COMPDEVS) {
		mutex_unlock(&dev_list_lock);
		return -ENOMEM;
	}

	cd = kzalloc(sizeof(struct compdev), GFP_KERNEL);
	if (!cd) {
		mutex_unlock(&dev_list_lock);
		return -ENOMEM;
	}

	cd->dev_index = dev_counter++;

	snprintf(cd->name, sizeof(cd->name), "%s%d",
			COMPDEV_DEFAULT_DEVICE_PREFIX,
			cd->dev_index);
	init_compdev(cd);

	ret = init_dss_context(&cd->dss_ctx, ddev, cd, cd->name);
	if (ret < 0)
		goto fail_dss_context;

	cd->display_worker_thread = create_workqueue(cd->name);
	if (!cd->display_worker_thread) {
		ret = -ENOMEM;
		goto fail_workqueue;
	}

	if (parent_ovly != NULL) {
		/* Framebuffer is used together with compdev */
		i = 1;
		cd->dss_ctx.ovly[0] = parent_ovly;
		cd->using_fb_overlay = true;
	} else {
		/* Compdev is used without framebuffer */
		cd->using_fb_overlay = false;
		i = 0;
		ret = mcde_dss_open_channel(ddev);
		if (ret)
			goto fail_open_channel;
		ret = mcde_dss_enable_display(ddev);
		if (ret)
			goto fail_enable_display;
	}

	for (; i < NUM_COMPDEV_BUFS; i++) {
		cd->dss_ctx.ovly[i] = mcde_dss_create_overlay(ddev, &info);
		if (!cd->dss_ctx.ovly[i]) {
			ret = -ENOMEM;
			goto fail_create_ovly;
		}
		if (mcde_dss_enable_overlay(cd->dss_ctx.ovly[i]))
			goto fail_create_ovly;
		if (disable_overlay(cd->dss_ctx.ovly[i]))
			goto fail_create_ovly;
	}
	cd->dss_ctx.current_buffer_transform = COMPDEV_TRANSFORM_ROT_0;
	cd->mcde_rotation = mcde_rotation;

	ret = misc_register(&cd->mdev);
	if (ret)
		goto fail_register_misc;
	cd->dev = cd->mdev.this_device;
	cd->dss_ctx.dev = cd->dev;
	cd->dss_ctx.cache_ctx.dev = cd->dev;

	compdevs[cd->dev_index] = cd;
	list_add_tail(&cd->list, &dev_list);
	mutex_unlock(&dev_list_lock);

	if (cd_pp != NULL)
		*cd_pp = cd;

#ifdef CONFIG_HAS_EARLYSUSPEND
	cd->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	cd->early_suspend.suspend = early_suspend;
	cd->early_suspend.resume = late_resume;
	register_early_suspend(&cd->early_suspend);
#endif

	goto out;

fail_enable_display:
	mcde_dss_close_channel(ddev);
fail_open_channel:
fail_register_misc:
fail_create_ovly:
	for (i = 0; i < NUM_COMPDEV_BUFS; i++) {
		if (cd->dss_ctx.ovly[i])
			mcde_dss_destroy_overlay(cd->dss_ctx.ovly[i]);
	}
fail_workqueue:
	if (cd->display_worker_thread)
		destroy_workqueue(cd->display_worker_thread);
fail_dss_context:
#ifdef CONFIG_COMPDEV_JANITOR
	mutex_destroy(&cd->dss_ctx.cache_ctx.janitor_lock);
	destroy_workqueue(cd->dss_ctx.cache_ctx.janitor_thread);
#endif
	kfree(cd);

	dev_counter--;
	mutex_unlock(&dev_list_lock);
out:
	return ret;
}


int compdev_get(int dev_idx, struct compdev **cd_pp)
{
	struct compdev *cd;
	int ret;
	cd = NULL;

	if (dev_idx >= MAX_NBR_OF_COMPDEVS)
		return -ENOMEM;

	mutex_lock(&dev_list_lock);
	cd = compdevs[dev_idx];
	if (cd != NULL && atomic_inc_not_zero(&cd->ref_count.refcount)) {
		*cd_pp = cd;
		ret = 0;
	} else {
		ret = -ENOMEM;
	}
	mutex_unlock(&dev_list_lock);

	return ret;
}
EXPORT_SYMBOL(compdev_get);

int compdev_put(struct compdev *cd)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	kref_put(&cd->ref_count, compdev_device_release);
	return ret;
}
EXPORT_SYMBOL(compdev_put);

int compdev_get_size(struct compdev *cd, struct compdev_size *size)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	ret = compdev_get_size_locked(&cd->dss_ctx, size);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_get_size);

int compdev_get_listener_state(struct compdev *cd,
	enum compdev_listener_state *listener_state)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	ret = compdev_get_listener_state_locked(cd, listener_state);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_get_listener_state);

int compdev_wait_for_vsync(struct compdev *cd, s64 *timestamp)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	ret = compdev_wait_for_vsync_unlocked(timestamp);
	return ret;
}
EXPORT_SYMBOL(compdev_wait_for_vsync);

const char *compdev_get_device_name(struct compdev *cd)
{
	return dev_name(cd->mdev.this_device);
}
EXPORT_SYMBOL(compdev_get_device_name);

int compdev_post_buffer(struct compdev *cd, struct compdev_img *img)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	ret = compdev_post_buffer_locked(cd, img);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_post_buffer);

int compdev_post_single_buffer_asynch(struct compdev *cd,
		struct compdev_img *img, int b2r2_handle,
		int b2r2_req_id)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	ret = compdev_post_single_buffer_asynch_locked(cd, img,
			b2r2_handle, b2r2_req_id);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_post_single_buffer_asynch);

int compdev_post_scene_info(struct compdev *cd,
			struct compdev_scene_info *s_info)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	mutex_lock(&cd->si_lock);
	ret = compdev_post_scene_info_locked(cd, s_info);
	mutex_unlock(&cd->si_lock);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_post_scene_info);

int compdev_set_video_mode(struct compdev *cd,
		struct compdev_video_mode *video_mode)
{
	int ret;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	ret = compdev_set_video_mode_locked(cd, video_mode);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_set_video_mode);

int compdev_clear_screen(struct compdev *cd)
{
	int ret;
	if (cd == NULL)
		return -ENOMEM;

	mutex_lock(&cd->lock);

	ret = compdev_clear_screen_locked(cd);

	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_clear_screen);

int compdev_register_listener_callbacks(struct compdev *cd, void *data,
		post_buffer_callback pb_cb, post_scene_info_callback si_cb,
		size_changed_callback sc_cb)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;
	mutex_lock(&cd->lock);
	cd->cb_data = data;
	cd->pb_cb = pb_cb;
	cd->si_cb = si_cb;
	cd->sc_cb = sc_cb;
	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_register_listener_callbacks);

int compdev_deregister_callbacks(struct compdev *cd)
{
	int ret = 0;
	if (cd == NULL)
		return -ENOMEM;
	mutex_lock(&cd->lock);
	if (cd->display_work != NULL) {
		flush_work_sync(&cd->display_work->work);
		kfree(cd->display_work);
		cd->display_work = NULL;
	}
	flush_workqueue(cd->display_worker_thread);
	cd->cb_data = NULL;
	cd->pb_cb = NULL;
	cd->si_cb = NULL;
	cd->sc_cb = NULL;
	mutex_unlock(&cd->lock);
	return ret;
}
EXPORT_SYMBOL(compdev_deregister_callbacks);

void compdev_destroy(struct mcde_display_device *ddev)
{
	struct compdev *cd;
	struct compdev *tmp;

	mutex_lock(&dev_list_lock);
	list_for_each_entry_safe(cd, tmp, &dev_list, list) {
		if (cd->dss_ctx.ddev == ddev) {
			list_del(&cd->list);
			misc_deregister(&cd->mdev);
			kref_put(&cd->ref_count,
				compdev_device_release);
			break;
		}
	}
	dev_counter--;
	mutex_unlock(&dev_list_lock);
}

static void compdev_destroy_all(void)
{
	struct compdev *cd;
	struct compdev *tmp;

	mutex_lock(&dev_list_lock);
	list_for_each_entry_safe(cd, tmp, &dev_list, list) {
		list_del(&cd->list);
		misc_deregister(&cd->mdev);
		kref_put(&cd->ref_count, compdev_device_release);
	}
	mutex_unlock(&dev_list_lock);

	mutex_destroy(&dev_list_lock);
}

static int __init compdev_init(void)
{
	pr_info("%s\n", __func__);

	mutex_init(&dev_list_lock);

	return 0;
}
module_init(compdev_init);

static void __exit compdev_exit(void)
{
	compdev_destroy_all();
	pr_info("%s\n", __func__);
}
module_exit(compdev_exit);

MODULE_AUTHOR("Anders Bauer <anders.bauer@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Display overlay device driver");

