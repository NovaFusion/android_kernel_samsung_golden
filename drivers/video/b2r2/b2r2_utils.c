/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 utils
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include <video/b2r2_blt.h>

#include "b2r2_utils.h"
#include "b2r2_debug.h"
#include "b2r2_internal.h"
#include "b2r2_hw_convert.h"

const s32 b2r2_s32_max = 2147483647;


/**
 * calculate_scale_factor() - calculates the scale factor between the given
 *                            values
 */
int calculate_scale_factor(struct device *dev,
			u32 from, u32 to, u16 *sf_out)
{
	int ret;
	u32 sf;

	b2r2_log_info(dev, "%s\n", __func__);

	if (to == from) {
		*sf_out = 1 << 10;
		return 0;
	} else if (to == 0) {
		b2r2_log_err(dev, "%s: To is 0!\n", __func__);
		BUG_ON(1);
	}

	sf = (from << 10) / to;

	if ((sf & 0xffff0000) != 0) {
		/* Overflow error */
		b2r2_log_warn(dev, "%s: "
			"Scale factor too large\n", __func__);
		ret = -EINVAL;
		goto error;
	} else if (sf == 0) {
		b2r2_log_warn(dev, "%s: "
			"Scale factor too small\n", __func__);
		ret = -EINVAL;
		goto error;
	}

	*sf_out = (u16)sf;

	b2r2_log_info(dev, "%s exit\n", __func__);

	return 0;

error:
	b2r2_log_warn(dev, "%s: Exit...\n", __func__);
	return ret;
}

void b2r2_get_img_bounding_rect(struct b2r2_blt_img *img,
					struct b2r2_blt_rect *bounding_rect)
{
	bounding_rect->x = 0;
	bounding_rect->y = 0;
	bounding_rect->width = img->width;
	bounding_rect->height = img->height;
}


bool b2r2_is_zero_area_rect(struct b2r2_blt_rect *rect)
{
	return rect->width == 0 || rect->height == 0;
}

bool b2r2_is_rect_inside_rect(struct b2r2_blt_rect *rect1,
						struct b2r2_blt_rect *rect2)
{
	return rect1->x >= rect2->x &&
		rect1->y >= rect2->y &&
		rect1->x + rect1->width <= rect2->x + rect2->width &&
		rect1->y + rect1->height <= rect2->y + rect2->height;
}

bool b2r2_is_rect_gte_rect(struct b2r2_blt_rect *rect1,
		struct b2r2_blt_rect *rect2)
{
	return rect1->width >= rect2->width &&
			rect1->height >= rect2->height;
}

void b2r2_intersect_rects(struct b2r2_blt_rect *rect1,
	struct b2r2_blt_rect *rect2, struct b2r2_blt_rect *intersection)
{
	struct b2r2_blt_rect tmp_rect;

	tmp_rect.x = max(rect1->x, rect2->x);
	tmp_rect.y = max(rect1->y, rect2->y);
	tmp_rect.width = min(rect1->x + rect1->width, rect2->x + rect2->width)
				- tmp_rect.x;
	if (tmp_rect.width < 0)
		tmp_rect.width = 0;
	tmp_rect.height =
		min(rect1->y + rect1->height, rect2->y + rect2->height) -
				tmp_rect.y;
	if (tmp_rect.height < 0)
		tmp_rect.height = 0;

	*intersection = tmp_rect;
}

/*
 * Calculate new rectangles for the supplied
 * request, so that clipping to destination imaage
 * can be avoided.
 * Essentially, the new destination rectangle is
 * defined inside the old one. Given the transform
 * and scaling, one has to calculate which part of
 * the old source rectangle corresponds to
 * to the new part of old destination rectangle.
 */
void b2r2_trim_rects(struct device *dev,
			const struct b2r2_blt_req *req,
			struct b2r2_blt_rect *new_bg_rect,
			struct b2r2_blt_rect *new_dst_rect,
			struct b2r2_blt_rect *new_src_rect)
{
	enum b2r2_blt_transform transform = req->transform;
	struct b2r2_blt_rect *old_src_rect =
		(struct b2r2_blt_rect *) &req->src_rect;
	struct b2r2_blt_rect *old_dst_rect =
		(struct b2r2_blt_rect *) &req->dst_rect;
	struct b2r2_blt_rect *old_bg_rect =
		(struct b2r2_blt_rect *) &req->bg_rect;
	struct b2r2_blt_rect dst_img_bounds;
	s32 src_x = 0;
	s32 src_y = 0;
	s32 src_w = 0;
	s32 src_h = 0;
	s32 dx = 0;
	s32 dy = 0;
	s16 hsf;
	s16 vsf;

