/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson B2R2 node splitter
 *
 * Author: Jorgen Nilsson <jorgen.nilsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include "b2r2_hw_convert.h"
#include "b2r2_internal.h"
#include "b2r2_utils.h"

/*
 * Macros and constants
 */

/*  VMX register values for RGB to YUV color conversion */
/*  Magic numbers from 27.11 in DB8500_DesignSpecification_v2.5.pdf */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_RGB_TO_YUV_601_FULL_RANGE 0x107e4beb
#define B2R2_VMX1_RGB_TO_YUV_601_FULL_RANGE 0x0982581d
#define B2R2_VMX2_RGB_TO_YUV_601_FULL_RANGE 0xfa9ea483
#define B2R2_VMX3_RGB_TO_YUV_601_FULL_RANGE 0x08000080

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_RGB_TO_YUV_601_STANDARD 0x0e1e8bee
#define B2R2_VMX1_RGB_TO_YUV_601_STANDARD 0x08420419
#define B2R2_VMX2_RGB_TO_YUV_601_STANDARD 0xfb5ed471
#define B2R2_VMX3_RGB_TO_YUV_601_STANDARD 0x08004080

/* 709 Full range conversion matrix */
#define B2R2_VMX0_RGB_TO_YUV_709_FULL_RANGE 0x107e27f4
#define B2R2_VMX1_RGB_TO_YUV_709_FULL_RANGE 0x06e2dc13
#define B2R2_VMX2_RGB_TO_YUV_709_FULL_RANGE 0xfc5e6c83
#define B2R2_VMX3_RGB_TO_YUV_709_FULL_RANGE 0x08000080

/* 709 Standard (clamped) conversion matrix */
#define B2R2_VMX0_RGB_TO_YUV_709_STANDARD 0x0e3e6bf5
#define B2R2_VMX1_RGB_TO_YUV_709_STANDARD 0x05e27410
#define B2R2_VMX2_RGB_TO_YUV_709_STANDARD 0xfcdea471
#define B2R2_VMX3_RGB_TO_YUV_709_STANDARD 0x08004080

/* VMX register values for YUV to RGB color conversion */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_YUV_TO_RGB_601_FULL_RANGE 0x2c440000
#define B2R2_VMX1_YUV_TO_RGB_601_FULL_RANGE 0xe9a403aa
#define B2R2_VMX2_YUV_TO_RGB_601_FULL_RANGE 0x0004013f
#define B2R2_VMX3_YUV_TO_RGB_601_FULL_RANGE 0x34f21322

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_YUV_TO_RGB_601_STANDARD 0x3324a800
#define B2R2_VMX1_YUV_TO_RGB_601_STANDARD 0xe604ab9c
#define B2R2_VMX2_YUV_TO_RGB_601_STANDARD 0x0004a957
#define B2R2_VMX3_YUV_TO_RGB_601_STANDARD 0x32121eeb

/* 709 Full range conversion matrix */
#define B2R2_VMX0_YUV_TO_RGB_709_FULL_RANGE 0x31440000
#define B2R2_VMX1_YUV_TO_RGB_709_FULL_RANGE 0xf16403d1
#define B2R2_VMX2_YUV_TO_RGB_709_FULL_RANGE 0x00040145
#define B2R2_VMX3_YUV_TO_RGB_709_FULL_RANGE 0x33b14b18

/* 709 Standard (clamped) conversion matrix */
#define B2R2_VMX0_YUV_TO_RGB_709_STANDARD 0x3964a800
#define B2R2_VMX1_YUV_TO_RGB_709_STANDARD 0xef04abc9
#define B2R2_VMX2_YUV_TO_RGB_709_STANDARD 0x0004a95f
#define B2R2_VMX3_YUV_TO_RGB_709_STANDARD 0x307132df

/* VMX register values for RGB to BGR conversion */
#define B2R2_VMX0_RGB_TO_BGR 0x00000100
#define B2R2_VMX1_RGB_TO_BGR 0x00040000
#define B2R2_VMX2_RGB_TO_BGR 0x20000000
#define B2R2_VMX3_RGB_TO_BGR 0x00000000

/* VMX register values for BGR to YUV color conversion */
/* Note: All BGR -> YUV values are calculated by multiplying
 * the RGB -> YUV matrices [A], with [S] to form [A]x[S] where
 *     |0 0 1|
 * S = |0 1 0|
 *     |1 0 0|
 * Essentially swapping first and third columns in
 * the matrices (VMX0, VMX1 and VMX2 values).
 * The offset vector VMX3 remains untouched.
 * Put another way, the value of bits 0 through 9
 * is swapped with the value of
 * bits 20 through 31 in VMX0, VMX1 and VMX2,
 * taking into consideration the compression
 * that is used on bits 0 through 9. Bit 0 being LSB.
 */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_BGR_TO_YUV_601_FULL_RANGE 0xfd7e4883
