/*
 * Copyright (C) ST-Ericsson SA 2011
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

#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/msp.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "ux500_msp_i2s.h"
#include "ux500_msp_dai.h"
#include "ux500_pcm.h"

static struct ux500_platform_drvdata platform_drvdata[UX500_NBR_OF_DAI] = {
	{
		.msp_i2s_drvdata = NULL,
		.fmt = 0,
		.slots = 1,
		.tx_mask = 0x01,
		.rx_mask = 0x01,
		.slot_width = 16,
		.playback_active = false,
		.capture_active = false,
		.configured = 0,
		.data_delay = MSP_DELAY_0,
		.master_clk = UX500_MSP_INTERNAL_CLOCK_FREQ,
	},
	{
		.msp_i2s_drvdata = NULL,
		.fmt = 0,
		.slots = 1,
		.tx_mask = 0x01,
		.rx_mask = 0x01,
		.slot_width = 16,
		.playback_active = false,
		.capture_active = false,
		.configured = 0,
		.data_delay = MSP_DELAY_0,
		.master_clk = UX500_MSP1_INTERNAL_CLOCK_FREQ,
	},
	{
		.msp_i2s_drvdata = NULL,
		.fmt = 0,
		.slots = 1,
		.tx_mask = 0x01,
		.rx_mask = 0x01,
		.slot_width = 16,
		.playback_active = false,
		.capture_active = false,
		.configured = 0,
		.data_delay = MSP_DELAY_0,
		.master_clk = UX500_MSP_INTERNAL_CLOCK_FREQ,
	},
	{
		.msp_i2s_drvdata = NULL,
		.fmt = 0,
		.slots = 1,
		.tx_mask = 0x01,
		.rx_mask = 0x01,
		.slot_width = 16,
		.playback_active = false,
		.capture_active = false,
		.configured = 0,
		.data_delay = MSP_DELAY_0,
		.master_clk = UX500_MSP1_INTERNAL_CLOCK_FREQ,
	},
};

static const char *stream_str(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return "Playback";
	else
		return "Capture";
}

static int ux500_msp_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];
	bool mode_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	pr_debug("%s: MSP %d (%s): Enter.\n", __func__, dai->id, stream_str(substream));

	if ((mode_playback && drvdata->playback_active) ||
		(!mode_playback && drvdata->capture_active)) {
		pr_err("%s: Error: MSP %d (%s): Stream already active.\n",
			__func__,
			dai->id,
			stream_str(substream));
		return -EBUSY;
	}

	if (mode_playback)
		drvdata->playback_active = true;
	else
		drvdata->capture_active = true;

	return 0;
}

static void ux500_msp_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];
	bool mode_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	pr_debug("%s: MSP %d (%s): Enter.\n", __func__, dai->id, stream_str(substream));

	if (drvdata == NULL)
		return;

	if (ux500_msp_i2s_close(drvdata->msp_i2s_drvdata,
			mode_playback ? DISABLE_TRANSMIT : DISABLE_RECEIVE)) {
			pr_err("%s: Error: MSP %d (%s): Unable to close i2s.\n",
				__func__,
				dai->id,
				stream_str(substream));
	}

	if (mode_playback) {
		drvdata->playback_active = false;
		drvdata->configured &= ~PLAYBACK_CONFIGURED;
	} else {
		drvdata->capture_active = false;
		drvdata->configured &= ~CAPTURE_CONFIGURED;
	}
}

static void ux500_msp_dai_setup_multichannel(struct ux500_platform_drvdata *private,
					struct msp_config *msp_config)
{
	struct msp_multichannel_config *multi =	&msp_config->multichannel_config;

	if (private->slots > 1) {
		msp_config->multichannel_configured = 1;

		multi->tx_multichannel_enable = true;
		multi->rx_multichannel_enable = true;
		multi->rx_comparison_enable_mode = MSP_COMPARISON_DISABLED;

		multi->tx_channel_0_enable = private->tx_mask;
		multi->tx_channel_1_enable = 0;
		multi->tx_channel_2_enable = 0;
		multi->tx_channel_3_enable = 0;

		multi->rx_channel_0_enable = private->rx_mask;
		multi->rx_channel_1_enable = 0;
		multi->rx_channel_2_enable = 0;
		multi->rx_channel_3_enable = 0;

		pr_debug("%s: Multichannel enabled."
			"Slots: %d TX: %u RX: %u\n",
			__func__,
			private->slots,
			multi->tx_channel_0_enable,
			multi->rx_channel_0_enable);
	}
}

static void ux500_msp_dai_setup_frameper(struct ux500_platform_drvdata *private,
					unsigned int rate,
					struct msp_protocol_desc *prot_desc)
{
	switch (private->slots) {
	default:
	case 1:
		switch (rate) {
		case 8000:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_8_KHZ;
			break;
		case 16000:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_16_KHZ;
			break;
		case 44100:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_44_1_KHZ;
			break;
		case 48000:
		default:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_48_KHZ;
			break;
		}
		break;

	case 2:
		prot_desc->frame_period = FRAME_PER_2_SLOTS;
		break;

	case 8:
		prot_desc->frame_period =
			FRAME_PER_8_SLOTS;
		break;

	case 16:
		prot_desc->frame_period =
			FRAME_PER_16_SLOTS;
		break;
	}

	prot_desc->total_clocks_for_one_frame =
			prot_desc->frame_period+1;

	pr_debug("%s: Total clocks per frame: %u\n",
		__func__,
		prot_desc->total_clocks_for_one_frame);
}

static void ux500_msp_dai_setup_framing_pcm(struct ux500_platform_drvdata *private,
					unsigned int rate,
					struct msp_protocol_desc *prot_desc)
{
	u32 frame_length = MSP_FRAME_LENGTH_1;
	u32 element_length = MSP_ELEM_LENGTH_16;
	prot_desc->frame_width = 0;

	switch (private->slots) {
	default:
	case 1:
		frame_length = MSP_FRAME_LENGTH_1;
		break;

	case 2:
		frame_length = MSP_FRAME_LENGTH_2;
		break;

	case 8:
		frame_length = MSP_FRAME_LENGTH_8;
		break;

	case 16:
		frame_length = MSP_FRAME_LENGTH_16;
		break;
	}

	prot_desc->tx_frame_length_1 = frame_length;
	prot_desc->rx_frame_length_1 = frame_length;
	prot_desc->tx_frame_length_2 = frame_length;
	prot_desc->rx_frame_length_2 = frame_length;

	switch (private->slot_width) {
	case 32:
		element_length = MSP_ELEM_LENGTH_32;
		break;
	case 20:
		element_length = MSP_ELEM_LENGTH_20;
		break;
	default:
	case 16:
		element_length = MSP_ELEM_LENGTH_16;
		break;
	}

	prot_desc->tx_element_length_1 = element_length;
	prot_desc->rx_element_length_1 = element_length;
	prot_desc->tx_element_length_2 = element_length;
	prot_desc->rx_element_length_2 = element_length;

	ux500_msp_dai_setup_frameper(private, rate, prot_desc);
}

static void ux500_msp_dai_setup_clocking(unsigned int fmt,
					struct msp_config *msp_config)
{

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	default:
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_NB_IF:
		msp_config->tx_frame_sync_pol ^= 1 << TFSPOL_SHIFT;
		msp_config->rx_frame_sync_pol ^= 1 << RFSPOL_SHIFT;
		break;
	}

	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM) {
		pr_debug("%s: Codec is MASTER.\n",
			__func__);
		msp_config->iodelay = 0x20;
		msp_config->rx_frame_sync_sel = 0;
		msp_config->tx_frame_sync_sel = 1 << TFSSEL_SHIFT;
		msp_config->tx_clock_sel = 0;
		msp_config->rx_clock_sel = 0;
		msp_config->srg_clock_sel = 0x2 << SCKSEL_SHIFT;

	} else {
		pr_debug("%s: Codec is SLAVE.\n",
			__func__);

		msp_config->tx_clock_sel = TX_CLK_SEL_SRG;
		msp_config->tx_frame_sync_sel = TX_SYNC_SRG_PROG;
		msp_config->rx_clock_sel = RX_CLK_SEL_SRG;
		msp_config->rx_frame_sync_sel = RX_SYNC_SRG;
		msp_config->srg_clock_sel = 1 << SCKSEL_SHIFT;
	}
}

static void ux500_msp_dai_compile_prot_desc_pcm(unsigned int fmt,
					struct msp_protocol_desc *prot_desc)
{
	prot_desc->rx_phase_mode = MSP_SINGLE_PHASE;
	prot_desc->tx_phase_mode = MSP_SINGLE_PHASE;
	prot_desc->rx_phase2_start_mode = MSP_PHASE2_START_MODE_IMEDIATE;
	prot_desc->tx_phase2_start_mode = MSP_PHASE2_START_MODE_IMEDIATE;
	prot_desc->rx_bit_transfer_format = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_bit_transfer_format = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_frame_sync_pol = MSP_FRAME_SYNC_POL(MSP_FRAME_SYNC_POL_ACTIVE_HIGH);
	prot_desc->rx_frame_sync_pol = MSP_FRAME_SYNC_POL_ACTIVE_HIGH << RFSPOL_SHIFT;

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_DSP_A) {
		pr_debug("%s: DSP_A.\n",
			__func__);
		prot_desc->rx_clock_pol = MSP_RISING_EDGE;
		prot_desc->tx_clock_pol = MSP_FALLING_EDGE;
	} else {
		pr_debug("%s: DSP_B.\n",
			__func__);
		prot_desc->rx_clock_pol = MSP_FALLING_EDGE;
		prot_desc->tx_clock_pol = MSP_RISING_EDGE;
	}

	prot_desc->rx_half_word_swap = MSP_HWS_NO_SWAP;
	prot_desc->tx_half_word_swap = MSP_HWS_NO_SWAP;
	prot_desc->compression_mode = MSP_COMPRESS_MODE_LINEAR;
	prot_desc->expansion_mode = MSP_EXPAND_MODE_LINEAR;
	prot_desc->spi_clk_mode = MSP_SPI_CLOCK_MODE_NON_SPI;
	prot_desc->spi_burst_mode = MSP_SPI_BURST_MODE_DISABLE;
	prot_desc->frame_sync_ignore = MSP_FRAME_SYNC_UNIGNORE;
}

static void ux500_msp_dai_compile_prot_desc_i2s(struct msp_protocol_desc *prot_desc)
{
	prot_desc->rx_phase_mode = MSP_DUAL_PHASE;
	prot_desc->tx_phase_mode = MSP_DUAL_PHASE;
	prot_desc->rx_phase2_start_mode =
		MSP_PHASE2_START_MODE_FRAME_SYNC;
	prot_desc->tx_phase2_start_mode =
		MSP_PHASE2_START_MODE_FRAME_SYNC;
	prot_desc->rx_bit_transfer_format = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_bit_transfer_format = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_frame_sync_pol = MSP_FRAME_SYNC_POL(MSP_FRAME_SYNC_POL_ACTIVE_LOW);
	prot_desc->rx_frame_sync_pol = MSP_FRAME_SYNC_POL_ACTIVE_LOW << RFSPOL_SHIFT;

	prot_desc->rx_frame_length_1 = MSP_FRAME_LENGTH_1;
	prot_desc->rx_frame_length_2 = MSP_FRAME_LENGTH_1;
	prot_desc->tx_frame_length_1 = MSP_FRAME_LENGTH_1;
	prot_desc->tx_frame_length_2 = MSP_FRAME_LENGTH_1;
	prot_desc->rx_element_length_1 = MSP_ELEM_LENGTH_16;
	prot_desc->rx_element_length_2 = MSP_ELEM_LENGTH_16;
	prot_desc->tx_element_length_1 = MSP_ELEM_LENGTH_16;
	prot_desc->tx_element_length_2 = MSP_ELEM_LENGTH_16;

	prot_desc->rx_clock_pol = MSP_RISING_EDGE;
	prot_desc->tx_clock_pol = MSP_FALLING_EDGE;

	prot_desc->tx_half_word_swap = MSP_HWS_NO_SWAP;
	prot_desc->rx_half_word_swap = MSP_HWS_NO_SWAP;
	prot_desc->compression_mode = MSP_COMPRESS_MODE_LINEAR;
	prot_desc->expansion_mode = MSP_EXPAND_MODE_LINEAR;
	prot_desc->spi_clk_mode = MSP_SPI_CLOCK_MODE_NON_SPI;
	prot_desc->spi_burst_mode = MSP_SPI_BURST_MODE_DISABLE;
	prot_desc->frame_sync_ignore = MSP_FRAME_SYNC_IGNORE;
}

static void ux500_msp_dai_compile_msp_config(struct snd_pcm_substream *substream,
					struct ux500_platform_drvdata *private,
					unsigned int rate,
					struct msp_config *msp_config)
{
	struct msp_protocol_desc *prot_desc = &msp_config->protocol_desc;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int fmt = private->fmt;

	memset(msp_config, 0, sizeof(*msp_config));

	msp_config->input_clock_freq = private->master_clk;

	msp_config->tx_fifo_config = TX_FIFO_ENABLE;
	msp_config->rx_fifo_config = RX_FIFO_ENABLE;
	msp_config->spi_clk_mode = SPI_CLK_MODE_NORMAL;
	msp_config->spi_burst_mode = 0;
	msp_config->def_elem_len = 1;
	msp_config->direction =
		substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		MSP_TRANSMIT_MODE : MSP_RECEIVE_MODE;
	msp_config->data_size = MSP_DATA_BITS_32;
	msp_config->work_mode = MSP_DMA_MODE;
	msp_config->frame_freq = rate;

	pr_debug("%s: input_clock_freq = %u, frame_freq = %u.\n",
	       __func__, msp_config->input_clock_freq, msp_config->frame_freq);
	/* To avoid division by zero in I2S-driver (i2s_setup) */
	prot_desc->total_clocks_for_one_frame = 1;

	prot_desc->rx_data_delay = private->data_delay;
	prot_desc->tx_data_delay = private->data_delay;

	pr_debug("%s: rate: %u channels: %d.\n",
			__func__,
			rate,
			runtime->channels);
	switch (fmt &
		(SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_MASTER_MASK)) {

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
		pr_debug("%s: SND_SOC_DAIFMT_I2S.\n",
			__func__);

		msp_config->default_protocol_desc = 1;
		msp_config->protocol = MSP_I2S_PROTOCOL;
		break;

	default:
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
		pr_debug("%s: SND_SOC_DAIFMT_I2S.\n",
			__func__);

		msp_config->data_size = MSP_DATA_BITS_16;
		msp_config->protocol = MSP_I2S_PROTOCOL;

		ux500_msp_dai_compile_prot_desc_i2s(prot_desc);
		break;

	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM:
		pr_debug("%s: PCM format.\n",
			__func__);
		msp_config->data_size = MSP_DATA_BITS_16;
		msp_config->protocol = MSP_PCM_PROTOCOL;

		ux500_msp_dai_compile_prot_desc_pcm(fmt, prot_desc);
		ux500_msp_dai_setup_multichannel(private, msp_config);
		ux500_msp_dai_setup_framing_pcm(private, rate, prot_desc);
		break;
	}

	ux500_msp_dai_setup_clocking(fmt, msp_config);
}

