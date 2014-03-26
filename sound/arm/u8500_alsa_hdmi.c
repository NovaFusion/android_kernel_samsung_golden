/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL),
 * version 2.
 */

/* This include must be defined at this point */
//#include <sound/driver.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <mach/hardware.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*  alsa system  */
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/control.h>
#include "u8500_alsa_ab8500.h"
#include <mach/msp.h>
#include <mach/debug.h>

#define ALSA_NAME		"DRIVER ALSA HDMI"

/* enables/disables debug msgs */
#define DRIVER_DEBUG	    CONFIG_STM_ALSA_DEBUG
/* msg header represents this module */
#define DRIVER_DEBUG_PFX	ALSA_NAME
/* message level */
#define DRIVER_DBG	        KERN_ERR
#define ELEMENT_SIZE 0

extern char *power_state_in_texts[NUMBER_POWER_STATE];

static int u8500_hdmi_power_ctrl_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo);
static int u8500_hdmi_power_ctrl_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *uinfo);
static int u8500_hdmi_power_ctrl_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *uinfo);

void dump_msp2_registers();

#ifdef CONFIG_U8500_ACODEC_DMA

static void u8500_alsa_hdmi_dma_start(audio_stream_t * stream);
#define stm_trigger_hdmi(x) u8500_alsa_hdmi_dma_start(x)
static void inline stm_pause_hdmi(audio_stream_t * stream)
{
	if (stream->state == ALSA_STATE_UNPAUSE) {
		stream->state = ALSA_STATE_PAUSE;
	}
}
static void inline stm_unpause_hdmi(audio_stream_t * stream)
{
	if (stream->state == ALSA_STATE_PAUSE) {
		stream->state = ALSA_STATE_UNPAUSE;
		stm_trigger_hdmi(stream);
	}
}
static void inline stm_stop_hdmi(audio_stream_t * stream)
{
	stream->active = 0;
	stream->period = 0;
}
#else /* Polling */

static int spawn_hdmi_feeding_thread(audio_stream_t * stream);
static int hdmi_feeding_thread(void *data);
static void u8500_hdmi_pio_start(audio_stream_t * stream);

#define stm_trigger_hdmi(x) spawn_hdmi_feeding_thread(x);

static void inline stm_pause_hdmi(audio_stream_t * stream)
{
	stream->state = ALSA_STATE_PAUSE;
}
static void inline stm_unpause_hdmi(audio_stream_t * stream)
{
	stream->state = ALSA_STATE_UNPAUSE;
	complete(&stream->alsa_com);
}
static void inline stm_stop_hdmi(audio_stream_t * stream)
{
	stream->active = 0;
	stream->period = 0;
	if (stream->state == ALSA_STATE_PAUSE)
		complete(&stream->alsa_com);
}

#endif

extern struct driver_debug_st DBG_ST;
extern struct i2sdrv_data *i2sdrv[MAX_I2S_CLIENTS];

static void u8500_audio_hdmi_init(u8500_acodec_chip_t * chip);
int u8500_register_alsa_hdmi_controls(struct snd_card *card,
				      u8500_acodec_chip_t * u8500_chip);
static int snd_u8500_alsa_hdmi_open(struct snd_pcm_substream *substream);
static int snd_u8500_alsa_hdmi_close(struct snd_pcm_substream *substream);
static int snd_u8500_alsa_hdmi_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params);
static int snd_u8500_alsa_hdmi_hw_free(struct snd_pcm_substream *substream);
static int snd_u8500_alsa_hdmi_prepare(struct snd_pcm_substream *substream);
static int snd_u8500_alsa_hdmi_trigger(struct snd_pcm_substream *substream,
				       int cmd);
static snd_pcm_uframes_t snd_u8500_alsa_hdmi_pointer(struct snd_pcm_substream
						     *substream);
static int configure_hdmi_rate(struct snd_pcm_substream *);
static int configure_msp_hdmi(int sampling_freq, int channel_count);

int u8500_hdmi_rates[] = { 32000, 44100, 48000, 64000, 88200,
	96000, 128000, 176100, 192000
};

