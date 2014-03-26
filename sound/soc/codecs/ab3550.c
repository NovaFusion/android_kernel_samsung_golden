/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Xie Xiaolei <xie.xiaolei@etericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>,
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
#include <linux/mfd/abx500.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <asm/atomic.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <stdarg.h>
#include "ab3550.h"


#define I2C_BANK 0

/* codec private data */
struct ab3550_codec_dai_data {
};

static struct device *ab3550_dev;

static u8 virtual_regs[] = {
	0, 0
};

static void set_reg(u8 reg, u8 val)
{
	if (!ab3550_dev) {
		pr_err("%s: The AB3550 codec driver not initialized.\n",
		       __func__);
		return;
	}
	if (reg < AB3550_FIRST_REG)
		return;
	else if (reg <= AB3550_LAST_REG) {
		abx500_set_register_interruptible(
			ab3550_dev, I2C_BANK, reg, val);
	} else if (reg - AB3550_LAST_REG - 1 < ARRAY_SIZE(virtual_regs)) {
		virtual_regs[reg - AB3550_LAST_REG - 1] = val;
	}
}

static void mask_set_reg(u8 reg, u8 mask, u8 val)
{
	if (!ab3550_dev) {
		pr_err("%s: The AB3550 codec driver not initialized.\n",
		       __func__);
		return;
	}
	if (reg < AB3550_FIRST_REG)
		return;
	else if (reg <= AB3550_LAST_REG) {
		abx500_mask_and_set_register_interruptible(
			ab3550_dev, I2C_BANK, reg, mask, val);
	} else if (reg - AB3550_LAST_REG - 1 < ARRAY_SIZE(virtual_regs)) {
		virtual_regs[reg - AB3550_LAST_REG - 1] &= ~mask;
		virtual_regs[reg - AB3550_LAST_REG - 1] |= val & mask;
	}
}

static u8 read_reg(u8 reg)
{
	if (!ab3550_dev) {
		pr_err("%s: The AB3550 codec driver not initialized.\n",
		       __func__);
		return 0;
	}
	if (reg < AB3550_FIRST_REG)
		return 0;
	else if (reg <= AB3550_LAST_REG) {
		u8 val;
		abx500_get_register_interruptible(
			ab3550_dev, I2C_BANK, reg, &val);
		return val;
	} else if (reg - AB3550_LAST_REG - 1 < ARRAY_SIZE(virtual_regs))
		return virtual_regs[reg - AB3550_LAST_REG - 1];
	dev_warn(ab3550_dev, "%s: out-of-scope reigster %u.\n",
		 __func__, reg);
	return 0;
}

/* Components that can be powered up/down */
enum enum_widget {
	widget_ear = 0,
	widget_auxo1,
	widget_auxo2,

	widget_spkr,
	widget_line1,
	widget_line2,

	widget_dac1,
	widget_dac2,
	widget_dac3,

	widget_rx1,
	widget_rx2,
	widget_rx3,

	widget_mic1,
	widget_mic2,

	widget_micbias1,
	widget_micbias2,

	widget_apga1,
	widget_apga2,

	widget_tx1,
	widget_tx2,

	widget_adc1,
	widget_adc2,

	widget_if0_dld_l,
	widget_if0_dld_r,
	widget_if0_uld_l,
	widget_if0_uld_r,
	widget_if1_dld_l,
	widget_if1_dld_r,
	widget_if1_uld_l,
	widget_if1_uld_r,

	widget_mic1p1,
	widget_mic1n1,
	widget_mic1p2,
	widget_mic1n2,

	widget_mic2p1,
	widget_mic2n1,
	widget_mic2p2,
	widget_mic2n2,

	widget_clock,

	number_of_widgets
};

/* This is only meant for debugging */
static const char *widget_names[] = {
	"EAR", "AUXO1", "AUXO2", "SPKR", "LINE1", "LINE2",
	"DAC1", "DAC2", "DAC3",
	"RX1", "RX2", "RX3",
	"MIC1", "MIC2",
	"MIC-BIAS1", "MIC-BIAS2",
	"APGA1", "APGA2",
	"TX1", "TX2",
	"ADC1", "ADC2",
	"IF0-DLD-L", "IF0-DLD-R", "IF0-ULD-L", "IF0-ULD-R",
	"IF1-DLD-L", "IF1-DLD-R", "IF1-ULD-L", "IF1-ULD-R",
	"MIC1P1", "MIC1N1", "MIC1P2", "MIC1N2",
	"MIC2P1", "MIC2N1", "MIC2P2", "MIC2N2",
	"CLOCK"
};

struct widget_pm {
	enum enum_widget widget;
	u8 reg;
	u8 shift;

	unsigned long source_list[BIT_WORD(number_of_widgets) + 1];
	unsigned long sink_list[BIT_WORD(number_of_widgets) + 1];
};

static struct widget_pm widget_pm_array[] = {
	{.widget = widget_ear, .reg = EAR, .shift = EAR_PWR_SHIFT},
	{.widget = widget_auxo1, .reg = AUXO1, .shift = AUXOx_PWR_SHIFT},
	{.widget = widget_auxo2, .reg = AUXO2, .shift = AUXOx_PWR_SHIFT},
	{.widget = widget_spkr, .reg = SPKR, .shift = SPKR_PWR_SHIFT},
	{.widget = widget_line1, .reg = LINE1, .shift = LINEx_PWR_SHIFT},
	{.widget = widget_line2, .reg = LINE2, .shift = LINEx_PWR_SHIFT},

	{.widget = widget_dac1, .reg = RX1, .shift = DACx_PWR_SHIFT},
	{.widget = widget_dac2, .reg = RX2, .shift = DACx_PWR_SHIFT},
	{.widget = widget_dac3, .reg = RX3, .shift = DACx_PWR_SHIFT},

	{.widget = widget_rx1, .reg = RX1, .shift = RXx_PWR_SHIFT},
	{.widget = widget_rx2, .reg = RX2, .shift = RXx_PWR_SHIFT},
	{.widget = widget_rx3, .reg = RX3, .shift = RXx_PWR_SHIFT},

	{.widget = widget_mic1, .reg = MIC1_GAIN, .shift = MICx_PWR_SHIFT},
	{.widget = widget_mic2, .reg = MIC2_GAIN, .shift = MICx_PWR_SHIFT},

	{.widget = widget_micbias1, .reg = MIC_BIAS1,
	 .shift = MBIAS_PWR_SHIFT},
	{.widget = widget_micbias2, .reg = MIC_BIAS2,
	 .shift = MBIAS_PWR_SHIFT},