	b2r2_log_info(dev,
		"%s\nold_dst_rect(x,y,w,h)=(%d, %d, %d, %d)\n", __func__,
		old_dst_rect->x, old_dst_rect->y,
		old_dst_rect->width, old_dst_rect->height);
	b2r2_log_info(dev,
		"%s\nold_src_rect(x,y,w,h)=(%d, %d, %d, %d)\n", __func__,
		old_src_rect->x, old_src_rect->y,
		old_src_rect->width, old_src_rect->height);

	b2r2_get_img_bounding_rect((struct b2r2_blt_img *) &req->dst_img,
		&dst_img_bounds);

	/* dst_rect inside dst_img, no clipping necessary */
	if (b2r2_is_rect_inside_rect(old_dst_rect, &dst_img_bounds))
		goto keep_rects;

	b2r2_intersect_rects(old_dst_rect, &dst_img_bounds, new_dst_rect);
	b2r2_log_info(dev,
		"%s\nnew_dst_rect(x,y,w,h)=(%d, %d, %d, %d)\n", __func__,
		new_dst_rect->x, new_dst_rect->y,
		new_dst_rect->width, new_dst_rect->height);

	/* dst_rect completely outside, leave it to validation */
	if (new_dst_rect->width == 0 || new_dst_rect->height == 0)
		goto keep_rects;

	dx = new_dst_rect->x - old_dst_rect->x;
	dy = new_dst_rect->y - old_dst_rect->y;

	if (transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		int res = 0;
		res = calculate_scale_factor(dev, old_src_rect->width,
			old_dst_rect->height, &hsf);
		/* invalid dimensions, leave them to validation */
		if (res < 0)
			goto keep_rects;

		res = calculate_scale_factor(dev, old_src_rect->height,
			old_dst_rect->width, &vsf);
		if (res < 0)
			goto keep_rects;

		/*
		 * After applying the inverse transform
		 * for 90 degree rotation, the top-left corner
		 * becomes top-right.
		 * src_rect origin is defined as top-left,
		 * so a translation between dst and src
		 * coordinate spaces is necessary.
		 */
		src_x = (old_src_rect->width << 10) -
			hsf * (dy + new_dst_rect->height);
		src_y = dx * vsf;
		src_w = new_dst_rect->height * hsf;
		src_h = new_dst_rect->width * vsf;
	} else {
		int res = 0;
		res = calculate_scale_factor(dev, old_src_rect->width,
			old_dst_rect->width, &hsf);
		if (res < 0)
			goto keep_rects;

		res = calculate_scale_factor(dev, old_src_rect->height,
			old_dst_rect->height, &vsf);
		if (res < 0)
			goto keep_rects;

		src_x = dx * hsf;
		src_y = dy * vsf;
		src_w = new_dst_rect->width * hsf;
		src_h = new_dst_rect->height * vsf;
	}

	/*
	 * src_w must contain all the pixels that contribute
	 * to a particular destination rectangle.
	 * ((x + 0x3ff) >> 10) is equivalent to ceiling(x),
	 * expressed in 6.10 fixed point format.
	 * Every destination rectangle, maps to a certain area in the source
	 * rectangle. The area in source will most likely not be a rectangle
	 * with exact integer dimensions whenever arbitrary scaling is involved.
	 * Consider the following example.
	 * Suppose, that width of the current destination rectangle maps
	 * to 1.7 pixels in source, starting at x == 5.4, as calculated
	 * using the scaling factor.
	 * This means that while the destination rectangle is written,
	 * the source should be read from x == 5.4 up to x == 5.4 + 1.7 == 7.1
	 * Consequently, color from 3 pixels (x == 5, 6 and 7)
	 * needs to be read from source.
	 * The formula below the comment yields:
	 * ceil(0.4 + 1.7) == ceil(2.1) == 3
	 * (src_x & 0x3ff) is the fractional part of src_x,
	 * which is expressed in 6.10 fixed point format.
	 * Thus, width of the source area should be 3 pixels wide,
	 * starting at x == 5.
	 */
	src_w = ((src_x & 0x3ff) + src_w + 0x3ff) >> 10;
	src_h = ((src_y & 0x3ff) + src_h + 0x3ff) >> 10;

	src_x >>= 10;
	src_y >>= 10;

	if (transform & B2R2_BLT_TRANSFORM_FLIP_H)
		src_x = old_src_rect->width - src_x - src_w;

	if (transform & B2R2_BLT_TRANSFORM_FLIP_V)
		src_y = old_src_rect->height - src_y - src_h;

	/*
	 * Translate the src_rect coordinates into true
	 * src_buffer coordinates.
	 */
	src_x += old_src_rect->x;
	src_y += old_src_rect->y;

