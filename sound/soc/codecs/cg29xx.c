/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Roger Nilsson <roger.xr.nilsson@stericsson.com>,
 *         Ola Lilja <ola.o.lilja@stericsson.com>
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
#include <sound/soc-dapm.h>
#include <linux/bitops.h>
#include <../../../drivers/staging/cg2900/include/cg2900_audio.h>

#include "cg29xx.h"

#define CG29XX_NBR_OF_DAI	2
#define CG29XX_SUPPORTED_RATE_PCM (SNDRV_PCM_RATE_8000 | \
	SNDRV_PCM_RATE_16000)

#define CG29XX_SUPPORTED_RATE (SNDRV_PCM_RATE_8000 | \
	SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

#define CG29XX_SUPPORTED_FMT (SNDRV_PCM_FMTBIT_S16_LE)

enum cg29xx_dai_direction {
	CG29XX_DAI_DIRECTION_TX,
	CG29XX_DAI_DIRECTION_RX
};

static int cg29xx_dai_startup(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai);

static int cg29xx_dai_prepare(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai);

static int cg29xx_dai_hw_params(
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hw_params,
	struct snd_soc_dai *dai);

static void cg29xx_dai_shutdown(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai);

static int cg29xx_set_dai_sysclk(
	struct snd_soc_dai *codec_dai,
	int clk_id,
	unsigned int freq, int dir);

static int cg29xx_set_dai_fmt(
	struct snd_soc_dai *codec_dai,
	unsigned int fmt);

static int cg29xx_set_tdm_slot(
	struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask,
	int slots,
	int slot_width);

static struct cg29xx_codec codec_private = {
	.session = 0,
};

static struct snd_soc_dai_ops cg29xx_dai_driver_dai_ops = {
	.startup = cg29xx_dai_startup,
	.prepare = cg29xx_dai_prepare,
	.hw_params = cg29xx_dai_hw_params,
	.shutdown = cg29xx_dai_shutdown,
	.set_sysclk = cg29xx_set_dai_sysclk,
	.set_fmt = cg29xx_set_dai_fmt,
	.set_tdm_slot = cg29xx_set_tdm_slot
};

struct snd_soc_dai_driver cg29xx_dai_driver[] = {
	{
	.name = "cg29xx-codec-dai.0",
	.id = 0,
	.playback = {
		.stream_name = "CG29xx.0 Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = CG29XX_SUPPORTED_RATE,
		.formats = CG29XX_SUPPORTED_FMT,
	},
	.capture = {
		.stream_name = "CG29xx.0 Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = CG29XX_SUPPORTED_RATE,
		.formats = CG29XX_SUPPORTED_FMT,
	},
	.ops = &cg29xx_dai_driver_dai_ops,
	.symmetric_rates = 1,
	},
	{
	.name = "cg29xx-codec-dai.1",
	.id = 1,
	.playback = {
		.stream_name = "CG29xx.1 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = CG29XX_SUPPORTED_RATE_PCM,
		.formats = CG29XX_SUPPORTED_FMT,
	},
	.capture = {
		.stream_name = "CG29xx.1 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = CG29XX_SUPPORTED_RATE_PCM,
		.formats = CG29XX_SUPPORTED_FMT,
	},
	.ops = &cg29xx_dai_driver_dai_ops,
	.symmetric_rates = 1,
	}
};
EXPORT_SYMBOL_GPL(cg29xx_dai_driver);

static const char *enum_ifs_input_select[] = {
	"BT_SCO", "FM_RX"
};

static const char *enum_ifs_output_select[] = {
	"BT_SCO", "FM_TX"
};

/* If0 Input Select */
static struct soc_enum if0_input_select =
	SOC_ENUM_SINGLE(INTERFACE0_INPUT_SELECT, 0,
		ARRAY_SIZE(enum_ifs_input_select),
		enum_ifs_input_select);

