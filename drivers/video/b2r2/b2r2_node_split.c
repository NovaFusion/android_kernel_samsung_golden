/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 node splitter
 *
 * Author: Fredrik Allansson <fredrik.allansson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include "b2r2_debug.h"
#include "b2r2_node_split.h"
#include "b2r2_internal.h"
#include "b2r2_hw_convert.h"
#include "b2r2_filters.h"
#include "b2r2_utils.h"

#include <linux/kernel.h>

/*
 * Macros and constants
 */

#define INSTANCES_DEFAULT_SIZE 10
#define INSTANCES_GROW_SIZE 5

/* Upscaling needs overlapping of strips */
#define B2R2_UPSCALE_OVERLAP 8

/*
 * Internal types
 */


/*
 * Global variables
 */


/*
 * Forward declaration of private functions
 */
static int analyze_fmt_conv(struct b2r2_control *cont,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		const u32 **vmx, u32 *node_count,
		bool fullrange);
static int analyze_color_fill(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count);
static int analyze_copy(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count);
static int analyze_scaling(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count);
static int analyze_rotate(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count);
static int analyze_transform(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count);
static int analyze_rot_scale(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count);
static int analyze_scale_factors(struct b2r2_control *cont,
		struct b2r2_node_split_job *this);

static void configure_src(struct b2r2_control *cont, struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_job *this);
static void configure_bg(struct b2r2_control *cont, struct b2r2_node *node,
		struct b2r2_node_split_buf *bg, bool swap_fg_bg);
static int configure_dst(struct b2r2_control *cont, struct b2r2_node *node,
		struct b2r2_node_split_buf *dst, struct b2r2_node **next);
static void configure_blend(struct b2r2_control *cont, struct b2r2_node *node,
		u32 flags, u32 global_alpha);
static void configure_clip(struct b2r2_control *cont, struct b2r2_node *node,
		struct b2r2_blt_rect *clip_rect);

static int configure_tile(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this);
static void configure_direct_fill(struct b2r2_control *cont,
		struct b2r2_node *node, u32 color,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next);
static int configure_fill(struct b2r2_control *cont,
		struct b2r2_node *node, u32 color,
		enum b2r2_blt_fmt fmt,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this);
static void configure_direct_copy(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next);
static int configure_copy(struct b2r2_control *cont,
		struct b2r2_node *node,	struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this);
static int configure_rotate(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this);
static int configure_scale(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		u16 h_rsf, u16 v_rsf,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this);
static int configure_rot_scale(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this);

static int check_rect(struct b2r2_control *cont,
		const struct b2r2_blt_img *img,
		const struct b2r2_blt_rect *rect,
		const struct b2r2_blt_rect *clip);
static void set_buf(struct b2r2_control *cont,
		struct b2r2_node_split_buf *buf,
		u32 addr, const struct b2r2_blt_img *img,
		const struct b2r2_blt_rect *rect, bool color_fill, u32 color);
static int setup_tmp_buf(struct b2r2_control *cont,
		struct b2r2_node_split_buf *this, u32 max_size,
		enum b2r2_blt_fmt pref_fmt, u32 pref_width, u32 pref_height);

static bool is_transform(const struct b2r2_blt_request *req);
static s32 rescale(struct b2r2_control *cont, s32 dim, u16 sf);
static s32 inv_rescale(s32 dim, u16 sf);

static void set_target(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf);
static void set_src(struct b2r2_src_config *src, u32 addr,
		struct b2r2_node_split_buf *buf);
static void set_src_1(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf);
static void set_src_2(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf);
static void set_src_3(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf);
static void set_ivmx(struct b2r2_node *node, const u32 *vmx_values);
static void set_ovmx(struct b2r2_node *node, const u32 *vmx_values);

static void reset_nodes(struct b2r2_node *node);

static bool bg_format_require_ivmx(enum b2r2_blt_fmt bg_fmt,
		enum b2r2_blt_fmt dst_fmt);
static bool is_scaling(struct b2r2_node_split_job *this);

/*
 * Public functions
 */

/**
 * b2r2_node_split_analyze() - analyzes the request
 */
int b2r2_node_split_analyze(const struct b2r2_blt_request *req,
		u32 max_buf_size, u32 *node_count, struct b2r2_work_buf **bufs,
		u32 *buf_count, struct b2r2_node_split_job *this)
{
	int ret;
	bool color_fill;
	struct b2r2_control *cont = req->instance->control;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	memset(this, 0, sizeof(*this));

	/* Copy parameters */
	this->flags = req->user_req.flags;
	this->transform = req->user_req.transform;
	this->max_buf_size = max_buf_size;
	this->global_alpha = req->user_req.global_alpha;
	this->buf_count = 0;
	this->node_count = 0;

	if (this->flags & B2R2_BLT_FLAG_BLUR) {
		ret = -ENOSYS;
		goto unsupported;
	}

	/* Unsupported formats on src */
	switch (req->user_req.src_img.fmt) {
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		if (b2r2_is_bgr_fmt(req->user_req.dst_img.fmt)) {
			ret = -ENOSYS;
			goto unsupported;
		}
		break;
	default:
		break;
	}

	/* Unsupported formats on dst */
	switch (req->user_req.dst_img.fmt) {
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		if (b2r2_is_bgr_fmt(req->user_req.src_img.fmt)) {
			ret = -ENOSYS;
			goto unsupported;
		}
		break;
	default:
		break;
	}

	/* Unsupported formats on bg */
	if (this->flags & B2R2_BLT_FLAG_BG_BLEND)
		/* TODO: fix the oVMx for this */
		/*
		 * There are no ivmx on source 1, so check that there is no
		 * such requirement on the background to destination format
		 * conversion. This check is sufficient since the node splitter
		 * currently does not support ovmx. That fact also
		 * removes the source format as a parameter when checking the
		 * background format.
		 */
		if (bg_format_require_ivmx(req->user_req.bg_img.fmt,
				req->user_req.dst_img.fmt)) {
			ret = -ENOSYS;
			goto unsupported;
		}

	if ((this->flags & B2R2_BLT_FLAG_SOURCE_COLOR_KEY) &&
			(b2r2_is_yuv_fmt(req->user_req.src_img.fmt) ||
			req->user_req.src_img.fmt == B2R2_BLT_FMT_1_BIT_A1 ||
			req->user_req.src_img.fmt == B2R2_BLT_FMT_8_BIT_A8)) {
		b2r2_log_warn(cont->dev, "%s: Unsupported: source color keying "
			"with YUV or pure alpha formats.\n", __func__);
		ret = -ENOSYS;
		goto unsupported;
	}

	if (this->flags & (B2R2_BLT_FLAG_DEST_COLOR_KEY |
			B2R2_BLT_FLAG_SOURCE_MASK)) {
		b2r2_log_warn(cont->dev, "%s: Unsupported: source mask, "
			"destination color keying.\n", __func__);
		ret = -ENOSYS;
		goto unsupported;
	}

	if ((req->user_req.flags & B2R2_BLT_FLAG_CLUT_COLOR_CORRECTION) &&
			req->user_req.clut == NULL) {
		b2r2_log_warn(cont->dev, "%s: Invalid request: no table "
			"specified for CLUT color correction.\n",
			__func__);
		return -EINVAL;
	}

	/* Check for color fill */
	color_fill = (this->flags & (B2R2_BLT_FLAG_SOURCE_FILL |
		B2R2_BLT_FLAG_SOURCE_FILL_RAW)) != 0;

	/* Configure the source and destination buffers */
	set_buf(cont, &this->src, req->src_resolved.physical_address,
		&req->user_req.src_img, &req->user_req.src_rect,
		color_fill, req->user_req.src_color);

	if (this->flags & B2R2_BLT_FLAG_BG_BLEND) {
		set_buf(cont, &this->bg, req->bg_resolved.physical_address,
			&req->user_req.bg_img, &req->user_req.bg_rect,
			false, 0);
	}

	set_buf(cont, &this->dst, req->dst_resolved.physical_address,
			&req->user_req.dst_img, &req->user_req.dst_rect, false,
			0);

	b2r2_log_info(cont->dev, "%s:\n"
		"\t\tsrc.rect=(%4d, %4d, %4d, %4d)\t"
		"bg.rect=(%4d, %4d, %4d, %4d)\t"
		"dst.rect=(%4d, %4d, %4d, %4d)\n", __func__, this->src.rect.x,
		this->src.rect.y, this->src.rect.width, this->src.rect.height,
		this->bg.rect.x, this->bg.rect.y, this->bg.rect.width,
		this->bg.rect.height, this->dst.rect.x, this->dst.rect.y,
		this->dst.rect.width, this->dst.rect.height);

	if (this->flags & B2R2_BLT_FLAG_DITHER)
		this->dst.dither = B2R2_TTY_RGB_ROUND_DITHER;

	if (this->flags & B2R2_BLT_FLAG_SOURCE_COLOR_KEY)
		this->flag_param = req->user_req.src_color;

	/* Check for blending */
	if ((this->flags & B2R2_BLT_FLAG_GLOBAL_ALPHA_BLEND) &&
			(this->global_alpha != 255))
		this->blend = true;
	else if (this->flags & B2R2_BLT_FLAG_PER_PIXEL_ALPHA_BLEND)
		this->blend = (color_fill && b2r2_fmt_has_alpha(this->dst.fmt)) ||
				b2r2_fmt_has_alpha(this->src.fmt);
	else if (this->flags & B2R2_BLT_FLAG_BG_BLEND)
		this->blend = true;

	/* Check for full range YUV conversion */
	if (this->flags & B2R2_BLT_FLAG_FULL_RANGE_YUV)
		this->fullrange = true;

	if (this->blend && this->src.type == B2R2_FMT_TYPE_PLANAR) {
		b2r2_log_warn(cont->dev, "%s: Unsupported: blend with planar"
			" source\n", __func__);
		ret = -ENOSYS;
		goto unsupported;
	}

	/* Check for clipping */
	this->clip = (this->flags & B2R2_BLT_FLAG_DESTINATION_CLIP) != 0;
	if (this->clip) {
		s32 l = req->user_req.dst_clip_rect.x;
		s32 r = l + req->user_req.dst_clip_rect.width;
		s32 t = req->user_req.dst_clip_rect.y;
		s32 b = t + req->user_req.dst_clip_rect.height;

		/* Intersect the clip and buffer rects */
		if (l < 0)
			l = 0;
		if (r > req->user_req.dst_img.width)
			r = req->user_req.dst_img.width;
		if (t < 0)
			t = 0;
		if (b > req->user_req.dst_img.height)
			b = req->user_req.dst_img.height;

		this->clip_rect.x = l;
		this->clip_rect.y = t;
		this->clip_rect.width = r - l;
		this->clip_rect.height = b - t;
	} else {
		/* Set the clip rectangle to the buffer bounds */
		this->clip_rect.x = 0;
		this->clip_rect.y = 0;
		this->clip_rect.width = req->user_req.dst_img.width;
		this->clip_rect.height = req->user_req.dst_img.height;
	}

	/* Validate the destination */
	ret = check_rect(cont, &req->user_req.dst_img, &req->user_req.dst_rect,
			&this->clip_rect);
	if (ret < 0)
		goto error;

	/* Validate the source (if not color fill) */
	if (!color_fill) {
		ret = check_rect(cont, &req->user_req.src_img,
					&req->user_req.src_rect, NULL);
		if (ret < 0)
			goto error;
	}

	/* Validate the background source */
	if (this->flags & B2R2_BLT_FLAG_BG_BLEND) {
		ret = check_rect(cont, &req->user_req.bg_img,
					&req->user_req.bg_rect, NULL);
		if (ret < 0)
			goto error;
	}

	/*
	 * The transform enum is defined so that all rotation transforms are
	 * masked with the rotation flag
	 */
	this->rotation = (this->transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) != 0;

	this->scaling = is_scaling(this);

	/* Do the analysis depending on the type of operation */
	if (color_fill) {
		ret = analyze_color_fill(this, req, &this->node_count);
	} else {

		bool upsample;
		bool downsample;

		/*
		 * YUV formats that are non-raster, non-yuv444 needs to be
		 * up (or down) sampled using the resizer.
		 *
		 * NOTE: The resizer needs to be enabled for YUV444 as well,
		 *       even though there is no upsampling. This is most
		 *       likely a bug in the hardware.
		 */
		upsample = this->src.type != B2R2_FMT_TYPE_RASTER &&
			b2r2_is_yuv_fmt(this->src.fmt);
		downsample = this->dst.type != B2R2_FMT_TYPE_RASTER &&
			b2r2_is_yuv_fmt(this->dst.fmt);

		if (is_transform(req) || upsample || downsample)
			ret = analyze_transform(this, req, &this->node_count,
						&this->buf_count);
		else
			ret = analyze_copy(this, req, &this->node_count,
						&this->buf_count);
	}

	/*
	 * Blending on YUV422R does not work when background is set on input 1.
	 * Also, swapping the background with the foreground can not be
	 * supported if scaling is required since input 1 can not do scaling.
	 */
	if (this->blend) {
		if (this->flags & B2R2_BLT_FLAG_BG_BLEND) {
			if (req->user_req.bg_img.fmt ==
						B2R2_BLT_FMT_CB_Y_CR_Y &&
					req->user_req.src_img.fmt !=
						B2R2_BLT_FMT_CB_Y_CR_Y)
				this->swap_fg_bg = true;
		} else {
			if (req->user_req.dst_img.fmt ==
						B2R2_BLT_FMT_CB_Y_CR_Y &&
					req->user_req.src_img.fmt !=
						B2R2_BLT_FMT_CB_Y_CR_Y)
				this->swap_fg_bg = true;
		}

		if (this->scaling && this->swap_fg_bg) {
			b2r2_log_warn(cont->dev, "%s: No support for "
				"scaling foreground when blending on "
				"YUV422R!\n", __func__);
			ret = -ENOSYS;
		}
	}

	if (ret == -ENOSYS) {
		goto unsupported;
	} else if (ret < 0) {
		b2r2_log_warn(cont->dev, "%s: Analysis failed!\n", __func__);
		goto error;
	}

	/* Setup default values for step size as fallback */
	if (!this->dst.dx)
		this->dst.dx = this->dst.win.width;

	if (!this->dst.dy)
		this->dst.dy = this->dst.win.height;

	/* Setup the origin and movement of the destination window */
	if (this->dst.hso == B2R2_TY_HSO_RIGHT_TO_LEFT) {
		this->dst.dx = -this->dst.dx;
		this->dst.win.x = this->dst.rect.x + this->dst.rect.width - 1;
	} else {
		this->dst.win.x = this->dst.rect.x;
	}
	if (this->dst.vso == B2R2_TY_VSO_BOTTOM_TO_TOP) {
		this->dst.dy = -this->dst.dy;
		this->dst.win.y = this->dst.rect.y + this->dst.rect.height - 1;
	} else {
		this->dst.win.y = this->dst.rect.y;
	}

	*buf_count = this->buf_count;
	*node_count = this->node_count;

	if (this->buf_count > 0)
		*bufs = &this->work_bufs[0];

	b2r2_log_info(cont->dev, "%s: dst.win=(%d, %d, %d, %d), "
		"dst.dx=%d, dst.dy=%d\n", __func__, this->dst.win.x,
		this->dst.win.y, this->dst.win.width, this->dst.win.height,
		this->dst.dx, this->dst.dy);
	if (this->buf_count > 0)
		b2r2_log_info(cont->dev, "%s: buf_count=%d, buf_size=%d, "
			"node_count=%d\n", __func__, *buf_count,
			bufs[0]->size, *node_count);
	else
		b2r2_log_info(cont->dev, "%s: buf_count=%d, node_count=%d\n",
			__func__, *buf_count, *node_count);

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
unsupported:
	return ret;
}

