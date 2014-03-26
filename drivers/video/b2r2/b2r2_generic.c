/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 generic. Full coverage of user interface but
 * non optimized implementation. For Fallback purposes.
 *
 * Author: Maciej Socha <maciej.socha@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>

#include "b2r2_generic.h"
#include "b2r2_internal.h"
#include "b2r2_global.h"
#include "b2r2_debug.h"
#include "b2r2_filters.h"
#include "b2r2_hw_convert.h"
#include "b2r2_utils.h"

/*
 * Debug printing
 */
#define B2R2_GENERIC_DEBUG_AREAS 0
#define B2R2_GENERIC_DEBUG

#define B2R2_GENERIC_WORK_BUF_WIDTH 16
#define B2R2_GENERIC_WORK_BUF_HEIGHT 16
#define B2R2_GENERIC_WORK_BUF_PITCH (16 * 4)
#define B2R2_GENERIC_WORK_BUF_FMT B2R2_NATIVE_ARGB8888

/*
 * Private functions
 */

/**
 * reset_nodes() - clears the node list
 */
static void reset_nodes(struct b2r2_control *cont,
		struct b2r2_node *node)
{
	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	while (node != NULL) {
		memset(&(node->node), 0, sizeof(node->node));

		/* TODO: Implement support for short linked lists */
		node->node.GROUP0.B2R2_CIC = 0x7fffc;

		if (node->next == NULL)
			break;

		node->node.GROUP0.B2R2_NIP = node->next->physical_address;

		node = node->next;
	}
	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

/**
 * dump_nodes() - prints the node list
 */
static void dump_nodes(struct b2r2_control *cont,
		struct b2r2_node *first, bool dump_all)
{
	struct b2r2_node *node = first;
	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);
	do {
		b2r2_log_debug(cont->dev, "\nNODE START:\n=============\n");
		b2r2_log_debug(cont->dev, "B2R2_ACK: \t0x%.8x\n",
				node->node.GROUP0.B2R2_ACK);
		b2r2_log_debug(cont->dev, "B2R2_INS: \t0x%.8x\n",
				node->node.GROUP0.B2R2_INS);
		b2r2_log_debug(cont->dev, "B2R2_CIC: \t0x%.8x\n",
				node->node.GROUP0.B2R2_CIC);
		b2r2_log_debug(cont->dev, "B2R2_NIP: \t0x%.8x\n",
				node->node.GROUP0.B2R2_NIP);

		b2r2_log_debug(cont->dev, "B2R2_TSZ: \t0x%.8x\n",
				node->node.GROUP1.B2R2_TSZ);
		b2r2_log_debug(cont->dev, "B2R2_TXY: \t0x%.8x\n",
				node->node.GROUP1.B2R2_TXY);
		b2r2_log_debug(cont->dev, "B2R2_TTY: \t0x%.8x\n",
				node->node.GROUP1.B2R2_TTY);
		b2r2_log_debug(cont->dev, "B2R2_TBA: \t0x%.8x\n",
				node->node.GROUP1.B2R2_TBA);

		b2r2_log_debug(cont->dev, "B2R2_S2CF: \t0x%.8x\n",
				node->node.GROUP2.B2R2_S2CF);
		b2r2_log_debug(cont->dev, "B2R2_S1CF: \t0x%.8x\n",
				node->node.GROUP2.B2R2_S1CF);

		b2r2_log_debug(cont->dev, "B2R2_S1SZ: \t0x%.8x\n",
				node->node.GROUP3.B2R2_SSZ);
		b2r2_log_debug(cont->dev, "B2R2_S1XY: \t0x%.8x\n",
				node->node.GROUP3.B2R2_SXY);
		b2r2_log_debug(cont->dev, "B2R2_S1TY: \t0x%.8x\n",
				node->node.GROUP3.B2R2_STY);
		b2r2_log_debug(cont->dev, "B2R2_S1BA: \t0x%.8x\n",
				node->node.GROUP3.B2R2_SBA);

		b2r2_log_debug(cont->dev, "B2R2_S2SZ: \t0x%.8x\n",
				node->node.GROUP4.B2R2_SSZ);
		b2r2_log_debug(cont->dev, "B2R2_S2XY: \t0x%.8x\n",
				node->node.GROUP4.B2R2_SXY);
		b2r2_log_debug(cont->dev, "B2R2_S2TY: \t0x%.8x\n",
				node->node.GROUP4.B2R2_STY);
		b2r2_log_debug(cont->dev, "B2R2_S2BA: \t0x%.8x\n",
				node->node.GROUP4.B2R2_SBA);

		b2r2_log_debug(cont->dev, "B2R2_S3SZ: \t0x%.8x\n",
				node->node.GROUP5.B2R2_SSZ);
		b2r2_log_debug(cont->dev, "B2R2_S3XY: \t0x%.8x\n",
				node->node.GROUP5.B2R2_SXY);
		b2r2_log_debug(cont->dev, "B2R2_S3TY: \t0x%.8x\n",
				node->node.GROUP5.B2R2_STY);
		b2r2_log_debug(cont->dev, "B2R2_S3BA: \t0x%.8x\n",
				node->node.GROUP5.B2R2_SBA);

		b2r2_log_debug(cont->dev, "B2R2_CWS: \t0x%.8x\n",
				node->node.GROUP6.B2R2_CWS);
		b2r2_log_debug(cont->dev, "B2R2_CWO: \t0x%.8x\n",
				node->node.GROUP6.B2R2_CWO);

		b2r2_log_debug(cont->dev, "B2R2_FCTL: \t0x%.8x\n",
				node->node.GROUP8.B2R2_FCTL);
		b2r2_log_debug(cont->dev, "B2R2_RSF: \t0x%.8x\n",
				node->node.GROUP9.B2R2_RSF);
		b2r2_log_debug(cont->dev, "B2R2_RZI: \t0x%.8x\n",
				node->node.GROUP9.B2R2_RZI);
		b2r2_log_debug(cont->dev, "B2R2_HFP: \t0x%.8x\n",
				node->node.GROUP9.B2R2_HFP);
		b2r2_log_debug(cont->dev, "B2R2_VFP: \t0x%.8x\n",
				node->node.GROUP9.B2R2_VFP);
		b2r2_log_debug(cont->dev, "B2R2_LUMA_RSF: \t0x%.8x\n",
				node->node.GROUP10.B2R2_RSF);
		b2r2_log_debug(cont->dev, "B2R2_LUMA_RZI: \t0x%.8x\n",
				node->node.GROUP10.B2R2_RZI);
		b2r2_log_debug(cont->dev, "B2R2_LUMA_HFP: \t0x%.8x\n",
				node->node.GROUP10.B2R2_HFP);
		b2r2_log_debug(cont->dev, "B2R2_LUMA_VFP: \t0x%.8x\n",
				node->node.GROUP10.B2R2_VFP);


		b2r2_log_debug(cont->dev, "B2R2_IVMX0: \t0x%.8x\n",
				node->node.GROUP15.B2R2_VMX0);
		b2r2_log_debug(cont->dev, "B2R2_IVMX1: \t0x%.8x\n",
				node->node.GROUP15.B2R2_VMX1);
		b2r2_log_debug(cont->dev, "B2R2_IVMX2: \t0x%.8x\n",
				node->node.GROUP15.B2R2_VMX2);
		b2r2_log_debug(cont->dev, "B2R2_IVMX3: \t0x%.8x\n",
				node->node.GROUP15.B2R2_VMX3);
		b2r2_log_debug(cont->dev, "\n=============\nNODE END\n");

		node = node->next;
	} while (node != NULL && dump_all);

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

/**
 * to_native_fmt() - returns the native B2R2 format
 */
static inline enum b2r2_native_fmt to_native_fmt(struct b2r2_control *cont,
		enum b2r2_blt_fmt fmt)
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
		return B2R2_NATIVE_ARGB4444;
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		return B2R2_NATIVE_ARGB4444;
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
		return B2R2_NATIVE_ARGB1555;
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
		return B2R2_NATIVE_ARGB8565;
	case B2R2_BLT_FMT_24_BIT_RGB888:
		return B2R2_NATIVE_RGB888;
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_24_BIT_YUV888:
		return B2R2_NATIVE_YCBCR888;
	case B2R2_BLT_FMT_32_BIT_ABGR8888: /* Not actually supported by HW */
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
		return B2R2_NATIVE_ARGB8888;
	case B2R2_BLT_FMT_32_BIT_VUYA8888: /* fall through */
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
		return B2R2_NATIVE_AYCBCR8888;
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		return B2R2_NATIVE_YCBCR422R;
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		return B2R2_NATIVE_YCBCR42X_R2B;
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		return B2R2_NATIVE_YCBCR42X_MBN;
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
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
 * get_alpha_range() - returns the alpha range of the given format
 */
static inline enum b2r2_ty get_alpha_range(struct b2r2_control *cont,
		enum b2r2_blt_fmt fmt)
{
	switch (fmt) {
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_8_BIT_A8:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
		return B2R2_TY_ALPHA_RANGE_255; /* 0 - 255 */
		break;
	default:
		break;
	}

