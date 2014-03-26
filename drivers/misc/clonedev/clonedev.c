/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Device for display cloning on external output.
 *
 * Author: Per-Daniel Olsson <per-daniel.olsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/clonedev.h>
#include <linux/compdev.h>
#include <linux/compdev_util.h>
#include <linux/hwmem.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/mm.h>

#include <video/mcde.h>
#include <video/b2r2_blt.h>

#define MAX_SCENE_IMAGES 2

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_list_lock);

static int dev_counter;

struct clonedev {
	struct mutex lock;
	struct miscdevice mdev;
	struct device *dev;
	char name[10];
	struct list_head list;
	bool open;
	struct compdev *src_compdev;
	struct compdev *dst_compdev;
	bool overlay_case;
	struct compdev_size dst_size;
	struct compdev_rect crop_rect;
	struct compdev_scene_info s_info;
	struct compdev_img scene_images[MAX_SCENE_IMAGES];
	u8 scene_img_count;
	enum clonedev_mode mode;
	struct buffer_cache_context cache_ctx;
	struct buffer_cache_context cache_tmp_ctx;
	int blt_handle;
	u8 crop_ratio;
};

struct scene_scale_factors {
	/* aspect ratio in 26.6 fixed point with remainder */
	uint32_t aqw;
	uint32_t arw;
	uint32_t dw;
	uint32_t aqh;
	uint32_t arh;
	uint32_t dh;
	uint32_t scene_width;
	uint32_t scene_height;
};

struct rect_span {
	uint32_t width;
	uint32_t height;
	uint32_t xtot;
	uint32_t ytot;
};