typedef enum {
	HDMI_SAMPLING_FREQ_32KHZ = 32,
	HDMI_SAMPLING_FREQ_44_1KHZ = 44,
	HDMI_SAMPLING_FREQ_48KHZ = 48,
	HDMI_SAMPLING_FREQ_64KHZ = 64,
	HDMI_SAMPLING_FREQ_88_2KHZ = 88,
	HDMI_SAMPLING_FREQ_96KHZ = 96,
	HDMI_SAMPLING_FREQ_128KHZ = 128,
	HDMI_SAMPLING_FREQ_176_1KHZ = 176,
	HDMI_SAMPLING_FREQ_192KHZ = 192
} t_hdmi_sample_freq;

static struct snd_pcm_ops snd_u8500_alsa_hdmi_playback_ops = {
	.open = snd_u8500_alsa_hdmi_open,
	.close = snd_u8500_alsa_hdmi_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_u8500_alsa_hdmi_hw_params,
	.hw_free = snd_u8500_alsa_hdmi_hw_free,
	.prepare = snd_u8500_alsa_hdmi_prepare,
	.trigger = snd_u8500_alsa_hdmi_trigger,
	.pointer = snd_u8500_alsa_hdmi_pointer,
};

static struct snd_pcm_ops snd_u8500_alsa_hdmi_capture_ops = {
	.open = snd_u8500_alsa_hdmi_open,
	.close = snd_u8500_alsa_hdmi_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_u8500_alsa_hdmi_hw_params,
	.hw_free = snd_u8500_alsa_hdmi_hw_free,
	.prepare = snd_u8500_alsa_hdmi_prepare,
	.trigger = snd_u8500_alsa_hdmi_trigger,
	.pointer = snd_u8500_alsa_hdmi_pointer,
};

/* Hardware description , this structure (struct snd_pcm_hardware )
 * contains the definitions of the fundamental hardware configuration.
 * This configuration will be applied on the runtime structure
 */
static struct snd_pcm_hardware snd_u8500_hdmi_playback_hw = {
	.info =
	    (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP |
	     SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_PAUSE),
	.formats =
	    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
	    SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_U16_BE,
	.rates = SNDRV_PCM_RATE_KNOT,
	.rate_min = MIN_RATE_PLAYBACK,
	.rate_max = MAX_RATE_PLAYBACK,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = NMDK_BUFFER_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = PAGE_SIZE,
	.periods_min = NMDK_BUFFER_SIZE / PAGE_SIZE,
	.periods_max = NMDK_BUFFER_SIZE / 128
};

static struct snd_pcm_hardware snd_u8500_hdmi_capture_hw = {
	.info =
	    (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP |
	     SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_PAUSE),
	.formats =
	    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE |
	    SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_U16_BE,
	.rates = SNDRV_PCM_RATE_KNOT,
	.rate_min = MIN_RATE_CAPTURE,
	.rate_max = MAX_RATE_CAPTURE,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = NMDK_BUFFER_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = PAGE_SIZE,
	.periods_min = NMDK_BUFFER_SIZE / PAGE_SIZE,
	.periods_max = NMDK_BUFFER_SIZE / 128
};

static struct snd_pcm_hw_constraint_list constraints_hdmi_rate = {
	.count = sizeof(u8500_hdmi_rates) / sizeof(u8500_hdmi_rates[0]),
	.list = u8500_hdmi_rates,
	.mask = 0,
};

/**
 * snd_card_u8500_alsa_hdmi_new - constructor for a new pcm cmponent
 * @chip - pointer to chip specific data
 * @device - specifies the card number
 */
int snd_card_u8500_alsa_hdmi_new(u8500_acodec_chip_t * chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err =
	     snd_pcm_new(chip->card, "u8500_hdmi", device, 1, 1, &pcm)) < 0) {
		stm_error(" : error in snd_pcm_new\n");
		return err;
	}

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_u8500_alsa_hdmi_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_u8500_alsa_hdmi_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	chip->pcm_hdmi = pcm;
	strcpy(pcm->name, "u8500_alsa_hdmi");

	u8500_audio_hdmi_init(pcm->private_data);
	return 0;
}