	return B2R2_TY_ALPHA_RANGE_128; /* 0 - 128 */
}

static s32 validate_buf(struct b2r2_control *cont,
		const struct b2r2_blt_img *image,
		const struct b2r2_resolved_buf *buf)
{
	u32 expect_buf_size;
	u32 pitch;
	u32 chroma_pitch;

	if (image->width <= 0 || image->height <= 0) {
		b2r2_log_warn(cont->dev, "%s: width=%d or height=%d negative"
				".\n", __func__, image->width, image->height);
		return -EINVAL;
	}

	if (image->pitch == 0)
		/* autodetect pitch based on format and width */
		pitch = b2r2_calc_pitch_from_width(cont->dev,
			image->width, image->fmt);
	else
		pitch = image->pitch;

	expect_buf_size = pitch * image->height;

	if (pitch == 0) {
		b2r2_log_warn(cont->dev, "%s: Unable to detect pitch. "
			"fmt=%#010x, width=%d\n",
			__func__,
			image->fmt, image->width);
		return -EINVAL;
	}

	/* format specific adjustments */
	chroma_pitch = b2r2_get_chroma_pitch(pitch, image->fmt);
	switch (image->fmt) {
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YV12:
		/*
		 * Use ceil(height/2) in case buffer height
		 * is not divisible by 2.
		 */
		expect_buf_size += chroma_pitch *
			((image->height + 1) >> 1) * 2;
		break;
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
		expect_buf_size += chroma_pitch * image->height * 2;
		break;
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
		/*
		 * include space occupied by U and V data.
		 * U and V interleaved, half resolution, which makes
		 * the UV pitch equal to luma pitch.
		 * Use ceil(height/2) in case buffer height
		 * is not divisible by 2.
		 */
		expect_buf_size += pitch * ((image->height + 1) >> 1);
		break;
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
		/*
		 * include space occupied by U and V data.
		 * U and V interleaved, half resolution, which makes
		 * the UV pitch equal to luma pitch.
		 */
		expect_buf_size += pitch * image->height;
		break;
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
		/* Height must be a multiple of 16 for macro-block format.*/
		if (image->height & 15) {
			b2r2_log_warn(cont->dev, "%s: Illegal height "
				"for fmt=%#010x height=%d\n", __func__,
				image->fmt, image->height);
			return -EINVAL;
		}
		expect_buf_size += pitch * (image->height >> 1);
		break;
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
		/* Height must be a multiple of 16 for macro-block format.*/
		if (image->height & 15) {
			b2r2_log_warn(cont->dev, "%s: Illegal height "
				"for fmt=%#010x height=%d\n", __func__,
				image->fmt, image->height);
			return -EINVAL;
		}
		expect_buf_size += pitch * image->height;
		break;
	default:
		break;
	}

	if (buf->file_len < expect_buf_size) {
		b2r2_log_warn(cont->dev, "%s: Invalid buffer size:\n"
			"fmt=%#010x w=%d h=%d buf.len=%d expect_buf_size=%d\n",
			__func__,
			image->fmt, image->width, image->height, buf->file_len,
			expect_buf_size);
		return -EINVAL;
	}

	if (image->buf.type == B2R2_BLT_PTR_VIRTUAL) {
		b2r2_log_warn(cont->dev, "%s: Virtual pointers not supported"
				" yet.\n", __func__);
		return -EINVAL;
	}
	return 0;
}

/*
 * Bit-expand the color from fmt to RGB888 with blue at LSB.
 * Copy MSBs into missing LSBs.
 */
static u32 to_RGB888(struct b2r2_control *cont, u32 color,
		const enum b2r2_blt_fmt fmt)
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


static void setup_fill_input_stage(const struct b2r2_blt_request *req,
					struct b2r2_node *node,
					struct b2r2_work_buf *out_buf)
{
	enum b2r2_native_fmt fill_fmt = 0;
	u32 src_color = req->user_req.src_color;
	const struct b2r2_blt_img *dst_img = &(req->user_req.dst_img);
	struct b2r2_control *cont = req->instance->control;
	bool fullrange = (req->user_req.flags &
		B2R2_BLT_FLAG_FULL_RANGE_YUV) != 0;

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	/* Determine format in src_color */
	switch (dst_img->fmt) {
	/* ARGB formats */
	case B2R2_BLT_FMT_16_BIT_ARGB4444:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
	case B2R2_BLT_FMT_16_BIT_ARGB1555:
	case B2R2_BLT_FMT_16_BIT_RGB565:
	case B2R2_BLT_FMT_24_BIT_RGB888:
	case B2R2_BLT_FMT_32_BIT_ARGB8888:
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_24_BIT_ARGB8565:
	case B2R2_BLT_FMT_1_BIT_A1:
	case B2R2_BLT_FMT_8_BIT_A8:
		if ((req->user_req.flags & B2R2_BLT_FLAG_SOURCE_FILL) != 0) {
			fill_fmt = B2R2_NATIVE_ARGB8888;
		} else {
			/* SOURCE_FILL_RAW */
			fill_fmt = to_native_fmt(cont, dst_img->fmt);
			if (dst_img->fmt == B2R2_BLT_FMT_32_BIT_ABGR8888 ||
				dst_img->fmt == B2R2_BLT_FMT_16_BIT_ABGR4444) {
				/*
				 * Color is read from a register,
				 * where it is stored in ABGR format.
				 * Set up IVMX.
				 */
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_BGR);
			}
		}
		break;
	/* YUV formats */
	case B2R2_BLT_FMT_CB_Y_CR_Y:
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12:
		if ((req->user_req.flags & B2R2_BLT_FLAG_SOURCE_FILL) != 0) {
			fill_fmt = B2R2_NATIVE_AYCBCR8888;
			/*
			 * Set up IVMX
			 * The destination format is in fact YUV,
			 * but the input stage stores the data in
			 * an intermediate buffer which is RGB.
			 * Hence the conversion from YUV to RGB.
			 * Format of the supplied src_color is
			 * B2R2_BLT_FMT_32_BIT_AYUV8888.
			 */
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_BLT_YUV888_FULL_TO_RGB);
			else
				b2r2_setup_ivmx(node, B2R2_CC_BLT_YUV888_TO_RGB);
		} else {
			/* SOURCE_FILL_RAW */
			if (b2r2_is_ycbcrp_fmt(dst_img->fmt) ||
					b2r2_is_ycbcrsp_fmt(dst_img->fmt) ||
					b2r2_is_mb_fmt(dst_img->fmt))
				/*
				 * SOURCE_FILL_RAW cannot be supported
				 * with multi-buffer formats.
				 * Force a legal format to prevent B2R2
				 * from misbehaving.
				 */
				fill_fmt = B2R2_NATIVE_AYCBCR8888;
			else
				fill_fmt = to_native_fmt(cont, dst_img->fmt);

			switch (dst_img->fmt) {
			case B2R2_BLT_FMT_24_BIT_YUV888:
			case B2R2_BLT_FMT_32_BIT_AYUV8888:
			case B2R2_BLT_FMT_24_BIT_VUY888:
			case B2R2_BLT_FMT_32_BIT_VUYA8888:
				if (fullrange)
					b2r2_setup_ivmx(node,
						B2R2_CC_BLT_YUV888_FULL_TO_RGB);
				else
					b2r2_setup_ivmx(node,
						B2R2_CC_BLT_YUV888_TO_RGB);
				/*
				 * Re-arrange the color components from
				 * VUY(A) to (A)YUV
				 */
				if (dst_img->fmt ==
					B2R2_BLT_FMT_24_BIT_VUY888) {
					u32 Y = src_color & 0xff;
					u32 U = src_color & 0xff00;
					u32 V = src_color & 0xff0000;
					src_color = (Y << 16) | U | (V >> 16);
				} else if (dst_img->fmt ==
						B2R2_BLT_FMT_32_BIT_VUYA8888) {
					u32 A = src_color & 0xff;
					u32 Y = src_color & 0xff00;
					u32 U = src_color & 0xff0000;
					u32 V = src_color & 0xff000000;
					src_color = (A << 24) |
							(Y << 8) |
							(U >> 8) |
							(V >> 24);
				}
				break;
			default:
				/*
				 * Set up IVMX
				 * The destination format is in fact YUV,
				 * but the input stage stores the data in
				 * an intermediate buffer which is RGB.
				 * Hence the conversion from YUV to RGB.
				 */
				if (fullrange)
					b2r2_setup_ivmx(node,
						B2R2_CC_YUV_FULL_TO_RGB);
				else
					b2r2_setup_ivmx(node, B2R2_CC_YUV_TO_RGB);
				break;
			}
		}
		break;
	default:
		src_color = 0;
		fill_fmt = B2R2_NATIVE_ARGB8888;
		break;
	}

	node->node.GROUP1.B2R2_TBA = out_buf->phys_addr;
	node->node.GROUP1.B2R2_TTY =
		(B2R2_GENERIC_WORK_BUF_PITCH << B2R2_TY_BITMAP_PITCH_SHIFT) |
		B2R2_GENERIC_WORK_BUF_FMT |
		B2R2_TY_ALPHA_RANGE_255 |
		B2R2_TY_HSO_LEFT_TO_RIGHT |
		B2R2_TY_VSO_TOP_TO_BOTTOM;
	/* Set color fill on SRC2 channel */
	node->node.GROUP4.B2R2_SBA = 0;
	node->node.GROUP4.B2R2_STY =
		(0 << B2R2_TY_BITMAP_PITCH_SHIFT) |
		fill_fmt |
		get_alpha_range(cont, dst_img->fmt) |
		B2R2_TY_HSO_LEFT_TO_RIGHT |
		B2R2_TY_VSO_TOP_TO_BOTTOM;

	node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_2_COLOR_FILL_REGISTER;
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_COLOR_FILL;
	node->node.GROUP2.B2R2_S2CF = src_color;

	node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;
	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

static void setup_input_stage(const struct b2r2_blt_request *req,
			      struct b2r2_node *node,
			      struct b2r2_work_buf *out_buf)
{
	/* Horizontal and vertical scaling factors in 6.10 fixed point format */
	s32 h_scf = 1 << 10;
	s32 v_scf = 1 << 10;
	const struct b2r2_blt_rect *src_rect = &(req->user_req.src_rect);
	const struct b2r2_blt_rect *dst_rect = &(req->user_req.dst_rect);
	const struct b2r2_blt_img *src_img = &(req->user_req.src_img);
	u32 src_pitch = 0;
	/* horizontal and vertical scan order for out_buf */
	enum b2r2_ty dst_hso = B2R2_TY_HSO_LEFT_TO_RIGHT;
	enum b2r2_ty dst_vso = B2R2_TY_VSO_TOP_TO_BOTTOM;
	u32 endianness = 0;
	u32 fctl = 0;
	u32 rsf = 0;
	u32 rzi = 0;
	struct b2r2_filter_spec *hf;
	struct b2r2_filter_spec *vf;
	bool use_h_filter = false;
	bool use_v_filter = false;
	const bool yuv_planar = b2r2_is_ycbcrp_fmt(src_img->fmt);
	const bool yuv_semi_planar = b2r2_is_ycbcrsp_fmt(src_img->fmt) ||
		b2r2_is_mb_fmt(src_img->fmt);
	struct b2r2_control *cont = req->instance->control;
	bool fullrange = (req->user_req.flags &
		B2R2_BLT_FLAG_FULL_RANGE_YUV) != 0;

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	if (((B2R2_BLT_FLAG_SOURCE_FILL | B2R2_BLT_FLAG_SOURCE_FILL_RAW) &
			req->user_req.flags) != 0) {
		setup_fill_input_stage(req, node, out_buf);
		b2r2_log_info(cont->dev, "%s DONE\n", __func__);
		return;
	}

	if (src_img->pitch == 0)
		/* Determine pitch based on format and width of the image. */
		src_pitch = b2r2_calc_pitch_from_width(cont->dev,
				src_img->width, src_img->fmt);
	else
		src_pitch = src_img->pitch;

	b2r2_log_info(cont->dev, "%s transform=%#010x\n",
			__func__, req->user_req.transform);
	if (req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		h_scf = (src_rect->width << 10) / dst_rect->height;
		v_scf = (src_rect->height << 10) / dst_rect->width;
	} else {
		h_scf = (src_rect->width << 10) / dst_rect->width;
		v_scf = (src_rect->height << 10) / dst_rect->height;
	}

	hf = b2r2_filter_find(h_scf);
	vf = b2r2_filter_find(v_scf);

	use_h_filter = h_scf != (1 << 10);
	use_v_filter = v_scf != (1 << 10);

	/* B2R2_BLT_FLAG_BLUR overrides any scaling filter. */
	if (req->user_req.flags & B2R2_BLT_FLAG_BLUR) {
		use_h_filter = true;
		use_v_filter = true;
		hf = b2r2_filter_blur();
		vf = b2r2_filter_blur();
	}

	/* Configure horizontal rescale */
	if (h_scf != (1 << 10))
		b2r2_log_info(cont->dev, "%s: Scaling horizontally by 0x%.8x"
			"\ns(%d, %d)->d(%d, %d)\n", __func__,
			h_scf, src_rect->width, src_rect->height,
			dst_rect->width, dst_rect->height);

	fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER;
	rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
	rsf |= h_scf << B2R2_RSF_HSRC_INC_SHIFT;
	rzi |= B2R2_RZI_DEFAULT_HNB_REPEAT;

	/* Configure vertical rescale */
	if (v_scf != (1 << 10))
		b2r2_log_info(cont->dev, "%s: Scaling vertically by 0x%.8x"
			"\ns(%d, %d)->d(%d, %d)\n", __func__,
			v_scf, src_rect->width, src_rect->height,
			dst_rect->width, dst_rect->height);

	fctl |= B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;
	rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
	rsf |= v_scf << B2R2_RSF_VSRC_INC_SHIFT;
	rzi |= 2 << B2R2_RZI_VNB_REPEAT_SHIFT;

	node->node.GROUP0.B2R2_INS |= B2R2_INS_RESCALE2D_ENABLED;
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_RESIZE_CHROMA;