static int ux500_msp_dai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int ret = 0;
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msp_config msp_config;
	bool mode_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	u8 configflag = mode_playback ? PLAYBACK_CONFIGURED : CAPTURE_CONFIGURED;

	pr_debug("%s: MSP %d (%s): Enter.\n", __func__, dai->id, stream_str(substream));

	if (configflag & drvdata->configured) {

		ret = ux500_msp_i2s_close(
			drvdata->msp_i2s_drvdata,
			mode_playback ? DISABLE_TRANSMIT : DISABLE_RECEIVE);

		if (ret) {
			pr_err("%s: Error: MSP %d (%s): Unable to close i2s.\n",
				__func__,
				dai->id,
				stream_str(substream));
			goto cleanup;
		}

		drvdata->configured &= ~configflag;
	}

	pr_debug("%s: Setup dai (Rate: %u).\n", __func__, runtime->rate);
	ux500_msp_dai_compile_msp_config(substream,
					drvdata,
					runtime->rate,
					&msp_config);

	ret = ux500_msp_i2s_open(drvdata->msp_i2s_drvdata, &msp_config);
	if (ret < 0) {
		pr_err("%s: Error: msp_setup failed (ret = %d)!\n", __func__, ret);
		goto cleanup;
	}

	drvdata->configured |= configflag;

