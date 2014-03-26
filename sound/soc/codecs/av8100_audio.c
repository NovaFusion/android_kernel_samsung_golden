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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <video/av8100.h>
#include <video/hdmi.h>

#include "av8100_audio.h"

/* codec private data */
struct av8100_codec_dai_data {
	struct hdmi_audio_settings as;
};

static struct av8100_codec_dai_data *get_dai_data_codec(struct snd_soc_codec *codec,
						int dai_id)
{
	struct av8100_codec_dai_data *dai_data = snd_soc_codec_get_drvdata(codec);
	return &dai_data[dai_id];
}

static struct av8100_codec_dai_data *get_dai_data(struct snd_soc_dai *codec_dai)
{
	return get_dai_data_codec(codec_dai->codec, codec_dai->id);
}

/* Controls - Non-DAPM Non-ASoC */

/* Coding Type */

static const char *hdmi_coding_type_str[] = {"AV8100_CODEC_CT_REFER",
					"AV8100_CODEC_CT_IEC60958_PCM",
					"AV8100_CODEC_CT_AC3",
					"AV8100_CODEC_CT_MPEG1",
					"AV8100_CODEC_CT_MP3",
					"AV8100_CODEC_CT_MPEG2",
					"AV8100_CODEC_CT_AAC",
					"AV8100_CODEC_CT_DTS",
					"AV8100_CODEC_CT_ATRAC",
					"AV8100_CODEC_CT_ONE_BIT_AUDIO",
					"AV8100_CODEC_CT_DOLBY_DIGITAL",
					"AV8100_CODEC_CT_DTS_HD",
					"AV8100_CODEC_CT_MAT",
					"AV8100_CODEC_CT_DST",
					"AV8100_CODEC_CT_WMA_PRO"};

enum hdmi_audio_coding_type audio_coding_type;

static int hdmi_coding_type_control_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	int items = ARRAY_SIZE(hdmi_coding_type_str);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = items;

	if (uinfo->value.enumerated.item > items - 1)
		uinfo->value.enumerated.item = items - 1;

	strcpy(uinfo->value.enumerated.name,
		hdmi_coding_type_str[uinfo->value.enumerated.item]);

	return 0;
}

static int hdmi_coding_type_control_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = audio_coding_type;

	return 0;
}

static int hdmi_coding_type_control_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	int items = ARRAY_SIZE(hdmi_coding_type_str);

	if (ucontrol->value.enumerated.item[0] > items - 1)
		ucontrol->value.enumerated.item[0] = items - 1;

	audio_coding_type = ucontrol->value.enumerated.item[0];

	return 1;
}

static const struct snd_kcontrol_new hdmi_coding_type_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "HDMI Coding Type",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = hdmi_coding_type_control_info,
	.get = hdmi_coding_type_control_get,
	.put = hdmi_coding_type_control_put,
};

/* Extended interface for codec-driver */

int av8100_audio_change_hdmi_audio_settings(struct snd_soc_dai *codec_dai,
					struct hdmi_audio_settings *as)
{
	struct av8100_codec_dai_data *dai_data = get_dai_data(codec_dai);

	pr_debug("%s: Enter.\n", __func__);

	dai_data->as.audio_channel_count = as->audio_channel_count;
	dai_data->as.sampling_frequency = as->sampling_frequency;
	dai_data->as.sample_size = as->sample_size;
	dai_data->as.channel_allocation = as->channel_allocation;
	dai_data->as.level_shift_value = as->level_shift_value;
	dai_data->as.downmix_inhibit = as->downmix_inhibit;

	return 0;
}

static int av8100_codec_powerup(void)
{
	struct av8100_status status;
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY) {
		pr_debug("%s: Powering up AV8100.", __func__);
		ret = av8100_powerup();
		if (ret != 0) {
			pr_err("%s: Power up AV8100 failed "
				"(av8100_powerup returned %d)!\n",
				__func__,
				ret);
			return -EINVAL;
		}
	}
	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		ret = av8100_download_firmware(I2C_INTERFACE);
		if (ret != 0) {
			pr_err("%s: Download firmware failed "
				"(av8100_download_firmware returned %d)!\n",
				__func__,
				ret);
			return -EINVAL;
		}
	}

	return 0;
}