static int clonedev_blt(struct clonedev *cd,
		struct compdev_img *src_img,
		struct compdev_img *dst_img,
		bool blend, bool sync)
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
		enum hwmem_access access = HWMEM_ACCESS_READ |
				HWMEM_ACCESS_WRITE;
		size_t size;
		enum hwmem_mem_type memtype;

		req.src_img.buf.type = B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET;
		req.src_img.buf.hwmem_buf_name = src_img->buf.hwmem_buf_name;

		alloc = hwmem_resolve_by_name(src_img->buf.hwmem_buf_name);
		if (IS_ERR_OR_NULL(alloc)) {
			dev_err(cd->dev, "HWMEM resolve src failed\n");
		} else {
			hwmem_get_info(alloc, &size, &memtype,
					&access);
			if (!(access & HWMEM_ACCESS_IMPORT))
				hwmem_set_access(alloc,
					access | HWMEM_ACCESS_IMPORT,
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
	req.dst_rect.x = src_img->dst_rect.x;
	req.dst_rect.y = src_img->dst_rect.y;
	req.dst_rect.width = src_img->dst_rect.width;
	req.dst_rect.height = src_img->dst_rect.height;

	req.global_alpha = 0xff;
	req.flags = B2R2_BLT_FLAG_DITHER | B2R2_BLT_FLAG_ASYNCH;

	if (blend)
		req.flags |= B2R2_BLT_FLAG_PER_PIXEL_ALPHA_BLEND;

	dev_dbg(cd->dev, "%s: src_rect: x %d, y %d, w %d h %d, ",
			__func__, req.src_rect.x, req.src_rect.y,
			req.src_rect.width,	req.src_rect.height);
	dev_dbg(cd->dev, "%s: dst_rect: x %d, y %d, w %d h %d\n",
			__func__, req.dst_rect.x, req.dst_rect.y,
			req.dst_rect.width, req.dst_rect.height);

	req_id = b2r2_blt_request(cd->blt_handle, &req);
	if (req_id < 0)
		dev_err(cd->dev, "%s: Err b2r2_blt_request, handle %d, id %d",
			__func__, cd->blt_handle, req_id);
	else if (sync && b2r2_blt_synch(cd->blt_handle, req_id) < 0)
		dev_err(cd->dev, "%s: Could not perform b2r2_blt_synch",
			__func__);

	return req_id;
}

static void clonedev_best_fit(struct clonedev *cd,
		struct compdev_rect *src_rect,
		struct compdev_rect *dst_rect,
		int coord_rot,
		struct scene_scale_factors *ssfs)
{
	uint32_t dst_w = 0;
	uint32_t dst_h = 0;
	uint32_t dst_x = 0;
	uint32_t dst_y = 0;
	uint32_t span_x;
	uint32_t span_y;

	/* Adjust orientation of dst_rect */
	if (coord_rot % 180) {
		dst_w = dst_rect->height;
		dst_h = dst_rect->width;
		dst_x = dst_rect->y;
		dst_y = dst_rect->x;
	} else {
		dst_w = dst_rect->width;
		dst_h = dst_rect->height;
		dst_x = dst_rect->x;
		dst_y = dst_rect->y;
	}

	/* Scale up the dst_rect */
	if (ssfs->aqw != 0 || ssfs->arw != 0) {
		dst_x = (ssfs->aqw * dst_x +
			((ssfs->arw * dst_x) / ssfs->dw)) >> 6;
		dst_y = (ssfs->aqw * dst_y +
			((ssfs->arw * dst_y) / ssfs->dw)) >> 6;
		dst_w = (ssfs->aqw * dst_w +
			((ssfs->arw * dst_w) / ssfs->dw)) >> 6;
		dst_h = (ssfs->aqw * dst_h +
			((ssfs->arw * dst_h) / ssfs->dw)) >> 6;
	} else {
		dst_y = (ssfs->aqh * dst_y +
			((ssfs->arh * dst_y) / ssfs->dh)) >> 6;
		dst_x = (ssfs->aqh * dst_x +
			((ssfs->arh * dst_x) / ssfs->dh)) >> 6;
		dst_h = (ssfs->aqh * dst_h +
			((ssfs->arh * dst_h) / ssfs->dh)) >> 6;
		dst_w = (ssfs->aqh * dst_w +
			((ssfs->arh * dst_w) / ssfs->dh)) >> 6;
	}

	/* Center if necessary */
	if (ssfs->scene_width < cd->crop_rect.width)
		dst_x += (cd->crop_rect.width - ssfs->scene_width) >> 1;
	if (ssfs->scene_height < cd->crop_rect.height)
		dst_y += (cd->crop_rect.height - ssfs->scene_height) >> 1;

	span_x = dst_w + dst_x;
	span_y = dst_h + dst_y;

	/* Avoid offseting off screen */
	if (span_x > cd->crop_rect.width && dst_x > 0)
		dst_x -= min((span_x - cd->crop_rect.width), dst_x);
	if (span_y > cd->crop_rect.height && dst_y > 0)
		dst_y -= min((span_y - cd->crop_rect.height), dst_y);

	dev_dbg(cd->dev, "%s: x %u, y %u, w %u, h %u, spx %u, spy %u\n",
		__func__, dst_x, dst_y, dst_w, dst_h, span_x, span_y);

	dst_rect->x = dst_x;
	dst_rect->y = dst_y;
	dst_rect->width = dst_w;
	dst_rect->height = dst_h;
}

static int clonedev_set_mode_locked(struct clonedev *cd,
		enum clonedev_mode mode)
{
	cd->mode = mode;
	cd->scene_img_count = 0;
	cd->s_info.img_count = 1;

	if (cd->mode == CLONEDEV_CLONE_NONE)
		compdev_clear_screen(cd->dst_compdev);

	return 0;
}

static void clonedev_recalculate_cropping(struct clonedev *cd)
{
	u32 ratio;
	u32 cropped_width;
	u32 cropped_height;

	/* Use 16.16 fix point */
	ratio = ((u32)cd->crop_ratio << 16) / 100;
	cropped_width = (u32)cd->dst_size.width * ratio + (0x1 << 15);
	cropped_height = (u32)cd->dst_size.height * ratio + (0x1 << 15);

	cd->crop_rect.width = (cropped_width >> 16) & ~0x00000001;
	cd->crop_rect.height = (cropped_height >> 16) & ~0x00000001;
	cd->crop_rect.x = (cd->dst_size.width - cd->crop_rect.width) >> 1;
	cd->crop_rect.y = (cd->dst_size.height - cd->crop_rect.height) >> 1;
}

static int clonedev_set_crop_ratio_locked(struct clonedev *cd, u8 crop_ratio)
{
	int ret = 0;

	if (crop_ratio > 100 || crop_ratio < 1) {
		dev_dbg(cd->dev, "%s: Illegal crop ratio (%d)\n",
			__func__, crop_ratio);
		ret = -EINVAL;
	} else {
		cd->crop_ratio = crop_ratio;
		clonedev_recalculate_cropping(cd);
	}

	return ret;
}

static void clonedev_transform_to_rotation(
		enum compdev_transform transform,
		int *degrees,
		enum compdev_transform *residual)
{
	enum compdev_transform rem = COMPDEV_TRANSFORM_ROT_0;

	switch (transform) {
	case COMPDEV_TRANSFORM_ROT_0:
		*degrees = 0;
		break;
	case COMPDEV_TRANSFORM_ROT_90_CW:
		*degrees = 90;
		break;
	case COMPDEV_TRANSFORM_ROT_180:
		*degrees = 180;
		break;
	case COMPDEV_TRANSFORM_ROT_270_CW:
		*degrees = 270;
		break;
	case COMPDEV_TRANSFORM_FLIP_H:
		rem = COMPDEV_TRANSFORM_FLIP_H;
		*degrees = 0;
		break;
	case COMPDEV_TRANSFORM_FLIP_V:
		rem = COMPDEV_TRANSFORM_FLIP_V;
		*degrees = 0;
		break;
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_H:
		rem = COMPDEV_TRANSFORM_FLIP_H;
		*degrees = 90;
		break;
	case COMPDEV_TRANSFORM_ROT_90_CW_FLIP_V:
		rem = COMPDEV_TRANSFORM_FLIP_V;
		*degrees = 90;
		break;
	default:
		*degrees = 0;
		break;
	}

	if (residual != NULL)
		*residual = rem;
}

static enum compdev_transform clonedev_rotation_to_transform(int degree)
{
	switch (degree) {
	case 0:
		return COMPDEV_TRANSFORM_ROT_0;
	case 90:
		return COMPDEV_TRANSFORM_ROT_90_CW;
	case 180:
		return COMPDEV_TRANSFORM_ROT_180;
	case 270:
		return COMPDEV_TRANSFORM_ROT_270_CW;
	default:
		return 0;
	}
}

static void set_transform_and_dest_rect(struct clonedev *cd,
		struct compdev_img *img, int coord_rot,
		struct scene_scale_factors *ssfs)
{
	int img_rot;
	int mcde_rot;
	enum compdev_transform img_rem_trans = COMPDEV_TRANSFORM_ROT_0;
	int tot_rot;

	clonedev_transform_to_rotation(cd->s_info.hw_transform,
			&mcde_rot, NULL);
	clonedev_transform_to_rotation(img->transform,
			&img_rot, &img_rem_trans);

	dev_dbg(cd->dev, "%s: src_rect: x %d, y %d, w %d h %d\n",
		__func__, img->src_rect.x, img->src_rect.y,
		img->src_rect.width, img->src_rect.height);
	dev_dbg(cd->dev, "%s: dst_rect: x %d, y %d, w %d h %d\n",
		__func__, img->dst_rect.x, img->dst_rect.y,
		img->dst_rect.width, img->dst_rect.height);

	/* Check if layer is rendered already */
	if (img->flags & COMPDEV_OVERLAY_FLAG)
		tot_rot = (img_rot + coord_rot + mcde_rot) % 360;
	else
		tot_rot = (coord_rot + mcde_rot) % 360;

	dev_dbg(cd->dev,
		"%s: t 0x%02x, tot_rot %d, img_rot %d, mcde_rot %d, coord_rot %d\n",
		__func__, img->transform, tot_rot, img_rot, mcde_rot, coord_rot);

	img->transform = clonedev_rotation_to_transform(tot_rot) |
		img_rem_trans;

	/* Adjust destination rect */
	clonedev_best_fit(cd, &img->src_rect,
		&img->dst_rect,	coord_rot, ssfs);
}

static void get_bounding_rect(struct compdev_rect *rect1,
		struct compdev_rect *rect2, struct compdev_rect *bounds)
{
	bounds->x = min(rect1->x, rect2->x);
	bounds->y = min(rect1->y, rect2->y);
	bounds->width = max(rect1->x + rect1->width, rect2->x + rect2->width)
			- bounds->x;
	bounds->height = max(rect1->y + rect1->height, rect2->y + rect2->height)
			- bounds->y;
}

static enum compdev_fmt clonedev_compatible_fmt(enum compdev_fmt fmt)
{
	switch (fmt) {
	case COMPDEV_FMT_RGB565:
	case COMPDEV_FMT_RGB888:
	case COMPDEV_FMT_RGBA8888:
	case COMPDEV_FMT_RGBX8888:
		return fmt;
	case COMPDEV_FMT_YUV422:
	case COMPDEV_FMT_YCBCR42XMBN:
	case COMPDEV_FMT_YUV420_SP:
	case COMPDEV_FMT_YVU420_SP:
	case COMPDEV_FMT_YUV420_P:
	case COMPDEV_FMT_YVU420_P:
	case COMPDEV_FMT_YV12:
		return COMPDEV_FMT_RGB888;
	default:
		return COMPDEV_FMT_RGBA8888;
	}
}
static inline void set_src_dst_rects(struct compdev_img *img0,
		struct compdev_img *img1,
		struct compdev_rect *src_rect,
		struct compdev_rect *dst_rect)
{
	if (img1 != NULL) {
		get_bounding_rect(&img0->dst_rect,
			&img1->dst_rect,
			src_rect);
		*dst_rect = *src_rect;
		img0->dst_rect.x -= src_rect->x;
		img1->dst_rect.x -= src_rect->x;
		src_rect->x = 0;
		img0->dst_rect.y -= src_rect->y;
		img1->dst_rect.y -= src_rect->y;
		src_rect->y = 0;
	} else {
		*dst_rect = img0->dst_rect;
		img0->dst_rect.x = 0;
		img0->dst_rect.y = 0;
		*src_rect = img0->dst_rect;
	}
}

static u32 get_b2r2_color_black(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		return 0x80108010;
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
		return 0xFF000000;
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	default:
		return 0;
	}
}