/**
* u8500_audio_hdmi_init
* @chip - pointer to u8500_acodec_chip_t structure.
*
* This function intialises the u8500 chip structure with default values
*/
static void u8500_audio_hdmi_init(u8500_acodec_chip_t * chip)
{
	audio_stream_t *ptr_audio_stream = NULL;

	ptr_audio_stream =
	    &chip->stream[ALSA_HDMI_DEV][SNDRV_PCM_STREAM_PLAYBACK];
	/* Setup DMA stuff */

	strlcpy(ptr_audio_stream->id, "u8500 hdmi playback",
		sizeof(ptr_audio_stream->id));

	ptr_audio_stream->stream_id = SNDRV_PCM_STREAM_PLAYBACK;

	/* default initialization  for playback */
	ptr_audio_stream->active = 0;
	ptr_audio_stream->period = 0;
	ptr_audio_stream->periods = 0;
	ptr_audio_stream->old_offset = 0;

	ptr_audio_stream =
	    &chip->stream[ALSA_HDMI_DEV][SNDRV_PCM_STREAM_CAPTURE];

	strlcpy(ptr_audio_stream->id, "u8500 hdmi capture",
		sizeof(ptr_audio_stream->id));

	ptr_audio_stream->stream_id = SNDRV_PCM_STREAM_CAPTURE;

	/* default initialization  for capture */
	ptr_audio_stream->active = 0;
	ptr_audio_stream->period = 0;
	ptr_audio_stream->periods = 0;
	ptr_audio_stream->old_offset = 0;

}

/**
 * snd_u8500_alsa_hdmi_open
 * @substream - pointer to the playback/capture substream structure
 *
 *  This routine is used by alsa framework to open a pcm stream .
 *  Here a dma pipe is requested and device is configured(default).
 */
static int snd_u8500_alsa_hdmi_open(struct snd_pcm_substream *substream)
{
	int error = 0, stream_id, status = 0;
	u8500_acodec_chip_t *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	audio_stream_t *ptr_audio_stream = NULL;

	stream_id = substream->pstr->stream;
	error = u8500_acodec_setuser(USER_ALSA);
	status = u8500_acodec_open(I2S_CLIENT_MSP2, stream_id);
	if (status) {
		printk("failed in getting open\n");
		return (-1);
	}

	error = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					   &constraints_hdmi_rate);
	if (error < 0) {
		stm_error
		    (": error initializing hdmi hw sample rate constraint\n");
		return error;
	}

	if ((error = configure_hdmi_rate(substream)))
		return error;

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK) {
		runtime->hw = snd_u8500_hdmi_playback_hw;
	} else {
		runtime->hw = snd_u8500_hdmi_capture_hw;
	}

	ptr_audio_stream = &chip->stream[ALSA_HDMI_DEV][stream_id];

	ptr_audio_stream->substream = substream;

	stm_config_hw(chip, substream, ALSA_HDMI_DEV, stream_id);
	sema_init(&(ptr_audio_stream->alsa_sem), 1);
	init_completion(&(ptr_audio_stream->alsa_com));

	ptr_audio_stream->state = ALSA_STATE_UNPAUSE;
	return 0;
}

/**
 * snd_u8500_alsa_hdmi_close
 * @substream - pointer to the playback/capture substream structure
 *
 *  This routine is used by alsa framework to close a pcm stream .
 *  Here a dma pipe is disabled and freed.
 */

static int snd_u8500_alsa_hdmi_close(struct snd_pcm_substream *substream)
{
	int stream_id, error = 0;
	u8500_acodec_chip_t *chip = snd_pcm_substream_chip(substream);
	audio_stream_t *ptr_audio_stream = NULL;

	stream_id = substream->pstr->stream;
	ptr_audio_stream = &chip->stream[ALSA_HDMI_DEV][stream_id];

	stm_close_alsa(chip, ALSA_HDMI_DEV, stream_id);

	/* reset the different variables to default */

	ptr_audio_stream->active = 0;
	ptr_audio_stream->period = 0;
	ptr_audio_stream->periods = 0;
	ptr_audio_stream->old_offset = 0;
	ptr_audio_stream->substream = NULL;

	/* Disable the MSP2 */
	error = u8500_acodec_unsetuser(USER_ALSA);
	u8500_acodec_close(I2S_CLIENT_MSP2, ACODEC_DISABLE_ALL);

	return error;

}