	{.widget = widget_apga1, .reg = ANALOG_LOOP_PGA1,
	 .shift = APGAx_PWR_SHIFT},
	{.widget = widget_apga2, .reg = ANALOG_LOOP_PGA2,
	 .shift = APGAx_PWR_SHIFT},

	{.widget = widget_tx1, .reg = TX1, .shift = TXx_PWR_SHIFT},
	{.widget = widget_tx2, .reg = TX2, .shift = TXx_PWR_SHIFT},

	{.widget = widget_adc1, .reg = TX1, .shift = ADCx_PWR_SHIFT},
	{.widget = widget_adc2, .reg = TX2, .shift = ADCx_PWR_SHIFT},

	{.widget = widget_if0_dld_l, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF0_DLD_L_PW_SHIFT},
	{.widget = widget_if0_dld_r, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF0_DLD_R_PW_SHIFT},
	{.widget = widget_if0_uld_l, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF0_ULD_L_PW_SHIFT},
	{.widget = widget_if0_uld_r, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF0_ULD_R_PW_SHIFT},

	{.widget = widget_if1_dld_l, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF1_DLD_L_PW_SHIFT},
	{.widget = widget_if1_dld_r, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF1_DLD_R_PW_SHIFT},
	{.widget = widget_if1_uld_l, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF1_ULD_L_PW_SHIFT},
	{.widget = widget_if1_uld_r, .reg = AB3550_VIRTUAL_REG1,
	 .shift = IF1_ULD_R_PW_SHIFT},

	{.widget = widget_mic1p1, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC1P1_PW_SHIFT},
	{.widget = widget_mic1n1, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC1N1_PW_SHIFT},
	{.widget = widget_mic1p2, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC1P2_PW_SHIFT},
	{.widget = widget_mic1n2, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC1N2_PW_SHIFT},

	{.widget = widget_mic2p1, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC2P1_PW_SHIFT},
	{.widget = widget_mic2n1, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC2N1_PW_SHIFT},
	{.widget = widget_mic2p2, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC2P2_PW_SHIFT},
	{.widget = widget_mic2n2, .reg = AB3550_VIRTUAL_REG2,
	 .shift = MIC2N2_PW_SHIFT},

	{.widget = widget_clock, .reg = CLOCK, .shift = CLOCK_ENABLE_SHIFT},
};

DEFINE_MUTEX(ab3550_pm_mutex);

static struct {
	enum enum_widget stack[number_of_widgets];
	int p;
} pm_stack;

struct ab3550_dai_private {
	unsigned int fmt;
};

#define pm_stack_as_bitmap ({						\
			unsigned long bitmap[BIT_WORD(number_of_widgets) + 1]; \
			int i;						\
			memset(bitmap, 0, sizeof(bitmap));		\
			for (i = 0; i < pm_stack.p; i++) {		\
				set_bit(pm_stack.stack[i], bitmap);	\
			}						\
			bitmap;						\
		})

/* These are only meant to meet the obligations of DAPM */
static const struct snd_soc_dapm_widget ab3550_dapm_widgets[] = {
};

static const struct snd_soc_dapm_route intercon[] = {
};


static const char *enum_rx2_select[] = {"I2S0", "I2S1"};
static const char *enum_i2s_input_select[] = {
	"tri-state", "MIC1", "MIC2", "mute"
};
static const char *enum_apga1_source[] = {"LINEIN1", "MIC1", "MIC2"};
static const char *enum_apga2_source[] = {"LINEIN2", "MIC1", "MIC2"};
static const char *enum_dac_side_tone[] = {"TX1", "TX2"};
static const char *enum_dac_power_mode[] = {"100%", "75%", "55%"};
static const char *enum_ear_power_mode[] = {"100%", "70%"};
static const char *enum_auxo_power_mode[] = {
	"100%", "67%", "50%", "25%", "auto"
};
static const char *enum_onoff[] = {"Off", "On"};
static const char *enum_mbias_hiz_option[] = {"GND", "HiZ"};
static const char *enum_mbias2_output_voltage[] = {"2.0v", "2.2v"};
static const char *enum_mic_input_impedance[] = {
	"12.5 kohm", "25 kohm", "50 kohm"
};
static const char *enum_hp_filter[] = {"HP3", "HP1", "bypass"};
static const char *enum_i2s_word_length[] = {"16 bits", "24 bits"};
static const char *enum_i2s_mode[] = {"Master Mode", "Slave Mode"};
static const char *enum_i2s_tristate[] = {"Normal", "Tri-state"};
static const char *enum_optional_resistor[] = {"disconnected", "connected"};
static const char *enum_i2s_sample_rate[] = {
	"8 kHz", "16 kHz", "44.1 kHz", "48 kHz"
};
static const char *enum_signal_inversion[] = {"normal", "inverted"};

/* RX2 Select */
static struct soc_enum soc_enum_rx2_select =
	SOC_ENUM_SINGLE(RX2, 4, ARRAY_SIZE(enum_rx2_select), enum_rx2_select);

/* I2S0 Input Select */
static struct soc_enum soc_enum_i2s0_input_select =
	SOC_ENUM_DOUBLE(INTERFACE0_DATA, 0, 2,
			ARRAY_SIZE(enum_i2s_input_select),
			enum_i2s_input_select);
/* I2S1 Input Select */
static struct soc_enum soc_enum_i2s1_input_select =
	SOC_ENUM_DOUBLE(INTERFACE1_DATA, 0, 2,
			ARRAY_SIZE(enum_i2s_input_select),
			enum_i2s_input_select);

/* APGA1 Source */
static struct soc_enum soc_enum_apga1_source =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA1, APGAx_MUX_SHIFT,
			ARRAY_SIZE(enum_apga1_source), enum_apga1_source);

/* APGA2 Source */
static struct soc_enum soc_enum_apga2_source =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA2, APGAx_MUX_SHIFT,
			ARRAY_SIZE(enum_apga2_source), enum_apga2_source);

static struct soc_enum soc_enum_apga1_enable =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA1, APGAx_PWR_SHIFT,
			ARRAY_SIZE(enum_onoff), enum_onoff);

static struct soc_enum soc_enum_apga2_enable =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA2, APGAx_PWR_SHIFT,
			ARRAY_SIZE(enum_onoff), enum_onoff);

/* DAC1 Side Tone */
static struct soc_enum soc_enum_dac1_side_tone =
	SOC_ENUM_SINGLE(SIDETONE1_PGA, STx_MUX_SHIFT,
			ARRAY_SIZE(enum_dac_side_tone), enum_dac_side_tone);

/* DAC2 Side Tone */
static struct soc_enum soc_enum_dac2_side_tone =
	SOC_ENUM_SINGLE(SIDETONE2_PGA, STx_MUX_SHIFT,
			ARRAY_SIZE(enum_dac_side_tone), enum_dac_side_tone);