/* If1 Input Select */
static struct soc_enum if1_input_select =
	SOC_ENUM_SINGLE(INTERFACE1_INPUT_SELECT, 0,
		ARRAY_SIZE(enum_ifs_input_select),
		enum_ifs_input_select);

/* If0 Output Select */
static struct soc_enum if0_output_select =
	SOC_ENUM_SINGLE(INTERFACE0_OUTPUT_SELECT, 0,
		ARRAY_SIZE(enum_ifs_output_select),
		enum_ifs_output_select);

/* If1 Output Select */
static struct soc_enum if1_output_select =
	SOC_ENUM_SINGLE(INTERFACE1_OUTPUT_SELECT, 4,
		ARRAY_SIZE(enum_ifs_output_select),
		enum_ifs_output_select);

static struct snd_kcontrol_new cg29xx_snd_controls[] = {
	SOC_ENUM("If0 Input Select",	if0_input_select),
	SOC_ENUM("If1 Input Select",	if1_input_select),
	SOC_ENUM("If0 Output Select",	if0_output_select),
	SOC_ENUM("If1 Output Select",	if1_output_select),
};


static struct cg29xx_codec_dai_data *get_dai_data_codec(struct snd_soc_codec *codec,
						int dai_id)
{
	struct cg29xx_codec_dai_data *codec_drvdata = snd_soc_codec_get_drvdata(codec);
	return &codec_drvdata[dai_id];
}

static struct cg29xx_codec_dai_data *get_dai_data(struct snd_soc_dai *codec_dai)
{
	return get_dai_data_codec(codec_dai->codec, codec_dai->id);
}

static int cg29xx_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id,
				unsigned int freq, int dir)
{
	return 0;
}