static inline void clear_rect(struct clonedev *cd,
		struct b2r2_blt_req *bltreq, int x, int y,
		int width, int height)
{
	int req_id;

	bltreq->dst_rect.x = x;
	bltreq->dst_rect.y = y;
	bltreq->dst_rect.width = width;
	bltreq->dst_rect.height = height;
	if (width != 0 && height != 0) {
		req_id = b2r2_blt_request(cd->blt_handle, bltreq);
		if (req_id < 0)
			dev_err(cd->dev, "%s: Err b2r2_blt_request, id %d",
					__func__, req_id);
	}
}

static void clonedev_clear_background(struct clonedev *cd,
			struct compdev_img *dst_img,
			struct compdev_rect *img_rect)
{
	/* Clear the dst_img outside of the rect specified by img_rect */
	struct b2r2_blt_req *bltreq;

	bltreq = kzalloc(sizeof(*bltreq), GFP_KERNEL);
	if (bltreq == NULL)
		return;

	bltreq->size = sizeof(struct b2r2_blt_req);
	bltreq->flags = B2R2_BLT_FLAG_ASYNCH | B2R2_BLT_FLAG_SOURCE_FILL_RAW;
	bltreq->transform = B2R2_BLT_TRANSFORM_NONE;
	bltreq->dst_img.buf.type = B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET;
	bltreq->dst_img.buf.hwmem_buf_name = dst_img->buf.hwmem_buf_name;
	bltreq->dst_img.width = dst_img->width;
	bltreq->dst_img.height = dst_img->height;
	bltreq->dst_img.fmt = compdev_to_blt_format(dst_img->fmt);
	bltreq->dst_img.pitch = dst_img->pitch;
	bltreq->src_color = get_b2r2_color_black(bltreq->dst_img.fmt);