	new_src_rect->x = src_x;
	new_src_rect->y = src_y;
	new_src_rect->width = src_w;
	new_src_rect->height = src_h;

	b2r2_log_info(dev,
		"%s\nnew_src_rect(x,y,w,h)=(%d, %d, %d, %d)\n", __func__,
		new_src_rect->x, new_src_rect->y,
		new_src_rect->width, new_src_rect->height);

	if (req->flags & B2R2_BLT_FLAG_BG_BLEND) {
		/* Modify bg_rect in the same way as dst_rect */
		s32 dw = new_dst_rect->width - old_dst_rect->width;
		s32 dh = new_dst_rect->height - old_dst_rect->height;
		b2r2_log_info(dev,
			"%s\nold bg_rect(x,y,w,h)=(%d, %d, %d, %d)\n",
			__func__, old_bg_rect->x, old_bg_rect->y,
			old_bg_rect->width, old_bg_rect->height);
		new_bg_rect->x = old_bg_rect->x + dx;
		new_bg_rect->y = old_bg_rect->y + dy;
		new_bg_rect->width = old_bg_rect->width + dw;
		new_bg_rect->height = old_bg_rect->height + dh;
		b2r2_log_info(dev,
			"%s\nnew bg_rect(x,y,w,h)=(%d, %d, %d, %d)\n",
			__func__, new_bg_rect->x, new_bg_rect->y,
			new_bg_rect->width, new_bg_rect->height);
	}
	return;
keep_rects:
	/*
	 * Recalculation was not possible, or not necessary.
	 * Do not change anything, leave it to validation.
	 */
	*new_src_rect = *old_src_rect;
	*new_dst_rect = *old_dst_rect;
	*new_bg_rect = *old_bg_rect;
	b2r2_log_info(dev, "%s original rectangles preserved.\n", __func__);
	return;
}

int b2r2_get_fmt_bpp(struct device *dev, enum b2r2_blt_fmt fmt)
{
	/*
	 * Currently this function is not used that often but if that changes a
	 * lookup table could make it a lot faster.
	 */
	switch (fmt) {
	case B2R2_BLT_FMT_1_BIT_A1:
		return 1;

	case B2R2_BLT_FMT_8_BIT_A8:
		return 8;

	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12:
		return 12;

	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return 16;

	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
		return 24;

	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		return 32;

	default:
		b2r2_log_err(dev,
			"%s: Internal error! Format %#x not recognized.\n",
			__func__, fmt);
		return 32;
	}
}

int b2r2_get_fmt_y_bpp(struct device *dev, enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_YV12:
		return 8;

	default:
		b2r2_log_err(dev,
			"%s: Internal error! Non YCbCr format supplied.\n",
			__func__);
		return 8;
	}
}


bool b2r2_is_single_plane_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_independent_pixel_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_ycbcri_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_ycbcrsp_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_ycbcrp_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_ycbcr420_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_ycbcr422_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_ycbcr444_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
		return true;

	default:
		return false;
	}
}

bool b2r2_is_mb_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return true;

	default:
		return false;
	}
}

u32 b2r2_get_chroma_pitch(u32 luma_pitch, enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YV12:
		return b2r2_align_up(luma_pitch >> 1, 16);
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
		return luma_pitch;
	default:
		return luma_pitch >> 1;
	}
}

u32 b2r2_calc_pitch_from_width(struct device *dev,
		s32 width, enum b2r2_blt_fmt fmt)
{
	if (b2r2_is_single_plane_fmt(fmt)) {
		return (u32)b2r2_div_round_up(width *
			b2r2_get_fmt_bpp(dev, fmt), 8);
	} else if (b2r2_is_ycbcrsp_fmt(fmt) || b2r2_is_ycbcrp_fmt(fmt)) {
		return (u32)b2r2_div_round_up(width *
			b2r2_get_fmt_y_bpp(dev, fmt), 8);
	} else if (b2r2_is_mb_fmt(fmt)) {
		return b2r2_align_up((u32)b2r2_div_round_up(width *
			b2r2_get_fmt_y_bpp(dev, fmt), 8), 16);
	} else {
		b2r2_log_err(dev, "%s: Internal error! "
			"Pitchless format supplied.\n",
			__func__);
		return 0;
	}
}

u32 b2r2_get_img_pitch(struct device *dev, struct b2r2_blt_img *img)
{
	if (img->pitch != 0)
		return img->pitch;
	else
		return b2r2_calc_pitch_from_width(dev, img->width, img->fmt);
}