static int cg29xx_set_dai_fmt(struct snd_soc_dai *codec_dai,
			unsigned int fmt)
{
	struct cg29xx_codec_dai_data *dai_data = get_dai_data(codec_dai);
	unsigned int prot;
	unsigned int msel;
	prot = fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	msel = fmt & SND_SOC_DAIFMT_MASTER_MASK;

	switch (prot) {
	case SND_SOC_DAIFMT_I2S:
		if (dai_data->config.port != PORT_0_I2S) {
			pr_err("cg29xx_dai: unsupported DAI format 0x%x\n",
				fmt);
			return -EINVAL;
		}

		if (msel == SND_SOC_DAIFMT_CBM_CFM)
			dai_data->config.conf.i2s.mode = DAI_MODE_MASTER;
		else
			dai_data->config.conf.i2s.mode = DAI_MODE_SLAVE;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		if (dai_data->config.port != PORT_1_I2S_PCM ||
			msel == SND_SOC_DAIFMT_CBM_CFM) {
			pr_err("cg29xx_dai: unsupported DAI format 0x%x port=%d,msel=%d\n",
				fmt, dai_data->config.port, msel);
			return -EINVAL;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int cg29xx_set_tdm_slot(struct snd_soc_dai *codec_dai,
			unsigned int tx_mask,
			unsigned int rx_mask,
			int slots,
			int slot_width)
{
	struct cg29xx_codec_dai_data *dai_data = get_dai_data(codec_dai);

	if (dai_data->config.port != PORT_1_I2S_PCM)
		return -EINVAL;

	dai_data->config.conf.i2s_pcm.slot_0_used =
		(tx_mask | rx_mask) & (1<<CG29XX_DAI_SLOT0_SHIFT) ?
		true : false;
	dai_data->config.conf.i2s_pcm.slot_1_used =
		(tx_mask | rx_mask) & (1<<CG29XX_DAI_SLOT1_SHIFT) ?
		true : false;
	dai_data->config.conf.i2s_pcm.slot_2_used =
		(tx_mask | rx_mask) & (1<<CG29XX_DAI_SLOT2_SHIFT) ?
		true : false;
	dai_data->config.conf.i2s_pcm.slot_3_used =
		(tx_mask | rx_mask) & (1<<CG29XX_DAI_SLOT3_SHIFT) ?
		true : false;

	dai_data->config.conf.i2s_pcm.slot_0_start = 0;
	dai_data->config.conf.i2s_pcm.slot_1_start = slot_width;
	dai_data->config.conf.i2s_pcm.slot_2_start = 2 * slot_width;
	dai_data->config.conf.i2s_pcm.slot_3_start = 3 * slot_width;

	return 0;
}

static int cg29xx_configure_endp(struct cg29xx_codec_dai_data *dai_data,
				enum cg2900_audio_endpoint_id endpid)
{
	struct cg2900_endpoint_config config;
	int err;
	enum cg2900_dai_sample_rate dai_sr;
	enum cg2900_endpoint_sample_rate endp_sr;

	switch (dai_data->config.port) {
	default:
	case PORT_0_I2S:
		dai_sr = dai_data->config.conf.i2s.sample_rate;
		break;

	case PORT_1_I2S_PCM:
		dai_sr = dai_data->config.conf.i2s_pcm.sample_rate;
		break;
	}

	switch (dai_sr) {
	default:
	case SAMPLE_RATE_8:
		endp_sr = ENDPOINT_SAMPLE_RATE_8_KHZ;
		break;
	case SAMPLE_RATE_16:
		endp_sr = ENDPOINT_SAMPLE_RATE_16_KHZ;
		break;
	case SAMPLE_RATE_44_1:
		endp_sr = ENDPOINT_SAMPLE_RATE_44_1_KHZ;
		break;
	case SAMPLE_RATE_48:
		endp_sr = ENDPOINT_SAMPLE_RATE_48_KHZ;
		break;
	}

	config.endpoint_id = endpid;

	switch (endpid) {
	default:
	case ENDPOINT_BT_SCO_INOUT:
		config.config.sco.sample_rate = endp_sr;
		break;

	case ENDPOINT_FM_TX:
	case ENDPOINT_FM_RX:
		config.config.fm.sample_rate = endp_sr;
		break;
	}

	err = cg2900_audio_config_endpoint(codec_private.session, &config);

	return err;
}

static int cg29xx_stop_if(struct cg29xx_codec_dai_data *dai_data,
			enum cg29xx_dai_direction direction)
{
	int err = 0;
	unsigned int *stream;

	if (direction == CG29XX_DAI_DIRECTION_TX)
		stream = &dai_data->tx_active;
	else
		stream = &dai_data->rx_active;

	if (*stream) {
		err = cg2900_audio_stop_stream(
			codec_private.session,
			*stream);
		if (!err) {
			*stream = 0;
		} else {
			pr_err("asoc cg29xx - %s - Failed to stop stream on interface %d.\n",
				__func__,
				dai_data->config.port);
		}
	}

	return err;
}

static int cg29xx_start_if(struct cg29xx_codec_dai_data *dai_data,
			enum cg29xx_dai_direction direction)
{
	enum cg2900_audio_endpoint_id if_endpid;
	enum cg2900_audio_endpoint_id endpid;
	unsigned int *stream;
	int err;

	if (dai_data->config.port == PORT_0_I2S)
		if_endpid = ENDPOINT_PORT_0_I2S;
	else
		if_endpid = ENDPOINT_PORT_1_I2S_PCM;

	if (direction == CG29XX_DAI_DIRECTION_RX) {
		switch (dai_data->output_select) {
		default:
		case 0:
			endpid = ENDPOINT_BT_SCO_INOUT;
			break;
		case 1:
			endpid = ENDPOINT_FM_TX;
		}
		stream = &dai_data->rx_active;
	} else {
		switch (dai_data->input_select) {
		default:
		case 0:
			endpid = ENDPOINT_BT_SCO_INOUT;
			break;
		case 1:
			endpid = ENDPOINT_FM_RX;
		}

		stream = &dai_data->tx_active;
	}

	if (*stream || (endpid == ENDPOINT_BT_SCO_INOUT)) {
		pr_debug("asoc cg29xx - %s - The interface has already been started.\n",
			__func__);
		return 0;
	}

	pr_debug("asoc cg29xx - %s - direction: %d, if_id: %d endpid: %d\n",
			__func__,
			direction,
			if_endpid,
			endpid);

	err = cg29xx_configure_endp(dai_data, endpid);

	if (err) {
		pr_err("asoc cg29xx - %s - Configure endpoint id: %d failed.\n",
			__func__,
			endpid);

		return err;
	}

	err = cg2900_audio_start_stream(codec_private.session,
		if_endpid,
		endpid,
		stream);

	return err;
}

static int cg29xx_dai_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	int err = 0;

	if (!codec_private.session)
		err = cg2900_audio_open(&codec_private.session, NULL);

	return err;
}

static int cg29xx_dai_prepare(struct snd_pcm_substream *substream,
			struct snd_soc_dai *codec_dai)
{
	struct cg29xx_codec_dai_data *dai_data = get_dai_data(codec_dai);
	int err = 0;
	enum cg29xx_dai_direction direction;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		direction = CG29XX_DAI_DIRECTION_RX;
	else
		direction = CG29XX_DAI_DIRECTION_TX;

	err = cg29xx_start_if(dai_data, direction);

	return err;
}

static void cg29xx_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *codec_dai)
{
	struct cg29xx_codec_dai_data *dai_data = get_dai_data(codec_dai);
	enum cg29xx_dai_direction direction;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		direction = CG29XX_DAI_DIRECTION_RX;
	else
		direction = CG29XX_DAI_DIRECTION_TX;

	(void) cg29xx_stop_if(dai_data, direction);
}

static int cg29xx_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params,
				struct snd_soc_dai *codec_dai)
{
	struct cg29xx_codec_dai_data *dai_data = get_dai_data(codec_dai);
	enum cg2900_dai_fs_duration duration = SYNC_DURATION_32;
	enum cg2900_dai_bit_clk bclk = BIT_CLK_512;
	int sr;
	int err = 0;
	enum cg2900_dai_stream_ratio ratio = STREAM_RATIO_FM48_VOICE16;