/* DAC1 Power Mode */
static struct soc_enum soc_enum_dac1_power_mode =
	SOC_ENUM_SINGLE(RX1, DACx_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_dac_power_mode), enum_dac_power_mode);

/* DAC2 Power Mode */
static struct soc_enum soc_enum_dac2_power_mode =
	SOC_ENUM_SINGLE(RX2, DACx_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_dac_power_mode), enum_dac_power_mode);

/* DAC3 Power Mode */
static struct soc_enum soc_enum_dac3_power_mode =
	SOC_ENUM_SINGLE(RX3, DACx_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_dac_power_mode), enum_dac_power_mode);

/* EAR Power Mode */
static struct soc_enum soc_enum_ear_power_mode =
	SOC_ENUM_SINGLE(EAR, EAR_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_ear_power_mode), enum_ear_power_mode);

/* AUXO Power Mode */
static struct soc_enum soc_enum_auxo_power_mode =
	SOC_ENUM_SINGLE(AUXO_PWR_MODE, AUXO_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_auxo_power_mode),
			enum_auxo_power_mode);

/* MBIAS1 HiZ Option */
static struct soc_enum soc_enum_mbias1_hiz_option =
	SOC_ENUM_SINGLE(MIC_BIAS1, MBIAS_PDN_IMP_SHIFT,
			ARRAY_SIZE(enum_mbias_hiz_option),
			enum_mbias_hiz_option);

/* MBIAS1 HiZ Option */
static struct soc_enum soc_enum_mbias2_hiz_option =
	SOC_ENUM_SINGLE(MIC_BIAS2, MBIAS_PDN_IMP_SHIFT,
			ARRAY_SIZE(enum_mbias_hiz_option),
			enum_mbias_hiz_option);

/* MBIAS2 Output voltage */
static struct soc_enum soc_enum_mbias2_output_voltage =
	SOC_ENUM_SINGLE(MIC_BIAS2, MBIAS2_OUT_V_SHIFT,
			ARRAY_SIZE(enum_mbias2_output_voltage),
			enum_mbias2_output_voltage);