s32 b2r2_get_img_size(struct device *dev, struct b2r2_blt_img *img)
{
	if (b2r2_is_single_plane_fmt(img->fmt)) {
		return (s32)b2r2_get_img_pitch(dev, img) * img->height;
	} else if (b2r2_is_ycbcrsp_fmt(img->fmt) ||
			b2r2_is_ycbcrp_fmt(img->fmt)) {
		s32 y_plane_size;

		y_plane_size = (s32)b2r2_get_img_pitch(dev, img) * img->height;

		if (b2r2_is_ycbcr420_fmt(img->fmt)) {
			return y_plane_size + y_plane_size / 2;
		} else if (b2r2_is_ycbcr422_fmt(img->fmt)) {
			return y_plane_size * 2;
		} else if (b2r2_is_ycbcr444_fmt(img->fmt)) {
			return y_plane_size * 3;
		} else {
			b2r2_log_err(dev,	"%s: Internal error!"
				" Format %#x not recognized.\n",
				__func__, img->fmt);
			return 0;
		}
	} else if (b2r2_is_mb_fmt(img->fmt)) {
		return (img->width * img->height *
			b2r2_get_fmt_bpp(dev, img->fmt)) / 8;
	} else {
		b2r2_log_err(dev, "%s: Internal error! "
			"Format %#x not recognized.\n",
			__func__, img->fmt);
		return 0;
	}
}


s32 b2r2_div_round_up(s32 dividend, s32 divisor)
{
	s32 quotient = dividend / divisor;
	if (dividend % divisor != 0)
		quotient++;

	return quotient;
}

bool b2r2_is_aligned(s32 value, s32 alignment)
{
	return value % alignment == 0;
}

s32 b2r2_align_up(s32 value, s32 alignment)
{
	s32 remainder = abs(value) % abs(alignment);
	s32 value_to_add;

	if (remainder > 0) {
		if (value >= 0)
			value_to_add = alignment - remainder;
		else
			value_to_add = remainder;
	} else {
		value_to_add = 0;
	}

	return value + value_to_add;
}

/**
 * b2r2_get_alpha_range() - returns the alpha range of the given format
 */
enum b2r2_ty b2r2_get_alpha_range(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_8_BIT_A8:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
		return B2R2_TY_ALPHA_RANGE_255; /* 0 - 255 */
	default:
		return B2R2_TY_ALPHA_RANGE_128; /* 0 - 128 */
	}
}

/**
 * b2r2_get_alpha() - returns the pixel alpha in 0...255 range
 */
u8 b2r2_get_alpha(enum b2r2_blt_fmt fmt, u32 pixel)
{
	switch (fmt) {
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
		return (pixel >> 24) & 0xff;
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		return pixel & 0xff;
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
		return (pixel & 0xfff) >> 16;
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		return (((pixel >> 12) & 0xf) * 255) / 15;
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
		return (pixel >> 15) * 255;
	case B2R2_BLT_FMT_1_BIT_A1:
		return pixel * 255;
	case B2R2_BLT_FMT_8_BIT_A8:
		return pixel;
	default:
		return 255;
	}
}

/**
 * b2r2_set_alpha() - returns a color value with the alpha component set
 */
u32 b2r2_set_alpha(enum b2r2_blt_fmt fmt, u8 alpha, u32 color)
{
	u32 alpha_mask;

	switch (fmt) {
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
		color &= 0x00ffffff;
		alpha_mask = alpha << 24;
		break;
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		color &= 0xffffff00;
		alpha_mask = alpha;
		break;
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
		color &= 0x00ffff;
		alpha_mask = alpha << 16;
		break;
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		color &= 0x0fff;
		alpha_mask = (alpha << 8) & 0xF000;
		break;
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
		color &= 0x7fff;
		alpha_mask = (alpha / 255) << 15 ;
		break;
	case B2R2_BLT_FMT_1_BIT_A1:
		color = 0;
		alpha_mask = (alpha / 255);
		break;
	case B2R2_BLT_FMT_8_BIT_A8:
		color = 0;
		alpha_mask = alpha;
		break;
	default:
		alpha_mask = 0;
	}

	return color | alpha_mask;
}

/**
 * b2r2_fmt_has_alpha() - returns whether the given format carries an alpha value
 */
bool b2r2_fmt_has_alpha(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_is_rgb_fmt() - returns whether the given format is a rgb format
 */
bool b2r2_is_rgb_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_is_bgr_fmt() - returns whether the given format is a bgr format
 */
bool b2r2_is_bgr_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_is_yuv_fmt() - returns whether the given format is a yuv format
 */
bool b2r2_is_yuv_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_is_yvu_fmt() - returns whether the given format is a yvu format
 */
bool b2r2_is_yvu_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_is_yuv420_fmt() - returns whether the given format is a yuv420 format
 */
bool b2r2_is_yuv420_fmt(enum b2r2_blt_fmt fmt)
{

	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
		return true;
	default:
		return false;
	}
}