/**
 * snd_u8500_alsa_hdmi_hw_params
 * @substream - pointer to the playback/capture substream structure
 * @hw_params - specifies the hw parameters like format/no of channels etc
 *
 *  This routine is used by alsa framework to allocate a dma buffer
 *  used to transfer the data from user space to kernel space
 *
 */
static int snd_u8500_alsa_hdmi_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/**
 * snd_u8500_alsa_hdmi_hw_free
 * @substream - pointer to the playback/capture substream structure
 *
 *  This routine is used by alsa framework to deallocate a dma buffer
 *  allocated berfore by snd_u8500_alsa_pcm_hw_params
 */
static int snd_u8500_alsa_hdmi_hw_free(struct snd_pcm_substream *substream)
{
	stm_hw_free(substream);
	return 0;
}

/**
 * snd_u8500_alsa_hdmi_pointer
 * @substream - pointer to the playback/capture substream structure
 *
 *  This callback is called whene the pcm middle layer inquires the current
 *  hardware position on the buffer .The position is returned in frames
 *  ranged from 0 to buffer_size -1
 */
static snd_pcm_uframes_t snd_u8500_alsa_hdmi_pointer(struct snd_pcm_substream
						     *substream)
{
	unsigned int offset;
	u8500_acodec_chip_t *chip = snd_pcm_substream_chip(substream);
	audio_stream_t *stream =
	    &chip->stream[ALSA_HDMI_DEV][substream->pstr->stream];
	struct snd_pcm_runtime *runtime = stream->substream->runtime;

	offset = bytes_to_frames(runtime, stream->old_offset);
	if (offset < 0 || stream->old_offset < 0)
		stm_dbg(DBG_ST.alsa, " Offset=%i %i\n", offset,
			stream->old_offset);

	return offset;
}

/**
 * snd_u8500_alsa_hdmi_prepare
 * @substream - pointer to the playback/capture substream structure
 *
 *  This callback is called whene the pcm is "prepared" Here is possible
 *  to set the format type ,sample rate ,etc.The callback is called as
 *  well everytime a recovery after an underrun happens.
 */

static int snd_u8500_alsa_hdmi_prepare(struct snd_pcm_substream *substream)
{
	u8500_acodec_chip_t *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int error;

	if (chip->hdmi_params.sampling_freq != runtime->rate
	    || chip->hdmi_params.channel_count != runtime->channels) {
		stm_dbg(DBG_ST.alsa, " freq not same, %d %d\n",
			chip->hdmi_params.sampling_freq, runtime->rate);
		stm_dbg(DBG_ST.alsa, " channels not same, %d %d\n",
			chip->hdmi_params.channel_count, runtime->channels);
		if (chip->hdmi_params.channel_count != runtime->channels) {
			chip->hdmi_params.channel_count = runtime->channels;
			if ((error =
			     stm_config_hw(chip, substream, ALSA_HDMI_DEV,
					   -1))) {
				stm_dbg(DBG_ST.alsa,
					"In func %s, stm_config_hw fails",
					__FUNCTION__);
				return error;
			}
		}
		chip->hdmi_params.sampling_freq = runtime->rate;
		if ((error = configure_hdmi_rate(substream))) {
			stm_dbg(DBG_ST.alsa, "In func %s, configure_rate fails",
				__FUNCTION__);
			return error;
		}
	}

	return 0;
}

/**
 * snd_u8500_alsa_hdmi_trigger
 * @substream -  pointer to the playback/capture substream structure
 * @cmd - specifies the command : start/stop/pause/resume
 *
 *  This callback is called whene the pcm is started ,stopped or paused
 *  The action is specified in the second argument, SND_PCM_TRIGGER_XXX in
 *  <sound/pcm.h>.
 *  This callback is atomic and the interrupts are disabled , so you can't
 *  call other functions that need interrupts without possible risks
 */