static struct soc_enum soc_enum_mbias2_internal_resistor =
	SOC_ENUM_SINGLE(MIC_BIAS2_VAD, MBIAS2_R_INT_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_mic1_input_impedance =
	SOC_ENUM_SINGLE(MIC1_GAIN, MICx_IN_IMP_SHIFT,
			ARRAY_SIZE(enum_mic_input_impedance),
			enum_mic_input_impedance);

static struct soc_enum soc_enum_mic2_input_impedance =
	SOC_ENUM_SINGLE(MIC2_GAIN, MICx_IN_IMP_SHIFT,
			ARRAY_SIZE(enum_mic_input_impedance),
			enum_mic_input_impedance);

static struct soc_enum soc_enum_tx1_hp_filter =
	SOC_ENUM_SINGLE(TX1, TXx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_tx2_hp_filter =
	SOC_ENUM_SINGLE(TX2, TXx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_st1_hp_filter =
	SOC_ENUM_SINGLE(SIDETONE1_PGA, STx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_st2_hp_filter =
	SOC_ENUM_SINGLE(SIDETONE2_PGA, STx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_i2s0_word_length =
	SOC_ENUM_SINGLE(INTERFACE0, I2Sx_WORDLENGTH_SHIFT,
			ARRAY_SIZE(enum_i2s_word_length),
			enum_i2s_word_length);

static struct soc_enum soc_enum_i2s1_word_length =
	SOC_ENUM_SINGLE(INTERFACE1, I2Sx_WORDLENGTH_SHIFT,
			ARRAY_SIZE(enum_i2s_word_length),
			enum_i2s_word_length);

static struct soc_enum soc_enum_i2s0_mode =
	SOC_ENUM_SINGLE(INTERFACE0, I2Sx_MODE_SHIFT,
			ARRAY_SIZE(enum_i2s_mode),
			enum_i2s_mode);

static struct soc_enum soc_enum_i2s1_mode =
	SOC_ENUM_SINGLE(INTERFACE1, I2Sx_MODE_SHIFT,
			ARRAY_SIZE(enum_i2s_mode),
			enum_i2s_mode);

static struct soc_enum soc_enum_i2s0_tristate =
	SOC_ENUM_SINGLE(INTERFACE0, I2Sx_TRISTATE_SHIFT,
			ARRAY_SIZE(enum_i2s_tristate),
			enum_i2s_tristate);

static struct soc_enum soc_enum_i2s1_tristate =
	SOC_ENUM_SINGLE(INTERFACE1, I2Sx_TRISTATE_SHIFT,
			ARRAY_SIZE(enum_i2s_tristate),
			enum_i2s_tristate);

static struct soc_enum soc_enum_i2s0_pulldown_resistor =
	SOC_ENUM_SINGLE(INTERFACE0, I2Sx_PULLDOWN_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_i2s1_pulldown_resistor =
	SOC_ENUM_SINGLE(INTERFACE1, I2Sx_PULLDOWN_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_i2s0_sample_rate =
	SOC_ENUM_SINGLE(INTERFACE0, I2Sx_SR_SHIFT,
			ARRAY_SIZE(enum_i2s_sample_rate),
			enum_i2s_sample_rate);

static struct soc_enum soc_enum_i2s1_sample_rate =
	SOC_ENUM_SINGLE(INTERFACE1, I2Sx_SR_SHIFT,
			ARRAY_SIZE(enum_i2s_sample_rate),
			enum_i2s_sample_rate);

static struct soc_enum soc_enum_line1_inversion =
	SOC_ENUM_SINGLE(LINE1, LINEx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_line2_inversion =
	SOC_ENUM_SINGLE(LINE2, LINEx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_auxo1_inversion =
	SOC_ENUM_SINGLE(AUXO1, AUXOx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_auxo2_inversion =
	SOC_ENUM_SINGLE(AUXO1, AUXOx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_auxo1_pulldown_resistor =
	SOC_ENUM_SINGLE(AUXO1, AUXOx_PULLDOWN_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_auxo2_pulldown_resistor =
	SOC_ENUM_SINGLE(AUXO1, AUXOx_PULLDOWN_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct snd_kcontrol_new ab3550_snd_controls[] = {
	/* RX Routing */
	SOC_ENUM("RX2 Select",	soc_enum_rx2_select),
	SOC_SINGLE("LINE1 Adder", LINE1_ADDER, 0, 0x07, 0),
	SOC_SINGLE("LINE2 Adder", LINE2_ADDER, 0, 0x07, 0),
	SOC_SINGLE("EAR Adder", EAR_ADDER, 0, 0x07, 0),
	SOC_SINGLE("SPKR Adder", SPKR_ADDER, 0, 0x07, 0),
	SOC_SINGLE("AUXO1 Adder", AUXO1_ADDER, 0, 0x07, 0),
	SOC_SINGLE("AUXO2 Adder", AUXO2_ADDER, 0, 0x07, 0),
	/* TX Routing */
	SOC_SINGLE("MIC1 Input Select",	MIC1_INPUT_SELECT, 0, 0xff, 0),
	SOC_SINGLE("MIC2 Input Select",	MIC1_INPUT_SELECT, 0, 0xff, 0),
	SOC_SINGLE("MIC2 to MIC1", MIC2_TO_MIC1, 0, 0x03, 0),
	SOC_ENUM("I2S0 Input Select",	soc_enum_i2s0_input_select),
	SOC_ENUM("I2S1 Input Select",	soc_enum_i2s1_input_select),
	/* Routing of Side Tone and Analop Loop */
	SOC_ENUM("APGA1 Source", soc_enum_apga1_source),
	SOC_ENUM("APGA2 Source", soc_enum_apga2_source),
	SOC_ENUM("APGA1 Enable", soc_enum_apga1_enable),
	SOC_ENUM("APGA2 Enable", soc_enum_apga2_enable),
	SOC_SINGLE("APGA1 Destination",	APGA1_ADDER, 0, 0x3f, 0),
	SOC_SINGLE("APGA2 Destination",	APGA2_ADDER, 0, 0x3f, 0),
	SOC_ENUM("DAC1 Side Tone",		soc_enum_dac1_side_tone),
	SOC_ENUM("DAC2 Side Tone",		soc_enum_dac2_side_tone),
	/* RX Volume Control */
	SOC_SINGLE("RX-DPGA1 Gain",		RX1_DIGITAL_PGA, 0, 0x43, 0),
	SOC_SINGLE("RX-DPGA2 Gain",		RX1_DIGITAL_PGA, 0, 0x43, 0),
	SOC_SINGLE("RX-DPGA3 Gain",		RX3_DIGITAL_PGA, 0, 0x43, 0),
	SOC_SINGLE("LINE1 Gain", LINE1, LINEx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("LINE2 Gain", LINE2, LINEx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("SPKR Gain",	SPKR, SPKR_GAIN_SHIFT, 0x16, 0),
	SOC_SINGLE("EAR Gain", EAR, EAR_GAIN_SHIFT, 0x0e, 0),
	SOC_SINGLE("AUXO1 Gain", AUXO1, AUXOx_GAIN_SHIFT, 0x0c, 0),
	SOC_SINGLE("AUXO2 Gain", AUXO2, AUXOx_GAIN_SHIFT, 0x0c, 0),
	/* TX Volume Control */
	SOC_SINGLE("MIC1 Gain", MIC1_GAIN, MICx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("MIC2 Gain",	MIC2_GAIN, MICx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("TX-DPGA1 Gain", TX_DIGITAL_PGA1, TXDPGAx_SHIFT, 0x0f, 0),
	SOC_SINGLE("TX-DPGA2 Gain", TX_DIGITAL_PGA2, TXDPGAx_SHIFT, 0x0f, 0),
	/* Volume Control of Side Tone and Analog Loop */
	SOC_SINGLE("ST-PGA1 Gain", SIDETONE1_PGA, STx_PGA_SHIFT, 0x0a, 0),
	SOC_SINGLE("ST-PGA2 Gain", SIDETONE2_PGA, STx_PGA_SHIFT, 0x0a, 0),
	SOC_SINGLE("APGA1 Gain", ANALOG_LOOP_PGA1, APGAx_GAIN_SHIFT, 0x1d, 0),
	SOC_SINGLE("APGA2 Gain", ANALOG_LOOP_PGA2, APGAx_GAIN_SHIFT, 0x1d, 0),
	/* RX Properties */
	SOC_ENUM("DAC1 Power Mode",		soc_enum_dac1_power_mode),
	SOC_ENUM("DAC2 Power Mode",		soc_enum_dac2_power_mode),
	SOC_ENUM("DAC3 Power Mode",		soc_enum_dac3_power_mode),
	SOC_ENUM("EAR Power Mode",		soc_enum_ear_power_mode),
	SOC_ENUM("AUXO Power Mode",		soc_enum_auxo_power_mode),
	SOC_ENUM("LINE1 Inversion",		soc_enum_line1_inversion),
	SOC_ENUM("LINE2 Inversion",		soc_enum_line2_inversion),
	SOC_ENUM("AUXO1 Inversion",		soc_enum_auxo1_inversion),
	SOC_ENUM("AUXO2 Inversion",		soc_enum_auxo2_inversion),
	SOC_ENUM("AUXO1 Pulldown Resistor", soc_enum_auxo1_pulldown_resistor),
	SOC_ENUM("AUXO2 Pulldown Resistor", soc_enum_auxo2_pulldown_resistor),
	/* TX Properties */
	SOC_SINGLE("MIC1 VMID",	MIC1_VMID_SELECT, 0, 0xff, 0),
	SOC_SINGLE("MIC2 VMID",	MIC2_VMID_SELECT, 0, 0xff, 0),
	SOC_ENUM("MBIAS1 HiZ Option",	soc_enum_mbias1_hiz_option),
	SOC_ENUM("MBIAS2 HiZ Option",	soc_enum_mbias2_hiz_option),
	SOC_ENUM("MBIAS2 Output Voltage", soc_enum_mbias2_output_voltage),
	SOC_ENUM("MBIAS2 Internal Resistor", soc_enum_mbias2_internal_resistor),
	SOC_ENUM("MIC1 Input Impedance",	soc_enum_mic1_input_impedance),
	SOC_ENUM("MIC2 Input Impedance",	soc_enum_mic2_input_impedance),
	SOC_ENUM("TX1 HP Filter",		soc_enum_tx1_hp_filter),
	SOC_ENUM("TX2 HP Filter",		soc_enum_tx2_hp_filter),
	/* Side Tone and Analog Loop Properties */
	SOC_ENUM("ST1 HP Filter",		soc_enum_st1_hp_filter),
	SOC_ENUM("ST2 HP Filter",		soc_enum_st2_hp_filter),
	/* I2S Interface Properties */
	SOC_ENUM("I2S0 Word Length",		soc_enum_i2s0_word_length),
	SOC_ENUM("I2S1 Word Length",		soc_enum_i2s1_word_length),
	SOC_ENUM("I2S0 Mode",			soc_enum_i2s0_mode),
	SOC_ENUM("I2S1 Mode",			soc_enum_i2s1_mode),
	SOC_ENUM("I2S0 tri-state",		soc_enum_i2s0_tristate),
	SOC_ENUM("I2S1 tri-state",		soc_enum_i2s1_tristate),
	SOC_ENUM("I2S0 Pulldown Resistor", soc_enum_i2s0_pulldown_resistor),
	SOC_ENUM("I2S1 Pulldown Resistor", soc_enum_i2s1_pulldown_resistor),
	SOC_ENUM("I2S0 Sample Rate",		soc_enum_i2s0_sample_rate),
	SOC_ENUM("I2S1 Sample Rate",		soc_enum_i2s1_sample_rate),
	SOC_SINGLE("Interface Loop",		INTERFACE_LOOP, 0, 0x0f, 0),
	SOC_SINGLE("Interface Swap",		INTERFACE_SWAP, 0, 0x1f, 0),
	/* Miscellaneous */
	SOC_SINGLE("Negative Charge Pump", NEGATIVE_CHARGE_PUMP, 0, 0x03, 0)
};

/* count the number of 1 */
#define count_ones(x) ({						\
			int num;					\
			for (num = 0; x; (x) &= (x) - 1, num++)		\
				;					\
			num;						\
		})

enum enum_power {
	POWER_OFF = 0,
	POWER_ON = 1
};

enum enum_link {
	UNLINK = 0,
	LINK = 1
};

static enum enum_power get_widget_power_status(enum enum_widget widget)
{
	u8 val;

	if (widget >= number_of_widgets)
		return POWER_OFF;
	val = read_reg(widget_pm_array[widget].reg);
	if (val & (1 << widget_pm_array[widget].shift))
		return POWER_ON;
	else
		return POWER_OFF;
}

static int count_powered_neighbors(const unsigned long *neighbors)
{
	unsigned long i;
	int n = 0;
	for_each_set_bit(i, neighbors, number_of_widgets) {
		if (get_widget_power_status(i) == POWER_ON)
			n++;
	}
	return n;
}

static int has_powered_neighbors(const unsigned long *neighbors)
{
	unsigned int i;
	for_each_set_bit(i, neighbors, number_of_widgets) {
		if (get_widget_power_status(i) == POWER_ON)
			return 1;
	}
	return 0;
}


static int has_stacked_neighbors(const unsigned long *neighbors)
{
	unsigned long *stack_map = pm_stack_as_bitmap;
	return bitmap_intersects(stack_map, neighbors, number_of_widgets);
}

static void power_widget_unlocked(enum enum_power onoff,
				  enum enum_widget widget)
{
	enum enum_widget w;
	int done;

	if (widget >= number_of_widgets)
		return;
	if (get_widget_power_status(widget) == onoff)
		return;

	for (w = widget, done = 0; !done;) {
		unsigned long i;
		unsigned long *srcs = widget_pm_array[w].source_list;
		unsigned long *sinks = widget_pm_array[w].sink_list;
		dev_dbg(ab3550_dev, "%s: processing widget %s.\n",
			__func__, widget_names[w]);

		if (onoff == POWER_ON &&
		    !bitmap_empty(srcs, number_of_widgets) &&
		    !has_powered_neighbors(srcs)) {
			pm_stack.stack[pm_stack.p++] = w;
			for_each_set_bit(i, srcs, number_of_widgets) {
				pm_stack.stack[pm_stack.p++] = i;
			}
			w = pm_stack.stack[--pm_stack.p];
			continue;
		} else if (onoff == POWER_OFF &&
			   has_powered_neighbors(sinks)) {
			int n = 0;
			pm_stack.stack[pm_stack.p++] = w;
			for_each_set_bit(i, sinks, number_of_widgets) {
				if (count_powered_neighbors(
					    widget_pm_array[i].source_list)
				    == 1 &&
				    get_widget_power_status(i) == POWER_ON) {
					pm_stack.stack[pm_stack.p++] = i;
					n++;
				}
			}
			if (n) {
				w = pm_stack.stack[--pm_stack.p];
				continue;
			} else
				--pm_stack.p;
		}
		mask_set_reg(widget_pm_array[w].reg,
			     1 << widget_pm_array[w].shift,
			     onoff == POWER_ON ? 0xff : 0);
		dev_dbg(ab3550_dev, "%s: widget %s powered %s.\n",
			__func__, widget_names[w],
			onoff == POWER_ON ? "on" : "off");

		if (onoff == POWER_ON &&
		    !bitmap_empty(sinks, number_of_widgets) &&
		    !has_powered_neighbors(sinks) &&
		    !has_stacked_neighbors(sinks)) {
			for_each_set_bit(i, sinks, number_of_widgets) {
				pm_stack.stack[pm_stack.p++] = i;
			}
			w = pm_stack.stack[--pm_stack.p];
			continue;
		} else if (onoff == POWER_OFF) {
			for_each_set_bit(i, srcs, number_of_widgets) {
				if (!has_powered_neighbors(
					    widget_pm_array[i].sink_list)
				    && get_widget_power_status(i) == POWER_ON
				    && !test_bit(i, pm_stack_as_bitmap)) {
					pm_stack.stack[pm_stack.p++] = i;
				}
			}
		}
		if (pm_stack.p > 0)
			w = pm_stack.stack[--pm_stack.p];
		else
			done = 1;
	}
}

static void power_widget_locked(enum enum_power onoff,
				enum enum_widget widget)
{
	if (mutex_lock_interruptible(&ab3550_pm_mutex)) {
		dev_warn(ab3550_dev,
			 "%s: Signal received while waiting on the PM mutex.\n",
			 __func__);
		return;
	}
	power_widget_unlocked(onoff, widget);
	mutex_unlock(&ab3550_pm_mutex);
}

static void dump_registers(const char *where, ...)
{
	va_list ap;
	va_start(ap, where);
	do {
		short reg = va_arg(ap, int);
		if (reg < 0)
			break;
		dev_dbg(ab3550_dev, "%s from %s> 0x%02X : 0x%02X.\n",
			__func__, where, reg, read_reg(reg));
	} while (1);
	va_end(ap);
}

/**
 * update the link between two widgets.
 * @op: 1 - connect; 0 - disconnect
 * @src: source of the connection
 * @sink: sink of the connection
 */
static int update_widgets_link(enum enum_link op, enum enum_widget src,
			       enum enum_widget sink,
			       u8 reg, u8 mask, u8 newval)
{
	int ret = 0;

	if (mutex_lock_interruptible(&ab3550_pm_mutex)) {
		dev_warn(ab3550_dev, "%s: A signal is received while waiting on"
			 " the PM mutex.\n", __func__);
		return -EINTR;
	}

	switch (op << 2 | test_bit(sink, widget_pm_array[src].sink_list) << 1 |
		test_bit(src, widget_pm_array[sink].source_list)) {
	case 3: /* UNLINK, sink in sink_list, src in source_list */
	case 4: /* LINK, sink not in sink_list, src not in source_list */
		break;
	default:
		ret = -EINVAL;
		goto end;
	}
	switch (((int)op) << 2 | get_widget_power_status(src) << 1 |
		get_widget_power_status(sink)) {
	case 3: /* op = 0, src on, sink on */
		if (count_powered_neighbors(widget_pm_array[sink].source_list)
		    == 1)
			power_widget_unlocked(POWER_OFF, sink);
		mask_set_reg(reg, mask, newval);
		break;
	case 6: /* op = 1, src on, sink off */
		mask_set_reg(reg, mask, newval);
		power_widget_unlocked(POWER_ON, sink);
		break;
	default:
		/* op = 0, src off, sink off */
		/* op = 0, src off, sink on */
		/* op = 0, src on, sink off */
		/* op = 1, src off, sink off */
		/* op = 1, src off, sink on */
		/* op = 1, src on, sink on */
		mask_set_reg(reg, mask, newval);
	}
	change_bit(sink, widget_pm_array[src].sink_list);
	change_bit(src, widget_pm_array[sink].source_list);
end:
	mutex_unlock(&ab3550_pm_mutex);
	return ret;
};

static enum enum_widget apga_source_translate(u8 reg_value)
{
	switch (reg_value) {
	case 1:
		return widget_mic1;
	case 2:
		return widget_mic2;
	default:
		return number_of_widgets;
	}
}

static enum enum_widget adder_sink_translate(u8 reg)
{
	switch (reg) {
	case EAR_ADDER:
		return widget_ear;
	case AUXO1_ADDER:
		return widget_auxo1;
	case AUXO2_ADDER:
		return widget_auxo2;
	case SPKR_ADDER:
		return widget_spkr;
	case LINE1_ADDER:
		return widget_line1;
	case LINE2_ADDER:
		return widget_line2;
	case APGA1_ADDER:
		return widget_apga1;
	case APGA2_ADDER:
		return widget_apga2;
	default:
		return number_of_widgets;
	}
}

static int ab3550_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(&codec->dapm, ab3550_dapm_widgets,
				  ARRAY_SIZE(ab3550_dapm_widgets));

	snd_soc_dapm_add_routes(&codec->dapm, intercon, ARRAY_SIZE(intercon));

	snd_soc_dapm_new_widgets(&codec->dapm);
	return 0;
}

static void power_for_playback(enum enum_power onoff, int ifsel)
{
	dev_dbg(ab3550_dev, "%s: interface %d power %s.\n", __func__,
		ifsel, onoff == POWER_ON ? "on" : "off");

	if (mutex_lock_interruptible(&ab3550_pm_mutex)) {
		dev_warn(ab3550_dev,
			 "%s: Signal received while waiting on the PM mutex.\n",
			 __func__);
		return;
	}
	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_dld_l : widget_if1_dld_l);
	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_dld_r : widget_if1_dld_r);
	mutex_unlock(&ab3550_pm_mutex);
}

static void power_for_capture(enum enum_power onoff, int ifsel)
{
	dev_dbg(ab3550_dev, "%s: interface %d power %s", __func__,
		ifsel, onoff == POWER_ON ? "on" : "off");
	if (mutex_lock_interruptible(&ab3550_pm_mutex)) {
		dev_warn(ab3550_dev,
			 "%s: Signal received while waiting on the PM mutex.\n",
			 __func__);
		return;
	}
	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_uld_l : widget_if1_uld_l);
	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_uld_r : widget_if1_uld_r);
	mutex_unlock(&ab3550_pm_mutex);
}

static int ab3550_add_controls(struct snd_soc_codec *codec)
{
	int err = 0, i, n = ARRAY_SIZE(ab3550_snd_controls);

	pr_debug("%s: %s called.\n", __FILE__, __func__);
	for (i = 0; i < n; i++) {
		err = snd_ctl_add(codec->card->snd_card, snd_ctl_new1(
					  &ab3550_snd_controls[i], codec));
		if (err < 0) {
			pr_err("%s failed to add control No.%d of %d.\n",
			       __func__, i, n);
			return err;
		}
	}
	return err;
}

static int ab3550_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params,
				struct snd_soc_dai *dai)
{
	u8 val;
	u8 reg = dai->id == 0 ? INTERFACE0 : INTERFACE1;

	if (!ab3550_dev) {
		pr_err("%s: The AB3550 codec driver not initialized.\n",
		       __func__);
		return -EAGAIN;
	}
	dev_info(ab3550_dev, "%s called.\n", __func__);
	switch (params_rate(hw_params)) {
	case 8000:
		val = I2Sx_SR_8000Hz;
		break;
	case 16000:
		val = I2Sx_SR_16000Hz;
		break;
	case 44100:
		val = I2Sx_SR_44100Hz;
		break;
	case 48000:
		val = I2Sx_SR_48000Hz;
		break;
	default:
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
	    !dai->capture_active : !dai->playback_active) {

		mask_set_reg(reg, I2Sx_SR_MASK, val << I2Sx_SR_SHIFT);
		if ((read_reg(reg) & I2Sx_MODE_MASK) == 0) {
			mask_set_reg(reg, MASTER_GENx_PWR_MASK,
				     1 << MASTER_GENx_PWR_SHIFT);
		}
	}
	return 0;
}

static int ab3550_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
	    dai->playback_active : dai->capture_active) {

		dev_err(ab3550_dev, "%s: A %s stream is already active.\n",
			__func__,
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			"PLAYBACK" : "CAPTURE");
		return -EBUSY;
	}
	return 0;
}
static int ab3550_pcm_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	dev_info(ab3550_dev, "%s called.\n", __func__);

	/* Configure registers for either playback or capture */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		power_for_playback(POWER_ON, dai->id);
		dump_registers(__func__,
			       dai->id == 0 ? INTERFACE0 : INTERFACE1,
			       RX1, RX2, SPKR, EAR, -1);
	} else {
		power_for_capture(POWER_ON, dai->id);
		dump_registers(__func__, MIC_BIAS1, MIC_BIAS2, MIC1_GAIN, TX1,
			       dai->id == 0 ? INTERFACE0 : INTERFACE1, -1);
	}
	return 0;
}

static void ab3550_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	u8 iface = dai->id == 0 ? INTERFACE0 : INTERFACE1;
	dev_info(ab3550_dev, "%s called.\n", __func__);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		power_for_playback(POWER_OFF, dai->id);
	else
		power_for_capture(POWER_OFF, dai->id);
	if (!dai->playback_active && !dai->capture_active &&
	    (read_reg(iface) & I2Sx_MODE_MASK) == 0)
		mask_set_reg(iface, MASTER_GENx_PWR_MASK, 0);
}

static int ab3550_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	return 0;
}

static int ab3550_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	u8 iface = (codec_dai->id == 0) ? INTERFACE0 : INTERFACE1;
	u8 val = 0;
	dev_info(ab3550_dev, "%s called.\n", __func__);

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_MASTER_MASK)) {

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
		val |= 1 << I2Sx_MODE_SHIFT;
		break;

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
		break;

	default:
		dev_warn(ab3550_dev, "AB3550_dai: unsupported DAI format "
			 "0x%x\n", fmt);
		return -EINVAL;
	}
	if (codec_dai->playback_active && codec_dai->capture_active) {
		if ((read_reg(iface) & I2Sx_MODE_MASK) == val)
			return 0;
		else {
			dev_err(ab3550_dev,
				"%s: DAI format set differently "
				"by an existing stream.\n", __func__);
			return -EINVAL;
		}
	}
	mask_set_reg(iface, I2Sx_MODE_MASK, val);
	return 0;
}