bool b2r2_is_yuv422_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_is_yvu420_fmt() - returns whether the given format is a yvu420 format
 */
bool b2r2_is_yvu420_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return true;
	default:
		return false;
	}
}

bool b2r2_is_yvu422_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		return true;
	default:
		return false;
	}
}


/**
 * b2r2_is_yuv444_fmt() - returns whether the given format is a yuv444 format
 */
bool b2r2_is_yuv444_fmt(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
		return true;
	default:
		return false;
	}
}

/**
 * b2r2_fmt_byte_pitch() - returns the pitch of a pixmap with the given width
 */
int b2r2_fmt_byte_pitch(enum b2r2_blt_fmt fmt, u32 width)
{
	int pitch;

	switch (fmt) {

	case B2R2_BLT_FMT_1_BIT_A1:
		pitch = width >> 3; /* Shift is faster than division */
		if ((width & 0x3) != 0) /* Check for remainder */
			pitch++;
		return pitch;

	case B2R2_BLT_FMT_8_BIT_A8:                        /* Fall through */
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:            /* Fall through */
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:            /* Fall through */
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE: /* Fall through */
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:       /* Fall through */
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:       /* Fall through */
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:            /* Fall through */
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:            /* Fall through */
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE: /* Fall through */
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:       /* Fall through */
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:       /* Fall through */
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return width;

	case B2R2_BLT_FMT_16_BIT_ARGB4444: /* Fall through */
	case B2R2_BLT_FMT_16_BIT_ABGR4444: /* Fall through */
	case B2R2_BLT_FMT_16_BIT_ARGB1555: /* Fall through */
	case B2R2_BLT_FMT_16_BIT_RGB565:   /* Fall through */
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		return width << 1;

	case B2R2_BLT_FMT_24_BIT_RGB888:   /* Fall through */
	case B2R2_BLT_FMT_24_BIT_ARGB8565: /* Fall through */
	case B2R2_BLT_FMT_24_BIT_YUV888:   /* Fall through */
	case B2R2_BLT_FMT_24_BIT_VUY888:
		return width * 3;

	case B2R2_BLT_FMT_32_BIT_ARGB8888: /* Fall through */
	case B2R2_BLT_FMT_32_BIT_ABGR8888: /* Fall through */
	case B2R2_BLT_FMT_32_BIT_AYUV8888: /* Fall through */
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		return width << 2;

	default:
		/* Should never, ever happen */
		BUG_ON(1);
		return 0;
	}
}

/**
 * b2r2_to_native_fmt() - returns the native B2R2 format
 */
enum b2r2_native_fmt b2r2_to_native_fmt(enum b2r2_blt_fmt fmt)
{

	switch (fmt) {
	case B2R2_BLT_FMT_UNUSED:
		return B2R2_NATIVE_RGB565;
	case B2R2_BLT_FMT_1_BIT_A1:
		return B2R2_NATIVE_A1;
	case B2R2_BLT_FMT_8_BIT_A8:
		return B2R2_NATIVE_A8;
	case B2R2_BLT_FMT_16_BIT_RGB565:
		return B2R2_NATIVE_RGB565;
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		return B2R2_NATIVE_ARGB4444;
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
		return B2R2_NATIVE_ARGB1555;
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
		return B2R2_NATIVE_ARGB8565;
	case B2R2_BLT_FMT_24_BIT_RGB888:
		return B2R2_NATIVE_RGB888;
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_24_BIT_VUY888: /* Not actually supported by HW */
		return B2R2_NATIVE_YCBCR888;
	case B2R2_BLT_FMT_32_BIT_ABGR8888: /* Not actually supported by HW */
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
		return B2R2_NATIVE_ARGB8888;
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888: /* Not actually supported by HW */
		return B2R2_NATIVE_AYCBCR8888;
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		return B2R2_NATIVE_YCBCR422R;
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		return B2R2_NATIVE_YCBCR42X_R2B;
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return B2R2_NATIVE_YCBCR42X_MBN;
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return B2R2_NATIVE_YUV;
	default:
		/* Should never ever happen */
		return B2R2_NATIVE_BYTE;
	}
}

/**
 * Bit-expand the color from fmt to RGB888 with blue at LSB.
 * Copy MSBs into missing LSBs.
 */
