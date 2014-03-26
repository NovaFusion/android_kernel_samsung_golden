/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <sound/soc.h>
#include "../codecs/cg29xx.h"
#include "ux500_msp_dai.h"

#define UX500_CG29XX_MSP_CLOCK_FREQ	19200000
#define U5500_CG29XX_MSP_CLOCK_FREQ 13000000
#define UX500_CG29XX_DAI_SLOT_WIDTH	16
#define UX500_CG29XX_DAI_SLOTS	2
#define UX500_CG29XX_DAI_ACTIVE_SLOTS	0x02

int ux500_cg29xx_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int channels = params_channels(params);
	int err;

	pr_debug("%s: Enter.\n", __func__);
	pr_debug("%s: substream->pcm->name = %s.\n", __func__, substream->pcm->name);
	pr_debug("%s: substream->pcm->id = %s.\n", __func__, substream->pcm->id);
	pr_debug("%s: substream->name = %s.\n", __func__, substream->name);
	pr_debug("%s: substream->number = %d.\n", __func__, substream->number);
	pr_debug("%s: channels = %d.\n", __func__, channels);
	pr_debug("%s: DAI-index (Codec): %d\n", __func__, codec_dai->id);
	pr_debug("%s: DAI-index (Platform): %d\n", __func__, cpu_dai->id);

	err = snd_soc_dai_set_fmt(codec_dai,
				SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS);

	if (err) {
		pr_err("%s: snd_soc_dai_set_fmt(codec) failed with %d.\n",
			__func__,
			err);
		goto out_err;
	}

	err = snd_soc_dai_set_tdm_slot(codec_dai,
				1 << CG29XX_DAI_SLOT0_SHIFT,
				1 << CG29XX_DAI_SLOT0_SHIFT,
				UX500_CG29XX_DAI_SLOTS,
				UX500_CG29XX_DAI_SLOT_WIDTH);

	if (err) {
		pr_err("%s: cg29xx_set_tdm_slot(codec_dai) failed with %d.\n",
				__func__,
				err);
		goto out_err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai,
				SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS |
				SND_SOC_DAIFMT_NB_NF);

	if (err) {
		pr_err("%s: snd_soc_dai_set_fmt(cpu_dai) failed with %d.\n",
			__func__,
			err);
		goto out_err;
	}

	err = snd_soc_dai_set_sysclk(cpu_dai,
		UX500_MSP_MASTER_CLOCK,
		UX500_CG29XX_MSP_CLOCK_FREQ,
		0);

	if (err) {
		pr_err("%s: snd_soc_dai_set_sysclk(cpu_dai) failed with %d.\n",
			__func__,
			err);
		goto out_err;
	}

	err = snd_soc_dai_set_tdm_slot(cpu_dai,
			UX500_CG29XX_DAI_ACTIVE_SLOTS,
			UX500_CG29XX_DAI_ACTIVE_SLOTS,
			UX500_CG29XX_DAI_SLOTS,
			UX500_CG29XX_DAI_SLOT_WIDTH);

	if (err) {
		pr_err("%s: cg29xx_set_tdm_slot(cpu_dai) failed with %d.\n",
			__func__,
			err);
		goto out_err;
	}

out_err:
	return err;
}

int u5500_cg29xx_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)

{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int channels = params_channels(params);
	int err;
	struct snd_soc_codec *codec = codec_dai->codec;
	int dai_id = codec_dai->id;
	struct cg29xx_codec_dai_data *codec_drvdata =
				snd_soc_codec_get_drvdata(codec);
	struct cg29xx_codec_dai_data *dai_data = &codec_drvdata[dai_id];

	pr_debug("%s: Enter.\n", __func__);
	pr_debug("%s: substream->pcm->name=%s\n",
						__func__, substream->pcm->name);
	pr_debug("%s: substream->pcm->id = %s\n", __func__, substream->pcm->id);
	pr_debug("%s: substream->name = %s.\n", __func__, substream->name);
	pr_debug("%s: substream->number = %d.\n", __func__, substream->number);
	pr_debug("%s: channels = %d.\n", __func__, channels);
	pr_debug("%s: DAI-index (Codec): %d\n", __func__, codec_dai->id);
	pr_debug("%s: DAI-index (Platform): %d\n", __func__, cpu_dai->id);

	if (dai_data->config.port == PORT_0_I2S) {
		err = snd_soc_dai_set_fmt(codec_dai,
				SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS);
		if (err) {
			pr_err("%s: snd_soc_dai_set_fmt (codec) failed with %d.\n",
				__func__,
				err);
			goto out_err;
		}

		err = snd_soc_dai_set_fmt(cpu_dai,
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS);

		if (err) {
			pr_err("%s: snd_soc_dai_set_sysclk(cpu_dai) failed with %d.\n",
				__func__,
				err);
			goto out_err;
		}
	}	else {
		err = snd_soc_dai_set_fmt(codec_dai,
			SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS);
		if (err) {
			pr_err("%s: snd_soc_dai_set_fmt(codec) failed with %d.\n",
				__func__,
				err);
			goto out_err;
			}

		err = snd_soc_dai_set_tdm_slot(codec_dai,
					1 << CG29XX_DAI_SLOT0_SHIFT,
					1 << CG29XX_DAI_SLOT0_SHIFT,
					UX500_CG29XX_DAI_SLOTS,
					UX500_CG29XX_DAI_SLOT_WIDTH);

		if (err) {
			pr_err("%s: cg29xx_set_tdm_slot(codec_dai) failed with %d.\n",
					__func__,
					err);
			goto out_err;
		}

		err = snd_soc_dai_set_fmt(cpu_dai,
					SND_SOC_DAIFMT_DSP_B |
					SND_SOC_DAIFMT_CBS_CFS |
					SND_SOC_DAIFMT_NB_NF);

		if (err) {
			pr_err("%s: snd_soc_dai_set_fmt(cpu_dai) failed with %d.\n",
				__func__,
				err);
			goto out_err;
		}

		err = snd_soc_dai_set_sysclk(cpu_dai,
			UX500_MSP_MASTER_CLOCK,
			U5500_CG29XX_MSP_CLOCK_FREQ,
			0);

		if (err) {
			pr_err("%s: snd_soc_dai_set_sysclk(cpu_dai) failed with %d.\n",
				__func__,
				err);
			goto out_err;
		}

		err = snd_soc_dai_set_tdm_slot(cpu_dai,
					UX500_CG29XX_DAI_ACTIVE_SLOTS,
					UX500_CG29XX_DAI_ACTIVE_SLOTS,
					UX500_CG29XX_DAI_SLOTS,
					UX500_CG29XX_DAI_SLOT_WIDTH);

		if (err) {
			pr_err("%s: cg29xx_set_tdm_slot(cpu_dai) failed with %d.\n",
				__func__,
				err);
			goto out_err;
		}

	}
	ux500_msp_dai_set_data_delay(cpu_dai, MSP_DELAY_0);
out_err:
	return err;
}

struct snd_soc_ops ux500_cg29xx_ops[] = {
	{
		.hw_params = ux500_cg29xx_hw_params,
	}
};

struct snd_soc_ops u5500_cg29xx_ops[] = {
	{
		.hw_params = u5500_cg29xx_hw_params,
	}
};