static int av8100_codec_setup_hdmi_format(void)
{
	union av8100_configuration config;
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	pr_debug("%s: hdmi_mode = AV8100_HDMI_ON.", __func__);
	pr_debug("%s: hdmi_format = AV8100_HDMI.", __func__);
	config.hdmi_format.hdmi_mode = AV8100_HDMI_ON;
	config.hdmi_format.hdmi_format = AV8100_HDMI;
	config.hdmi_format.dvi_format = AV8100_DVI_CTRL_CTL0;
	av8100_conf_lock();
	ret = av8100_conf_prep(AV8100_COMMAND_HDMI, &config);
	if (ret != 0) {
		pr_err("%s: Setting hdmi_format failed "
			"(av8100_conf_prep returned %d)!\n",
			__func__,
			ret);
		av8100_conf_unlock();
		return -EINVAL;
	}
	ret = av8100_conf_w(AV8100_COMMAND_HDMI,
			NULL,
			NULL,
			I2C_INTERFACE);
	if (ret != 0) {
		pr_err("%s: Setting hdmi_format failed "
			"(av8100_conf_w returned %d)!\n",
			__func__,
			ret);
		av8100_conf_unlock();
		return -EINVAL;
	}

	av8100_conf_unlock();
	return 0;
}

static int av8100_codec_pcm_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	pr_debug("%s: Enter.\n", __func__);

	return 0;
}

static int av8100_codec_send_audio_infoframe(struct hdmi_audio_settings *as)
{
	union av8100_configuration config;
	struct av8100_infoframes_format_cmd info_fr;
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	pr_debug("%s: HDMI-settings:\n", __func__);
	pr_debug("%s:	audio_coding_type = %d\n", __func__, audio_coding_type);
	pr_debug("%s:	audio_channel_count = %d\n", __func__, as->audio_channel_count);
	pr_debug("%s:	sampling_frequency = %d\n", __func__, as->sampling_frequency);
	pr_debug("%s:	sample_size = %d\n", __func__, as->sample_size);
	pr_debug("%s:	channel_allocation = %d\n", __func__, as->channel_allocation);
	pr_debug("%s:	level_shift_value = %d\n", __func__, as->level_shift_value);
	pr_debug("%s:	downmix_inhibit = %d\n", __func__, as->downmix_inhibit);

	/* Prepare the infoframe from the hdmi_audio_settings struct */
	pr_info("%s: Preparing audio info-frame.", __func__);
	info_fr.type = 0x84;
	info_fr.version = 0x01;
	info_fr.length = 0x0a;
	info_fr.data[0] = (audio_coding_type << 4) | as->audio_channel_count;
	info_fr.data[1] = (as->sampling_frequency << 2) | as->sample_size;
	info_fr.data[2] = 0;
	info_fr.data[3] = as->channel_allocation;
	info_fr.data[4] = ((int)as->downmix_inhibit << 7) |
			(as->level_shift_value << 3);
	info_fr.data[5] = 0;
	info_fr.data[6] = 0;
	info_fr.data[7] = 0;
	info_fr.data[8] = 0;
	info_fr.data[9] = 0;
	info_fr.crc = 0x100 - (info_fr.type +
		info_fr.version +
		info_fr.length +
		info_fr.data[0] +
		info_fr.data[1] +
		info_fr.data[3] +
		info_fr.data[4]);
	config.infoframes_format.type = info_fr.type;
	config.infoframes_format.version = info_fr.version;
	config.infoframes_format.crc = info_fr.crc;
	config.infoframes_format.length = info_fr.length;
	memcpy(&config.infoframes_format.data, info_fr.data, info_fr.length);

	/* Send audio info-frame */
	pr_info("%s: Sending audio info-frame.", __func__);
	av8100_conf_lock();
	ret = av8100_conf_prep(AV8100_COMMAND_INFOFRAMES, &config);
	if (ret != 0) {
		pr_err("%s: Sending audio info-frame failed "
			"(av8100_conf_prep returned %d)!\n",
			__func__,
			ret);
		av8100_conf_unlock();
		return -EINVAL;
	}
	ret = av8100_conf_w(AV8100_COMMAND_INFOFRAMES,
			NULL,
			NULL,
			I2C_INTERFACE);
	if (ret != 0) {
		pr_err("%s: Sending audio info-frame failed "
			"(av8100_conf_w returned %d)!\n",
			__func__,
			ret);
		av8100_conf_unlock();
		return -EINVAL;
	}

	av8100_conf_unlock();
	return 0;
}