/**
 * b2r2_node_split_configure() - configures the node list
 */
int b2r2_node_split_configure(struct b2r2_control *cont,
		struct b2r2_node_split_job *this, struct b2r2_node *first)
{
	int ret;

	struct b2r2_node_split_buf *dst = &this->dst;
	struct b2r2_node *node = first;

	u32 x_pixels = 0;
	u32 y_pixels = 0;

	reset_nodes(node);

	while (y_pixels < dst->rect.height) {
		s32 dst_x = dst->win.x;
		s32 dst_w = dst->win.width;

		/* Clamp window height */
		if (dst->win.height > dst->rect.height - y_pixels)
			dst->win.height = dst->rect.height - y_pixels;

		while (x_pixels < dst->rect.width) {

			/* Clamp window width */
			if (dst_w > dst->rect.width - x_pixels)
				dst->win.width = dst->rect.width - x_pixels;

			ret = configure_tile(cont, node, &node, this);
			if (ret < 0)
				goto error;

			/* did the last tile cover the remaining pixels? */
			if (x_pixels + dst->win.width == dst->rect.width) {
				b2r2_log_info(cont->dev,
					"%s: dst.rect.width covered.\n",
					__func__);
				break;
			}

			dst->win.x += dst->dx;
			x_pixels += max(dst->dx, -dst->dx);
			b2r2_log_info(cont->dev, "%s: x_pixels=%d\n",
				__func__, x_pixels);
		}

		/* did the last tile cover the remaining pixels? */
		if (y_pixels + dst->win.height == dst->rect.height) {
			b2r2_log_info(cont->dev,
					"%s: dst.rect.height covered.\n",
					__func__);
			break;
		}

		dst->win.y += dst->dy;
		y_pixels += max(dst->dy, -dst->dy);

		dst->win.x = dst_x;
		dst->win.width = dst_w;
		x_pixels = 0;

		b2r2_log_info(cont->dev, "%s: y_pixels=%d\n",
			__func__, y_pixels);
	}

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: error!\n", __func__);
	return ret;
}

/**
 * b2r2_node_split_assign_buffers() - assigns temporary buffers to the node list
 */
int b2r2_node_split_assign_buffers(struct b2r2_control *cont,
		struct b2r2_node_split_job *this, struct b2r2_node *first,
		struct b2r2_work_buf *bufs, u32 buf_count)
{
	struct b2r2_node *node = first;

	while (node != NULL) {
		/* The indices are offset by one */
		if (node->dst_tmp_index) {
			BUG_ON(node->dst_tmp_index > buf_count);

			b2r2_log_info(cont->dev, "%s: assigning buf %d as "
				"dst\n", __func__, node->dst_tmp_index);

			node->node.GROUP1.B2R2_TBA =
				bufs[node->dst_tmp_index - 1].phys_addr;
		}
		if (node->src_tmp_index) {
			u32 addr = bufs[node->src_tmp_index - 1].phys_addr;

			b2r2_log_info(cont->dev, "%s: assigning buf %d as src "
				"%d ", __func__, node->src_tmp_index,
				node->src_index);

			BUG_ON(node->src_tmp_index > buf_count);

			switch (node->src_index) {
			case 1:
				b2r2_log_info(cont->dev, "1\n");
				node->node.GROUP3.B2R2_SBA = addr;
				break;
			case 2:
				b2r2_log_info(cont->dev, "2\n");
				node->node.GROUP4.B2R2_SBA = addr;
				break;
			case 3:
				b2r2_log_info(cont->dev, "3\n");
				node->node.GROUP5.B2R2_SBA = addr;
				break;
			default:
				BUG_ON(1);
				break;
			}
		}

		b2r2_log_info(cont->dev, "%s: tba=%p\tsba=%p\n", __func__,
			(void *)node->node.GROUP1.B2R2_TBA,
			(void *)node->node.GROUP4.B2R2_SBA);

		node = node->next;
	}

	return 0;
}

/**
 * b2r2_node_split_unassign_buffers() - releases temporary buffers
 */
void b2r2_node_split_unassign_buffers(struct b2r2_control *cont,
		struct b2r2_node_split_job *this, struct b2r2_node *first)
{
	return;
}

/**
 * b2r2_node_split_cancel() - cancels and releases a job instance
 */
void b2r2_node_split_cancel(struct b2r2_control *cont,
		struct b2r2_node_split_job *this)
{
	memset(this, 0, sizeof(*this));

	return;
}

static bool is_scaling(struct b2r2_node_split_job *this)
{
	bool scaling;

	if (this->rotation)
		scaling = (this->src.rect.width != this->dst.rect.height) ||
			(this->src.rect.height != this->dst.rect.width);
	else
		scaling = (this->src.rect.width != this->dst.rect.width) ||
			(this->src.rect.height != this->dst.rect.height);

	/* Plane separated formats must be treated as scaling */
	scaling = scaling ||
			(this->src.type == B2R2_FMT_TYPE_SEMI_PLANAR) ||
			(this->src.type == B2R2_FMT_TYPE_PLANAR) ||
			(this->dst.type == B2R2_FMT_TYPE_SEMI_PLANAR) ||
			(this->dst.type == B2R2_FMT_TYPE_PLANAR);

	return scaling;
}