u32 b2r2_to_RGB888(u32 color, const enum b2r2_blt_fmt fmt)
{
	u32 out_color = 0;
	u32 r = 0;
	u32 g = 0;
	u32 b = 0;
	switch (fmt) {
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
		r = ((color & 0xf00) << 12) | ((color & 0xf00) << 8);
		g = ((color & 0xf0) << 8) | ((color & 0xf0) << 4);
		b = ((color & 0xf) << 4) | (color & 0xf);
		out_color = r | g | b;
		break;
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		b = ((color & 0xf00) << 12) | ((color & 0xf00) << 8);
		g = ((color & 0xf0) << 8) | ((color & 0xf0) << 4);
		r = ((color & 0xf) << 4) | (color & 0xf);
		out_color = r | g | b;
		break;
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
		r = ((color & 0x7c00) << 9) | ((color & 0x7000) << 4);
		g = ((color & 0x3e0) << 6) | ((color & 0x380) << 1);
		b = ((color & 0x1f) << 3) | ((color & 0x1c) >> 2);
		out_color = r | g | b;
		break;
	case B2R2_BLT_FMT_16_BIT_RGB565:
		r = ((color & 0xf800) << 8) | ((color & 0xe000) << 3);
		g = ((color & 0x7e0) << 5) | ((color & 0x600) >> 1);
		b = ((color & 0x1f) << 3) | ((color & 0x1c) >> 2);
		out_color = r | g | b;
		break;
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
		out_color = color & 0xffffff;
		break;
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
		r = (color & 0xff) << 16;
		g = color & 0xff00;
		b = (color & 0xff0000) >> 16;
		out_color = r | g | b;
		break;
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
		r = ((color & 0xf800) << 8) | ((color & 0xe000) << 3);
		g = ((color & 0x7e0) << 5) | ((color & 0x600) >> 1);
		b = ((color & 0x1f) << 3) | ((color & 0x1c) >> 2);
		out_color = r | g | b;
		break;
	default:
		break;
	}

	return out_color;
}

/**
 * b2r2_get_fmt_type() - returns the type of the given format (raster, planar, etc.)
 */
enum b2r2_fmt_type b2r2_get_fmt_type(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
		return B2R2_FMT_TYPE_RASTER;
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		return B2R2_FMT_TYPE_PLANAR;
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return B2R2_FMT_TYPE_SEMI_PLANAR;
	default:
		return B2R2_FMT_TYPE_RASTER;
	}
}

#ifdef CONFIG_DEBUG_FS
/*
 * Similar behaviour to vsnprintf.
 *
 * parameters:
 * buf - base address of string
 * cur_size - current offset into string buffer
 * max_size - size of string buffer from the base address
 * (e.g. "char string[16]" -> max_size = 16)
 * format - format string
 *
 * IFF buf == 0, then the return value is the number of characters
 * (excluding the trailing '\0')
 * that would have been written were the buffer large enough.
 *
 * Otherwise,
 * the return value is the number of characters written to the buffer,
 * with the following caveats:
 *     output was truncated:
 *         number of characters written INCLUDING trailing '\0'
 *     otherwise:
 *         number of characters written EXCLUDING trailing '\0'
 *
 */
static size_t b2r2_snprintf(char *buf, size_t cur_size,
				size_t max_size, char *format, ...)
{
	size_t ret;
	va_list args;
	va_start(args, format);

	buf = buf != 0 ? buf + cur_size : 0;
	max_size = max_size < cur_size ? 0 : max_size - cur_size;

	if (buf) {
		ret = vsnprintf(buf, max_size, format, args);
		return ret > max_size ? max_size : ret;
	} else
		return vsnprintf(0, 0, format, args);
}

/**
 * sprintf_req() - Builds a string representing the request, for debug
 *
 * @request:Request that should be encoded into a string
 * @buf: Receiving buffer
 * @size: Size of receiving buffer
 *
 * Returns number of characters in string, excluding null terminator
 */