	/* Adjustments that depend on the source format */
	switch (src_img->fmt) {
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_BGR);
		break;
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_YUV_FULL_TO_RGB);
		else
			b2r2_setup_ivmx(node, B2R2_CC_YUV_TO_RGB);
		break;
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		/*
		 * Set up IVMX.
		 * For B2R2_BLT_FMT_32_BIT_YUV888 and
		 * B2R2_BLT_FMT_32_BIT_AYUV8888
		 * the color components are laid out in memory as V, U, Y, (A)
		 * with V at the first byte (due to little endian addressing).
		 * B2R2 expects them to be as U, Y, V, (A)
		 * with U at the first byte.
		 */
		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_BLT_YUV888_FULL_TO_RGB);
		else
			b2r2_setup_ivmx(node, B2R2_CC_BLT_YUV888_TO_RGB);

		/*
		 * Re-arrange color components from VUY(A) to (A)YUV
		 * for input VMX to work on them further.
		 */
		if (src_img->fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				src_img->fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			endianness = B2R2_TY_ENDIAN_BIG_NOT_LITTLE;
		break;
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12: {
		/*
		 * Luma handled in the same way
		 * for all YUV multi-buffer formats.
		 * Set luma rescale registers.
		 */
		u32 rsf_luma = 0;
		u32 rzi_luma = 0;

		node->node.GROUP0.B2R2_INS |= B2R2_INS_RESCALE2D_ENABLED;
		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_RESIZE_LUMA;

		if (src_img->fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			src_img->fmt ==
				B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR) {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_YVU_FULL_TO_RGB);
			else
				b2r2_setup_ivmx(node, B2R2_CC_YVU_TO_RGB);
		} else {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_YUV_FULL_TO_RGB);
			else
				b2r2_setup_ivmx(node, B2R2_CC_YUV_TO_RGB);
		}

		fctl |= B2R2_FCTL_LUMA_HF2D_MODE_ENABLE_RESIZER |
			B2R2_FCTL_LUMA_VF2D_MODE_ENABLE_RESIZER;

		if (use_h_filter && hf) {
			fctl |= B2R2_FCTL_LUMA_HF2D_MODE_ENABLE_FILTER;
			node->node.GROUP10.B2R2_HFP = hf->h_coeffs_phys_addr;
		}

		if (use_v_filter && vf) {
			fctl |= B2R2_FCTL_LUMA_VF2D_MODE_ENABLE_FILTER;
			node->node.GROUP10.B2R2_VFP = vf->v_coeffs_phys_addr;
		}

		rsf_luma |= h_scf << B2R2_RSF_HSRC_INC_SHIFT;
		rzi_luma |= B2R2_RZI_DEFAULT_HNB_REPEAT;

		rsf_luma |= v_scf << B2R2_RSF_VSRC_INC_SHIFT;
		rzi_luma |= 2 << B2R2_RZI_VNB_REPEAT_SHIFT;

		node->node.GROUP10.B2R2_RSF = rsf_luma;
		node->node.GROUP10.B2R2_RZI = rzi_luma;

		switch (src_img->fmt) {
		case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
		case B2R2_BLT_FMT_YV12:
			/*
			 * Chrominance is always half the luminance size
			 * so chrominance resizer is always active.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (h_scf >> 1) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (v_scf >> 1) << B2R2_RSF_VSRC_INC_SHIFT;
			/* Select suitable filter for chroma */
			hf = b2r2_filter_find(h_scf >> 1);
			vf = b2r2_filter_find(v_scf >> 1);
			use_h_filter = true;
			use_v_filter = true;
			break;
		case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
			/*
			 * Chrominance is always half the luminance size
			 * only in horizontal direction.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (h_scf >> 1) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= v_scf << B2R2_RSF_VSRC_INC_SHIFT;
			/* Select suitable filter for chroma */
			hf = b2r2_filter_find(h_scf >> 1);
			use_h_filter = true;
			break;
		case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
			/* Chrominance is the same size as luminance.*/
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= h_scf << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= v_scf << B2R2_RSF_VSRC_INC_SHIFT;
			/* Select suitable filter for chroma */
			hf = b2r2_filter_find(h_scf);
			vf = b2r2_filter_find(v_scf);
			use_h_filter = true;
			use_v_filter = true;
			break;
		default:
			break;
		}
		break;
	}
	default:
		break;
	}

	/*
	 * Set the filter control and rescale registers.
	 * GROUP9 registers are used for all single-buffer formats
	 * or for chroma in case of multi-buffer YUV formats.
	 * h/v_filter is now appropriately selected for chroma scaling,
	 * be it YUV multi-buffer, or single-buffer raster format.
	 * B2R2_BLT_FLAG_BLUR overrides any scaling filter.
	 */
	if (req->user_req.flags & B2R2_BLT_FLAG_BLUR) {
		use_h_filter = true;
		use_v_filter = true;
		hf = b2r2_filter_blur();
		vf = b2r2_filter_blur();
	}

	if (use_h_filter && hf) {
		fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_COLOR_CHANNEL_FILTER;
		node->node.GROUP9.B2R2_HFP = hf->h_coeffs_phys_addr;
	}

	if (use_v_filter && vf) {
		fctl |= B2R2_FCTL_VF2D_MODE_ENABLE_COLOR_CHANNEL_FILTER;
		node->node.GROUP9.B2R2_VFP = vf->v_coeffs_phys_addr;
	}

	node->node.GROUP8.B2R2_FCTL |= fctl;
	node->node.GROUP9.B2R2_RSF |= rsf;
	node->node.GROUP9.B2R2_RZI |= rzi;
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_FILTER_CONTROL;

	/*
	 * Flip transform is done before potential rotation.
	 * This can be achieved with appropriate scan order.
	 * Transform stage will only do rotation.
	 */
	if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_H)
		dst_hso = B2R2_TY_HSO_RIGHT_TO_LEFT;

	if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_V)
		dst_vso = B2R2_TY_VSO_BOTTOM_TO_TOP;

	/* Set target buffer */
	node->node.GROUP1.B2R2_TBA = out_buf->phys_addr;
	node->node.GROUP1.B2R2_TTY =
		(B2R2_GENERIC_WORK_BUF_PITCH << B2R2_TY_BITMAP_PITCH_SHIFT) |
		B2R2_GENERIC_WORK_BUF_FMT |
		B2R2_TY_ALPHA_RANGE_255 |
		dst_hso | dst_vso;

	if (yuv_planar) {
		/*
		 * Set up chrominance buffers on source 1 and 2,
		 * luminance on source 3.
		 * src_pitch and physical_address apply to luminance,
		 * corresponding chrominance values have to be derived.
		 */
		u32 cb_addr = 0;
		u32 cr_addr = 0;
		enum b2r2_native_fmt src_fmt =
			to_native_fmt(cont, src_img->fmt);
		u32 chroma_pitch =
			b2r2_get_chroma_pitch(src_pitch, src_img->fmt);

		b2r2_get_cb_cr_addr(req->src_resolved.physical_address,
			src_pitch, src_img->height, src_img->fmt,
			&cb_addr, &cr_addr);

		node->node.GROUP3.B2R2_SBA = cr_addr;
		node->node.GROUP3.B2R2_STY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			src_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP4.B2R2_SBA = cb_addr;
		node->node.GROUP4.B2R2_STY = node->node.GROUP3.B2R2_STY;

		node->node.GROUP5.B2R2_SBA = req->src_resolved.physical_address;
		node->node.GROUP5.B2R2_STY =
			(src_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			src_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_1_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_3_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_SOURCE_1 |
			B2R2_CIC_SOURCE_2 |
			B2R2_CIC_SOURCE_3;
	} else if (yuv_semi_planar) {
		/*
		 * Set up chrominance buffer on source 2, luminance on source 3.
		 * src_pitch and physical_address apply to luminance,
		 * corresponding chrominance values have to be derived.
		 * U and V are interleaved at half the luminance resolution,
		 * which makes the pitch of the UV plane equal
		 * to luminance pitch.
		 */
		u32 chroma_addr = req->src_resolved.physical_address +
			src_pitch * src_img->height;
		u32 chroma_pitch = src_pitch;

		enum b2r2_native_fmt src_fmt =
			to_native_fmt(cont, src_img->fmt);

		node->node.GROUP4.B2R2_SBA = chroma_addr;
		node->node.GROUP4.B2R2_STY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			src_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP5.B2R2_SBA = req->src_resolved.physical_address;
		node->node.GROUP5.B2R2_STY =
			(src_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			src_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_3_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_SOURCE_2 | B2R2_CIC_SOURCE_3;
	} else {
		/* single buffer format */
		node->node.GROUP4.B2R2_SBA = req->src_resolved.physical_address;
		node->node.GROUP4.B2R2_STY =
			(src_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			to_native_fmt(cont, src_img->fmt) |
			get_alpha_range(cont, src_img->fmt) |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			endianness;

		node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2;
	}

	if ((req->user_req.flags &
			B2R2_BLT_FLAG_CLUT_COLOR_CORRECTION) != 0) {
		node->node.GROUP0.B2R2_INS |= B2R2_INS_CLUTOP_ENABLED;
		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_CLUT;
		node->node.GROUP7.B2R2_CCO = B2R2_CCO_CLUT_COLOR_CORRECTION |
			B2R2_CCO_CLUT_UPDATE;
		node->node.GROUP7.B2R2_CML = req->clut_phys_addr;
	}

	node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

static void setup_transform_stage(const struct b2r2_blt_request *req,
				  struct b2r2_node *node,
				  struct b2r2_work_buf *out_buf,
				  struct b2r2_work_buf *in_buf)
{
	/* vertical scan order for out_buf */
	enum b2r2_ty dst_vso = B2R2_TY_VSO_TOP_TO_BOTTOM;
	enum b2r2_blt_transform transform = req->user_req.transform;
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_control *cont = req->instance->control;
#endif

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	if (transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		/*
		 * Scan order must be flipped otherwise contents will
		 * be mirrored vertically. Leftmost column of in_buf
		 * would become top instead of bottom row of out_buf.
		 */
		dst_vso = B2R2_TY_VSO_BOTTOM_TO_TOP;
		node->node.GROUP0.B2R2_INS |= B2R2_INS_ROTATION_ENABLED;
	}

	/* Set target buffer */
	node->node.GROUP1.B2R2_TBA = out_buf->phys_addr;
	node->node.GROUP1.B2R2_TTY =
		(B2R2_GENERIC_WORK_BUF_PITCH << B2R2_TY_BITMAP_PITCH_SHIFT) |
		B2R2_GENERIC_WORK_BUF_FMT |
		B2R2_TY_ALPHA_RANGE_255 |
		B2R2_TY_HSO_LEFT_TO_RIGHT | dst_vso;

	/* Set source buffer on SRC2 channel */
	node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
	node->node.GROUP4.B2R2_STY =
		(B2R2_GENERIC_WORK_BUF_PITCH << B2R2_TY_BITMAP_PITCH_SHIFT) |
		B2R2_GENERIC_WORK_BUF_FMT |
		B2R2_TY_ALPHA_RANGE_255 |
		B2R2_TY_HSO_LEFT_TO_RIGHT |
		B2R2_TY_VSO_TOP_TO_BOTTOM;

	node->node.GROUP0.B2R2_INS |= B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2;
	node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

/*
static void setup_mask_stage(const struct b2r2_blt_request req,
			     struct b2r2_node *node,
			     struct b2r2_work_buf *out_buf,
			     struct b2r2_work_buf *in_buf);
*/

static void setup_dst_read_stage(const struct b2r2_blt_request *req,
				 struct b2r2_node *node,
				 struct b2r2_work_buf *out_buf)
{
	const struct b2r2_blt_img *dst_img = &(req->user_req.dst_img);
	u32 fctl = 0;
	u32 rsf = 0;
	u32 endianness = 0;
	const bool yuv_planar = b2r2_is_ycbcrp_fmt(dst_img->fmt);
	const bool yuv_semi_planar = b2r2_is_ycbcrsp_fmt(dst_img->fmt) ||
		b2r2_is_mb_fmt(dst_img->fmt);

	u32 dst_pitch = 0;
	struct b2r2_control *cont = req->instance->control;
	bool fullrange = (req->user_req.flags &
		B2R2_BLT_FLAG_FULL_RANGE_YUV) != 0;

	if (dst_img->pitch == 0)
		/* Determine pitch based on format and width of the image. */
		dst_pitch = b2r2_calc_pitch_from_width(cont->dev,
				dst_img->width, dst_img->fmt);
	else
		dst_pitch = dst_img->pitch;

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	/* Adjustments that depend on the destination format */
	switch (dst_img->fmt) {
	case B2R2_BLT_FMT_32_BIT_ABGR8888:
	case B2R2_BLT_FMT_16_BIT_ABGR4444:
		b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_BGR);
		break;
	case B2R2_BLT_FMT_CB_Y_CR_Y:
		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_YUV_FULL_TO_RGB);
		else
			b2r2_setup_ivmx(node, B2R2_CC_YUV_TO_RGB);
		break;
	case B2R2_BLT_FMT_24_BIT_YUV888:
	case B2R2_BLT_FMT_32_BIT_AYUV8888:
	case B2R2_BLT_FMT_24_BIT_VUY888:
	case B2R2_BLT_FMT_32_BIT_VUYA8888:
		/*
		 * Set up IVMX.
		 * For B2R2_BLT_FMT_32_BIT_YUV888 and
		 * B2R2_BLT_FMT_32_BIT_AYUV8888
		 * the color components are laid out in memory as V, U, Y, (A)
		 * with V at the first byte (due to little endian addressing).
		 * B2R2 expects them to be as U, Y, V, (A)
		 * with U at the first byte.
		 */
		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_BLT_YUV888_FULL_TO_RGB);
		else
			b2r2_setup_ivmx(node, B2R2_CC_BLT_YUV888_TO_RGB);

		/*
		 * Re-arrange color components from VUY(A) to (A)YUV
		 * for input VMX to work on them further.
		 */
		if (dst_img->fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_img->fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			endianness = B2R2_TY_ENDIAN_BIG_NOT_LITTLE;
		break;
	case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
	case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
	case B2R2_BLT_FMT_YV12: {
		if (dst_img->fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			dst_img->fmt ==
				B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR) {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_YVU_FULL_TO_RGB);
			else
				b2r2_setup_ivmx(node, B2R2_CC_YVU_TO_RGB);
		} else {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_YUV_FULL_TO_RGB);
			else
				b2r2_setup_ivmx(node, B2R2_CC_YUV_TO_RGB);
		}

		switch (dst_img->fmt) {
		case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
		case B2R2_BLT_FMT_YV12:
			/*
			 * Chrominance is always half the luminance size
			 * so chrominance resizer is always active.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (1 << 9) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (1 << 9) << B2R2_RSF_VSRC_INC_SHIFT;
			break;
		case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
			/*
			 * Chrominance is always half the luminance size
			 * only in horizontal direction.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (1 << 9) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (1 << 10) << B2R2_RSF_VSRC_INC_SHIFT;
			break;
		case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
			/* Chrominance is the same size as luminance.*/
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
				B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (1 << 10) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (1 << 10) << B2R2_RSF_VSRC_INC_SHIFT;
			break;
		default:
			break;
		}
		/* Set the filter control and rescale registers for chroma */
		node->node.GROUP8.B2R2_FCTL |= fctl;
		node->node.GROUP9.B2R2_RSF |= rsf;
		node->node.GROUP9.B2R2_RZI =
			B2R2_RZI_DEFAULT_HNB_REPEAT |
			(2 << B2R2_RZI_VNB_REPEAT_SHIFT);
		node->node.GROUP0.B2R2_INS |= B2R2_INS_RESCALE2D_ENABLED;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_FILTER_CONTROL | B2R2_CIC_RESIZE_CHROMA;
		break;
	}
	default:
		break;
	}

	/* Set target buffer */
	node->node.GROUP1.B2R2_TBA = out_buf->phys_addr;
	node->node.GROUP1.B2R2_TTY =
		(B2R2_GENERIC_WORK_BUF_PITCH << B2R2_TY_BITMAP_PITCH_SHIFT) |
		B2R2_GENERIC_WORK_BUF_FMT |
		B2R2_TY_ALPHA_RANGE_255 |
		B2R2_TY_HSO_LEFT_TO_RIGHT |
		B2R2_TY_VSO_TOP_TO_BOTTOM;

	if (yuv_planar) {
		/*
		 * Set up chrominance buffers on source 1 and 2,
		 * luminance on source 3.
		 * dst_pitch and physical_address apply to luminance,
		 * corresponding chrominance values have to be derived.
		 */
		u32 cb_addr = 0;
		u32 cr_addr = 0;
		u32 chroma_pitch =
			b2r2_get_chroma_pitch(dst_pitch, dst_img->fmt);
		enum b2r2_native_fmt dst_native_fmt =
				to_native_fmt(cont, dst_img->fmt);

		b2r2_get_cb_cr_addr(req->dst_resolved.physical_address,
			dst_pitch, dst_img->height, dst_img->fmt,
			&cb_addr, &cr_addr);

		node->node.GROUP3.B2R2_SBA = cr_addr;
		node->node.GROUP3.B2R2_STY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP4.B2R2_SBA = cb_addr;
		node->node.GROUP4.B2R2_STY = node->node.GROUP3.B2R2_STY;

		node->node.GROUP5.B2R2_SBA = req->dst_resolved.physical_address;
		node->node.GROUP5.B2R2_STY =
			(dst_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_1_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_3_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_SOURCE_1 |
			B2R2_CIC_SOURCE_2 |
			B2R2_CIC_SOURCE_3;
	} else if (yuv_semi_planar) {
		/*
		 * Set up chrominance buffer on source 2, luminance on source 3.
		 * dst_pitch and physical_address apply to luminance,
		 * corresponding chrominance values have to be derived.
		 * U and V are interleaved at half the luminance resolution,
		 * which makes the pitch of the UV plane equal
		 * to luminance pitch.
		 */
		u32 chroma_addr = req->dst_resolved.physical_address +
			dst_pitch * dst_img->height;
		u32 chroma_pitch = dst_pitch;

		enum b2r2_native_fmt dst_native_fmt =
				to_native_fmt(cont, dst_img->fmt);

		node->node.GROUP4.B2R2_SBA = chroma_addr;
		node->node.GROUP4.B2R2_STY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP5.B2R2_SBA = req->dst_resolved.physical_address;
		node->node.GROUP5.B2R2_STY =
			(dst_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_3_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_SOURCE_2 | B2R2_CIC_SOURCE_3;
	} else {
		/* single buffer format */
		node->node.GROUP4.B2R2_SBA = req->dst_resolved.physical_address;
		node->node.GROUP4.B2R2_STY =
			(dst_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			to_native_fmt(cont, dst_img->fmt) |
			get_alpha_range(cont, dst_img->fmt) |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			endianness;

		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2;
	}

	node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

static void setup_blend_stage(const struct b2r2_blt_request *req,
			      struct b2r2_node *node,
			      struct b2r2_work_buf *bg_buf,
			      struct b2r2_work_buf *fg_buf)
{
	u32 global_alpha = req->user_req.global_alpha;
#ifdef CONFIG_B2R2_DEBUG
	struct b2r2_control *cont = req->instance->control;
#endif

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	node->node.GROUP0.B2R2_ACK = 0;

	if (req->user_req.flags &
			(B2R2_BLT_FLAG_GLOBAL_ALPHA_BLEND |
			B2R2_BLT_FLAG_PER_PIXEL_ALPHA_BLEND)) {
		/* Some kind of blending needs to be done. */
		if (req->user_req.flags & B2R2_BLT_FLAG_SRC_IS_NOT_PREMULT)
			node->node.GROUP0.B2R2_ACK |=
				B2R2_ACK_MODE_BLEND_NOT_PREMULT;
		else
			node->node.GROUP0.B2R2_ACK |=
				B2R2_ACK_MODE_BLEND_PREMULT;

		/*
		 * global_alpha register accepts 0..128 range,
		 * global_alpha in the request is 0..255, remap needed.
		 */
		if (req->user_req.flags & B2R2_BLT_FLAG_GLOBAL_ALPHA_BLEND) {
			if (global_alpha == 255)
				global_alpha = 128;
			else
				global_alpha >>= 1;
		} else {
			/*
			 * Use solid global_alpha
			 * if global alpha blending is not set.
			 */
			global_alpha = 128;
		}

		node->node.GROUP0.B2R2_ACK |=
			global_alpha << (B2R2_ACK_GALPHA_ROPID_SHIFT);

		/* Set background on SRC1 channel */
		node->node.GROUP3.B2R2_SBA = bg_buf->phys_addr;
		node->node.GROUP3.B2R2_STY =
			(B2R2_GENERIC_WORK_BUF_PITCH <<
				B2R2_TY_BITMAP_PITCH_SHIFT) |
			B2R2_GENERIC_WORK_BUF_FMT |
			B2R2_TY_ALPHA_RANGE_255 |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		/* Set foreground on SRC2 channel */
		node->node.GROUP4.B2R2_SBA = fg_buf->phys_addr;
		node->node.GROUP4.B2R2_STY =
			(B2R2_GENERIC_WORK_BUF_PITCH <<
				B2R2_TY_BITMAP_PITCH_SHIFT) |
			B2R2_GENERIC_WORK_BUF_FMT |
			B2R2_TY_ALPHA_RANGE_255 |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		/* Set target buffer */
		node->node.GROUP1.B2R2_TBA = bg_buf->phys_addr;
		node->node.GROUP1.B2R2_TTY =
			(B2R2_GENERIC_WORK_BUF_PITCH <<
				B2R2_TY_BITMAP_PITCH_SHIFT) |
			B2R2_GENERIC_WORK_BUF_FMT |
			B2R2_TY_ALPHA_RANGE_255 |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_1_FETCH_FROM_MEM |
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_SOURCE_1 |
			B2R2_CIC_SOURCE_2;
	} else {
		/*
		 * No blending, foreground goes on SRC2. No global alpha.
		 * EMACSOC TODO: The blending stage should be skipped altogether
		 * if no blending is to be done. Probably could go directly from
		 * transform to writeback.
		 */
		node->node.GROUP0.B2R2_ACK |= B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM;
		node->node.GROUP0.B2R2_CIC |= B2R2_CIC_SOURCE_2;

		node->node.GROUP4.B2R2_SBA = fg_buf->phys_addr;
		node->node.GROUP4.B2R2_STY =
			(B2R2_GENERIC_WORK_BUF_PITCH <<
				B2R2_TY_BITMAP_PITCH_SHIFT) |
			B2R2_GENERIC_WORK_BUF_FMT |
			B2R2_TY_ALPHA_RANGE_255 |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;

		node->node.GROUP1.B2R2_TBA = bg_buf->phys_addr;
		node->node.GROUP1.B2R2_TTY =
			(B2R2_GENERIC_WORK_BUF_PITCH <<
				B2R2_TY_BITMAP_PITCH_SHIFT) |
			B2R2_GENERIC_WORK_BUF_FMT |
			B2R2_TY_ALPHA_RANGE_255 |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM;
	}

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

static void setup_writeback_stage(const struct b2r2_blt_request *req,
				  struct b2r2_node *node,
				  struct b2r2_work_buf *in_buf)
{
	const struct b2r2_blt_img *dst_img = &(req->user_req.dst_img);
	const enum b2r2_blt_fmt dst_fmt = dst_img->fmt;
	const bool yuv_planar_dst = b2r2_is_ycbcrp_fmt(dst_fmt);
	const bool yuv_semi_planar_dst = b2r2_is_ycbcrsp_fmt(dst_fmt) ||
		b2r2_is_mb_fmt(dst_fmt);
	const u32 group4_b2r2_sty =
		(B2R2_GENERIC_WORK_BUF_PITCH << B2R2_TY_BITMAP_PITCH_SHIFT) |
		B2R2_GENERIC_WORK_BUF_FMT |
		B2R2_TY_ALPHA_RANGE_255 |
		B2R2_TY_HSO_LEFT_TO_RIGHT |
		B2R2_TY_VSO_TOP_TO_BOTTOM;

	u32 dst_dither = 0;
	u32 dst_pitch = 0;
	u32 endianness = 0;

	struct b2r2_control *cont = req->instance->control;
	bool fullrange = (req->user_req.flags &
		B2R2_BLT_FLAG_FULL_RANGE_YUV) != 0;

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	if (dst_img->pitch == 0)
		dst_pitch = b2r2_calc_pitch_from_width(cont->dev,
			dst_img->width, dst_img->fmt);
	else
		dst_pitch = dst_img->pitch;

	if ((req->user_req.flags & B2R2_BLT_FLAG_DITHER) != 0)
		dst_dither = B2R2_TTY_RGB_ROUND_DITHER;

	/* Set target buffer(s) */
	if (yuv_planar_dst) {
		/*
		 * three nodes required to write the output.
		 * Luma, blue chroma and red chroma.
		 */
		u32 fctl = 0;
		u32 rsf = 0;
		const u32 group0_b2r2_ins =
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_RECT_CLIP_ENABLED;
		const u32 group0_b2r2_cic =
			B2R2_CIC_SOURCE_2 |
			B2R2_CIC_CLIP_WINDOW;

		u32 cb_addr = 0;
		u32 cr_addr = 0;
		u32 chroma_pitch = b2r2_get_chroma_pitch(dst_pitch, dst_fmt);

		enum b2r2_native_fmt dst_native_fmt =
				to_native_fmt(cont, dst_img->fmt);
		enum b2r2_ty alpha_range = get_alpha_range(cont, dst_img->fmt);

		b2r2_get_cb_cr_addr(req->dst_resolved.physical_address,
			dst_pitch, dst_img->height, dst_fmt,
			&cb_addr, &cr_addr);

		switch (dst_fmt) {
		case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
			/*
			 * Chrominance is always half the luminance size
			 * so chrominance resizer is always active.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
					B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (2 << 10) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (2 << 10) << B2R2_RSF_VSRC_INC_SHIFT;
			break;
		case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
			/*
			 * YUV422 or YVU422
			 * Chrominance is always half the luminance size
			 * only in horizontal direction.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (2 << 10) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (1 << 10) << B2R2_RSF_VSRC_INC_SHIFT;
			break;
		case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
			/*
			 * No scaling required since
			 * chrominance is not subsampled.
			 */
		default:
			break;
		}

		/* Luma (Y-component) */
		node->node.GROUP1.B2R2_TBA = req->dst_resolved.physical_address;
		node->node.GROUP1.B2R2_TTY =
			(dst_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt | alpha_range |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			dst_dither;

		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV_FULL);
		else
			b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV);

		 /* bypass ALU, no blending here. Handled in its own stage. */
		node->node.GROUP0.B2R2_ACK = B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS = group0_b2r2_ins;
		node->node.GROUP0.B2R2_CIC |= group0_b2r2_cic;

		/* Set source buffer on SRC2 channel */
		node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
		node->node.GROUP4.B2R2_STY = group4_b2r2_sty;

		/* Blue chroma (U-component)*/
		node = node->next;
		node->node.GROUP1.B2R2_TBA = cb_addr;
		node->node.GROUP1.B2R2_TTY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt | alpha_range |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			dst_dither |
			B2R2_TTY_CHROMA_NOT_LUMA;

		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV_FULL);
		else
			b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV);

		node->node.GROUP0.B2R2_ACK = B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS = group0_b2r2_ins;
		node->node.GROUP0.B2R2_CIC |= group0_b2r2_cic;
		if (dst_fmt != B2R2_BLT_FMT_YUV444_PACKED_PLANAR) {
			node->node.GROUP0.B2R2_INS |=
				B2R2_INS_RESCALE2D_ENABLED;
			node->node.GROUP0.B2R2_CIC |=
				B2R2_CIC_FILTER_CONTROL |
				B2R2_CIC_RESIZE_CHROMA;
			/* Set the filter control and rescale registers */
			node->node.GROUP8.B2R2_FCTL = fctl;
			node->node.GROUP9.B2R2_RSF = rsf;
			node->node.GROUP9.B2R2_RZI =
				B2R2_RZI_DEFAULT_HNB_REPEAT |
				(2 << B2R2_RZI_VNB_REPEAT_SHIFT);
		}

		node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
		node->node.GROUP4.B2R2_STY = group4_b2r2_sty;

		/*
		 * Red chroma (V-component)
		 * The flag B2R2_TTY_CB_NOT_CR actually works
		 * the other way around, i.e. as if it was
		 * CR_NOT_CB.
		 */
		node = node->next;
		node->node.GROUP1.B2R2_TBA = cr_addr;
		node->node.GROUP1.B2R2_TTY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt | alpha_range |
			B2R2_TTY_CB_NOT_CR |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			dst_dither |
			B2R2_TTY_CHROMA_NOT_LUMA;

		if (fullrange)
			b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV_FULL);
		else
			b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV);

		node->node.GROUP0.B2R2_ACK = B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS = group0_b2r2_ins;
		node->node.GROUP0.B2R2_CIC |= group0_b2r2_cic;
		if (dst_fmt != B2R2_BLT_FMT_YUV444_PACKED_PLANAR) {
			node->node.GROUP0.B2R2_INS |=
				B2R2_INS_RESCALE2D_ENABLED;
			node->node.GROUP0.B2R2_CIC |=
				B2R2_CIC_FILTER_CONTROL |
				B2R2_CIC_RESIZE_CHROMA;
			/* Set the filter control and rescale registers */
			node->node.GROUP8.B2R2_FCTL = fctl;
			node->node.GROUP9.B2R2_RSF = rsf;
			node->node.GROUP9.B2R2_RZI =
				B2R2_RZI_DEFAULT_HNB_REPEAT |
				(2 << B2R2_RZI_VNB_REPEAT_SHIFT);
		}

		node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
		node->node.GROUP4.B2R2_STY = group4_b2r2_sty;
	} else if (yuv_semi_planar_dst) {
		/*
		 * two nodes required to write the output.
		 * One node for luma and one for interleaved chroma
		 * components.
		 */
		u32 fctl = 0;
		u32 rsf = 0;
		const u32 group0_b2r2_ins =
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_RECT_CLIP_ENABLED;
		const u32 group0_b2r2_cic =
			B2R2_CIC_SOURCE_2 |
			B2R2_CIC_CLIP_WINDOW;

		u32 chroma_addr = req->dst_resolved.physical_address +
			dst_pitch * dst_img->height;
		u32 chroma_pitch = dst_pitch;
		enum b2r2_native_fmt dst_native_fmt =
				to_native_fmt(cont, dst_img->fmt);
		enum b2r2_ty alpha_range = get_alpha_range(cont, dst_img->fmt);

		if (dst_fmt == B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR ||
			dst_fmt ==
				B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE ||
			dst_fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR) {
			/*
			 * Chrominance is always half the luminance size
			 * so chrominance resizer is always active.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER |
					B2R2_FCTL_VF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (2 << 10) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (2 << 10) << B2R2_RSF_VSRC_INC_SHIFT;
		} else {
			/*
			 * YUV422
			 * Chrominance is always half the luminance size
			 * only in horizontal direction.
			 */
			fctl |= B2R2_FCTL_HF2D_MODE_ENABLE_RESIZER;

			rsf &= ~(0xffff << B2R2_RSF_HSRC_INC_SHIFT);
			rsf |= (2 << 10) << B2R2_RSF_HSRC_INC_SHIFT;
			rsf &= ~(0xffff << B2R2_RSF_VSRC_INC_SHIFT);
			rsf |= (1 << 10) << B2R2_RSF_VSRC_INC_SHIFT;
		}

		/* Luma (Y-component) */
		node->node.GROUP1.B2R2_TBA = req->dst_resolved.physical_address;
		node->node.GROUP1.B2R2_TTY =
			(dst_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt | alpha_range |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			dst_dither;

		if (dst_fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			dst_fmt == B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR) {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YVU_FULL);
			else
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YVU);
		} else {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV_FULL);
			else
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV);
		}

		 /* bypass ALU, no blending here. Handled in its own stage. */
		node->node.GROUP0.B2R2_ACK = B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS = group0_b2r2_ins;
		node->node.GROUP0.B2R2_CIC |= group0_b2r2_cic;

		/* Set source buffer on SRC2 channel */
		node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
		node->node.GROUP4.B2R2_STY = group4_b2r2_sty;

		/* Chroma (UV-components)*/
		node = node->next;
		node->node.GROUP1.B2R2_TBA = chroma_addr;
		node->node.GROUP1.B2R2_TTY =
			(chroma_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			dst_native_fmt | alpha_range |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			dst_dither |
			B2R2_TTY_CHROMA_NOT_LUMA;

		if (dst_fmt == B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR ||
			dst_fmt == B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR) {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YVU_FULL);
			else
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YVU);
		} else {
			if (fullrange)
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV_FULL);
			else
				b2r2_setup_ivmx(node, B2R2_CC_RGB_TO_YUV);
		}

		node->node.GROUP0.B2R2_ACK = B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS =
			group0_b2r2_ins | B2R2_INS_RESCALE2D_ENABLED;
		node->node.GROUP0.B2R2_CIC |= group0_b2r2_cic |
			B2R2_CIC_FILTER_CONTROL |
			B2R2_CIC_RESIZE_CHROMA;

		/* Set the filter control and rescale registers */
		node->node.GROUP8.B2R2_FCTL = fctl;
		node->node.GROUP9.B2R2_RSF = rsf;
		node->node.GROUP9.B2R2_RZI =
			B2R2_RZI_DEFAULT_HNB_REPEAT |
			(2 << B2R2_RZI_VNB_REPEAT_SHIFT);

		node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
		node->node.GROUP4.B2R2_STY = group4_b2r2_sty;
	} else {
		/* single buffer target */

		switch (dst_fmt) {
		case B2R2_BLT_FMT_32_BIT_ABGR8888:
		case B2R2_BLT_FMT_16_BIT_ABGR4444:
			b2r2_setup_ovmx(node, B2R2_CC_RGB_TO_BGR);
			break;
		case B2R2_BLT_FMT_24_BIT_YUV888: /* fall through */
		case B2R2_BLT_FMT_32_BIT_AYUV8888: /* fall through */
		case B2R2_BLT_FMT_24_BIT_VUY888: /* fall through */
		case B2R2_BLT_FMT_32_BIT_VUYA8888:
			if (fullrange)
				b2r2_setup_ovmx(node, B2R2_CC_RGB_TO_BLT_YUV888_FULL);
			else
				b2r2_setup_ovmx(node, B2R2_CC_RGB_TO_BLT_YUV888);
			/*
			 * Re-arrange color components from (A)YUV to VUY(A)
			 * when bytes are stored in memory.
			 */
			if (dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
					dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
				endianness = B2R2_TY_ENDIAN_BIG_NOT_LITTLE;
			break;
		default:
			break;
		}

		node->node.GROUP1.B2R2_TBA = req->dst_resolved.physical_address;
		node->node.GROUP1.B2R2_TTY =
			(dst_pitch << B2R2_TY_BITMAP_PITCH_SHIFT) |
			to_native_fmt(cont, dst_img->fmt) |
			get_alpha_range(cont, dst_img->fmt) |
			B2R2_TY_HSO_LEFT_TO_RIGHT |
			B2R2_TY_VSO_TOP_TO_BOTTOM |
			dst_dither |
			endianness;

		node->node.GROUP0.B2R2_ACK = B2R2_ACK_MODE_BYPASS_S2_S3;
		node->node.GROUP0.B2R2_INS |=
			B2R2_INS_SOURCE_2_FETCH_FROM_MEM |
			B2R2_INS_RECT_CLIP_ENABLED;
		node->node.GROUP0.B2R2_CIC |=
			B2R2_CIC_SOURCE_2 | B2R2_CIC_CLIP_WINDOW;

		if (req->user_req.flags & B2R2_BLT_FLAG_SOURCE_COLOR_KEY) {
			u32 key_color = 0;

			node->node.GROUP0.B2R2_ACK |=
				B2R2_ACK_CKEY_SEL_SRC_AFTER_CLUT |
				B2R2_ACK_CKEY_RED_MATCH_IF_BETWEEN |
				B2R2_ACK_CKEY_GREEN_MATCH_IF_BETWEEN |
				B2R2_ACK_CKEY_BLUE_MATCH_IF_BETWEEN;
			node->node.GROUP0.B2R2_INS |= B2R2_INS_CKEY_ENABLED;
			node->node.GROUP0.B2R2_CIC |= B2R2_CIC_COLOR_KEY;

			key_color = to_RGB888(cont, req->user_req.src_color,
				req->user_req.src_img.fmt);
			node->node.GROUP12.B2R2_KEY1 = key_color;
			node->node.GROUP12.B2R2_KEY2 = key_color;
		}

		/* Set source buffer on SRC2 channel */
		node->node.GROUP4.B2R2_SBA = in_buf->phys_addr;
		node->node.GROUP4.B2R2_STY = group4_b2r2_sty;
	}
	/*
	 * Writeback is the last stage. Terminate the program chain
	 * to prevent out-of-control B2R2 execution.
	 */
	node->node.GROUP0.B2R2_NIP = 0;

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}