static int snd_u8500_alsa_hdmi_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	int stream_id = substream->pstr->stream;
	audio_stream_t *stream = NULL;
	u8500_acodec_chip_t *chip = snd_pcm_substream_chip(substream);
	int error = 0;

	stream = &chip->stream[ALSA_HDMI_DEV][stream_id];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Start the pcm engine */
		stm_dbg(DBG_ST.alsa, " TRIGGER START\n");
		if (stream->active == 0) {
			stream->active = 1;
			stm_trigger_hdmi(stream);
			break;
		}
		stm_error(": H/w is busy\n");
		return -EINVAL;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		stm_dbg(DBG_ST.alsa, " SNDRV_PCM_TRIGGER_PAUSE_PUSH\n");
		if (stream->active == 1) {
			stm_pause_hdmi(stream);
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		stm_dbg(DBG_ST.alsa, " SNDRV_PCM_TRIGGER_PAUSE_RELEASE\n");
		if (stream->active == 1)
			stm_unpause_hdmi(stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		/* Stop the pcm engine */
		stm_dbg(DBG_ST.alsa, " TRIGGER STOP\n");
		if (stream->active == 1)
			stm_stop_hdmi(stream);
		break;
	default:
		stm_error(": invalid command in pcm trigger\n");
		return -EINVAL;
	}

	return error;

}

struct snd_kcontrol_new u8500_hdmi_power_ctrl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.device = 1,
	.subdevice = 0,
	.name = "HDMI Power",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.private_value = 0xfff,
	.info = u8500_hdmi_power_ctrl_info,
	.get = u8500_hdmi_power_ctrl_get,
	.put = u8500_hdmi_power_ctrl_put
};

/**
* u8500_hdmi_power_ctrl_info
* @kcontrol - pointer to the snd_kcontrol structure
* @uinfo - pointer to the snd_ctl_elem_info structure, this is filled by the function
*
*	This functions fills playback device info into user structure.
*/
static int u8500_hdmi_power_ctrl_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->value.enumerated.items = NUMBER_POWER_STATE;
	uinfo->count = 1;
	if (uinfo->value.enumerated.item >= NUMBER_POWER_STATE)
		uinfo->value.enumerated.item = NUMBER_POWER_STATE - 1;
	strcpy(uinfo->value.enumerated.name,
	       power_state_in_texts[uinfo->value.enumerated.item]);
	return 0;
}

/**
* u8500_hdmi_power_ctrl_get
* @kcontrol - pointer to the snd_kcontrol structure
* @uinfo - pointer to the snd_ctl_elem_info structure, this is filled by the function
*
*	This functions returns the current playback device selected.
*/
static int u8500_hdmi_power_ctrl_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *uinfo)
{
	u8500_acodec_chip_t *chip =
	    (u8500_acodec_chip_t *) snd_kcontrol_chip(kcontrol);
	uinfo->value.enumerated.item[0] = 0;
	return 0;
}

/**
* u8500_hdmi_power_ctrl_put
* @kcontrol - pointer to the snd_kcontrol structure
* @	.
*
*	This functions sets the playback device.
*/
static int u8500_hdmi_power_ctrl_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *uinfo)
{
	u8500_acodec_chip_t *chip =
	    (u8500_acodec_chip_t *) snd_kcontrol_chip(kcontrol);
	int changed = 0;
	t_ab8500_codec_error error;
	t_u8500_bool_state power_state;

	power_state = uinfo->value.enumerated.item[0];

	changed = 1;

	return changed;
}

int u8500_register_alsa_hdmi_controls(struct snd_card *card,
				      u8500_acodec_chip_t * u8500_chip)
{
	int error;

	if ((error =
	     snd_ctl_add(card,
			 snd_ctl_new1(&u8500_hdmi_power_ctrl,
				      u8500_chip))) < 0) {
		stm_error
		    (": error initializing u8500_hdmi_power_ctrl interface \n\n");
		return (-1);
	}

	return 0;
}