cleanup:
	return ret;
}

static int ux500_msp_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	unsigned int mask, slots_active;
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];
	struct ux500_msp_i2s_drvdata *msp_i2s = snd_soc_dai_get_drvdata(dai);

	pr_debug("%s: MSP %d (%s): Enter.\n",
			__func__,
			dai->id,
			stream_str(substream));

	switch (drvdata->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		if (params_channels(params) != 2) {
			pr_err("%s: Error: I2S requires channels = 2 "
				"(channels = %d)!\n",
				__func__,
				params_channels(params));
			return -EINVAL;
		}
		if (params_format(params) != SNDRV_PCM_FORMAT_S16_LE) {
			pr_err("%s: Error: I2S requires SNDRV_PCM_FORMAT_S16_LE\n",
				__func__);
			return -EINVAL;
		}

		msp_i2s->playback_dma_data.data_size = 16;
		msp_i2s->capture_dma_data.data_size = 16;
		break;
	case SND_SOC_DAIFMT_DSP_B:
	case SND_SOC_DAIFMT_DSP_A:

		mask = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			drvdata->tx_mask :
			drvdata->rx_mask;

		slots_active = hweight32(mask);

		pr_debug("TDM slots active: %d", slots_active);

		if (params_channels(params) != slots_active) {
			pr_err("%s: Error: PCM TDM format requires channels "
				"to match active slots "
				"(channels = %d, active slots = %d)!\n",
				__func__,
				params_channels(params),
				slots_active);
			return -EINVAL;
		}

		msp_i2s->playback_dma_data.data_size = drvdata->slot_width;
		msp_i2s->capture_dma_data.data_size = drvdata->slot_width;
		break;

	default:
		break;
	}

	return 0;
}