	clear_rect(cd, bltreq,
			0, 0, img_rect->x, dst_img->height);
	clear_rect(cd, bltreq,	img_rect->x, 0,
			img_rect->width, img_rect->y);
	clear_rect(cd, bltreq,	img_rect->x + img_rect->width,
			0, dst_img->width - img_rect->width - img_rect->x,
			dst_img->height);
	clear_rect(cd, bltreq,	img_rect->x,
			img_rect->y + img_rect->height,	img_rect->width,
			dst_img->height - img_rect->y - img_rect->height);

	kfree(bltreq);
}

static struct compdev_img_internal *clonedev_get_rotated_img(
		struct clonedev *cd, struct compdev_img *img,
		bool protected, int img_rot)
{
	struct compdev_img_internal *result = NULL;
	enum compdev_fmt dst_fmt;

	dst_fmt = clonedev_compatible_fmt(img->fmt);
	result = compdev_buffer_cache_get_image(
			&cd->cache_tmp_ctx, dst_fmt,
			img->height, img->width, protected);

	result->img.transform = COMPDEV_TRANSFORM_ROT_0;
	result->img.z_position = 0;

	/* Current dest rect is needed later */
	result->img.dst_rect = img->dst_rect;

	/* Set new dest rect (no scaling) */
	img->dst_rect.x = 0;
	img->dst_rect.y = 0;
	if (img_rot % 180) {
		img->dst_rect.height = img->src_rect.width;
		img->dst_rect.width = img->src_rect.height;
	} else {
		img->dst_rect.height = img->src_rect.height;
		img->dst_rect.width = img->src_rect.width;
	}

	/* Do rotation blit */
	clonedev_blt(cd, img, &result->img, false, true);

	/* Set new source rect */
	result->img.src_rect = img->dst_rect;

	return result;
}

static void get_span(struct compdev_rect *rect,
		struct rect_span *span, int rot)
{
	if (rot % 180) {
		span->width = rect->height;
		span->height = rect->width;
		span->xtot = rect->height + rect->y*2;
		span->ytot = rect->width + rect->x*2;
	} else {
		span->width = rect->width;
		span->height = rect->height;
		span->xtot = rect->width + rect->x*2;
		span->ytot = rect->height + rect->y*2;
	}
}

static void get_scene_scale_factors(struct clonedev *cd,
		struct compdev_img *img0,
		struct compdev_img *img1,
		int coord_rot,
		struct scene_scale_factors *ssfs)
{
	uint32_t nw, nh;
	bool base_on_width = false;
	struct rect_span spanbig;
	struct rect_span spansmall;
	struct rect_span span0;
	struct rect_span span1;

	get_span(&img0->dst_rect, &span0, coord_rot);

	if (img1 == NULL) {
		spanbig = span0;
	} else {
		get_span(&img1->dst_rect, &span1, coord_rot);
		if ((span0.width > span1.width &&
					span0.height <= span1.height) ||
				(span0.height > span1.height &&
					span0.width <= span1.width)) {
			spanbig = span0;
			spansmall = span1;
		} else {
			spanbig = span1;
			spansmall = span0;
		}
	}