int sprintf_req(struct b2r2_blt_request *request, char *buf, int size)
{
	size_t dev_size = 0;
	size_t max_size = (size_t) size;
	enum b2r2_color_conversion cc = b2r2_get_color_conversion(
		request->user_req.src_img.fmt,
		request->user_req.dst_img.fmt,
		((request->user_req.flags &
			B2R2_BLT_FLAG_FULL_RANGE_YUV) ==
				B2R2_BLT_FLAG_FULL_RANGE_YUV));

	/* generic request info */
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"instance      : 0x%08lX\n",
		(unsigned long) request->instance);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"size          : %d bytes\n", request->user_req.size);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"flags         : 0x%08lX\n",
		(unsigned long) request->user_req.flags);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"transform     : %d\n",
		(int) request->user_req.transform);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"prio          : %d\n", request->user_req.transform);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"global_alpha  : %d\n",
		(int) request->user_req.global_alpha);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"report1       : 0x%08lX\n",
		(unsigned long) request->user_req.report1);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"report2       : 0x%08lX\n",
		(unsigned long) request->user_req.report2);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"request_id    : 0x%08lX\n",
		(unsigned long) request->request_id);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"fmt_conv:     : 0x%08lX\n\n",
		(unsigned long) cc);

	/* src info */
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_img.fmt   : %s (%#010x)\n",
		b2r2_fmt_to_string(request->user_req.src_img.fmt),
		request->user_req.src_img.fmt);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_img.buf   : {type=%d, hwmem_buf_name=%d, fd=%d, "
		"offset=%d, len=%d}\n",
		request->user_req.src_img.buf.type,
		request->user_req.src_img.buf.hwmem_buf_name,
		request->user_req.src_img.buf.fd,
		request->user_req.src_img.buf.offset,
		request->user_req.src_img.buf.len);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_img       : {width=%d, height=%d, pitch=%d}\n",
		request->user_req.src_img.width,
		request->user_req.src_img.height,
		request->user_req.src_img.pitch);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask.fmt  : %s (%#010x)\n",
		b2r2_fmt_to_string(request->user_req.src_mask.fmt),
		request->user_req.src_mask.fmt);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask.buf  : {type=%d, hwmem_buf_name=%d, fd=%d,"
		" offset=%d, len=%d}\n",
		request->user_req.src_mask.buf.type,
		request->user_req.src_mask.buf.hwmem_buf_name,
		request->user_req.src_mask.buf.fd,
		request->user_req.src_mask.buf.offset,
		request->user_req.src_mask.buf.len);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask      : {width=%d, height=%d, pitch=%d}\n",
		request->user_req.src_mask.width,
		request->user_req.src_mask.height,
		request->user_req.src_mask.pitch);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_rect      : {x=%d, y=%d, width=%d, height=%d}\n",
		request->user_req.src_rect.x,
		request->user_req.src_rect.y,
		request->user_req.src_rect.width,
		request->user_req.src_rect.height);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_color     : 0x%08lX\n\n",
		(unsigned long) request->user_req.src_color);

	/* bg info */
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"bg_img.fmt    : %s (%#010x)\n",
		b2r2_fmt_to_string(request->user_req.bg_img.fmt),
		request->user_req.bg_img.fmt);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"bg_img.buf    : {type=%d, hwmem_buf_name=%d, fd=%d,"
		" offset=%d, len=%d}\n",
		request->user_req.bg_img.buf.type,
		request->user_req.bg_img.buf.hwmem_buf_name,
		request->user_req.bg_img.buf.fd,
		request->user_req.bg_img.buf.offset,
		request->user_req.bg_img.buf.len);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"bg_img        : {width=%d, height=%d, pitch=%d}\n",
		request->user_req.bg_img.width,
		request->user_req.bg_img.height,
		request->user_req.bg_img.pitch);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"bg_rect       : {x=%d, y=%d, width=%d, height=%d}\n\n",
		request->user_req.bg_rect.x,
		request->user_req.bg_rect.y,
		request->user_req.bg_rect.width,
		request->user_req.bg_rect.height);

	/* dst info */
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_img.fmt   : %s (%#010x)\n",
		b2r2_fmt_to_string(request->user_req.dst_img.fmt),
		request->user_req.dst_img.fmt);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_img.buf   : {type=%d, hwmem_buf_name=%d, fd=%d,"
		" offset=%d, len=%d}\n",
		request->user_req.dst_img.buf.type,
		request->user_req.dst_img.buf.hwmem_buf_name,
		request->user_req.dst_img.buf.fd,
		request->user_req.dst_img.buf.offset,
		request->user_req.dst_img.buf.len);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_img       : {width=%d, height=%d, pitch=%d}\n",
		request->user_req.dst_img.width,
		request->user_req.dst_img.height,
		request->user_req.dst_img.pitch);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_rect      : {x=%d, y=%d, width=%d, height=%d}\n",
		request->user_req.dst_rect.x,
		request->user_req.dst_rect.y,
		request->user_req.dst_rect.width,
		request->user_req.dst_rect.height);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_clip_rect : {x=%d, y=%d, width=%d, height=%d}\n",
		request->user_req.dst_clip_rect.x,
		request->user_req.dst_clip_rect.y,
		request->user_req.dst_clip_rect.width,
		request->user_req.dst_clip_rect.height);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_color     : 0x%08lX\n\n",
		(unsigned long) request->user_req.dst_color);

	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_resolved.physical                  : 0x%08lX\n",
		(unsigned long) request->src_resolved.
		physical_address);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_resolved.virtual                   : 0x%08lX\n",
		(unsigned long) request->src_resolved.virtual_address);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_resolved.filep                     : 0x%08lX\n",
		(unsigned long) request->src_resolved.filep);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_resolved.filep_physical_start      : 0x%08lX\n",
		(unsigned long) request->src_resolved.
		file_physical_start);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_resolved.filep_virtual_start       : 0x%08lX\n",
		(unsigned long) request->src_resolved.file_virtual_start);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_resolved.file_len                  : %d\n\n",
		request->src_resolved.file_len);

	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask_resolved.physical             : 0x%08lX\n",
		(unsigned long) request->src_mask_resolved.
		physical_address);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask_resolved.virtual              : 0x%08lX\n",
		(unsigned long) request->src_mask_resolved.virtual_address);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask_resolved.filep                : 0x%08lX\n",
		(unsigned long) request->src_mask_resolved.filep);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask_resolved.filep_physical_start : 0x%08lX\n",
		(unsigned long) request->src_mask_resolved.
		file_physical_start);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask_resolved.filep_virtual_start  : 0x%08lX\n",
		(unsigned long) request->src_mask_resolved.
		file_virtual_start);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"src_mask_resolved.file_len             : %d\n\n",
		request->src_mask_resolved.file_len);

	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_resolved.physical                  : 0x%08lX\n",
		(unsigned long) request->dst_resolved.
		physical_address);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_resolved.virtual                   : 0x%08lX\n",
		(unsigned long) request->dst_resolved.virtual_address);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_resolved.filep                     : 0x%08lX\n",
		(unsigned long) request->dst_resolved.filep);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_resolved.filep_physical_start      : 0x%08lX\n",
		(unsigned long) request->dst_resolved.
		file_physical_start);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_resolved.filep_virtual_start       : 0x%08lX\n",
		(unsigned long) request->dst_resolved.file_virtual_start);
	dev_size += b2r2_snprintf(buf, dev_size,
		max_size,
		"dst_resolved.file_len                  : %d\n\n",
		request->dst_resolved.file_len);

	return dev_size;
}
#endif

