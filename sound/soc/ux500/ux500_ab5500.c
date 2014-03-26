/*
 * Copyright (C) ST-Ericsson SA 2011
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
#include <linux/clk.h>
#include "../codecs/ab5500.h"
#include "ux500_msp_dai.h"

/* For a workwround purpose we enable sysclk
   by default this will be changed later */
static unsigned int sysclk_state = 1;/* Enabled */
static struct clk *ux500_ab5500_sysclk;

static int sysclk_input_select_control_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int sysclk_input_select_control_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = sysclk_state;
	return 0;
}

static int sysclk_input_select_control_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	sysclk_state = ucontrol->value.integer.value[0];
	return 0;
}

static const struct snd_kcontrol_new sysclk_input_select_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Sysclk Input Select",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = sysclk_input_select_control_info,
	.get = sysclk_input_select_control_get,
	.put = sysclk_input_select_control_put
};

int ux500_ab5500_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	int ret = 0;

	if (sysclk_state == 1) {
		ret = clk_enable(ux500_ab5500_sysclk);
	if (ret)
		dev_err(codec->dev, "failed to enable clock %d\n", ret);
	}

	return ret;
}

void ux500_ab5500_shutdown(struct snd_pcm_substream *substream)
{
	pr_info("%s: Enter.\n", __func__);
	if (sysclk_state == 1)
		clk_disable(ux500_ab5500_sysclk);
}

int ux500_ab5500_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	int channels = params_channels(params);

	printk(KERN_DEBUG "%s: Enter.\n", __func__);
	printk(KERN_DEBUG "%s: substream->pcm->name = %s.\n", __func__, substream->pcm->name);
	printk(KERN_DEBUG "%s: substream->pcm->id = %s.\n", __func__, substream->pcm->id);
	printk(KERN_DEBUG "%s: substream->name = %s.\n", __func__, substream->name);
	printk(KERN_DEBUG "%s: substream->number = %d.\n", __func__, substream->number);
	printk(KERN_DEBUG "%s: channels = %d.\n", __func__, channels);
	printk(KERN_DEBUG "%s: DAI-index (Codec): %d\n", __func__, codec_dai->id);
	printk(KERN_DEBUG "%s: DAI-index (Platform): %d\n", __func__, cpu_dai->id);

	ret = snd_soc_dai_set_fmt(codec_dai,
		SND_SOC_DAIFMT_I2S |  SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_fmt(cpu_dai,
		SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_CBM_CFM |
		SND_SOC_DAIFMT_NB_NF);
	if (ret < 0)
		return ret;
	ux500_msp_dai_set_data_delay(cpu_dai, MSP_DELAY_1);

	return ret;
}

int ux500_ab5500_machine_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	int ret = 0;

	snd_ctl_add(codec->card->snd_card,
		snd_ctl_new1(&sysclk_input_select_control, codec));

	ux500_ab5500_sysclk = clk_get(codec->dev, "sysclk");
	if (IS_ERR(ux500_ab5500_sysclk)) {
		dev_err(codec->dev, "could not get sysclk %ld\n",
			PTR_ERR(ux500_ab5500_sysclk));
		ret = PTR_ERR(ux500_ab5500_sysclk);
	}

	return ret;
}