	nw = cd->crop_rect.width << 6;
	ssfs->dw = spanbig.width;
	nh = cd->crop_rect.height << 6;
	ssfs->dh = spanbig.height;

	if (img1 == NULL) {
		if (span0.width >= span0.height)
			base_on_width = true;
	} else {
		if (spanbig.height >= spansmall.height)
			base_on_width = true;
	}

	if (base_on_width) {
		/* Aspect quotient and remainder based on width */
		ssfs->aqw = nw / ssfs->dw;
		ssfs->arw = nw % ssfs->dw;
		ssfs->aqh = 0;
		ssfs->arh = 0;

		ssfs->scene_width = cd->crop_rect.width;
		ssfs->scene_height = ((ssfs->aqw * spanbig.ytot +
			((ssfs->arw * spanbig.ytot) / ssfs->dw)) >> 6);
	} else {
		/* Aspect quotient and remainder based on height */
		ssfs->aqh = nh / ssfs->dh;
		ssfs->arh = nh % ssfs->dh;
		ssfs->aqw = 0;
		ssfs->arw = 0;

		ssfs->scene_width = ((ssfs->aqh * spanbig.xtot +
			((ssfs->arh * spanbig.xtot) / ssfs->dh)) >> 6);
		ssfs->scene_height = cd->crop_rect.height;
	}

	dev_dbg(cd->dev, "%s: crop_w %u, sc_w %u, crop_h %u, sc_h %u\n",
		__func__, cd->crop_rect.width, ssfs->scene_width,
		cd->crop_rect.height, ssfs->scene_height);

	dev_dbg(cd->dev,
		"%s: aqw %u, arw %u, dw %u, aqh %u, arh %u, dh %u\n",
		__func__, ssfs->aqw, ssfs->arw, ssfs->dw, ssfs->aqh,
		ssfs->arh, ssfs->dh);
}

static void clonedev_compose_locked(struct clonedev *cd)
{
	struct compdev_img *img0;
	struct compdev_img *img1 = NULL;
	bool protected = false;
	struct compdev_img_internal *dst_img;
	struct compdev_rect src_rect;
	struct compdev_rect dst_rect;
	struct compdev_img_internal *rotated_img = NULL;
	int b2r2_req_id;
	enum compdev_fmt dst_fmt;
	struct scene_scale_factors ssfs;
	int display_to_tv_rot = 360 - 90;
	int app_rot;

	clonedev_transform_to_rotation(cd->s_info.app_transform,
			&app_rot, NULL);
	display_to_tv_rot = (display_to_tv_rot + app_rot) % 360;

	/* Now there should be two images */
	if (cd->scene_img_count == 0) {
		dev_err(cd->dev, "%s: There should be two images at this "
				"point\n", __func__);
		return;
	}

	img0 = &cd->scene_images[0];
	if (cd->scene_img_count >= 2)
		img1 = &cd->scene_images[1];

	get_scene_scale_factors(cd, img0, img1,
			display_to_tv_rot, &ssfs);

	/* Adjust to output size */
	set_transform_and_dest_rect(cd, img0, display_to_tv_rot, &ssfs);
	if (img1)
		set_transform_and_dest_rect(cd, img1, display_to_tv_rot, &ssfs);

	/* Organize the images according to z-order, img1 on top of img0 */
	if (img1 != NULL && img0->z_position < img1->z_position) {
		struct compdev_img *temp;
		temp = img0;
		img0 = img1;
		img1 = temp;
	}

	if (img0->flags & COMPDEV_PROTECTED_FLAG ||
			(img1 != NULL && img1->flags & COMPDEV_PROTECTED_FLAG))
		protected = true;


	/*
	 * Get src_rect and dst_rect for completed image
	 * to be passed to MCDE
	 */
	set_src_dst_rects(img0, img1, &src_rect, &dst_rect);

	/*
	 * NOTE: Should be hard coded to RGB888 but somehow
	 * the mcde driver can not handle the offset into
	 * the destination for 24-bit source which happends
	 * in the boot app. So 24-bit will have to do for the
	 * video case.
	 */
	dst_fmt = clonedev_compatible_fmt(img0->fmt);

	dst_img = compdev_buffer_cache_get_image(&cd->cache_ctx, dst_fmt,
			src_rect.x + src_rect.width,
			src_rect.y + src_rect.height,
			protected);

	if (dst_img != NULL) {
		if (cd->blt_handle < 0) {
			dev_dbg(cd->dev, "%s: B2R2 opened\n", __func__);
			cd->blt_handle = b2r2_blt_open();
			if (cd->blt_handle < 0)
				dev_err(cd->dev, "%s(%d): Failed to "
					"open b2r2 device\n",
					__func__, __LINE__);
		}

		/* Set destination image parameters */
		dst_img->img.src_rect = src_rect;
		dst_img->img.dst_rect = dst_rect;
		dst_img->img.z_position = 1;
		dst_img->img.flags |= img0->flags;
		dst_img->img.transform = COMPDEV_TRANSFORM_ROT_0;

		/* Clear destination buf outside of img0 */
		clonedev_clear_background(cd, &dst_img->img,
			&img0->dst_rect);

		/* Handle the blit jobs */
		if (img1 == NULL) {
			b2r2_req_id = clonedev_blt(cd, img0, &dst_img->img,
						false, false);
		} else {
			int ret;
			int img1_rot;

			clonedev_transform_to_rotation(img1->transform,
				&img1_rot, NULL);

			if (img1_rot != 0)
				/*
				 * B2R2 cannot handle rotation, resizing and
				 * blending at the same time. Split it into
				 * two jobs.
				 */
				rotated_img = clonedev_get_rotated_img(cd,
						img1, protected, img1_rot);

			b2r2_req_id = clonedev_blt(cd, img0, &dst_img->img,
					false, false);
			if (rotated_img != NULL)
				ret = clonedev_blt(cd,
					&rotated_img->img, &dst_img->img,
					true, false);
			else
				ret = clonedev_blt(cd,
					img1, &dst_img->img,
					true, false);
			if (ret >= 0)
				b2r2_req_id = ret;
		}

		dst_img->img.dst_rect.x += cd->crop_rect.x;
		dst_img->img.dst_rect.y += cd->crop_rect.y;
		compdev_post_single_buffer_asynch(cd->dst_compdev,
				&dst_img->img, cd->blt_handle, b2r2_req_id);

		compdev_free_img(&cd->cache_ctx, dst_img);
		if (rotated_img != NULL)
			compdev_free_img(&cd->cache_tmp_ctx, rotated_img);
	} else {
		dev_err(cd->dev, "%s: Could not allocate hwmem "
				"temporary buffer\n", __func__);
	}
}

