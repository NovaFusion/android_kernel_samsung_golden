/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#ifndef AV8100_AUDIO_CODEC_H
#define AV8100_AUDIO_CODEC_H

/* Supported sampling rates */
#define AV8100_SUPPORTED_RATE (SNDRV_PCM_RATE_48000)

/* Supported data formats */
#define AV8100_SUPPORTED_FMT (SNDRV_PCM_FMTBIT_S16_LE)

/* TDM-slot mask */
#define AV8100_CODEC_MASK_MONO		0x0001
#define AV8100_CODEC_MASK_STEREO	0x0005
#define AV8100_CODEC_MASK_2DOT1		0x0015
#define AV8100_CODEC_MASK_QUAD		0x0505
#define AV8100_CODEC_MASK_5DOT0		0x0545
#define AV8100_CODEC_MASK_5DOT1		0x0555
#define AV8100_CODEC_MASK_7DOT0		0x5545
#define AV8100_CODEC_MASK_7DOT1		0x5555

enum hdmi_audio_coding_type {
	AV8100_CODEC_CT_REFER,
	AV8100_CODEC_CT_IEC60958_PCM,
	AV8100_CODEC_CT_AC3,
	AV8100_CODEC_CT_MPEG1,
	AV8100_CODEC_CT_MP3,
	AV8100_CODEC_CT_MPEG2,
	AV8100_CODEC_CT_AAC,
	AV8100_CODEC_CT_DTS,
	AV8100_CODEC_CT_ATRAC,
	AV8100_CODEC_CT_ONE_BIT_AUDIO,
	AV8100_CODEC_CT_DOLBY_DIGITAL,
	AV8100_CODEC_CT_DTS_HD,
	AV8100_CODEC_CT_MAT,
	AV8100_CODEC_CT_DST,
	AV8100_CODEC_CT_WMA_PRO
};

enum hdmi_audio_channel_count {
	AV8100_CODEC_CC_REFER,
	AV8100_CODEC_CC_2CH,
	AV8100_CODEC_CC_3CH,
	AV8100_CODEC_CC_4CH,
	AV8100_CODEC_CC_5CH,
	AV8100_CODEC_CC_6CH,
	AV8100_CODEC_CC_7CH,
	AV8100_CODEC_CC_8CH
};

enum hdmi_sampling_frequency {
	AV8100_CODEC_SF_REFER,
	AV8100_CODEC_SF_32KHZ,
	AV8100_CODEC_SF_44_1KHZ,
	AV8100_CODEC_SF_48KHZ,
	AV8100_CODEC_SF_88_2KHZ,
	AV8100_CODEC_SF_96KHZ,
	AV8100_CODEC_SF_176_4KHZ,
	AV8100_CODEC_SF_192KHZ
};

enum hdmi_sample_size {
	AV8100_CODEC_SS_REFER,
	AV8100_CODEC_SS_16BIT,
	AV8100_CODEC_SS_20BIT,
	AV8100_CODEC_SS_24BIT
};

enum hdmi_speaker_placement {
	AV8100_CODEC_SP_FL,	/* Front Left */
	AV8100_CODEC_SP_FC,	/* Front Center */
	AV8100_CODEC_SP_FR,	/* Front Right */
	AV8100_CODEC_SP_FLC,	/* Front Left Center */
	AV8100_CODEC_SP_FRC,	/* Front Right Center */
	AV8100_CODEC_SP_RL,	/* Rear Left */
	AV8100_CODEC_SP_RC,	/* Rear Center */
	AV8100_CODEC_SP_RR,	/* Rear Right */
	AV8100_CODEC_SP_RLC,	/* Rear Left Center */
	AV8100_CODEC_SP_RRC,	/* Rear Right Center */
	AV8100_CODEC_SP_LFE,	/* Low Frequency Effekt */
};

