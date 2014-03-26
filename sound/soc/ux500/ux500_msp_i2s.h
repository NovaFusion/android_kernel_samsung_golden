/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */


#ifndef UX500_MSP_I2S_H
#define UX500_MSP_I2S_H

#include <linux/platform_device.h>
#include <mach/msp.h>
#include "ux500_pcm.h"

struct ux500_msp_i2s_drvdata {
	int id;
	struct msp *msp;
	struct regulator *reg_vape;
	struct ux500_pcm_dma_params playback_dma_data;
	struct ux500_pcm_dma_params capture_dma_data;
};

struct ux500_msp_i2s_drvdata *ux500_msp_i2s_init(struct platform_device *pdev,
						struct msp_i2s_platform_data *platform_data);
int ux500_msp_i2s_exit(struct ux500_msp_i2s_drvdata *drvdata);
int ux500_msp_i2s_open(struct ux500_msp_i2s_drvdata *drvdata, struct msp_config *msp_config);
int ux500_msp_i2s_close(struct ux500_msp_i2s_drvdata *drvdata, enum i2s_flag flag);
int ux500_msp_i2s_trigger(struct ux500_msp_i2s_drvdata *drvdata, int cmd, int direction);
int ux500_msp_i2s_suspend(struct ux500_msp_i2s_drvdata *drvdata);
int ux500_msp_i2s_resume(struct ux500_msp_i2s_drvdata *drvdata);

#endif

