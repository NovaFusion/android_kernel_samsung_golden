/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Mikko J. Lehto <mikko.lehto@symbio.com>,
 *         Mikko Sarmanne <mikko.sarmanne@symbio.com>,
 *         Jarmo K. Kuronen <jarmo.kuronen@symbio.com>,
 *         Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Kristoffer Karlsson <kristoffer.karlsson@stericsson.com>,
 *         Johan Bohlin <johan.bohlin@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500/ux500_sysctrl.h>
#include <linux/mfd/dbx500-prcmu.h>
#include "ab8500_audio.h"

/* To convert register definition shifts to masks */
#define BMASK(bsft)	(1 << (bsft))

/* Register in AB8500_SYS_CTRL1_BLOCK for VAMIC12 low power mode */
#define REG_CTRLUSBUICCPUD 0x54
#define REG_CTRLUSBUICCPUD_VAMIC12LPMODEENA_MASK 0x8

/* Macrocell value definitions */
#define CLK_32K_OUT2_DISABLE			0x01
#define INACTIVE_RESET_AUDIO			0x02
#define ENABLE_AUDIO_CLK_TO_AUDIO_BLK		0x10
#define ENABLE_VINTCORE12_SUPPLY		0x04
#define GPIO27_DIR_OUTPUT			0x04
#define GPIO29_DIR_OUTPUT			0x10
#define GPIO31_DIR_OUTPUT			0x40

/* Macrocell register definitions */
#define AB8500_CTRL3_REG			0x0200
#define AB8500_GPIO_DIR4_REG			0x1013

/* Nr of FIR/IIR-coeff banks in ANC-block */
#define AB850X_NR_OF_ANC_COEFF_BANKS		2

/* Minimum duration to keep ANC IIR Init bit high or
low before proceeding with the configuration sequence */
#define AB850X_ANC_SM_DELAY			2000

/* Chipset revision definitions */
#define AB850X_CUT1P0				0x10
#define AB850X_CUT1P2				0x12
#define AB850X_CUT2P0				0x20
#define AB850X_CUT3P0				0x30
#define AB850X_CUT3P1				0x31

/*
 * AB850X register cache & default register settings
 */
static const u8 ab850x_reg_cache[] = {
	0x00, /* REG_POWERUP		(0x00) */
	0x00, /* REG_AUDSWRESET		(0x01) */
	0x00, /* REG_ADPATHENA		(0x02) */
	0x00, /* REG_DAPATHENA		(0x03) */
	0x00, /* REG_ANACONF1		(0x04) */
	0x0F, /* REG_ANACONF2		(0x05) */
	0x00, /* REG_DIGMICCONF		(0x06) */
	0x00, /* REG_ANACONF3		(0x07) */
	0x00, /* REG_ANACONF4		(0x08) */
	0x00, /* REG_DAPATHCONF		(0x09) */
	0x40, /* REG_MUTECONF		(0x0A) */
	0x01, /* REG_SHORTCIRCONF	(0x0B) */
	0x01, /* REG_ANACONF5		(0x0C) */
	0x00, /* REG_ENVCPCONF		(0x0D) */
	0x00, /* REG_SIGENVCONF		(0x0E) */
	0x3F, /* REG_PWMGENCONF1	(0x0F) */
	0x32, /* REG_PWMGENCONF2	(0x10) */
	0x32, /* REG_PWMGENCONF3	(0x11) */
	0x32, /* REG_PWMGENCONF4	(0x12) */
	0x32, /* REG_PWMGENCONF5	(0x13) */
	0x0F, /* REG_ANAGAIN1		(0x14) */
	0x0F, /* REG_ANAGAIN2		(0x15) */
	0x22, /* REG_ANAGAIN3		(0x16) */
	0x55, /* REG_ANAGAIN4		(0x17) */
	0x13, /* REG_DIGLINHSLGAIN	(0x18) */
	0x13, /* REG_DIGLINHSRGAIN	(0x19) */
	0x00, /* REG_ADFILTCONF		(0x1A) */
	0x00, /* REG_DIGIFCONF1		(0x1B) */
	0x02, /* REG_DIGIFCONF2		(0x1C) */
	0x00, /* REG_DIGIFCONF3		(0x1D) */
	0x02, /* REG_DIGIFCONF4		(0x1E) */
	0xCC, /* REG_ADSLOTSEL1		(0xCC) */
	0xCC, /* REG_ADSLOTSEL2		(0xCC) */
	0xCC, /* REG_ADSLOTSEL3		(0xCC) */
	0xCC, /* REG_ADSLOTSEL4		(0xCC) */
	0xCC, /* REG_ADSLOTSEL5		(0xCC) */
	0xCC, /* REG_ADSLOTSEL6		(0xCC) */
	0xCC, /* REG_ADSLOTSEL7		(0xCC) */
	0xCC, /* REG_ADSLOTSEL8		(0xCC) */
	0xCC, /* REG_ADSLOTSEL9		(0xCC) */
	0xCC, /* REG_ADSLOTSEL10	(0xCC) */
	0xCC, /* REG_ADSLOTSEL11	(0xCC) */
	0xCC, /* REG_ADSLOTSEL12	(0xCC) */
	0xCC, /* REG_ADSLOTSEL13	(0xCC) */
	0xCC, /* REG_ADSLOTSEL14	(0xCC) */
	0xCC, /* REG_ADSLOTSEL15	(0xCC) */
	0xCC, /* REG_ADSLOTSEL16	(0xCC) */
	0x00, /* REG_ADSLOTHIZCTRL1	(0x2F) */
	0x00, /* REG_ADSLOTHIZCTRL2	(0x30) */
	0x00, /* REG_ADSLOTHIZCTRL3	(0x31) */
	0x00, /* REG_ADSLOTHIZCTRL4	(0x32) */
	0x08, /* REG_DASLOTCONF1	(0x33) */
	0x08, /* REG_DASLOTCONF2	(0x34) */
	0x08, /* REG_DASLOTCONF3	(0x35) */
	0x08, /* REG_DASLOTCONF4	(0x36) */
	0x08, /* REG_DASLOTCONF5	(0x37) */
	0x08, /* REG_DASLOTCONF6	(0x38) */
	0x08, /* REG_DASLOTCONF7	(0x39) */
	0x08, /* REG_DASLOTCONF8	(0x3A) */
	0x00, /* REG_CLASSDCONF1	(0x3B) */
	0x00, /* REG_CLASSDCONF2	(0x3C) */
	0x84, /* REG_CLASSDCONF3	(0x3D) */
	0x00, /* REG_DMICFILTCONF	(0x3E) */
	0xFE, /* REG_DIGMULTCONF1	(0x3F) */
	0xC0, /* REG_DIGMULTCONF2	(0x40) */
	0x3F, /* REG_ADDIGGAIN1		(0x41) */
	0x3F, /* REG_ADDIGGAIN2		(0x42) */
	0x1F, /* REG_ADDIGGAIN3		(0x43) */
	0x1F, /* REG_ADDIGGAIN4		(0x44) */
	0x3F, /* REG_ADDIGGAIN5		(0x45) */
	0x3F, /* REG_ADDIGGAIN6		(0x46) */
	0x1F, /* REG_DADIGGAIN1		(0x47) */
	0x1F, /* REG_DADIGGAIN2		(0x48) */
	0x3F, /* REG_DADIGGAIN3		(0x49) */
	0x3F, /* REG_DADIGGAIN4		(0x4A) */
	0x3F, /* REG_DADIGGAIN5		(0x4B) */
	0x3F, /* REG_DADIGGAIN6		(0x4C) */
	0x3F, /* REG_ADDIGLOOPGAIN1	(0x4D) */
	0x3F, /* REG_ADDIGLOOPGAIN2	(0x4E) */
	0x00, /* REG_HSLEARDIGGAIN	(0x4F) */
	0x00, /* REG_HSRDIGGAIN		(0x50) */
	0x1F, /* REG_SIDFIRGAIN1	(0x51) */
	0x1F, /* REG_SIDFIRGAIN2	(0x52) */
	0x00, /* REG_ANCCONF1		(0x53) */
	0x00, /* REG_ANCCONF2		(0x54) */
	0x00, /* REG_ANCCONF3		(0x55) */
	0x00, /* REG_ANCCONF4		(0x56) */
	0x00, /* REG_ANCCONF5		(0x57) */
	0x00, /* REG_ANCCONF6		(0x58) */
	0x00, /* REG_ANCCONF7		(0x59) */
	0x00, /* REG_ANCCONF8		(0x5A) */
	0x00, /* REG_ANCCONF9		(0x5B) */
	0x00, /* REG_ANCCONF10		(0x5C) */
	0x00, /* REG_ANCCONF11		(0x5D) - read only */
	0x00, /* REG_ANCCONF12		(0x5E) - read only */
	0x00, /* REG_ANCCONF13		(0x5F) - read only */
	0x00, /* REG_ANCCONF14		(0x60) - read only */
	0x00, /* REG_SIDFIRADR		(0x61) */
	0x00, /* REG_SIDFIRCOEF1	(0x62) */
	0x00, /* REG_SIDFIRCOEF2	(0x63) */
	0x00, /* REG_SIDFIRCONF		(0x64) */
	0x00, /* REG_AUDINTMASK1	(0x65) */
	0x00, /* REG_AUDINTSOURCE1	(0x66) - read only */
	0x00, /* REG_AUDINTMASK2	(0x67) */
	0x00, /* REG_AUDINTSOURCE2	(0x68) - read only */
	0x00, /* REG_FIFOCONF1		(0x69) */
	0x00, /* REG_FIFOCONF2		(0x6A) */
	0x00, /* REG_FIFOCONF3		(0x6B) */
	0x00, /* REG_FIFOCONF4		(0x6C) */
	0x00, /* REG_FIFOCONF5		(0x6D) */
	0x00, /* REG_FIFOCONF6		(0x6E) */
	0x02, /* REG_AUDREV		(0x6F) - read only */
	/* AB8505 extended v1 & v2 register cache & default register settings */
	0x00, /* REG_EPWM1CONF		(0x70) */
	0x00, /* REG_EPWM2CONF		(0x71) */
	0x00, /* REG_DMICFREQ		(0x72) */
	0xFF, /* REG_USBHSGAIN		(0x73) */
	0x0C, /* REG_USBDRVCTRL		(0x74) */
	0x00, /* REG_EARGAINMICSEL	(0x75) */
	0x00, /* REG_PDMCTRL		(0x76) */
	0x00, /* REG_CIDEMICTRL		(0x77) */
	0x00, /* REG_HFGAINCTRL_V1	(0x78)  - Depricated in AB8505 v2 */
	0x00, /* REG_VIBGAINCTRL_V1	(0x79)  - Depricated in AB8505 v2 */
	/* AB8505 extended v2 register cache & default register settings */
	0x32, /* REG_EPWM1ACDCA		(0x7A) */
	0x32, /* REG_EPWM1ACDCB		(0x7B) */
	0x00, /* REG_EPWM1ACFREQ	(0x7C) */
	0x32, /* REG_EPWM2ACDCA		(0x7D) */
	0x32, /* REG_EPWM2ACDCB		(0x7E) */
	0x00, /* REG_EPWM2ACFREQ	(0x7F) */
	0x18, /* REG_HFLGAINCTRL_V2	(0x80) */
	0x18, /* REG_HFRGAINCTRL_V2	(0x81) */
	0x18, /* REG_VIBGAINCTRL_V2	(0x82) */
	0x00, /* REG_MIXCTRL		(0x83) */
};

static struct snd_soc_codec *ab850x_codec;

/* ADCM */
static const u8 ADCM_ANACONF5_MASK = BMASK(REG_ANACONF5_ENCPHS);
static const u8 ADCM_MUTECONF_MASK = BMASK(REG_MUTECONF_MUTHSL) |
		BMASK(REG_MUTECONF_MUTHSR);
static const u8 ADCM_ANACONF4_MASK = BMASK(REG_ANACONF4_ENHSL) |
		BMASK(REG_ANACONF4_ENHSR);
static unsigned int adcm_anaconf5, adcm_muteconf, adcm_anaconf4;
static int adcm = AB850X_AUDIO_ADCM_NORMAL;

/* Signed multi register array controls. */
struct soc_smra_control {
	unsigned int *reg;
	const unsigned int rcount, count, invert;
	long min, max;
	const char **texts;
	long *values;
};

/* Sidetone FIR-coeff cache */
static long sid_fir_cache[REG_SID_FIR_COEFFS];

/* ANC FIR- & IIR-coeff caches */
static long anc_fir_cache[REG_ANC_FIR_COEFFS];
static long anc_iir_cache[REG_ANC_IIR_COEFFS];

/* Private data for AB8500 device-driver */
struct ab8500_codec_drvdata {
	enum ab850x_audio_chipid chipid;
};

/* Reads an arbitrary register from the ab8500 chip.
*/
static int ab850x_codec_read_reg(struct snd_soc_codec *codec,
		unsigned int bank, unsigned int reg)
{
	u8 value;
	int status = abx500_get_register_interruptible(
		codec->dev, bank, reg, &value);

	if (status < 0) {
		pr_err("%s: Register (%02x:%02x) read failed (%d).\n",
			__func__, (u8)bank, (u8)reg, status);
	} else {
		pr_debug("Read 0x%02x from register %02x:%02x\n",
			(u8)value, (u8)bank, (u8)reg);
		status = value;
	}

	return status;
}

/* Writes an arbitrary register to the ab8500 chip.
 */
static int ab850x_codec_write_reg(struct snd_soc_codec *codec,
		unsigned int bank, unsigned int reg, unsigned int value)
{
	int status = abx500_set_register_interruptible(
		codec->dev, bank, reg, value);

	if (status < 0) {
		pr_err("%s: Register (%02x:%02x) write failed (%d).\n",
			__func__, (u8)bank, (u8)reg, status);
	} else {
		pr_debug("Wrote 0x%02x into register %02x:%02x\n",
			(u8)value, (u8)bank, (u8)reg);
	}

	return status;
}

/* Reads an audio register from the cache or hardware.
 */
static unsigned int ab850x_codec_read_reg_audio(struct snd_soc_codec *codec,
		unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	if (reg >= codec->reg_size) {
		pr_err("%s: Register (%02x:%02x) read failed.\n", __func__,
				AB8500_AUDIO, (u8)reg);
		return -EIO;
	}

	if (reg == REG_SIDFIRCONF)
		return ab850x_codec_read_reg(codec, AB8500_AUDIO, reg);

	return cache[reg];
}

/* Writes an audio register to the hardware and cache.
 */
static int ab850x_codec_write_reg_audio(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	int status;
	u8 *cache = codec->reg_cache;

	if (reg >= codec->reg_size) {
		pr_err("%s: Register (%02x:%02x) write failed.\n", __func__,
				AB8500_AUDIO, (u8)reg);
		return -EIO;
	}

	status = ab850x_codec_write_reg(codec, AB8500_AUDIO, reg, value);
	if (status >= 0)
		cache[reg] = value;

	return status;
}

/* Generic soc info for signed register controls. */
int snd_soc_info_s(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct soc_smra_control *smra =
		(struct soc_smra_control *)kcontrol->private_value;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = smra->count;
	uinfo->value.integer.min = smra->min;
	uinfo->value.integer.max = smra->max;

	return 0;
}

/* Generic soc get for signed multi register controls. */
int snd_soc_get_smr(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_smra_control *smra =
		(struct soc_smra_control *)kcontrol->private_value;
	unsigned int *reg = smra->reg;
	unsigned int rcount = smra->rcount;
	long min = smra->min;
	long max = smra->max;
	unsigned int invert = smra->invert;
	unsigned long mask = abs(min) | abs(max);
	long value = 0;
	int i, rvalue;

	for (i = 0; i < rcount; i++) {
		rvalue = snd_soc_read(codec, reg[i]) & REG_MASK_ALL;
		value |= rvalue << (8 * (rcount - i - 1));
	}
	value &= mask;
	if (min < 0 && value > max)
		value |= ~mask;
	if (invert)
		value = ~value;
	ucontrol->value.integer.value[0] = value;

	return 0;
}

/* Generic soc put for signed multi register controls. */
int snd_soc_put_smr(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_smra_control *smra =
		(struct soc_smra_control *)kcontrol->private_value;
	unsigned int *reg = smra->reg;
	unsigned int rcount = smra->rcount;
	long min = smra->min;
	long max = smra->max;
	unsigned int invert = smra->invert;
	unsigned long mask = abs(min) | abs(max);
	long value = ucontrol->value.integer.value[0];
	int i, rvalue, err;

	if (invert)
		value = ~value;
	if (value > max)
		value = max;
	else if (value < min)
		value = min;
	value &= mask;
	for (i = 0; i < rcount; i++) {
		rvalue = (value >> (8 * (rcount - i - 1))) & REG_MASK_ALL;
		err = snd_soc_write(codec, reg[i], rvalue);
		if (err < 0)
			return 0;
	}

	return 1;
}

/* Generic soc get for signed array controls. */
static int snd_soc_get_sa(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_smra_control *smra =
		(struct soc_smra_control *)kcontrol->private_value;
	long *values = smra->values;
	unsigned int count = smra->count;
	unsigned int idx;

	for (idx = 0; idx < count; idx++)
		ucontrol->value.integer.value[idx] = values[idx];

	return 0;
}

/* Generic soc put for signed array controls. */
static int snd_soc_put_sa(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_smra_control *smra =
			(struct soc_smra_control *) kcontrol->private_value;
	long *values = smra->values;
	unsigned int count = smra->count;
	long min = smra->min;
	long max = smra->max;
	unsigned int idx;
	long value;

	for (idx = 0; idx < count; idx++) {
		value = ucontrol->value.integer.value[idx];
		if (value > max)
			value = max;
		else if (value < min)
			value = min;
		values[idx] = value;
	}

	return 0;
}

/* Generic soc get for enum strobe controls. */
int snd_soc_get_enum_strobe(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg = e->reg;
	unsigned int bit = e->shift_l;
	unsigned int invert = e->shift_r != 0;
	unsigned int value = snd_soc_read(codec, reg) & BMASK(bit);

	if (bit != 0 && value != 0)
		value = value >> bit;
	ucontrol->value.enumerated.item[0] = value ^ invert;

	return 0;
}

/* Generic soc put for enum strobe controls. */
int snd_soc_put_enum_strobe(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg = e->reg;
	unsigned int bit = e->shift_l;
	unsigned int invert = e->shift_r != 0;
	unsigned int strobe = ucontrol->value.enumerated.item[0] != 0;
	unsigned int clr_mask = (strobe ^ invert) ? REG_MASK_NONE : BMASK(bit);
	unsigned int set_mask = (strobe ^ invert) ? BMASK(bit) : REG_MASK_NONE;

	if (snd_soc_update_bits(codec, reg, clr_mask, set_mask) == 0)
		return 0;
	return snd_soc_update_bits(codec, reg, set_mask, clr_mask);
}

static const char * const enum_ena_dis[] = {"Enabled", "Disabled"};
static const char * const enum_dis_ena[] = {"Disabled", "Enabled"};
static const char * const enum_rdy_apl[] = {"Ready", "Apply"};

/* Sidetone FIR-coefficients configuration sequence */
static int sid_apply_control_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec;
	unsigned int param, sidconf;
	int ret = 0;

	pr_debug("%s: Enter\n", __func__);

	if (ucontrol->value.integer.value[0] != 1) {
		/*pr_err("%s: ERROR: This control supports 'Apply' only!\n",
			__func__);*/
		return ret;
	}

	codec = snd_kcontrol_chip(kcontrol);

	mutex_lock(&codec->mutex);

	sidconf = snd_soc_read(codec, REG_SIDFIRCONF);
	if (((sidconf & BMASK(REG_SIDFIRCONF_FIRSIDBUSY)) != 0)) {
		if ((sidconf & BMASK(REG_SIDFIRCONF_ENFIRSIDS)) == 0) {
			pr_err("%s: Sidetone busy while off. Resetting...\n",
				__func__);
			snd_soc_update_bits(codec, REG_SIDFIRADR,
				REG_MASK_NONE, BMASK(REG_SIDFIRADR_FIRSIDSET));
			snd_soc_update_bits(codec, REG_SIDFIRADR,
				BMASK(REG_SIDFIRADR_FIRSIDSET), REG_MASK_NONE);
		}
		ret = -EBUSY;
		goto out;
	}

	snd_soc_write(codec, REG_SIDFIRADR, REG_MASK_NONE);

	for (param = 0; param < REG_SID_FIR_COEFFS; param++) {
		snd_soc_write(codec, REG_SIDFIRCOEF1,
			sid_fir_cache[param] >> 8 & REG_MASK_ALL);
		snd_soc_write(codec, REG_SIDFIRCOEF2,
			sid_fir_cache[param] & REG_MASK_ALL);
	}

	snd_soc_update_bits(codec, REG_SIDFIRADR,
		REG_MASK_NONE, BMASK(REG_SIDFIRADR_FIRSIDSET));
	snd_soc_update_bits(codec, REG_SIDFIRADR,
		BMASK(REG_SIDFIRADR_FIRSIDSET), REG_MASK_NONE);

	ret = 1;
out:
	mutex_unlock(&codec->mutex);

	pr_debug("%s: Exit\n", __func__);

	return ret;
}

static int digital_mute_control_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
  int reg_daslot1 = ab850x_codec_read_reg_audio(ab850x_codec, REG_DASLOTCONF1)
                    & REG_DASLOTCONFX_SLTODAX_MASK;
  int reg_daslot2 = ab850x_codec_read_reg_audio(ab850x_codec, REG_DASLOTCONF2)
                    & REG_DASLOTCONFX_SLTODAX_MASK;
  int reg_daslot3 = ab850x_codec_read_reg_audio(ab850x_codec, REG_DASLOTCONF3)
                    & REG_DASLOTCONFX_SLTODAX_MASK;
  int reg_daslot4 = ab850x_codec_read_reg_audio(ab850x_codec, REG_DASLOTCONF4)
                    & REG_DASLOTCONFX_SLTODAX_MASK;
  int reg_dapath = ab850x_codec_read_reg_audio(ab850x_codec, REG_DAPATHENA);

  ucontrol->value.enumerated.item[0] = (((reg_daslot1 & reg_daslot2 & reg_daslot3 &
            reg_daslot4) == REG_DASLOTCONFX_SLTODAX_MASK) &
            ((reg_dapath & (BMASK(REG_DAPATHENA_ENDA1) |
            BMASK(REG_DAPATHENA_ENDA2) |
            BMASK(REG_DAPATHENA_ENDA3) |
            BMASK(REG_DAPATHENA_ENDA4))) == 0));

	return 0;
}

