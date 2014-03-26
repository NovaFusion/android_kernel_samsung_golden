/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Deepak Karda
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef _U8500_ALSA_H_
#define _U8500_ALSA_H_

#ifdef CONFIG_U8500_AB8500_CUT10
#include <mach/ab8500_codec_v1_0.h>
//#include <mach/ab8500_codec_p_v1_0.h>
#else
//#include <mach/ab8500_codec_p.h>
#include <mach/ab8500_codec.h>
#endif
#include <mach/u8500_acodec_ab8500.h>

#define DEFAULT_SAMPLE_RATE      48000
#define NMDK_BUFFER_SIZE 	(64*1024)
#define U8500_ALSA_DRIVER	"u8500_alsa"

#define MAX_NUMBER_OF_DEVICES		3	/* ALSA_PCM, ALSA_BT, ALSA_HDMI */
#define MAX_NUMBER_OF_STREAMS		2	/* PLAYBACK, CAPTURE */

#define ALSA_PCM_DEV				0
#define ALSA_BT_DEV					2
#define ALSA_HDMI_DEV				1

/* Debugging stuff */
#ifndef CONFIG_DEBUG_USER
#define DEBUG_LEVEL 0
#else
#define DEBUG_LEVEL 10
#endif

#if DEBUG_LEVEL > 0
static int u8500_acodec_debug = DEBUG_LEVEL;
#define DEBUG(n, args...) do { if (u8500_acodec_debug>(n)) printk(args); } while (0)
#else
#define DEBUG(n, args...) do { } while (0)
#endif
enum alsa_state {
	ALSA_STATE_PAUSE,
	ALSA_STATE_UNPAUSE
};

/* audio stream definition */
typedef struct audio_stream_s {
	char id[64];		/* module identifier  string */
	int stream_id;		/* stream identifier  */
	int status;
	int active;		/* we are using this stream for transfer now */
	int period;		/* current transfer period */
	int periods;		/* current count of periods registerd in the DMA engine */
	enum alsa_state state;
	unsigned int old_offset;
	struct snd_pcm_substream *substream;
	unsigned int exchId;
	snd_pcm_uframes_t played_frame;
	struct semaphore alsa_sem;
	struct completion alsa_com;

} audio_stream_t;

typedef struct hdmi_params_s {
	int sampling_freq;
	int channel_count;
} hdmi_params_t;

/* chip structure definition */
typedef struct u8500_acodec_s {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm *pcm_hdmi;
	struct snd_pcm *pcm_bt;
	unsigned int freq;
	unsigned int channels;
	unsigned int input_lvolume;
	unsigned int input_rvolume;
	unsigned int output_lvolume;
	unsigned int output_rvolume;
	t_ab8500_codec_src input_device;
	t_ab8500_codec_dest output_device;
	t_u8500_bool_state analog_lpbk;
	t_u8500_bool_state digital_lpbk;
	t_u8500_bool_state playback_switch;
	t_u8500_bool_state capture_switch;
	t_u8500_bool_state tdm8_ch_mode;
	t_u8500_bool_state direct_rendering_mode;
	t_u8500_pmc_rendering_state burst_fifo_mode;
	t_u8500_pmc_rendering_state fm_playback_mode;
	t_u8500_pmc_rendering_state fm_tx_mode;
	audio_stream_t stream[MAX_NUMBER_OF_DEVICES][MAX_NUMBER_OF_STREAMS];
	hdmi_params_t hdmi_params;
} u8500_acodec_chip_t;

void u8500_alsa_dma_start(audio_stream_t * stream);

#if (defined(CONFIG_U8500_ACODEC_DMA) || defined(CONFIG_U8500_ACODEC_INTR))

#define stm_trigger_alsa(x) u8500_alsa_dma_start(x)
static void inline stm_pause_alsa(audio_stream_t * stream)
{
	if (stream->state == ALSA_STATE_UNPAUSE) {
		stream->state = ALSA_STATE_PAUSE;
	}

}
static void inline stm_unpause_alsa(audio_stream_t * stream)
{
	if (stream->state == ALSA_STATE_PAUSE) {
		stream->state = ALSA_STATE_UNPAUSE;
		stm_trigger_alsa(stream);
	}
}
static void inline stm_stop_alsa(audio_stream_t * stream)
{
	stream->active = 0;
	stream->period = 0;

}
static void inline stm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
}

#define stm_close_alsa(x, y,z)
#define stm_config_hw(w,x, y, z) 0

#else ////// CONFIG_U8500_ACODEC_POLL ////////////

int spawn_acodec_feeding_thread(audio_stream_t * stream);
//static int configure_dmadev_acodec(struct snd_pcm_substream *substream);

#define stm_trigger_alsa(x) spawn_acodec_feeding_thread(x)
#define stm_close_alsa(x, y,z)
#define stm_config_hw(w,x, y, z) 0
#define stm_hw_free(x)
static void inline stm_pause_alsa(audio_stream_t * stream)
{
	stream->state = ALSA_STATE_PAUSE;
}
static void inline stm_unpause_alsa(audio_stream_t * stream)
{
	stream->state = ALSA_STATE_UNPAUSE;
	complete(&stream->alsa_com);
}
static void inline stm_stop_alsa(audio_stream_t * stream)
{
	stream->active = 0;
	stream->period = 0;
	if (stream->state == ALSA_STATE_PAUSE)
		complete(&stream->alsa_com);
}

#endif
#endif /*END OF HEADER FILE */