static int check_rect(struct b2r2_control *cont,
		const struct b2r2_blt_img *img,
		const struct b2r2_blt_rect *rect,
		const struct b2r2_blt_rect *clip)
{
	int ret;

	s32 l, r, b, t;

	/* Check rectangle dimensions*/
	if ((rect->width <= 0) || (rect->height <= 0)) {
		b2r2_log_warn(cont->dev, "%s: Illegal rect (%d, %d, %d, %d)\n",
				__func__, rect->x, rect->y, rect->width,
				rect->height);
		ret = -EINVAL;
		goto error;
	}

	/*
	 * If we are using clip we should only look at the intersection of the
	 * rects.
	 */
	if (clip) {
		l = max(rect->x, clip->x);
		t = max(rect->y, clip->y);
		r = min(rect->x + rect->width, clip->x + clip->width);
		b = min(rect->y + rect->height, clip->y + clip->height);
	} else {
		l = rect->x;
		t = rect->y;
		r = rect->x + rect->width;
		b = rect->y + rect->height;
	}

	/* Check so that the rect isn't outside the buffer */
	if ((l < 0) || (t < 0) || (l >= img->width) || (t >= img->height)) {
		b2r2_log_warn(cont->dev, "%s: rect origin outside buffer\n",
			__func__);
		ret = -EINVAL;
		goto error;
	}

	if ((r > img->width) || (b > img->height)) {
		b2r2_log_warn(cont->dev, "%s: rect ends outside buffer\n",
			__func__);
		ret = -EINVAL;
		goto error;
	}

	/* Check so the intersected rectangle isn't empty */
	if ((l == r) || (t == b)) {
		b2r2_log_warn(cont->dev,
			"%s: rect is empty (width or height zero)\n",
			__func__);
		ret = -EINVAL;
		goto error;
	}

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

/**
 * bg_format_require_ivmx()
 *
 * Check if there are any color space conversion needed for the
 * background to the destination format.
 */
static bool bg_format_require_ivmx(enum b2r2_blt_fmt bg_fmt,
				enum b2r2_blt_fmt dst_fmt)
{
	if (b2r2_is_rgb_fmt(bg_fmt)) {
		if (b2r2_is_yvu_fmt(dst_fmt))
			return true;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			return true;
		else if (b2r2_is_yuv_fmt(dst_fmt))
			return true;
		else if (b2r2_is_bgr_fmt(dst_fmt))
			return true;
	} else if (b2r2_is_yvu_fmt(bg_fmt)) {
		if (b2r2_is_rgb_fmt(dst_fmt))
			return true;
		else if (b2r2_is_bgr_fmt(dst_fmt))
			return true;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			return true;
		else if (b2r2_is_yuv_fmt(dst_fmt) &&
				!b2r2_is_yvu_fmt(dst_fmt))
			return true;
	} else if (bg_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
			bg_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
			bg_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
			bg_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888) {
		if (b2r2_is_rgb_fmt(dst_fmt)) {
			return true;
		} else if (b2r2_is_yvu_fmt(dst_fmt)) {
			return true;
		} else if (b2r2_is_yuv_fmt(dst_fmt)) {
			switch (dst_fmt) {
			case B2R2_BLT_FMT_24_BIT_YUV888:
			case B2R2_BLT_FMT_32_BIT_AYUV8888:
			case B2R2_BLT_FMT_24_BIT_VUY888:
			case B2R2_BLT_FMT_32_BIT_VUYA8888:
				break;
			default:
				return true;
			}
		}
	} else if (b2r2_is_yuv_fmt(bg_fmt)) {
		if (b2r2_is_rgb_fmt(dst_fmt))
			return true;
		else if (b2r2_is_bgr_fmt(dst_fmt))
			return true;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			return true;
		else if (b2r2_is_yvu_fmt(dst_fmt))
			return true;
	} else if (b2r2_is_bgr_fmt(bg_fmt)) {
		if (b2r2_is_rgb_fmt(dst_fmt))
			return true;
		else if (b2r2_is_yvu_fmt(dst_fmt))
			return true;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			return true;
		else if (b2r2_is_yuv_fmt(dst_fmt))
			return true;
	}

	return false;
}

/**
 * analyze_fmt_conv() - analyze the format conversions needed for a job
 */
static int analyze_fmt_conv(struct b2r2_control *cont,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		const u32 **vmx, u32 *node_count,
		bool fullrange)
{
	enum b2r2_color_conversion cc =
		b2r2_get_color_conversion(src->fmt, dst->fmt, fullrange);

	b2r2_log_info(cont->dev,
			"%s: Color conversion: %d\n",
			__func__, cc);

	b2r2_get_vmx(cc, vmx);

	if (node_count) {
		if (dst->type == B2R2_FMT_TYPE_RASTER)
			*node_count = 1;
		else if (dst->type == B2R2_FMT_TYPE_SEMI_PLANAR)
			*node_count = 2;
		else if (dst->type == B2R2_FMT_TYPE_PLANAR)
			*node_count = 3;
		else
			/* That's strange... */
			BUG_ON(1);
	}

	return 0;
}

/**
 * analyze_color_fill() - analyze a color fill operation
 */
static int analyze_color_fill(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count)
{
	int ret;
	struct b2r2_control *cont = req->instance->control;

	/* Destination must be raster for raw fill to work */
	if (this->dst.type != B2R2_FMT_TYPE_RASTER) {
		b2r2_log_warn(cont->dev,
			"%s: fill requires raster destination\n",
			__func__);
		ret = -EINVAL;
		goto error;
	}

	/* We will try to fill the entire rectangle in one go */
	memcpy(&this->dst.win, &this->dst.rect, sizeof(this->dst.win));

	/* Check if this is a direct fill */
	if ((!this->blend) && ((this->flags & B2R2_BLT_FLAG_SOURCE_FILL_RAW) ||
			(this->dst.fmt == B2R2_BLT_FMT_32_BIT_ARGB8888) ||
			(this->dst.fmt == B2R2_BLT_FMT_32_BIT_ABGR8888) ||
			(this->dst.fmt == B2R2_BLT_FMT_32_BIT_AYUV8888) ||
			(this->dst.fmt == B2R2_BLT_FMT_32_BIT_VUYA8888))) {
		this->type = B2R2_DIRECT_FILL;

		/* The color format will be the same as the dst fmt */
		this->src.fmt = this->dst.fmt;

		/* The entire destination rectangle will be  */
		memcpy(&this->dst.win, &this->dst.rect,
			sizeof(this->dst.win));
		*node_count = 1;
	} else {
		this->type = B2R2_FILL;

		/* Determine the fill color format */
		if (this->flags & B2R2_BLT_FLAG_SOURCE_FILL_RAW) {
			/* The color format will be the same as the dst fmt */
			this->src.fmt = this->dst.fmt;
		} else {
			/* If the dst fmt is YUV the fill fmt will be as well */
			if (b2r2_is_yuv_fmt(this->dst.fmt)) {
				this->src.fmt =	B2R2_BLT_FMT_32_BIT_AYUV8888;
			} else if (b2r2_is_rgb_fmt(this->dst.fmt)) {
				this->src.fmt =	B2R2_BLT_FMT_32_BIT_ARGB8888;
			} else if (b2r2_is_bgr_fmt(this->dst.fmt)) {
				/* Color will still be ARGB, we will translate
				   using IVMX (configured later) */
				this->src.fmt =	B2R2_BLT_FMT_32_BIT_ARGB8888;
			} else {
				/* Wait, what? */
				b2r2_log_warn(cont->dev, "%s: "
					"Illegal destination format for fill",
					__func__);
				ret = -EINVAL;
				goto error;
			}
		}

		/* Also, B2R2 seems to ignore the pixel alpha value */
		if (((this->flags & B2R2_BLT_FLAG_PER_PIXEL_ALPHA_BLEND)
					!= 0) &&
				((this->flags & B2R2_BLT_FLAG_SOURCE_FILL_RAW)
					== 0) && b2r2_fmt_has_alpha(this->src.fmt)) {
			u8 pixel_alpha = b2r2_get_alpha(this->src.fmt,
							this->src.color);
			u32 new_global = pixel_alpha * this->global_alpha / 255;

			this->global_alpha = (u8)new_global;

			/*
			 * Set the pixel alpha to full opaque so we don't get
			 * any nasty surprises.
			 */
			this->src.color = b2r2_set_alpha(this->src.fmt, 0xFF,
						this->src.color);
		}

		ret = analyze_fmt_conv(
			cont, &this->src, &this->dst, &this->ivmx,
			node_count, this->fullrange);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;

}

/**
 * analyze_transform() - analyze a transform operation (rescale, rotate, etc.)
 */
static int analyze_transform(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	int ret;
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_control *cont = req->instance->control;
#endif

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/* B2R2 cannot do rotations if the destination is not raster, or 422R */
	if (this->rotation && (this->dst.type != B2R2_FMT_TYPE_RASTER ||
				this->dst.fmt == B2R2_BLT_FMT_CB_Y_CR_Y)) {
		b2r2_log_warn(cont->dev,
			"%s: Unsupported operation "
			"(rot && (!dst_raster || dst==422R))",
			__func__);
		ret = -ENOSYS;
		goto unsupported;
	}

	/* Flip the image by changing the scan order of the destination */
	if (this->transform & B2R2_BLT_TRANSFORM_FLIP_H)
		this->dst.hso = B2R2_TY_HSO_RIGHT_TO_LEFT;
	if (this->transform & B2R2_BLT_TRANSFORM_FLIP_V)
		this->dst.vso = B2R2_TY_VSO_BOTTOM_TO_TOP;

	if (this->scaling && this->rotation && this->blend) {
		/* TODO: This is unsupported. Fix it! */
		b2r2_log_info(cont->dev, "%s: Unsupported operation "
			"(rot+rescale+blend)\n", __func__);
		ret = -ENOSYS;
		goto unsupported;
	}

	/* Check which type of transform */
	if (this->scaling && this->rotation) {
		ret = analyze_rot_scale(this, req, node_count, buf_count);
		if (ret < 0)
			goto error;
	} else if (this->scaling) {
		ret = analyze_scaling(this, req, node_count, buf_count);
		if (ret < 0)
			goto error;
	} else if (this->rotation) {
		ret = analyze_rotate(this, req, node_count, buf_count);
		if (ret < 0)
			goto error;
	} else {
		/* No additional nodes needed for a flip */
		ret = analyze_copy(this, req, node_count, buf_count);
		if (ret < 0)
			goto error;
		this->type = B2R2_FLIP;
	}

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: error!\n", __func__);
unsupported:
	return ret;
}

/**
 * analyze_copy() - analyze a copy operation
 */
static int analyze_copy(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	int ret;
	struct b2r2_control *cont = req->instance->control;

	memcpy(&this->dst.win, &this->dst.rect, sizeof(this->dst.win));

	if (!this->blend &&
			!(this->flags & B2R2_BLT_FLAG_CLUT_COLOR_CORRECTION) &&
			(this->src.fmt == this->dst.fmt) &&
			(this->src.type == B2R2_FMT_TYPE_RASTER) &&
			(this->dst.rect.x >= this->clip_rect.x) &&
			(this->dst.rect.y >= this->clip_rect.y) &&
			(this->dst.rect.x + this->dst.rect.width <=
				this->clip_rect.x + this->clip_rect.width) &&
			(this->dst.rect.y + this->dst.rect.height <=
				this->clip_rect.y + this->clip_rect.height)) {
		this->type = B2R2_DIRECT_COPY;
		*node_count = 1;
	} else {
		u32 copy_count;

		this->type = B2R2_COPY;

		ret = analyze_fmt_conv(cont, &this->src, &this->dst,
			&this->ivmx, &copy_count, this->fullrange);
		if (ret < 0)
			goto error;

		*node_count = copy_count;
	}

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

static int calc_rot_count(u32 width, u32 height)
{
	int count;

	count = width / B2R2_ROTATE_MAX_WIDTH;
	if (width % B2R2_ROTATE_MAX_WIDTH)
		count++;
	if (height > B2R2_ROTATE_MAX_WIDTH &&
			height % B2R2_ROTATE_MAX_WIDTH)
		count *= 2;

	return count;
}

static int calc_ovrlp(struct b2r2_control *cont,
			u32 win_size, u32 rect_size, u16 scale, int *src_ovlp)
{
	int ovlp = 0;

	if (scale >= (1 << 10))
		goto exit; /* downscale, no overlap */

	if (rect_size <= win_size)
		goto exit; /* The window is big enough for the whole rect */

	ovlp = rescale(cont, B2R2_UPSCALE_OVERLAP, scale);
	ovlp >>= 10;

	/* The windows size needs to at least twice the overlap */
	if (win_size < (ovlp * 2))
		ovlp = 0;

exit:
	if (src_ovlp)
		*src_ovlp = ovlp ? B2R2_UPSCALE_OVERLAP : 0;

	return ovlp;
}


/**
 * Calculate the number of strips that are needed to cover a certain rect
 * taking into account eventual strip overlap (i.e. step_size < win_size)
 * Optionally the partial size of the last strip is returned in 'remainder'
 * In that case that partial strip is not included in total count.
 */
static int calc_strip_count(s32 win_size, u32 step_size,
					s32 rect_size, u32 *remainder)
{
	u32 count, width_remain;

	for (count = 1, width_remain = (u32)rect_size;
				width_remain > win_size; count++)
		width_remain -= step_size;

	if (remainder) {
		if (width_remain == win_size) {
			*remainder = 0;
		} else {
			*remainder = width_remain;
			--count;
		}
	}

	return count;
}

static int analyze_rot_scale_downscale(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	int ret;
	struct b2r2_control *cont = req->instance->control;
	struct b2r2_node_split_buf *src = &this->src;
	struct b2r2_node_split_buf *dst = &this->dst;
	struct b2r2_node_split_buf *tmp = &this->tmp_bufs[0];

	u32 num_rows;
	u32 num_cols;
	u32 rot_count;
	u32 rescale_count;
	u32 nodes_per_rot;
	u32 nodes_per_rescale;
	u32 right_width;
	u32 bottom_height;
	const u32 *dummy_vmx;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	/* Calculate the desired tmp buffer size */
	tmp->win.width = rescale(cont, B2R2_RESCALE_MAX_WIDTH - 1, this->h_rsf);
	tmp->win.width >>= 10;
	tmp->win.width = min(tmp->win.width, dst->rect.height);
	tmp->win.height = dst->rect.width;

	setup_tmp_buf(cont, tmp, this->max_buf_size, dst->fmt, tmp->win.width,
			tmp->win.height);
	tmp->tmp_buf_index = 1;
	this->work_bufs[0].size = tmp->pitch * tmp->height;

	tmp->win.width = tmp->rect.width;
	tmp->win.height = tmp->rect.height;

	tmp->dither = dst->dither;
	dst->dither = 0;

	/* Update the dst window with the actual tmp buffer dimensions */
	dst->win.width = tmp->win.height;
	dst->win.height = tmp->win.width;

	/* The rotated stripes are written to the destination bottom-up */
	if (this->dst.vso == B2R2_TY_VSO_TOP_TO_BOTTOM)
		this->dst.vso = B2R2_TY_VSO_BOTTOM_TO_TOP;
	else
		this->dst.vso = B2R2_TY_VSO_TOP_TO_BOTTOM;

	/*
	 * Calculate how many nodes are required to copy to and from the tmp
	 * buffer
	 */
	ret = analyze_fmt_conv(cont, src, tmp, &this->ivmx, &nodes_per_rescale,
			this->fullrange);
	if (ret < 0)
		goto error;

	/* We will not do any format conversion in the rotation stage */
	ret = analyze_fmt_conv(cont, tmp, dst, &dummy_vmx, &nodes_per_rot,
			this->fullrange);
	if (ret < 0)
		goto error;

	/* Calculate node count for the inner tiles */
	num_cols = dst->rect.width / dst->win.width;
	num_rows = dst->rect.height / dst->win.height;

	rescale_count = num_cols * num_rows;
	rot_count = calc_rot_count(dst->win.height, dst->win.width) *
		num_cols * num_rows;

	right_width = dst->rect.width % dst->win.width;
	bottom_height = dst->rect.height % dst->win.height;

	/* Calculate node count for the rightmost tiles */
	if (right_width) {
		u32 count = calc_rot_count(dst->win.height, right_width);

		rot_count += count * num_rows;
		rescale_count += num_rows;
		b2r2_log_info(cont->dev, "%s: rightmost: %d nodes\n", __func__,
			count*num_rows);
	}

	/* Calculate node count for the bottom tiles */
	if (bottom_height) {
		u32 count = calc_rot_count(bottom_height, dst->win.width);

		rot_count += count * num_cols;
		rescale_count += num_cols;
		b2r2_log_info(cont->dev, "%s: bottom: %d nodes\n", __func__,
			count * num_cols);

	}

	/* And finally for the bottom right corner */
	if (right_width && bottom_height) {
		u32 count = calc_rot_count(bottom_height, right_width);

		rot_count += count;
		rescale_count++;
		b2r2_log_info(cont->dev, "%s: bottom right: %d nodes\n",
			__func__, count);

	}

	*node_count = rot_count * nodes_per_rot;
	*node_count += rescale_count * nodes_per_rescale;
	*buf_count = 1;

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: error!\n", __func__);
	return ret;
}

static int analyze_rot_scale_upscale(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	/* TODO: When upscaling we should optimally to the rotation first... */
	return analyze_rot_scale_downscale(this, req, node_count, buf_count);
}

/**
 * analyze_rot_scaling() - analyzes a combined rotation and scaling op
 */
static int analyze_rot_scale(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	int ret;
	bool upscale;
	struct b2r2_control *cont = req->instance->control;

	ret = analyze_scale_factors(cont, this);
	if (ret < 0)
		goto error;

	upscale = (u32)this->h_rsf * (u32)this->v_rsf < (1 << 20);

	if (upscale)
		ret = analyze_rot_scale_upscale(this, req, node_count,
			buf_count);
	else
		ret = analyze_rot_scale_downscale(this, req, node_count,
			buf_count);

	if (ret < 0)
		goto error;

	this->type = B2R2_SCALE_AND_ROTATE;

	return 0;

error:
	return ret;
}

/**
 * analyze_scaling() - analyze a rescale operation
 */
static int analyze_scaling(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	int ret;
	u32 copy_count;
	u32 nbr_cols;
	s32 dst_w, ovlp;
	struct b2r2_control *cont = req->instance->control;

	b2r2_log_info(cont->dev, "%s\n", __func__);

	ret = analyze_scale_factors(cont, this);
	if (ret < 0)
		goto error;

	/* Find out how many nodes a simple copy would require */
	ret = analyze_fmt_conv(cont, &this->src, &this->dst, &this->ivmx,
			&copy_count, this->fullrange);
	if (ret < 0)
		goto error;

	memcpy(&this->dst.win, &this->dst.rect, sizeof(this->dst.win));

	/*
	 * We need to subtract from the actual maximum rescale width since the
	 * start of the stripe will be floored and the end ceiled. This could in
	 * some cases cause the stripe to be one pixel more than the maximum
	 * width.
	 *
	 * Example:
	 *   x = 127.8, w = 127.8
	 *
	 *   The stripe will touch pixels 127.8 through 255.6, i.e. 129 pixels.
	 */
	dst_w = rescale(cont, B2R2_RESCALE_MAX_WIDTH - 1, this->h_rsf);
	if (dst_w < (1 << 10))
		dst_w = 1;
	else
		dst_w >>= 10;

	b2r2_log_info(cont->dev, "%s: dst_w=%d dst.rect.width=%d\n",
		__func__, dst_w, this->dst.rect.width);

	this->dst.win.width = min(dst_w, this->dst.rect.width);

	/* Calculate the eventual stripe overlap */
	ovlp = calc_ovrlp(cont, this->dst.win.width,
				this->dst.rect.width, this->h_rsf, NULL);

	this->dst.dx = this->dst.win.width - ovlp;

	b2r2_log_info(cont->dev, "%s: dst.win.width=%d\n",
		__func__, this->dst.win.width);

	b2r2_log_info(cont->dev, "%s: stripe_ovlp=%d\n",
					__func__, ovlp);

	nbr_cols = calc_strip_count(this->dst.win.width, this->dst.dx,
						this->dst.rect.width, NULL);

	*node_count = copy_count * nbr_cols;

	this->type = B2R2_SCALE;

	b2r2_log_info(cont->dev, "%s exit\n", __func__);

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;

}

/**
 * analyze_rotate() - analyze a rotate operation
 */
static int analyze_rotate(struct b2r2_node_split_job *this,
		const struct b2r2_blt_request *req, u32 *node_count,
		u32 *buf_count)
{
	int ret;
	u32 nodes_per_tile;
	struct b2r2_control *cont = req->instance->control;

	/* Find out how many nodes a simple copy would require */
	ret = analyze_fmt_conv(cont, &this->src, &this->dst, &this->ivmx,
			&nodes_per_tile, this->fullrange);
	if (ret < 0)
		goto error;

	this->type = B2R2_ROTATE;

	/* The rotated stripes are written to the destination bottom-up */
	if (this->dst.vso == B2R2_TY_VSO_TOP_TO_BOTTOM)
		this->dst.vso = B2R2_TY_VSO_BOTTOM_TO_TOP;
	else
		this->dst.vso = B2R2_TY_VSO_TOP_TO_BOTTOM;

	memcpy(&this->dst.win, &this->dst.rect, sizeof(this->dst.win));

	this->dst.win.height = min(this->dst.win.height, B2R2_ROTATE_MAX_WIDTH);

	/*
	 * B2R2 cannot do rotations on stripes that are not a multiple of 16
	 * pixels high (if larger than 16 pixels).
	 */
	if (this->dst.win.width > 16)
		this->dst.win.width -= (this->dst.win.width % 16);

	/* Blending cannot be combined with rotation */
	if (this->blend) {
		struct b2r2_node_split_buf *tmp = &this->tmp_bufs[0];
		enum b2r2_blt_fmt tmp_fmt;

		if (b2r2_is_yuv_fmt(this->dst.fmt))
			tmp_fmt = B2R2_BLT_FMT_32_BIT_AYUV8888;
		else if (b2r2_is_bgr_fmt(this->dst.fmt))
			tmp_fmt = B2R2_BLT_FMT_32_BIT_ABGR8888;
		else
			tmp_fmt = B2R2_BLT_FMT_32_BIT_ARGB8888;

		setup_tmp_buf(cont, tmp, this->max_buf_size, tmp_fmt,
				this->dst.win.width, this->dst.win.height);

		tmp->tmp_buf_index = 1;

		tmp->vso = B2R2_TY_VSO_BOTTOM_TO_TOP;

		this->dst.win.width = tmp->rect.width;
		this->dst.win.height = tmp->rect.height;

		memcpy(&tmp->win, &tmp->rect, sizeof(tmp->win));

		*buf_count = 1;
		this->work_bufs[0].size = tmp->pitch * tmp->height;

		/*
		 * One more node per tile is required to rotate to the temp
		 * buffer.
		 */
		nodes_per_tile++;
	}

	/* Finally, calculate the node count */
	*node_count = nodes_per_tile *
		calc_rot_count(this->src.rect.width, this->src.rect.height);

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

/**
 * analyze_scale_factors() - determines the scale factors for the op
 */
static int analyze_scale_factors(struct b2r2_control *cont,
		struct b2r2_node_split_job *this)
{
	int ret;

	u16 hsf;
	u16 vsf;

	if (this->rotation) {
		ret = calculate_scale_factor(cont->dev, this->src.rect.width,
					this->dst.rect.height, &hsf);
		if (ret < 0)
			goto error;

		ret = calculate_scale_factor(cont->dev, this->src.rect.height,
					this->dst.rect.width, &vsf);
		if (ret < 0)
			goto error;
	} else {
		ret = calculate_scale_factor(cont->dev, this->src.rect.width,
					this->dst.rect.width, &hsf);
		if (ret < 0)
			goto error;

		ret = calculate_scale_factor(cont->dev, this->src.rect.height,
					this->dst.rect.height, &vsf);
		if (ret < 0)
			goto error;
	}

	this->h_rescale = hsf != (1 << 10);
	this->v_rescale = vsf != (1 << 10);

	this->h_rsf = hsf;
	this->v_rsf = vsf;

	b2r2_log_info(cont->dev, "%s: h_rsf=%.4x\n", __func__, this->h_rsf);
	b2r2_log_info(cont->dev, "%s: v_rsf=%.4x\n", __func__, this->v_rsf);

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

/**
 * configure_tile() - configures one tile of a blit operation
 */
static int configure_tile(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret = 0;

	struct b2r2_node *last;
	struct b2r2_node_split_buf *src = &this->src;
	struct b2r2_node_split_buf *dst = &this->dst;
	struct b2r2_node_split_buf *bg = &this->bg;

	struct b2r2_blt_rect dst_norm;
	struct b2r2_blt_rect src_norm;
	struct b2r2_blt_rect bg_norm;

	/* Normalize the dest coords to the dest rect coordinate space  */
	dst_norm.x = dst->win.x - dst->rect.x;
	dst_norm.y = dst->win.y - dst->rect.y;
	dst_norm.width = dst->win.width;
	dst_norm.height = dst->win.height;

	if (dst->vso == B2R2_TY_VSO_BOTTOM_TO_TOP) {
		/* The y coord should be counted from the bottom */
		dst_norm.y = dst->rect.height - (dst_norm.y + 1);
	}
	if (dst->hso == B2R2_TY_HSO_RIGHT_TO_LEFT) {
		/* The x coord should be counted from the right */
		dst_norm.x = dst->rect.width - (dst_norm.x + 1);
	}

	/* If the destination is rotated we should swap x, y */
	if (this->rotation) {
		src_norm.x = dst_norm.y;
		src_norm.y = dst_norm.x;
		src_norm.width = dst_norm.height;
		src_norm.height = dst_norm.width;
	} else {
		src_norm.x = dst_norm.x;
		src_norm.y = dst_norm.y;
		src_norm.width = dst_norm.width;
		src_norm.height = dst_norm.height;
	}

	/* Convert to src coordinate space */
	src->win.x = src_norm.x + src->rect.x;
	src->win.y = src_norm.y + src->rect.y;
	src->win.width = src_norm.width;
	src->win.height = src_norm.height;

	/* Set bg norm */
	bg_norm.x = dst->win.x - dst->rect.x;
	bg_norm.y = dst->win.y - dst->rect.y;
	bg_norm.width = dst->win.width;
	bg_norm.height = dst->win.height;

	/* Convert to bg coordinate space */
	bg->win.x = bg_norm.x + bg->rect.x;
	bg->win.y = bg_norm.y + bg->rect.y;
	bg->win.width = bg_norm.width;
	bg->win.height = bg_norm.height;
	bg->vso = dst->vso;
	bg->hso = dst->hso;

	/* Do the configuration depending on operation type */
	switch (this->type) {
	case B2R2_DIRECT_FILL:
		configure_direct_fill(cont, node, this->src.color, dst, &last);
		break;

	case B2R2_DIRECT_COPY:
		configure_direct_copy(cont, node, src, dst, &last);
		break;

	case B2R2_FILL:
		ret = configure_fill(cont, node, src->color, src->fmt,
				dst, &last, this);
		break;

	case B2R2_FLIP: /* FLIP is just a copy with different VSO/HSO */
	case B2R2_COPY:
		ret = configure_copy(
			cont, node, src, dst, &last, this);
		break;

	case B2R2_ROTATE:
		{
			struct b2r2_node_split_buf *tmp = &this->tmp_bufs[0];

			if (this->blend) {
				b2r2_log_info(cont->dev, "%s: rotation + "
						"blend\n", __func__);

				tmp->win.x = 0;
				tmp->win.y = tmp->win.height - 1;
				tmp->win.width = dst->win.width;
				tmp->win.height = dst->win.height;

				/* Rotate to the temp buf */
				ret = configure_rotate(cont, node, src, tmp,
						&node, NULL);
				if (ret < 0)
					goto error;

				/* Then do a copy to the destination */
				ret = configure_copy(cont, node, tmp, dst,
						&last, this);
			} else {
				/* Just do a rotation */
				ret = configure_rotate(cont, node, src, dst,
						&last, this);
			}
		}
		break;

	case B2R2_SCALE:
		ret = configure_scale(cont, node, src, dst, this->h_rsf,
				this->v_rsf, &last, this);
		break;

	case B2R2_SCALE_AND_ROTATE:
		ret = configure_rot_scale(cont, node, &last, this);
		break;

	default:
		b2r2_log_warn(cont->dev, "%s: Unsupported request\n", __func__);
		ret = -ENOSYS;
		goto error;
		break;

	}

	if (ret < 0)
		goto error;

	/* Scale and rotate will configure its own blending and clipping */
	if (this->type != B2R2_SCALE_AND_ROTATE) {

		/* Configure blending and clipping */
		do {
			if (node == NULL) {
				b2r2_log_warn(cont->dev, "%s: "
					"Internal error! Out of nodes!\n",
					__func__);
				ret = -ENOMEM;
				goto error;
			}

			if (this->blend) {
				if (this->flags & B2R2_BLT_FLAG_BG_BLEND)
					configure_bg(cont, node, bg,
							this->swap_fg_bg);
				else
					configure_bg(cont, node, dst,
							this->swap_fg_bg);
				configure_blend(cont, node, this->flags,
						this->global_alpha);
			}
			if (this->clip)
				configure_clip(cont, node, &this->clip_rect);

			node = node->next;

		} while (node != last);
	}

	/* Consume the nodes */
	*next = last;

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: Error!\n", __func__);
	return ret;
}

/*
 * configure_sub_rot() - configure a sub-rotation
 *
 * This functions configures a set of nodes for rotation using the destination
 * window instead of the rectangle for calculating tiles.
 */
static int configure_sub_rot(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;

	struct b2r2_blt_rect src_win;
	struct b2r2_blt_rect dst_win;

	u32 y_pixels = 0;
	u32 x_pixels = 0;

	memcpy(&src_win, &src->win, sizeof(src_win));
	memcpy(&dst_win, &dst->win, sizeof(dst_win));

	b2r2_log_info(cont->dev, "%s: src_win=(%d, %d, %d, %d) "
			"dst_win=(%d, %d, %d, %d)\n", __func__,
			src_win.x, src_win.y, src_win.width, src_win.height,
			dst_win.x, dst_win.y, dst_win.width, dst_win.height);

	dst->win.height = B2R2_ROTATE_MAX_WIDTH;
	if (dst->win.width % B2R2_ROTATE_MAX_WIDTH)
		dst->win.width -= dst->win.width % B2R2_ROTATE_MAX_WIDTH;

	while (x_pixels < dst_win.width) {
		u32 src_x = src->win.x;
		u32 src_w = src->win.width;
		u32 dst_y = dst->win.y;
		u32 dst_h = dst->win.height;

		dst->win.width = min(dst->win.width, dst_win.width -
			(int)x_pixels);
		src->win.height = dst->win.width;

		b2r2_log_info(cont->dev, "%s: x_pixels=%d\n",
			__func__, x_pixels);

		while (y_pixels < dst_win.height) {
			dst->win.height = min(dst->win.height,
					dst_win.height - (int)y_pixels);
			src->win.width = dst->win.height;

			b2r2_log_info(cont->dev, "%s: y_pixels=%d\n",
				__func__, y_pixels);

			ret = configure_rotate(cont, node, src, dst, &node,
					this);
			if (ret < 0)
				goto error;

			src->win.x += (src->hso == B2R2_TY_HSO_LEFT_TO_RIGHT) ?
					src->win.width : -src->win.width;
			dst->win.y += (dst->vso == B2R2_TY_VSO_TOP_TO_BOTTOM) ?
					dst->win.height : -dst->win.height;

			y_pixels += dst->win.height;
		}

		src->win.x = src_x;
		src->win.y += (src->vso == B2R2_TY_VSO_TOP_TO_BOTTOM) ?
				src->win.height : -src->win.height;
		src->win.width = src_w;

		dst->win.x += (dst->hso == B2R2_TY_HSO_LEFT_TO_RIGHT) ?
				dst->win.width : -dst->win.width;
		dst->win.y = dst_y;
		dst->win.height = dst_h;

		x_pixels += dst->win.width;
		y_pixels = 0;

	}

	memcpy(&src->win, &src_win, sizeof(src->win));
	memcpy(&dst->win, &dst_win, sizeof(dst->win));

	*next = node;

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: error!\n", __func__);
	return ret;
}

/**
 * configure_rot_downscale() - configures a combined rotate and downscale
 *
 * When doing a downscale it is better to do the rotation last.
 */
static int configure_rot_downscale(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;

	struct b2r2_node_split_buf *src = &this->src;
	struct b2r2_node_split_buf *dst = &this->dst;
	struct b2r2_node_split_buf *tmp = &this->tmp_bufs[0];

	tmp->win.x = 0;
	tmp->win.y = 0;
	tmp->win.width = dst->win.height;
	tmp->win.height = dst->win.width;

	ret = configure_scale(cont, node, src, tmp, this->h_rsf, this->v_rsf,
			&node, this);
	if (ret < 0)
		goto error;

	ret = configure_sub_rot(cont, node, tmp, dst, &node, NULL);
	if (ret < 0)
		goto error;

	*next = node;

	return 0;

error:
	b2r2_log_info(cont->dev, "%s: error!\n", __func__);
	return ret;
}

/**
 * configure_rot_upscale() - configures a combined rotate and upscale
 *
 * When doing an upscale it is better to do the rotation first.
 */
static int configure_rot_upscale(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	/* TODO: Implement a optimal upscale (rotation first) */
	return configure_rot_downscale(cont, node, next, this);
}

/**
 * configure_rot_scale() - configures a combined rotation and scaling op
 */
static int configure_rot_scale(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;

	bool upscale = (u32)this->h_rsf * (u32)this->v_rsf < (1 << 20);

	if (upscale)
		ret = configure_rot_upscale(cont, node, next, this);
	else
		ret = configure_rot_downscale(cont, node, next, this);

	if (ret < 0)
		goto error;

	return 0;

error:
	b2r2_log_warn(cont->dev, "%s: error!\n", __func__);
	return ret;
}

/**
 * configure_direct_fill() - configures the given node for direct fill
 *
 * @cont  - the b2r2 core control
 * @node  - the node to configure
 * @color - the fill color
 * @dst   - the destination buffer
 * @next  - the next empty node in the node list
 *
 * This operation will always consume one node only.
 */
static void configure_direct_fill(
		struct b2r2_control *cont,
		struct b2r2_node *node,
		u32 color,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next)
{
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_COLOR_FILL | B2R2_CIC_SOURCE_1;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_1_DIRECT_FILL;

	/* Target setup */
	set_target(node, dst->addr, dst);

	/* Source setup */

	/* It seems B2R2 checks so that source and dest has the same format */
	node->node.GROUP3.B2R2_STY = b2r2_to_native_fmt(dst->fmt);
	node->node.GROUP2.B2R2_S1CF = color;
	node->node.GROUP2.B2R2_S2CF = 0;

	/* Consume the node */
	*next = node->next;
}

/**
 * configure_direct_copy() - configures the node for direct copy
 *
 * @cont - the b2r2 core control
 * @node - the node to configure
 * @src  - the source buffer
 * @dst  - the destination buffer
 * @next - the next empty node in the node list
 *
 * This operation will always consume one node only.
 */
static void configure_direct_copy(
		struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next)
{
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_1;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_1_DIRECT_COPY;

	/* Source setup, use the base function to avoid altering the INS */
	set_src(&node->node.GROUP3, src->addr, src);

	/* Target setup */
	set_target(node, dst->addr, dst);

	/* Consume the node */
	*next = node->next;
}

/**
 * configure_fill() - configures the given node for color fill
 *
 * @cont  - the b2r2 core control
 * @node  - the node to configure
 * @color - the fill color
 * @fmt   - the source color format
 * @dst   - the destination buffer
 * @next  - the next empty node in the node list
 * @this  - the current b2r2 node split job
 *
 * A normal fill operation can be combined with any other per pixel operations
 * such as blend.
 *
 * This operation will consume as many nodes as are required to write to the
 * destination format.
 */
static int configure_fill(
		struct b2r2_control *cont,
		struct b2r2_node *node,
		u32 color,
		enum b2r2_blt_fmt fmt,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;
	struct b2r2_node *last;

	/* Configure the destination */
	ret = configure_dst(cont, node, dst, &last);
	if (ret < 0)
		goto error;

	do {
		if (node == NULL) {
			b2r2_log_warn(cont->dev, "%s: "
			"Internal error! Out of nodes!\n", __func__);
			ret = -ENOMEM;
			goto error;
		}

		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2 |
							B2R2_CIC_COLOR_FILL;
		node->node.GROUP0.B2R2_INS |=
				B2R2_INS_SOURCE_2_COLOR_FILL_REGISTER;
		node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;

		/*
		 * B2R2 has a bug that disables color fill from S2. As a
		 * workaround we use S1 for the color.
		 */
		node->node.GROUP2.B2R2_S1CF = 0;
		node->node.GROUP2.B2R2_S2CF = color;

		/* TO BE REMOVED: */
		set_src_2(node, dst->addr, dst);
		node->node.GROUP4.B2R2_STY = b2r2_to_native_fmt(fmt);

		/* Setup the VMX for color conversion */
		if (this != NULL && this->ivmx != NULL)
			set_ivmx(node, this->ivmx);

		if  ((dst->type == B2R2_FMT_TYPE_PLANAR) ||
				(dst->type == B2R2_FMT_TYPE_SEMI_PLANAR)) {

			node->node.GROUP0.B2R2_INS |=
				B2R2_INS_RESCALE2D_ENABLED;
			node->node.GROUP8.B2R2_FCTL =
				B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_LUMA_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_LUMA_VF2D_MODE_ENABLE_RESIZER;
			node->node.GROUP9.B2R2_RSF =
				(1 << (B2R2_RSF_HSRC_INC_SHIFT + 10)) |
				(1 << (B2R2_RSF_VSRC_INC_SHIFT + 10));
			node->node.GROUP9.B2R2_RZI =
				B2R2_RZI_DEFAULT_HNB_REPEAT |
				(2 << B2R2_RZI_VNB_REPEAT_SHIFT);

			node->node.GROUP10.B2R2_RSF =
				(1 << (B2R2_RSF_HSRC_INC_SHIFT + 10)) |
				(1 << (B2R2_RSF_VSRC_INC_SHIFT + 10));
			node->node.GROUP10.B2R2_RZI =
				B2R2_RZI_DEFAULT_HNB_REPEAT |
				(2 << B2R2_RZI_VNB_REPEAT_SHIFT);
		}

		node = node->next;

	} while (node != last);

	/* Consume the nodes */
	*next = node;

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

/**
 * configure_copy() - configures the given node for a copy operation
 *
 * @cont - the b2r2 core control
 * @node - the node to configure
 * @src  - the source buffer
 * @dst  - the destination buffer
 * @next - the next empty node in the node list
 * @this - the current node job
 *
 * This operation will consume as many nodes as are required to write to the
 * destination format.
 */
static int configure_copy(
		struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;

	struct b2r2_node *last;

	/* If the sources are swapped, change the iVMx/oVMx accordingly */
	if (this != NULL && this->swap_fg_bg) {
		/* Setup iVMx for this scenario */
		ret = analyze_fmt_conv(cont, dst, src,
				&this->ivmx, NULL, this->fullrange);
		if (ret < 0)
			goto error;

		/* Setup oVMx for this scenario */
		ret = analyze_fmt_conv(cont, src, dst,
				&this->ovmx, NULL, this->fullrange);
		if (ret < 0)
			goto error;
	}

	ret = configure_dst(cont, node, dst, &last);
	if (ret < 0)
		goto error;

	/* Configure the source for each node */
	do {
		if (node == NULL) {
			b2r2_log_warn(cont->dev, "%s: "
				" Internal error! Out of nodes!\n",
				__func__);
			ret = -ENOMEM;
			goto error;
		}

		node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;
		if (this != NULL &&
				(this->flags & B2R2_BLT_FLAG_SOURCE_COLOR_KEY)
				!= 0) {
			u32 key_color = 0;

			node->node.GROUP0.B2R2_ACK |=
				B2R2_ACK_CKEY_SEL_SRC_AFTER_CLUT |
				B2R2_ACK_CKEY_RED_MATCH_IF_BETWEEN |
				B2R2_ACK_CKEY_GREEN_MATCH_IF_BETWEEN |
				B2R2_ACK_CKEY_BLUE_MATCH_IF_BETWEEN;
			node->node.GROUP0.B2R2_INS |= B2R2_INS_CKEY_ENABLED;
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_COLOR_KEY;

			key_color = b2r2_to_RGB888(this->flag_param, src->fmt);
			node->node.GROUP12.B2R2_KEY1 = key_color;
			node->node.GROUP12.B2R2_KEY2 = key_color;
		}

		if (this != NULL &&
				(this->flags &
				B2R2_BLT_FLAG_CLUT_COLOR_CORRECTION) != 0) {
			struct b2r2_blt_request *request =
				container_of(this, struct b2r2_blt_request,
						node_split_job);
			node->node.GROUP0.B2R2_INS |= B2R2_INS_CLUTOP_ENABLED;
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_CLUT;
			node->node.GROUP7.B2R2_CCO =
				B2R2_CCO_CLUT_COLOR_CORRECTION |
				B2R2_CCO_CLUT_UPDATE;
			node->node.GROUP7.B2R2_CML = request->clut_phys_addr;
		}
		/* Configure the source(s) */
		configure_src(cont, node, src, this);

		node = node->next;
	} while (node != last);

	/* Consume the nodes */
	*next = node;

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

/**
 * configure_rotate() - configures the given node for rotation
 *
 * @cont - the b2r2 core control
 * @node - the node to configure
 * @src  - the source buffer
 * @dst  - the destination buffer
 * @next - the next empty node in the node list
 * @this - the current b2r2 node split job
 *
 * This operation will consume as many nodes are are required by the combination
 * of rotating and writing the destination format.
 */
static int configure_rotate(
		struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;

	struct b2r2_node *last;

	ret = configure_copy(cont, node, src, dst, &last, this);
	if (ret < 0)
		goto error;

	do {
		if (node == NULL) {
			b2r2_log_warn(cont->dev, "%s: "
				"Internal error! Out of nodes!\n",
				__func__);
			ret = -ENOMEM;
			goto error;
		}

		node->node.GROUP0.B2R2_INS |= B2R2_INS_ROTATION_ENABLED;

		b2r2_log_debug(cont->dev, "%s:\n"
				"\tB2R2_TXY:  %.8x\tB2R2_TSZ:  %.8x\n"
				"\tB2R2_S1XY: %.8x\tB2R2_S1SZ: %.8x\n"
				"\tB2R2_S2XY: %.8x\tB2R2_S2SZ: %.8x\n"
				"\tB2R2_S3XY: %.8x\tB2R2_S3SZ: %.8x\n"
				"-----------------------------------\n",
				__func__, node->node.GROUP1.B2R2_TXY,
				node->node.GROUP1.B2R2_TSZ,
				node->node.GROUP3.B2R2_SXY,
				node->node.GROUP3.B2R2_SSZ,
				node->node.GROUP4.B2R2_SXY,
				node->node.GROUP4.B2R2_SSZ,
				node->node.GROUP5.B2R2_SXY,
				node->node.GROUP5.B2R2_SSZ);

		node = node->next;

	} while (node != last);

	/* Consume the nodes */
	*next = node;

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: error!\n", __func__);
	return ret;
}

/**
 * configure_scale() - configures the given node for scaling
 *
 * @cont  - the b2r2 core control
 * @node  - the node to configure
 * @src   - the source buffer
 * @dst   - the destination buffer
 * @h_rsf - the horizontal rescale factor
 * @v_rsf - the vertical rescale factor
 * @next  - the next empty node in the node list
 * @this  - the current b2r2 node split job
 */
static int configure_scale(
		struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_buf *dst,
		u16 h_rsf, u16 v_rsf,
		struct b2r2_node **next,
		struct b2r2_node_split_job *this)
{
	int ret;

	struct b2r2_node *last;

	struct b2r2_filter_spec *hf = NULL;
	struct b2r2_filter_spec *vf = NULL;

	u32 fctl = 0;
	u32 rsf = 0;
	u32 rzi = 0;
	u32 hsrc_init = 0;
	u32 vsrc_init = 0;
	u32 hfp = 0;
	u32 vfp = 0;

	u16 luma_h_rsf = h_rsf;
	u16 luma_v_rsf = v_rsf;

	struct b2r2_filter_spec *luma_hf = NULL;
	struct b2r2_filter_spec *luma_vf = NULL;

	u32 luma_fctl = 0;
	u32 luma_rsf = 0;
	u32 luma_rzi = 0;
	u32 luma_hsrc_init = 0;
	u32 luma_vsrc_init = 0;
	u32 luma_hfp = 0;
	u32 luma_vfp = 0;

	s32 src_x;
	s32 src_y;
	s32 src_w;
	s32 src_h;

	bool upsample;
	bool downsample;

	struct b2r2_blt_rect tmp_win = src->win;
	bool src_raster = src->type == B2R2_FMT_TYPE_RASTER;
	bool dst_raster = dst->type == B2R2_FMT_TYPE_RASTER;

	/* Rescale the normalized source window */
	src_x = inv_rescale(src->win.x - src->rect.x, luma_h_rsf);
	src_y = inv_rescale(src->win.y - src->rect.y, luma_v_rsf);
	src_w = inv_rescale(src->win.width, luma_h_rsf);
	src_h = inv_rescale(src->win.height, luma_v_rsf);

	/* Convert to src coordinate space */
	src->win.x = (src_x >> 10) + src->rect.x;
	src->win.y = (src_y >> 10) + src->rect.y;

	/*
	 * Since the stripe might start and end on a fractional pixel
	 * we need to count all the touched pixels in the width.
	 *
	 * Example:
	 *   src_x = 1.8, src_w = 2.8
	 *
	 *   The stripe touches pixels 1.8 through 4.6, i.e. 4 pixels
	 */
	src->win.width = ((src_x & 0x3ff) + src_w + 0x3ff) >> 10;
	src->win.height = ((src_y & 0x3ff) + src_h + 0x3ff) >> 10;

	luma_hsrc_init = src_x & 0x3ff;
	luma_vsrc_init = src_y & 0x3ff;

	/* Check for upsampling of chroma */
	upsample = !src_raster && !b2r2_is_yuv444_fmt(src->fmt);
	if (upsample) {
		h_rsf /= 2;

		if (b2r2_is_yuv420_fmt(src->fmt) ||
				b2r2_is_yvu420_fmt(src->fmt))
			v_rsf /= 2;
	}

	/* Check for downsampling of chroma */
	downsample = !dst_raster && !b2r2_is_yuv444_fmt(dst->fmt);
	if (downsample) {
		h_rsf *= 2;

		if (b2r2_is_yuv420_fmt(dst->fmt) ||
				b2r2_is_yvu420_fmt(dst->fmt))
			v_rsf *= 2;
	}

	src_x = inv_rescale(tmp_win.x - src->rect.x, h_rsf);
	src_y = inv_rescale(tmp_win.y - src->rect.y, v_rsf);
	hsrc_init = src_x & 0x3ff;
	vsrc_init = src_y & 0x3ff;

	/* Configure resize and filters */
	fctl = B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
			B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;
	luma_fctl = B2R2_FCTL_LUMA_HF2D_MODE_ENABLE_RESIZER |
			B2R2_FCTL_LUMA_VF2D_MODE_ENABLE_RESIZER;

	rsf =  (h_rsf << B2R2_RSF_HSRC_INC_SHIFT) |
			(v_rsf << B2R2_RSF_VSRC_INC_SHIFT);
	luma_rsf = (luma_h_rsf << B2R2_RSF_HSRC_INC_SHIFT) |
			(luma_v_rsf << B2R2_RSF_VSRC_INC_SHIFT);

	rzi = B2R2_RZI_DEFAULT_HNB_REPEAT |
			(2 << B2R2_RZI_VNB_REPEAT_SHIFT) |
			(hsrc_init << B2R2_RZI_HSRC_INIT_SHIFT) |
			(vsrc_init << B2R2_RZI_VSRC_INIT_SHIFT);
	luma_rzi = B2R2_RZI_DEFAULT_HNB_REPEAT |
			(2 << B2R2_RZI_VNB_REPEAT_SHIFT) |
			(luma_hsrc_init << B2R2_RZI_HSRC_INIT_SHIFT) |
			(luma_vsrc_init << B2R2_RZI_VSRC_INIT_SHIFT);

	/*
	 * We should only filter if there is an actual rescale (i.e. not when
	 * up or downsampling).
	 */
	if (luma_h_rsf != (1 << 10)) {
		hf = b2r2_filter_find(h_rsf);
		luma_hf = b2r2_filter_find(luma_h_rsf);
	}
	if (luma_v_rsf != (1 << 10)) {
		vf = b2r2_filter_find(v_rsf);
		luma_vf = b2r2_filter_find(luma_v_rsf);
	}

	if (hf) {
		fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_COLOR_CHANNEL_FILTER;
		hfp = hf->h_coeffs_phys_addr;
	}

	if (vf) {
		fctl |= B2R2_FCTL_VF2D_MODE_ENABLE_COLOR_CHANNEL_FILTER;
		vfp = vf->v_coeffs_phys_addr;
	}

	if (luma_hf) {
		luma_fctl |= B2R2_FCTL_LUMA_HF2D_MODE_ENABLE_FILTER;
		luma_hfp = luma_hf->h_coeffs_phys_addr;
	}

	if (luma_vf) {
		luma_fctl |= B2R2_FCTL_LUMA_VF2D_MODE_ENABLE_FILTER;
		luma_vfp = luma_vf->v_coeffs_phys_addr;
	}

	ret = configure_copy(cont, node, src, dst, &last, this);
	if (ret < 0)
		goto error;

	do {
		bool chroma_rescale =
				(h_rsf != (1 << 10)) || (v_rsf != (1 << 10));
		bool luma_rescale =
				(luma_h_rsf != (1 << 10)) ||
				(luma_v_rsf != (1 << 10));
		bool dst_chroma = node->node.GROUP1.B2R2_TTY &
				B2R2_TTY_CHROMA_NOT_LUMA;
		bool dst_luma = !dst_chroma;

		if (node == NULL) {
			b2r2_log_warn(cont->dev, "%s: Internal error! Out "
					"of nodes!\n", __func__);
			ret = -ENOMEM;
			goto error;
		}

		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_FILTER_CONTROL;

		/*
		 * If the source format is anything other than raster, we
		 * always have to enable both chroma and luma resizers. This
		 * could be a bug in the hardware, since it is not mentioned in
		 * the specification.
		 *
		 * Otherwise, we will only enable the chroma resizer when
		 * writing chroma and the luma resizer when writing luma
		 * (or both when writing raster). Also, if there is no rescale
		 * to be done there's no point in using the resizers.
		 */

		if (!src_raster || (chroma_rescale &&
				(dst_raster || dst_chroma))) {
			/* Enable chroma resize */
			node->node.GROUP0.B2R2_INS |=
					B2R2_INS_RESCALE2D_ENABLED;
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_RESIZE_CHROMA;
			node->node.GROUP8.B2R2_FCTL |= fctl;

			node->node.GROUP9.B2R2_RSF = rsf;
			node->node.GROUP9.B2R2_RZI = rzi;
			node->node.GROUP9.B2R2_HFP = hfp;
			node->node.GROUP9.B2R2_VFP = vfp;
		}

		if (!src_raster || (luma_rescale &&
				(dst_raster || dst_luma))) {
			/* Enable luma resize */
			node->node.GROUP0.B2R2_INS |=
					B2R2_INS_RESCALE2D_ENABLED;
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_RESIZE_LUMA;
			node->node.GROUP8.B2R2_FCTL |= luma_fctl;

			node->node.GROUP10.B2R2_RSF = luma_rsf;
			node->node.GROUP10.B2R2_RZI = luma_rzi;
			node->node.GROUP10.B2R2_HFP = luma_hfp;
			node->node.GROUP10.B2R2_VFP = luma_vfp;
			/*
			 * Scaling operation from raster to a multi-buffer
			 * format, requires the raster input to be scaled
			 * before luminance information can be extracted.
			 * Raster input is scaled by the chroma resizer.
			 * Luma resizer only handles luminance data which
			 * exists in a separate buffer in source image,
			 * as is the case with YUV planar/semi-planar formats.
			 */
			if (src_raster) {
				/* Activate chroma scaling */
				node->node.GROUP0.B2R2_CIC |=
					B2R2_CIC_RESIZE_CHROMA;
				node->node.GROUP8.B2R2_FCTL |= fctl;
				/*
				 * Color data must be scaled
				 * to the same size as luma.
				 * Use luma scaling parameters.
				 */
				node->node.GROUP9.B2R2_RSF = luma_rsf;
				node->node.GROUP9.B2R2_RZI = luma_rzi;
				node->node.GROUP9.B2R2_HFP = luma_hfp;
				node->node.GROUP9.B2R2_VFP = luma_vfp;
			}
		}

		b2r2_log_info(cont->dev, "%s:\n"
				"\tB2R2_TXY:  %.8x\tB2R2_TSZ:  %.8x\n"
				"\tB2R2_S1XY: %.8x\tB2R2_S1SZ: %.8x\n"
				"\tB2R2_S2XY: %.8x\tB2R2_S2SZ: %.8x\n"
				"\tB2R2_S3XY: %.8x\tB2R2_S3SZ: %.8x\n"
				"----------------------------------\n",
				__func__, node->node.GROUP1.B2R2_TXY,
				node->node.GROUP1.B2R2_TSZ,
				node->node.GROUP3.B2R2_SXY,
				node->node.GROUP3.B2R2_SSZ,
				node->node.GROUP4.B2R2_SXY,
				node->node.GROUP4.B2R2_SSZ,
				node->node.GROUP5.B2R2_SXY,
				node->node.GROUP5.B2R2_SSZ);

		node = node->next;

	} while (node != last);



	/* Consume the nodes */
	*next = node;

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;
}

/**
 * configure_src() - configures the source registers and the iVMX
 *
 * @cont - the b2r2 core control
 * @node - the node to configure
 * @src  - the source buffer
 * @this - the current node split job
 *
 * This operation will not consume any nodes
 */
static void configure_src(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *src,
		struct b2r2_node_split_job *this)
{
	struct b2r2_node_split_buf tmp_buf;

	b2r2_log_info(cont->dev,
			"%s: src.win=(%d, %d, %d, %d)\n", __func__,
			src->win.x, src->win.y, src->win.width,
			src->win.height);

	/* Configure S1 - S3 */
	switch (src->type) {
	case B2R2_FMT_TYPE_RASTER:
		if (this != NULL && this->swap_fg_bg)
			set_src_1(node, src->addr, src);
		else
			set_src_2(node, src->addr, src);
		break;
	case B2R2_FMT_TYPE_SEMI_PLANAR:
		memcpy(&tmp_buf, src, sizeof(tmp_buf));

		/*
		 * For 420 and 422 the chroma has lower resolution than the
		 * luma
		 */
		if (!b2r2_is_yuv444_fmt(src->fmt)) {
			tmp_buf.win.x >>= 1;
			tmp_buf.win.width = (tmp_buf.win.width + 1) / 2;

			if (b2r2_is_yuv420_fmt(src->fmt) ||
					b2r2_is_yvu420_fmt(src->fmt)) {
				tmp_buf.win.height =
						(tmp_buf.win.height + 1) / 2;
				tmp_buf.win.y >>= 1;
			}
		}

		set_src_3(node, src->addr, src);
		set_src_2(node, tmp_buf.chroma_addr, &tmp_buf);
		break;
	case B2R2_FMT_TYPE_PLANAR:
		memcpy(&tmp_buf, src, sizeof(tmp_buf));

		if (!b2r2_is_yuv444_fmt(src->fmt)) {
			/*
			 * Each chroma buffer will have half as many values
			 * per line as the luma buffer
			 */
			tmp_buf.pitch = b2r2_get_chroma_pitch(src->pitch,
				src->fmt);

			/* Horizontal resolution is half */
			tmp_buf.win.x >>= 1;
			tmp_buf.win.width = (tmp_buf.win.width + 1) / 2;

			/*
			 * If the buffer is in YUV420 format, the vertical
			 * resolution is half as well
			 */
			if (b2r2_is_yuv420_fmt(src->fmt) ||
					b2r2_is_yvu420_fmt(src->fmt)) {
				tmp_buf.win.height =
						(tmp_buf.win.height + 1) / 2;
				tmp_buf.win.y >>= 1;
			}
		}

		set_src_3(node, src->addr, src);
		/*
		 * The VMXs are restrained by the semi planar input
		 * order. And since the internal format of b2r2 is 32-bit
		 * AYUV we need to supply same order of components on
		 * the b2r2 bus for the planar input formats.
		 */
		if (b2r2_is_yvu_fmt(src->fmt)) {
			set_src_1(node, tmp_buf.chroma_addr, &tmp_buf);
			set_src_2(node, tmp_buf.chroma_cr_addr, &tmp_buf);
		} else {
			set_src_2(node, tmp_buf.chroma_addr, &tmp_buf);
			set_src_1(node, tmp_buf.chroma_cr_addr, &tmp_buf);
		}

		break;
	default:
		/* Should never, ever happen */
		BUG_ON(1);
		break;
	}

	/* Configure the iVMx and oVMx for color space conversions */
	if (this != NULL && this->ivmx != NULL)
		set_ivmx(node, this->ivmx);

	if (this != NULL && this->ovmx != NULL)
		set_ovmx(node, this->ovmx);
}

/**
 * configure_bg() - configures a background for the given node
 *
 * @cont         - the b2r2 core control
 * @node         - the node to configure
 * @bg           - the background buffer
 * @swap_fg_bg   - if true, fg will be on s1 instead of s2
 *
 * This operation will not consume any nodes.
 *
 * NOTE: This method should be called _AFTER_ the destination has been
 *       configured.
 *
 * WARNING: Take care when using this with semi-planar or planar sources since
 *          either S1 or S2 will be overwritten!
 */
static void configure_bg(struct b2r2_control *cont,
		struct b2r2_node *node,
		struct b2r2_node_split_buf *bg, bool swap_fg_bg)
{
	b2r2_log_info(cont->dev,
			"%s: bg.win=(%d, %d, %d, %d)\n", __func__,
			bg->win.x, bg->win.y, bg->win.width,
			bg->win.height);

	/* Configure S1 */
	switch (bg->type) {
	case B2R2_FMT_TYPE_RASTER:
		if (swap_fg_bg) {
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2;
			node->node.GROUP0.B2R2_INS |=
				B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
			node->node.GROUP0.B2R2_ACK |= B2R2_ACK_SWAP_FG_BG;

			set_src(&node->node.GROUP4, bg->addr, bg);
		} else {
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_1;
			node->node.GROUP0.B2R2_INS |=
				B2R2_INS_SOURCE_1_FETCH_FROM_MEM;

			set_src(&node->node.GROUP3, bg->addr, bg);
		}
		break;
	default:
		/* Should never, ever happen */
		BUG_ON(1);
		break;
	}
}

/**
 * configure_dst() - configures the destination registers of the given node
 *
 * @cont - the b2r2 core control
 * @node - the node to configure
 * @dst  - the destination buffer
 * @next - the next b2r2 node
 *
 * This operation will consume as many nodes as are required to write the
 * destination format.
 */
static int configure_dst(struct b2r2_control *cont, struct b2r2_node *node,
		struct b2r2_node_split_buf *dst, struct b2r2_node **next)
{
	int ret;
	int nbr_planes = 1;
	int i;

	struct b2r2_node_split_buf dst_planes[3];

	b2r2_log_info(cont->dev,
		"%s: dst.win=(%d, %d, %d, %d)\n", __func__,
		dst->win.x, dst->win.y, dst->win.width,
		dst->win.height);

	memcpy(&dst_planes[0], dst, sizeof(dst_planes[0]));

	if (dst->type != B2R2_FMT_TYPE_RASTER) {
		/* There will be at least 2 planes */
		nbr_planes = 2;

		memcpy(&dst_planes[1], dst, sizeof(dst_planes[1]));

		dst_planes[1].addr = dst->chroma_addr;
		dst_planes[1].plane_selection = B2R2_TTY_CHROMA_NOT_LUMA;

		if (!b2r2_is_yuv444_fmt(dst->fmt)) {
			/* Horizontal resolution is half */
			dst_planes[1].win.x /= 2;
			/*
			 * Must round up the chroma size to handle cases when
			 * luma size is not divisible by 2. E.g. luma width==7 r
			 * equires chroma width==4. Chroma width==7/2==3 is only
			 * enough for luma width==6.
			 */
			dst_planes[1].win.width =
				(dst_planes[1].win.width + 1) / 2;

			/*
			 * If the buffer is in YUV420 format, the vertical
			 * resolution is half as well. Height must be rounded in
			 * the same way as is done for width.
			 */
			if (b2r2_is_yuv420_fmt(dst->fmt) ||
					b2r2_is_yvu420_fmt(dst->fmt)) {
				dst_planes[1].win.y /= 2;
				dst_planes[1].win.height =
					(dst_planes[1].win.height + 1) / 2;
			}
		}

		if (dst->type == B2R2_FMT_TYPE_PLANAR) {
			/* There will be a third plane as well */
			nbr_planes = 3;

			dst_planes[1].pitch = b2r2_get_chroma_pitch(
				dst->pitch, dst->fmt);

			memcpy(&dst_planes[2], &dst_planes[1],
				sizeof(dst_planes[2]));

			dst_planes[2].addr = dst->chroma_cr_addr;
			/*
			 * The third plane will be Cr.
			 * The flag B2R2_TTY_CB_NOT_CR actually works
			 * the other way around, i.e. as if it was
			 * B2R2_TTY_CR_NOT_CB.
			 */
			dst_planes[2].chroma_selection
					= B2R2_TTY_CB_NOT_CR;

			/* switch the U and V planes for YVU formats */
			if (b2r2_is_yvu420_fmt(dst->fmt)) {
				dst_planes[2].addr = dst->chroma_addr;
				dst_planes[1].addr = dst->chroma_cr_addr;
			}
		}

	}

	/* Configure one node for each plane */
	for (i = 0; i < nbr_planes; i++) {

		if (node == NULL) {
			b2r2_log_warn(cont->dev, "%s: "
				"Internal error! Out of nodes!\n", __func__);
			ret = -ENOMEM;
			goto error;
		}

		/*
		 * When writing chroma, there's no need to read the luma and
		 * vice versa.
		 */
		if ((node->node.GROUP3.B2R2_STY & B2R2_NATIVE_YUV) &&
				(nbr_planes > 1)) {
			if (i != 0) {
				node->node.GROUP4.B2R2_STY |=
					B2R2_S3TY_ENABLE_BLANK_ACCESS;
			}
			if (i != 1) {
				node->node.GROUP0.B2R2_INS &=
					~B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
				node->node.GROUP0.B2R2_INS |=
					B2R2_INS_SOURCE_2_COLOR_FILL_REGISTER;
			}
			if (i != 2) {
				node->node.GROUP0.B2R2_INS &=
					~B2R2_INS_SOURCE_1_FETCH_FROM_MEM;
				node->node.GROUP0.B2R2_INS |=
					B2R2_INS_SOURCE_1_COLOR_FILL_REGISTER;
			}
		} else if ((node->node.GROUP3.B2R2_STY &
					(B2R2_NATIVE_YCBCR42X_MBN |
						B2R2_NATIVE_YCBCR42X_R2B)) &&
				(nbr_planes > 1)) {
			if (i != 0) {
				node->node.GROUP4.B2R2_STY |=
					B2R2_S3TY_ENABLE_BLANK_ACCESS;
			}
		}

		set_target(node, dst_planes[i].addr, &dst_planes[i]);

		node = node->next;
	}

	/* Consume the nodes */
	*next = node;

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;

}

/**
 * configure_blend() - configures the given node for alpha blending
 *
 * @cont         - the b2r2 core control
 * @node         - the node to configure
 * @flags        - the flags passed in the blt_request
 * @global_alpha - the global alpha to use (if enabled in flags)
 *
 * This operation will not consume any nodes.
 *
 * NOTE: This method should be called _AFTER_ the destination has been
 *       configured.
 *
 * WARNING: Take care when using this with semi-planar or planar sources since
 *          either S1 or S2 will be overwritten!
 */
static void configure_blend(struct b2r2_control *cont,
		struct b2r2_node *node, u32 flags, u32 global_alpha)
{
	node->node.GROUP0.B2R2_ACK &= ~(B2R2_ACK_MODE_BYPASS_S2_S3);

	/* Check if the foreground is premultiplied */
	if ((flags & B2R2_BLT_FLAG_SRC_IS_NOT_PREMULT) != 0)
		node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BLEND_NOT_PREMULT;
	else
		node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BLEND_PREMULT;

	/* Check if global alpha blend should be enabled */
	if (flags & B2R2_BLT_FLAG_GLOBAL_ALPHA_BLEND) {

		/* B2R2 expects the global alpha to be in 0...128 range */
		global_alpha = (global_alpha*128)/255;

		node->node.GROUP0.B2R2_ACK |=
			global_alpha <<	B2R2_ACK_GALPHA_ROPID_SHIFT;
	} else {
		node->node.GROUP0.B2R2_ACK |=
			(128 << B2R2_ACK_GALPHA_ROPID_SHIFT);
	}
}

/**
 * configure_clip() - configures destination clipping for the given node
 *
 * @cont      - the b2r2 core control
 * @node      - the node to configure
 * @clip_rect - the clip rectangle
 *
 *  This operation does not consume any nodes.
 */
static void configure_clip(struct b2r2_control *cont, struct b2r2_node *node,
		struct b2r2_blt_rect *clip_rect)
{
	s32 l = clip_rect->x;
	s32 r = clip_rect->x + clip_rect->width - 1;
	s32 t = clip_rect->y;
	s32 b = clip_rect->y + clip_rect->height - 1;

	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_CLIP_WINDOW;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_RECT_CLIP_ENABLED;

	/* Clip window setup */
	node->node.GROUP6.B2R2_CWO =
			((t & 0x7FFF) << B2R2_CWO_Y_SHIFT) |
			((l & 0x7FFF) << B2R2_CWO_X_SHIFT);
	node->node.GROUP6.B2R2_CWS =
			((b & 0x7FFF) << B2R2_CWO_Y_SHIFT) |
			((r & 0x7FFF) << B2R2_CWO_X_SHIFT);
}

/**
 * set_buf() - configures the given buffer with the provided values
 *
 * @cont       - the b2r2 core control
 * @buf        - the buffer to configure
 * @addr       - the physical base address
 * @img        - the blt image to base the buffer on
 * @rect       - the rectangle to use
 * @color_fill - determines whether the buffer should be used for color fill
 * @color      - the color to use in case of color fill
 */
static void set_buf(struct b2r2_control *cont,
		struct b2r2_node_split_buf *buf,
		u32 addr,
		const struct b2r2_blt_img *img,
		const struct b2r2_blt_rect *rect,
		bool color_fill,
		u32 color)
{
	memset(buf, 0, sizeof(*buf));

	buf->fmt = img->fmt;
	buf->type = b2r2_get_fmt_type(img->fmt);

	if (color_fill) {
		buf->type = B2R2_FMT_TYPE_RASTER;
		buf->color = color;
	} else {
		buf->addr = addr;

		buf->alpha_range = b2r2_get_alpha_range(img->fmt);

		if (img->pitch == 0)
			buf->pitch = b2r2_fmt_byte_pitch(img->fmt, img->width);
		else
			buf->pitch = img->pitch;

		buf->height = img->height;
		buf->width = img->width;

		switch (buf->type) {
		case B2R2_FMT_TYPE_SEMI_PLANAR:
			b2r2_get_cb_cr_addr(buf->addr, buf->pitch, buf->height,
				buf->fmt, &buf->chroma_addr,
				&buf->chroma_addr);
			break;
		case B2R2_FMT_TYPE_PLANAR:
			b2r2_get_cb_cr_addr(buf->addr, buf->pitch, buf->height,
				buf->fmt, &buf->chroma_addr,
				&buf->chroma_cr_addr);
			break;
		default:
			break;
		}

		memcpy(&buf->rect, rect, sizeof(buf->rect));
	}
}

/**
 * setup_tmp_buf() - configure a temporary buffer
 */
static int setup_tmp_buf(struct b2r2_control *cont,
		struct b2r2_node_split_buf *tmp,
		u32 max_size,
		enum b2r2_blt_fmt pref_fmt,
		u32 pref_width,
		u32 pref_height)
{
	int ret;

	enum b2r2_blt_fmt fmt;

	u32 width;
	u32 height;
	u32 pitch;
	u32 size;

	/* Determine what format we should use for the tmp buf */
	if (b2r2_is_rgb_fmt(pref_fmt)) {
		fmt = B2R2_BLT_FMT_32_BIT_ARGB8888;
	} else if (b2r2_is_bgr_fmt(pref_fmt)) {
		fmt = B2R2_BLT_FMT_32_BIT_ABGR8888;
	} else if (b2r2_is_yuv_fmt(pref_fmt)) {
		fmt = B2R2_BLT_FMT_32_BIT_AYUV8888;
	} else {
		/* Wait, what? */
		b2r2_log_warn(cont->dev, "%s: "
			"Cannot create tmp buf from this fmt (%d)\n",
			__func__, pref_fmt);
		ret = -EINVAL;
		goto error;
	}

	/* See if we can fit the entire preferred rectangle */
	width = pref_width;
	height = pref_height;
	pitch = b2r2_fmt_byte_pitch(fmt, width);
	size = pitch * height;

	if (size > max_size) {
		/* We need to limit the size, so we choose a different width */
		width = min(width, (u32) B2R2_RESCALE_MAX_WIDTH);
		pitch = b2r2_fmt_byte_pitch(fmt, width);
		height = min(height, max_size / pitch);
		size = pitch * height;
	}

	/* We should at least have enough room for one scanline */
	if (height == 0) {
		b2r2_log_warn(cont->dev, "%s: Not enough tmp mem!\n",
			__func__);
		ret = -ENOMEM;
		goto error;
	}

	memset(tmp, 0, sizeof(*tmp));

	tmp->fmt = fmt;
	tmp->type = B2R2_FMT_TYPE_RASTER;
	tmp->height = height;
	tmp->width = width;
	tmp->pitch = pitch;

	tmp->rect.width = width;
	tmp->rect.height = tmp->height;
	tmp->alpha_range = B2R2_TY_ALPHA_RANGE_255;

	return 0;
error:
	b2r2_log_warn(cont->dev, "%s: Exit...\n", __func__);
	return ret;

}

/**
 * is_transform() - returns whether the given request is a transform operation
 */
static bool is_transform(const struct b2r2_blt_request *req)
{
	return (req->user_req.transform != B2R2_BLT_TRANSFORM_NONE) ||
			(req->user_req.src_rect.width !=
				req->user_req.dst_rect.width) ||
			(req->user_req.src_rect.height !=
				req->user_req.dst_rect.height);
}

/**
 * rescale() - rescales the given dimension
 *
 * Returns the rescaled dimension in 22.10 fixed point format.
 */
static s32 rescale(struct b2r2_control *cont, s32 dim, u16 sf)
{
	b2r2_log_info(cont->dev, "%s\n", __func__);

	if (sf == 0) {
		b2r2_log_err(cont->dev, "%s: Scale factor is 0!\n", __func__);
		BUG_ON(1);
	}

	/*
	 * This is normally not safe to do, since it drastically decreases the
	 * precision of the integer part of the dimension. But since the B2R2
	 * hardware only has 12-bit registers for these values, we are safe.
	 */
	return (dim << 20) / sf;
}

/**
 * inv_rescale() - does an inverted rescale of the given dimension
 *
 * Returns the rescaled dimension in 22.10 fixed point format.
 */
static s32 inv_rescale(s32 dim, u16 sf)
{
	if (sf == 0)
		return dim;

	return dim * sf;
}

/**
 * set_target() - sets the target registers of the given node
 */
static void set_target(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf)
{
	s32 l;
	s32 r;
	s32 t;
	s32 b;

	if (buf->tmp_buf_index)
		node->dst_tmp_index = buf->tmp_buf_index;

	node->node.GROUP1.B2R2_TBA = addr;
	node->node.GROUP1.B2R2_TTY = buf->pitch | b2r2_to_native_fmt(buf->fmt) |
			buf->alpha_range | buf->chroma_selection | buf->hso |
			buf->vso | buf->dither | buf->plane_selection;

	if (buf->fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
			buf->fmt == B2R2_BLT_FMT_32_BIT_VUYA8888 ||
			buf->fmt == B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR)
		node->node.GROUP1.B2R2_TTY |= B2R2_TY_ENDIAN_BIG_NOT_LITTLE;

	node->node.GROUP1.B2R2_TSZ =
			((buf->win.width & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((buf->win.height & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);
	node->node.GROUP1.B2R2_TXY =
			((buf->win.x & 0xffff) << B2R2_XY_X_SHIFT) |
			((buf->win.y & 0xffff) << B2R2_XY_Y_SHIFT);

	/* Check if the rectangle is outside the buffer */
	if (buf->vso == B2R2_TY_VSO_BOTTOM_TO_TOP)
		t = buf->win.y - (buf->win.height - 1);
	else
		t = buf->win.y;

	if (buf->hso == B2R2_TY_HSO_RIGHT_TO_LEFT)
		l = buf->win.x - (buf->win.width - 1);
	else
		l = buf->win.x;

	r = l + buf->win.width;
	b = t + buf->win.height;

	/* Clip to the destination buffer to prevent memory overwrites */
	if ((l < 0) || (r > buf->width) || (t < 0) || (b > buf->height)) {
		/* The clip rectangle is including the borders */
		l = max(l, 0);
		r = min(r, (s32) buf->width) - 1;
		t = max(t, 0);
		b = min(b, (s32) buf->height) - 1;

		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_CLIP_WINDOW;
		node->node.GROUP0.B2R2_INS |= B2R2_INS_RECT_CLIP_ENABLED;
		node->node.GROUP6.B2R2_CWO =
			((l & 0x7FFF) << B2R2_CWS_X_SHIFT) |
			((t & 0x7FFF) << B2R2_CWS_Y_SHIFT);
		node->node.GROUP6.B2R2_CWS =
			((r & 0x7FFF) << B2R2_CWO_X_SHIFT) |
			((b & 0x7FFF) << B2R2_CWO_Y_SHIFT);
	}

}

/**
 * set_src() - configures the given source register with the given values
 */
static void set_src(struct b2r2_src_config *src, u32 addr,
		struct b2r2_node_split_buf *buf)
{
	src->B2R2_SBA = addr;
	src->B2R2_STY = buf->pitch | b2r2_to_native_fmt(buf->fmt) |
			buf->alpha_range | buf->hso | buf->vso;

	if (buf->fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
			buf->fmt == B2R2_BLT_FMT_32_BIT_VUYA8888 ||
			buf->fmt == B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR)
		src->B2R2_STY |= B2R2_TY_ENDIAN_BIG_NOT_LITTLE;

	src->B2R2_SSZ = ((buf->win.width & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((buf->win.height & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);
	src->B2R2_SXY = ((buf->win.x & 0xffff) << B2R2_XY_X_SHIFT) |
			((buf->win.y & 0xffff) << B2R2_XY_Y_SHIFT);

}

/**
 * set_src_1() - sets the source 1 registers of the given node
 */
static void set_src_1(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf)
{
	if (buf->tmp_buf_index)
		node->src_tmp_index = buf->tmp_buf_index;

	node->src_index = 1;

	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_1;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_1_FETCH_FROM_MEM;

	node->node.GROUP3.B2R2_SBA = addr;
	node->node.GROUP3.B2R2_STY = buf->pitch | b2r2_to_native_fmt(buf->fmt) |
			buf->alpha_range | buf->hso | buf->vso;

	if (buf->fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
			buf->fmt == B2R2_BLT_FMT_32_BIT_VUYA8888 ||
			buf->fmt == B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			buf->fmt == B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR)
		node->node.GROUP3.B2R2_STY |= B2R2_TY_ENDIAN_BIG_NOT_LITTLE;

	node->node.GROUP3.B2R2_SXY =
			((buf->win.x & 0xffff) << B2R2_XY_X_SHIFT) |
			((buf->win.y & 0xffff) << B2R2_XY_Y_SHIFT);

	/* NOTE: Source 1 has no size register */
}

/**
 * set_src_2() - sets the source 2 registers of the given node
 */
static void set_src_2(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf)
{
	if (buf->tmp_buf_index)
		node->src_tmp_index = buf->tmp_buf_index;

	node->src_index = 2;

	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_2_FETCH_FROM_MEM;

	set_src(&node->node.GROUP4, addr, buf);
}

/**
 * set_src_3() - sets the source 3 registers of the given node
 */
static void set_src_3(struct b2r2_node *node, u32 addr,
		struct b2r2_node_split_buf *buf)
{
	if (buf->tmp_buf_index)
		node->src_tmp_index = buf->tmp_buf_index;

	node->src_index = 3;

	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_3;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_3_FETCH_FROM_MEM;

	set_src(&node->node.GROUP5, addr, buf);
}

/**
 * set_ivmx() - configures the iVMx registers with the given values
 */
static void set_ivmx(struct b2r2_node *node, const u32 *vmx_values)
{
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_IVMX;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_IVMX_ENABLED;

	node->node.GROUP15.B2R2_VMX0 = vmx_values[0];
	node->node.GROUP15.B2R2_VMX1 = vmx_values[1];
	node->node.GROUP15.B2R2_VMX2 = vmx_values[2];
	node->node.GROUP15.B2R2_VMX3 = vmx_values[3];
}

/**
 * set_ovmx() - configures the oVMx registers with the given values
 */
static void set_ovmx(struct b2r2_node *node, const u32 *vmx_values)
{
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_OVMX;
	node->node.GROUP0.B2R2_INS |= B2R2_INS_OVMX_ENABLED;

	node->node.GROUP16.B2R2_VMX0 = vmx_values[0];
	node->node.GROUP16.B2R2_VMX1 = vmx_values[1];
	node->node.GROUP16.B2R2_VMX2 = vmx_values[2];
	node->node.GROUP16.B2R2_VMX3 = vmx_values[3];
}

/**
 * reset_nodes() - clears the node list
 */
static void reset_nodes(struct b2r2_node *node)
{
	while (node != NULL) {
		memset(&node->node, 0, sizeof(node->node));

		node->src_tmp_index = 0;
		node->dst_tmp_index = 0;

		/* TODO: Implement support for short linked lists */
		node->node.GROUP0.B2R2_CIC = 0x7ffff;

		if (node->next != NULL)
			node->node.GROUP0.B2R2_NIP =
					node->next->physical_address;
		node = node->next;
	}
}

int b2r2_node_split_init(struct b2r2_control *cont)
{
	return 0;
}

void b2r2_node_split_exit(struct b2r2_control *cont)
{

}