/**
* configure_hdmi_rate
* @substream - pointer to the playback/capture substream structure
*
*	This functions configures audio codec in to stream frequency frequency
*/
static int configure_hdmi_rate(struct snd_pcm_substream *substream)
{
	t_hdmi_sample_freq hdmi_sampling_freq;

	u8500_acodec_chip_t *chip = snd_pcm_substream_chip(substream);

	switch (chip->hdmi_params.sampling_freq) {
	case 32000:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_32KHZ;
		break;
	case 44100:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_44_1KHZ;
		break;
	case 48000:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_48KHZ;
		break;
	case 64000:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_64KHZ;
		break;
	case 88200:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_88_2KHZ;
		break;
	case 96000:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_96KHZ;
		break;
	case 128000:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_128KHZ;
		break;
	case 176100:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_176_1KHZ;
		break;
	case 192000:
		hdmi_sampling_freq = HDMI_SAMPLING_FREQ_192KHZ;
	default:
		stm_error("not supported frequnecy\n");
		return -EINVAL;
	}

	configure_msp_hdmi(hdmi_sampling_freq, chip->hdmi_params.channel_count);

	return 0;

}

static int configure_msp_hdmi(int sampling_freq, int channel_count)
{
	struct i2s_device *i2s_dev = i2sdrv[I2S_CLIENT_MSP2]->i2s;
	struct msp_config msp_config;
	t_ab8500_codec_error error_status = AB8500_CODEC_OK;

	memset(&msp_config, 0, sizeof(msp_config));


	if (i2sdrv[I2S_CLIENT_MSP2]->flag) {
		stm_dbg(DBG_ST.acodec, " I2S controller not available\n");
		return -1;
	}

	/* MSP configuration  */

	msp_config.tx_clock_sel = 0;
	msp_config.rx_clock_sel = 0;

	msp_config.tx_frame_sync_sel = 0;
	msp_config.rx_frame_sync_sel = 0;

	msp_config.input_clock_freq = MSP_INPUT_FREQ_48MHZ;
	msp_config.srg_clock_sel = 0;

	msp_config.rx_frame_sync_pol = RX_FIFO_SYNC_HI;
	msp_config.tx_frame_sync_pol = TX_FIFO_SYNC_HI;

	msp_config.rx_fifo_config = 0;
	msp_config.tx_fifo_config = TX_FIFO_ENABLE;

	msp_config.spi_clk_mode = SPI_CLK_MODE_NORMAL;
	msp_config.spi_burst_mode = 0;
	msp_config.tx_data_enable = 0;
	msp_config.loopback_enable = 0;
	msp_config.default_protocol_desc = 1;
	msp_config.direction = MSP_TRANSMIT_MODE;
	msp_config.protocol = MSP_I2S_PROTOCOL;
	msp_config.frame_size = ELEMENT_SIZE;
	msp_config.frame_freq = sampling_freq;
	msp_config.def_elem_len = 0;
	/* enable msp for both tr and rx mode with dma data transfer.
	   THIS IS NOW DONE SEPARATELY from SAA. */
	msp_config.data_size = MSP_DATA_SIZE_16BIT;

#ifdef CONFIG_U8500_ACODEC_DMA
	msp_config.work_mode = MSP_DMA_MODE;
#elif defined(CONFIG_U8500_ACODEC_POLL)
	msp_config.work_mode = MSP_POLLING_MODE;
#else
	msp_config.work_mode = MSP_INTERRUPT_MODE;
#endif
	msp_config.default_protocol_desc = 0;

	msp_config.protocol_desc.rx_phase_mode = MSP_DUAL_PHASE;
	msp_config.protocol_desc.tx_phase_mode = MSP_DUAL_PHASE;
	msp_config.protocol_desc.rx_phase2_start_mode =
	    MSP_PHASE2_START_MODE_FRAME_SYNC;
	msp_config.protocol_desc.tx_phase2_start_mode =
	    MSP_PHASE2_START_MODE_FRAME_SYNC;
	msp_config.protocol_desc.rx_bit_transfer_format = MSP_BTF_MS_BIT_FIRST;
	msp_config.protocol_desc.tx_bit_transfer_format = MSP_BTF_MS_BIT_FIRST;
	msp_config.protocol_desc.rx_frame_length_1 = MSP_FRAME_LENGTH_1;
	msp_config.protocol_desc.rx_frame_length_2 = MSP_FRAME_LENGTH_1;
	msp_config.protocol_desc.tx_frame_length_1 = MSP_FRAME_LENGTH_1;
	msp_config.protocol_desc.tx_frame_length_2 = MSP_FRAME_LENGTH_1;
	msp_config.protocol_desc.rx_element_length_1 = MSP_ELEM_LENGTH_16;
	msp_config.protocol_desc.rx_element_length_2 = MSP_ELEM_LENGTH_16;
	msp_config.protocol_desc.tx_element_length_1 = MSP_ELEM_LENGTH_16;
	msp_config.protocol_desc.tx_element_length_2 = MSP_ELEM_LENGTH_16;
	msp_config.protocol_desc.rx_data_delay = MSP_DELAY_1;
	msp_config.protocol_desc.tx_data_delay = MSP_DELAY_1;
	msp_config.protocol_desc.rx_clock_pol = MSP_RISING_EDGE;
	msp_config.protocol_desc.tx_clock_pol = 0;
	msp_config.protocol_desc.rx_frame_sync_pol =
	    MSP_FRAME_SYNC_POL_ACTIVE_HIGH;
	msp_config.protocol_desc.tx_frame_sync_pol =
	    MSP_FRAME_SYNC_POL_ACTIVE_HIGH;
	msp_config.protocol_desc.rx_half_word_swap = MSP_HWS_NO_SWAP;
	msp_config.protocol_desc.tx_half_word_swap = MSP_HWS_NO_SWAP;
	msp_config.protocol_desc.compression_mode = MSP_COMPRESS_MODE_LINEAR;
	msp_config.protocol_desc.expansion_mode = MSP_EXPAND_MODE_LINEAR;
	msp_config.protocol_desc.spi_clk_mode = MSP_SPI_CLOCK_MODE_NON_SPI;
	msp_config.protocol_desc.spi_burst_mode = MSP_SPI_BURST_MODE_DISABLE;
	msp_config.protocol_desc.frame_period = 63;
	msp_config.protocol_desc.frame_width = 31;
	msp_config.protocol_desc.total_clocks_for_one_frame = 64;
	msp_config.multichannel_configured = 0;
	msp_config.multichannel_config.tx_multichannel_enable = 0;
	/* Channel 1 and channel 3 */
	msp_config.multichannel_config.tx_channel_0_enable = 0x0000005;
	msp_config.multichannel_config.tx_channel_1_enable = 0x0000000;
	msp_config.multichannel_config.tx_channel_2_enable = 0x0000000;
	msp_config.multichannel_config.tx_channel_3_enable = 0x0000000;
	error_status = i2s_setup(i2s_dev->controller, &msp_config);

#ifdef CONFIG_DEBUG
	{
		dump_msp2_registers();
	}
#endif

	if (error_status < 0) {
		printk("error in msp enable, error_status is %d\n",
		       error_status);
		return error_status;
	}

	return 0;

}