struct snd_soc_dai_driver ab3550_dai_drv[] = {
	{
		.name = "ab3550-codec-dai.0",
		.id = 0,
		.playback = {
			.stream_name = "AB3550.0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB3550_SUPPORTED_RATE,
			.formats = AB3550_SUPPORTED_FMT,
		},
		.capture = {
			.stream_name = "AB3550.0 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB3550_SUPPORTED_RATE,
			.formats = AB3550_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab3550_pcm_startup,
				.prepare = ab3550_pcm_prepare,
				.hw_params = ab3550_pcm_hw_params,
				.shutdown = ab3550_pcm_shutdown,
				.set_sysclk = ab3550_set_dai_sysclk,
				.set_fmt = ab3550_set_dai_fmt,
			}
		},
		.symmetric_rates = 1,
	},
	{
		.name = "ab3550-codec-dai.1",
		.id = 1,
		.playback = {
			.stream_name = "AB3550.1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB3550_SUPPORTED_RATE,
			.formats = AB3550_SUPPORTED_FMT,
		},
		.capture = {
			.stream_name = "AB3550.0 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB3550_SUPPORTED_RATE,
			.formats = AB3550_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab3550_pcm_startup,
				.prepare = ab3550_pcm_prepare,
				.hw_params = ab3550_pcm_hw_params,
				.shutdown = ab3550_pcm_shutdown,
				.set_sysclk = ab3550_set_dai_sysclk,
				.set_fmt = ab3550_set_dai_fmt,
			}
		},
		.symmetric_rates = 1,
	}
};
EXPORT_SYMBOL_GPL(ab3550_dai_drv);