/*
 * Public functions
 */
void b2r2_generic_init(struct b2r2_control *cont)
{

}

void b2r2_generic_exit(struct b2r2_control *cont)
{

}

int b2r2_generic_analyze(const struct b2r2_blt_request *req,
			 s32 *work_buf_width,
			 s32 *work_buf_height,
			 u32 *work_buf_count,
			 u32 *node_count)
{
	/*
	 * Need at least 4 nodes, read or fill input, read dst, blend
	 * and write back the result */
	u32 n_nodes = 4;
	/* Need at least 2 bufs, 1 for blend output and 1 for input */
	u32 n_work_bufs = 2;
	/* Horizontal and vertical scaling factors in 6.10 fixed point format */
	s32 h_scf = 1 << 10;
	s32 v_scf = 1 << 10;
	enum b2r2_blt_fmt dst_fmt = 0;
	bool is_src_fill = false;
	bool yuv_planar_dst;
	bool yuv_semi_planar_dst;
	struct b2r2_blt_rect src_rect;
	struct b2r2_blt_rect dst_rect;
	struct b2r2_control *cont = req->instance->control;

	if (req == NULL || work_buf_width == NULL || work_buf_height == NULL ||
			work_buf_count == NULL || node_count == NULL) {
		b2r2_log_warn(cont->dev, "%s: Invalid in or out pointers:\n"
			"req=0x%p\n"
			"work_buf_width=0x%p work_buf_height=0x%p "
			"work_buf_count=0x%p\n"
			"node_count=0x%p.\n",
			__func__,
			req,
			work_buf_width, work_buf_height,
			work_buf_count,
			node_count);
		return -EINVAL;
	}

	dst_fmt = req->user_req.dst_img.fmt;

	is_src_fill = (req->user_req.flags &
				(B2R2_BLT_FLAG_SOURCE_FILL |
				B2R2_BLT_FLAG_SOURCE_FILL_RAW)) != 0;

	yuv_planar_dst = b2r2_is_ycbcrp_fmt(dst_fmt);
	yuv_semi_planar_dst = b2r2_is_ycbcrsp_fmt(dst_fmt) ||
		b2r2_is_mb_fmt(dst_fmt);

	*node_count = 0;
	*work_buf_width = 0;
	*work_buf_height = 0;
	*work_buf_count = 0;

	if (req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		n_nodes++;
		n_work_bufs++;
	}

	if ((yuv_planar_dst || yuv_semi_planar_dst) &&
			(req->user_req.flags & B2R2_BLT_FLAG_SOURCE_FILL_RAW)) {
		b2r2_log_warn(cont->dev,
			"%s: Invalid combination: source_fill_raw"
			" and multi-buffer destination.\n",
			__func__);
		return -EINVAL;
	}

	if ((req->user_req.flags & B2R2_BLT_FLAG_SOURCE_COLOR_KEY) != 0 &&
			(req->user_req.flags & B2R2_BLT_FLAG_DEST_COLOR_KEY)) {
		b2r2_log_warn(cont->dev,
			"%s: Invalid combination: source and "
			"destination color keying.\n", __func__);
		return -EINVAL;
	}

	if ((req->user_req.flags &
			(B2R2_BLT_FLAG_SOURCE_FILL |
			B2R2_BLT_FLAG_SOURCE_FILL_RAW)) &&
			(req->user_req.flags &
			(B2R2_BLT_FLAG_SOURCE_COLOR_KEY |
			B2R2_BLT_FLAG_DEST_COLOR_KEY))) {
		b2r2_log_warn(cont->dev, "%s: Invalid combination: "
			"source_fill and color keying.\n",
			__func__);
		return -EINVAL;
	}

	if ((req->user_req.flags &
			(B2R2_BLT_FLAG_PER_PIXEL_ALPHA_BLEND |
			B2R2_BLT_FLAG_GLOBAL_ALPHA_BLEND)) &&
			(req->user_req.flags &
			(B2R2_BLT_FLAG_DEST_COLOR_KEY |
			B2R2_BLT_FLAG_SOURCE_COLOR_KEY))) {
		b2r2_log_warn(cont->dev, "%s: Invalid combination: "
			"blending and color keying.\n",
			__func__);
		return -EINVAL;
	}

	if ((req->user_req.flags & B2R2_BLT_FLAG_SOURCE_MASK) &&
			(req->user_req.flags &
			(B2R2_BLT_FLAG_DEST_COLOR_KEY |
			B2R2_BLT_FLAG_SOURCE_COLOR_KEY))) {
		b2r2_log_warn(cont->dev, "%s: Invalid combination: source mask"
				"and color keying.\n",
			__func__);
		return -EINVAL;
	}

	if (req->user_req.flags &
			(B2R2_BLT_FLAG_DEST_COLOR_KEY |
			B2R2_BLT_FLAG_SOURCE_MASK)) {
		b2r2_log_warn(cont->dev, "%s: Unsupported: source mask, "
			"destination color keying.\n",
			__func__);
		return -ENOSYS;
	}

	if ((req->user_req.flags & B2R2_BLT_FLAG_SOURCE_MASK)) {
		enum b2r2_blt_fmt src_fmt = req->user_req.src_img.fmt;
		if (b2r2_is_yuv_fmt(src_fmt) ||
				src_fmt == B2R2_BLT_FMT_1_BIT_A1 ||
				src_fmt == B2R2_BLT_FMT_8_BIT_A8) {
			b2r2_log_warn(cont->dev, "%s: Unsupported: source "
				"color keying with YUV or pure alpha "
				"formats.\n", __func__);
			return -ENOSYS;
		}
	}

	/* Check for invalid dimensions that would hinder scale calculations */
	src_rect = req->user_req.src_rect;
	dst_rect = req->user_req.dst_rect;
	/* Check for invalid src_rect unless src_fill is enabled */
	if (!is_src_fill && (src_rect.x < 0 || src_rect.y < 0 ||
		src_rect.x + src_rect.width > req->user_req.src_img.width ||
		src_rect.y + src_rect.height > req->user_req.src_img.height)) {
		b2r2_log_warn(cont->dev, "%s: src_rect outside src_img:\n"
			"src(x,y,w,h)=(%d, %d, %d, %d) "
			"src_img(w,h)=(%d, %d).\n",
			__func__,
			src_rect.x, src_rect.y, src_rect.width, src_rect.height,
			req->user_req.src_img.width,
			req->user_req.src_img.height);
		return -EINVAL;
	}

	if (!is_src_fill && (src_rect.width <= 0 || src_rect.height <= 0)) {
		b2r2_log_warn(cont->dev, "%s: Invalid source dimensions:\n"
			"src(w,h)=(%d, %d).\n",
			__func__,
			src_rect.width, src_rect.height);
		return -EINVAL;
	}

	if (dst_rect.width <= 0 || dst_rect.height <= 0) {
		b2r2_log_warn(cont->dev, "%s: Invalid dest dimensions:\n"
			"dst(w,h)=(%d, %d).\n",
			__func__,
			dst_rect.width, dst_rect.height);
		return -EINVAL;
	}

	if ((req->user_req.flags & B2R2_BLT_FLAG_CLUT_COLOR_CORRECTION) &&
			req->user_req.clut == NULL) {
		b2r2_log_warn(cont->dev, "%s: Invalid request: no table "
				"specified for CLUT color correction.\n",
			__func__);
		return -EINVAL;
	}

	/* Check for invalid image params */
	if (!is_src_fill && validate_buf(cont, &(req->user_req.src_img),
			&(req->src_resolved)))
		return -EINVAL;

	if (validate_buf(cont, &(req->user_req.dst_img), &(req->dst_resolved)))
		return -EINVAL;

	if (is_src_fill) {
		/*
		 * Params correct for a source fill operation.
		 * No need for further checking.
		 */
		if (yuv_planar_dst)
			n_nodes += 2;
		else if (yuv_semi_planar_dst)
			n_nodes++;

		*work_buf_width = B2R2_GENERIC_WORK_BUF_WIDTH;
		*work_buf_height = B2R2_GENERIC_WORK_BUF_HEIGHT;
		*work_buf_count = n_work_bufs;
		*node_count = n_nodes;
		b2r2_log_info(cont->dev, "%s DONE buf_w=%d buf_h=%d "
				"buf_count=%d node_count=%d\n", __func__,
			*work_buf_width, *work_buf_height,
			*work_buf_count, *node_count);
		return 0;
	}

	/*
	 * Calculate scaling factors, all transform enum values
	 * that include rotation have the CCW_ROT_90 bit set.
	 */
	if (req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		h_scf = (src_rect.width << 10) / dst_rect.height;
		v_scf = (src_rect.height << 10) / dst_rect.width;
	} else {
		h_scf = (src_rect.width << 10) / dst_rect.width;
		v_scf = (src_rect.height << 10) / dst_rect.height;
	}

	/* Check for degenerate/out_of_range scaling factors. */
	if (h_scf <= 0 || v_scf <= 0 || h_scf > 0x7C00 || v_scf > 0x7C00) {
		b2r2_log_warn(cont->dev,
			"%s: Dimensions result in degenerate or "
			"out of range scaling:\n"
			"src(w,h)=(%d, %d) "
			"dst(w,h)=(%d,%d).\n"
			"h_scf=0x%.8x, v_scf=0x%.8x\n",
			__func__,
			src_rect.width, src_rect.height,
			dst_rect.width, dst_rect.height,
			h_scf, v_scf);
		return -EINVAL;
	}

	if (yuv_planar_dst)
		n_nodes += 2;
	else if (yuv_semi_planar_dst)
		n_nodes++;

	*work_buf_width = B2R2_GENERIC_WORK_BUF_WIDTH;
	*work_buf_height = B2R2_GENERIC_WORK_BUF_HEIGHT;
	*work_buf_count = n_work_bufs;
	*node_count = n_nodes;
	b2r2_log_info(cont->dev, "%s DONE buf_w=%d buf_h=%d buf_count=%d "
		"node_count=%d\n", __func__, *work_buf_width,
		*work_buf_height, *work_buf_count, *node_count);
	return 0;
}