#ifdef CONFIG_U8500_ACODEC_DMA
/**
 * u8500_alsa_hdmi_dma_start - used to transmit or recive a dma chunk
 * @stream - specifies the playback/record stream structure
 */
static void u8500_alsa_hdmi_dma_start(audio_stream_t * stream)
{
	unsigned int offset, dma_size, stream_id;

	struct snd_pcm_substream *substream = stream->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;

	stream_id = substream->pstr->stream;

	dma_size = frames_to_bytes(runtime, runtime->period_size);
	offset = dma_size * stream->period;
	stream->old_offset = offset;

	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
#ifdef CONFIG_U8500_ACODEC_DMA
		u8500_acodec_send_data(I2S_CLIENT_MSP2,
				       (void *)(runtime->dma_addr + offset),
				       dma_size, 1);
#else
		u8500_acodec_send_data(I2S_CLIENT_MSP2,
				       (void *)(runtime->dma_area + offset),
				       dma_size, 0);
#endif
	else
#ifdef CONFIG_U8500_ACODEC_DMA
		u8500_acodec_receive_data(I2S_CLIENT_MSP2,
					  (void *)(runtime->dma_addr + offset),
					  dma_size, 1);
#else
		u8500_acodec_receive_data(I2S_CLIENT_MSP2,
					  (void *)(runtime->dma_area + offset),
					  dma_size, 0);
#endif

	stm_dbg(DBG_ST.alsa, " DMA Transfer started\n");
	stm_dbg(DBG_ST.alsa, " address = %x  size=%d\n",
		(runtime->dma_addr + offset), dma_size);

	stream->period++;
	stream->period %= runtime->periods;
	stream->periods++;


}