static int ab3550_codec_probe(struct snd_soc_codec *codec)
{
	int ret;

	pr_info("%s: Enter.\n", __func__);

	/* Add controls */
	if (ab3550_add_controls(codec) < 0)
		return ret;

	/* Add widgets */
	ab3550_add_widgets(codec);

	return 0;
}

static int ab3550_codec_remove(struct snd_soc_codec *codec)
{
	snd_soc_dapm_free(&codec->dapm);

	return 0;
}

#ifdef CONFIG_PM
static int ab3550_codec_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0);

	return 0;
}

static int ab3550_codec_resume(struct snd_soc_codec *codec)
{
	mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0xff);

	return 0;
}
#else
#define ab3550_codec_resume NULL
#define ab3550_codec_suspend NULL
#endif

/*
 * This function is only called by the SOC framework to
 * set registers associated to the mixer controls.
 */
static int ab3550_codec_write_reg(struct snd_soc_codec *codec, unsigned int reg,
			    unsigned int value)
{
	if (reg < MIC_BIAS1 || reg > INTERFACE_SWAP)
		return -EINVAL;
	switch (reg) {
		u8 diff, oldval;
	case ANALOG_LOOP_PGA1:
	case ANALOG_LOOP_PGA2: {
		enum enum_widget apga = reg == ANALOG_LOOP_PGA1 ?
			widget_apga1 : widget_apga2;

		oldval = read_reg(reg);
		diff = value ^ oldval;

		/* The APGA is to be turned on/off.
		 * The power bit and the other bits in the
		 * same register won't be changed at the same time
		 * since they belong to different controls.
		 */
		if (diff & (1 << APGAx_PWR_SHIFT)) {
			power_widget_locked(value >> APGAx_PWR_SHIFT & 1,
					    apga);
		} else if (diff & APGAx_MUX_MASK) {
			enum enum_widget old_source =
				apga_source_translate(oldval);
			enum enum_widget new_source =
				apga_source_translate(value);
			update_widgets_link(UNLINK, old_source, apga,
					    reg, APGAx_MUX_MASK, 0);
			update_widgets_link(LINK, new_source, apga,
					    reg, APGAx_MUX_MASK, value);
		} else {
			set_reg(reg, value);
		}
		break;
	}

	case APGA1_ADDER:
	case APGA2_ADDER: {
		int i;
		enum enum_widget apga;
		enum enum_widget apga_dst[] = {
			widget_auxo2, widget_auxo1, widget_ear, widget_spkr,
			widget_line2, widget_line1
		};

		apga = adder_sink_translate(reg);
		oldval = read_reg(reg);
		diff = value ^ oldval;
		for (i = 0; diff; i++) {
			if (!(diff & 1 << i))
				continue;
			diff ^= 1 << i;
			update_widgets_link(value >> i & 1, apga, apga_dst[i],
					    reg, 1 << i, value);
		}
		break;
	}

	case EAR_ADDER:
	case AUXO1_ADDER:
	case AUXO2_ADDER:
	case SPKR_ADDER:
	case LINE1_ADDER:
	case LINE2_ADDER: {
		int i;
		enum enum_widget widgets[] = {
			widget_dac1, widget_dac2, widget_dac3,
		};
		oldval = read_reg(reg);
		diff = value ^ oldval;
		for (i = 0; diff; i++) {
			if (!(diff & 1 << i))
				continue;
			diff ^= 1 << i;
			update_widgets_link(value >> i & 1, widgets[i],
					    adder_sink_translate(reg),
					    reg, 1 << i, value);
		}
		break;
	}

	default:
		set_reg(reg, value);
	}
	return 0;
}