static void clonedev_post_buffer_callback(void *data,
		struct compdev_img *cb_img)
{
	struct clonedev *cd = (struct clonedev *)data;

	mutex_lock(&cd->lock);

	switch (cd->mode) {
	case CLONEDEV_CLONE_VIDEO_OR_UI:
		if (!cd->overlay_case || (cd->overlay_case &&
				(cb_img->flags & COMPDEV_OVERLAY_FLAG))) {
			cd->scene_images[cd->scene_img_count] = *cb_img;
			cd->scene_img_count++;
			clonedev_compose_locked(cd);
			cd->scene_img_count = 0;
			cd->s_info.img_count = 1;
			cd->s_info.reuse_fb_img = 0;
		}
		break;
	case CLONEDEV_CLONE_VIDEO_AND_UI:
		cd->scene_images[cd->scene_img_count] = *cb_img;
		cd->scene_img_count++;
		if (cd->scene_img_count >= cd->s_info.img_count) {
			clonedev_compose_locked(cd);
			cd->scene_img_count = 0;
			cd->s_info.img_count = 1;
			cd->s_info.reuse_fb_img = 0;
		}
		break;
	case CLONEDEV_CLONE_VIDEO:
	case CLONEDEV_CLONE_UI:
	case CLONEDEV_CLONE_NONE:
	default:
		break;
	}

	mutex_unlock(&cd->lock);
}

static void clonedev_post_scene_info_callback(void *data,
		struct compdev_scene_info *s_info)
{
	struct clonedev *cd = (struct clonedev *)data;

	mutex_lock(&cd->lock);
	switch (cd->mode) {
	case CLONEDEV_CLONE_VIDEO_OR_UI:
		if (s_info->reuse_fb_img) {
			cd->overlay_case = true;
		} else {
			if (s_info->img_count > 1)
				cd->overlay_case = true;
			else
				cd->overlay_case = false;
		}

		cd->s_info = *s_info;
		cd->s_info.img_count = 1;
		compdev_post_scene_info(cd->dst_compdev, &cd->s_info);
		break;
	case CLONEDEV_CLONE_VIDEO_AND_UI:
	{
		struct compdev_scene_info scene_info = *s_info;
		if (s_info->reuse_fb_img) {
			scene_info.img_count = 2;
			cd->overlay_case = true;
		} else {
			if (s_info->img_count > 1)
				cd->overlay_case = true;
			else
				cd->overlay_case = false;
		}

		cd->scene_img_count = 0;
		cd->s_info = scene_info;
		scene_info.img_count = 1;
		compdev_post_scene_info(cd->dst_compdev, &scene_info);
	}
		break;
	case CLONEDEV_CLONE_VIDEO:
		/* TODO: Implement */
		break;
	case CLONEDEV_CLONE_UI:
		/* TODO: Implement */
		break;
	case CLONEDEV_CLONE_NONE:
		/* Do nothing */
		break;
	default:
		break;
	}
	mutex_unlock(&cd->lock);
}

static void clonedev_dest_size_changed_callback(void *data,
		struct compdev_size *size)
{
	struct clonedev *cd = (void *)data;

