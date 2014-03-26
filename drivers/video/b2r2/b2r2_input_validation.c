/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com> for ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 */


#include "b2r2_internal.h"
#include "b2r2_input_validation.h"
#include "b2r2_debug.h"
#include "b2r2_utils.h"

#include <video/b2r2_blt.h>
#include <linux/kernel.h>
#include <linux/errno.h>


static bool is_valid_format(enum b2r2_blt_fmt fmt);
static bool is_valid_bg_format(enum b2r2_blt_fmt fmt);

static bool is_valid_pitch_for_fmt(struct device *dev,
		u32 pitch, s32 width, enum b2r2_blt_fmt fmt);

static bool is_aligned_width_for_fmt(s32 width, enum b2r2_blt_fmt fmt);
static s32 width_2_complete_width(s32 width, enum b2r2_blt_fmt fmt);
static bool is_complete_width_for_fmt(s32 width, enum b2r2_blt_fmt fmt);
static bool is_valid_height_for_fmt(s32 height, enum b2r2_blt_fmt fmt);

static bool validate_img(struct device *dev,
		struct b2r2_blt_img *img);
static bool validate_rect(struct device *dev,
		struct b2r2_blt_rect *rect);


static bool is_valid_format(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
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
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_YV12:
		return true;

	default:
		return false;
	}
}

static bool is_valid_bg_format(enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12:
		return false;
	default:
		return true;
	}
}


static bool is_valid_pitch_for_fmt(struct device *dev,
		u32 pitch, s32 width, enum b2r2_blt_fmt fmt)
{
	s32 complete_width;
	u32 pitch_derived_from_width;

	complete_width =  width_2_complete_width(width, fmt);

	pitch_derived_from_width = b2r2_calc_pitch_from_width(dev,
			complete_width, fmt);

	if (pitch < pitch_derived_from_width)
		return false;

	switch (fmt) {
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
		if (!b2r2_is_aligned(pitch, 2))
			return false;

		break;

	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		if (!b2r2_is_aligned(pitch, 4))
			return false;

		break;

	default:
		break;
	}

	return true;
}


static bool is_aligned_width_for_fmt(s32 width, enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
		if (!b2r2_is_aligned(width, 4))
			return false;

		break;

	case B2R2_BLT_FMT_1_BIT_A1:
		if (!b2r2_is_aligned(width, 8))
			return false;

		break;

	case B2R2_BLT_FMT_CB_Y_CR_Y:
		if (!b2r2_is_aligned(width, 2))
			return false;

		break;

	default:
		break;
	}

	return true;
}

static s32 width_2_complete_width(s32 width, enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		return b2r2_align_up(width, 2);

	case B2R2_BLT_FMT_YV12:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return b2r2_align_up(width, 16);

	default:
		return width;
	}
}

static bool is_complete_width_for_fmt(s32 width, enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		if (!b2r2_is_aligned(width, 2))
			return false;

		break;

	case B2R2_BLT_FMT_YV12:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		if (!b2r2_is_aligned(width, 16))
			return false;

		break;

	default:
		break;
	}

	return true;
}

static bool is_valid_height_for_fmt(s32 height, enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YV12:
		if (!b2r2_is_aligned(height, 2))
			return false;

		break;

	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		if (!b2r2_is_aligned(height, 16))
			return false;

		break;

	default:
		break;
	}

	return true;
}

static bool validate_img(struct device *dev,
		struct b2r2_blt_img *img)
{
	/*
	 * So that we always can do width * height * bpp without overflowing a
	 * 32 bit signed integer. isqrt(s32_max / max_bpp) was used to
	 * calculate the value.
	 */
	static const s32 max_img_width_height = 8191;

	s32 img_size;

	if (!is_valid_format(img->fmt)) {
		b2r2_log_info(dev, "Validation Error: "
				"!is_valid_format(img->fmt)\n");
		return false;
	}

	if (img->width < 0 || img->width > max_img_width_height ||
		img->height < 0 || img->height > max_img_width_height) {
		b2r2_log_info(dev, "Validation Error: "
				"img->width < 0 || "
				"img->width > max_img_width_height || "
				"img->height < 0 || "
				"img->height > max_img_width_height\n");
		return false;
	}

	if (b2r2_is_mb_fmt(img->fmt)) {
		if (!is_complete_width_for_fmt(img->width, img->fmt)) {
			b2r2_log_info(dev, "Validation Error: "
					"!is_complete_width_for_fmt(img->width,"
					" img->fmt)\n");
			return false;
		}
	} else {
		if (0 == img->pitch &&
			(!is_aligned_width_for_fmt(img->width, img->fmt) ||
			!is_complete_width_for_fmt(img->width, img->fmt))) {
			b2r2_log_info(dev,
					"Validation Error: "
					"0 == img->pitch && "
					"(!is_aligned_width_for_fmt(img->width,"
					" img->fmt) || "
					"!is_complete_width_for_fmt(img->width,"
					" img->fmt))\n");
			return false;
		}

		if (img->pitch != 0 &&
			!is_valid_pitch_for_fmt(dev, img->pitch, img->width,
				img->fmt)) {
			b2r2_log_info(dev,
				"Validation Error: "
				"img->pitch != 0 && "
				"!is_valid_pitch_for_fmt(dev, "
				"img->pitch, img->width, img->fmt)\n");
			return false;
		}
	}

	if (!is_valid_height_for_fmt(img->width, img->fmt)) {
		b2r2_log_info(dev, "Validation Error: "
				"!is_valid_height_for_fmt(img->width, img->fmt)\n");
		return false;
	}