static int hs_disable_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		mdelay(10);
		break;
	}
	return 0;
}

static int if1_enable = 0;
static int digital_mute_control_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	unsigned int mask = 0;
	unsigned int value = 0;

  if(if1_enable == 0) {
    if (ucontrol->value.enumerated.item[0] != 0) {
      value = 	BMASK(REG_DAPATHENA_ENDA1) |
			          BMASK(REG_DAPATHENA_ENDA2) |
			          BMASK(REG_DAPATHENA_ENDA3) |
			          BMASK(REG_DAPATHENA_ENDA4);
		} else {
			mask = BMASK(REG_DAPATHENA_ENDA1) |
				     BMASK(REG_DAPATHENA_ENDA2) |
				     BMASK(REG_DAPATHENA_ENDA3) |
				     BMASK(REG_DAPATHENA_ENDA4);
		}
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DAPATHENA,
			    mask, value);
	}

	if (ucontrol->value.enumerated.item[0] != 0) {
		mask = REG_DASLOTCONFX_SLTODAX_MASK;
		value = 0x08;
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF1,
			                    						mask, value);
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF2,
			                  							mask, value+1);
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF3,
			                                mask, value+2);
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF4,
			                    						mask, value+3);
	} else {
		mask = REG_DASLOTCONFX_SLTODAX_MASK;
		value = 0x1f;
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF1,
			                      					mask, value);
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF2,
			                        				mask, value);
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF3,
			                      					mask, value);
		ret = snd_soc_update_bits_locked(ab850x_codec, REG_DASLOTCONF4,
			                      					mask, value);
	}

	if (ret < 0) {
		pr_err("%s: ERROR: Failed to change digital mute (%d)!\n",
		__func__, ucontrol->value.enumerated.item[0]);
		return 0;
	}

	pr_debug("%s: Digital mute set to %d\n",
		__func__, ucontrol->value.enumerated.item[0]);

	return 1;
}

static int if0_fifo_enable_control_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int reg = ab850x_codec_read_reg_audio(ab850x_codec, REG_DIGIFCONF3);
	ucontrol->value.integer.value[0] =
		reg & BMASK(REG_DIGIFCONF3_IF0BFIFOEN);

	return 0;
}

static int if0_fifo_enable_control_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	unsigned int set_mask, clear_mask;

	if (ucontrol->value.integer.value[0] != 0) {
		clear_mask = 0;
		set_mask = BMASK(REG_DIGIFCONF3_IF0BFIFOEN);

		pr_debug("%s: IF0 FIFO disable: override APE OPP\n", __func__);
		ret = prcmu_qos_lpa_override(true);
	} else {
		clear_mask = BMASK(REG_DIGIFCONF3_IF0BFIFOEN);
		set_mask = 0;

		pr_debug("%s: IF0 FIFO disable: restore APE OPP\n", __func__);
		ret = prcmu_qos_lpa_override(false);
	}
	if (ret < 0) {
		pr_err("%s: ERROR: Failed to modify APE OPP (%ld)!\n",
			__func__, ucontrol->value.integer.value[0]);
		return 0;
	}

	ret = snd_soc_update_bits(ab850x_codec,
				REG_DIGIFCONF3,
				clear_mask,
				set_mask);
	if (ret < 0) {
		pr_err("%s: ERROR: Failed to change burst-mode (%ld)!\n",
			__func__, ucontrol->value.integer.value[0]);
		return 0;
	}

	return 1;
}

static int chipid_control_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] =
			ab850x_audio_get_chipid(codec->dev);

	return 0;
}

static const char * const enum_chipid[] = {
        "Unknown",
        "AB8500",
        "AB9540_V1", "AB9540_V2", "AB9540_V3",
        "AB8505_V1", "AB8505_V2", "AB8505_V3"
};

static SOC_ENUM_SINGLE_EXT_DECL(soc_enum_chipid, enum_chipid);

static struct snd_kcontrol_new chipid_control = \
	SOC_ENUM_EXT("ChipId", soc_enum_chipid, chipid_control_get, NULL);


/* Controls - DAPM */

/* Inverted order - Ascending/Descending */
enum control_inversion {
	NORMAL = 0,
	INVERT = 1
};

/* Headset */

/* Headset Left - Enable/Disable */
static const struct soc_enum enum_headset_left = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_headset_left_mux =
				SOC_DAPM_ENUM_VIRT("Headset Left", enum_headset_left);

/* Headset Right - Enable/Disable */
static const struct soc_enum enum_headset_right = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_headset_right_mux =
				SOC_DAPM_ENUM_VIRT("Headset Right", enum_headset_right);

/* USB Headset Left - Enable/Disable */
static const struct soc_enum enum_usbheadset_left = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_usbheadset_left_mux =
		SOC_DAPM_ENUM_VIRT("USB Headset Left", enum_usbheadset_left);

/* USB Headset Right - Enable/Disable */
static const struct soc_enum enum_usbheadset_right = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_usbheadset_right_mux =
		SOC_DAPM_ENUM_VIRT("USB Headset Right", enum_usbheadset_right);

/* Carkit Left - Enable/Disable */
static const struct soc_enum enum_ck_left = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_ck_left_mux =
		SOC_DAPM_ENUM_VIRT("Carkit Left", enum_ck_left);

/* Carkit Right - Enable/Disable */
static const struct soc_enum enum_ck_right = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_ck_right_mux =
		SOC_DAPM_ENUM_VIRT("Carkit Right", enum_ck_right);

/* DA7 to DA1 - Enable/Disable */
static const struct soc_enum enum_da7toda1 = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_da7toda1_mux =
		SOC_DAPM_ENUM_VIRT("DA7 to DA1", enum_da7toda1);

/* DA7 to DA3 - Enable/Disable */
static const struct soc_enum enum_da7toda3 = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_da7toda3_mux =
		SOC_DAPM_ENUM_VIRT("DA7 to DA3", enum_da7toda3);

/* DA8 to DA2 - Enable/Disable */
static const struct soc_enum enum_da8toda2 = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_da8toda2_mux =
		SOC_DAPM_ENUM_VIRT("DA8 to DA2", enum_da8toda2);

/* DA8 to DA4 - Enable/Disable */
static const struct soc_enum enum_da8toda4 = SOC_ENUM_SINGLE(0, 0, 2,
		enum_dis_ena);
static const struct snd_kcontrol_new dapm_da8toda4_mux =
		SOC_DAPM_ENUM_VIRT("DA8 to DA4", enum_da8toda4);

/* Earpiece */

/* Earpiece - Mute */
static const struct soc_enum enum_ear = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ear_mux =
				SOC_DAPM_ENUM_VIRT("Earpiece", enum_ear);

/* Earpiece source selector */
static const char * const enum_ear_lineout_source[] = {"Headset Left", "IHF Left"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ear_lineout_source, REG_DMICFILTCONF,
			REG_DMICFILTCONF_DA3TOEAR, enum_ear_lineout_source);
static const struct snd_kcontrol_new dapm_ear_lineout_source[] = {
	SOC_DAPM_ENUM("Earpiece or LineOut Mono Source", dapm_enum_ear_lineout_source),
};

/* LineOut */

/* LineOut source selector */
static const char * const enum_lineout_source[] = {"Mono Path", "Stereo Path"};
static SOC_ENUM_DOUBLE_DECL(dapm_enum_lineout_source, REG_ANACONF5,
			REG_ANACONF5_HSLDACTOLOL, REG_ANACONF5_HSRDACTOLOR, enum_lineout_source);
static const struct snd_kcontrol_new dapm_lineout_source[] = {
	SOC_DAPM_ENUM("LineOut Source", dapm_enum_lineout_source),
};

/* LineOut */

/* LineOut Left - Enable/Disable */
static const struct soc_enum enum_lineout_left = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_lineout_left_mux =
				SOC_DAPM_ENUM_VIRT("LineOut Left", enum_lineout_left);

/* LineOut Right - Enable/Disable */
static const struct soc_enum enum_lineout_right = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_lineout_right_mux =
				SOC_DAPM_ENUM_VIRT("LineOut Right", enum_lineout_right);

/* LineOut/IHF - Select */
static const char * const enum_ihf_or_lineout_select_sel[] = {"IHF", "LineOut"};
static const struct soc_enum enum_ihf_or_lineout_select = SOC_ENUM_SINGLE(0, 0, 2, enum_ihf_or_lineout_select_sel);
static const struct snd_kcontrol_new dapm_ihf_or_lineout_select_mux =
				SOC_DAPM_ENUM_VIRT("IHF or LineOut Select", enum_ihf_or_lineout_select);


/* IHF */

/* IHF - Enable/Disable */
static const struct soc_enum enum_ihf_left = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ihf_left_mux =
				SOC_DAPM_ENUM_VIRT("IHF Left", enum_ihf_left);

static const struct soc_enum enum_ihf_right = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ihf_right_mux =
				SOC_DAPM_ENUM_VIRT("IHF Right", enum_ihf_right);

/* IHF left - ANC selector */
static const char * const enum_ihfx_sel[] = {"Audio Path", "ANC"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ihfl_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_HFLSEL, enum_ihfx_sel);
static const struct snd_kcontrol_new dapm_ihfl_select[] = {
	SOC_DAPM_ENUM("IHF Left Source", dapm_enum_ihfl_sel),
};

/* IHF right - ANC selector */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ihfr_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_HFRSEL, enum_ihfx_sel);
static const struct snd_kcontrol_new dapm_ihfr_select[] = {
	SOC_DAPM_ENUM("IHF Right Source", dapm_enum_ihfr_sel),
};

/* DA1/DA4 to HfR selector control */
static const char * const enum_da_hfr_sel[] = {"DA4", "DA1"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_da_hfr_sel, REG_PDMCTRL,
		REG_PDMCTRL_D1TOHFR, enum_da_hfr_sel);
static const struct snd_kcontrol_new dapm_da_hfr_select[] = {
	SOC_DAPM_ENUM("IHF Right Select", dapm_enum_da_hfr_sel),
};

/* DA3/DA5 to HFl selector control (ab8505 v1) */
static const char * const enum_da_hfl_sel_v1[] = {"DA3", "DA5"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_da_hfl_sel_v1, REG_PDMCTRL,
		REG_PDMCTRL_D5TOHFL, enum_da_hfl_sel_v1);
static const struct snd_kcontrol_new dapm_da_hfl_select_v1[] = {
	SOC_DAPM_ENUM("IHF Left Select", dapm_enum_da_hfl_sel_v1),
};

/* DA3/DA5 to HFl selector control (ab8505 v2) */
static const char * const enum_da_hfl_sel_v2[] = {"DA3", "DA6"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_da_hfl_sel_v2, REG_PDMCTRL,
		REG_PDMCTRL_D6TOHFL, enum_da_hfl_sel_v2);
static const struct snd_kcontrol_new dapm_da_hfl_select_v2[] = {
	SOC_DAPM_ENUM("IHF Left Select", dapm_enum_da_hfl_sel_v2),
};

/* HFR/EPWM1 to HFR driver route */
static const char * const enum_hfr2_sel[] = {"IHFR", "EPWM1"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_hfr2_sel, REG_EPWM1CONF,
		REG_EPWM1CONF_TOHFR, enum_hfr2_sel);
static const struct snd_kcontrol_new dapm_hfr2_select[] = {
	SOC_DAPM_ENUM("IHF Right Select 2", dapm_enum_hfr2_sel),
};

/* HFL/EPWM2 to HFL driver route */
static const char * const enum_hfl2_sel[] = {"IHFL", "EPWM2"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_hfl2_sel, REG_EPWM2CONF,
		REG_EPWM2CONF_TOHFL, enum_hfl2_sel);
static const struct snd_kcontrol_new dapm_hfl2_select[] = {
	SOC_DAPM_ENUM("IHF Left Select 2", dapm_enum_hfl2_sel),
};

/* AD 1 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad1loop_enable, REG_DASLOTCONF1,
			REG_DASLOTCONF1_DAI7TOADO1, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad1loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 1 Loopback Switch", dapm_enum_ad1loop_enable),
};

/* AD 2 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad2loop_enable, REG_DASLOTCONF2,
			REG_DASLOTCONF2_DAI8TOADO2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad2loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 2 Loopback Switch", dapm_enum_ad2loop_enable),
};

/* AD 3 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad3loop_enable, REG_DASLOTCONF3,
			REG_DASLOTCONF3_DAI7TOADO3, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad3loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 3 Loopback Switch", dapm_enum_ad3loop_enable),
};

/* AD 4 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad4loop_enable, REG_DASLOTCONF4,
			REG_DASLOTCONF4_DAI8TOADO4, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad4loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 4 Loopback Switch", dapm_enum_ad4loop_enable),
};

/* AD 5 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad5loop_enable, REG_DASLOTCONF5,
			REG_DASLOTCONF5_DAI7TOADO5, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad5loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 5 Loopback Switch", dapm_enum_ad5loop_enable),
};

/* AD 6 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad6loop_enable, REG_DASLOTCONF6,
			REG_DASLOTCONF6_DAI8TOADO6, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad6loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 6 Loopback Switch", dapm_enum_ad6loop_enable),
};

/* AD 7 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad7loop_enable, REG_DASLOTCONF7,
			REG_DASLOTCONF7_DAI8TOADO7, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad7loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 7 Loopback Switch", dapm_enum_ad7loop_enable),
};

/* AD 8 Loopback Switch - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad8loop_enable, REG_DASLOTCONF8,
			REG_DASLOTCONF8_DAI7TOADO8, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad8loop_mux[] = {
	SOC_DAPM_ENUM("Digital Interface AD 8 Loopback Switch", dapm_enum_ad8loop_enable),
};

/* Mic 1 */

/* Mic 1 - Mute */
static const struct soc_enum enum_mic1 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_mic1_mux =
				SOC_DAPM_ENUM_VIRT("Mic 1", enum_mic1);

/* Mic 1 - Mic 1A or 1B selector */
static const char * const enum_mic1ab_sel[] = {"Mic 1A", "Mic 1B"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_mic1ab_sel, REG_ANACONF3,
			REG_ANACONF3_MIC1SEL, enum_mic1ab_sel);
static const struct snd_kcontrol_new dapm_mic1ab_select[] = {
	SOC_DAPM_ENUM("Mic 1A or 1B Select", dapm_enum_mic1ab_sel),
};

/* Mic 1 - AD3 - Mic 1 or DMic 3 selector */
static const char * const enum_ad3_sel[] = {"Mic 1", "DMic 3"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad3_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD3SEL, enum_ad3_sel);
static const struct snd_kcontrol_new dapm_ad3_select[] = {
	SOC_DAPM_ENUM("AD 3 Select", dapm_enum_ad3_sel),
};

/* Mic 1 - AD6 - Mic 1 or DMic 6 selector */
static const char * const enum_ad6_sel[] = {"Mic 1", "DMic 6"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad6_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD6SEL, enum_ad6_sel);
static const struct snd_kcontrol_new dapm_ad6_select[] = {
	SOC_DAPM_ENUM("AD 6 Select", dapm_enum_ad6_sel),
};

/* Mic 2 */

/* Mic 2 - Mute */
static const struct soc_enum enum_mic2 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_mic2_mux =
				SOC_DAPM_ENUM_VIRT("Mic 2", enum_mic2);

/* Mic 2 - AD5 - Mic 2 or DMic 5 selector */
static const char * const enum_ad5_sel[] = {"Mic 2", "DMic 5"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad5_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD5SEL, enum_ad5_sel);
static const struct snd_kcontrol_new dapm_ad5_select[] = {
	SOC_DAPM_ENUM("AD 5 Select", dapm_enum_ad5_sel),
};

/* LineIn */

/* LineIn left - Mute */
static const struct soc_enum enum_linl = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_linl_mux =
				SOC_DAPM_ENUM_VIRT("LineIn Left", enum_linl);

static const struct snd_kcontrol_new dapm_linl_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_ANACONF2, REG_ANACONF2_MUTLINL, 1, INVERT),
};

/* LineIn left - AD1 - LineIn Left or DMic 1 selector */
static const char * const enum_ad1_sel[] = {"LineIn Left", "DMic 1"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad1_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD1SEL, enum_ad1_sel);
static const struct snd_kcontrol_new dapm_ad1_select[] = {
	SOC_DAPM_ENUM("AD 1 Select", dapm_enum_ad1_sel),
};

/* LineIn right - Mute */
static const struct soc_enum enum_linr = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_linr_mux =
				SOC_DAPM_ENUM_VIRT("LineIn Right", enum_linr);

static const struct snd_kcontrol_new dapm_linr_mute[] = {
	SOC_DAPM_SINGLE("Capture Switch", REG_ANACONF2, REG_ANACONF2_MUTLINR, 1, INVERT),
};

/* LineIn right - Mic 2 or LineIn Right selector */
static const char * const enum_mic2lr_sel[] = {"Mic 2", "LineIn Right"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_mic2lr_sel, REG_ANACONF3,
			REG_ANACONF3_LINRSEL, enum_mic2lr_sel);
static const struct snd_kcontrol_new dapm_mic2lr_select[] = {
	SOC_DAPM_ENUM("Mic 2 or LINR Select", dapm_enum_mic2lr_sel),
};

/* LineIn right - AD2 - LineIn Right or DMic2 selector */
static const char * const enum_ad2_sel[] = {"LineIn Right", "DMic 2"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_ad2_sel, REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_AD2SEL, enum_ad2_sel);
static const struct snd_kcontrol_new dapm_ad2_select[] = {
	SOC_DAPM_ENUM("AD 2 Select", dapm_enum_ad2_sel),
};

/* AD1 to DA1 (IHF Left) Switch */
static const struct soc_enum enum_ad1_to_ihf_left = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad1_to_ihf_left_mux =
	SOC_DAPM_ENUM_VIRT("AD1 to IHF Left", enum_ad1_to_ihf_left);

/* AD2 to DA1 (IHF Left) Switch */
static const struct soc_enum enum_ad2_to_ihf_right = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_ad2_to_ihf_right_mux =
	SOC_DAPM_ENUM_VIRT("AD2 to IHF Right", enum_ad2_to_ihf_right);

/* LineIn Left to Headset Left switch */
static const struct soc_enum enum_linl_to_hs_left = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_linl_to_hs_left_mux =
	SOC_DAPM_ENUM_VIRT("LineIn Left to Headset Left", enum_linl_to_hs_left);

/* LineIn Right to Headset Right switch */
static const struct soc_enum enum_linr_to_hs_right = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_linr_to_hs_right_mux =
	SOC_DAPM_ENUM_VIRT("LineIn Right to Headset Right", enum_linr_to_hs_right);


/* DMic */

/* DMic 1 - Mute */
static const struct soc_enum enum_dmic1 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_dmic1_mux =
				SOC_DAPM_ENUM_VIRT("DMic 1", enum_dmic1);

/* DMic 2 - Mute */
static const struct soc_enum enum_dmic2 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_dmic2_mux =
				SOC_DAPM_ENUM_VIRT("DMic 2", enum_dmic2);

/* DMic 3 - Mute */
static const struct soc_enum enum_dmic3 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_dmic3_mux =
				SOC_DAPM_ENUM_VIRT("DMic 3", enum_dmic3);

/* DMic 4 - Mute */
static const struct soc_enum enum_dmic4 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_dmic4_mux =
				SOC_DAPM_ENUM_VIRT("DMic 4", enum_dmic4);

/* DMic 5 - Mute */
static const struct soc_enum enum_dmic5 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_dmic5_mux =
				SOC_DAPM_ENUM_VIRT("DMic 5", enum_dmic5);

/* DMic 6 - Mute */
static const struct soc_enum enum_dmic6 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_dmic6_mux =
				SOC_DAPM_ENUM_VIRT("DMic 6", enum_dmic6);

/* USB Mic */
static const char * const enum_usbmic_sel[] = {"None",
		"USBSWCAP", "USB DP", "USB ID"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_usbmic_sel, REG_EARGAINMICSEL,
		REG_EARGAINMICSEL_USBMICSEL_SHIFT, enum_usbmic_sel);
static const struct snd_kcontrol_new dapm_usbmic_select[] = {
	SOC_DAPM_ENUM("USB Mic Source", dapm_enum_usbmic_sel),
};

/* ANC */

static const char * const enum_anc_in_sel[] = {"Mic 1 / DMic 6", "Mic 2 / DMic 5"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_anc_in_sel, REG_DMICFILTCONF,
			REG_DMICFILTCONF_ANCINSEL, enum_anc_in_sel);
static const struct snd_kcontrol_new dapm_anc_in_select[] = {
	SOC_DAPM_ENUM("ANC Source", dapm_enum_anc_in_sel),
};

/* ANC - Enable/Disable */
static SOC_ENUM_SINGLE_DECL(dapm_enum_anc_enable, REG_ANCCONF1,
			REG_ANCCONF1_ENANC, enum_dis_ena);
static const struct snd_kcontrol_new dapm_anc_enable[] = {
	SOC_DAPM_ENUM("ANC", dapm_enum_anc_enable),
};

/* ANC to Earpiece - Mute */
static const struct snd_kcontrol_new dapm_anc_ear_mute[] = {
	SOC_DAPM_SINGLE("Playback Switch", REG_DIGMULTCONF1,
			REG_DIGMULTCONF1_ANCSEL, 1, NORMAL),
};

/* Sidetone left */

/* Sidetone left - Input selector */
static const char * const enum_stfir1_in_sel[] = {
	"LineIn Left", "LineIn Right", "Mic 1", "Headset Left"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_stfir1_in_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_FIRSID1SEL, enum_stfir1_in_sel);
static const struct snd_kcontrol_new dapm_stfir1_in_select[] = {
	SOC_DAPM_ENUM("Sidetone Left Source", dapm_enum_stfir1_in_sel),
};

/* Sidetone left - Enable/Disable */
static const struct soc_enum enum_stfir1_ena = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_stfir1_ena =
				SOC_DAPM_ENUM_VIRT("Sidetone Left", enum_stfir1_ena);

/* Sidetone right path */

/* Sidetone right - Input selector */
static const char * const enum_stfir2_in_sel[] = {
	"LineIn Right", "Mic 1", "DMic 4", "Headset Right"};