	mutex_lock(&cd->lock);
	cd->dst_size = *size;
	clonedev_recalculate_cropping(cd);
	mutex_unlock(&cd->lock);
}

static int clonedev_open(struct inode *inode, struct file *file)
{
	struct clonedev *cd = NULL;

	mutex_lock(&dev_list_lock);
	list_for_each_entry(cd, &dev_list, list)
		if (cd->mdev.minor == iminor(inode))
			break;

	if (&cd->list == &dev_list) {
		mutex_unlock(&dev_list_lock);
		return -ENODEV;
	}

	if (cd->open) {
		mutex_unlock(&dev_list_lock);
		return -EBUSY;
	}

	cd->open = true;

	mutex_unlock(&dev_list_lock);

	file->private_data = cd;

	return 0;
}

static int clonedev_release(struct inode *inode, struct file *file)
{
	struct clonedev *cd = NULL;

	mutex_lock(&dev_list_lock);
	list_for_each_entry(cd, &dev_list, list)
		if (cd->mdev.minor == iminor(inode))
			break;

	if (&cd->list == &dev_list) {
		mutex_unlock(&dev_list_lock);
		return -ENODEV;
	}

	cd->open = false;

	mutex_unlock(&dev_list_lock);

	return 0;
}

static long clonedev_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int ret;
	enum clonedev_mode mode;
	u8 crop_ratio;
	struct clonedev *cd = (struct clonedev *)file->private_data;

	mutex_lock(&cd->lock);

	switch (cmd) {
	case CLONEDEV_SET_MODE_IOC:
		mode = (enum clonedev_mode)arg;
		ret = clonedev_set_mode_locked(cd, mode);
		break;
	case CLONEDEV_SET_CROP_RATIO_IOC:
		crop_ratio = (u8)arg;
		ret = clonedev_set_crop_ratio_locked(cd, crop_ratio);
		break;
	default:
		ret = -ENOSYS;
	}

	mutex_unlock(&cd->lock);

	return ret;
}

static const struct file_operations clonedev_fops = {
	.open = clonedev_open,
	.release = clonedev_release,
	.unlocked_ioctl = clonedev_ioctl,
};