void b2r2_recalculate_rects(struct device *dev,
		struct b2r2_blt_req *req)
{
	struct b2r2_blt_rect new_dst_rect;
	struct b2r2_blt_rect new_src_rect;
	struct b2r2_blt_rect new_bg_rect;

	b2r2_trim_rects(dev,
		req, &new_bg_rect, &new_dst_rect, &new_src_rect);

	req->dst_rect = new_dst_rect;
	req->src_rect = new_src_rect;
	if (req->flags & B2R2_BLT_FLAG_BG_BLEND)
		req->bg_rect = new_bg_rect;
}

const char *b2r2_fmt_to_string(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_UNUSED:
		return "UNUSED";
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
		return "ARGB4444";
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
		return "ARGB1555";
	case B2R2_BLT_FMT_16_BIT_RGB565:
		return "RGB565";
	case B2R2_BLT_FMT_24_BIT_RGB888:
		return "RGB888";
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
		return "ARGB8888";
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
		return "YUV420P";
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
		return "YUV422P";
	case B2R2_BLT_FMT_Y_CB_Y_CR:
		return "YUV422I";
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		return "YUV422R";
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
		return "YUV420SP";
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
		return "YUV422SP";
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
		return "ABGR8888";
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
		return "ARGB8565";
	case B2R2_BLT_FMT_24_BIT_YUV888:
		return "YUV888";
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
		return "AYUV8888";
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
		return "YUV420MB";
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return "YUV422MB";
	case B2R2_BLT_FMT_1_BIT_A1:
		return "1_BIT_A1";
	case B2R2_BLT_FMT_8_BIT_A8:
		return "8_BIT_A8";
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
		return "YUV444P";
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
		return "YVU420SP";
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		return "YVU422SP";
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
		return "YVU420P";
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
		return "YVU422P";
	case B2R2_BLT_FMT_24_BIT_VUY888:
		return "VUY888";
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		return "VUYA8888";
	case B2R2_BLT_FMT_YV12:
		return "YV12";
	default:
		return "UNKNOWN";
	}
}

void b2r2_get_cb_cr_addr(u32 phy_base_addr, u32 luma_pitch, u32 height,
		enum b2r2_blt_fmt fmt, u32 *cb_addr, u32 *cr_addr)
{
	u32 chroma_pitch = b2r2_get_chroma_pitch(luma_pitch, fmt);
	u32 plane1, plane2;

	if (cr_addr == NULL || cb_addr == NULL)
		return;

	plane1 = phy_base_addr + luma_pitch * height;

	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		plane2 = plane1 + chroma_pitch * (height >> 1);
		break;
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
		plane2 = plane1 + chroma_pitch * height;
		break;
	default:
		plane2 = plane1;
		break;
	}

	if (b2r2_is_yvu_fmt(fmt)) {
		*cb_addr = plane2;
		*cr_addr = plane1;
	} else {
		*cb_addr = plane1;
		*cr_addr = plane2;
	}
}