static unsigned int ab3550_codec_read_reg(struct snd_soc_codec *codec,
				    unsigned int reg)
{
	return read_reg(reg);
}

static struct snd_soc_codec_driver ab3550_codec_drv = {
	.probe =	ab3550_codec_probe,
	.remove =	ab3550_codec_remove,
	.suspend =	ab3550_codec_suspend,
	.resume =	ab3550_codec_resume,
	.read =		ab3550_codec_read_reg,
	.write =	ab3550_codec_write_reg,
};
EXPORT_SYMBOL_GPL(ab3550_codec_drv);

static inline void init_playback_route(void)
{
	update_widgets_link(LINK, widget_if0_dld_l, widget_rx1, 0, 0, 0);
	update_widgets_link(LINK, widget_rx1, widget_dac1, 0, 0, 0);
	update_widgets_link(LINK, widget_dac1, widget_spkr,
			    SPKR_ADDER, DAC1_TO_ADDER_MASK, 0xff);

	update_widgets_link(LINK, widget_if0_dld_r, widget_rx2,
			    RX2, RX2_IF_SELECT_MASK, 0);
	update_widgets_link(LINK, widget_rx2, widget_dac2, 0, 0, 0);
	update_widgets_link(LINK, widget_dac2, widget_ear,
			    EAR_ADDER, DAC2_TO_ADDER_MASK, 0xff);
}

