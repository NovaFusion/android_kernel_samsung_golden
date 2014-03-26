/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson header for MCDE globals and function prototypes
 *
 * Author: Craig Sutherland <craig.sutherland@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __MCDE_HW__H__
#define __MCDE_HW__H__

extern u8 *mcdeio;
extern u8 num_channels;
extern u8 num_overlays;
extern int mcde_irq;
extern u32 input_fifo_size;
extern u32 output_fifo_ab_size;
extern u32 output_fifo_c0c1_size;
extern u8 mcde_is_enabled;
extern u8 dsi_pll_is_enabled;
extern u8 dsi_ifc_is_supported;
extern u8 dsi_use_clk_framework;
extern u32 mcde_clk_rate;
extern u8 hw_alignment;
extern u8 mcde_dynamic_power_management;

u32 mcde_rreg(u32 reg);
void mcde_wreg(u32 reg, u32 val);

#endif /* __MCDE_HW__H__ */