int ux500_msp_dai_set_data_delay(struct snd_soc_dai *dai, int delay)
{
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];

	pr_debug("%s: MSP %d: Enter.\n", __func__, dai->id);

	switch (delay) {
	case MSP_DELAY_0:
	case MSP_DELAY_1:
	case MSP_DELAY_2:
	case MSP_DELAY_3:
		break;
	default:
		goto unsupported_delay;
	}

	drvdata->data_delay = delay;
	return 0;

unsupported_delay:
	pr_err("%s: MSP %d: Error: Unsupported DAI delay (%d)!\n",
		__func__,
		dai->id,
		delay);
	return -EINVAL;
}

static int ux500_msp_dai_set_dai_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];

	pr_debug("%s: MSP %d: Enter.\n", __func__, dai->id);

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_MASTER_MASK)) {
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM:
		break;

	default:
		goto unsupported_format;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_IF:
		break;

	default:
		goto unsupported_format;
	}

	drvdata->fmt = fmt;
	return 0;

unsupported_format:
	pr_err("%s: MSP %d: Error: Unsupported DAI format (0x%x)!\n",
		__func__,
		dai->id,
		fmt);
	return -EINVAL;
}

static int ux500_msp_dai_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots,
				int slot_width)
{
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];
	unsigned int cap;

	if (!(slots == 1 || slots == 2 || slots == 8 || slots == 16)) {
		pr_err("%s: Error: Unsupported slots (%d)! "
			"Supported values are 1/2/8/16.\n",
			__func__,
			slots);
		return -EINVAL;
	}
	drvdata->slots = slots;

	if (!((slot_width == 16) || (slot_width == 20) || slot_width == 32)) {
		pr_err("%s: Error: Unsupported slots_width (%d)!. "
			"Supported values are 16, 20 or 32.\n",
			__func__,
			slot_width);
		return -EINVAL;
	}
	drvdata->slot_width = slot_width;

	switch (slots) {
	default:
	case 1:
		cap = 0x01;
		break;
	case 2:
		cap = 0x03;
		break;
	case 8:
		cap = 0xFF;
		break;
	case 16:
		cap = 0xFFFF;
		break;
	}

	drvdata->tx_mask = tx_mask & cap;
	drvdata->rx_mask = rx_mask & cap;

	return 0;
}