#define B2R2_VMX1_BGR_TO_YUV_601_FULL_RANGE 0x03a2584c
#define B2R2_VMX2_BGR_TO_YUV_601_FULL_RANGE 0x107ea7d4
#define B2R2_VMX3_BGR_TO_YUV_601_FULL_RANGE 0x08000080

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_BGR_TO_YUV_601_STANDARD 0xfdde8870
#define B2R2_VMX1_BGR_TO_YUV_601_STANDARD 0x03220442
#define B2R2_VMX2_BGR_TO_YUV_601_STANDARD 0x0e3ed7da
#define B2R2_VMX3_BGR_TO_YUV_601_STANDARD 0x08004080

/* 709 Full range conversion matrix */
#define B2R2_VMX0_BGR_TO_YUV_709_FULL_RANGE 0xfe9e2483
#define B2R2_VMX1_BGR_TO_YUV_709_FULL_RANGE 0x0262dc37
#define B2R2_VMX2_BGR_TO_YUV_709_FULL_RANGE 0x107e6fe2
#define B2R2_VMX3_BGR_TO_YUV_709_FULL_RANGE 0x08000080

/* 709 Standard (clamped) conversion matrix */
#define B2R2_VMX0_BGR_TO_YUV_709_STANDARD 0xfebe6871
#define B2R2_VMX1_BGR_TO_YUV_709_STANDARD 0x0202742f
#define B2R2_VMX2_BGR_TO_YUV_709_STANDARD 0x0e3ea7e6
#define B2R2_VMX3_BGR_TO_YUV_709_STANDARD 0x08004080


/* VMX register values for YUV to BGR conversion */
/*  Note: All YUV -> BGR values are constructed
 * from the YUV -> RGB ones, by swapping
 * first and third rows in the matrix
 * (VMX0 and VMX2 values). Further, the first and
 * third values in the offset vector need to be
 * swapped as well, i.e. bits 0 through 9 are swapped
 * with bits 20 through 29 in the VMX3 value.
 * Bit 0 being LSB.
 */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_YUV_TO_BGR_601_FULL_RANGE (B2R2_VMX2_YUV_TO_RGB_601_FULL_RANGE)
#define B2R2_VMX1_YUV_TO_BGR_601_FULL_RANGE (B2R2_VMX1_YUV_TO_RGB_601_FULL_RANGE)
#define B2R2_VMX2_YUV_TO_BGR_601_FULL_RANGE (B2R2_VMX0_YUV_TO_RGB_601_FULL_RANGE)
#define B2R2_VMX3_YUV_TO_BGR_601_FULL_RANGE 0x3222134f

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_YUV_TO_BGR_601_STANDARD (B2R2_VMX2_YUV_TO_RGB_601_STANDARD)
#define B2R2_VMX1_YUV_TO_BGR_601_STANDARD (B2R2_VMX1_YUV_TO_RGB_601_STANDARD)
#define B2R2_VMX2_YUV_TO_BGR_601_STANDARD (B2R2_VMX0_YUV_TO_RGB_601_STANDARD)
#define B2R2_VMX3_YUV_TO_BGR_601_STANDARD 0x2eb21f21

/* 709 Full range conversion matrix */
#define B2R2_VMX0_YUV_TO_BGR_709_FULL_RANGE (B2R2_VMX2_YUV_TO_RGB_709_FULL_RANGE)
#define B2R2_VMX1_YUV_TO_BGR_709_FULL_RANGE (B2R2_VMX1_YUV_TO_RGB_709_FULL_RANGE)
#define B2R2_VMX2_YUV_TO_BGR_709_FULL_RANGE (B2R2_VMX0_YUV_TO_RGB_709_FULL_RANGE)
#define B2R2_VMX3_YUV_TO_BGR_709_FULL_RANGE 0x31814b3b

/* 709 Standard (clamped) conversion matrix */
#define B2R2_VMX0_YUV_TO_BGR_709_STANDARD (B2R2_VMX2_YUV_TO_RGB_709_STANDARD)
#define B2R2_VMX1_YUV_TO_BGR_709_STANDARD (B2R2_VMX1_YUV_TO_RGB_709_STANDARD)
#define B2R2_VMX2_YUV_TO_BGR_709_STANDARD (B2R2_VMX0_YUV_TO_RGB_709_STANDARD)
#define B2R2_VMX3_YUV_TO_BGR_709_STANDARD 0x2df13307


/* VMX register values for YVU to RGB conversion */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_YVU_TO_RGB_601_FULL_RANGE 0x00040120
#define B2R2_VMX1_YVU_TO_RGB_601_FULL_RANGE 0xF544034D
#define B2R2_VMX2_YVU_TO_RGB_601_FULL_RANGE 0x37840000
#define B2R2_VMX3_YVU_TO_RGB_601_FULL_RANGE (B2R2_VMX3_YUV_TO_RGB_601_FULL_RANGE)

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_YVU_TO_RGB_601_STANDARD 0x0004A933
#define B2R2_VMX1_YVU_TO_RGB_601_STANDARD 0xF384AB30
#define B2R2_VMX2_YVU_TO_RGB_601_STANDARD 0x40A4A800
#define B2R2_VMX3_YVU_TO_RGB_601_STANDARD (B2R2_VMX3_YUV_TO_RGB_601_STANDARD)

