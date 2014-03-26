/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Xie Xiaolei (xie.xiaolei@stericsson.com)
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef UX500_AB5500_H
#define UX500_AB5500_H

struct snd_soc_pcm_runtime;

int ux500_ab5500_startup(struct snd_pcm_substream *substream);

void ux500_ab5500_shutdown(struct snd_pcm_substream *substream);

int ux500_ab5500_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params);

int ux500_ab5500_machine_codec_init(struct snd_soc_pcm_runtime *runtime);

#endif