#else

/**
* acodec_feeding_thread
* @stream - pointer to the playback/capture audio_stream_t structure
*
*	This function creates a kernel thread .
*/

static int spawn_hdmi_feeding_thread(audio_stream_t * stream)
{
	pid_t pid;

	pid =
	    kernel_thread(hdmi_feeding_thread, stream,
			  CLONE_FS | CLONE_SIGHAND);

	return 0;
}

/**
* hdmi_feeding_thread
* @data - void pointer to the playback/capture audio_stream_t structure
*
*	This thread sends/receive data to MSP while stream is active
*/
static int hdmi_feeding_thread(void *data)
{
	audio_stream_t *stream = (audio_stream_t *) data;

	daemonize("hdmi_feeding_thread");
	allow_signal(SIGKILL);
	down(&stream->alsa_sem);

	while ((!signal_pending(current)) && (stream->active)) {
		if (stream->state == ALSA_STATE_PAUSE)
			wait_for_completion(&(stream->alsa_com));

		u8500_hdmi_pio_start(stream);
		if (stream->substream)
			snd_pcm_period_elapsed(stream->substream);
	}

	up(&stream->alsa_sem);

	return 0;
}

/**
* u8500_hdmi_pio_start
* @stream - pointer to the playback/capture audio_stream_t structure
*
*  This function sends/receive one chunck of stream data to/from MSP
*/
static void u8500_hdmi_pio_start(audio_stream_t * stream)
{
	unsigned int offset, dma_size, stream_id;
	struct snd_pcm_substream *substream = stream->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	stream_id = substream->pstr->stream;

	dma_size = frames_to_bytes(runtime, runtime->period_size);
	offset = dma_size * stream->period;
	stream->old_offset = offset;

	stm_dbg(DBG_ST.alsa, " Transfer started\n");
	stm_dbg(DBG_ST.alsa, " address = %x  size=%d\n",
		(runtime->dma_addr + offset), dma_size);

	/* Send our stuff */
	if (stream_id == SNDRV_PCM_STREAM_PLAYBACK)
#ifdef CONFIG_U8500_ACODEC_DMA
		u8500_acodec_send_data(I2S_CLIENT_MSP2,
				       (void *)(runtime->dma_addr + offset),
				       dma_size, 1);
#else
		u8500_acodec_send_data(I2S_CLIENT_MSP2,
				       (void *)(runtime->dma_area + offset),
				       dma_size, 0);
#endif
	else
#ifdef CONFIG_U8500_ACODEC_DMA
		u8500_acodec_receive_data(I2S_CLIENT_MSP2,
					  (void *)(runtime->dma_addr + offset),
					  dma_size, 1);
#else
		u8500_acodec_receive_data(I2S_CLIENT_MSP2,
					  (void *)(runtime->dma_area + offset),
					  dma_size, 0);
#endif

	stream->period++;
	stream->period %= runtime->periods;
	stream->periods++;
}
#endif

void dump_msp2_registers()
{
	int i;

	stm_dbg(DBG_ST.acodec, "\nMSP_2 base add = 0x%x\n",
		(unsigned int)U8500_MSP2_BASE);

	for (i = 0; i < 0x40; i += 4)
		stm_dbg(DBG_ST.acodec, "msp[0x%x]=0x%x\n", i,
			readl((char *)(IO_ADDRESS(U8500_MSP2_BASE) + i)));

	return 0;
}