	pr_debug("cg29xx asoc - %s called. Port: %d.\n",
		__func__,
		dai_data->config.port);

	switch (params_rate(hw_params)) {
	case 8000:
		sr = SAMPLE_RATE_8;
		bclk = BIT_CLK_512;
		duration = SYNC_DURATION_32;
		ratio = STREAM_RATIO_FM48_VOICE8;
		break;
	case 16000:
		sr = SAMPLE_RATE_16;
		bclk = BIT_CLK_512;
		duration = SYNC_DURATION_32;
		ratio = STREAM_RATIO_FM48_VOICE16;
		break;
	case 44100:
		sr = SAMPLE_RATE_44_1;
		break;
	case 48000:
		sr = SAMPLE_RATE_48;
		break;
	default:
		return -EINVAL;
	}

	if (dai_data->config.port == PORT_0_I2S) {
		dai_data->config.conf.i2s.sample_rate = sr;
	} else {
		dai_data->config.conf.i2s_pcm.sample_rate = sr;
		dai_data->config.conf.i2s_pcm.duration = duration;
		dai_data->config.conf.i2s_pcm.clk = bclk;
		dai_data->config.conf.i2s_pcm.ratio = ratio;
	}

	if (!(dai_data->tx_active | dai_data->rx_active) && dai_data->config.port != PORT_1_I2S_PCM) {
		err = cg2900_audio_set_dai_config(
			codec_private.session,
			&dai_data->config);

		pr_debug("asoc cg29xx: cg2900_audio_set_dai_config"
			"on port %d completed with result: %d.\n",
			dai_data->config.port,
			err);
	}

	return err;
}