static int av8100_codec_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params,
				struct snd_soc_dai *codec_dai)
{
	struct av8100_codec_dai_data *dai_data = get_dai_data(codec_dai);

	pr_debug("%s: Enter.\n", __func__);

	av8100_codec_send_audio_infoframe(&dai_data->as);

	return 0;
}

static int av8100_codec_pcm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *codec_dai)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	/* Get HDMI resource */
	if (av8100_hdmi_get(AV8100_HDMI_USER_AUDIO) < 0)
		return -EBUSY;

	/* Startup AV8100 if it is not already started */
	ret = av8100_codec_powerup();
	if (ret != 0) {
		pr_err("%s: Startup of AV8100 failed "
			"(av8100_codec_powerupAV8100 returned %d)!\n",
			__func__,
			ret);
		/* Put HDMI resource */
		av8100_hdmi_put(AV8100_HDMI_USER_AUDIO);
		return -EINVAL;
	}

	return 0;
}

static void av8100_codec_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *codec_dai)
{
	/* Put HDMI resource */
	av8100_hdmi_put(AV8100_HDMI_USER_AUDIO);

	pr_debug("%s: Enter.\n", __func__);
}

static int av8100_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id,
				unsigned int freq, int dir)
{
	pr_debug("%s: Enter.\n", __func__);

	return 0;
}