/* VMX register values for RGB to YVU conversion */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_RGB_TO_YVU_601_FULL_RANGE (B2R2_VMX2_RGB_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX1_RGB_TO_YVU_601_FULL_RANGE (B2R2_VMX1_RGB_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX2_RGB_TO_YVU_601_FULL_RANGE (B2R2_VMX0_RGB_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX3_RGB_TO_YVU_601_FULL_RANGE (B2R2_VMX3_RGB_TO_YUV_601_FULL_RANGE)

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_RGB_TO_YVU_601_STANDARD (B2R2_VMX2_RGB_TO_YUV_601_STANDARD)
#define B2R2_VMX1_RGB_TO_YVU_601_STANDARD (B2R2_VMX1_RGB_TO_YUV_601_STANDARD)
#define B2R2_VMX2_RGB_TO_YVU_601_STANDARD (B2R2_VMX0_RGB_TO_YUV_601_STANDARD)
#define B2R2_VMX3_RGB_TO_YVU_601_STANDARD (B2R2_VMX3_RGB_TO_YUV_601_STANDARD)

/* VMX register values for YVU to BGR conversion */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_YVU_TO_BGR_601_FULL_RANGE (B2R2_VMX2_YVU_TO_RGB_601_FULL_RANGE)
#define B2R2_VMX1_YVU_TO_BGR_601_FULL_RANGE (B2R2_VMX1_YVU_TO_RGB_601_FULL_RANGE)
#define B2R2_VMX2_YVU_TO_BGR_601_FULL_RANGE (B2R2_VMX0_YVU_TO_RGB_601_FULL_RANGE)
#define B2R2_VMX3_YVU_TO_BGR_601_FULL_RANGE 0x3222134F

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_YVU_TO_BGR_601_STANDARD (B2R2_VMX2_YVU_TO_RGB_601_STANDARD)
#define B2R2_VMX1_YVU_TO_BGR_601_STANDARD (B2R2_VMX1_YVU_TO_RGB_601_STANDARD)
#define B2R2_VMX2_YVU_TO_BGR_601_STANDARD (B2R2_VMX0_YVU_TO_RGB_601_STANDARD)
#define B2R2_VMX3_YVU_TO_BGR_601_STANDARD 0x3222134F

/* VMX register values for BGR to YVU conversion */

/* 601 Full range conversion matrix */
#define B2R2_VMX0_BGR_TO_YVU_601_FULL_RANGE (B2R2_VMX2_BGR_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX1_BGR_TO_YVU_601_FULL_RANGE (B2R2_VMX1_BGR_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX2_BGR_TO_YVU_601_FULL_RANGE (B2R2_VMX0_BGR_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX3_BGR_TO_YVU_601_FULL_RANGE (B2R2_VMX3_BGR_TO_YUV_601_FULL_RANGE)

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_BGR_TO_YVU_601_STANDARD (B2R2_VMX2_BGR_TO_YUV_601_STANDARD)
#define B2R2_VMX1_BGR_TO_YVU_601_STANDARD (B2R2_VMX1_BGR_TO_YUV_601_STANDARD)
#define B2R2_VMX2_BGR_TO_YVU_601_STANDARD (B2R2_VMX0_BGR_TO_YUV_601_STANDARD)
#define B2R2_VMX3_BGR_TO_YVU_601_STANDARD (B2R2_VMX3_BGR_TO_YUV_601_STANDARD)

/* VMX register values for YVU to YUV conversion */

/* 601 Video Matrix (standard 601 conversion) */
/* Internally, the components are in fact stored
 * with luma in the middle, i.e. UYV, which is why
 * the values are just like for RGB->BGR conversion.
 */
#define B2R2_VMX0_YVU_TO_YUV 0x00000100
#define B2R2_VMX1_YVU_TO_YUV 0x00040000
#define B2R2_VMX2_YVU_TO_YUV 0x20000000
#define B2R2_VMX3_YVU_TO_YUV 0x00000000

/* VMX register values for RGB to BLT_YUV888 conversion */

/*
 * BLT_YUV888 has color components laid out in memory as V, U, Y, (Alpha)
 * with V at the first byte (due to little endian addressing).
 * B2R2 expects them to be as U, Y, V, (A)
 * with U at the first byte.
 * Note: RGB -> BLT_YUV888 values are calculated by multiplying
 * the RGB -> YUV matrix [A], with [S] to form [S]x[A] where
 *     |0 1 0|
 * S = |0 0 1|
 *     |1 0 0|
 * Essentially changing the order of rows in the original
 * matrix [A].
 * row1 -> row3
 * row2 -> row1
 * row3 -> row2
 * Values in the offset vector are swapped in the same manner.
 */
/* 601 Full range conversion matrix */
#define B2R2_VMX0_RGB_TO_BLT_YUV888_601_FULL_RANGE (B2R2_VMX1_RGB_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX1_RGB_TO_BLT_YUV888_601_FULL_RANGE (B2R2_VMX2_RGB_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX2_RGB_TO_BLT_YUV888_601_FULL_RANGE (B2R2_VMX0_RGB_TO_YUV_601_FULL_RANGE)
#define B2R2_VMX3_RGB_TO_BLT_YUV888_601_FULL_RANGE 0x00020080

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_RGB_TO_BLT_YUV888_601_STANDARD (B2R2_VMX1_RGB_TO_YUV_601_STANDARD)
#define B2R2_VMX1_RGB_TO_BLT_YUV888_601_STANDARD (B2R2_VMX2_RGB_TO_YUV_601_STANDARD)
#define B2R2_VMX2_RGB_TO_BLT_YUV888_601_STANDARD (B2R2_VMX0_RGB_TO_YUV_601_STANDARD)
#define B2R2_VMX3_RGB_TO_BLT_YUV888_601_STANDARD 0x00020080

/* VMX register values for BLT_YUV888 to RGB conversion */

/*
 * Note: BLT_YUV888 -> RGB values are calculated by multiplying
 * the YUV -> RGB matrix [A], with [S] to form [A]x[S] where
 *     |0 0 1|
 * S = |1 0 0|
 *     |0 1 0|
 * Essentially changing the order of columns in the original
 * matrix [A].
 * col1 -> col3
 * col2 -> col1
 * col3 -> col2
 * Values in the offset vector remain unchanged.
 */
/* 601 Full range conversion matrix */
#define B2R2_VMX0_BLT_YUV888_TO_RGB_601_FULL_RANGE 0x20000121
#define B2R2_VMX1_BLT_YUV888_TO_RGB_601_FULL_RANGE 0x201ea74c
#define B2R2_VMX2_BLT_YUV888_TO_RGB_601_FULL_RANGE 0x2006f000
#define B2R2_VMX3_BLT_YUV888_TO_RGB_601_FULL_RANGE (B2R2_VMX3_YUV_TO_RGB_601_FULL_RANGE)

/* 601 Standard (clamped) conversion matrix */
#define B2R2_VMX0_BLT_YUV888_TO_RGB_601_STANDARD 0x25400133
#define B2R2_VMX1_BLT_YUV888_TO_RGB_601_STANDARD 0x255E7330
#define B2R2_VMX2_BLT_YUV888_TO_RGB_601_STANDARD 0x25481400
#define B2R2_VMX3_BLT_YUV888_TO_RGB_601_STANDARD (B2R2_VMX3_YUV_TO_RGB_601_FULL_RANGE)

/* VMX register values for YUV to BLT_YUV888 conversion */
#define B2R2_VMX0_YUV_TO_BLT_YUV888 0x00040000
#define B2R2_VMX1_YUV_TO_BLT_YUV888 0x00000100
#define B2R2_VMX2_YUV_TO_BLT_YUV888 0x20000000
#define B2R2_VMX3_YUV_TO_BLT_YUV888 0x00000000

/* VMX register values for BLT_YUV888 to YUV conversion */
#define B2R2_VMX0_BLT_YUV888_TO_YUV 0x00000100
#define B2R2_VMX1_BLT_YUV888_TO_YUV 0x20000000
#define B2R2_VMX2_BLT_YUV888_TO_YUV 0x00040000
#define B2R2_VMX3_BLT_YUV888_TO_YUV 0x00000000

/* VMX register values for YVU to BLT_YUV888 conversion */
#define B2R2_VMX0_YVU_TO_BLT_YUV888 0x00040000
#define B2R2_VMX1_YVU_TO_BLT_YUV888 0x20000000
#define B2R2_VMX2_YVU_TO_BLT_YUV888 0x00000100
#define B2R2_VMX3_YVU_TO_BLT_YUV888 0x00000000

/* VMX register values for BLT_YUV888 to YVU conversion */
#define B2R2_VMX0_BLT_YUV888_TO_YVU 0x00040000
#define B2R2_VMX1_BLT_YUV888_TO_YVU 0x20000000
#define B2R2_VMX2_BLT_YUV888_TO_YVU 0x00000100
#define B2R2_VMX3_BLT_YUV888_TO_YVU 0x00000000

/*
 * Internal types
 */

/*
 * Global variables
 */

 /**
 * VMx values for color space conversion
 * (component swap)
 */
static const u32 vmx_yuv_to_blt_yuv888[] = {
	B2R2_VMX0_YUV_TO_BLT_YUV888,
	B2R2_VMX1_YUV_TO_BLT_YUV888,
	B2R2_VMX2_YUV_TO_BLT_YUV888,
	B2R2_VMX3_YUV_TO_BLT_YUV888,
};

static const u32 vmx_blt_yuv888_to_yuv[] = {
	B2R2_VMX0_BLT_YUV888_TO_YUV,
	B2R2_VMX1_BLT_YUV888_TO_YUV,
	B2R2_VMX2_BLT_YUV888_TO_YUV,
	B2R2_VMX3_BLT_YUV888_TO_YUV,
};

static const u32 vmx_yvu_to_blt_yuv888[] = {
	B2R2_VMX0_YVU_TO_BLT_YUV888,
	B2R2_VMX1_YVU_TO_BLT_YUV888,
	B2R2_VMX2_YVU_TO_BLT_YUV888,
	B2R2_VMX3_YVU_TO_BLT_YUV888,
};

static const u32 vmx_blt_yuv888_to_yvu[] = {
	B2R2_VMX0_BLT_YUV888_TO_YVU,
	B2R2_VMX1_BLT_YUV888_TO_YVU,
	B2R2_VMX2_BLT_YUV888_TO_YVU,
	B2R2_VMX3_BLT_YUV888_TO_YVU,
};

static const u32 vmx_rgb_to_bgr[] = {
	B2R2_VMX0_RGB_TO_BGR,
	B2R2_VMX1_RGB_TO_BGR,
	B2R2_VMX2_RGB_TO_BGR,
	B2R2_VMX3_RGB_TO_BGR,
};

static const u32 vmx_yvu_to_yuv[] = {
	B2R2_VMX0_YVU_TO_YUV,
	B2R2_VMX1_YVU_TO_YUV,
	B2R2_VMX2_YVU_TO_YUV,
	B2R2_VMX3_YVU_TO_YUV,
};

/**
 * VMx values for color space conversions
 * (standard 601 conversions)
 */
static const u32 vmx_rgb_to_yuv[] = {
	B2R2_VMX0_RGB_TO_YUV_601_STANDARD,
	B2R2_VMX1_RGB_TO_YUV_601_STANDARD,
	B2R2_VMX2_RGB_TO_YUV_601_STANDARD,
	B2R2_VMX3_RGB_TO_YUV_601_STANDARD,
};

static const u32 vmx_yuv_to_rgb[] = {
	B2R2_VMX0_YUV_TO_RGB_601_STANDARD,
	B2R2_VMX1_YUV_TO_RGB_601_STANDARD,
	B2R2_VMX2_YUV_TO_RGB_601_STANDARD,
	B2R2_VMX3_YUV_TO_RGB_601_STANDARD,
};

static const u32 vmx_rgb_to_blt_yuv888[] = {
	B2R2_VMX0_RGB_TO_BLT_YUV888_601_STANDARD,
	B2R2_VMX1_RGB_TO_BLT_YUV888_601_STANDARD,
	B2R2_VMX2_RGB_TO_BLT_YUV888_601_STANDARD,
	B2R2_VMX3_RGB_TO_BLT_YUV888_601_STANDARD,
};

static const u32 vmx_blt_yuv888_to_rgb[] = {
	B2R2_VMX0_BLT_YUV888_TO_RGB_601_STANDARD,
	B2R2_VMX1_BLT_YUV888_TO_RGB_601_STANDARD,
	B2R2_VMX2_BLT_YUV888_TO_RGB_601_STANDARD,
	B2R2_VMX3_BLT_YUV888_TO_RGB_601_STANDARD,
};

static const u32 vmx_rgb_to_yvu[] = {
	B2R2_VMX0_RGB_TO_YVU_601_STANDARD,
	B2R2_VMX1_RGB_TO_YVU_601_STANDARD,
	B2R2_VMX2_RGB_TO_YVU_601_STANDARD,
	B2R2_VMX3_RGB_TO_YVU_601_STANDARD,
};

static const u32 vmx_yvu_to_rgb[] = {
	B2R2_VMX0_YVU_TO_RGB_601_STANDARD,
	B2R2_VMX1_YVU_TO_RGB_601_STANDARD,
	B2R2_VMX2_YVU_TO_RGB_601_STANDARD,
	B2R2_VMX3_YVU_TO_RGB_601_STANDARD,
};

static const u32 vmx_bgr_to_yuv[] = {
	B2R2_VMX0_BGR_TO_YUV_601_STANDARD,
	B2R2_VMX1_BGR_TO_YUV_601_STANDARD,
	B2R2_VMX2_BGR_TO_YUV_601_STANDARD,
	B2R2_VMX3_BGR_TO_YUV_601_STANDARD,
};

static const u32 vmx_yuv_to_bgr[] = {
	B2R2_VMX0_YUV_TO_BGR_601_STANDARD,
	B2R2_VMX1_YUV_TO_BGR_601_STANDARD,
	B2R2_VMX2_YUV_TO_BGR_601_STANDARD,
	B2R2_VMX3_YUV_TO_BGR_601_STANDARD,
};

static const u32 vmx_bgr_to_yvu[] = {
	B2R2_VMX0_BGR_TO_YVU_601_STANDARD,
	B2R2_VMX1_BGR_TO_YVU_601_STANDARD,
	B2R2_VMX2_BGR_TO_YVU_601_STANDARD,
	B2R2_VMX3_BGR_TO_YVU_601_STANDARD,
};

static const u32 vmx_yvu_to_bgr[] = {
	B2R2_VMX0_YVU_TO_BGR_601_STANDARD,
	B2R2_VMX1_YVU_TO_BGR_601_STANDARD,
	B2R2_VMX2_YVU_TO_BGR_601_STANDARD,
	B2R2_VMX3_YVU_TO_BGR_601_STANDARD,
};

/**
 * VMx values for color space conversions
 * (full range conversions)
 */

static const u32 vmx_full_rgb_to_yuv[] = {
	B2R2_VMX0_RGB_TO_YUV_601_FULL_RANGE,
	B2R2_VMX1_RGB_TO_YUV_601_FULL_RANGE,
	B2R2_VMX2_RGB_TO_YUV_601_FULL_RANGE,
	B2R2_VMX3_RGB_TO_YUV_601_FULL_RANGE,
};

static const u32 vmx_full_yuv_to_rgb[] = {
	B2R2_VMX0_YUV_TO_RGB_601_FULL_RANGE,
	B2R2_VMX1_YUV_TO_RGB_601_FULL_RANGE,
	B2R2_VMX2_YUV_TO_RGB_601_FULL_RANGE,
	B2R2_VMX3_YUV_TO_RGB_601_FULL_RANGE,
};

static const u32 vmx_full_rgb_to_blt_yuv888[] = {
	B2R2_VMX0_RGB_TO_BLT_YUV888_601_FULL_RANGE,
	B2R2_VMX1_RGB_TO_BLT_YUV888_601_FULL_RANGE,
	B2R2_VMX2_RGB_TO_BLT_YUV888_601_FULL_RANGE,
	B2R2_VMX3_RGB_TO_BLT_YUV888_601_FULL_RANGE,
};

static const u32 vmx_full_blt_yuv888_to_rgb[] = {
	B2R2_VMX0_BLT_YUV888_TO_RGB_601_FULL_RANGE,
	B2R2_VMX1_BLT_YUV888_TO_RGB_601_FULL_RANGE,
	B2R2_VMX2_BLT_YUV888_TO_RGB_601_FULL_RANGE,
	B2R2_VMX3_BLT_YUV888_TO_RGB_601_FULL_RANGE,
};

static const u32 vmx_full_yvu_to_rgb[] = {
	B2R2_VMX0_YVU_TO_RGB_601_FULL_RANGE,
	B2R2_VMX1_YVU_TO_RGB_601_FULL_RANGE,
	B2R2_VMX2_YVU_TO_RGB_601_FULL_RANGE,
	B2R2_VMX3_YVU_TO_RGB_601_FULL_RANGE,
};

static const u32 vmx_full_rgb_to_yvu[] = {
	B2R2_VMX0_RGB_TO_YVU_601_FULL_RANGE,
	B2R2_VMX1_RGB_TO_YVU_601_FULL_RANGE,
	B2R2_VMX2_RGB_TO_YVU_601_FULL_RANGE,
	B2R2_VMX3_RGB_TO_YVU_601_FULL_RANGE,
};

static const u32 vmx_full_bgr_to_yuv[] = {
	B2R2_VMX0_BGR_TO_YUV_601_FULL_RANGE,
	B2R2_VMX1_BGR_TO_YUV_601_FULL_RANGE,
	B2R2_VMX2_BGR_TO_YUV_601_FULL_RANGE,
	B2R2_VMX3_BGR_TO_YUV_601_FULL_RANGE,
};

static const u32 vmx_full_yuv_to_bgr[] = {
	B2R2_VMX0_YUV_TO_BGR_601_FULL_RANGE,
	B2R2_VMX1_YUV_TO_BGR_601_FULL_RANGE,
	B2R2_VMX2_YUV_TO_BGR_601_FULL_RANGE,
	B2R2_VMX3_YUV_TO_BGR_601_FULL_RANGE,
};

static const u32 vmx_full_bgr_to_yvu[] = {
	B2R2_VMX0_BGR_TO_YVU_601_FULL_RANGE,
	B2R2_VMX1_BGR_TO_YVU_601_FULL_RANGE,
	B2R2_VMX2_BGR_TO_YVU_601_FULL_RANGE,
	B2R2_VMX3_BGR_TO_YVU_601_FULL_RANGE,
};

static const u32 vmx_full_yvu_to_bgr[] = {
	B2R2_VMX0_YVU_TO_BGR_601_FULL_RANGE,
	B2R2_VMX1_YVU_TO_BGR_601_FULL_RANGE,
	B2R2_VMX2_YVU_TO_BGR_601_FULL_RANGE,
	B2R2_VMX3_YVU_TO_BGR_601_FULL_RANGE,
};

/*
 * Forward declaration of private functions
 */

/*
 * Public functions
 */

/**
 * Setup input versatile matrix for color space conversion
 */
int b2r2_setup_ivmx(struct b2r2_node *node, enum b2r2_color_conversion cc)
{
	const u32 *vmx = NULL;

	if (b2r2_get_vmx(cc, &vmx) < 0 || vmx == NULL)
		return -1;

	node->node.GROUP0.B2R2_INS |= B2R2_INS_IVMX_ENABLED;
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_IVMX;

	node->node.GROUP15.B2R2_VMX0 = vmx[0];
	node->node.GROUP15.B2R2_VMX1 = vmx[1];
	node->node.GROUP15.B2R2_VMX2 = vmx[2];
	node->node.GROUP15.B2R2_VMX3 = vmx[3];

	return 0;
}

/**
 * Setup output versatile matrix for color space conversion
 */
int b2r2_setup_ovmx(struct b2r2_node *node, enum b2r2_color_conversion cc)
{
	const u32 *vmx = NULL;

	if (b2r2_get_vmx(cc, &vmx) < 0 || vmx == NULL)
		return -1;

	node->node.GROUP0.B2R2_INS |= B2R2_INS_OVMX_ENABLED;
	node->node.GROUP0.B2R2_CIC |= B2R2_CIC_OVMX;

	node->node.GROUP16.B2R2_VMX0 = vmx[0];
	node->node.GROUP16.B2R2_VMX1 = vmx[1];
	node->node.GROUP16.B2R2_VMX2 = vmx[2];
	node->node.GROUP16.B2R2_VMX3 = vmx[3];

	return 0;
}

enum b2r2_color_conversion b2r2_get_color_conversion(enum b2r2_blt_fmt src_fmt,
		enum b2r2_blt_fmt dst_fmt, bool fullrange)
{
	if (b2r2_is_rgb_fmt(src_fmt)) {
		if (b2r2_is_yvu_fmt(dst_fmt))
			return fullrange ? B2R2_CC_RGB_TO_YVU_FULL :
				B2R2_CC_RGB_TO_YVU;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			/*
			 * (A)YUV/VUY(A) formats differ only in component
			 * order. This is handled by the endianness bit
			 * in B2R2_STY/TTY registers when src/target are set.
			 */
			return fullrange ? B2R2_CC_RGB_TO_BLT_YUV888_FULL :
				B2R2_CC_RGB_TO_BLT_YUV888;
		else if (b2r2_is_yuv_fmt(dst_fmt))
			return fullrange ? B2R2_CC_RGB_TO_YUV_FULL :
				B2R2_CC_RGB_TO_YUV;
		else if (b2r2_is_bgr_fmt(dst_fmt))
			return B2R2_CC_RGB_TO_BGR;
	} else if (b2r2_is_yvu_fmt(src_fmt)) {
		if (b2r2_is_rgb_fmt(dst_fmt))
			return fullrange ? B2R2_CC_YVU_FULL_TO_RGB :
				B2R2_CC_YVU_TO_RGB;
		else if (b2r2_is_bgr_fmt(dst_fmt))
			return fullrange ? B2R2_CC_YVU_FULL_TO_BGR :
				B2R2_CC_YVU_TO_BGR;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			return B2R2_CC_YVU_TO_BLT_YUV888;
		else if (b2r2_is_yuv_fmt(dst_fmt) &&
				!b2r2_is_yvu_fmt(dst_fmt))
			return B2R2_CC_YVU_TO_YUV;
	} else if (src_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
			src_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
			src_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
			src_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888) {
		/*
		 * (A)YUV/VUY(A) formats differ only in component
		 * order. This is handled by the endianness bit
		 * in B2R2_STY/TTY registers when src/target are set.
		 */
		if (b2r2_is_rgb_fmt(dst_fmt))
			return fullrange ? B2R2_CC_BLT_YUV888_FULL_TO_RGB :
				B2R2_CC_BLT_YUV888_TO_RGB;
		else if (b2r2_is_yvu_fmt(dst_fmt))
			return B2R2_CC_BLT_YUV888_TO_YVU;
		else if (b2r2_is_yuv_fmt(dst_fmt)) {
			switch (dst_fmt) {
			case B2R2_BLT_FMT_24_BIT_YUV888:
			case B2R2_BLT_FMT_32_BIT_AYUV8888:
			case B2R2_BLT_FMT_24_BIT_VUY888:
			case B2R2_BLT_FMT_32_BIT_VUYA8888:
				return B2R2_CC_NOTHING;
			default:
				return B2R2_CC_BLT_YUV888_TO_YUV;
			}
		}
	} else if (b2r2_is_yuv_fmt(src_fmt)) {
		if (b2r2_is_rgb_fmt(dst_fmt))
			return fullrange ? B2R2_CC_YUV_FULL_TO_RGB :
				B2R2_CC_YUV_TO_RGB;
		else if (b2r2_is_bgr_fmt(dst_fmt))
			return fullrange ? B2R2_CC_YUV_FULL_TO_BGR :
				B2R2_CC_YUV_TO_BGR;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			return B2R2_CC_YUV_TO_BLT_YUV888;
		else if (b2r2_is_yvu_fmt(dst_fmt))
			return B2R2_CC_YVU_TO_YUV;
	} else if (b2r2_is_bgr_fmt(src_fmt)) {
		if (b2r2_is_rgb_fmt(dst_fmt))
			return B2R2_CC_RGB_TO_BGR;
		else if (b2r2_is_yvu_fmt(dst_fmt))
			return fullrange ? B2R2_CC_BGR_TO_YVU_FULL :
				B2R2_CC_BGR_TO_YVU;
		else if (dst_fmt == B2R2_BLT_FMT_24_BIT_YUV888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_AYUV8888 ||
				dst_fmt == B2R2_BLT_FMT_24_BIT_VUY888 ||
				dst_fmt == B2R2_BLT_FMT_32_BIT_VUYA8888)
			BUG_ON(1);
		else if (b2r2_is_yuv_fmt(dst_fmt))
			return fullrange ? B2R2_CC_BGR_TO_YUV_FULL :
				B2R2_CC_BGR_TO_YUV;
	}

	return B2R2_CC_NOTHING;
}

int b2r2_get_vmx(enum b2r2_color_conversion cc, const u32 **vmx)
{
	if (vmx == NULL)
		return -1;

	switch (cc) {
	case B2R2_CC_RGB_TO_BGR:
		*vmx = &vmx_rgb_to_bgr[0];
		break;
	case B2R2_CC_BLT_YUV888_TO_YVU:
		*vmx = &vmx_blt_yuv888_to_yvu[0];
		break;
	case B2R2_CC_BLT_YUV888_TO_YUV:
		*vmx = &vmx_blt_yuv888_to_yuv[0];
		break;
	case B2R2_CC_YVU_TO_YUV:
		*vmx = &vmx_yvu_to_yuv[0];
		break;
	case B2R2_CC_YVU_TO_BLT_YUV888:
		*vmx = &vmx_yvu_to_blt_yuv888[0];
		break;
	case B2R2_CC_YUV_TO_BLT_YUV888:
		*vmx = &vmx_yuv_to_blt_yuv888[0];
		break;
	case B2R2_CC_RGB_TO_YUV:
		*vmx = &vmx_rgb_to_yuv[0];
		break;
	case B2R2_CC_RGB_TO_YUV_FULL:
		*vmx = &vmx_full_rgb_to_yuv[0];
		break;
	case B2R2_CC_RGB_TO_YVU:
		*vmx = &vmx_rgb_to_yvu[0];
		break;
	case B2R2_CC_RGB_TO_YVU_FULL:
		*vmx = &vmx_full_rgb_to_yvu[0];
		break;
	case B2R2_CC_RGB_TO_BLT_YUV888:
		*vmx = &vmx_rgb_to_blt_yuv888[0];
		break;
	case B2R2_CC_RGB_TO_BLT_YUV888_FULL:
		*vmx = &vmx_full_rgb_to_blt_yuv888[0];
		break;
	case B2R2_CC_BGR_TO_YVU:
		*vmx = &vmx_bgr_to_yvu[0];
		break;
	case B2R2_CC_BGR_TO_YVU_FULL:
		*vmx = &vmx_full_bgr_to_yvu[0];
		break;
	case B2R2_CC_BGR_TO_YUV:
		*vmx = &vmx_bgr_to_yuv[0];
		break;
	case B2R2_CC_BGR_TO_YUV_FULL:
		*vmx = &vmx_full_bgr_to_yuv[0];
		break;
	case B2R2_CC_YUV_TO_RGB:
		*vmx = &vmx_yuv_to_rgb[0];
		break;
	case B2R2_CC_YUV_FULL_TO_RGB:
		*vmx = &vmx_full_yuv_to_rgb[0];
		break;
	case B2R2_CC_YUV_TO_BGR:
		*vmx = &vmx_yuv_to_bgr[0];
		break;
	case B2R2_CC_YUV_FULL_TO_BGR:
		*vmx = &vmx_full_yuv_to_bgr[0];
		break;
	case B2R2_CC_YVU_TO_RGB:
		*vmx = &vmx_yvu_to_rgb[0];
		break;
	case B2R2_CC_YVU_FULL_TO_RGB:
		*vmx = &vmx_full_yvu_to_rgb[0];
		break;
	case B2R2_CC_YVU_TO_BGR:
		*vmx = &vmx_yvu_to_bgr[0];
		break;
	case B2R2_CC_YVU_FULL_TO_BGR:
		*vmx = &vmx_full_yvu_to_bgr[0];
		break;
	case B2R2_CC_BLT_YUV888_TO_RGB:
		*vmx = &vmx_blt_yuv888_to_rgb[0];
		break;
	case B2R2_CC_BLT_YUV888_FULL_TO_RGB:
		*vmx = &vmx_full_blt_yuv888_to_rgb[0];
		break;
	case B2R2_CC_NOTHING:
	default:
		break;
	}

	return 0;
}