	img_size = b2r2_get_img_size(dev, img);

	/*
	 * To keep the entire image inside s32 range.
	 */
	if ((B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET == img->buf.type ||
				B2R2_BLT_PTR_FD_OFFSET == img->buf.type) &&
			img->buf.offset > (u32)b2r2_s32_max - (u32)img_size) {
		b2r2_log_info(dev, "Validation Error: "
				"(B2R2_BLT_PTR_HWMEM_BUF_NAME_OFFSET == "
				"img->buf.type || B2R2_BLT_PTR_FD_OFFSET == "
				"img->buf.type) && img->buf.offset > "
				"(u32)B2R2_MAX_S32 - (u32)img_size\n");
		return false;
	}

	return true;
}

static bool validate_rect(struct device *dev,
		struct b2r2_blt_rect *rect)
{
	if (rect->width < 0 || rect->height < 0) {
		b2r2_log_info(dev, "Validation Error: "
				"rect->width < 0 || rect->height < 0\n");
		return false;
	}

	return true;
}

bool b2r2_validate_user_req(struct device *dev,
		struct b2r2_blt_req *req)
{
	bool is_src_img_used;
	bool is_bg_img_used;
	bool is_src_mask_used;
	bool is_dst_clip_rect_used;

	if (req->size != sizeof(struct b2r2_blt_req)) {
		b2r2_log_err(dev, "Validation Error: "
				"req->size != sizeof(struct b2r2_blt_req)\n");
		return false;
	}

	is_src_img_used = !(req->flags & B2R2_BLT_FLAG_SOURCE_FILL ||
		req->flags & B2R2_BLT_FLAG_SOURCE_FILL_RAW);
	is_bg_img_used = (req->flags & B2R2_BLT_FLAG_BG_BLEND);
	is_src_mask_used = req->flags & B2R2_BLT_FLAG_SOURCE_MASK;
	is_dst_clip_rect_used = req->flags & B2R2_BLT_FLAG_DESTINATION_CLIP;

	if (is_src_img_used || is_src_mask_used) {
		if (!validate_rect(dev, &req->src_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!validate_rect(dev, &req->src_rect)\n");
			return false;
		}
	}

	if (!validate_rect(dev, &req->dst_rect)) {
		b2r2_log_info(dev, "Validation Error: "
			"!validate_rect(dev, &req->dst_rect)\n");
		return false;
	}

	if (is_bg_img_used)	{
		if (!validate_rect(dev, &req->bg_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!validate_rect(dev, &req->bg_rect)\n");
			return false;
		}
	}

	if (is_dst_clip_rect_used) {
		if (!validate_rect(dev, &req->dst_clip_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!validate_rect(dev, &req->dst_clip_rect)\n");
			return false;
		}
	}

	if (is_src_img_used) {
		struct b2r2_blt_rect src_img_bounding_rect;

		if (!validate_img(dev, &req->src_img)) {
			b2r2_log_info(dev, "Validation Error: "
					"!validate_img(dev, &req->src_img)\n");
			return false;
		}

		b2r2_get_img_bounding_rect(&req->src_img,
				&src_img_bounding_rect);
		if (!b2r2_is_rect_inside_rect(&req->src_rect,
				&src_img_bounding_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!b2r2_is_rect_inside_rect(&req->src_rect, "
				"&src_img_bounding_rect)\n");
			return false;
		}
	}

	if (is_bg_img_used) {
		struct b2r2_blt_rect bg_img_bounding_rect;

		if (!validate_img(dev, &req->bg_img)) {
			b2r2_log_info(dev, "Validation Error: "
				"!validate_img(dev, &req->bg_img)\n");
			return false;
		}

		if (!is_valid_bg_format(req->bg_img.fmt)) {
			b2r2_log_info(dev, "Validation Error: "
				"!is_valid_bg_format(req->bg_img->fmt)\n");
			return false;
		}

		b2r2_get_img_bounding_rect(&req->bg_img,
			&bg_img_bounding_rect);
		if (!b2r2_is_rect_inside_rect(&req->bg_rect,
				&bg_img_bounding_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!b2r2_is_rect_inside_rect(&req->bg_rect, "
				"&bg_img_bounding_rect)\n");
			return false;
		}
	}

	if (is_src_mask_used) {
		struct b2r2_blt_rect src_mask_bounding_rect;

		if (!validate_img(dev, &req->src_mask)) {
			b2r2_log_info(dev, "Validation Error: "
				"!validate_img(dev, &req->src_mask)\n");
			return false;
		}

		b2r2_get_img_bounding_rect(&req->src_mask,
			&src_mask_bounding_rect);
		if (!b2r2_is_rect_inside_rect(&req->src_rect,
					&src_mask_bounding_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!b2r2_is_rect_inside_rect(&req->src_rect, "
				"&src_mask_bounding_rect)\n");
			return false;
		}
	}

	if (!validate_img(dev, &req->dst_img)) {
		b2r2_log_info(dev, "Validation Error: "
			"!validate_img(dev, &req->dst_img)\n");
		return false;
	}

	if (is_bg_img_used)	{
		if (!b2r2_is_rect_gte_rect(&req->bg_rect, &req->dst_rect)) {
			b2r2_log_info(dev, "Validation Error: "
				"!b2r2_is_rect_gte_rect(&req->bg_rect, "
				"&req->dst_rect)\n");
			return false;
		}
	}

	return true;
}