static int av8100_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
				unsigned int fmt)
{
	union av8100_configuration config;
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	/* Set the HDMI format of AV8100 */
	ret = av8100_codec_setup_hdmi_format();
	if (ret != 0)
		return ret;

	/* Set the audio input format of AV8100 */
	config.audio_input_format.audio_input_if_format	=
		((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_DSP_A) ?
		AV8100_AUDIO_TDM_MODE : AV8100_AUDIO_I2SDELAYED_MODE;
	config.audio_input_format.audio_if_mode	=
		((fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM) ?
		AV8100_AUDIO_MASTER : AV8100_AUDIO_SLAVE;
	pr_info("%s: Setting audio_input_format "
		"(if_format = %d, if_mode = %d).",
		__func__,
		config.audio_input_format.audio_input_if_format,
		config.audio_input_format.audio_if_mode);
	config.audio_input_format.i2s_input_nb = 1;
	config.audio_input_format.sample_audio_freq = AV8100_AUDIO_FREQ_48KHZ;
	config.audio_input_format.audio_word_lg	= AV8100_AUDIO_16BITS;
	config.audio_input_format.audio_format = AV8100_AUDIO_LPCM_MODE;
	config.audio_input_format.audio_mute = AV8100_AUDIO_MUTE_DISABLE;
	av8100_conf_lock();
	ret = av8100_conf_prep(AV8100_COMMAND_AUDIO_INPUT_FORMAT, &config);
	if (ret != 0) {
		pr_err("%s: Setting audio_input_format failed "
			"(av8100_conf_prep returned %d)!\n",
			__func__,
			ret);
		av8100_conf_unlock();
		return -EINVAL;
	}
	ret = av8100_conf_w(AV8100_COMMAND_AUDIO_INPUT_FORMAT,
			NULL,
			NULL,
			I2C_INTERFACE);
	if (ret != 0) {
		pr_err("%s: Setting audio_input_format failed "
			"(av8100_conf_w returned %d)!\n",
			__func__,
			ret);
		av8100_conf_unlock();
		return -EINVAL;
	}

	av8100_conf_unlock();
	return 0;
}

struct snd_soc_dai_driver av8100_dai_driver = {
		.name = "av8100-codec-dai",
		.playback = {
			.stream_name = "AV8100 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = AV8100_SUPPORTED_RATE,
			.formats = AV8100_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
			.prepare = av8100_codec_pcm_prepare,
			.hw_params = av8100_codec_pcm_hw_params,
			.startup = av8100_codec_pcm_startup,
			.shutdown = av8100_codec_pcm_shutdown,
			.set_sysclk = av8100_codec_set_dai_sysclk,
			.set_fmt = av8100_codec_set_dai_fmt,
			}
		},
};
EXPORT_SYMBOL_GPL(av8100_dai_driver);

static int av8100_codec_probe(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	audio_coding_type = AV8100_CODEC_CT_REFER;

	/* Add controls with events */
	snd_ctl_add(codec->card->snd_card, snd_ctl_new1(&hdmi_coding_type_control, codec));

	return 0;
}

static int av8100_codec_remove(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	return 0;
}

static int av8100_codec_suspend(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	return 0;
}

static int av8100_codec_resume(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	return 0;
}

struct snd_soc_codec_driver av8100_codec_drv = {
	.probe = av8100_codec_probe,
	.remove = av8100_codec_remove,
	.suspend = av8100_codec_suspend,
	.resume = av8100_codec_resume
};

static __devinit int av8100_codec_drv_probe(struct platform_device *pdev)
{
	struct av8100_codec_dai_data *dai_data;
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	pr_info("%s: Init codec private data..\n", __func__);
	dai_data = kzalloc(sizeof(struct av8100_codec_dai_data), GFP_KERNEL);
	if (dai_data == NULL)
		return -ENOMEM;

	/* Setup hdmi_audio_settings default values */
	dai_data[0].as.audio_channel_count = AV8100_CODEC_CC_2CH;
	dai_data[0].as.sampling_frequency = AV8100_CODEC_SF_48KHZ;
	dai_data[0].as.sample_size = AV8100_CODEC_SS_16BIT;
	dai_data[0].as.channel_allocation = AV8100_CODEC_CA_FL_FR;
	dai_data[0].as.level_shift_value = AV8100_CODEC_LSV_0DB;
	dai_data[0].as.downmix_inhibit = false;

	platform_set_drvdata(pdev, dai_data);

	pr_info("%s: Register codec.\n", __func__);
	ret = snd_soc_register_codec(&pdev->dev, &av8100_codec_drv, &av8100_dai_driver, 1);
	if (ret < 0) {
		pr_debug("%s: Error: Failed to register codec (ret = %d).\n",
			__func__,
			ret);
		return ret;
	}

	return 0;
}

static int __devexit av8100_codec_drv_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	kfree(platform_get_drvdata(pdev));
	return 0;
}

static const struct platform_device_id av8100_codec_platform_id[] = {
	{ "av8100-codec", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, av8100_codec_platform_id);

static struct platform_driver av8100_codec_platform_driver = {
	.driver = {
		.name = "av8100-codec",
		.owner = THIS_MODULE,
	},
	.probe = av8100_codec_drv_probe,
	.remove = __devexit_p(av8100_codec_drv_remove),
	.id_table = av8100_codec_platform_id,
};

static int __devinit av8100_codec_platform_drv_init(void)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	ret = platform_driver_register(&av8100_codec_platform_driver);
	if (ret != 0) {
		pr_err("Failed to register AV8100 platform driver (%d)!\n", ret);
	}

	return ret;
}

static void __exit av8100_codec_platform_drv_exit(void)
{
	pr_debug("%s: Enter.\n", __func__);

	platform_driver_unregister(&av8100_codec_platform_driver);
}

module_init(av8100_codec_platform_drv_init);
module_exit(av8100_codec_platform_drv_exit);

MODULE_LICENSE("GPL v2");