static unsigned int cg29xx_codec_read(struct snd_soc_codec *codec,
				unsigned int reg)
{
	struct cg29xx_codec_dai_data *dai_data;

	switch (reg) {
	case INTERFACE0_INPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 0);
		return dai_data->input_select;

	case INTERFACE1_INPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 1);
		return dai_data->input_select;

	case INTERFACE0_OUTPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 0);
		return dai_data->output_select;

	case INTERFACE1_OUTPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 1);
		return dai_data->output_select;

	default:
		return 0;
	}

	return 0;
}

static int cg29xx_codec_write(struct snd_soc_codec *codec,
			unsigned int reg,
			unsigned int value)
{
	struct cg29xx_codec_dai_data *dai_data;
	enum cg29xx_dai_direction direction;
	bool restart_if = false;
	int old_value;

	switch (reg) {
	case INTERFACE0_INPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 0);
		direction = CG29XX_DAI_DIRECTION_TX;

		old_value = dai_data->input_select;
		dai_data->input_select = value;

		if ((old_value ^ value) && dai_data->tx_active)
			restart_if = true;
		break;

	case INTERFACE1_INPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 1);
		direction = CG29XX_DAI_DIRECTION_TX;

		old_value = dai_data->input_select;
		dai_data->input_select = value;

		if ((old_value ^ value) && dai_data->tx_active)
			restart_if = true;
		break;

	case INTERFACE0_OUTPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 0);
		direction = CG29XX_DAI_DIRECTION_RX;

		old_value = dai_data->output_select;
		dai_data->output_select = value;

		if ((old_value ^ value) && dai_data->rx_active)
			restart_if = true;
		break;

	case INTERFACE1_OUTPUT_SELECT:
		dai_data = get_dai_data_codec(codec, 1);
		direction = CG29XX_DAI_DIRECTION_RX;

		old_value = dai_data->output_select;
		dai_data->output_select = value;

		if ((old_value ^ value) && dai_data->rx_active)
			restart_if = true;
		break;

	default:
		return -EINVAL;
	}

	if (restart_if) {
		(void) cg29xx_stop_if(dai_data, direction);
		(void) cg29xx_start_if(dai_data, direction);
	}

	return 0;
}

static int cg29xx_codec_probe(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	snd_soc_add_controls(
		codec,
		cg29xx_snd_controls,
		ARRAY_SIZE(cg29xx_snd_controls));

	return 0;
}

static int cg29xx_codec_remove(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	return 0;
}

static int cg29xx_codec_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	return 0;
}

static int cg29xx_codec_resume(struct snd_soc_codec *codec)
{
	pr_debug("%s: Enter (codec->name = %s).\n", __func__, codec->name);

	return 0;
}

struct snd_soc_codec_driver cg29xx_codec_driver = {
	.probe = cg29xx_codec_probe,
	.remove = cg29xx_codec_remove,
	.suspend = cg29xx_codec_suspend,
	.resume = cg29xx_codec_resume,
	.read = cg29xx_codec_read,
	.write = cg29xx_codec_write,
};