static SOC_ENUM_SINGLE_DECL(dapm_enum_stfir2_in_sel, REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_FIRSID2SEL, enum_stfir2_in_sel);
static const struct snd_kcontrol_new dapm_stfir2_in_select[] = {
	SOC_DAPM_ENUM("Sidetone Right Source", dapm_enum_stfir2_in_sel),
};

/* Sidetone right - Enable/Disable */
static const struct soc_enum enum_stfir2_ena = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_stfir2_ena =
			SOC_DAPM_ENUM_VIRT("Sidetone Right", enum_stfir2_ena);

/* Vibra */

/* Vibra 1 - Enable/Disable */
static const struct soc_enum enum_vibra1 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_vibra1_mux =
				SOC_DAPM_ENUM_VIRT("Vibra 1", enum_vibra1);

/* Vibra 2 - Enable/Disable */
static const struct soc_enum enum_vibra2 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_vibra2_mux =
				SOC_DAPM_ENUM_VIRT("Vibra 2", enum_vibra2);

static const char * const enum_pwm2vibx[] = {"Audio Path", "PWM Generator"};

static SOC_ENUM_SINGLE_DECL(dapm_enum_pwm2vib1, REG_PWMGENCONF1,
			REG_PWMGENCONF1_PWMTOVIB1, enum_pwm2vibx);

static const struct snd_kcontrol_new dapm_pwm2vib1[] = {
	SOC_DAPM_ENUM("Vibra 1 Controller", dapm_enum_pwm2vib1),
};

static SOC_ENUM_SINGLE_DECL(dapm_enum_pwm2vib2, REG_PWMGENCONF1,
			REG_PWMGENCONF1_PWMTOVIB2, enum_pwm2vibx);

static const struct snd_kcontrol_new dapm_pwm2vib2[] = {
	SOC_DAPM_ENUM("Vibra 2 Controller", dapm_enum_pwm2vib2),
};

/* EPWM */
/* EPWM 1 - Enable/Disable */
static const struct soc_enum enum_epwm1 = SOC_ENUM_SINGLE(0, 0, 2,
						enum_dis_ena);
static const struct snd_kcontrol_new dapm_epwm1_mux =
				SOC_DAPM_ENUM_VIRT("EPWM 1", enum_epwm1);
/* EPWM 2 - Enable/Disable */
static const struct soc_enum enum_epwm2 = SOC_ENUM_SINGLE(0, 0, 2,
						enum_dis_ena);
static const struct snd_kcontrol_new dapm_epwm2_mux =
				SOC_DAPM_ENUM_VIRT("EPWM 2", enum_epwm2);

/* PDM */
/* PDM 1 - Enable/Disable */
static const struct soc_enum enum_pdm1 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_pdm1_mux =
				SOC_DAPM_ENUM_VIRT("PDM 1", enum_pdm1);
/* PDM 2 - Enable/Disable */
static const struct soc_enum enum_pdm2 = SOC_ENUM_SINGLE(0, 0, 2, enum_dis_ena);
static const struct snd_kcontrol_new dapm_pdm2_mux =
				SOC_DAPM_ENUM_VIRT("PDM 2", enum_pdm2);

/* Event-handlers - DAPM */

static int stfir_enable;
static int stfir_enable_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (!stfir_enable)
			snd_soc_update_bits(codec, REG_SIDFIRCONF,
					0, BMASK(REG_SIDFIRCONF_ENFIRSIDS));
		stfir_enable++;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		stfir_enable--;
		if (!stfir_enable)
			snd_soc_update_bits(codec, REG_SIDFIRCONF,
					BMASK(REG_SIDFIRCONF_ENFIRSIDS), 0);
		break;
	}
	return 0;
}

static int linein_enable_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
	case SND_SOC_DAPM_POST_PMD:
		mdelay(LINEIN_RAMP_DELAY);
		break;
	}
	return 0;
}

static int if1_enable_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	unsigned int fmt_if1;
	struct snd_soc_codec *codec = w->codec;
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (!if1_enable) {
			pr_info("%s: Enable IF1 \n", __func__);
			fmt_if1 = SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_I2S;
			ret = ab850x_audio_setup_if1(codec, fmt_if1, 16, 1);
			if (ret < 0)
				return ret;
		}
		if1_enable++;
		break;
	case SND_SOC_DAPM_POST_PMD:
		if1_enable--;
		if (if1_enable == 0)
			snd_soc_update_bits(codec, REG_DIGIFCONF4, REG_MASK_ALL, 0);
	}

	return ret;
}

