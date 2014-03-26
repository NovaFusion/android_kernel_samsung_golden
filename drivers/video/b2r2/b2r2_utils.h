/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 utils
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _LINUX_DRIVERS_VIDEO_B2R2_UTILS_H_
#define _LINUX_DRIVERS_VIDEO_B2R2_UTILS_H_

#include <video/b2r2_blt.h>

#include "b2r2_internal.h"

extern const s32 b2r2_s32_max;

int calculate_scale_factor(struct device *dev,
		u32 from, u32 to, u16 *sf_out);
void b2r2_get_img_bounding_rect(struct b2r2_blt_img *img,
		struct b2r2_blt_rect *bounding_rect);

bool b2r2_is_zero_area_rect(struct b2r2_blt_rect *rect);
bool b2r2_is_rect_inside_rect(struct b2r2_blt_rect *rect1,
		struct b2r2_blt_rect *rect2);
bool b2r2_is_rect_gte_rect(struct b2r2_blt_rect *rect1,
		struct b2r2_blt_rect *rect2);
void b2r2_intersect_rects(struct b2r2_blt_rect *rect1,
		struct b2r2_blt_rect *rect2,
		struct b2r2_blt_rect *intersection);
void b2r2_trim_rects(struct device *dev,
			const struct b2r2_blt_req *req,
			struct b2r2_blt_rect *new_bg_rect,
			struct b2r2_blt_rect *new_dst_rect,
			struct b2r2_blt_rect *new_src_rect);

int b2r2_get_fmt_bpp(struct device *dev, enum b2r2_blt_fmt fmt);
int b2r2_get_fmt_y_bpp(struct device *dev, enum b2r2_blt_fmt fmt);

bool b2r2_is_single_plane_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_independent_pixel_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_ycbcri_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_ycbcrsp_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_ycbcrp_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_ycbcr420_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_ycbcr422_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_ycbcr444_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_mb_fmt(enum b2r2_blt_fmt fmt);

/*
 * Rounds up if an invalid width causes the pitch to be non byte aligned.
 */
u32 b2r2_calc_pitch_from_width(struct device *dev,
		s32 width, enum b2r2_blt_fmt fmt);
u32 b2r2_get_img_pitch(struct device *dev,
		struct b2r2_blt_img *img);
s32 b2r2_get_img_size(struct device *dev,
		struct b2r2_blt_img *img);

s32 b2r2_div_round_up(s32 dividend, s32 divisor);
bool b2r2_is_aligned(s32 value, s32 alignment);
s32 b2r2_align_up(s32 value, s32 alignment);

enum b2r2_ty b2r2_get_alpha_range(enum b2r2_blt_fmt fmt);
u8 b2r2_get_alpha(enum b2r2_blt_fmt fmt, u32 pixel);
u32 b2r2_set_alpha(enum b2r2_blt_fmt fmt, u8 alpha, u32 color);
bool b2r2_fmt_has_alpha(enum b2r2_blt_fmt fmt);
bool b2r2_is_rgb_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_bgr_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yuv_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yvu_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yuv420_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yuv422_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yvu420_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yvu422_fmt(enum b2r2_blt_fmt fmt);
bool b2r2_is_yuv444_fmt(enum b2r2_blt_fmt fmt);
int b2r2_fmt_byte_pitch(enum b2r2_blt_fmt fmt, u32 width);
u32 b2r2_get_chroma_pitch(u32 luma_pitch, enum b2r2_blt_fmt fmt);
void b2r2_get_cb_cr_addr(u32 phy_base_addr, u32 luma_pitch, u32 height,
		enum b2r2_blt_fmt fmt, u32 *cb_addr, u32 *cr_addr);
enum b2r2_native_fmt b2r2_to_native_fmt(enum b2r2_blt_fmt fmt);
u32 b2r2_to_RGB888(u32 color, const enum b2r2_blt_fmt fmt);
enum b2r2_fmt_type b2r2_get_fmt_type(enum b2r2_blt_fmt fmt);
#ifdef CONFIG_DEBUG_FS
int sprintf_req(struct b2r2_blt_request *request, char *buf, int size);
#endif
void b2r2_recalculate_rects(struct device *dev,
		struct b2r2_blt_req *req);
const char *b2r2_fmt_to_string(enum b2r2_blt_fmt fmt);

#endif
