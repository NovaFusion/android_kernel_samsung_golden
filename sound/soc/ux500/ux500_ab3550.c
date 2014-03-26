/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Ola Lilja ola.o.lilja@stericsson.com,
 *         Roger Nilsson roger.xr.nilsson@stericsson.com
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <sound/soc.h>
#include "../codecs/ab3550.h"

static int ux500_ab3550_startup(struct snd_pcm_substream *substream)
{
	pr_debug("%s: Enter.\n", __func__);

	return 0;
}

static void ux500_ab3550_shutdown(struct snd_pcm_substream *substream)
{
	pr_debug("%s: Enter.\n", __func__);
}

static int ux500_ab3550_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	int channels = params_channels(params);

	pr_debug("%s: Enter.\n", __func__);
	pr_debug("%s: substream->pcm->name = %s.\n", __func__, substream->pcm->name);
	pr_debug("%s: substream->pcm->id = %s.\n", __func__, substream->pcm->id);
	pr_debug("%s: substream->name = %s.\n", __func__, substream->name);
	pr_debug("%s: substream->number = %d.\n", __func__, substream->number);
	pr_debug("%s: channels = %d.\n", __func__, channels);
	pr_debug("%s: DAI-index (Codec): %d\n", __func__, codec_dai->id);
	pr_debug("%s: DAI-index (Platform): %d\n", __func__, cpu_dai->id);

	ret = snd_soc_dai_set_fmt(codec_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		pr_debug("%s: snd_soc_dai_set_fmt failed with %d.\n",
			__func__,
			ret);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		pr_debug("%s: snd_soc_dai_set_fmt failed with %d.\n",
			__func__,
			ret);
		return ret;
	}

	return ret;
}

struct snd_soc_ops ux500_ab3550_ops[] = {
	{
		.startup = ux500_ab3550_startup,
		.shutdown = ux500_ab3550_shutdown,
		.hw_params = ux500_ab3550_hw_params,
	}
};
