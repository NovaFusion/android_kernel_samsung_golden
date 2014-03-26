/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson B2R2 hw color conversion definitions
 *
 * Author: Jorgen Nilsson <jorgen.nilsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef B2R2_HW_CONVERT_H__
#define B2R2_HW_CONVERT_H__

#include "b2r2_internal.h"

enum b2r2_color_conversion {
	B2R2_CC_NOTHING                   = 0x00,
	B2R2_CC_RGB_TO_BGR                = 0x01,
	B2R2_CC_BLT_YUV888_TO_YVU         = 0x02,
	B2R2_CC_BLT_YUV888_TO_YUV         = 0x03,
	B2R2_CC_YUV_TO_BLT_YUV888         = 0x04,
	B2R2_CC_YVU_TO_YUV                = 0x05,
	B2R2_CC_YVU_TO_BLT_YUV888         = 0x06,
	B2R2_CC_RGB_TO_YUV                = 0x07,
	B2R2_CC_RGB_TO_YUV_FULL           = 0x08,
	B2R2_CC_RGB_TO_YVU                = 0x09,
	B2R2_CC_RGB_TO_YVU_FULL           = 0x0A,
	B2R2_CC_RGB_TO_BLT_YUV888         = 0x0B,
	B2R2_CC_RGB_TO_BLT_YUV888_FULL    = 0x0C,
	B2R2_CC_BGR_TO_YVU                = 0x0D,
	B2R2_CC_BGR_TO_YVU_FULL           = 0x0E,
	B2R2_CC_BGR_TO_YUV                = 0x0F,
	B2R2_CC_BGR_TO_YUV_FULL           = 0x10,
	B2R2_CC_YUV_TO_RGB                = 0x11,
	B2R2_CC_YUV_FULL_TO_RGB           = 0x12,
	B2R2_CC_YUV_TO_BGR                = 0x13,
	B2R2_CC_YUV_FULL_TO_BGR           = 0x14,
	B2R2_CC_YVU_TO_RGB                = 0x15,
	B2R2_CC_YVU_FULL_TO_RGB           = 0x16,
	B2R2_CC_YVU_TO_BGR                = 0x17,
	B2R2_CC_YVU_FULL_TO_BGR           = 0x18,
	B2R2_CC_BLT_YUV888_TO_RGB         = 0x19,
	B2R2_CC_BLT_YUV888_FULL_TO_RGB    = 0x1A,
};

int b2r2_setup_ivmx(struct b2r2_node *node, enum b2r2_color_conversion cc);
int b2r2_setup_ovmx(struct b2r2_node *node, enum b2r2_color_conversion cc);
enum b2r2_color_conversion b2r2_get_color_conversion(enum b2r2_blt_fmt src_fmt,
		enum b2r2_blt_fmt dst_fmt, bool fullrange);
int b2r2_get_vmx(enum b2r2_color_conversion cc, const u32 **vmx);

#endif /* B2R2_HW_CONVERT_H__ */