static int ux500_msp_dai_set_dai_sysclk(struct snd_soc_dai *dai,
	int clk_id,
	unsigned int freq,
	int dir)
{
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];

	pr_debug("%s: MSP %d: Enter. Clk id: %d, freq: %u.\n",
		__func__,
		dai->id,
		clk_id,
		freq);

	switch (clk_id) {
	case UX500_MSP_MASTER_CLOCK:
		drvdata->master_clk = freq;
		break;

	default:
		pr_err("%s: MSP %d: Invalid clkid: %d.\n",
			__func__,
			dai->id,
			clk_id);
	}

	return 0;
}

static int ux500_msp_dai_trigger(struct snd_pcm_substream *substream,
				int cmd,
				struct snd_soc_dai *dai)
{
	int ret = 0;
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[dai->id];

	pr_debug("%s: MSP %d (%s): Enter (msp->id = %d, cmd = %d).\n",
		__func__,
		dai->id,
		stream_str(substream),
		(int)drvdata->msp_i2s_drvdata->id,
		cmd);

	ret = ux500_msp_i2s_trigger(drvdata->msp_i2s_drvdata,
		cmd,
		substream->stream);

	return ret;
}

static int
ux500_msp_dai_probe(struct snd_soc_dai *dai)
{
	struct ux500_msp_i2s_drvdata *drvdata = snd_soc_dai_get_drvdata(dai);
	struct ux500_platform_drvdata *private = &platform_drvdata[dai->id];

	drvdata->playback_dma_data.dma_cfg = drvdata->msp->dma_cfg_tx;
	drvdata->capture_dma_data.dma_cfg = drvdata->msp->dma_cfg_rx;

	dai->playback_dma_data = &drvdata->playback_dma_data;
	dai->capture_dma_data = &drvdata->capture_dma_data;

	drvdata->playback_dma_data.data_size = private->slot_width;
	drvdata->capture_dma_data.data_size = private->slot_width;

	return 0;
}