/*
 *
 */
int b2r2_generic_configure(const struct b2r2_blt_request *req,
			   struct b2r2_node *first,
			   struct b2r2_work_buf *tmp_bufs,
			   u32 buf_count)
{
	struct b2r2_node *node = NULL;
	struct b2r2_work_buf *in_buf = NULL;
	struct b2r2_work_buf *out_buf = NULL;
	struct b2r2_work_buf *empty_buf = NULL;
	struct b2r2_control *cont = req->instance->control;

#ifdef B2R2_GENERIC_DEBUG
	u32 needed_bufs = 0;
	u32 needed_nodes = 0;
	s32 work_buf_width = 0;
	s32 work_buf_height = 0;
	u32 n_nodes = 0;
	int invalid_req = b2r2_generic_analyze(req, &work_buf_width,
					       &work_buf_height, &needed_bufs,
					       &needed_nodes);
	if (invalid_req < 0) {
		b2r2_log_warn(cont->dev,
			"%s: Invalid request supplied, ec=%d\n",
			__func__, invalid_req);
		return -EINVAL;
	}

	node = first;

	while (node != NULL) {
		n_nodes++;
		node = node->next;
	}
	if (n_nodes < needed_nodes) {
		b2r2_log_warn(cont->dev, "%s: Not enough nodes %d < %d.\n",
			      __func__, n_nodes, needed_nodes);
		return -EINVAL;
	}

	if (buf_count < needed_bufs) {
		b2r2_log_warn(cont->dev, "%s: Not enough buffers %d < %d.\n",
			      __func__, buf_count, needed_bufs);
		return -EINVAL;
	}

#endif

	reset_nodes(cont, first);
	node = first;
	empty_buf = tmp_bufs;
	out_buf = empty_buf;
	empty_buf++;
	/* Prepare input tile. Color_fill or read from src */
	setup_input_stage(req, node, out_buf);
	in_buf = out_buf;
	out_buf = empty_buf;
	empty_buf++;
	node = node->next;

	if ((req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) != 0) {
		setup_transform_stage(req, node, out_buf, in_buf);
		node = node->next;
		in_buf = out_buf;
		out_buf = empty_buf++;
	}
	/* EMACSOC TODO: mask */
	/*
	if (req->user_req.flags & B2R2_BLT_FLAG_SOURCE_MASK) {
		setup_mask_stage(req, node, out_buf, in_buf);
		node = node->next;
		in_buf = out_buf;
		out_buf = empty_buf++;
	}
	*/
	/* Read the part of destination that will be updated */
	setup_dst_read_stage(req, node, out_buf);
	node = node->next;
	setup_blend_stage(req, node, out_buf, in_buf);
	node = node->next;
	in_buf = out_buf;
	setup_writeback_stage(req, node, in_buf);
	return 0;
}