static int __devinit cg29xx_codec_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct cg29xx_codec_dai_data *dai_data;

	pr_debug("%s: Enter.\n", __func__);

	pr_info("%s: Init codec private data..\n", __func__);
	dai_data = kzalloc(CG29XX_NBR_OF_DAI * sizeof(struct cg29xx_codec_dai_data),
			GFP_KERNEL);
	if (dai_data == NULL)
		return -ENOMEM;

	dai_data[0].tx_active = 0;
	dai_data[0].rx_active = 0;
	dai_data[0].input_select = 1;
	dai_data[0].output_select = 1;
	dai_data[0].config.port = PORT_0_I2S;
	dai_data[0].config.conf.i2s.mode = DAI_MODE_SLAVE;
	dai_data[0].config.conf.i2s.half_period = HALF_PER_DUR_16;
	dai_data[0].config.conf.i2s.channel_sel = CHANNEL_SELECTION_BOTH;
	dai_data[0].config.conf.i2s.sample_rate = SAMPLE_RATE_48;
	dai_data[0].config.conf.i2s.word_width = WORD_WIDTH_32;
	dai_data[1].tx_active = 0;
	dai_data[1].rx_active = 0;
	dai_data[1].input_select = 0;
	dai_data[1].output_select = 0;
	dai_data[1].config.port = PORT_1_I2S_PCM;
	dai_data[1].config.conf.i2s_pcm.mode = DAI_MODE_SLAVE;
	dai_data[1].config.conf.i2s_pcm.slot_0_dir = DAI_DIR_B_RX_A_TX;
	dai_data[1].config.conf.i2s_pcm.slot_1_dir = DAI_DIR_B_TX_A_RX;
	dai_data[1].config.conf.i2s_pcm.slot_2_dir = DAI_DIR_B_RX_A_TX;
	dai_data[1].config.conf.i2s_pcm.slot_3_dir = DAI_DIR_B_RX_A_TX;
	dai_data[1].config.conf.i2s_pcm.slot_0_used = true;
	dai_data[1].config.conf.i2s_pcm.slot_1_used = false;
	dai_data[1].config.conf.i2s_pcm.slot_2_used = false;
	dai_data[1].config.conf.i2s_pcm.slot_3_used = false;
	dai_data[1].config.conf.i2s_pcm.slot_0_start = 0;
	dai_data[1].config.conf.i2s_pcm.slot_1_start = 16;
	dai_data[1].config.conf.i2s_pcm.slot_2_start = 32;
	dai_data[1].config.conf.i2s_pcm.slot_3_start = 48;
	dai_data[1].config.conf.i2s_pcm.protocol = PORT_PROTOCOL_PCM;
	dai_data[1].config.conf.i2s_pcm.ratio = STREAM_RATIO_FM48_VOICE16;
	dai_data[1].config.conf.i2s_pcm.duration = SYNC_DURATION_32;
	dai_data[1].config.conf.i2s_pcm.clk = BIT_CLK_512;
	dai_data[1].config.conf.i2s_pcm.sample_rate = SAMPLE_RATE_16;

	platform_set_drvdata(pdev, dai_data);

	pr_info("%s: Register codec.\n", __func__);
	ret = snd_soc_register_codec(&pdev->dev, &cg29xx_codec_driver, &cg29xx_dai_driver[0], 2);
	if (ret < 0) {
		pr_debug("%s: Error: Failed to register codec (ret = %d).\n",
			__func__,
			ret);
		snd_soc_unregister_codec(&pdev->dev);
		kfree(platform_get_drvdata(pdev));
		return ret;
	}

	return 0;
}

static int __devexit cg29xx_codec_driver_remove(struct platform_device *pdev)
{
	(void)cg2900_audio_close(&codec_private.session);

	snd_soc_unregister_codec(&pdev->dev);
	kfree(platform_get_drvdata(pdev));

	return 0;
}

static int cg29xx_codec_driver_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int cg29xx_codec_driver_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver cg29xx_codec_platform_driver = {
	.driver = {
		.name = "cg29xx-codec",
		.owner = THIS_MODULE,
	},
	.probe = cg29xx_codec_driver_probe,
	.remove = __devexit_p(cg29xx_codec_driver_remove),
	.suspend = cg29xx_codec_driver_suspend,
	.resume = cg29xx_codec_driver_resume,
};


static int __devinit cg29xx_codec_platform_driver_init(void)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	ret = platform_driver_register(&cg29xx_codec_platform_driver);
	if (ret != 0)
		pr_err("Failed to register CG29xx platform driver (%d)!\n", ret);

	return ret;
}

static void __exit cg29xx_codec_platform_driver_exit(void)
{
	pr_debug("%s: Enter.\n", __func__);

	platform_driver_unregister(&cg29xx_codec_platform_driver);
}


module_init(cg29xx_codec_platform_driver_init);
module_exit(cg29xx_codec_platform_driver_exit);

MODULE_LICENSE("GPL v2");