static inline void init_capture_route(void)
{
	update_widgets_link(LINK, widget_micbias2, widget_mic1p1,
			    0, 0, 0);
	update_widgets_link(LINK, widget_micbias2, widget_mic1n1,
			    0, 0, 0);
	update_widgets_link(LINK, widget_mic1p1, widget_mic1,
			    MIC1_INPUT_SELECT, MICxP1_SEL_MASK, 0xff);
	update_widgets_link(LINK, widget_mic1n1, widget_mic1,
			    MIC1_INPUT_SELECT, MICxN1_SEL_MASK, 0xff);
	update_widgets_link(LINK, widget_mic1, widget_adc1,
			    0, 0, 0);
	update_widgets_link(LINK, widget_adc1, widget_tx1,
			    0, 0, 0);
	update_widgets_link(LINK, widget_tx1, widget_if0_uld_l,
			    INTERFACE0_DATA, I2Sx_L_DATA_MASK,
			    I2Sx_L_DATA_TX1_MASK);
	update_widgets_link(LINK, widget_tx1, widget_if0_uld_r,
			    INTERFACE0_DATA, I2Sx_R_DATA_MASK,
			    I2Sx_R_DATA_TX1_MASK);
}

static inline void init_playback_gain(void)
{
	mask_set_reg(RX1_DIGITAL_PGA, RXx_PGA_GAIN_MASK,
		     0x40 << RXx_PGA_GAIN_SHIFT);
	mask_set_reg(RX2_DIGITAL_PGA, RXx_PGA_GAIN_MASK,
		     0x40 << RXx_PGA_GAIN_SHIFT);
	mask_set_reg(EAR, EAR_GAIN_MASK, 0x06 << EAR_GAIN_SHIFT);
	mask_set_reg(SPKR, SPKR_GAIN_MASK, 0x6 << SPKR_GAIN_SHIFT);
}

static inline void init_capture_gain(void)
{
	mask_set_reg(MIC1_GAIN, MICx_GAIN_MASK, 0x06 << MICx_GAIN_SHIFT);
	mask_set_reg(TX_DIGITAL_PGA1, TXDPGAx_MASK, 0x0f << TXDPGAx_SHIFT);
}

static __devinit int ab3550_codec_drv_probe(struct platform_device *pdev)
{
	struct ab3550_codec_dai_data *codec_drvdata;
	int ret = 0;
	u8 reg;

	pr_debug("%s: Enter.\n", __func__);

	pr_info("%s: Init codec private data.\n", __func__);
	codec_drvdata = kzalloc(sizeof(struct ab3550_codec_dai_data), GFP_KERNEL);
	if (codec_drvdata == NULL)
		return -ENOMEM;

	/* TODO: Add private data to codec_drvdata */

	platform_set_drvdata(pdev, codec_drvdata);

	pr_info("%s: Register codec.\n", __func__);
	ret = snd_soc_register_codec(&pdev->dev, &ab3550_codec_drv, &ab3550_dai_drv[0], 2);
	if (ret < 0) {
		pr_debug("%s: Error: Failed to register codec (ret = %d).\n",
			__func__,
			ret);
		snd_soc_unregister_codec(&pdev->dev);
		kfree(platform_get_drvdata(pdev));
		return ret;
	}

	ab3550_dev = &pdev->dev;
	/* Initialize the codec registers */
	for (reg = AB3550_FIRST_REG; reg <= AB3550_LAST_REG; reg++)
		set_reg(reg, 0);

	mask_set_reg(CLOCK, CLOCK_REF_SELECT_MASK | CLOCK_ENABLE_MASK,
		     1 << CLOCK_REF_SELECT_SHIFT | 1 << CLOCK_ENABLE_SHIFT);
	init_playback_route();
	init_playback_gain();
	init_capture_route();
	init_capture_gain();
	memset(&pm_stack, 0, sizeof(pm_stack));

	return 0;
}

static int __devexit ab3550_codec_drv_remove(struct platform_device *pdev)
{
	mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0);

	ab3550_dev = NULL;

	snd_soc_unregister_codec(&pdev->dev);
	kfree(platform_get_drvdata(pdev));

	return 0;
}

static int ab3550_codec_drv_suspend(struct platform_device *pdev,
				   pm_message_t state)
{
	return 0;
}

static int ab3550_codec_drv_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ab3550_codec_platform_drv = {
	.driver = {
		.name = "ab3550-codec",
		.owner = THIS_MODULE,
	},
	.probe = ab3550_codec_drv_probe,
	.remove = __devexit_p(ab3550_codec_drv_remove),
	.suspend	= ab3550_codec_drv_suspend,
	.resume		= ab3550_codec_drv_resume,
};


static int __devinit ab3550_codec_platform_drv_init(void)
{
	int ret;

	pr_debug("%s: Enter.\n", __func__);

	ab3550_dev = NULL;

	ret = platform_driver_register(&ab3550_codec_platform_drv);
	if (ret != 0)
		pr_err("Failed to register AB3550 platform driver (%d)!\n", ret);

	return ret;
}

static void __exit ab3550_codec_platform_drv_exit(void)
{
	pr_debug("%s: Enter.\n", __func__);

	platform_driver_unregister(&ab3550_codec_platform_drv);
}


module_init(ab3550_codec_platform_drv_init);
module_exit(ab3550_codec_platform_drv_exit);

MODULE_DESCRIPTION("AB3550 Codec driver");
MODULE_AUTHOR("Xie Xiaolei <xie.xiaolei@stericsson.com>");
MODULE_LICENSE("GPL v2");
