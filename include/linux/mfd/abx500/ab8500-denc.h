/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * AB8500 tvout driver interface
 *
 * Author: Marcel Tunnissen <marcel.tuennissen@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __AB8500_DENC__H__
#define __AB8500_DENC__H__

#include <linux/platform_device.h>

struct ab8500_denc_platform_data {
	/* Platform info */
	bool ddr_enable;
	bool ddr_little_endian;
};

enum ab8500_denc_TV_std {
	TV_STD_PAL_BDGHI,
	TV_STD_PAL_N,
	TV_STD_PAL_M,
	TV_STD_NTSC_M,
};

enum ab8500_denc_cr_filter_bandwidth {
	TV_CR_NTSC_LOW_DEF_FILTER,
	TV_CR_PAL_LOW_DEF_FILTER,
	TV_CR_NTSC_HIGH_DEF_FILTER,
	TV_CR_PAL_HIGH_DEF_FILTER,
};

enum ab8500_denc_phase_reset_mode {
	TV_PHASE_RST_MOD_DISABLE,
	TV_PHASE_RST_MOD_FROM_PHASE_BUF,
	TV_PHASE_RST_MOD_FROM_INC_DFS,
	TV_PHASE_RST_MOD_RST,
};

enum ab8500_denc_plug_time {
	TV_PLUG_TIME_0_5S,
	TV_PLUG_TIME_1S,
	TV_PLUG_TIME_1_5S,
	TV_PLUG_TIME_2S,
	TV_PLUG_TIME_2_5S,
	TV_PLUG_TIME_3S,
};

struct ab8500_denc_conf {
	/* register settings for DENC_configuration */
	bool					act_output;
	enum ab8500_denc_TV_std			TV_std;
	bool					progressive;
	bool					test_pattern;
	bool					partial_blanking;
	bool					blank_all;
	bool					black_level_setup;
	enum ab8500_denc_cr_filter_bandwidth	cr_filter;
	bool					suppress_col;
	enum ab8500_denc_phase_reset_mode	phase_reset_mode;
	bool					dac_enable;
	bool					act_dc_output;
};

struct platform_device *ab8500_denc_get_device(void);
void ab8500_denc_put_device(struct platform_device *pdev);

void ab8500_denc_reset(struct platform_device *pdev, bool hard);
void ab8500_denc_power_up(struct platform_device *pdev);
void ab8500_denc_power_down(struct platform_device *pdev);

void ab8500_denc_conf(struct platform_device *pdev,
						struct ab8500_denc_conf *conf);
void ab8500_denc_conf_plug_detect(struct platform_device *pdev,
					bool enable, bool load_RC,
					enum ab8500_denc_plug_time time);
void ab8500_denc_mask_int_plug_det(struct platform_device *pdev, bool plug,
								bool unplug);
#endif /* __AB8500_DENC__H__ */