static struct snd_soc_dai_driver ux500_msp_dai_drv[UX500_NBR_OF_DAI] = {
	{
		.name = "ux500-msp-i2s.0",
		.probe = ux500_msp_dai_probe,
		.id = 0,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
			.set_sysclk = ux500_msp_dai_set_dai_sysclk,
			.set_fmt = ux500_msp_dai_set_dai_fmt,
			.set_tdm_slot = ux500_msp_dai_set_tdm_slot,
			.startup = ux500_msp_dai_startup,
			.shutdown = ux500_msp_dai_shutdown,
			.prepare = ux500_msp_dai_prepare,
			.trigger = ux500_msp_dai_trigger,
			.hw_params = ux500_msp_dai_hw_params,
			}
		},
	},
	{
		.name = "ux500-msp-i2s.1",
		.probe = ux500_msp_dai_probe,
		.id = 1,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
			.set_sysclk = ux500_msp_dai_set_dai_sysclk,
			.set_fmt = ux500_msp_dai_set_dai_fmt,
			.set_tdm_slot = ux500_msp_dai_set_tdm_slot,
			.startup = ux500_msp_dai_startup,
			.shutdown = ux500_msp_dai_shutdown,
			.prepare = ux500_msp_dai_prepare,
			.trigger = ux500_msp_dai_trigger,
			.hw_params = ux500_msp_dai_hw_params,
			}
		},
	},
	{
		.name = "ux500-msp-i2s.2",
		.id = 2,
		.probe = ux500_msp_dai_probe,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
			.set_sysclk = ux500_msp_dai_set_dai_sysclk,
			.set_fmt = ux500_msp_dai_set_dai_fmt,
			.set_tdm_slot = ux500_msp_dai_set_tdm_slot,
			.startup = ux500_msp_dai_startup,
			.shutdown = ux500_msp_dai_shutdown,
			.prepare = ux500_msp_dai_prepare,
			.trigger = ux500_msp_dai_trigger,
			.hw_params = ux500_msp_dai_hw_params,
			}
		},
	},
	{
		.name = "ux500-msp-i2s.3",
		.probe = ux500_msp_dai_probe,
		.id = 3,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
			.set_sysclk = ux500_msp_dai_set_dai_sysclk,
			.set_fmt = ux500_msp_dai_set_dai_fmt,
			.set_tdm_slot = ux500_msp_dai_set_tdm_slot,
			.startup = ux500_msp_dai_startup,
			.shutdown = ux500_msp_dai_shutdown,
			.prepare = ux500_msp_dai_prepare,
			.trigger = ux500_msp_dai_trigger,
			.hw_params = ux500_msp_dai_hw_params,
			}
		},
	},
};
EXPORT_SYMBOL(ux500_msp_dai_drv);

