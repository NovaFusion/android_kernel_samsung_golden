/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Jarmo K. Kuronen <jarmo.kuronen@symbio.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef UX500_AB8500_H
#define UX500_AB8500_H

extern struct snd_soc_ops ux500_ab8500_ops[];

struct snd_soc_pcm_runtime;

int ux500_ab8500_startup(struct snd_pcm_substream *substream);

void ux500_ab8500_shutdown(struct snd_pcm_substream *substream);

int ux500_ab8500_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params);

int ux500_ab8500_soc_machine_drv_init(void);

void ux500_ab8500_soc_machine_drv_cleanup(void);

int ux500_ab8500_machine_codec_init(struct snd_soc_pcm_runtime *runtime);

extern void ux500_ab8500_jack_report(int);

#endif