void b2r2_generic_set_areas(const struct b2r2_blt_request *req,
			    struct b2r2_node *first,
			    struct b2r2_blt_rect *dst_rect_area)
{
	/*
	 * Nodes come in the following order: <input stage>, [transform],
	 * [src_mask], <dst_read>, <blend>, <writeback>
	 */
	struct b2r2_node *node = first;
	const struct b2r2_blt_rect *dst_rect = &(req->user_req.dst_rect);
	const struct b2r2_blt_rect *src_rect = &(req->user_req.src_rect);
	const enum b2r2_blt_fmt src_fmt = req->user_req.src_img.fmt;
	const bool yuv_multi_buffer_src = b2r2_is_ycbcrsp_fmt(src_fmt) ||
		b2r2_is_ycbcrp_fmt(src_fmt) || b2r2_is_mb_fmt(src_fmt);
	const enum b2r2_blt_fmt dst_fmt = req->user_req.dst_img.fmt;
	const bool yuv_multi_buffer_dst = b2r2_is_ycbcrsp_fmt(dst_fmt) ||
		b2r2_is_ycbcrp_fmt(dst_fmt) || b2r2_is_mb_fmt(dst_fmt);

	s32 h_scf = 1 << 10;
	s32 v_scf = 1 << 10;
	s32 src_x = 0;
	s32 src_y = 0;
	s32 src_w = 0;
	s32 src_h = 0;
	u32 b2r2_rzi = 0;
	s32 clip_top = 0;
	s32 clip_left = 0;
	s32 clip_bottom = req->user_req.dst_img.height - 1;
	s32 clip_right = req->user_req.dst_img.width - 1;
	/* Dst coords inside the dst_rect, not the buffer */
	s32 dst_x = dst_rect_area->x;
	s32 dst_y = dst_rect_area->y;
	struct b2r2_control *cont = req->instance->control;

	b2r2_log_info(cont->dev, "%s ENTRY\n", __func__);

	if (req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		h_scf = (src_rect->width << 10) / dst_rect->height;
		v_scf = (src_rect->height << 10) / dst_rect->width;
	} else {
		h_scf = (src_rect->width << 10) / dst_rect->width;
		v_scf = (src_rect->height << 10) / dst_rect->height;
	}

	if (req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		/*
		 * Normally the inverse transform for 90 degree rotation
		 * is given by:
		 * | 0  1|   |x|   | y|
		 * |     | X | | = |  |
		 * |-1  0|   |y|   |-x|
		 * but screen coordinates are flipped in y direction
		 * (compared to usual Cartesian coordinates), hence the offsets.
		 */
		src_x = (dst_rect->height - dst_y - dst_rect_area->height) *
			h_scf;
		src_y = dst_x * v_scf;
		src_w = dst_rect_area->height * h_scf;
		src_h = dst_rect_area->width * v_scf;
	} else {
		src_x = dst_x * h_scf;
		src_y = dst_y * v_scf;
		src_w = dst_rect_area->width * h_scf;
		src_h = dst_rect_area->height * v_scf;
	}

	b2r2_rzi |= ((src_x & 0x3ff) << B2R2_RZI_HSRC_INIT_SHIFT) |
		((src_y & 0x3ff) << B2R2_RZI_VSRC_INIT_SHIFT);