static const struct snd_soc_dapm_widget ab850x_dapm_widgets[] = {

	/* DA/AD */

	SND_SOC_DAPM_INPUT("ADC Input"),
	SND_SOC_DAPM_ADC("ADC", "ab8500_0c", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC", "ab8500_0p", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("DAC Output"),

	SND_SOC_DAPM_AIF_IN("DA_IN1", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN2", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN3", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN4", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN5", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DA_IN6", "ab8500_0p", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("DA_IN7_CG2900"),
	SND_SOC_DAPM_INPUT("DA_IN8_CG2900"),
	SND_SOC_DAPM_SUPPLY("IF1 Enable",
			SND_SOC_NOPM, 0, 0, if1_enable_event,
			SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_AIF_OUT("AD_OUT1", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT2", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT3", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT4", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT57", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AD_OUT68", "ab8500_0c", 0, SND_SOC_NOPM, 0, 0),

	/* Headset path */

	SND_SOC_DAPM_SUPPLY("Charge Pump", REG_ANACONF5, REG_ANACONF5_ENCPHS,
			NORMAL, NULL, 0),

	SND_SOC_DAPM_DAC("DA1 Enable", "ab8500_0p",
			REG_DAPATHENA, REG_DAPATHENA_ENDA1, 0),
	SND_SOC_DAPM_DAC("DA2 Enable", "ab8500_0p",
			REG_DAPATHENA, REG_DAPATHENA_ENDA2, 0),

	SND_SOC_DAPM_MUX("Headset Left",
			SND_SOC_NOPM, 0, 0, &dapm_headset_left_mux),
	SND_SOC_DAPM_MUX("Headset Right",
			SND_SOC_NOPM, 0, 0, &dapm_headset_right_mux),

	SND_SOC_DAPM_PGA("HSL Digital Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HSR Digital Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_DAC("HSL DAC", "ab8500_0p",
			REG_DAPATHCONF, REG_DAPATHCONF_ENDACHSL, 0),
	SND_SOC_DAPM_DAC("HSR DAC", "ab8500_0p",
			REG_DAPATHCONF, REG_DAPATHCONF_ENDACHSR, 0),
	SND_SOC_DAPM_MIXER("HSL DAC Mute", REG_MUTECONF, REG_MUTECONF_MUTDACHSL,
			INVERT, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR DAC Mute", REG_MUTECONF, REG_MUTECONF_MUTDACHSR,
			INVERT, NULL, 0),
	SND_SOC_DAPM_MIXER("HSL DAC Driver", REG_ANACONF3, REG_ANACONF3_ENDRVHSL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR DAC Driver", REG_ANACONF3, REG_ANACONF3_ENDRVHSR, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("HSL Mute", REG_MUTECONF, REG_MUTECONF_MUTHSL,
			INVERT, NULL, 0),
	SND_SOC_DAPM_MIXER("HSR Mute", REG_MUTECONF, REG_MUTECONF_MUTHSR,
			INVERT, NULL, 0),
	SND_SOC_DAPM_PGA_E("HSL Enable", REG_ANACONF4, REG_ANACONF4_ENHSL,
			NORMAL, NULL, 0, hs_disable_event, SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HSR Enable", REG_ANACONF4, REG_ANACONF4_ENHSR,
			NORMAL, NULL, 0, hs_disable_event, SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA("HSL Gain", SND_SOC_NOPM, 0,
			0, NULL, 0),
	SND_SOC_DAPM_PGA("HSR Gain", SND_SOC_NOPM, 0,
			0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HSL"),
	SND_SOC_DAPM_OUTPUT("HSR"),

	/* LineOut path */

	SND_SOC_DAPM_MUX("LineOut Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_lineout_source),

	SND_SOC_DAPM_MIXER("LOL Enable", REG_ANACONF5,
			REG_ANACONF5_ENLOL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LOR Enable", REG_ANACONF5,
			REG_ANACONF5_ENLOR, 0, NULL, 0),

	SND_SOC_DAPM_MUX("LineOut Left",
			SND_SOC_NOPM, 0, 0, &dapm_lineout_left_mux),

	SND_SOC_DAPM_MUX("LineOut Right",
			SND_SOC_NOPM, 0, 0, &dapm_lineout_right_mux),

	/* Earpiece path */

	SND_SOC_DAPM_MUX("Earpiece or LineOut Mono Source",
			SND_SOC_NOPM, 0, 0, dapm_ear_lineout_source),

	SND_SOC_DAPM_MIXER("EAR DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACEAR, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Earpiece", SND_SOC_NOPM, 0, 0, &dapm_ear_mux),

	SND_SOC_DAPM_MIXER("EAR Mute", REG_MUTECONF,
			REG_MUTECONF_MUTEAR, 1, NULL, 0),

	SND_SOC_DAPM_MIXER("EAR Enable", REG_ANACONF4,
			REG_ANACONF4_ENEAR, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("EAR"),

	/* Handsfree path */

	SND_SOC_DAPM_MIXER("DA3 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA3, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA4 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA4, 0, NULL, 0),

	SND_SOC_DAPM_MUX("IHF Left Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_ihfl_select),
	SND_SOC_DAPM_MUX("IHF Right Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_ihfr_select),

	SND_SOC_DAPM_MUX("IHF Left", SND_SOC_NOPM, 0, 0, &dapm_ihf_left_mux),
	SND_SOC_DAPM_MUX("IHF Right", SND_SOC_NOPM, 0, 0, &dapm_ihf_right_mux),

	SND_SOC_DAPM_MUX("IHF or LineOut Select", SND_SOC_NOPM,
			0, 0, &dapm_ihf_or_lineout_select_mux),

	SND_SOC_DAPM_MIXER("IHFL DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACHFL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("IHFR DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACHFR, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("DA4 or ANC path to HfR", REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_DATOHFREN, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA3 or ANC path to HfL", REG_DIGMULTCONF2,
			REG_DIGMULTCONF2_DATOHFLEN, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("IHFL Enable", REG_ANACONF4,
			REG_ANACONF4_ENHFL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("IHFR Enable", REG_ANACONF4,
			REG_ANACONF4_ENHFR, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("IHFL"),
	SND_SOC_DAPM_OUTPUT("IHFR"),

	/* Vibrator path */

	SND_SOC_DAPM_MUX("Vibra 1", SND_SOC_NOPM, 0, 0, &dapm_vibra1_mux),
	SND_SOC_DAPM_MIXER("DA5 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA5, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("VIB1 DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACVIB1, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("PWMGEN1"),

	SND_SOC_DAPM_MUX("Vibra 1 Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_pwm2vib1),

	SND_SOC_DAPM_MIXER("VIB1 Enable", REG_ANACONF4,
			REG_ANACONF4_ENVIB1, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("VIB1"),

	/* LineIn & Microphone 2 path */

	SND_SOC_DAPM_INPUT("LINL"),
	SND_SOC_DAPM_INPUT("LINR"),
	SND_SOC_DAPM_INPUT("MIC2 Input"),


	SND_SOC_DAPM_MUX("LineIn Left", SND_SOC_NOPM, 0, 0, &dapm_linl_mux),
	SND_SOC_DAPM_MIXER("LINL Mute", REG_ANACONF2,
			REG_ANACONF2_MUTLINL, INVERT, NULL, 0),

	SND_SOC_DAPM_MUX("LineIn Right", SND_SOC_NOPM, 0, 0, &dapm_linr_mux),
	SND_SOC_DAPM_MIXER("LINR Mute", REG_ANACONF2,
			REG_ANACONF2_MUTLINR, INVERT, NULL, 0),

	SND_SOC_DAPM_MUX("Mic 2", SND_SOC_NOPM, 0, 0, &dapm_mic2_mux),
	SND_SOC_DAPM_MIXER("MIC2 Mute", REG_ANACONF2,
			REG_ANACONF2_MUTMIC2, INVERT, NULL, 0),

	SND_SOC_DAPM_MIXER_E("LINL Enable", REG_ANACONF2,
			REG_ANACONF2_ENLINL, 0, NULL, 0,
			linein_enable_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("LINR Enable", REG_ANACONF2,
			REG_ANACONF2_ENLINR, 0, NULL, 0,
			linein_enable_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("MIC2 Enable", REG_ANACONF2,
			REG_ANACONF2_ENMIC2, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Mic 2 or LINR Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_mic2lr_select),

	SND_SOC_DAPM_MIXER("LINL ADC", REG_ANACONF3,
			REG_ANACONF3_ENADCLINL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LINR ADC", REG_ANACONF3,
			REG_ANACONF3_ENADCLINR, 0, NULL, 0),

	SND_SOC_DAPM_MUX("AD 1 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad1_select),
	SND_SOC_DAPM_MUX("AD 2 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad2_select),

	SND_SOC_DAPM_MIXER("AD1 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD2 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD12 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD12, 0, NULL, 0),

	/* Microphone 1 path */

	SND_SOC_DAPM_INPUT("MIC1A Input"),
	SND_SOC_DAPM_INPUT("MIC1B Input"),

	SND_SOC_DAPM_MUX("Mic 1A or 1B Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_mic1ab_select),

	SND_SOC_DAPM_MUX("Mic 1", SND_SOC_NOPM, 0, 0, &dapm_mic1_mux),

	SND_SOC_DAPM_MIXER("MIC1 Mute", REG_ANACONF2,
			REG_ANACONF2_MUTMIC1, INVERT, NULL, 0),

	SND_SOC_DAPM_MIXER("MIC1 Enable", REG_ANACONF2,
			REG_ANACONF2_ENMIC1, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("MIC1 ADC", REG_ANACONF3,
			REG_ANACONF3_ENADCMIC, 0, NULL, 0),

	SND_SOC_DAPM_MUX("AD 3 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad3_select),

	SND_SOC_DAPM_MIXER("AD3 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD3 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD34, 0, NULL, 0),

	/* HD Capture path */

	SND_SOC_DAPM_MUX("AD 5 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad5_select),
	SND_SOC_DAPM_MUX("AD 6 Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_ad6_select),

	SND_SOC_DAPM_MIXER("AD5 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD6 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD57 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD5768, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("AD68 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD5768, 0, NULL, 0),

	/* Digital Microphone path */

	SND_SOC_DAPM_INPUT("DMIC Input"),

	SND_SOC_DAPM_MUX("DMic 1", SND_SOC_NOPM, 0, 0, &dapm_dmic1_mux),
	SND_SOC_DAPM_MUX("DMic 2", SND_SOC_NOPM, 0, 0, &dapm_dmic2_mux),
	SND_SOC_DAPM_MUX("DMic 3", SND_SOC_NOPM, 0, 0, &dapm_dmic3_mux),
	SND_SOC_DAPM_MUX("DMic 4", SND_SOC_NOPM, 0, 0, &dapm_dmic4_mux),
	SND_SOC_DAPM_MUX("DMic 5", SND_SOC_NOPM, 0, 0, &dapm_dmic5_mux),
	SND_SOC_DAPM_MUX("DMic 6", SND_SOC_NOPM, 0, 0, &dapm_dmic6_mux),

	SND_SOC_DAPM_MIXER("DMIC1 Mute", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC1, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DMIC2 Mute", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC2, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DMIC3 Mute", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC3, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DMIC4 Mute", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC4, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DMIC5 Mute", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC5, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DMIC6 Mute", REG_DIGMICCONF,
			REG_DIGMICCONF_ENDMIC6, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD4 Channel Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("AD4 Enable", REG_ADPATHENA,
			REG_ADPATHENA_ENAD34, 0, NULL, 0),

	/* LineIn to Bypass path */

	SND_SOC_DAPM_MIXER("LINL to HSL Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LINR to HSR Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("LineIn Left to Headset Left", SND_SOC_NOPM, 0, 0, &dapm_linl_to_hs_left_mux),
	SND_SOC_DAPM_MUX("LineIn Right to Headset Right", SND_SOC_NOPM, 0, 0, &dapm_linr_to_hs_right_mux),

	/* LineIn to Speaker */

	SND_SOC_DAPM_MUX("AD1 to IHF Left", SND_SOC_NOPM, 0, 0, &dapm_ad1_to_ihf_left_mux),
	SND_SOC_DAPM_MUX("AD2 to IHF Right", SND_SOC_NOPM, 0, 0, &dapm_ad2_to_ihf_right_mux),

	/* Digital interface - Loopback */

	SND_SOC_DAPM_MUX("Digital Interface AD 1 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad1loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 2 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad2loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 3 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad3loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 4 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad4loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 5 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad5loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 6 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad6loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 7 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad7loop_mux),
	SND_SOC_DAPM_MUX("Digital Interface AD 8 Loopback Switch", SND_SOC_NOPM, 0, 0, dapm_ad8loop_mux),

	/* Acoustical Noise Cancellation path */

	SND_SOC_DAPM_MUX("ANC Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_anc_in_select),

	SND_SOC_DAPM_MUX("ANC Playback Switch",
			SND_SOC_NOPM, 0, 0, dapm_anc_enable),

	SND_SOC_DAPM_SWITCH("ANC to Earpiece",
			SND_SOC_NOPM, 0, 0, dapm_anc_ear_mute),

	/* Sidetone Filter path */

	SND_SOC_DAPM_MUX("Sidetone Left Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_stfir1_in_select),
	SND_SOC_DAPM_MUX("Sidetone Right Source Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_stfir2_in_select),

	SND_SOC_DAPM_MUX("Sidetone Left",
			SND_SOC_NOPM, 0, 0, &dapm_stfir1_ena),
	SND_SOC_DAPM_MUX("Sidetone Right",
			SND_SOC_NOPM, 0, 0, &dapm_stfir2_ena),

	SND_SOC_DAPM_MIXER_E("STFIR1 Control", SND_SOC_NOPM, 0, 0, NULL, 0,
		stfir_enable_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER_E("STFIR2 Control", SND_SOC_NOPM, 0, 0, NULL, 0,
		stfir_enable_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MIXER("STFIR1 Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("STFIR2 Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget ab8500_dapm_widgets[] = {

	/* Vibrator path */

	SND_SOC_DAPM_MUX("Vibra 2", SND_SOC_NOPM, 0, 0, &dapm_vibra2_mux),
	SND_SOC_DAPM_MIXER("DA6 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA6, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("VIB2 DAC", REG_DAPATHCONF,
			REG_DAPATHCONF_ENDACVIB2, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("PWMGEN2"),

	SND_SOC_DAPM_MUX("Vibra 2 Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_pwm2vib2),

	SND_SOC_DAPM_MIXER("VIB2 Enable", REG_ANACONF4,
			REG_ANACONF4_ENVIB2, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("VIB2"),
};

static const struct snd_soc_dapm_widget ab8505_vx_dapm_widgets[] = {

	/* USHLR & CKL path */

	SND_SOC_DAPM_MUX("USB Headset Left",
			SND_SOC_NOPM, 0, 0, &dapm_usbheadset_left_mux),
	SND_SOC_DAPM_MUX("USB Headset Right",
			SND_SOC_NOPM, 0, 0, &dapm_usbheadset_right_mux),

	SND_SOC_DAPM_MIXER("UHSL Mute", REG_USBDRVCTRL, REG_USBDRVCTRL_MUTEUHSL,
			1, NULL, 0),
	SND_SOC_DAPM_MIXER("UHSR Mute", REG_USBDRVCTRL, REG_USBDRVCTRL_MUTEUHSR,
			1, NULL, 0),

	SND_SOC_DAPM_MIXER("UHSL Enable", REG_USBDRVCTRL,
			REG_USBDRVCTRL_ENUHSL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("UHSR Enable", REG_USBDRVCTRL,
			REG_USBDRVCTRL_ENUHSR, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Carkit Left", SND_SOC_NOPM, 0, 0,
			&dapm_ck_left_mux),
	SND_SOC_DAPM_MUX("Carkit Right", SND_SOC_NOPM, 0, 0,
			&dapm_ck_right_mux),

	SND_SOC_DAPM_MIXER("Carkit Left to DM", REG_USBDRVCTRL,
			REG_USBDRVCTRL_ENCKLOLDM, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Carkit Right to DP", REG_USBDRVCTRL,
			REG_USBDRVCTRL_ENCKLORDP, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("CKLoL Enable", REG_USBDRVCTRL,
			REG_USBDRVCTRL_ENCKLOL, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("CKLoR Enable", REG_USBDRVCTRL,
			REG_USBDRVCTRL_ENCKLOR, 0, NULL, 0),

	SND_SOC_DAPM_PGA("UHSL Gain", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("UHSR Gain", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("UHSL"),
	SND_SOC_DAPM_OUTPUT("UHSR"),

	SND_SOC_DAPM_OUTPUT("CKLoL"),
	SND_SOC_DAPM_OUTPUT("CKLoR"),

	/* Handsfree path */

	SND_SOC_DAPM_MUX("IHF Right Select Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_da_hfr_select),

	/* USB Mic path */

	SND_SOC_DAPM_MUX("USB Mic Select Capture Route",
			SND_SOC_NOPM, 0, 0, dapm_usbmic_select),

	/* PDM1 & PDM2 path */

	SND_SOC_DAPM_MUX("PDM 1", SND_SOC_NOPM, 0, 0, &dapm_pdm1_mux),
	SND_SOC_DAPM_MUX("PDM 2", SND_SOC_NOPM, 0, 0, &dapm_pdm2_mux),

	SND_SOC_DAPM_MIXER("PDM1 Enable", REG_PDMCTRL, REG_PDMCTRL_ENPDM1,
			0, NULL, 0),
	SND_SOC_DAPM_MIXER("PDM2 Enable", REG_PDMCTRL, REG_PDMCTRL_ENPDM2,
			0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("PDM"),

	/* EPWM1 & EPWM2 path */

	SND_SOC_DAPM_MUX("EPWM 1", SND_SOC_NOPM, 0, 0, &dapm_epwm1_mux),
	SND_SOC_DAPM_MUX("EPWM 2", SND_SOC_NOPM, 0, 0, &dapm_epwm2_mux),

	SND_SOC_DAPM_MIXER("DA4 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA4, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA6 Channel Gain", REG_DAPATHENA,
			REG_DAPATHENA_ENDA6, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("EPWM1 Enable", REG_EPWM1CONF, REG_EPWM1CONF_EN,
			0, NULL, 0),
	SND_SOC_DAPM_MIXER("EPWM2 Enable", REG_EPWM2CONF, REG_EPWM2CONF_EN,
			0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("EPWM1"),
	SND_SOC_DAPM_OUTPUT("EPWM2"),
};

static const struct snd_soc_dapm_widget ab8505_v1_dapm_widgets[] = {

	/* Handsfree path */

	SND_SOC_DAPM_MUX("IHF Left Select Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_da_hfl_select_v1),
};

static const struct snd_soc_dapm_widget ab8505_v2_dapm_widgets[] = {

	/* Handsfree path */

	SND_SOC_DAPM_MUX("IHF Left Select Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_da_hfl_select_v2),

	/* DA7 & DA8 path */

	SND_SOC_DAPM_MUX("DA7 to DA1", SND_SOC_NOPM, 0, 0, &dapm_da7toda1_mux),
	SND_SOC_DAPM_MUX("DA7 to DA3", SND_SOC_NOPM, 0, 0, &dapm_da7toda3_mux),
	SND_SOC_DAPM_MUX("DA8 to DA2", SND_SOC_NOPM, 0, 0, &dapm_da8toda2_mux),
	SND_SOC_DAPM_MUX("DA8 to DA4", SND_SOC_NOPM, 0, 0, &dapm_da8toda4_mux),

	SND_SOC_DAPM_MIXER("DA7DA1 Enable", REG_MIXCTRL, REG_MIXCTRL_DA7ADDDA1,
			0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA7DA3 Enable", REG_MIXCTRL, REG_MIXCTRL_DA7ADDDA3,
			0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA8DA2 Enable", REG_MIXCTRL, REG_MIXCTRL_DA8ADDDA2,
			0, NULL, 0),
	SND_SOC_DAPM_MIXER("DA8DA4 Enable", REG_MIXCTRL, REG_MIXCTRL_DA8ADDDA4,
			0, NULL, 0),

	/* Handsfree path */

	SND_SOC_DAPM_MUX("IHF Left Select 2 Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_hfl2_select),
	SND_SOC_DAPM_MUX("IHF Right Select 2 Playback Route",
			SND_SOC_NOPM, 0, 0, dapm_hfr2_select),
};

/* DAPM-routes */

static const struct snd_soc_dapm_route ab850x_dapm_routes[] = {
	/* AD/DA */
	{"ADC", NULL, "ADC Input"},
	{"DAC Output", NULL, "DAC"},

	/* Powerup charge pump if DA1/2 is in use */
	{"HSL Mute", NULL, "Charge Pump"},
	{"HSR Mute", NULL, "Charge Pump"},

	/* Headset path */

	{"DA1 Enable", NULL, "DA_IN1"},
	{"DA2 Enable", NULL, "DA_IN2"},

	{"HSL Digital Gain", NULL, "DA1 Enable"},
	{"HSR Digital Gain", NULL, "DA2 Enable"},

	{"HSL DAC", NULL, "HSL Digital Gain"},
	{"HSR DAC", NULL, "HSR Digital Gain"},

	{"HSL DAC Mute", NULL, "HSL DAC"},
	{"HSR DAC Mute", NULL, "HSR DAC"},

	{"HSL DAC Driver", NULL, "HSL DAC Mute"},
	{"HSR DAC Driver", NULL, "HSR DAC Mute"},

	{"HSL Mute", NULL, "HSL DAC Driver"},
	{"HSR Mute", NULL, "HSR DAC Driver"},

	{"Headset Left", "Enabled", "HSL Mute"},
	{"Headset Right", "Enabled", "HSR Mute"},

	{"HSL Enable", NULL, "Headset Left"},
	{"HSR Enable", NULL, "Headset Right"},

	{"HSL Gain", NULL, "HSL Enable"},
	{"HSR Gain", NULL, "HSR Enable"},

	{"HSL", NULL, "HSL Gain"},
	{"HSR", NULL, "HSR Gain"},

	/* IHF or LineOut path */

	{"DA3 Channel Gain", NULL, "DA_IN3"},
	{"DA4 Channel Gain", NULL, "DA_IN4"},

	{"IHF Left Source Playback Route", "Audio Path", "DA3 Channel Gain"},
	{"IHF Right Source Playback Route", "Audio Path", "DA4 Channel Gain"},

	{"DA3 or ANC path to HfL", NULL, "IHF Left Source Playback Route"},
	{"DA4 or ANC path to HfR", NULL, "IHF Right Source Playback Route"},

	/* IHF path */

	{"IHF Left", "Enabled", "DA3 or ANC path to HfL"},
	{"IHF Right", "Enabled", "DA4 or ANC path to HfR"},

	{"IHFL DAC", NULL, "IHF Left"},
	{"IHFR DAC", NULL, "IHF Right"},

	{"IHF or LineOut Select", "IHF", "IHFL Enable"},
	{"IHF or LineOut Select", "IHF", "IHFR Enable"},

	/* Earpiece path */

	{"Earpiece or LineOut Mono Source", "Headset Left", "HSL Digital Gain"},
	{"Earpiece or LineOut Mono Source", "IHF Left", "DA3 or ANC path to HfL"},

	{"EAR DAC", NULL, "Earpiece or LineOut Mono Source"},

	{"Earpiece", "Enabled", "EAR DAC"},

	{"EAR Mute", NULL, "Earpiece"},

	{"EAR Enable", NULL, "EAR Mute"},

	{"EAR", NULL, "EAR Enable"},

	/* LineOut path stereo */

	{"LineOut Source Playback Route", "Stereo Path", "HSL DAC Driver"},
	{"LineOut Source Playback Route", "Stereo Path", "HSR DAC Driver"},

	/* LineOut path mono */

	{"LineOut Source Playback Route", "Mono Path", "EAR DAC"},

	/* LineOut path */

	{"LineOut Left", "Enabled", "LineOut Source Playback Route"},
	{"LineOut Right", "Enabled", "LineOut Source Playback Route"},

	{"LOL Enable", NULL, "LineOut Left"},
	{"LOR Enable", NULL, "LineOut Right"},

	{"IHF or LineOut Select", "LineOut", "LOL Enable"},
	{"IHF or LineOut Select", "LineOut", "LOR Enable"},

	/* IHF path */

	{"IHFL", NULL, "IHF or LineOut Select"},
	{"IHFR", NULL, "IHF or LineOut Select"},

	/* Vibrator path */

	{"DA5 Channel Gain", NULL, "DA_IN5"},

	{"VIB1 DAC", NULL, "DA5 Channel Gain"},

	{"Vibra 1 Playback Route", "Audio Path", "VIB1 DAC"},
	{"Vibra 1 Playback Route", "PWM Generator", "PWMGEN1"},

	{"Vibra 1", "Enabled", "Vibra 1 Playback Route"},

	{"VIB1 Enable", NULL, "Vibra 1"},

	{"VIB1", NULL, "VIB1 Enable"},

	/* LineIn & Microphone 2 path */

	{"LineIn Left", "Enabled", "LINL"},
	{"LineIn Right", "Enabled", "LINR"},

	{"LINL Mute", NULL, "LineIn Left"},
	{"LINR Mute", NULL, "LineIn Right"},

	{"Mic 2", "Enabled", "MIC2 Input"},

	{"MIC2 Mute", NULL, "Mic 2"},

	{"LINL Enable", NULL, "LINL Mute"},
	{"LINR Enable", NULL, "LINR Mute"},
	{"MIC2 Enable", NULL, "MIC2 Mute"},

	{"Mic 2 or LINR Select Capture Route", "LineIn Right", "LINR Enable"},
	{"Mic 2 or LINR Select Capture Route", "Mic 2", "MIC2 Enable"},

	{"LINL ADC", NULL, "LINL Enable"},
	{"LINR ADC", NULL, "Mic 2 or LINR Select Capture Route"},

	{"AD 1 Select Capture Route", "LineIn Left", "LINL ADC"},
	{"AD 2 Select Capture Route", "LineIn Right", "LINR ADC"},

	{"AD1 Channel Gain", NULL, "AD 1 Select Capture Route"},
	{"AD2 Channel Gain", NULL, "AD 2 Select Capture Route"},

	{"AD12 Enable", NULL, "AD1 Channel Gain"},
	{"AD12 Enable", NULL, "AD2 Channel Gain"},

	{"AD_OUT1", NULL, "AD12 Enable"},
	{"AD_OUT2", NULL, "AD12 Enable"},

	/* Microphone 1 path */

	{"Mic 1A or 1B Select Capture Route", "Mic 1A", "MIC1A Input"},
	{"Mic 1A or 1B Select Capture Route", "Mic 1B", "MIC1B Input"},

	{"Mic 1", "Enabled", "Mic 1A or 1B Select Capture Route"},

	{"MIC1 Mute", NULL, "Mic 1"},

	{"MIC1 Enable", NULL, "MIC1 Mute"},

	{"MIC1 ADC", NULL, "MIC1 Enable"},

	{"AD 3 Select Capture Route", "Mic 1", "MIC1 ADC"},

	{"AD3 Channel Gain", NULL, "AD 3 Select Capture Route"},

	{"AD3 Enable", NULL, "AD3 Channel Gain"},

	{"AD_OUT3", NULL, "AD3 Enable"},

	/* HD Capture path */

	{"AD 5 Select Capture Route", "Mic 2", "LINR ADC"},
	{"AD 6 Select Capture Route", "Mic 1", "MIC1 ADC"},

	{"AD5 Channel Gain", NULL, "AD 5 Select Capture Route"},
	{"AD6 Channel Gain", NULL, "AD 6 Select Capture Route"},

	{"AD57 Enable", NULL, "AD5 Channel Gain"},
	{"AD68 Enable", NULL, "AD6 Channel Gain"},

	{"AD_OUT57", NULL, "AD57 Enable"},
	{"AD_OUT68", NULL, "AD68 Enable"},

	/* Digital Microphone path */

	{"DMic 1", "Enabled", "DMIC Input"},
	{"DMic 2", "Enabled", "DMIC Input"},
	{"DMic 3", "Enabled", "DMIC Input"},
	{"DMic 4", "Enabled", "DMIC Input"},
	{"DMic 5", "Enabled", "DMIC Input"},
	{"DMic 6", "Enabled", "DMIC Input"},

	{"DMIC1 Mute", NULL, "DMic 1"},
	{"DMIC2 Mute", NULL, "DMic 2"},
	{"DMIC3 Mute", NULL, "DMic 3"},
	{"DMIC4 Mute", NULL, "DMic 4"},
	{"DMIC5 Mute", NULL, "DMic 5"},
	{"DMIC6 Mute", NULL, "DMic 6"},

	{"AD 1 Select Capture Route", "DMic 1", "DMIC1 Mute"},
	{"AD 2 Select Capture Route", "DMic 2", "DMIC2 Mute"},
	{"AD 3 Select Capture Route", "DMic 3", "DMIC3 Mute"},
	{"AD 5 Select Capture Route", "DMic 5", "DMIC5 Mute"},
	{"AD 6 Select Capture Route", "DMic 6", "DMIC6 Mute"},

	{"AD4 Channel Gain", NULL, "DMIC4 Mute"},

	{"AD4 Enable", NULL, "AD4 Channel Gain"},

	{"AD_OUT4", NULL, "AD4 Enable"},

	/* LineIn to Headset Bypass path */

	{"LINL to HSL Gain", NULL, "LINL Enable"},
	{"LINR to HSR Gain", NULL, "LINR Enable"},

	{"LineIn Left to Headset Left", "Enabled", "LINL to HSL Gain"},
	{"LineIn Right to Headset Right", "Enabled", "LINR to HSR Gain"},

	{"HSL DAC Driver", NULL, "LineIn Left to Headset Left"},
	{"HSR DAC Driver", NULL, "LineIn Right to Headset Right"},

	/* LineIn to Speaker path */

	{"AD1 to IHF Left", "Enabled", "AD 1 Select Capture Route"},
	{"AD2 to IHF Right", "Enabled", "AD 2 Select Capture Route"},

	{"IHFL DAC", NULL, "AD1 to IHF Left"},
	{"IHFR DAC", NULL, "AD2 to IHF Right"},

	/* Acoustical Noise Cancellation path */

	{"ANC Source Playback Route", "Mic 2 / DMic 5", "AD5 Channel Gain"},
	{"ANC Source Playback Route", "Mic 1 / DMic 6", "AD6 Channel Gain"},

	{"ANC Playback Switch", "Enabled", "ANC Source Playback Route"},

	{"IHF Left Source Playback Route", "ANC", "ANC Playback Switch"},
	{"IHF Right Source Playback Route", "ANC", "ANC Playback Switch"},
	{"ANC to Earpiece", "Playback Switch", "ANC Playback Switch"},

	{"HSL Digital Gain", NULL, "ANC to Earpiece"},

	/* Sidetone Filter path */

	{"Sidetone Left Source Playback Route", "LineIn Left", "AD12 Enable"},
	{"Sidetone Left Source Playback Route", "LineIn Right", "AD12 Enable"},
	{"Sidetone Left Source Playback Route", "Mic 1", "AD3 Enable"},
	{"Sidetone Left Source Playback Route", "Headset Left", "DA_IN1"},
	{"Sidetone Right Source Playback Route", "LineIn Right", "AD12 Enable"},
	{"Sidetone Right Source Playback Route", "Mic 1", "AD3 Enable"},
	{"Sidetone Right Source Playback Route", "DMic 4", "AD4 Enable"},
	{"Sidetone Right Source Playback Route", "Headset Right", "DA_IN2"},

	{"Sidetone Left", "Enabled", "Sidetone Left Source Playback Route"},
	{"Sidetone Right", "Enabled", "Sidetone Right Source Playback Route"},

	{"STFIR1 Control", NULL, "Sidetone Left"},
	{"STFIR2 Control", NULL, "Sidetone Right"},

	{"STFIR1 Gain", NULL, "STFIR1 Control"},
	{"STFIR2 Gain", NULL, "STFIR2 Control"},

	{"DA1 Enable", NULL, "STFIR1 Gain"},
	{"DA2 Enable", NULL, "STFIR2 Gain"},


	{"Digital Interface AD 1 Loopback Switch", "Enabled", "DA_IN7_CG2900"},
	{"Digital Interface AD 2 Loopback Switch", "Enabled", "DA_IN8_CG2900"},
	{"Digital Interface AD 3 Loopback Switch", "Enabled", "DA_IN7_CG2900"},
	{"Digital Interface AD 4 Loopback Switch", "Enabled", "DA_IN8_CG2900"},
	{"Digital Interface AD 5 Loopback Switch", "Enabled", "DA_IN7_CG2900"},
	{"Digital Interface AD 6 Loopback Switch", "Enabled", "DA_IN8_CG2900"},
	{"Digital Interface AD 7 Loopback Switch", "Enabled", "DA_IN8_CG2900"},
	{"Digital Interface AD 8 Loopback Switch", "Enabled", "DA_IN7_CG2900"},

	{"AD_OUT1", NULL, "Digital Interface AD 1 Loopback Switch"},
	{"AD_OUT2", NULL, "Digital Interface AD 2 Loopback Switch"},
	{"AD_OUT3", NULL, "Digital Interface AD 3 Loopback Switch"},
	{"AD_OUT4", NULL, "Digital Interface AD 4 Loopback Switch"},
	{"AD_OUT57", NULL, "Digital Interface AD 5 Loopback Switch"},
	{"AD_OUT68", NULL, "Digital Interface AD 6 Loopback Switch"},
	{"AD_OUT57", NULL, "Digital Interface AD 7 Loopback Switch"},
	{"AD_OUT68", NULL, "Digital Interface AD 8 Loopback Switch"},

};

static const struct snd_soc_dapm_route ab8500_dapm_routes[] = {

	/* IHF path */

	{"IHFL Enable", NULL, "IHFL DAC"},
	{"IHFR Enable", NULL, "IHFR DAC"},

	{"IHFL DAC", NULL, "IHF Left"},
	{"IHFR DAC", NULL, "IHF Right"},

	/* Vibrator path */

	{"DA6 Channel Gain", NULL, "DA_IN6"},

	{"VIB2 DAC", NULL, "DA6 Channel Gain"},

	{"Vibra 2 Playback Route", "Audio Path", "VIB2 DAC"},
	{"Vibra 2 Playback Route", "PWM Generator", "PWMGEN2"},

	{"Vibra 2", "Enabled", "Vibra 2 Playback Route"},

	{"VIB2 Enable", NULL, "Vibra 2"},

	{"VIB2", NULL, "VIB2 Enable"},
};

static const struct snd_soc_dapm_route ab8505_vx_dapm_routes[] = {

	/* Headset & USHLR & CK path */

	{"UHSL Mute", NULL, "Charge Pump"},
	{"UHSR Mute", NULL, "Charge Pump"},

	{"UHSL Mute", NULL, "HSL DAC Driver"},
	{"UHSR Mute", NULL, "HSR DAC Driver"},
	{"CKLoL Enable", NULL, "HSL DAC Driver"},
	{"CKLoR Enable", NULL, "HSR DAC Driver"},

	{"USB Headset Left", "Enabled", "UHSL Mute"},
	{"USB Headset Right", "Enabled", "UHSR Mute"},
	{"Carkit Left", "Enabled", "CKLoL Enable"},
	{"Carkit Right", "Enabled", "CKLoR Enable"},
	{"Carkit Left to DM", NULL, "Carkit Left"},
	{"Carkit Right to DP", NULL, "Carkit Right"},

	{"UHSL Enable", NULL, "USB Headset Left"},
	{"UHSR Enable", NULL, "USB Headset Right"},

	{"UHSL Gain", NULL, "UHSL Enable"},
	{"UHSR Gain", NULL, "UHSR Enable"},

	{"UHSL", NULL, "UHSL Gain"},
	{"UHSR", NULL, "UHSR Gain"},
	{"CKLoL", NULL, "Carkit Left to DM"},
	{"CKLoR", NULL, "Carkit Right to DP"},

	/* USB Mic path */

	/* IHF path */

	{"IHF Left Select Playback Route", "DA3", "IHF Left"},
	{"IHF Right Select Playback Route", "DA4", "IHF Right"},
	{"IHF Right Select Playback Route", "DA1", "DA1 Enable"},

	{"IHFL DAC", NULL, "IHF Left Select Playback Route"},
	{"IHFR DAC", NULL, "IHF Right Select Playback Route"},

	/* PDM1 & PDM2 path */

	{"PDM 1", "Enabled", "IHF Left"},
	{"PDM 2", "Enabled", "IHF Right"},

	{"PDM1 Enable", NULL, "PDM 1"},
	{"PDM2 Enable", NULL, "PDM 2"},

	{"PDM", NULL, "PDM1 Enable"},
	{"PDM", NULL, "PDM2 Enable"},

	/* EPWM1 & EPWM2 path */

	{"DA4 Channel Gain", NULL, "DA_IN4"},
	{"DA6 Channel Gain", NULL, "DA_IN6"},

	{"EPWM 2", "Enabled", "DA4 Channel Gain"},
	{"EPWM 1", "Enabled", "DA6 Channel Gain"},

	{"EPWM2 Enable", NULL, "EPWM 2"},
	{"EPWM1 Enable", NULL, "EPWM 1"},

	{"EPWM2", NULL, "EPWM2 Enable"},
	{"EPWM1", NULL, "EPWM1 Enable"},
};

static const struct snd_soc_dapm_route ab8505_v1_dapm_routes[] = {

	/* IHF path */

	{"IHF Left Select Playback Route", "DA5", "DA5 Channel Gain"},

	{"IHFL Enable", NULL, "IHFL DAC"},
	{"IHFR Enable", NULL, "IHFR DAC"},
};

static const struct snd_soc_dapm_route ab8505_v2_dapm_routes[] = {

	/* IHF path */

	{"IHF Left Select Playback Route", "DA6", "DA6 Channel Gain"},

	/* DA7 & DA8 path */

	{"DA_IN7_CG2900", NULL, "IF1 Enable"},
	{"DA_IN8_CG2900", NULL, "IF1 Enable"},

	{"DA7 to DA1", "Enabled", "DA_IN7_CG2900"},
	{"DA7DA1 Enable", NULL, "DA7 to DA1"},
	{"DA1 Enable", NULL, "DA7DA1 Enable"},

	{"DA7 to DA3", "Enabled", "DA_IN7_CG2900"},
	{"DA7DA3 Enable", NULL, "DA7 to DA3"},
	{"DA3 Channel Gain", NULL, "DA7DA3 Enable"},

	{"DA8 to DA2", "Enabled", "DA_IN8_CG2900"},
	{"DA8DA2 Enable", NULL, "DA8 to DA2"},
	{"DA2 Enable", NULL, "DA8DA2 Enable"},

	{"DA8 to DA4", "Enabled", "DA_IN8_CG2900"},
	{"DA8DA4 Enable", NULL, "DA8 to DA4"},
	{"DA4 Channel Gain", NULL, "DA8DA4 Enable"},

	/* IHF path */

	{"IHF Left Select 2 Playback Route", "IHFL", "IHFL DAC"},
	{"IHF Right Select 2 Playback Route", "IHFR", "IHFR DAC"},

	{"IHFL Enable", NULL, "IHF Left Select 2 Playback Route"},
	{"IHFR Enable", NULL, "IHF Right Select 2 Playback Route"},

	/* EPWM1 & EPWM2 path */

	{"IHF Left Select 2 Playback Route", "EPWM2", "EPWM2 Enable"},
	{"IHF Right Select 2 Playback Route", "EPWM1", "EPWM1 Enable"},
};

/* Controls - Non-DAPM ASoC */

/* from -31 to 31 dB in 1 dB steps (mute instead of -32 dB) */
static DECLARE_TLV_DB_SCALE(adx_dig_gain_tlv, -3200, 100, 1);

/* from -62 to 0 dB in 1 dB steps (mute instead of -63 dB) */
static DECLARE_TLV_DB_SCALE(dax_dig_gain_tlv, -6300, 100, 1);

/* from 0 to 8 dB in 1 dB steps (mute instead of -1 dB) */
static DECLARE_TLV_DB_SCALE(hs_ear_dig_gain_tlv, -100, 100, 1);

/* from -30 to 0 dB in 1 dB steps (mute instead of -31 dB) */
static DECLARE_TLV_DB_SCALE(stfir_dig_gain_tlv, -3100, 100, 1);

/* from -32 to -20 dB in 4 dB steps / from -18 to 2 dB in 2 dB steps */
static const unsigned int ab8500_hs_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 3, TLV_DB_SCALE_ITEM(-3200, 400, 0),
	4, 15, TLV_DB_SCALE_ITEM(-1800, 200, 0),
};

/* -31.8, -28.2, -32 to -20 dB in 4 dB steps / -18 to 2 dB in 2 dB steps */
static const unsigned int ab8505_hs_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(-3180, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-2820, 0, 0),
	2, 3, TLV_DB_SCALE_ITEM(-2400, 400, 0),
	4, 15, TLV_DB_SCALE_ITEM(-1800, 200, 0),
};

/* -31.8, -28.2, -24 to -20 dB in 4 dB steps / -18 to 2 dB in 2 dB steps */
static const unsigned int uhs_gain_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 0, TLV_DB_SCALE_ITEM(-3180, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-2820, 0, 0),
	2, 3, TLV_DB_SCALE_ITEM(-2400, 400, 0),
	4, 15, TLV_DB_SCALE_ITEM(-1800, 200, 0),
};

/* from -3 to 12 dB in 1 dB steps (ab8505 v1) */
static DECLARE_TLV_DB_SCALE(hf_dig_gain_v1_tlv, -300, 100, 0);

/* from -3 to 20 dB in 1 dB steps (ab8505 v2) (mute instead of -4 dB) */
static DECLARE_TLV_DB_SCALE(hf_dig_gain_v2_tlv, -400, 100, 1);

/* from -3 to 12 dB in 1 dB steps (ab8505 v1) */
static DECLARE_TLV_DB_SCALE(vib_dig_gain_v1_tlv, -300, 100, 0);

/* from -3 to 20 dB in 1 dB steps (ab8505 v2) (mute instead of -4 dB) */
static DECLARE_TLV_DB_SCALE(vib_dig_gain_v2_tlv, -400, 100, 1);

/* from -8 to 8 dB in 2 dB steps */
static DECLARE_TLV_DB_SCALE(ear_gain_tlv, -800, 200, 0);

/* from 0 to 31 dB in 1 dB steps */
static DECLARE_TLV_DB_SCALE(mic_gain_tlv, 0, 100, 0);

/* from -10 to 20 dB in 2 dB steps */
static DECLARE_TLV_DB_SCALE(lin_gain_tlv, -1000, 200, 0);

/* from -36 to 0 dB in 2 dB steps (mute instead of -38 dB) */
static DECLARE_TLV_DB_SCALE(lin2hs_gain_tlv, -3800, 200, 1);

static SOC_ENUM_SINGLE_DECL(soc_enum_hshpen,
	REG_ANACONF1, REG_ANACONF1_HSHPEN, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_hslowpow,
	REG_ANACONF1, REG_ANACONF1_HSLOWPOW, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_daclowpow1,
	REG_ANACONF1, REG_ANACONF1_DACLOWPOW1, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_daclowpow0,
	REG_ANACONF1, REG_ANACONF1_DACLOWPOW0, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_eardaclowpow,
	REG_ANACONF1, REG_ANACONF1_EARDACLOWPOW, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_eardrvlowpow,
	REG_ANACONF1, REG_ANACONF1_EARDRVLOWPOW, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_enhpear,
	REG_DAPATHENA, REG_DAPATHENA_ENHPEAR, enum_dis_ena);

static const char * const enum_earselcm[] = {"0.95V", "1.10V", "1.27V", "1.58V"};
static SOC_ENUM_SINGLE_DECL(soc_enum_earselcm,
	REG_ANACONF1, REG_ANACONF1_EARSELCM, enum_earselcm);

static const char * const enum_hsfadspeed[] = {"2ms", "0.5ms", "10.6ms", "5ms"};
static SOC_ENUM_SINGLE_DECL(soc_enum_hsfadspeed,
	REG_DIGMICCONF, REG_DIGMICCONF_HSFADSPEED, enum_hsfadspeed);

static const char * const enum_envdetthre[] = {
	"250mV", "300mV", "350mV", "400mV",
	"450mV", "500mV", "550mV", "600mV",
	"650mV", "700mV", "750mV", "800mV",
	"850mV", "900mV", "950mV", "1.00V" };
static SOC_ENUM_SINGLE_DECL(soc_enum_envdetcpen,
	REG_SIGENVCONF, REG_SIGENVCONF_ENVDETCPEN, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_envdeththre,
	REG_ENVCPCONF, REG_ENVCPCONF_ENVDETHTHRE, enum_envdetthre);
static SOC_ENUM_SINGLE_DECL(soc_enum_envdetlthre,
	REG_ENVCPCONF, REG_ENVCPCONF_ENVDETLTHRE, enum_envdetthre);

static const char * const enum_envdettime[] = {
	"26.6us", "53.2us", "106us",  "213us",
	"426us",  "851us",  "1.70ms", "3.40ms",
	"6.81ms", "13.6ms", "27.2ms", "54.5ms",
	"109ms",  "218ms",  "436ms",  "872ms" };
static SOC_ENUM_SINGLE_DECL(soc_enum_envdettime,
	REG_SIGENVCONF, REG_SIGENVCONF_ENVDETTIME, enum_envdettime);

static const char * const enum_ensemicx[] = {"Differential", "Single Ended"};
static SOC_ENUM_SINGLE_DECL(soc_enum_ensemic1,
	REG_ANAGAIN1, REG_ANAGAINX_ENSEMICX, enum_ensemicx);
static SOC_ENUM_SINGLE_DECL(soc_enum_ensemic2,
	REG_ANAGAIN2, REG_ANAGAINX_ENSEMICX, enum_ensemicx);
static SOC_ENUM_SINGLE_DECL(soc_enum_lowpowmic1,
	REG_ANAGAIN1, REG_ANAGAINX_LOWPOWMICX, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_lowpowmic2,
	REG_ANAGAIN2, REG_ANAGAINX_LOWPOWMICX, enum_dis_ena);

static SOC_ENUM_DOUBLE_DECL(soc_enum_ad12nh, REG_ADFILTCONF,
	REG_ADFILTCONF_AD1NH, REG_ADFILTCONF_AD2NH, enum_ena_dis);
static SOC_ENUM_DOUBLE_DECL(soc_enum_ad34nh, REG_ADFILTCONF,
	REG_ADFILTCONF_AD3NH, REG_ADFILTCONF_AD4NH, enum_ena_dis);

static const char * const enum_av_mode[] = {"Audio", "Voice"};
static SOC_ENUM_DOUBLE_DECL(soc_enum_ad12voice, REG_ADFILTCONF,
	REG_ADFILTCONF_AD1VOICE, REG_ADFILTCONF_AD2VOICE, enum_av_mode);
static SOC_ENUM_DOUBLE_DECL(soc_enum_ad34voice, REG_ADFILTCONF,
	REG_ADFILTCONF_AD3VOICE, REG_ADFILTCONF_AD4VOICE, enum_av_mode);

static SOC_ENUM_SINGLE_DECL(soc_enum_da12voice,
	REG_DASLOTCONF1, REG_DASLOTCONF1_DA12VOICE, enum_av_mode);
static SOC_ENUM_SINGLE_DECL(soc_enum_da34voice,
	REG_DASLOTCONF3, REG_DASLOTCONF3_DA34VOICE, enum_av_mode);
static SOC_ENUM_SINGLE_DECL(soc_enum_da56voice,
	REG_DASLOTCONF5, REG_DASLOTCONF5_DA56VOICE, enum_av_mode);

static SOC_ENUM_SINGLE_DECL(soc_enum_swapda12_34,
	REG_DASLOTCONF1, REG_DASLOTCONF1_SWAPDA12_34, enum_dis_ena);

static SOC_ENUM_DOUBLE_DECL(soc_enum_vib12swap, REG_CLASSDCONF1,
	REG_CLASSDCONF1_VIB1SWAPEN, REG_CLASSDCONF1_VIB2SWAPEN, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_hflrswap, REG_CLASSDCONF1,
	REG_CLASSDCONF1_HFLSWAPEN, REG_CLASSDCONF1_HFRSWAPEN, enum_dis_ena);

static SOC_ENUM_DOUBLE_DECL(soc_enum_fir01byp, REG_CLASSDCONF2,
	REG_CLASSDCONF2_FIRBYP0, REG_CLASSDCONF2_FIRBYP1, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_fir23byp, REG_CLASSDCONF2,
	REG_CLASSDCONF2_FIRBYP2, REG_CLASSDCONF2_FIRBYP3, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_highvol01, REG_CLASSDCONF2,
	REG_CLASSDCONF2_HIGHVOLEN0, REG_CLASSDCONF2_HIGHVOLEN1, enum_dis_ena);
static SOC_ENUM_DOUBLE_DECL(soc_enum_highvol23, REG_CLASSDCONF2,
	REG_CLASSDCONF2_HIGHVOLEN2, REG_CLASSDCONF2_HIGHVOLEN3, enum_dis_ena);

static const char * const enum_sinc53[] = {"Sinc 5", "Sinc 3"};
static SOC_ENUM_DOUBLE_DECL(soc_enum_dmic12sinc, REG_DMICFILTCONF,
	REG_DMICFILTCONF_DMIC1SINC3, REG_DMICFILTCONF_DMIC2SINC3, enum_sinc53);
static SOC_ENUM_DOUBLE_DECL(soc_enum_dmic34sinc, REG_DMICFILTCONF,
	REG_DMICFILTCONF_DMIC3SINC3, REG_DMICFILTCONF_DMIC4SINC3, enum_sinc53);
static SOC_ENUM_DOUBLE_DECL(soc_enum_dmic56sinc, REG_DMICFILTCONF,
	REG_DMICFILTCONF_DMIC5SINC3, REG_DMICFILTCONF_DMIC6SINC3, enum_sinc53);

static const char * const enum_da2hslr[] = {"Sidetone", "Audio Path"};
static SOC_ENUM_DOUBLE_DECL(soc_enum_da2hslr, REG_DIGMULTCONF1,
	REG_DIGMULTCONF1_DATOHSLEN, REG_DIGMULTCONF1_DATOHSREN,	enum_da2hslr);

static const char * const enum_sinc31[] = {"Sinc 3", "Sinc 1"};
static SOC_ENUM_SINGLE_DECL(soc_enum_hsesinc,
		REG_HSLEARDIGGAIN, REG_HSLEARDIGGAIN_HSSINC1, enum_sinc31);

static const char * const enum_fadespeed[] = {"1ms", "4ms", "8ms", "16ms"};
static SOC_ENUM_SINGLE_DECL(soc_enum_fadespeed,
	REG_HSRDIGGAIN, REG_HSRDIGGAIN_FADESPEED, enum_fadespeed);

/* Digital interface - Clocks */
static SOC_ENUM_SINGLE_DECL(soc_enum_mastgen,
	REG_DIGIFCONF1, REG_DIGIFCONF1_ENMASTGEN, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_fsbitclk0,
	REG_DIGIFCONF1, REG_DIGIFCONF1_ENFSBITCLK0, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_fsbitclk1,
	REG_DIGIFCONF1, REG_DIGIFCONF1_ENFSBITCLK1, enum_dis_ena);

/* Digital interface - DA from slot mapping */
static const char * const enum_da_from_slot_map[] = {"SLOT0",
					"SLOT1",
					"SLOT2",
					"SLOT3",
					"SLOT4",
					"SLOT5",
					"SLOT6",
					"SLOT7",
					"SLOT8",
					"SLOT9",
					"SLOT10",
					"SLOT11",
					"SLOT12",
					"SLOT13",
					"SLOT14",
					"SLOT15",
					"SLOT16",
					"SLOT17",
					"SLOT18",
					"SLOT19",
					"SLOT20",
					"SLOT21",
					"SLOT22",
					"SLOT23",
					"SLOT24",
					"SLOT25",
					"SLOT26",
					"SLOT27",
					"SLOT28",
					"SLOT29",
					"SLOT30",
					"SLOT31"};
static SOC_ENUM_SINGLE_DECL(soc_enum_da1slotmap,
	REG_DASLOTCONF1, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da2slotmap,
	REG_DASLOTCONF2, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da3slotmap,
	REG_DASLOTCONF3, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da4slotmap,
	REG_DASLOTCONF4, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da5slotmap,
	REG_DASLOTCONF5, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da6slotmap,
	REG_DASLOTCONF6, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da7slotmap,
	REG_DASLOTCONF7, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_da8slotmap,
	REG_DASLOTCONF8, REG_DASLOTCONFX_SLTODAX_SHIFT, enum_da_from_slot_map);

/* Digital interface - AD to slot mapping */
static const char * const enum_ad_to_slot_map[] = {"AD_OUT1",
					"AD_OUT2",
					"AD_OUT3",
					"AD_OUT4",
					"AD_OUT5",
					"AD_OUT6",
					"AD_OUT7",
					"AD_OUT8",
					"zeroes",
					"tristate"};
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot0map,
	REG_ADSLOTSEL1, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot1map,
	REG_ADSLOTSEL1, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot2map,
	REG_ADSLOTSEL2, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot3map,
	REG_ADSLOTSEL2, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot4map,
	REG_ADSLOTSEL3, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot5map,
	REG_ADSLOTSEL3, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot6map,
	REG_ADSLOTSEL4, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot7map,
	REG_ADSLOTSEL4, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot8map,
	REG_ADSLOTSEL5, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot9map,
	REG_ADSLOTSEL5, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot10map,
	REG_ADSLOTSEL6, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot11map,
	REG_ADSLOTSEL6, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot12map,
	REG_ADSLOTSEL7, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot13map,
	REG_ADSLOTSEL7, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot14map,
	REG_ADSLOTSEL8, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot15map,
	REG_ADSLOTSEL8, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot16map,
	REG_ADSLOTSEL9, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot17map,
	REG_ADSLOTSEL9, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot18map,
	REG_ADSLOTSEL10, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot19map,
	REG_ADSLOTSEL10, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot20map,
	REG_ADSLOTSEL11, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot21map,
	REG_ADSLOTSEL11, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot22map,
	REG_ADSLOTSEL12, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot23map,
	REG_ADSLOTSEL12, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot24map,
	REG_ADSLOTSEL13, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot25map,
	REG_ADSLOTSEL13, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot26map,
	REG_ADSLOTSEL14, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot27map,
	REG_ADSLOTSEL14, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot28map,
	REG_ADSLOTSEL15, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot29map,
	REG_ADSLOTSEL15, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot30map,
	REG_ADSLOTSEL16, REG_ADSLOTSELX_EVEN_SHIFT, enum_ad_to_slot_map);
static SOC_ENUM_SINGLE_DECL(soc_enum_adslot31map,
	REG_ADSLOTSEL16, REG_ADSLOTSELX_ODD_SHIFT, enum_ad_to_slot_map);

/* Digital interface - Burst mode */
static SOC_ENUM_SINGLE_EXT_DECL(soc_enum_if0fifoen, enum_dis_ena);
static const char * const enum_mask[] = {"Unmasked", "Masked"};
static SOC_ENUM_SINGLE_DECL(soc_enum_bfifomask,
	REG_FIFOCONF1, REG_FIFOCONF1_BFIFOMASK, enum_mask);
static const char * const enum_bitclk0[] = {"38_4_MHz", "19_2_MHz"};
static SOC_ENUM_SINGLE_DECL(soc_enum_bfifo19m2,
	REG_FIFOCONF1, REG_FIFOCONF1_BFIFO19M2, enum_bitclk0);
static const char * const enum_slavemaster[] = {"Slave", "Master"};
static SOC_ENUM_SINGLE_DECL(soc_enum_bfifomast,
	REG_FIFOCONF3, REG_FIFOCONF3_BFIFOMAST_SHIFT, enum_slavemaster);
static SOC_ENUM_SINGLE_DECL(soc_enum_bfifoint,
	REG_FIFOCONF3, REG_FIFOCONF3_BFIFORUN_SHIFT, enum_dis_ena);
static SOC_ENUM_SINGLE_EXT_DECL(soc_enum_digimute, enum_dis_ena);
static const char * const enum_dmicfreq[] = {"2.4MHz", "3.84MHz", "4.8MHz"};
static SOC_ENUM_SINGLE_DECL(soc_enum_dmic12freq,
		REG_DMICFREQ, REG_DMICFREQ_MIC12FREQ, enum_dmicfreq);
static SOC_ENUM_SINGLE_DECL(soc_enum_dmic34freq,
		REG_DMICFREQ, REG_DMICFREQ_MIC34FREQ, enum_dmicfreq);
static SOC_ENUM_SINGLE_DECL(soc_enum_dmic56freq,
		REG_DMICFREQ, REG_DMICFREQ_MIC56FREQ, enum_dmicfreq);

/* TODO: move to DAPM */
static SOC_ENUM_SINGLE_DECL(soc_enum_enfirsids,
	REG_SIDFIRCONF, REG_SIDFIRCONF_ENFIRSIDS, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_parlhf,
	REG_CLASSDCONF1, REG_CLASSDCONF1_PARLHF, enum_dis_ena);
static SOC_ENUM_SINGLE_DECL(soc_enum_parlvib,
	REG_CLASSDCONF1, REG_CLASSDCONF1_PARLVIB, enum_dis_ena);
static SOC_ENUM_SINGLE_EXT_DECL(soc_enum_applysidetone, enum_rdy_apl);

static const char * const enum_vib_mode[] = {"N/P Polarity", "Duty Cycle"};
static SOC_ENUM_SINGLE_DECL(soc_enum_vib1_mode,
	REG_PWMGENCONF1, REG_PWMGENCONF1_PWM1CTRL, enum_vib_mode);
static SOC_ENUM_SINGLE_DECL(soc_enum_vib2_mode,
	REG_PWMGENCONF1, REG_PWMGENCONF1_PWM2CTRL, enum_vib_mode);

static const char * const enum_vib_pol[] = {"GndVib", "VinVib"};
static SOC_ENUM_SINGLE_DECL(soc_enum_vib1npol,
	REG_PWMGENCONF2, REG_PWMGENCONFX_PWMVIBXPOL, enum_vib_pol);
static SOC_ENUM_SINGLE_DECL(soc_enum_vib1ppol,
	REG_PWMGENCONF3, REG_PWMGENCONFX_PWMVIBXPOL, enum_vib_pol);
static SOC_ENUM_SINGLE_DECL(soc_enum_vib2npol,
	REG_PWMGENCONF4, REG_PWMGENCONFX_PWMVIBXPOL, enum_vib_pol);
static SOC_ENUM_SINGLE_DECL(soc_enum_vib2ppol,
	REG_PWMGENCONF5, REG_PWMGENCONFX_PWMVIBXPOL, enum_vib_pol);

static const char * const enum_epwmfreq[] = {
	"20kHz", "24kHz", "30kHz", "48kHz",
	"60kHz", "64kHz", "80kHz", "100kHz",
	"120kHz", "150kHz", "192kHz", "256kHz",
	"480kHz", "600kHz", "768kHz", "960kHz" };
static SOC_ENUM_SINGLE_DECL(soc_enum_epwm1freq,
		REG_EPWM1CONF, REG_EPWM1CONF_FREQ, enum_epwmfreq);
static SOC_ENUM_SINGLE_DECL(soc_enum_epwm2freq,
		REG_EPWM2CONF, REG_EPWM2CONF_FREQ, enum_epwmfreq);

static const char * const enum_epwmedge[] = {"Sawtooth", "Triangular"};
static SOC_ENUM_SINGLE_DECL(soc_enum_epwm1edge,
		REG_EPWM1CONF, REG_EPWM1CONF_EDGE, enum_epwmedge);
static SOC_ENUM_SINGLE_DECL(soc_enum_epwm2edge,
		REG_EPWM2CONF, REG_EPWM2CONF_EDGE, enum_epwmedge);

static SOC_ENUM_SINGLE_DECL(soc_enum_adc3lpswitch,
		REG_ADPATHENA, REG_ADPATHENA_LPADC3, enum_dis_ena);

static const char * const enum_lineincfg[] = {"Differential", "Single Ended"};
static SOC_ENUM_SINGLE_DECL(soc_enum_lineincfg,
		REG_ADPATHENA, REG_ADPATHENA_SELIN, enum_lineincfg);

static const char * const enum_dacmode[] = {"Normal", "Low High Freq Noise Mode"};
static SOC_ENUM_SINGLE_DECL(soc_enum_dacmode,
		REG_CIDEMICTRL, REG_CIDEMICTRL_DALOWHFNOI, enum_dacmode);

static const char * const enum_pwmacmode[] = {"DC", "AC"};
static SOC_ENUM_SINGLE_DECL(soc_enum_pwmacmode,
		REG_CIDEMICTRL, REG_CIDEMICTRL_PWMACMODE, enum_pwmacmode);

static const char * const enum_ad12lb[] = {"Stereo", "Mono"};
static SOC_ENUM_SINGLE_DECL(soc_enum_ad12lb,
		REG_DMICFREQ, REG_DMICFREQ_AD12LBMONO, enum_ad12lb);

static const char * const enum_ad78[] = {"Stereo", "Mono"};
static SOC_ENUM_SINGLE_DECL(soc_enum_ad78,
		REG_DMICFREQ, REG_MIXCTRL_DA78MONO, enum_ad78);

static const char * const enum_epwm2gpiosel[] = {"PdmClk", "PWM2"};
static SOC_ENUM_SINGLE_DECL(soc_enum_epwm2gpiosel,
		REG_EPWM2CONF, REG_EPWM2CONF_GPIOSEL, enum_epwm2gpiosel);

static const char * const enum_epwmsprwin[] = {
	"376kHz", "376kHz to 400kHz", "376kHz to 427kHz",
	"376kHz to 457kHz", "376kHz to 492kHz", "376kHz to 533kHz"};
static SOC_ENUM_SINGLE_DECL(soc_enum_epwmsprwin, REG_CIDEMICTRL,
		REG_CIDEMICTRL_MAX, enum_epwmsprwin);

static struct snd_kcontrol_new ab850x_snd_controls[] = {
	SOC_ENUM("Headset High Pass Playback Switch", soc_enum_hshpen),
	SOC_ENUM("Headset Low Power Playback Switch", soc_enum_hslowpow),
	SOC_ENUM("Headset DAC Low Power Playback Switch", soc_enum_daclowpow1),
	SOC_ENUM("Headset DAC Drv Low Power Playback Switch",
		soc_enum_daclowpow0),
	SOC_ENUM("Earpiece High Pass Playback Switch", soc_enum_enhpear),
	SOC_ENUM("Earpiece DAC Low Power Playback Switch",
		soc_enum_eardaclowpow),
	SOC_ENUM("Earpiece DAC Drv Low Power Playback Switch",
		soc_enum_eardrvlowpow),
	SOC_ENUM("Earpiece Common Mode Playback Switch", soc_enum_earselcm),

	SOC_ENUM("Headset Fade Speed Playback Switch", soc_enum_hsfadspeed),

	SOC_ENUM("Charge Pump High Threshold For Low Voltage",
		soc_enum_envdeththre),
	SOC_ENUM("Charge Pump Low Threshold For Low Voltage",
		soc_enum_envdetlthre),
	SOC_ENUM("Charge Pump Envelope Detection", soc_enum_envdetcpen),
	SOC_ENUM("Charge Pump Envelope Detection Decay Time",
		soc_enum_envdettime),

	SOC_ENUM("Mic 1 Type Capture Switch", soc_enum_ensemic1),
	SOC_ENUM("Mic 2 Type Capture Switch", soc_enum_ensemic2),
	SOC_ENUM("Mic 1 Low Power Capture Switch", soc_enum_lowpowmic1),
	SOC_ENUM("Mic 2 Low Power Capture Switch", soc_enum_lowpowmic2),

	SOC_ENUM("LineIn High Pass Capture Switch", soc_enum_ad12nh),
	SOC_ENUM("Mic High Pass Capture Switch", soc_enum_ad34nh),
	SOC_ENUM("LineIn Mode Capture Switch", soc_enum_ad12voice),
	SOC_ENUM("Mic Mode Capture Switch", soc_enum_ad34voice),

	SOC_ENUM("Headset Mode Playback Switch", soc_enum_da12voice),
	SOC_ENUM("IHF Mode Playback Switch", soc_enum_da34voice),
	SOC_ENUM("Vibra Mode Playback Switch", soc_enum_da56voice),

	SOC_ENUM("IHF and Headset Swap Playback Switch", soc_enum_swapda12_34),

	SOC_ENUM("Vibra 1 PWM Mode", soc_enum_vib1_mode),
	SOC_ENUM("Vibra 1 PWM Polarity N", soc_enum_vib1npol),
	SOC_ENUM("Vibra 1 PWM Polarity P", soc_enum_vib1ppol),

	SOC_ENUM("LineIn Filter Capture Switch", soc_enum_dmic12sinc),
	SOC_ENUM("Mic Filter Capture Switch", soc_enum_dmic34sinc),
	SOC_ENUM("HD Mic Filter Capture Switch", soc_enum_dmic56sinc),

	SOC_ENUM("Headset Source Playback Route", soc_enum_da2hslr),

	/* TODO: Cannot be changed on the fly with digital channel enabled. */
	SOC_ENUM("Headset Filter Playback Switch", soc_enum_hsesinc),

	SOC_ENUM("Digital Gain Fade Speed Switch", soc_enum_fadespeed),

	/* TODO: move to DAPM */
	SOC_ENUM("Sidetone Playback Switch", soc_enum_enfirsids),

	/* Digital gains for AD side */

	SOC_DOUBLE_R_TLV("LineIn Master Gain Capture Volume",
		REG_ADDIGGAIN1, REG_ADDIGGAIN2,
		0, REG_ADDIGGAINX_ADXGAIN_MAX, INVERT, adx_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Mic Master Gain Capture Volume",
		REG_ADDIGGAIN3, REG_ADDIGGAIN4,
		0, REG_ADDIGGAINX_ADXGAIN_MAX, INVERT, adx_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("HD Mic Master Gain Capture Volume",
		REG_ADDIGGAIN5, REG_ADDIGGAIN6,
		0, REG_ADDIGGAINX_ADXGAIN_MAX, INVERT, adx_dig_gain_tlv),

	/* Digital gains for DA side */

	SOC_DOUBLE_R_TLV("Headset Master Gain Playback Volume",
		REG_DADIGGAIN1, REG_DADIGGAIN2,
		0, REG_DADIGGAINX_DAXGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("IHF Master Gain Playback Volume",
		REG_DADIGGAIN3, REG_DADIGGAIN4,
		0, REG_DADIGGAINX_DAXGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Vibra Master Gain Playback Volume",
		REG_DADIGGAIN5, REG_DADIGGAIN6,
		0, REG_DADIGGAINX_DAXGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Analog Loopback Gain Playback Volume",
		REG_ADDIGLOOPGAIN1, REG_ADDIGLOOPGAIN2,
		0, REG_ADDIGLOOPGAINX_ADXLBGAIN_MAX, INVERT, dax_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Headset Digital Gain Playback Volume",
		REG_HSLEARDIGGAIN, REG_HSRDIGGAIN,
		0, REG_HSLEARDIGGAIN_HSLDGAIN_MAX, INVERT, hs_ear_dig_gain_tlv),
	SOC_DOUBLE_R_TLV("Sidetone Digital Gain Playback Volume",
		REG_SIDFIRGAIN1, REG_SIDFIRGAIN2,
		0, REG_SIDFIRGAINX_FIRSIDXGAIN_MAX, INVERT, stfir_dig_gain_tlv),

	/* Analog gains */

	SOC_SINGLE_TLV("Mic 1 Capture Volume",
		REG_ANAGAIN1,
		REG_ANAGAINX_MICXGAIN,
		REG_ANAGAINX_MICXGAIN_MAX, NORMAL, mic_gain_tlv),
	SOC_SINGLE_TLV("Mic 2 Capture Volume",
		REG_ANAGAIN2,
		REG_ANAGAINX_MICXGAIN,
		REG_ANAGAINX_MICXGAIN_MAX, NORMAL, mic_gain_tlv),
	SOC_DOUBLE_TLV("LineIn Capture Volume",
		REG_ANAGAIN4,
		REG_ANAGAIN4_LINLGAIN, REG_ANAGAIN4_LINRGAIN,
		REG_ANAGAIN4_LINXGAIN_MAX, NORMAL, lin_gain_tlv),
	SOC_DOUBLE_R_TLV("LineIn to Headset Bypass Playback Volume",
		REG_DIGLINHSLGAIN, REG_DIGLINHSRGAIN,
		REG_DIGLINHSXGAIN_LINTOHSXGAIN,
		REG_DIGLINHSXGAIN_LINTOHSXGAIN_MAX, INVERT, lin2hs_gain_tlv),

	/* Digital interface - Clocks */
	SOC_ENUM("Digital Interface Master Generator Switch", soc_enum_mastgen),
	SOC_ENUM("Digital Interface 0 Bit-clock Switch", soc_enum_fsbitclk0),
	SOC_ENUM("Digital Interface 1 Bit-clock Switch", soc_enum_fsbitclk1),

	/* Digital interface - DA from slot mapping */
	SOC_ENUM("Digital Interface DA 1 From Slot Map", soc_enum_da1slotmap),
	SOC_ENUM("Digital Interface DA 2 From Slot Map", soc_enum_da2slotmap),
	SOC_ENUM("Digital Interface DA 3 From Slot Map", soc_enum_da3slotmap),
	SOC_ENUM("Digital Interface DA 4 From Slot Map", soc_enum_da4slotmap),
	SOC_ENUM("Digital Interface DA 5 From Slot Map", soc_enum_da5slotmap),
	SOC_ENUM("Digital Interface DA 6 From Slot Map", soc_enum_da6slotmap),
	SOC_ENUM("Digital Interface DA 7 From Slot Map", soc_enum_da7slotmap),
	SOC_ENUM("Digital Interface DA 8 From Slot Map", soc_enum_da8slotmap),

	/* Digital interface - AD to slot mapping */
	SOC_ENUM("Digital Interface AD To Slot 0 Map", soc_enum_adslot0map),
	SOC_ENUM("Digital Interface AD To Slot 1 Map", soc_enum_adslot1map),
	SOC_ENUM("Digital Interface AD To Slot 2 Map", soc_enum_adslot2map),
	SOC_ENUM("Digital Interface AD To Slot 3 Map", soc_enum_adslot3map),
	SOC_ENUM("Digital Interface AD To Slot 4 Map", soc_enum_adslot4map),
	SOC_ENUM("Digital Interface AD To Slot 5 Map", soc_enum_adslot5map),
	SOC_ENUM("Digital Interface AD To Slot 6 Map", soc_enum_adslot6map),
	SOC_ENUM("Digital Interface AD To Slot 7 Map", soc_enum_adslot7map),
	SOC_ENUM("Digital Interface AD To Slot 8 Map", soc_enum_adslot8map),
	SOC_ENUM("Digital Interface AD To Slot 9 Map", soc_enum_adslot9map),
	SOC_ENUM("Digital Interface AD To Slot 10 Map", soc_enum_adslot10map),
	SOC_ENUM("Digital Interface AD To Slot 11 Map", soc_enum_adslot11map),
	SOC_ENUM("Digital Interface AD To Slot 12 Map", soc_enum_adslot12map),
	SOC_ENUM("Digital Interface AD To Slot 13 Map", soc_enum_adslot13map),
	SOC_ENUM("Digital Interface AD To Slot 14 Map", soc_enum_adslot14map),
	SOC_ENUM("Digital Interface AD To Slot 15 Map", soc_enum_adslot15map),
	SOC_ENUM("Digital Interface AD To Slot 16 Map", soc_enum_adslot16map),
	SOC_ENUM("Digital Interface AD To Slot 17 Map", soc_enum_adslot17map),
	SOC_ENUM("Digital Interface AD To Slot 18 Map", soc_enum_adslot18map),
	SOC_ENUM("Digital Interface AD To Slot 19 Map", soc_enum_adslot19map),
	SOC_ENUM("Digital Interface AD To Slot 20 Map", soc_enum_adslot20map),
	SOC_ENUM("Digital Interface AD To Slot 21 Map", soc_enum_adslot21map),
	SOC_ENUM("Digital Interface AD To Slot 22 Map", soc_enum_adslot22map),
	SOC_ENUM("Digital Interface AD To Slot 23 Map", soc_enum_adslot23map),
	SOC_ENUM("Digital Interface AD To Slot 24 Map", soc_enum_adslot24map),
	SOC_ENUM("Digital Interface AD To Slot 25 Map", soc_enum_adslot25map),
	SOC_ENUM("Digital Interface AD To Slot 26 Map", soc_enum_adslot26map),
	SOC_ENUM("Digital Interface AD To Slot 27 Map", soc_enum_adslot27map),
	SOC_ENUM("Digital Interface AD To Slot 28 Map", soc_enum_adslot28map),
	SOC_ENUM("Digital Interface AD To Slot 29 Map", soc_enum_adslot29map),
	SOC_ENUM("Digital Interface AD To Slot 30 Map", soc_enum_adslot30map),
	SOC_ENUM("Digital Interface AD To Slot 31 Map", soc_enum_adslot31map),

	/* Digital interface - Burst FIFO */
	SOC_ENUM_EXT("Digital Interface 0 FIFO Enable Switch",
		soc_enum_if0fifoen,
		if0_fifo_enable_control_get,
		if0_fifo_enable_control_put),
	SOC_ENUM("Burst FIFO Mask", soc_enum_bfifomask),
	SOC_ENUM("Burst FIFO Bit-clock Frequency", soc_enum_bfifo19m2),
	SOC_SINGLE("Burst FIFO Threshold",
		REG_FIFOCONF1,
		REG_FIFOCONF1_BFIFOINT_SHIFT,
		REG_FIFOCONF1_BFIFOINT_MAX,
		NORMAL),
	SOC_SINGLE("Burst FIFO Length",
		REG_FIFOCONF2,
		REG_FIFOCONF2_BFIFOTX_SHIFT,
		REG_FIFOCONF2_BFIFOTX_MAX,
		NORMAL),
	SOC_SINGLE("Burst FIFO EOS Extra Slots",
		REG_FIFOCONF3,
		REG_FIFOCONF3_BFIFOEXSL_SHIFT,
		REG_FIFOCONF3_BFIFOEXSL_MAX,
		NORMAL),
	SOC_SINGLE("Burst FIFO FS Extra Bit-clocks",
		REG_FIFOCONF3,
		REG_FIFOCONF3_PREBITCLK0_SHIFT,
		REG_FIFOCONF3_PREBITCLK0_MAX,
		NORMAL),
	SOC_ENUM("Burst FIFO Interface Mode", soc_enum_bfifomast),
	SOC_ENUM("Burst FIFO Interface Switch", soc_enum_bfifoint),
	SOC_SINGLE("Burst FIFO Switch Frame Number",
		REG_FIFOCONF4,
		REG_FIFOCONF4_BFIFOFRAMSW_SHIFT,
		REG_FIFOCONF4_BFIFOFRAMSW_MAX,
		NORMAL),
	SOC_SINGLE("Burst FIFO Wake Up Delay",
		REG_FIFOCONF5,
		REG_FIFOCONF5_BFIFOWAKEUP_SHIFT,
		REG_FIFOCONF5_BFIFOWAKEUP_MAX,
		NORMAL),
	SOC_SINGLE("Burst FIFO Samples In FIFO",
		REG_FIFOCONF6,
		REG_FIFOCONF6_BFIFOSAMPLE_SHIFT,
		REG_FIFOCONF6_BFIFOSAMPLE_MAX,
		NORMAL),

	/* ANC */
	SOC_SINGLE_S1R("ANC Warp Delay Shift",
		REG_ANCCONF2,
		REG_ANCCONF2_VALUE_MIN,
		REG_ANCCONF2_VALUE_MAX,
		0),
	SOC_SINGLE_S1R("ANC FIR Output Shift",
		REG_ANCCONF3,
		REG_ANCCONF3_VALUE_MIN,
		REG_ANCCONF3_VALUE_MAX,
		0),
	SOC_SINGLE_S1R("ANC IIR Output Shift",
		REG_ANCCONF4,
		REG_ANCCONF4_VALUE_MIN,
		REG_ANCCONF4_VALUE_MAX,
		0),
	SOC_SINGLE_S2R("ANC Warp Delay",
		REG_ANCCONF9, REG_ANCCONF10,
		REG_ANC_WARP_DELAY_MIN,
		REG_ANC_WARP_DELAY_MAX,
		0),
	SOC_MULTIPLE_SA("ANC FIR Coefficients",
		anc_fir_cache,
		REG_ANC_FIR_COEFF_MIN,
		REG_ANC_FIR_COEFF_MAX,
		0),
	SOC_MULTIPLE_SA("ANC IIR Coefficients",
		anc_iir_cache,
		REG_ANC_IIR_COEFF_MIN,
		REG_ANC_IIR_COEFF_MAX,
		0),

	/* Sidetone */
	SOC_MULTIPLE_SA("Sidetone FIR Coefficients",
		sid_fir_cache,
		REG_SID_FIR_COEFF_MIN,
		REG_SID_FIR_COEFF_MAX,
		0),
	SOC_ENUM_EXT("Sidetone FIR Apply Coefficients",
		soc_enum_applysidetone,
		snd_soc_get_enum_double,
		sid_apply_control_put),
	SOC_ENUM_EXT("Digital Interface Mute", soc_enum_digimute,
		digital_mute_control_get, digital_mute_control_put),
};

static struct snd_kcontrol_new ab8500_snd_controls[] = {
	SOC_ENUM("IHF Low EMI Mode Playback Switch", soc_enum_hflrswap),
	SOC_ENUM("Vibra Low EMI Mode Playback Switch", soc_enum_vib12swap),
	SOC_ENUM("IHF Bridge Playback Route", soc_enum_parlhf),
	SOC_ENUM("Vibra Bridge Playback Route", soc_enum_parlvib),

	SOC_ENUM("IHF FIR Bypass Playback Switch", soc_enum_fir01byp),
	SOC_ENUM("Vibra FIR Bypass Playback Switch", soc_enum_fir23byp),

	/* TODO: Cannot be changed on the fly with digital channel enabled. */
	SOC_ENUM("IHF High Volume Playback Switch", soc_enum_highvol01),
	SOC_ENUM("Vibra High Volume Playback Switch", soc_enum_highvol23),

	SOC_SINGLE("ClassD High Pass Gain Playback Volume",
		REG_CLASSDCONF3, REG_CLASSDCONF3_DITHHPGAIN,
		REG_CLASSDCONF3_DITHHPGAIN_MAX, 0),
	SOC_SINGLE("ClassD White Gain Playback Volume",
		REG_CLASSDCONF3, REG_CLASSDCONF3_DITHWGAIN,
		REG_CLASSDCONF3_DITHWGAIN_MAX, 0),

	SOC_DOUBLE_R("Vibra PWM Duty Cycle N Playback Volume",
		REG_PWMGENCONF3, REG_PWMGENCONF5,
		REG_PWMGENCONFX_PWMVIBXDUTCYC,
		REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX, 0),
	SOC_DOUBLE_R("Vibra PWM Duty Cycle P Playback Volume",
		REG_PWMGENCONF2, REG_PWMGENCONF4,
		REG_PWMGENCONFX_PWMVIBXDUTCYC,
		REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX, 0),

	SOC_ENUM("Vibra 2 PWM Mode", soc_enum_vib2_mode),
	SOC_ENUM("Vibra 2 PWM Polarity N", soc_enum_vib2npol),
	SOC_ENUM("Vibra 2 PWM Polarity P", soc_enum_vib2ppol),

	SOC_DOUBLE_TLV("Headset Gain Playback Volume",
		REG_ANAGAIN3, REG_ANAGAIN3_HSLGAIN, REG_ANAGAIN3_HSRGAIN,
		REG_ANAGAIN3_HSXGAIN_MAX, 1, ab8500_hs_gain_tlv),
};

static struct snd_kcontrol_new ab8505_vx_snd_controls[] = {

	SOC_ENUM("EPWM 1 Modulation Type", soc_enum_epwm1edge),
	SOC_ENUM("EPWM 2 Modulation Type", soc_enum_epwm2edge),
	SOC_ENUM("EPWM 1 Frequency", soc_enum_epwm1freq),
	SOC_ENUM("EPWM 2 Frequency", soc_enum_epwm2freq),
	SOC_ENUM("EPWM 2 GPIO Signal Source", soc_enum_epwm2gpiosel),
	SOC_ENUM("EPWM Spreading Frequency", soc_enum_epwmsprwin),

	SOC_SINGLE("Vibra PWM Duty Cycle P Playback Volume",
		REG_PWMGENCONF3, REG_PWMGENCONFX_PWMVIBXDUTCYC,
		REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX, 0),
	SOC_SINGLE("Vibra PWM Duty Cycle N Playback Volume",
		REG_PWMGENCONF2, REG_PWMGENCONFX_PWMVIBXDUTCYC,
		REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX, 0),

	SOC_DOUBLE_TLV("USB Headset Gain Playback Volume",
		REG_USBHSGAIN, REG_USBHSGAIN_UHSL,
		REG_USBHSGAIN_UHSR, REG_USBHSGAIN_UHSX_MAX, 1, uhs_gain_tlv),

	SOC_SINGLE_TLV("Earpiece Analog Gain Playback Volume",
		REG_EARGAINMICSEL, REG_EARGAINMICSEL_GAIN,
		REG_EARGAINMICSEL_GAIN_MAX, 1, ear_gain_tlv),

	SOC_ENUM("DMic 1 and 2 Frequency", soc_enum_dmic12freq),
	SOC_ENUM("DMic 3 and 4 Frequency", soc_enum_dmic34freq),
	SOC_ENUM("DMic 5 and 6 Frequency", soc_enum_dmic56freq),

	SOC_DOUBLE_TLV("Headset Gain Playback Volume",
		REG_ANAGAIN3, REG_ANAGAIN3_HSLGAIN, REG_ANAGAIN3_HSRGAIN,
		REG_ANAGAIN3_HSXGAIN_MAX, 1, ab8505_hs_gain_tlv),
};

static struct snd_kcontrol_new ab8505_v1_snd_controls[] = {

	SOC_SINGLE_TLV("Vibra Digital Gain Playback Volume",
		REG_VIBGAINCTRL_V1, REG_VIBGAINCTRL_V1_SHIFT,
		REG_VIBGAINCTRL_V1_MAX, 1, vib_dig_gain_v1_tlv),

	SOC_DOUBLE_TLV("IHF Digital Gain Playback Volume", REG_HFGAINCTRL_V1,
		REG_HFGAINCTRL_V1_HFL, REG_HFGAINCTRL_V1_HFR,
		REG_HFGAINCTRL_V1_HFX_MAX, 1, hf_dig_gain_v1_tlv),
};

static struct snd_kcontrol_new ab8505_v2_snd_controls[] = {

	SOC_ENUM("LineIn Configuration", soc_enum_lineincfg),
	SOC_ENUM("ADC3 Low Power Switch", soc_enum_adc3lpswitch),
	SOC_SINGLE("Internal PWM AC Frequence Value", REG_IPWMACFREQ,
		REG_IPWMACFREQ_FREQ, REG_IPWMACFREQ_FREQ_MAX, 0),

	SOC_ENUM("DAC Working Mode", soc_enum_dacmode),
	SOC_ENUM("PWM Generator Mode", soc_enum_pwmacmode),

	SOC_SINGLE("EPWM1 DC A MAXDC Multiplier", REG_EPWM1ACDCA,
		REG_EPWM1ACDCA_ACDCA, REG_EPWM1ACDCA_ACDCA_MAX, 0),

	SOC_SINGLE("EPWM1 DC B MAXDC Multiplier", REG_EPWM1ACDCB,
		REG_EPWM1ACDCB_ACDCB, REG_EPWM1ACDCB_ACDCB_MAX, 0),

	SOC_SINGLE("EPWM1 AC Gen Frequency", REG_EPWM1ACFREQ,
		0, REG_EPWM1ACFREQ_FREQ_MAX, 0),

	SOC_SINGLE("EPWM2 DC A MAXDC Multiplier", REG_EPWM2ACDCA,
		REG_EPWM2ACDCA_ACDCA, REG_EPWM2ACDCA_ACDCA_MAX, 0),

	SOC_SINGLE("EPWM2 DC B MAXDC Multiplier", REG_EPWM2ACDCB,
		REG_EPWM2ACDCB_ACDCB, REG_EPWM2ACDCB_ACDCB_MAX, 0),

	SOC_SINGLE("EPWM2 AC Gen Frequency", REG_EPWM2ACFREQ,
		0, REG_EPWM2ACFREQ_FREQ_MAX, 0),

	SOC_SINGLE_TLV("Vibra Digital Gain Playback Volume",
		REG_VIBGAINCTRL_V2, REG_VIBGAINCTRL_V2_SHIFT,
		REG_VIBGAINCTRL_V2_MAX, 1, vib_dig_gain_v2_tlv),

	SOC_DOUBLE_R_TLV("IHF Digital Gain Playback Volume",
		REG_HFLGAINCTRL_V2, REG_HFRGAINCTRL_V2,
		REG_HFGAINCTRL_V2_SHIFT, REG_HFGAINCTRL_V2_MAX,
		1, hf_dig_gain_v2_tlv),

	SOC_ENUM("AD12 Loopback Mode", soc_enum_ad12lb),
	SOC_ENUM("DA78 Output Mode", soc_enum_ad78),
};

static int ab850x_codec_set_format_if1(struct snd_soc_codec *codec,
		unsigned int fmt)
{
	unsigned int clear_mask, set_mask;

	/* Master or slave */

	clear_mask = BMASK(REG_DIGIFCONF3_IF1MASTER);
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* codec clk & FRM master */
		pr_debug("%s: IF1 Master-mode: ab850x master.\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF3_IF1MASTER);
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* codec clk & FRM slave */
		pr_debug("%s: IF1 Master-mode: ab850x slave.\n", __func__);
		break;
	case SND_SOC_DAIFMT_CBS_CFM: /* codec clk slave & FRM master */
	case SND_SOC_DAIFMT_CBM_CFS: /* codec clk master & frame slave */
		pr_err("%s: ERROR: The device is either a master or a slave.\n",
			__func__);
	default:
		pr_err("%s: ERROR: Unsupported master mask 0x%x\n",
			__func__,
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, REG_DIGIFCONF3,
				BMASK(REG_DIGIFCONF3_IF1MASTER),
				BMASK(REG_DIGIFCONF3_IF1MASTER));

	/* I2S or TDM */

	clear_mask = BMASK(REG_DIGIFCONF4_FSYNC1P) |
			BMASK(REG_DIGIFCONF4_BITCLK1P) |
			BMASK(REG_DIGIFCONF4_IF1FORMAT1) |
			BMASK(REG_DIGIFCONF4_IF1FORMAT0);
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S: /* I2S mode */
		pr_debug("%s: IF1 Protocol: I2S\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF4_IF1FORMAT1);
		break;
	case SND_SOC_DAIFMT_DSP_B: /* L data MSB during FRM LRC */
		pr_debug("%s: IF1 Protocol: DSP B (TDM)\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF4_IF1FORMAT0);
		break;
	default:
		pr_err("%s: ERROR: Unsupported format (0x%x)!\n",
			__func__,
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, REG_DIGIFCONF4, clear_mask, set_mask);

	return 0;
}

static int ab850x_codec_set_word_length_if1(struct snd_soc_codec *codec,
			unsigned int wl)
{
	unsigned int clear_mask, set_mask;

	clear_mask = BMASK(REG_DIGIFCONF4_IF1WL1) | BMASK(REG_DIGIFCONF4_IF1WL0);
	set_mask = 0;

	switch (wl) {
	case 16:
		break;
	case 20:
		set_mask |= BMASK(REG_DIGIFCONF4_IF1WL0);
		break;
	case 24:
		set_mask |= BMASK(REG_DIGIFCONF4_IF1WL1);
		break;
	case 32:
		set_mask |= BMASK(REG_DIGIFCONF2_IF0WL1) |
			BMASK(REG_DIGIFCONF2_IF0WL0);
		break;
	default:
		pr_err("%s: Unsupporter word-length 0x%x\n", __func__, wl);
		return -EINVAL;
	}

	pr_debug("%s: Word-length: %d bits.\n", __func__, wl);
	snd_soc_update_bits(codec, REG_DIGIFCONF4, clear_mask, set_mask);

	return 0;
}

static int ab850x_codec_set_bit_delay_if1(struct snd_soc_codec *codec,
			unsigned int delay)
{
	unsigned int clear_mask, set_mask;

	clear_mask = BMASK(REG_DIGIFCONF4_IF1DEL);
	set_mask = 0;

	switch (delay) {
	case 0:
		break;
	case 1:
		set_mask |= BMASK(REG_DIGIFCONF4_IF1DEL);
		break;
	default:
		pr_err("%s: ERROR: Unsupported bit-delay (0x%x)!\n", __func__, delay);
		return -EINVAL;
	}

	pr_debug("%s: IF1 Bit-delay: %d bits.\n", __func__, delay);
	snd_soc_update_bits(codec, REG_DIGIFCONF4, clear_mask, set_mask);

	return 0;
}

/* Configures audio macrocell into the AB8500 Chip */
static void ab850x_codec_configure_audio_macrocell(struct snd_soc_codec *codec)
{
	int data, ret;

	ret = ab8500_sysctrl_write(AB8500_STW4500CTRL3,
		AB8500_STW4500CTRL3_CLK32KOUT2DIS | AB8500_STW4500CTRL3_RESETAUDN,
		AB8500_STW4500CTRL3_RESETAUDN);
	if (ret < 0)
		pr_err("%s: WARN: Unable to set reg STW4500CTRL3!\n", __func__);

	data = ab850x_codec_read_reg(codec, AB8500_MISC, AB8500_GPIO_DIR4_REG);
	data |= GPIO27_DIR_OUTPUT | GPIO29_DIR_OUTPUT | GPIO31_DIR_OUTPUT;
	ab850x_codec_write_reg(codec, AB8500_MISC, AB8500_GPIO_DIR4_REG, data);
}

/* Extended interface for codec-driver */

int ab850x_audio_power_control(bool power_on)
{
	int pwr_mask = BMASK(REG_POWERUP_POWERUP) | BMASK(REG_POWERUP_ENANA);

	if (ab850x_codec == NULL) {
		pr_err("%s: ERROR: ab850x ASoC-driver not yet probed!\n",
			__func__);
		return -EIO;
	}

	pr_debug("%s ab850x.", (power_on) ? "Enabling" : "Disabling");

	return snd_soc_update_bits(ab850x_codec, REG_POWERUP,
		pwr_mask, (power_on) ? pwr_mask : REG_MASK_NONE);
}

int ab850x_audio_pwm_vibra(unsigned char pdutcyc1, unsigned char ndutcyc1,
		unsigned char pdutcyc2, unsigned char ndutcyc2)
{
	enum ab850x_audio_chipid chipid;
	unsigned int pwm_cfg, pwm_en;
	bool vibra_on;

	if (ab850x_codec == NULL) {
		pr_err("%s: ERROR: ab850x ASoC-driver not yet probed!\n",
			__func__);
		return -EIO;
	}

	chipid = ab850x_audio_get_chipid(ab850x_codec->dev);
	if (chipid == AB850X_AUDIO_UNKNOWN) {
		pr_err("%s: ERROR: ab850x chipset not yet known!\n", __func__);
		return -EIO;
	}

	vibra_on = pdutcyc1 | ndutcyc1;

	pwm_cfg = BIT(REG_PWMGENCONF1_PWMTOVIB1) |
		BIT(REG_PWMGENCONF1_PWM1CTRL) |
		BIT(REG_PWMGENCONF1_PWM1NCTRL) |
		BIT(REG_PWMGENCONF1_PWM1PCTRL);

	if (pdutcyc1 > REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX)
		pdutcyc1 = REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX;
	if (ndutcyc1 > REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX)
		ndutcyc1 = REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX;

	pwm_en = BIT(REG_ANACONF4_ENVIB1);

	if (chipid == AB850X_AUDIO_AB8500) {
		pr_debug("%s: PWM-vibra (%d, %d, %d, %d).\n", __func__,
			pdutcyc1, ndutcyc1, pdutcyc2, ndutcyc2);

		vibra_on |= pdutcyc2 | ndutcyc2;

		pwm_cfg |= BIT(REG_PWMGENCONF1_PWMTOVIB2) |
			BIT(REG_PWMGENCONF1_PWM2CTRL) |
			BIT(REG_PWMGENCONF1_PWM2NCTRL) |
			BIT(REG_PWMGENCONF1_PWM2PCTRL);

		if (pdutcyc2 > REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX)
			pdutcyc2 = REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX;
		if (ndutcyc2 > REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX)
			ndutcyc2 = REG_PWMGENCONFX_PWMVIBXDUTCYC_MAX;

		pwm_en |= BIT(REG_ANACONF4_ENVIB2);
	} else {
		pr_debug("%s: PWM-vibra (%d, %d).\n", __func__,
			pdutcyc1, ndutcyc1);
	}

	snd_soc_update_bits(ab850x_codec, REG_PWMGENCONF1, pwm_cfg, pwm_cfg);
	snd_soc_update_bits(ab850x_codec, REG_PWMGENCONF3, 0xff, pdutcyc1);
	snd_soc_update_bits(ab850x_codec, REG_PWMGENCONF2, 0xff, ndutcyc1);

	if (chipid == AB850X_AUDIO_AB8500) {
		snd_soc_update_bits(ab850x_codec, REG_PWMGENCONF5,
				0xff, pdutcyc2);
		snd_soc_update_bits(ab850x_codec, REG_PWMGENCONF4,
				0xff, ndutcyc2);
	}

	snd_soc_update_bits(ab850x_codec, REG_ANACONF4, pwm_en,
			(vibra_on) ? pwm_en : 0);

	return 0;
}

int ab8505_audio_pwm_output(bool pwm1_mod, unsigned char pwm1_freq,
		bool pwm2_mod, unsigned char pwm2_freq)
{
	enum ab850x_audio_chipid chipid;
	unsigned int mask, value;

	if (ab850x_codec == NULL) {
		pr_err("%s: ERROR: ab850x ASoC-driver not yet probed!\n",
			__func__);
		return -EIO;
	}

	chipid = ab850x_audio_get_chipid(ab850x_codec->dev);
	if (chipid == AB850X_AUDIO_UNKNOWN) {
		pr_err("%s: ERROR: ab850x chipset not yet known!\n", __func__);
		return -EIO;
	} else if (chipid != AB850X_AUDIO_AB8505_V1 &&
			chipid != AB850X_AUDIO_AB8505_V2 &&
			chipid != AB850X_AUDIO_AB8505_V3) {
		pr_err("%s: ERROR: Functionality only supported by ab8505 v1/v2/v3!\n",
			__func__);
		return -EPERM;
	}

	pr_err("%s: PWM-output (%d, %d, %d, %d).\n", __func__,
			pwm1_mod, pwm1_freq, pwm2_mod, pwm2_freq);

	mask = BIT(REG_EPWM1CONF_EDGE) | REG_EPWM1CONF_FREQ_MAX;
	value = (pwm1_mod) ? BIT(REG_EPWM1CONF_EDGE) | pwm1_freq : pwm1_freq;
	snd_soc_update_bits_locked(ab850x_codec, REG_EPWM1CONF,
			mask, value & mask);

	mask = BIT(REG_EPWM2CONF_EDGE) | REG_EPWM2CONF_FREQ_MAX;
	value = (pwm2_mod) ? BIT(REG_EPWM2CONF_EDGE) | pwm2_freq : pwm2_freq;
	snd_soc_update_bits_locked(ab850x_codec, REG_EPWM2CONF,
			mask, value & mask);

	return 0;
}

int ab850x_audio_set_word_length(struct snd_soc_dai *dai, unsigned int wl)
{
	unsigned int clear_mask, set_mask;
	struct snd_soc_codec *codec = dai->codec;

	clear_mask = BMASK(REG_DIGIFCONF2_IF0WL0) | BMASK(REG_DIGIFCONF2_IF0WL1);
	set_mask = 0;

	switch (wl) {
	case 16:
		break;
	case 20:
		set_mask |= BMASK(REG_DIGIFCONF2_IF0WL0);
		break;
	case 24:
		set_mask |= BMASK(REG_DIGIFCONF2_IF0WL1);
		break;
	case 32:
		set_mask |= BMASK(REG_DIGIFCONF2_IF0WL1) |
			BMASK(REG_DIGIFCONF2_IF0WL0);
		break;
	default:
		pr_err("%s: Unsupported word-length 0x%x\n", __func__, wl);
		return -EINVAL;
	}

	pr_debug("%s: IF0 Word-length: %d bits.\n", __func__, wl);
	snd_soc_update_bits(codec, REG_DIGIFCONF2, clear_mask, set_mask);

	return 0;
}

int ab850x_audio_set_bit_delay(struct snd_soc_dai *dai, unsigned int delay)
{
	unsigned int clear_mask, set_mask;
	struct snd_soc_codec *codec = dai->codec;

	clear_mask = BMASK(REG_DIGIFCONF2_IF0DEL);
	set_mask = 0;

	switch (delay) {
	case 0:
		break;
	case 1:
		set_mask |= BMASK(REG_DIGIFCONF2_IF0DEL);
		break;
	default:
		pr_err("%s: ERROR: Unsupported bit-delay (0x%x)!\n", __func__, delay);
		return -EINVAL;
	}

	pr_debug("%s: IF0 Bit-delay: %d bits.\n", __func__, delay);
	snd_soc_update_bits(codec, REG_DIGIFCONF2, clear_mask, set_mask);

	return 0;
}

int ab850x_audio_setup_if1(struct snd_soc_codec *codec,
			unsigned int fmt,
			unsigned int wl,
			unsigned int delay)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	ret = ab850x_codec_set_format_if1(codec, fmt);
	if (ret)
		return -1;

	ret = ab850x_codec_set_bit_delay_if1(codec, delay);
	if (ret)
		return -1;


	ret = ab850x_codec_set_word_length_if1(codec, wl);
	if (ret)
		return -1;

	return 0;
}

/* ANC FIR-coefficients configuration sequence */
static void ab850x_audio_anc_fir(struct snd_soc_codec *codec,
		unsigned int bank, unsigned int param)
{
	if (param == 0 && bank == 0)
		snd_soc_update_bits(codec, REG_ANCCONF1,
			REG_MASK_NONE, BMASK(REG_ANCCONF1_ANCFIRUPDATE));

	snd_soc_write(codec, REG_ANCCONF5,
		anc_fir_cache[param] >> 8 & REG_MASK_ALL);
	snd_soc_write(codec, REG_ANCCONF6,
		anc_fir_cache[param] & REG_MASK_ALL);

	if (param == REG_ANC_FIR_COEFFS - 1 && bank == 1)
		snd_soc_update_bits(codec, REG_ANCCONF1,
			BMASK(REG_ANCCONF1_ANCFIRUPDATE), REG_MASK_NONE);
}

/* ANC IIR-coefficients configuration sequence */
static void ab850x_audio_anc_iir(struct snd_soc_codec *codec,
		unsigned int bank, unsigned int param)
{
	if (param == 0) {
		if (bank == 0) {
			snd_soc_update_bits(codec, REG_ANCCONF1,
				REG_MASK_NONE, BMASK(REG_ANCCONF1_ANCIIRINIT));
			usleep_range(AB850X_ANC_SM_DELAY, AB850X_ANC_SM_DELAY);
			snd_soc_update_bits(codec, REG_ANCCONF1,
				BMASK(REG_ANCCONF1_ANCIIRINIT), REG_MASK_NONE);
			usleep_range(AB850X_ANC_SM_DELAY, AB850X_ANC_SM_DELAY);
		} else {
			snd_soc_update_bits(codec, REG_ANCCONF1,
				REG_MASK_NONE, BMASK(REG_ANCCONF1_ANCIIRUPDATE));
		}
	} else if (param > 3) {
		snd_soc_write(codec, REG_ANCCONF7, REG_MASK_NONE);
		snd_soc_write(codec, REG_ANCCONF8,
			anc_iir_cache[param] >> 16 & REG_MASK_ALL);
	}

	snd_soc_write(codec, REG_ANCCONF7,
		anc_iir_cache[param] >> 8 & REG_MASK_ALL);
	snd_soc_write(codec, REG_ANCCONF8,
		anc_iir_cache[param] & REG_MASK_ALL);

	if (param == REG_ANC_IIR_COEFFS - 1 && bank == 1)
		snd_soc_update_bits(codec, REG_ANCCONF1,
			BMASK(REG_ANCCONF1_ANCIIRUPDATE), REG_MASK_NONE);
}

/* ANC IIR-/FIR-coefficients configuration sequence */
void ab850x_audio_anc_configure(struct snd_soc_codec *codec,
		bool apply_fir, bool apply_iir)
{
	unsigned int bank, param;

	pr_debug("%s: Enter.\n", __func__);

	if (apply_fir)
		snd_soc_update_bits(codec, REG_ANCCONF1,
			BMASK(REG_ANCCONF1_ENANC), REG_MASK_NONE);

	snd_soc_update_bits(codec, REG_ANCCONF1,
		REG_MASK_NONE, BMASK(REG_ANCCONF1_ENANC));

	if (apply_fir)
		for (bank = 0; bank < AB850X_NR_OF_ANC_COEFF_BANKS; bank++)
			for (param = 0; param < REG_ANC_FIR_COEFFS; param++)
				ab850x_audio_anc_fir(codec, bank, param);

	if (apply_iir)
		for (bank = 0; bank < AB850X_NR_OF_ANC_COEFF_BANKS; bank++)
			for (param = 0; param < REG_ANC_IIR_COEFFS; param++)
				ab850x_audio_anc_iir(codec, bank, param);

	pr_debug("%s: Exit.\n", __func__);
}

int ab850x_audio_set_adcm(enum ab850x_audio_adcm req_adcm)
{
	if (ab850x_codec == NULL) {
		pr_err("%s: ERROR: ab850x ASoC-driver not yet probed!\n",
			__func__);
		return -EIO;
	}

	if (adcm == req_adcm)
		return 0;

	pr_debug("%s: Enter.\n", __func__);

	if (AB850X_AUDIO_ADCM_FORCE_UP == req_adcm ||
			AB850X_AUDIO_ADCM_FORCE_DOWN == req_adcm) {

		mutex_lock(&ab850x_codec->mutex);

		adcm_anaconf5 = snd_soc_read(ab850x_codec, REG_ANACONF5);
		adcm_muteconf = snd_soc_read(ab850x_codec, REG_MUTECONF);
		adcm_anaconf4 = snd_soc_read(ab850x_codec, REG_ANACONF4);

		if (AB850X_AUDIO_ADCM_FORCE_UP == req_adcm) {
			snd_soc_update_bits(ab850x_codec, REG_ANACONF5,
					REG_MASK_NONE, ADCM_ANACONF5_MASK);
			snd_soc_update_bits(ab850x_codec, REG_MUTECONF,
					REG_MASK_NONE, ADCM_MUTECONF_MASK);
			snd_soc_update_bits(ab850x_codec, REG_ANACONF4,
					REG_MASK_NONE, ADCM_ANACONF4_MASK);
		} else {
			snd_soc_update_bits(ab850x_codec, REG_ANACONF5,
					ADCM_ANACONF5_MASK, REG_MASK_NONE);
		}
	} else if (AB850X_AUDIO_ADCM_NORMAL == req_adcm) {
		if (AB850X_AUDIO_ADCM_FORCE_UP == adcm) {
			snd_soc_update_bits(ab850x_codec, REG_ANACONF5,
					~adcm_anaconf5 & ADCM_ANACONF5_MASK,
					adcm_anaconf5 & ADCM_ANACONF5_MASK);
			snd_soc_update_bits(ab850x_codec, REG_MUTECONF,
					~adcm_muteconf & ADCM_MUTECONF_MASK,
					adcm_muteconf & ADCM_MUTECONF_MASK);
			snd_soc_update_bits(ab850x_codec, REG_ANACONF4,
					~adcm_anaconf4 & ADCM_ANACONF4_MASK,
					adcm_anaconf4 & ADCM_ANACONF4_MASK);
		} else {
			snd_soc_update_bits(ab850x_codec, REG_ANACONF5,
					~adcm_anaconf5 & ADCM_ANACONF5_MASK,
					adcm_anaconf5 & ADCM_ANACONF5_MASK);
		}
	}

	adcm = req_adcm;

	if (AB850X_AUDIO_ADCM_NORMAL == adcm)
		mutex_unlock(&ab850x_codec->mutex);

	pr_debug("%s: Exit.\n", __func__);

	return 0;
}

static int ab850x_codec_add_widgets(struct snd_soc_codec *codec)
{
	enum ab850x_audio_chipid chipid;
	int ret;

	pr_debug("%s Enter.\n", __func__);

	ret = snd_soc_dapm_new_controls(&codec->dapm, ab850x_dapm_widgets,
			ARRAY_SIZE(ab850x_dapm_widgets));
	if (ret < 0) {
		pr_err("%s: Failed to create ab850x DAPM widgets (%d).\n",
			__func__, ret);
		return ret;
	}
	chipid = ab850x_audio_get_chipid(codec->dev);
	switch (chipid) {
	case AB850X_AUDIO_AB8500:
		ret = snd_soc_dapm_new_controls(&codec->dapm,
				ab8500_dapm_widgets,
				ARRAY_SIZE(ab8500_dapm_widgets));
		if (ret < 0) {
			pr_err("%s: Failed to create ab8500 DAPM widgets (%d).\n",
				__func__, ret);
			return ret;
		}
		break;
	case AB850X_AUDIO_AB8505_V1:
	case AB850X_AUDIO_AB8505_V2:
	case AB850X_AUDIO_AB8505_V3:
		ret = snd_soc_dapm_new_controls(&codec->dapm,
				ab8505_vx_dapm_widgets,
				ARRAY_SIZE(ab8505_vx_dapm_widgets));
		if (ret < 0) {
			pr_err("%s: Failed to create ab8505 vx DAPM widgets (%d).\n",
				__func__, ret);
			return ret;
		}
		switch (chipid) {
		case AB850X_AUDIO_AB8505_V1:
			ret = snd_soc_dapm_new_controls(&codec->dapm,
					ab8505_v1_dapm_widgets,
					ARRAY_SIZE(ab8505_v1_dapm_widgets));
			if (ret < 0) {
				pr_err("%s: Failed to create ab8505 v1 DAPM widgets (%d).\n",
					__func__, ret);
				return ret;
			}
			break;
		case AB850X_AUDIO_AB8505_V2:
		case AB850X_AUDIO_AB8505_V3:
			ret = snd_soc_dapm_new_controls(&codec->dapm,
					ab8505_v2_dapm_widgets,
					ARRAY_SIZE(ab8505_v2_dapm_widgets));
			if (ret < 0) {
				pr_err("%s: Failed to create ab8505 v2 DAPM widgets (%d).\n",
					__func__, ret);
				return ret;
			}
			break;
		default:
			return -EIO;
		}
		break;
	default:
		return -EIO;
	}

	ret = snd_soc_dapm_add_routes(&codec->dapm, ab850x_dapm_routes,
			ARRAY_SIZE(ab850x_dapm_routes));
	if (ret < 0) {
		pr_err("%s: Failed to add ab850x DAPM routes (%d).\n",
			__func__, ret);
		return ret;
	}
	switch (chipid) {
	case AB850X_AUDIO_AB8500:
		ret = snd_soc_dapm_add_routes(&codec->dapm, ab8500_dapm_routes,
				ARRAY_SIZE(ab8500_dapm_routes));
		if (ret < 0) {
			pr_err("%s: Failed to add ab8500 DAPM routes (%d).\n",
				__func__, ret);
			return ret;
		}
		break;
	case AB850X_AUDIO_AB8505_V1:
	case AB850X_AUDIO_AB8505_V2:
	case AB850X_AUDIO_AB8505_V3:
		ret = snd_soc_dapm_add_routes(&codec->dapm,
				ab8505_vx_dapm_routes,
				ARRAY_SIZE(ab8505_vx_dapm_routes));
		if (ret < 0) {
			pr_err("%s: Failed to add ab8505 vx DAPM routes (%d).\n",
				__func__, ret);
			return ret;
		}
		switch (chipid) {
		case AB850X_AUDIO_AB8505_V1:
			ret = snd_soc_dapm_add_routes(&codec->dapm,
					ab8505_v1_dapm_routes,
					ARRAY_SIZE(ab8505_v1_dapm_routes));
			if (ret < 0) {
				pr_err("%s: Failed to add ab8505 v1 DAPM routes (%d).\n",
					__func__, ret);
				return ret;
			}
			break;
		case AB850X_AUDIO_AB8505_V2:
		case AB850X_AUDIO_AB8505_V3:
			ret = snd_soc_dapm_add_routes(&codec->dapm,
					ab8505_v2_dapm_routes,
					ARRAY_SIZE(ab8505_v2_dapm_routes));
			if (ret < 0) {
				pr_err("%s: Failed to add ab8505 v2 DAPM routes (%d).\n",
					__func__, ret);
				return ret;
			}
			break;
		default:
			return -EIO;
		}
		break;
	default:
		return -EIO;
	}

	snd_soc_dapm_ignore_suspend(&codec->dapm, "LINL");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "LINR");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "DA_IN7_CG2900");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "DA_IN8_CG2900");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "IHFL");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "IHFR");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "HSL");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "HSR");

	return 0;
}

static int ab850x_codec_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params, struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static int ab850x_codec_pcm_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static int ab850x_codec_pcm_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static void ab850x_codec_pcm_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	pr_debug("%s Enter.\n", __func__);

	snd_soc_update_bits(rtd->codec, REG_DIGIFCONF2, REG_MASK_ALL, 0);
}

static int ab850x_codec_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
	pr_err("%s Enter.\n", __func__);

	return 0;
}

/* Gates clocking according format mask */
static int ab850x_codec_set_dai_clock_gate(struct snd_soc_codec *codec,
			unsigned int fmt)
{
	unsigned int clear_mask;
	unsigned int set_mask;

	clear_mask = BMASK(REG_DIGIFCONF1_ENMASTGEN) |
			BMASK(REG_DIGIFCONF1_ENFSBITCLK0);

	set_mask = BMASK(REG_DIGIFCONF1_ENMASTGEN);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_DAIFMT_CONT: /* continuous clock */
		pr_debug("%s: IF0 Clock is continuous.\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF1_ENFSBITCLK0);
		break;
	case SND_SOC_DAIFMT_GATED: /* clock is gated */
		pr_debug("%s: IF0 Clock is gated.\n", __func__);
		break;
	default:
		pr_err("%s: ERROR: Unsupported clock mask (0x%x)!\n",
			__func__,
			fmt & SND_SOC_DAIFMT_CLOCK_MASK);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, REG_DIGIFCONF1, clear_mask, set_mask);

	return 0;
}

static int ab850x_codec_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	unsigned int clear_mask;
	unsigned int set_mask;
	struct snd_soc_codec *codec = dai->codec;
	int err;

	pr_debug("%s: Enter (fmt = 0x%x)\n", __func__, fmt);

	clear_mask = BMASK(REG_DIGIFCONF3_IF1DATOIF0AD) |
			BMASK(REG_DIGIFCONF3_IF1CLKTOIF0CLK) |
			BMASK(REG_DIGIFCONF3_IF0BFIFOEN) |
			BMASK(REG_DIGIFCONF3_IF0MASTER);
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* codec clk & FRM master */
		pr_debug("%s: IF0 Master-mode: AB8500 master.\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF3_IF0MASTER);
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* codec clk & FRM slave */
		pr_debug("%s: IF0 Master-mode: AB8500 slave.\n", __func__);
		break;
	case SND_SOC_DAIFMT_CBS_CFM: /* codec clk slave & FRM master */
	case SND_SOC_DAIFMT_CBM_CFS: /* codec clk master & frame slave */
		pr_err("%s: ERROR: The device is either a master or a slave.\n", __func__);
	default:
		pr_err("%s: ERROR: Unsupporter master mask 0x%x\n",
				__func__,
				(fmt & SND_SOC_DAIFMT_MASTER_MASK));
		return -EINVAL;
		break;
	}

	snd_soc_update_bits(codec, REG_DIGIFCONF3, clear_mask, set_mask);

	/* Set clock gating */
	err = ab850x_codec_set_dai_clock_gate(codec, fmt);
	if (err) {
		pr_err("%s: ERRROR: Failed to set clock gate (%d).\n", __func__, err);
		return err;
	}

	/* Setting data transfer format */

	clear_mask = BMASK(REG_DIGIFCONF2_IF0FORMAT0) |
		BMASK(REG_DIGIFCONF2_IF0FORMAT1) |
		BMASK(REG_DIGIFCONF2_FSYNC0P) |
		BMASK(REG_DIGIFCONF2_BITCLK0P);
	set_mask = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S: /* I2S mode */
		pr_debug("%s: IF0 Protocol: I2S\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF2_IF0FORMAT1);

		/* 32 bit, 0 delay */
		ab850x_audio_set_word_length(dai, 32);
		ab850x_audio_set_bit_delay(dai, 0);

		break;
	case SND_SOC_DAIFMT_DSP_A: /* L data MSB after FRM LRC */
		pr_debug("%s: IF0 Protocol: DSP A (TDM)\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF2_IF0FORMAT0);
		break;
	case SND_SOC_DAIFMT_DSP_B: /* L data MSB during FRM LRC */
		pr_debug("%s: IF0 Protocol: DSP B (TDM)\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF2_IF0FORMAT0);
		break;
	default:
		pr_err("%s: ERROR: Unsupported format (0x%x)!\n",
				__func__,
				fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF: /* normal bit clock + frame */
		pr_debug("%s: IF0: Normal bit clock, normal frame\n", __func__);
		break;
	case SND_SOC_DAIFMT_NB_IF: /* normal BCLK + inv FRM */
		pr_debug("%s: IF0: Normal bit clock, inverted frame\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF2_FSYNC0P);
		break;
	case SND_SOC_DAIFMT_IB_NF: /* invert BCLK + nor FRM */
		pr_debug("%s: IF0: Inverted bit clock, normal frame\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF2_BITCLK0P);
		break;
	case SND_SOC_DAIFMT_IB_IF: /* invert BCLK + FRM */
		pr_debug("%s: IF0: Inverted bit clock, inverted frame\n", __func__);
		set_mask |= BMASK(REG_DIGIFCONF2_FSYNC0P);
		set_mask |= BMASK(REG_DIGIFCONF2_BITCLK0P);
		break;
	default:
		pr_err("%s: ERROR: Unsupported INV mask 0x%x\n",
				__func__,
				(fmt & SND_SOC_DAIFMT_INV_MASK));
		return -EINVAL;
		break;
	}

	snd_soc_update_bits(codec, REG_DIGIFCONF2, clear_mask, set_mask);

	return 0;
}

static int ab850x_codec_set_dai_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int set_mask, mask, slots_active, i;

	if (!(slot_width == 16 || slot_width == 32 || slot_width == 20)) {
		pr_err("%s: ERROR: Unsupported slot_width %d.\n",
			__func__, slot_width);
		return -EINVAL;
	}

	/* Setup TDM bitclock */
	pr_debug("%s: Slots, total: (%d) slot_width: (%d)\n",
		__func__,
		slots,
		slot_width);

	mask = BMASK(REG_DIGIFCONF1_IF0BITCLKOS0) |
			BMASK(REG_DIGIFCONF1_IF0BITCLKOS1);

	i = slots * slot_width;

	if (i > 128)
		set_mask = BMASK(REG_DIGIFCONF1_IF0BITCLKOS0) |
				BMASK(REG_DIGIFCONF1_IF0BITCLKOS1);
	else if (i > 64)
		set_mask = BMASK(REG_DIGIFCONF1_IF0BITCLKOS1);
	else if (i > 32)
		set_mask = BMASK(REG_DIGIFCONF1_IF0BITCLKOS0);
	else
		set_mask = REG_MASK_NONE;

	snd_soc_update_bits(codec, REG_DIGIFCONF1, mask, set_mask);

	mask = REG_DASLOTCONFX_SLTODAX_MASK;
	slots_active = hweight32(tx_mask);
	pr_debug("%s: Slots: (%d), TX: (%d)\n", __func__, slots_active, tx_mask);
	switch (slots_active) {
	case 0:
		break;
	case 1:
		i = find_first_bit(&tx_mask, sizeof(tx_mask));
		snd_soc_update_bits(codec, REG_DASLOTCONF1, mask, slots+i);
		snd_soc_update_bits(codec, REG_DASLOTCONF2, mask, slots+i);
		snd_soc_update_bits(codec, REG_DASLOTCONF3, mask, slots+i);
		snd_soc_update_bits(codec, REG_DASLOTCONF4, mask, slots+i);
		break;
	case 2:
		i = find_first_bit(&tx_mask, sizeof(tx_mask));
		snd_soc_update_bits(codec, REG_DASLOTCONF1, mask, slots+i);
		snd_soc_update_bits(codec, REG_DASLOTCONF3, mask, slots+i);
		i = find_next_bit(&tx_mask, sizeof(tx_mask), i+1);
		snd_soc_update_bits(codec, REG_DASLOTCONF2, mask, slots+i);
		snd_soc_update_bits(codec, REG_DASLOTCONF4, mask, slots+i);
		break;
	case 8:
		pr_debug("%s: In 8-channel mode DA-from-slot mapping is set manually.", __func__);
		break;
	default:
		pr_err("%s: Unsupported number of active TX-slots (%d)!\n", __func__, slots_active);
		return -EINVAL;
	}

	slots_active = hweight32(rx_mask);
	pr_debug("%s: Slots, active, RX: %d\n", __func__, slots_active);
	switch (slots_active) {
	case 0:
		break;
	case 1:
		/* AD_OUT3 -> slot 0 & 1 */
		snd_soc_update_bits(codec, REG_ADSLOTSEL1, REG_MASK_ALL,
			REG_ADSLOTSELX_AD_OUT3_TO_SLOT_EVEN |
			REG_ADSLOTSELX_AD_OUT3_TO_SLOT_ODD);
		break;
	case 2:
		/* AD_OUT3 -> slot 0, AD_OUT2 -> slot 1 */
		snd_soc_update_bits(codec, REG_ADSLOTSEL1, REG_MASK_ALL,
			REG_ADSLOTSELX_AD_OUT3_TO_SLOT_EVEN |
			REG_ADSLOTSELX_AD_OUT2_TO_SLOT_ODD);
		break;
	case 8:
		pr_debug("%s: In 8-channel mode AD-to-slot mapping is set manually.", __func__);
		break;
	default:
		pr_err("%s: Unsupported number of active RX-slots (%d)!\n", __func__, slots_active);
		return -EINVAL;
	}

	return 0;
}

struct snd_soc_dai_driver ab850x_codec_dai[] = {
	{
		.name = "ab8500-codec-dai.0",
		.id = 0,
		.playback = {
			.stream_name = "ab8500_0p",
			.channels_min = 1,
			.channels_max = 8,
			.rates = AB850X_SUPPORTED_RATE,
			.formats = AB850X_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab850x_codec_pcm_startup,
				.prepare = ab850x_codec_pcm_prepare,
				.hw_params = ab850x_codec_pcm_hw_params,
				.shutdown = ab850x_codec_pcm_shutdown,
				.set_sysclk = ab850x_codec_set_dai_sysclk,
				.set_tdm_slot = ab850x_codec_set_dai_tdm_slot,
				.set_fmt = ab850x_codec_set_dai_fmt,
			}
		},
		.symmetric_rates = 1
	},
	{
		.name = "ab8500-codec-dai.1",
		.id = 1,
		.capture = {
			.stream_name = "ab8500_0c",
			.channels_min = 1,
			.channels_max = 8,
			.rates = AB850X_SUPPORTED_RATE,
			.formats = AB850X_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab850x_codec_pcm_startup,
				.prepare = ab850x_codec_pcm_prepare,
				.hw_params = ab850x_codec_pcm_hw_params,
				.shutdown = ab850x_codec_pcm_shutdown,
				.set_sysclk = ab850x_codec_set_dai_sysclk,
				.set_tdm_slot = ab850x_codec_set_dai_tdm_slot,
				.set_fmt = ab850x_codec_set_dai_fmt,
			}
		},
		.symmetric_rates = 1
	}
};

static int ab850x_codec_probe(struct snd_soc_codec *codec)
{
	enum ab850x_audio_chipid chipid;
	u8 *reg_cache;
	short reg_size;
	int i, ret;

	pr_debug("%s: Enter.\n", __func__);

	reg_cache = codec->reg_cache;
	reg_size = codec->reg_size;
	ab850x_codec_configure_audio_macrocell(codec);

	for (i = reg_size - 1; i >= 0; i--)
		snd_soc_write(codec, i, reg_cache[i]);

	chipid = ab850x_audio_get_chipid(codec->dev);

	/* Add controls */
	chipid_control.access = SNDRV_CTL_ELEM_ACCESS_READ;
	ret = snd_ctl_add(codec->card->snd_card,
			snd_ctl_new1(&chipid_control, codec));
	if (ret < 0)
		goto controls_done;

	ret = snd_soc_add_controls(codec, ab850x_snd_controls,
			ARRAY_SIZE(ab850x_snd_controls));
	if (ret < 0) {
		goto controls_done;
	}
	switch (chipid) {
	case AB850X_AUDIO_AB8500:
		ret = snd_soc_add_controls(codec, ab8500_snd_controls,
				ARRAY_SIZE(ab8500_snd_controls));
		break;
	case AB850X_AUDIO_AB8505_V1:
	case AB850X_AUDIO_AB8505_V2:
	case AB850X_AUDIO_AB8505_V3:
		ret = snd_soc_add_controls(codec, ab8505_vx_snd_controls,
				ARRAY_SIZE(ab8505_vx_snd_controls));
		if (ret < 0)
			break;
		switch (chipid) {
		case AB850X_AUDIO_AB8505_V1:
			ret = snd_soc_add_controls(codec, ab8505_v1_snd_controls,
					ARRAY_SIZE(ab8505_v1_snd_controls));
			break;
		case AB850X_AUDIO_AB8505_V2:
		case AB850X_AUDIO_AB8505_V3:
			ret = snd_soc_add_controls(codec, ab8505_v2_snd_controls,
					ARRAY_SIZE(ab8505_v2_snd_controls));
			if (ret < 0)
				break;
			/* Setting low power mode for VAMIC1 & 2 */
			abx500_mask_and_set_register_interruptible(codec->dev,
					AB8500_SYS_CTRL1_BLOCK, REG_CTRLUSBUICCPUD,
					REG_CTRLUSBUICCPUD_VAMIC12LPMODEENA_MASK, 1);
			break;
		default:
			ret = -EIO;
		}
		break;
	default:
		ret = -EIO;
	}

controls_done:
	if (ret < 0) {
		pr_err("%s: Failed to add %s controls (%d).\n", __func__,
				enum_chipid[chipid], ret);
		return ret;
	}

	/* Add DAPM-widgets */
	ret = ab850x_codec_add_widgets(codec);
	if (ret < 0) {
		pr_err("%s: Failed add widgets (%d).\n", __func__, ret);
		return ret;
	}

	ab850x_codec = codec;

	return ret;
}

enum ab850x_audio_chipid ab850x_audio_get_chipid(struct device *dev)
{
	struct ab8500_codec_drvdata *drvdata;
	drvdata = dev_get_drvdata(dev);
	return drvdata->chipid;
}

static enum ab850x_audio_chipid detect_chipid(struct platform_device *pdev)
{
	struct ab8500 *ab850x_drvdata;
	int chipid_raw;
	enum ab850x_audio_chipid chipid;

	ab850x_drvdata = dev_get_drvdata(pdev->dev.parent);
	chipid_raw = abx500_get_chip_id(&pdev->dev);
	chipid = AB850X_AUDIO_UNKNOWN;

	if (is_ab8500(ab850x_drvdata)) {
		chipid = AB850X_AUDIO_AB8500;
	} else if (is_ab8505(ab850x_drvdata)) {
		switch (chipid_raw) {
		case AB850X_CUT1P0:
			chipid = AB850X_AUDIO_AB8505_V1;
			break;
		case AB850X_CUT2P0:
			chipid = AB850X_AUDIO_AB8505_V2;
			break;
		case AB850X_CUT3P0:
		case AB850X_CUT3P1:
			chipid = AB850X_AUDIO_AB8505_V3;
			break;
		default:
			break;
		}
	}

	if (chipid == AB850X_AUDIO_UNKNOWN)
		pr_err("%s: Unsupported chipset detected! (cid %d)\n",
			__func__, chipid_raw);
	else
		pr_info("%s: %s chipset detected! (cid %d)\n", __func__,
			enum_chipid[chipid], chipid_raw);

	return chipid;
}


static int ab850x_codec_remove(struct snd_soc_codec *codec)
{
	snd_soc_dapm_free(&codec->dapm);
	ab850x_codec = NULL;

	return 0;
}

static int ab850x_codec_suspend(struct snd_soc_codec *codec,
		pm_message_t state)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static int ab850x_codec_resume(struct snd_soc_codec *codec)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

struct snd_soc_codec_driver ab850x_codec_driver = {
	.probe =		ab850x_codec_probe,
	.remove =		ab850x_codec_remove,
	.suspend =		ab850x_codec_suspend,
	.resume =		ab850x_codec_resume,
	.read =			ab850x_codec_read_reg_audio,
	.write =		ab850x_codec_write_reg_audio,
	.reg_cache_size =	0,
	.reg_word_size =	sizeof(u8),
	.reg_cache_default =	ab850x_reg_cache,
};

static int __devinit ab850x_codec_driver_probe(struct platform_device *pdev)
{
	struct ab8500_codec_drvdata *drvdata;
	int err;

	pr_debug("%s: Enter.\n", __func__);

	/* Create driver private-data struct */
	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct ab8500_codec_drvdata),
			GFP_KERNEL);
	if(drvdata == NULL){
		pr_err("%s: Error: Failed to kzalloc.\n", __func__);
		return -ENODEV;
	}
	drvdata->chipid = detect_chipid(pdev);
	dev_set_drvdata(&pdev->dev, drvdata);

	switch (drvdata->chipid) {
	case AB850X_AUDIO_AB8500:
		ab850x_codec_driver.reg_cache_size =
				REG_AUDREV - REG_POWERUP + 1;
		break;
	case AB850X_AUDIO_AB8505_V1:
		ab850x_codec_driver.reg_cache_size =
				REG_VIBGAINCTRL_V1 - REG_POWERUP + 1;
		break;
	case AB850X_AUDIO_AB8505_V2:
	case AB850X_AUDIO_AB8505_V3:
		ab850x_codec_driver.reg_cache_size =
				REG_MIXCTRL - REG_POWERUP + 1;
		break;
	default:
		pr_err("%s: %s chipset not supported!\n", __func__,
			enum_chipid[drvdata->chipid]);
		return -ENODEV;
	}

	pr_info("%s: Register codec.\n", __func__);
	err = snd_soc_register_codec(&pdev->dev,
				&ab850x_codec_driver,
				ab850x_codec_dai,
				ARRAY_SIZE(ab850x_codec_dai));
	if (err < 0) {
		pr_err("%s: Error: Failed to register codec (%d).\n",
			__func__, err);
	}

	pr_debug("%s: Exit.\n", __func__);

	return err;
}

static int __devexit ab850x_codec_driver_remove(struct platform_device *pdev)
{
	pr_info("%s Enter.\n", __func__);

	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static int ab850x_codec_driver_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static int ab850x_codec_driver_resume(struct platform_device *pdev)
{
	pr_debug("%s Enter.\n", __func__);

	return 0;
}

static struct platform_driver ab850x_codec_platform_driver = {
	.driver	= {
		.name	= "ab8500-codec",
		.owner	= THIS_MODULE,
	},
	.probe		= ab850x_codec_driver_probe,
	.remove		= __devexit_p(ab850x_codec_driver_remove),
	.suspend	= ab850x_codec_driver_suspend,
	.resume		= ab850x_codec_driver_resume,
};

static int __devinit ab850x_codec_platform_driver_init(void)
{
	int ret;

	pr_info("%s: Enter.\n", __func__);

	ret = platform_driver_register(&ab850x_codec_platform_driver);
	if (ret != 0) {
		pr_err("%s: Failed to register ab850x platform driver (%d)!\n",
			__func__, ret);
	}

	return ret;
}

static void __exit ab850x_codec_platform_driver_exit(void)
{
	pr_info("%s: Enter.\n", __func__);

	platform_driver_unregister(&ab850x_codec_platform_driver);
}

module_init(ab850x_codec_platform_driver_init);
module_exit(ab850x_codec_platform_driver_exit);

MODULE_DESCRIPTION("AB850X Codec driver");
MODULE_ALIAS("platform:ab8500-codec");
MODULE_AUTHOR("ST-Ericsson");
MODULE_LICENSE("GPL v2");