static int ux500_msp_drv_probe(struct platform_device *pdev)
{
	struct ux500_msp_i2s_drvdata *msp_i2s_drvdata;
	struct ux500_platform_drvdata *drvdata;
	struct msp_i2s_platform_data *platform_data;
	int id;
	int ret = 0;

	pr_err("%s: Enter (pdev->name = %s).\n", __func__, pdev->name);

	platform_data = (struct msp_i2s_platform_data *)pdev->dev.platform_data;
	msp_i2s_drvdata = ux500_msp_i2s_init(pdev, platform_data);
	if (!msp_i2s_drvdata) {
		pr_err("%s: ERROR: ux500_msp_i2s_init failed!", __func__);
		return -EINVAL;
	}

	id = msp_i2s_drvdata->id;
	drvdata = &platform_drvdata[id];
	drvdata->msp_i2s_drvdata = msp_i2s_drvdata;

	pr_info("%s: Registering ux500-msp-dai SoC CPU-DAI.\n", __func__);
	ret = snd_soc_register_dai(&pdev->dev, &ux500_msp_dai_drv[id]);
	if (ret < 0) {
		pr_err("Error: %s: Failed to register MSP %d.\n", __func__, id);
		return ret;
	}

	return ret;
}

static int ux500_msp_drv_remove(struct platform_device *pdev)
{
	struct ux500_msp_i2s_drvdata *msp_i2s_drvdata = dev_get_drvdata(&pdev->dev);
	struct ux500_platform_drvdata *drvdata = &platform_drvdata[msp_i2s_drvdata->id];

	pr_info("%s: Unregister ux500-msp-dai ASoC CPU-DAI.\n", __func__);
	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(ux500_msp_dai_drv));

	ux500_msp_i2s_exit(msp_i2s_drvdata);
	drvdata->msp_i2s_drvdata = NULL;

	return 0;
}

int ux500_msp_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ux500_msp_i2s_drvdata *msp_i2s_drvdata = dev_get_drvdata(&pdev->dev);

	pr_debug("%s: Enter (pdev->name = %s).\n", __func__, pdev->name);

	return ux500_msp_i2s_suspend(msp_i2s_drvdata);
}

int ux500_msp_drv_resume(struct platform_device *pdev)
{
	struct ux500_msp_i2s_drvdata *msp_i2s_drvdata = dev_get_drvdata(&pdev->dev);

	pr_debug("%s: Enter (pdev->name = %s).\n", __func__, pdev->name);

	return ux500_msp_i2s_resume(msp_i2s_drvdata);
}

static struct platform_driver msp_i2s_driver = {
	.driver = {
		.name = "ux500-msp-i2s",
		.owner = THIS_MODULE,
	},
	.probe = ux500_msp_drv_probe,
	.remove = ux500_msp_drv_remove,
	.suspend = ux500_msp_drv_suspend,
	.resume = ux500_msp_drv_resume,
};

static int __init ux500_msp_init(void)
{
	pr_info("%s: Register ux500-msp-dai platform driver.\n", __func__);
	return platform_driver_register(&msp_i2s_driver);
}

static void __exit ux500_msp_exit(void)
{
	pr_info("%s: Unregister ux500-msp-dai platform driver.\n", __func__);
	platform_driver_unregister(&msp_i2s_driver);
}

module_init(ux500_msp_init);
module_exit(ux500_msp_exit);

MODULE_LICENSE("GPLv2");