	/*
	 * src_w must contain all the pixels that contribute
	 * to a particular tile.
	 * ((x + 0x3ff) >> 10) is equivalent to ceiling(x),
	 * expressed in 6.10 fixed point format.
	 * Every destination tile, maps to a certain area in the source
	 * rectangle. The area in source will most likely not be a rectangle
	 * with exact integer dimensions whenever arbitrary scaling is involved.
	 * Consider the following example.
	 * Suppose, that width of the current destination tile maps
	 * to 1.7 pixels in source, starting at x == 5.4, as calculated
	 * using the scaling factor.
	 * This means that while the destination tile is written,
	 * the source should be read from x == 5.4 up to x == 5.4 + 1.7 == 7.1
	 * Consequently, color from 3 pixels (x == 5, 6 and 7)
	 * needs to be read from source.
	 * The formula below the comment yields:
	 * ceil(0.4 + 1.7) == ceil(2.1) == 3
	 * (src_x & 0x3ff) is the fractional part of src_x,
	 * which is expressed in 6.10 fixed point format.
	 * Thus, width of the source area should be 3 pixels wide,
	 * starting at x == 5.
	 * However, the reading should not start at x == 5.0
	 * but a bit inside, namely x == 5.4
	 * The B2R2_RZI register is used to instruct the HW to do so.
	 * It contains the fractional part that will be added to
	 * the first pixel coordinate, before incrementing the current source
	 * coordinate with the step specified in B2R2_RSF register.
	 * The same applies to scaling in vertical direction.
	 */
	src_w = ((src_x & 0x3ff) + src_w + 0x3ff) >> 10;
	src_h = ((src_y & 0x3ff) + src_h + 0x3ff) >> 10;

	/*
	 * EMACSOC TODO: Remove this debug clamp, once tile size
	 * is taken into account in generic_analyze()
	 */
	if (src_w > 128)
		src_w = 128;

	src_x >>= 10;
	src_y >>= 10;