enum hdmi_channel_allocation {
	AV8100_CODEC_CA_FL_FR,				/* 0x00, Stereo */
	AV8100_CODEC_CA_FL_FR_LFE,			/* 0x01, 2.1 */
	AV8100_CODEC_CA_FL_FR_FC,			/* 0x02*/
	AV8100_CODEC_CA_FL_FR_LFE_FC,			/* 0x03*/
	AV8100_CODEC_CA_FL_FR_RC,			/* 0x04*/
	AV8100_CODEC_CA_FL_FR_LFE_RC,			/* 0x05*/
	AV8100_CODEC_CA_FL_FR_FC_RC,			/* 0x06*/
	AV8100_CODEC_CA_FL_FR_LFE_FC_RC,		/* 0x07*/
	AV8100_CODEC_CA_FL_FR_RL_RR,			/* 0x08, Quad */
	AV8100_CODEC_CA_FL_FR_LFE_RL_RR,		/* 0x09*/
	AV8100_CODEC_CA_FL_FR_FC_RL_RR,			/* 0x0a, 5.0*/
	AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR,		/* 0x0b, 5.1*/
	AV8100_CODEC_CA_FL_FR_RL_RR_RC,			/* 0x0c*/
	AV8100_CODEC_CA_FL_FR_LFE_RL_RR_RC,		/* 0x0d*/
	AV8100_CODEC_CA_FL_FR_RC_RL_RR_RC,		/* 0x0e*/
	AV8100_CODEC_CA_FL_FR_LFE_RC_RL_RR_RC,		/* 0x0f*/
	AV8100_CODEC_CA_FL_FR_RL_RR_RLC_RRC,		/* 0x10*/
	AV8100_CODEC_CA_FL_FR_LFE_RL_RR_RLC_RRC,	/* 0x11*/
	AV8100_CODEC_CA_FL_FR_FC_RL_RR_RLC_RRC,		/* 0x12*/
	AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR_RLC_RRC,	/* 0x13*/
	AV8100_CODEC_CA_FL_FR_FLC_FRC,			/* 0x14*/
	AV8100_CODEC_CA_FL_FR_LFE_FLC_FRC,		/* 0x15*/
	AV8100_CODEC_CA_FL_FR_FC_FLC_FRC,		/* 0x16*/
	AV8100_CODEC_CA_FL_FR_LFE_FC_FLC_FRC,		/* 0x17*/
	AV8100_CODEC_CA_FL_FR_RC_FLC_FRC,		/* 0x18*/
	AV8100_CODEC_CA_FL_FR_LFE_RC_FLC_FRC,		/* 0x19*/
	AV8100_CODEC_CA_FL_FR_FC_RC_FLC_FRC,		/* 0x1a*/
	AV8100_CODEC_CA_FL_FR_LFE_FR_FC_RC_FLC_FRC,	/* 0x1b*/
	AV8100_CODEC_CA_FL_FR_RL_RR_FLC_FRC,		/* 0x1c*/
	AV8100_CODEC_CA_FL_FR_LFE_RL_RR_FLC_FRC,	/* 0x1d*/
	AV8100_CODEC_CA_FL_FR_FC_RL_RR_FLC_FRC,		/* 0x1e*/
	AV8100_CODEC_CA_FL_FR_LFE_FC_RL_RR_FLC_FRC	/* 0x1f, 7.1 */
};

enum hdmi_level_shift_value {
	AV8100_CODEC_LSV_0DB,
	AV8100_CODEC_LSV_1DB,
	AV8100_CODEC_LSV_2DB,
	AV8100_CODEC_LSV_3DB,
	AV8100_CODEC_LSV_4DB,
	AV8100_CODEC_LSV_5DB,
	AV8100_CODEC_LSV_6DB,
	AV8100_CODEC_LSV_7DB,
	AV8100_CODEC_LSV_8DB,
	AV8100_CODEC_LSV_9DB,
	AV8100_CODEC_LSV_10DB,
	AV8100_CODEC_LSV_11DB,
	AV8100_CODEC_LSV_12DB,
	AV8100_CODEC_LSV_13DB,
	AV8100_CODEC_LSV_14DB,
	AV8100_CODEC_LSV_15DB
};

struct hdmi_audio_settings {
	enum hdmi_audio_channel_count	audio_channel_count;
	enum hdmi_sampling_frequency	sampling_frequency;
	enum hdmi_sample_size		sample_size;
	enum hdmi_channel_allocation	channel_allocation;
	enum hdmi_level_shift_value	level_shift_value;
	bool				downmix_inhibit;
};

/* Extended interface for codec-driver */
int av8100_audio_change_hdmi_audio_settings(struct snd_soc_dai *dai,
					struct hdmi_audio_settings *as);

#endif /* AV8100_AUDIO_CODEC_H */



