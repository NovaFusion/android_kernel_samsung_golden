/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <sound/soc.h>
#include "../codecs/av8100_audio.h"
#include "ux500_av8100.h"
#include "ux500_msp_dai.h"

static const char *stream_str(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return "Playback";
	else
		return "Capture";
}

static int ux500_av8100_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int channels = params_channels(params);
	unsigned int tx_mask, fmt;
	enum hdmi_channel_allocation hdmi_ca;
	enum hdmi_audio_channel_count hdmi_cc;
	struct hdmi_audio_settings as;
	int ret;

	pr_debug("%s: Enter (%s).\n", __func__, stream_str(substream));
	pr_debug("%s: substream->pcm->name = %s.\n", __func__, substream->pcm->name);
	pr_debug("%s: substream->pcm->id = %s.\n", __func__, substream->pcm->id);
	pr_debug("%s: substream->name = %s.\n", __func__, substream->name);
	pr_debug("%s: substream->number = %d.\n", __func__, substream->number);
	pr_debug("%s: channels = %d.\n", __func__, channels);
	pr_debug("%s: DAI-index (Codec): %d\n", __func__, codec_dai->id);
	pr_debug("%s: DAI-index (Platform): %d\n", __func__, cpu_dai->id);

	switch (channels) {
	case 1:
		hdmi_cc = AV8100_CODEC_CC_2CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR; /* Stereo-setup */
		tx_mask = AV8100_CODEC_MASK_MONO;
		break;
	case 2:
		hdmi_cc = AV8100_CODEC_CC_2CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR; /* Stereo */
		tx_mask = AV8100_CODEC_MASK_STEREO;
		break;
	case 3:
		hdmi_cc = AV8100_CODEC_CC_6CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR; /* 5.1-setup */
		tx_mask = AV8100_CODEC_MASK_2DOT1;
		break;
	case 4:
		hdmi_cc = AV8100_CODEC_CC_6CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR; /* 5.1-setup */
		tx_mask = AV8100_CODEC_MASK_QUAD;
		break;
	case 5:
		hdmi_cc = AV8100_CODEC_CC_6CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR; /* 5.1-setup */
		tx_mask = AV8100_CODEC_MASK_5DOT0;
		break;
	case 6:
		hdmi_cc = AV8100_CODEC_CC_6CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR; /* 5.1 */
		tx_mask = AV8100_CODEC_MASK_5DOT1;
		break;
	case 7:
		hdmi_cc = AV8100_CODEC_CC_8CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR_RLC_RRC; /* 7.1 */
		tx_mask = AV8100_CODEC_MASK_7DOT0;
		break;
	case 8:
		hdmi_cc = AV8100_CODEC_CC_8CH;
		hdmi_ca = AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR_RLC_RRC; /* 7.1 */
		tx_mask = AV8100_CODEC_MASK_7DOT1;
		break;
	default:
		pr_err("%s: Unsupported number of channels (channels = %d)!\n",
			__func__,
			channels);
		return -EINVAL;
	}

	/* Change HDMI audio-settings for codec-DAI. */
	pr_debug("%s: Change HDMI audio-settings for codec-DAI.\n", __func__);
	as.audio_channel_count = hdmi_cc;
	as.sampling_frequency = AV8100_CODEC_SF_REFER;
	as.sample_size = AV8100_CODEC_SS_REFER;
	as.channel_allocation = hdmi_ca;
	as.level_shift_value = AV8100_CODEC_LSV_0DB;
	as.downmix_inhibit = false;
	ret = av8100_audio_change_hdmi_audio_settings(codec_dai, &as);
	if (ret < 0) {
		pr_err("%s: Unable to change HDMI audio-settings for codec-DAI "
			"(av8100_codec_change_hdmi_audio_settings returned %d)!\n",
			__func__,
			ret);
		return ret;
	}

	/* Set format for codec-DAI */
	fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM;
	pr_debug("%s: Setting format for codec-DAI (fmt = %d).\n",
		__func__,
		fmt);
	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		pr_err("%s: Unable to set format for codec-DAI "
			"(snd_soc_dai_set_tdm_slot returned %d)!\n",
			__func__,
			ret);
		return ret;
	}

	/* Set TDM-slot for CPU-DAI */
	pr_debug("%s: Setting TDM-slot for codec-DAI (tx_mask = %d).\n",
		__func__,
		tx_mask);
	ret = snd_soc_dai_set_tdm_slot(cpu_dai, tx_mask, 0, 16, 16);
	if (ret < 0) {
		pr_err("%s: Unable to set TDM-slot for codec-DAI "
			"(snd_soc_dai_set_tdm_slot returned %d)!\n",
			__func__,
			ret);
		return ret;
	}

	/* Set format for CPU-DAI */
	fmt = SND_SOC_DAIFMT_DSP_A |
		SND_SOC_DAIFMT_CBM_CFM |
		SND_SOC_DAIFMT_NB_IF;
	pr_debug("%s: Setting DAI-format for Ux500-platform (fmt = %d).\n",
		__func__,
		fmt);
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		pr_err("%s: Unable to set DAI-format for Ux500-platform "
			"(snd_soc_dai_set_fmt returned %d).\n",
			__func__,
			ret);
		return ret;
	}

	ux500_msp_dai_set_data_delay(cpu_dai, MSP_DELAY_1);

	return ret;
}

struct snd_soc_ops ux500_av8100_ops[] = {
	{
	.hw_params = ux500_av8100_hw_params,
	}
};

