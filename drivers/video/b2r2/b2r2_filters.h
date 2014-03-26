/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 filters.
 *
 * Author: Fredrik Allansson <fredrik.allansson@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _LINUX_VIDEO_B2R2_FILTERS_H
#define _LINUX_VIDEO_B2R2_FILTERS_H

#include <linux/kernel.h>

#include "b2r2_internal.h"

#define B2R2_HF_TABLE_SIZE 64
#define B2R2_VF_TABLE_SIZE 40

/**
 * @struct b2r2_filter_spec - Filter specification structure
 *
 *   @param min - Minimum scale factor for this filter (in 6.10 fixed point)
 *   @param max - Maximum scale factor for this filter (in 6.10 fixed point)
 *   @param h_coeffs - Horizontal filter coefficients
 *   @param v_coeffs - Vertical filter coefficients
 *   @param h_coeffs_dma_addr - Virtual DMA address for horizontal coefficients
 *   @param v_coeffs_dma_addr - Virtual DMA address for vertical coefficients
 *   @param h_coeffs_phys_addr - Physical address for horizontal coefficients
 *   @param v_coeffs_phys_addr - Physical address for vertical coefficients
 */
struct b2r2_filter_spec {
	const u16 min;
	const u16 max;

	const u8 h_coeffs[B2R2_HF_TABLE_SIZE];
	const u8 v_coeffs[B2R2_VF_TABLE_SIZE];

	void *h_coeffs_dma_addr;
	u32 h_coeffs_phys_addr;

	void *v_coeffs_dma_addr;
	u32 v_coeffs_phys_addr;
};

/**
 * b2r2_filters_init() - Initilizes the B2R2 filters
 */
int b2r2_filters_init(struct b2r2_control *control);

/**
 * b2r2_filters_init() - De-initilizes the B2R2 filters
 */
void b2r2_filters_exit(struct b2r2_control *control);

/**
 * b2r2_filter_find() - Find a filter matching the given scale factor
 *
 *   @param scale_factor - Scale factor to find a filter for
 *
 * Returns NULL if no filter could be found.
 */
struct b2r2_filter_spec *b2r2_filter_find(u16 scale_factor);

/**
 * b2r2_filter_blur() - Returns the blur filter
 *
 * Returns NULL if no blur filter is available.
 */
struct b2r2_filter_spec *b2r2_filter_blur(void);

#endif /* _LINUX_VIDEO_B2R2_FILTERS_H */