static int init_clonedev(struct clonedev *cd)
{
#ifdef CONFIG_COMPDEV_JANITOR
	char wq_name[20];
	char wq_name2[20];
#endif
	mutex_init(&cd->lock);
	INIT_LIST_HEAD(&cd->list);
	cd->blt_handle = -1;
	memset(&cd->cache_ctx, 0, sizeof(cd->cache_ctx));
	memset(&cd->cache_tmp_ctx, 0, sizeof(cd->cache_tmp_ctx));

	cd->mdev.minor = MISC_DYNAMIC_MINOR;
	cd->mdev.name = cd->name;
	cd->mdev.fops = &clonedev_fops;

#ifdef CONFIG_COMPDEV_JANITOR
	snprintf(wq_name, sizeof(wq_name), "%s_janitor", cd->name);
	mutex_init(&cd->cache_ctx.janitor_lock);
	cd->cache_ctx.janitor_thread = create_workqueue(wq_name);
	if (!cd->cache_ctx.janitor_thread) {
		mutex_destroy(&cd->cache_ctx.janitor_lock);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK_DEFERRABLE(&cd->cache_ctx.free_buffers_work,
		compdev_free_cache_context_buffers);

	snprintf(wq_name2, sizeof(wq_name2), "%s_janitor2", cd->name);
	mutex_init(&cd->cache_tmp_ctx.janitor_lock);
	cd->cache_tmp_ctx.janitor_thread = create_workqueue(wq_name2);
	if (!cd->cache_tmp_ctx.janitor_thread) {
		mutex_destroy(&cd->cache_tmp_ctx.janitor_lock);
		return -ENOMEM;
	}
	INIT_DELAYED_WORK_DEFERRABLE(&cd->cache_tmp_ctx.free_buffers_work,
		compdev_free_cache_context_buffers);
#endif

	return 0;
}

int clonedev_create(void)
{
	int ret = 0;
	struct clonedev *cd;

	cd = kzalloc(sizeof(struct clonedev), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	mutex_lock(&dev_list_lock);

	snprintf(cd->name, sizeof(cd->name), "%s%d", CLONEDEV_DEFAULT_DEVICE_PREFIX,
			dev_counter++);
	if (init_clonedev(cd) < 0) {
		kfree(cd);
		dev_counter--;
		mutex_unlock(&dev_list_lock);
		return -ENOMEM;
	}

	mutex_lock(&cd->lock);

	ret = compdev_get(0, &cd->src_compdev);
	if (ret < 0)
		goto fail_register_misc;

	ret = compdev_get(1, &cd->dst_compdev);
	if (ret < 0)
		goto fail_register_misc;

	ret = compdev_get_size(cd->dst_compdev, &cd->dst_size);
	if (ret < 0)
		goto fail_register_misc;

	cd->crop_rect.x = 0;
	cd->crop_rect.y = 0;
	cd->crop_rect.width = cd->dst_size.width;
	cd->crop_rect.height = cd->dst_size.height;
	cd->crop_ratio = 100;

	ret = compdev_register_listener_callbacks(cd->src_compdev, (void *)cd,
			&clonedev_post_buffer_callback,
			&clonedev_post_scene_info_callback,
			NULL);
	if (ret < 0)
		goto fail_register_misc;

	ret = compdev_register_listener_callbacks(cd->dst_compdev, (void *)cd,
			NULL,
			NULL,
			&clonedev_dest_size_changed_callback);
	if (ret < 0)
		goto fail_register_misc;

	/* Default setting */
	cd->mode = CLONEDEV_CLONE_VIDEO_AND_UI;

	ret = misc_register(&cd->mdev);
	if (ret)
		goto fail_register_misc;

	list_add_tail(&cd->list, &dev_list);

	cd->dev = cd->mdev.this_device;
	cd->cache_ctx.dev = cd->dev;
	cd->cache_tmp_ctx.dev = cd->dev;

	mutex_unlock(&cd->lock);
	mutex_unlock(&dev_list_lock);

	return ret;

fail_register_misc:
	if (cd->src_compdev != NULL)
		compdev_put(cd->src_compdev);
	if (cd->dst_compdev != NULL)
		compdev_put(cd->dst_compdev);
#ifdef CONFIG_COMPDEV_JANITOR
	mutex_destroy(&cd->cache_ctx.janitor_lock);
	destroy_workqueue(cd->cache_ctx.janitor_thread);
	mutex_destroy(&cd->cache_tmp_ctx.janitor_lock);
	destroy_workqueue(cd->cache_tmp_ctx.janitor_thread);
#endif
	mutex_unlock(&cd->lock);
	kfree(cd);
	dev_counter--;
	mutex_unlock(&dev_list_lock);

	return ret;
}

void clonedev_destroy(void)
{
	struct clonedev *cd;
	struct clonedev *tmp;
	int i;

	mutex_lock(&dev_list_lock);
	list_for_each_entry_safe(cd, tmp, &dev_list, list) {
		compdev_deregister_callbacks(cd->src_compdev);
		compdev_put(cd->src_compdev);
		compdev_put(cd->dst_compdev);
		list_del(&cd->list);
		misc_deregister(&cd->mdev);

#ifdef CONFIG_COMPDEV_JANITOR
		cancel_delayed_work_sync(&cd->cache_ctx.free_buffers_work);
		flush_workqueue(cd->cache_ctx.janitor_thread);
		cancel_delayed_work_sync(&cd->cache_tmp_ctx.free_buffers_work);
		flush_workqueue(cd->cache_tmp_ctx.janitor_thread);
#endif

		for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
			if (cd->cache_ctx.img[i] != NULL) {
				kref_put(&cd->cache_ctx.img[i]->ref_count,
					compdev_image_release);
				cd->cache_ctx.img[i] = NULL;
			}
		}
		for (i = 0; i < BUFFER_CACHE_DEPTH; i++) {
			if (cd->cache_tmp_ctx.img[i] != NULL) {
				kref_put(&cd->cache_tmp_ctx.img[i]->ref_count,
					compdev_image_release);
				cd->cache_tmp_ctx.img[i] = NULL;
			}
		}

#ifdef CONFIG_COMPDEV_JANITOR
		mutex_destroy(&cd->cache_ctx.janitor_lock);
		destroy_workqueue(cd->cache_ctx.janitor_thread);
		mutex_destroy(&cd->cache_tmp_ctx.janitor_lock);
		destroy_workqueue(cd->cache_tmp_ctx.janitor_thread);
#endif

		if (cd->blt_handle >= 0)
			b2r2_blt_close(cd->blt_handle);

		kfree(cd);
		break;
	}
	dev_counter--;
	mutex_unlock(&dev_list_lock);
}

static void clonedev_destroy_all(void)
{
	struct clonedev *cd;
	struct clonedev *tmp;

	mutex_lock(&dev_list_lock);
	list_for_each_entry_safe(cd, tmp, &dev_list, list) {
		list_del(&cd->list);
		misc_deregister(&cd->mdev);
		kfree(cd);
	}
	mutex_unlock(&dev_list_lock);

	mutex_destroy(&dev_list_lock);
}

static int __init clonedev_init(void)
{
	pr_info("%s\n", __func__);

	mutex_init(&dev_list_lock);

	return 0;
}
module_init(clonedev_init);

static void __exit clonedev_exit(void)
{
	clonedev_destroy_all();
	pr_info("%s\n", __func__);
}
module_exit(clonedev_exit);

MODULE_AUTHOR("Per-Daniel Olsson <per-daniel.olsson@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Device for display cloning on external output");

