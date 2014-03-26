/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Roger Nilsson roger.xr.nilsson@stericsson.com
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#ifndef CG29XX_CODEC_H
#define CG29XX_CODEC_H

#include <../../../drivers/staging/cg2900/include/cg2900_audio.h>

struct cg29xx_codec_dai_data {
	struct mutex mutex;
	unsigned int rx_active;
	unsigned int tx_active;
	int input_select;
	int output_select;
	struct cg2900_dai_config config;
};

struct cg29xx_codec{
	unsigned int session;
};

#define CG29XX_DAI_SLOT0_SHIFT	0
#define CG29XX_DAI_SLOT1_SHIFT	1
#define CG29XX_DAI_SLOT2_SHIFT	2
#define CG29XX_DAI_SLOT3_SHIFT	3

#define INTERFACE0_INPUT_SELECT 0x00
#define INTERFACE1_INPUT_SELECT 0x01
#define INTERFACE0_OUTPUT_SELECT 0x02
#define INTERFACE1_OUTPUT_SELECT 0x03

#endif /* CG29XX_CODEC_H */