	if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_H)
		src_x = src_rect->width - src_x - src_w;

	if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_V)
		src_y = src_rect->height - src_y - src_h;

	/*
	 * Translate the src/dst_rect coordinates into true
	 * src/dst_buffer coordinates
	 */
	src_x += src_rect->x;
	src_y += src_rect->y;

	dst_x += dst_rect->x;
	dst_y += dst_rect->y;

	/*
	 * Clamp the src coords to buffer dimensions
	 * to prevent illegal reads.
	 */
	if (src_x < 0)
		src_x = 0;

	if (src_y < 0)
		src_y = 0;

	if ((src_x + src_w) > req->user_req.src_img.width)
		src_w = req->user_req.src_img.width - src_x;

	if ((src_y + src_h) > req->user_req.src_img.height)
		src_h = req->user_req.src_img.height - src_y;


	/* The input node */
	if (yuv_multi_buffer_src) {
		/* Luma on SRC3 */
		node->node.GROUP5.B2R2_SXY =
			((src_x & 0xffff) << B2R2_XY_X_SHIFT) |
			((src_y & 0xffff) << B2R2_XY_Y_SHIFT);
		node->node.GROUP5.B2R2_SSZ =
			((src_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((src_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

		/* Clear and set only the SRC_INIT bits */
		node->node.GROUP10.B2R2_RZI &=
			~((0x3ff << B2R2_RZI_HSRC_INIT_SHIFT) |
			(0x3ff << B2R2_RZI_VSRC_INIT_SHIFT));
		node->node.GROUP10.B2R2_RZI |= b2r2_rzi;

		node->node.GROUP9.B2R2_RZI &=
			~((0x3ff << B2R2_RZI_HSRC_INIT_SHIFT) |
			(0x3ff << B2R2_RZI_VSRC_INIT_SHIFT));
		switch (src_fmt) {
		case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
		case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
			/*
			 * Chroma goes on SRC2 and potentially on SRC1.
			 * Chroma is half the size of luma. Must round up
			 * the chroma size to handle cases when luma size is not
			 * divisible by 2.
			 * E.g. luma width==7 requires chroma width==4.
			 * Chroma width==7/2==3 is only enough
			 * for luma width==6.
			 */
			node->node.GROUP4.B2R2_SXY =
				(((src_x & 0xffff) >> 1) << B2R2_XY_X_SHIFT) |
				(((src_y & 0xffff) >> 1) << B2R2_XY_Y_SHIFT);
			node->node.GROUP4.B2R2_SSZ =
				((((src_w + 1) & 0xfff) >> 1) <<
					B2R2_SZ_WIDTH_SHIFT) |
				((((src_h + 1) & 0xfff) >> 1) <<
					B2R2_SZ_HEIGHT_SHIFT);
			if (src_fmt == B2R2_BLT_FMT_YUV420_PACKED_PLANAR ||
					src_fmt ==
					B2R2_BLT_FMT_YVU420_PACKED_PLANAR) {
				node->node.GROUP3.B2R2_SXY =
					node->node.GROUP4.B2R2_SXY;
				node->node.GROUP3.B2R2_SSZ =
					node->node.GROUP4.B2R2_SSZ;
			}
			node->node.GROUP9.B2R2_RZI |= (b2r2_rzi >> 1) &
				((0x3ff << B2R2_RZI_HSRC_INIT_SHIFT) |
				 (0x3ff << B2R2_RZI_VSRC_INIT_SHIFT));
			break;
		case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
		case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
		case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
			/*
			 * Chroma goes on SRC2 and potentially on SRC1.
			 * Now chroma is half the size of luma
			 * only in horizontal direction.
			 * Same rounding applies as for 420 formats above,
			 * except it is only done horizontally.
			 */
			node->node.GROUP4.B2R2_SXY =
				(((src_x & 0xffff) >> 1) << B2R2_XY_X_SHIFT) |
				((src_y & 0xffff) << B2R2_XY_Y_SHIFT);
			node->node.GROUP4.B2R2_SSZ =
				((((src_w + 1) & 0xfff) >> 1) <<
							B2R2_SZ_WIDTH_SHIFT) |
				((src_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);
			if (src_fmt == B2R2_BLT_FMT_YUV422_PACKED_PLANAR ||
					src_fmt ==
					B2R2_BLT_FMT_YVU422_PACKED_PLANAR) {
				node->node.GROUP3.B2R2_SXY =
					node->node.GROUP4.B2R2_SXY;
				node->node.GROUP3.B2R2_SSZ =
					node->node.GROUP4.B2R2_SSZ;
			}
			node->node.GROUP9.B2R2_RZI |=
				(((src_x & 0x3ff) >> 1) <<
						B2R2_RZI_HSRC_INIT_SHIFT) |
				((src_y & 0x3ff) << B2R2_RZI_VSRC_INIT_SHIFT);
			break;
		case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
			/*
			 * Chroma goes on SRC2 and SRC1.
			 * It is the same size as luma.
			 */
			node->node.GROUP4.B2R2_SXY =
				((src_x & 0xffff) << B2R2_XY_X_SHIFT) |
				((src_y & 0xffff) << B2R2_XY_Y_SHIFT);
			node->node.GROUP4.B2R2_SSZ =
				((src_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
				((src_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);
			node->node.GROUP3.B2R2_SXY = node->node.GROUP4.B2R2_SXY;
			node->node.GROUP3.B2R2_SSZ = node->node.GROUP4.B2R2_SSZ;

			/* Clear and set only the SRC_INIT bits */
			node->node.GROUP9.B2R2_RZI &=
				~((0x3ff << B2R2_RZI_HSRC_INIT_SHIFT) |
				  (0x3ff << B2R2_RZI_VSRC_INIT_SHIFT));
			node->node.GROUP9.B2R2_RZI |= b2r2_rzi;
			break;
		default:
			break;
		}
	} else {
		node->node.GROUP4.B2R2_SXY =
			((src_x & 0xffff) << B2R2_XY_X_SHIFT) |
			((src_y & 0xffff) << B2R2_XY_Y_SHIFT);
		node->node.GROUP4.B2R2_SSZ =
			((src_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((src_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

		/* Clear and set only the SRC_INIT bits */
		node->node.GROUP9.B2R2_RZI &=
			~((0x3ff << B2R2_RZI_HSRC_INIT_SHIFT) |
			  (0x3ff << B2R2_RZI_VSRC_INIT_SHIFT));
		node->node.GROUP9.B2R2_RZI |= b2r2_rzi;
	}

	node->node.GROUP1.B2R2_TXY = 0;
	if (req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) {
		/*
		 * dst_rect_area coordinates are specified
		 * after potential rotation.
		 * Input is read before rotation, hence the width and height
		 * need to be swapped.
		 * Horizontal and vertical flips are accomplished with
		 * suitable scanning order while writing
		 * to the temporary buffer.
		 */
		if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_H)
			node->node.GROUP1.B2R2_TXY |=
				((dst_rect_area->height - 1) & 0xffff) <<
				B2R2_XY_X_SHIFT;

		if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_V)
			node->node.GROUP1.B2R2_TXY |=
				((dst_rect_area->width - 1) & 0xffff) <<
				B2R2_XY_Y_SHIFT;

		node->node.GROUP1.B2R2_TSZ =
			((dst_rect_area->height & 0xfff) <<
						B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->width & 0xfff) <<
						B2R2_SZ_HEIGHT_SHIFT);
	} else {
		if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_H)
			node->node.GROUP1.B2R2_TXY |=
				((dst_rect_area->width - 1) & 0xffff) <<
				B2R2_XY_X_SHIFT;

		if (req->user_req.transform & B2R2_BLT_TRANSFORM_FLIP_V)
			node->node.GROUP1.B2R2_TXY |=
				((dst_rect_area->height - 1) & 0xffff) <<
				B2R2_XY_Y_SHIFT;

		node->node.GROUP1.B2R2_TSZ =
			((dst_rect_area->width & 0xfff) <<
						B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->height & 0xfff) <<
						B2R2_SZ_HEIGHT_SHIFT);
	}

	if (req->user_req.flags &
		(B2R2_BLT_FLAG_SOURCE_FILL | B2R2_BLT_FLAG_SOURCE_FILL_RAW)) {
		/*
		 * Scan order for source fill should always be left-to-right
		 * and top-to-bottom. Fill the input tile from top left.
		 */
		node->node.GROUP1.B2R2_TXY = 0;
		node->node.GROUP4.B2R2_SSZ = node->node.GROUP1.B2R2_TSZ;
	}

	if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
			dst_rect_area->y == 0) {
		dump_nodes(cont, node, false);
		b2r2_log_debug(cont->dev, "%s Input node done.\n", __func__);
	}

	/* Transform */
	if ((req->user_req.transform & B2R2_BLT_TRANSFORM_CCW_ROT_90) != 0) {
		/*
		 * Transform node operates on temporary buffers.
		 * Content always at top left, but scanning order
		 * has to be flipped during rotation.
		 * Width and height need to be considered as well, since
		 * a tile may not necessarily be filled completely.
		 * dst_rect_area dimensions are specified
		 * after potential rotation.
		 * Input is read before rotation, hence the width and height
		 * need to be swapped on src.
		 */
		node = node->next;

		node->node.GROUP4.B2R2_SXY = 0;
		node->node.GROUP4.B2R2_SSZ =
			((dst_rect_area->height & 0xfff) <<
						B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->width & 0xfff) <<
						B2R2_SZ_HEIGHT_SHIFT);
		/* Bottom line written first */
		node->node.GROUP1.B2R2_TXY =
			((dst_rect_area->height - 1) & 0xffff) <<
			B2R2_XY_Y_SHIFT;

		node->node.GROUP1.B2R2_TSZ =
			((dst_rect_area->width & 0xfff) <<
						B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->height & 0xfff) <<
						B2R2_SZ_HEIGHT_SHIFT);

		if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
				dst_rect_area->y == 0) {
			dump_nodes(cont, node, false);
			b2r2_log_debug(cont->dev,
				"%s Tranform node done.\n", __func__);
		}
	}

	/* Source mask */
	if (req->user_req.flags & B2R2_BLT_FLAG_SOURCE_MASK) {
		node = node->next;
		/*
		 * Same coords for mask as for the input stage.
		 * Should the mask be transformed together with source?
		 * EMACSOC TODO: Apply mask before any
		 * transform/scaling is done.
		 * Otherwise it will be dst_ not src_mask.
		 */
		if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
				dst_rect_area->y == 0) {
			dump_nodes(cont, node, false);
			b2r2_log_debug(cont->dev,
				"%s Source mask node done.\n", __func__);
		}
	}

	/* dst_read */
	if (yuv_multi_buffer_dst) {
		s32 dst_w = dst_rect_area->width;
		s32 dst_h = dst_rect_area->height;
		node = node->next;
		/* Luma on SRC3 */
		node->node.GROUP5.B2R2_SXY =
			((dst_x & 0xffff) << B2R2_XY_X_SHIFT) |
			((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
		node->node.GROUP5.B2R2_SSZ =
			((dst_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((dst_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

		if (b2r2_is_ycbcr420_fmt(dst_fmt)) {
			/*
			 * Chroma goes on SRC2 and potentially on SRC1.
			 * Chroma is half the size of luma. Must round up
			 * the chroma size to handle cases when luma size is not
			 * divisible by 2.
			 * E.g. luma width==7 requires chroma width==4.
			 * Chroma width==7/2==3 is only enough
			 * for luma width==6.
			 */
			node->node.GROUP4.B2R2_SXY =
				(((dst_x & 0xffff) >> 1) << B2R2_XY_X_SHIFT) |
				(((dst_y & 0xffff) >> 1) << B2R2_XY_Y_SHIFT);
			node->node.GROUP4.B2R2_SSZ =
				((((dst_w + 1) & 0xfff) >> 1) <<
							B2R2_SZ_WIDTH_SHIFT) |
				((((dst_h + 1) & 0xfff) >> 1) <<
							B2R2_SZ_HEIGHT_SHIFT);

			if (dst_fmt == B2R2_BLT_FMT_YUV420_PACKED_PLANAR ||
					dst_fmt ==
					B2R2_BLT_FMT_YVU420_PACKED_PLANAR) {
				node->node.GROUP3.B2R2_SXY =
					node->node.GROUP4.B2R2_SXY;
				node->node.GROUP3.B2R2_SSZ =
					node->node.GROUP4.B2R2_SSZ;
			}
		} else if (b2r2_is_ycbcr422_fmt(dst_fmt)) {
			/*
			 * Chroma goes on SRC2 and potentially on SRC1.
			 * Now chroma is half the size of luma
			 * only in horizontal direction.
			 * Same rounding applies as for 420 formats above,
			 * except it is only done horizontally.
			 */
			node->node.GROUP4.B2R2_SXY =
				(((dst_x & 0xffff) >> 1) << B2R2_XY_X_SHIFT) |
				((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
			node->node.GROUP4.B2R2_SSZ =
				((((dst_w + 1) & 0xfff) >> 1) <<
							B2R2_SZ_WIDTH_SHIFT) |
				((dst_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

			if (dst_fmt == B2R2_BLT_FMT_YUV422_PACKED_PLANAR ||
					dst_fmt ==
					B2R2_BLT_FMT_YVU422_PACKED_PLANAR) {
				node->node.GROUP3.B2R2_SXY =
					node->node.GROUP4.B2R2_SXY;
				node->node.GROUP3.B2R2_SSZ =
					node->node.GROUP4.B2R2_SSZ;
			}
		} else if (dst_fmt == B2R2_BLT_FMT_YUV444_PACKED_PLANAR) {
			/*
			 * Chroma goes on SRC2 and SRC1.
			 * It is the same size as luma.
			 */
			node->node.GROUP4.B2R2_SXY = node->node.GROUP5.B2R2_SXY;
			node->node.GROUP4.B2R2_SSZ = node->node.GROUP5.B2R2_SSZ;
			node->node.GROUP3.B2R2_SXY = node->node.GROUP5.B2R2_SXY;
			node->node.GROUP3.B2R2_SSZ = node->node.GROUP5.B2R2_SSZ;
		}

		node->node.GROUP1.B2R2_TXY = 0;
		node->node.GROUP1.B2R2_TSZ =
			((dst_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((dst_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);
	} else {
		node = node->next;
		node->node.GROUP4.B2R2_SXY =
			((dst_x & 0xffff) << B2R2_XY_X_SHIFT) |
			((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
		node->node.GROUP4.B2R2_SSZ =
			((dst_rect_area->width & 0xfff) <<
							B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->height & 0xfff) <<
							B2R2_SZ_HEIGHT_SHIFT);
		node->node.GROUP1.B2R2_TXY = 0;
		node->node.GROUP1.B2R2_TSZ =
			((dst_rect_area->width & 0xfff) <<
							B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->height & 0xfff) <<
							B2R2_SZ_HEIGHT_SHIFT);
	}

	if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
			dst_rect_area->y == 0) {
		dump_nodes(cont, node, false);
		b2r2_log_debug(cont->dev, "%s dst_read node done.\n", __func__);
	}

	/* blend */
	node = node->next;
	node->node.GROUP3.B2R2_SXY = 0;
	node->node.GROUP3.B2R2_SSZ =
		((dst_rect_area->width & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
		((dst_rect_area->height & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);
	/* contents of the foreground temporary buffer always at top left */
	node->node.GROUP4.B2R2_SXY = 0;
	node->node.GROUP4.B2R2_SSZ =
		((dst_rect_area->width & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
		((dst_rect_area->height & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

	node->node.GROUP1.B2R2_TXY = 0;
	node->node.GROUP1.B2R2_TSZ =
		((dst_rect_area->width & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
		((dst_rect_area->height & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

	if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
			dst_rect_area->y == 0) {
		dump_nodes(cont, node, false);
		b2r2_log_debug(cont->dev, "%s Blend node done.\n", __func__);
	}

	/* writeback */
	node = node->next;
	if ((req->user_req.flags & B2R2_BLT_FLAG_DESTINATION_CLIP) != 0) {
		clip_left = req->user_req.dst_clip_rect.x;
		clip_top = req->user_req.dst_clip_rect.y;
		clip_right = clip_left + req->user_req.dst_clip_rect.width - 1;
		clip_bottom = clip_top + req->user_req.dst_clip_rect.height - 1;
	}
	/*
	 * Clamp the dst clip rectangle to buffer dimensions to prevent
	 * illegal writes. An illegal clip rectangle, e.g. outside the
	 * buffer will be ignored, resulting in nothing being clipped.
	 */
	if (clip_left < 0 || req->user_req.dst_img.width <= clip_left)
		clip_left = 0;

	if (clip_top < 0 || req->user_req.dst_img.height <= clip_top)
		clip_top = 0;

	if (clip_right < 0 || req->user_req.dst_img.width <= clip_right)
		clip_right = req->user_req.dst_img.width - 1;

	if (clip_bottom < 0 || req->user_req.dst_img.height <= clip_bottom)
		clip_bottom = req->user_req.dst_img.height - 1;

	/*
	 * Only allow writing inside the clip rect.
	 * INTNL bit in B2R2_CWO should be zero.
	 */
	node->node.GROUP6.B2R2_CWO =
		((clip_top & 0x7fff) << B2R2_CWO_Y_SHIFT) |
		((clip_left & 0x7fff) << B2R2_CWO_X_SHIFT);
	node->node.GROUP6.B2R2_CWS =
		((clip_bottom & 0x7fff) << B2R2_CWS_Y_SHIFT) |
		((clip_right & 0x7fff) << B2R2_CWS_X_SHIFT);

	if (yuv_multi_buffer_dst) {
		const s32 dst_w = dst_rect_area->width;
		const s32 dst_h = dst_rect_area->height;
		int i = 0;
		/* Number of nodes required to write chroma output */
		int n_nodes = 1;
		if (b2r2_is_ycbcrp_fmt(dst_fmt))
			n_nodes = 2;

		node->node.GROUP4.B2R2_SXY = 0;
		node->node.GROUP4.B2R2_SSZ =
			((dst_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((dst_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

		/* Luma (Y-component) */
		node->node.GROUP1.B2R2_TXY =
			((dst_x & 0xffff) << B2R2_XY_X_SHIFT) |
			((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
		node->node.GROUP1.B2R2_TSZ =
			((dst_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
			((dst_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

		node->node.GROUP6.B2R2_CWO =
			((clip_top & 0x7fff) << B2R2_CWO_Y_SHIFT) |
			((clip_left & 0x7fff) << B2R2_CWO_X_SHIFT);
		node->node.GROUP6.B2R2_CWS =
			((clip_bottom & 0x7fff) << B2R2_CWS_Y_SHIFT) |
			((clip_right & 0x7fff) << B2R2_CWS_X_SHIFT);

		if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
				dst_rect_area->y == 0) {
			dump_nodes(cont, node, false);
			b2r2_log_debug(cont->dev,
				"%s Writeback luma node done.\n", __func__);
		}

		node = node->next;

		/*
		 * Chroma components. 1 or 2 nodes
		 * for semi-planar or planar buffer respectively.
		 */
		for (i = 0; i < n_nodes && node != NULL; ++i) {

			node->node.GROUP4.B2R2_SXY = 0;
			node->node.GROUP4.B2R2_SSZ =
				((dst_w & 0xfff) << B2R2_SZ_WIDTH_SHIFT) |
				((dst_h & 0xfff) << B2R2_SZ_HEIGHT_SHIFT);

			switch (dst_fmt) {
			case B2R2_BLT_FMT_YUV420_PACKED_PLANAR:
			case B2R2_BLT_FMT_YVU420_PACKED_PLANAR:
			case B2R2_BLT_FMT_YUV420_PACKED_SEMI_PLANAR:
			case B2R2_BLT_FMT_YVU420_PACKED_SEMI_PLANAR:
			case B2R2_BLT_FMT_YUV420_PACKED_SEMIPLANAR_MB_STE:
				/*
				 * Chroma is half the size of luma.
				 * Must round up the chroma size to handle
				 * cases when luma size is not divisible by 2.
				 * E.g. luma_width==7 requires chroma_width==4.
				 * Chroma_width==7/2==3 is only enough
				 * for luma_width==6.
				 */
				node->node.GROUP1.B2R2_TXY =
					(((dst_x & 0xffff) >> 1) <<
							B2R2_XY_X_SHIFT) |
					(((dst_y & 0xffff) >> 1) <<
							B2R2_XY_Y_SHIFT);
				node->node.GROUP1.B2R2_TSZ =
					((((dst_w + 1) & 0xfff) >> 1) <<
							B2R2_SZ_WIDTH_SHIFT) |
					((((dst_h + 1) & 0xfff) >> 1) <<
							B2R2_SZ_HEIGHT_SHIFT);
				break;
			case B2R2_BLT_FMT_YUV422_PACKED_PLANAR:
			case B2R2_BLT_FMT_YVU422_PACKED_PLANAR:
			case B2R2_BLT_FMT_YUV422_PACKED_SEMI_PLANAR:
			case B2R2_BLT_FMT_YVU422_PACKED_SEMI_PLANAR:
			case B2R2_BLT_FMT_YUV422_PACKED_SEMIPLANAR_MB_STE:
				/*
				 * Now chroma is half the size of luma only
				 * in horizontal direction.
				 * Same rounding applies as
				 * for 420 formats above, except it is only
				 * done horizontally.
				 */
				node->node.GROUP1.B2R2_TXY =
					(((dst_x & 0xffff) >> 1) <<
							B2R2_XY_X_SHIFT) |
					((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
				node->node.GROUP1.B2R2_TSZ =
					((((dst_w + 1) & 0xfff) >> 1) <<
							B2R2_SZ_WIDTH_SHIFT) |
					((dst_h & 0xfff) <<
							B2R2_SZ_HEIGHT_SHIFT);
				break;
			case B2R2_BLT_FMT_YUV444_PACKED_PLANAR:
				/*
				 * Chroma has the same resolution as luma.
				 */
				node->node.GROUP1.B2R2_TXY =
					((dst_x & 0xffff) << B2R2_XY_X_SHIFT) |
					((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
				node->node.GROUP1.B2R2_TSZ =
					((dst_w & 0xfff) <<
							B2R2_SZ_WIDTH_SHIFT) |
					((dst_h & 0xfff) <<
							B2R2_SZ_HEIGHT_SHIFT);
				break;
			default:
				break;
			}

			node->node.GROUP6.B2R2_CWO =
				((clip_top & 0x7fff) << B2R2_CWO_Y_SHIFT) |
				((clip_left & 0x7fff) << B2R2_CWO_X_SHIFT);
			node->node.GROUP6.B2R2_CWS =
				((clip_bottom & 0x7fff) << B2R2_CWS_Y_SHIFT) |
				((clip_right & 0x7fff) << B2R2_CWS_X_SHIFT);

			if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
					dst_rect_area->y == 0) {
				dump_nodes(cont, node, false);
				b2r2_log_debug(cont->dev, "%s Writeback chroma "
						"node %d of %d done.\n",
					__func__, i + 1, n_nodes);
			}

			node = node->next;
		}
	} else {
		node->node.GROUP4.B2R2_SXY = 0;
		node->node.GROUP4.B2R2_SSZ =
			((dst_rect_area->width & 0xfff) <<
					B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->height & 0xfff) <<
					B2R2_SZ_HEIGHT_SHIFT);
		node->node.GROUP1.B2R2_TXY =
			((dst_x & 0xffff) << B2R2_XY_X_SHIFT) |
			((dst_y & 0xffff) << B2R2_XY_Y_SHIFT);
		node->node.GROUP1.B2R2_TSZ =
			((dst_rect_area->width & 0xfff) <<
					B2R2_SZ_WIDTH_SHIFT) |
			((dst_rect_area->height & 0xfff) <<
					B2R2_SZ_HEIGHT_SHIFT);

		if (B2R2_GENERIC_DEBUG_AREAS && dst_rect_area->x == 0 &&
				dst_rect_area->y == 0) {
			dump_nodes(cont, node, false);
			b2r2_log_debug(cont->dev, "%s Writeback node done.\n",
					__func__);
		}
	}

	b2r2_log_info(cont->dev, "%s DONE\n", __func__);
}
