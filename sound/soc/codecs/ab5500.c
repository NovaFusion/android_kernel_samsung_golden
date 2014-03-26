/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Xie Xiaolei <xie.xiaolei@etericsson.com>,
 *         Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
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
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <asm/atomic.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <stdarg.h>
#include "ab5500.h"

/* No of digital interface on the Codec */
#define NO_CODEC_DAI_IF  2

/* codec private data */
struct ab5500_codec_dai_data {
	bool playback_active;
	bool capture_active;

};

enum regulator_idx {
	REGULATOR_DMIC,
	REGULATOR_AMIC
};

static struct device *ab5500_dev;
static struct regulator_bulk_data reg_info[2] = {
	{	.supply = "vdigmic"	},
	{	.supply = "v-amic"	}
};

static bool reg_enabled[2] = {
	false,
	false
};

static u8 virtual_regs[] = {
	0, 0, 0, 0, 0
};

static int ab5500_clk_request;
static DEFINE_MUTEX(ab5500_clk_mutex);

#define set_reg(reg, val) mask_set_reg((reg), 0xff, (val))

static void mask_set_reg(u8 reg, u8 mask, u8 val)
{
	u8 newval = mask & val;
	u8 oldval, diff;

	if (!ab5500_dev) {
		pr_err("%s: The AB5500 codec driver not initialized.\n",
		       __func__);
		return;
	}
	/* Check if the reg value falls within the
	 * range of AB5500 real registers. If
	 * so, set the mask */
	if (reg < AB5500_FIRST_REG)
		return;
	if (reg <= AB5500_LAST_REG) {
		abx500_mask_and_set_register_interruptible(
			ab5500_dev, AB5500_BANK_AUDIO_HEADSETUSB,
			reg, mask, val);
		return;
	}
	if (reg - AB5500_LAST_REG - 1 >= ARRAY_SIZE(virtual_regs))
		return;

	/* treatment of virtual registers follows */
	/*Compute the difference between the new value and the old value.
	 *1.If there is no difference, do nothing.
	 *2.If the difference is in the PWR_SHIFT,
	 *set the PWR masks appropriately.
	 */
	oldval = virtual_regs[reg - AB5500_LAST_REG - 1];
	diff = (val ^ oldval) & mask;
	if (!diff)
		return;

	switch (reg) {
	case AB5500_VIRTUAL_REG3:
		if ((diff & (1 << SPKR1_PWR_SHIFT))) {
			if ((val & (1 << SPKR1_PWR_SHIFT)) == 0) {
				/*
				 * If the new value has PWR_SHIFT
				 * disabled, set the
				 * PWR_MASK to 0
				 */
				mask_set_reg(SPKR1, SPKRx_PWR_MASK, 0);
			}
			else {
				/* Else, set the PWR_MASK values based on the old value. */
				switch (oldval & SPKR1_MODE_MASK) {
				case 0:
					mask_set_reg(SPKR1, SPKRx_PWR_MASK,
						     SPKRx_PWR_VBR_VALUE);
					break;
				case 1:
					mask_set_reg(SPKR1, SPKRx_PWR_MASK,
						     SPKRx_PWR_CLS_D_VALUE);
					break;
				case 2:
					mask_set_reg(SPKR1, SPKRx_PWR_MASK,
						     SPKRx_PWR_CLS_AB_VALUE);
					break;
				}
		}
		}
		if ((diff & (1 << SPKR2_PWR_SHIFT))) {
			if ((val & (1 << SPKR2_PWR_SHIFT)) == 0) {
				/*
				 * If the new value has PWR_SHIFT
				 * disabled, set the
				 * PWR_MASK to 0
				 */
				mask_set_reg(SPKR2, SPKRx_PWR_MASK, 0);
			}
			else {
				/* Else, set the PWR_MASK values based on the old value. */
				switch (oldval & SPKR2_MODE_MASK) {
				case 0:
					mask_set_reg(SPKR2, SPKRx_PWR_MASK,
						     SPKRx_PWR_VBR_VALUE);
					break;
				case 1:
					mask_set_reg(SPKR2, SPKRx_PWR_MASK,
						     SPKRx_PWR_CLS_D_VALUE);
					break;
				}
			}
		}

		break;
	case AB5500_VIRTUAL_REG4:
		;
		/* configure PWMCTRL_SPKR1, PWMCTRL_SPKR2, etc. */
	}
	virtual_regs[reg - AB5500_LAST_REG - 1] &= ~mask;
	virtual_regs[reg - AB5500_LAST_REG - 1] |= newval;
}

static u8 read_reg(u8 reg)
{
	if (!ab5500_dev) {
		pr_err("%s: The AB5500 codec driver not initialized.\n",
		       __func__);
		return 0;
	}
	/* Check if the reg value falls within the range of AB5500 real
	 * registers.If so, set the mask */
	if (reg < AB5500_FIRST_REG)
		return 0;
	else if (reg <= AB5500_LAST_REG) {
		u8 val;
		abx500_get_register_interruptible(
			ab5500_dev, AB5500_BANK_AUDIO_HEADSETUSB, reg, &val);
		return val;
	} else if (reg - AB5500_LAST_REG - 1 < ARRAY_SIZE(virtual_regs))
		return virtual_regs[reg - AB5500_LAST_REG - 1];
	dev_warn(ab5500_dev, "%s: out-of-scope reigster %u.\n",
		 __func__, reg);
	return 0;
}

/* Components that can be powered up/down */
enum enum_widget {
	widget_ear = 0,
	widget_auxo1,
	widget_auxo2,
	widget_auxo3,
	widget_auxo4,
	widget_spkr1,
	widget_spkr2,
	widget_spkr1_adder,
	widget_spkr2_adder,
	widget_pwm_spkr1,
	widget_pwm_spkr2,
	widget_pwm_spkr1n,
	widget_pwm_spkr1p,
	widget_pwm_spkr2n,
	widget_pwm_spkr2p,
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
	"EAR", "AUXO1", "AUXO2", "AUXO3", "AUXO4",
	"SPKR1", "SPKR2", "SPKR1_ADDER", "SPKR2_ADDER",
	"PWM_SPKR1", "PWM_SPKR2",
	"PWM_SPKR1N", "PWM_SPKR1P",
	"PWM_SPKR2N", "PWM_SPKR2P",
	"LINE1", "LINE2",
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
	{.widget = widget_ear, .reg = EAR_PWR, .shift = EAR_PWR_SHIFT},

	{.widget = widget_auxo1, .reg = AUXO1, .shift = AUXOx_PWR_SHIFT},
	{.widget = widget_auxo2, .reg = AUXO2, .shift = AUXOx_PWR_SHIFT},
	{.widget = widget_auxo3, .reg = AUXO3, .shift = AUXOx_PWR_SHIFT},
	{.widget = widget_auxo4, .reg = AUXO4, .shift = AUXOx_PWR_SHIFT},

	{.widget = widget_spkr1, .reg = DUMMY_REG,  .shift = 0},
	{.widget = widget_spkr2, .reg = AB5500_VIRTUAL_REG3,
	 .shift = SPKR2_PWR_SHIFT},

	{.widget = widget_spkr1_adder, .reg = AB5500_VIRTUAL_REG3,
	 .shift = SPKR1_ADDER_PWR_SHIFT},
	{.widget = widget_spkr2_adder, .reg = AB5500_VIRTUAL_REG3,
	 .shift = SPKR2_ADDER_PWR_SHIFT},

	{.widget = widget_pwm_spkr1, .reg = AB5500_VIRTUAL_REG4,
	 .shift = PWM_SPKR1_PWR_SHIFT},
	{.widget = widget_pwm_spkr2, .reg = AB5500_VIRTUAL_REG4,
	 .shift = PWM_SPKR2_PWR_SHIFT},

	{.widget = widget_pwm_spkr1n, .reg = AB5500_VIRTUAL_REG4,
	 .shift = PWM_SPKR1N_PWR_SHIFT},
	{.widget = widget_pwm_spkr1p, .reg = AB5500_VIRTUAL_REG4,
	 .shift = PWM_SPKR1P_PWR_SHIFT},

	{.widget = widget_pwm_spkr2n, .reg = AB5500_VIRTUAL_REG4,
	 .shift = PWM_SPKR2N_PWR_SHIFT},
	{.widget = widget_pwm_spkr2p, .reg = AB5500_VIRTUAL_REG4,
	 .shift = PWM_SPKR2P_PWR_SHIFT},


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
	 .shift = MBIASx_PWR_SHIFT},
	{.widget = widget_micbias2, .reg = MIC_BIAS2,
	 .shift = MBIASx_PWR_SHIFT},

	{.widget = widget_apga1, .reg = ANALOG_LOOP_PGA1,
	 .shift = APGAx_PWR_SHIFT},
	{.widget = widget_apga2, .reg = ANALOG_LOOP_PGA2,
	 .shift = APGAx_PWR_SHIFT},

	{.widget = widget_tx1, .reg = TX1, .shift = TXx_PWR_SHIFT},
	{.widget = widget_tx2, .reg = TX2, .shift = TXx_PWR_SHIFT},

	{.widget = widget_adc1, .reg = TX1, .shift = ADCx_PWR_SHIFT},
	{.widget = widget_adc2, .reg = TX2, .shift = ADCx_PWR_SHIFT},

	{.widget = widget_if0_dld_l, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF0_DLD_L_PW_SHIFT},
	{.widget = widget_if0_dld_r, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF0_DLD_R_PW_SHIFT},
	{.widget = widget_if0_uld_l, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF0_ULD_L_PW_SHIFT},
	{.widget = widget_if0_uld_r, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF0_ULD_R_PW_SHIFT},

	{.widget = widget_if1_dld_l, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF1_DLD_L_PW_SHIFT},
	{.widget = widget_if1_dld_r, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF1_DLD_R_PW_SHIFT},
	{.widget = widget_if1_uld_l, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF1_ULD_L_PW_SHIFT},
	{.widget = widget_if1_uld_r, .reg = AB5500_VIRTUAL_REG1,
	 .shift = IF1_ULD_R_PW_SHIFT},

	{.widget = widget_mic1p1, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC1P1_PW_SHIFT},
	{.widget = widget_mic1n1, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC1N1_PW_SHIFT},
	{.widget = widget_mic1p2, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC1P2_PW_SHIFT},
	{.widget = widget_mic1n2, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC1N2_PW_SHIFT},

	{.widget = widget_mic2p1, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC2P1_PW_SHIFT},
	{.widget = widget_mic2n1, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC2N1_PW_SHIFT},
	{.widget = widget_mic2p2, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC2P2_PW_SHIFT},
	{.widget = widget_mic2n2, .reg = AB5500_VIRTUAL_REG2,
	 .shift = MIC2N2_PW_SHIFT},

	{.widget = widget_clock, .reg = CLOCK, .shift = CLOCK_ENABLE_SHIFT},
};

DEFINE_MUTEX(ab5500_pm_mutex);

static struct {
	enum enum_widget stack[number_of_widgets];
	int p;
} pm_stack;

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
static const struct snd_soc_dapm_widget ab5500_dapm_widgets[] = {
};

static const struct snd_soc_dapm_route intercon[] = {
};


struct ab5500_codec_dai_data  ab5500_codec_privates[NO_CODEC_DAI_IF] = {
	{
		.playback_active = false,
		.capture_active = false,
	},
	{
		.playback_active = false,
		.capture_active = false,
	}
};

static const char *enum_rx_input_select[] = {
	"Mute", "TX1", "TX2", "I2S0_DLD_L",
	"I2S0_DLD_R", "I2S1_DLD_L", "I2S1_DLD_R"
};

static const char *enum_i2s_uld_select[] = {
	"Mute", "TX1", "TX2", "I2S0_DLD_L",
	"I2S0_DLD_R", "I2S1_DLD_L", "I2S1_DLD_R", "tri-state"
};
static const char *enum_apga1_source[] = {"LINEIN1", "MIC1", "MIC2", "None"};
static const char *enum_apga2_source[] = {"LINEIN2", "MIC1", "MIC2", "None"};
static const char *enum_rx_side_tone[] = {"TX1", "TX2"};
static const char *enum_dac_power_mode[] = {"100%", "75%", "55%"};
static const char *enum_ear_power_mode[] = {"100%", "70%", "50%"};
static const char *enum_auxo_power_mode[] = {
	"100%", "67%", "50%", "25%", "auto"
};
static const char *enum_onoff[] = {"Off", "On"};
static const char *enum_mbias_pdn_imp[] = {"GND", "HiZ"};
static const char *enum_mbias2_out_v[] = {"2.0v", "2.2v"};
static const char *enum_mic_in_imp[] = {
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
static const char *enum_tx1_input_select[] = {
	"ADC1", "DIGMIC1", "DIGMIC2"
};
static const char *enum_tx2_input_select[] = {
	"ADC2", "DIGMIC1", "DIGMIC2"
};
static const char *enum_signal_inversion[] = {"normal", "inverted"};
static const char *enum_spkr1_mode[] = {
	"SPKR1 power down", "Vibra PWM", "class D amplifier", "class AB amplifier"
};
static const char *enum_spkr2_mode[] = {
	"Vibra PWM", "class D amplifier",
};
static const char *enum_pwm_pol[] = {
	"GND", "VDD"
};
/* RX1 Input Select */
static struct soc_enum soc_enum_rx1_in_sel =
	SOC_ENUM_SINGLE(RX1, RXx_DATA_SHIFT,
			ARRAY_SIZE(enum_rx_input_select),
			enum_rx_input_select);

/* RX2 Input Select */
static struct soc_enum soc_enum_rx2_in_sel =
	SOC_ENUM_SINGLE(RX2, RXx_DATA_SHIFT,
			ARRAY_SIZE(enum_rx_input_select),
			enum_rx_input_select);
/* RX3 Input Select */
static struct soc_enum soc_enum_rx3_in_sel =
	SOC_ENUM_SINGLE(RX3, RXx_DATA_SHIFT,
			ARRAY_SIZE(enum_rx_input_select),
			enum_rx_input_select);
/* TX1 Input Select */
static struct soc_enum soc_enum_tx1_in_sel =
	SOC_ENUM_SINGLE(TX1, TXx_MUX_SHIFT,
			ARRAY_SIZE(enum_tx1_input_select),
			enum_tx1_input_select);
/* TX2 Input Select */
static struct soc_enum soc_enum_tx2_in_sel =
	SOC_ENUM_SINGLE(TX2, TXx_MUX_SHIFT,
			ARRAY_SIZE(enum_tx2_input_select),
			enum_tx2_input_select);

/* I2S0 ULD Select */
static struct soc_enum soc_enum_i2s0_input_select =
	SOC_ENUM_DOUBLE(INTERFACE0_ULD, 0, 4,
			ARRAY_SIZE(enum_i2s_uld_select),
			enum_i2s_uld_select);
/* I2S1 ULD Select */
static struct soc_enum soc_enum_i2s1_input_select =
	SOC_ENUM_DOUBLE(INTERFACE1_ULD, 0, 4,
			ARRAY_SIZE(enum_i2s_uld_select),
			enum_i2s_uld_select);

/* APGA1 Source */
static struct soc_enum soc_enum_apga1_source =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA1, APGAx_MUX_SHIFT,
			ARRAY_SIZE(enum_apga1_source),
			enum_apga1_source);

/* APGA2 Source */
static struct soc_enum soc_enum_apga2_source =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA2, APGAx_MUX_SHIFT,
			ARRAY_SIZE(enum_apga2_source),
			enum_apga2_source);

static struct soc_enum soc_enum_apga1_enable =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA1, APGAx_PWR_SHIFT,
			ARRAY_SIZE(enum_onoff), enum_onoff);

static struct soc_enum soc_enum_apga2_enable =
	SOC_ENUM_SINGLE(ANALOG_LOOP_PGA2, APGAx_PWR_SHIFT,
			ARRAY_SIZE(enum_onoff), enum_onoff);

/* RX1 Side Tone */
static struct soc_enum soc_enum_dac1_side_tone =
	SOC_ENUM_SINGLE(ST1_PGA, STx_MUX_SHIFT,
			ARRAY_SIZE(enum_rx_side_tone),
			enum_rx_side_tone);

/* RX2 Side Tone */
static struct soc_enum soc_enum_dac2_side_tone =
	SOC_ENUM_SINGLE(ST2_PGA, STx_MUX_SHIFT,
			ARRAY_SIZE(enum_rx_side_tone),
			enum_rx_side_tone);

/* DAC1 Power Mode */
static struct soc_enum soc_enum_dac1_power_mode =
	SOC_ENUM_SINGLE(RX1, DACx_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_dac_power_mode),
			enum_dac_power_mode);

/* DAC2 Power Mode */
static struct soc_enum soc_enum_dac2_power_mode =
	SOC_ENUM_SINGLE(RX2, DACx_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_dac_power_mode),
			enum_dac_power_mode);

/* DAC3 Power Mode */
static struct soc_enum soc_enum_dac3_power_mode =
	SOC_ENUM_SINGLE(RX3, DACx_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_dac_power_mode),
			enum_dac_power_mode);

/* EAR Power Mode */
static struct soc_enum soc_enum_ear_power_mode =
	SOC_ENUM_SINGLE(EAR_PWR, EAR_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_ear_power_mode),
			enum_ear_power_mode);

/* AUXO12 Power Mode */
static struct soc_enum soc_enum_auxo12_power_mode =
	SOC_ENUM_SINGLE(AUXO12_PWR_MODE, AUXOxy_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_auxo_power_mode),
			enum_auxo_power_mode);

/* AUXO34 Power Mode */
static struct soc_enum soc_enum_auxo34_power_mode =
	SOC_ENUM_SINGLE(AUXO34_PWR_MODE, AUXOxy_PWR_MODE_SHIFT,
			ARRAY_SIZE(enum_auxo_power_mode),
			enum_auxo_power_mode);

/* MBIAS1 PDN Impedance */
static struct soc_enum soc_enum_mbias1_pdn_imp =
	SOC_ENUM_SINGLE(MIC_BIAS1, MBIASx_PDN_IMP_SHIFT,
			ARRAY_SIZE(enum_mbias_pdn_imp),
			enum_mbias_pdn_imp);

/* MBIAS2 PDN Impedance */
static struct soc_enum soc_enum_mbias2_pdn_imp =
	SOC_ENUM_SINGLE(MIC_BIAS2, MBIASx_PDN_IMP_SHIFT,
			ARRAY_SIZE(enum_mbias_pdn_imp),
			enum_mbias_pdn_imp);

/* MBIAS2 Output voltage */
static struct soc_enum soc_enum_mbias2_out_v =
	SOC_ENUM_SINGLE(MIC_BIAS2, MBIAS2_OUT_V_SHIFT,
			ARRAY_SIZE(enum_mbias2_out_v),
			enum_mbias2_out_v);

static struct soc_enum soc_enum_mbias2_int_r =
	SOC_ENUM_SINGLE(MIC_BIAS2_VAD, MBIAS2_R_INT_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_mic1_in_imp =
	SOC_ENUM_SINGLE(MIC1_GAIN, MICx_IN_IMP_SHIFT,
			ARRAY_SIZE(enum_mic_in_imp),
			enum_mic_in_imp);

static struct soc_enum soc_enum_mic2_in_imp =
	SOC_ENUM_SINGLE(MIC2_GAIN, MICx_IN_IMP_SHIFT,
			ARRAY_SIZE(enum_mic_in_imp),
			enum_mic_in_imp);

static struct soc_enum soc_enum_tx1_hp_filter =
	SOC_ENUM_SINGLE(TX1, TXx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_tx2_hp_filter =
	SOC_ENUM_SINGLE(TX2, TXx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_st1_hp_filter =
	SOC_ENUM_SINGLE(ST1_PGA, STx_HP_FILTER_SHIFT,
			ARRAY_SIZE(enum_hp_filter),
			enum_hp_filter);

static struct soc_enum soc_enum_st2_hp_filter =
	SOC_ENUM_SINGLE(ST2_PGA, STx_HP_FILTER_SHIFT,
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
	SOC_ENUM_SINGLE(AUXO2, AUXOx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_auxo3_inversion =
	SOC_ENUM_SINGLE(AUXO3, AUXOx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_auxo4_inversion =
	SOC_ENUM_SINGLE(AUXO4, AUXOx_INV_SHIFT,
			ARRAY_SIZE(enum_signal_inversion),
			enum_signal_inversion);

static struct soc_enum soc_enum_auxo1_pulldown_resistor =
	SOC_ENUM_SINGLE(AUXO1, AUXOx_PULLDOWN_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_auxo2_pulldown_resistor =
	SOC_ENUM_SINGLE(AUXO2, AUXOx_PULLDOWN_SHIFT,
			ARRAY_SIZE(enum_optional_resistor),
			enum_optional_resistor);

static struct soc_enum soc_enum_spkr1_mode =
	SOC_ENUM_SINGLE(SPKR1, SPKRx_PWR_SHIFT,
			ARRAY_SIZE(enum_spkr1_mode),
			enum_spkr1_mode);

static struct soc_enum soc_enum_spkr2_mode =
	SOC_ENUM_SINGLE(AB5500_VIRTUAL_REG3, SPKR2_MODE_SHIFT,
			ARRAY_SIZE(enum_spkr2_mode),
			enum_spkr2_mode);

static struct soc_enum soc_enum_pwm_spkr1n_pol =
	SOC_ENUM_SINGLE(PWMCTRL_SPKR1, PWMCTRL_SPKRx_N1_POL_SHIFT,
			ARRAY_SIZE(enum_pwm_pol), enum_pwm_pol);

static struct soc_enum soc_enum_pwm_spkr1p_pol =
	SOC_ENUM_SINGLE(PWMCTRL_SPKR1, PWMCTRL_SPKRx_P1_POL_SHIFT,
			ARRAY_SIZE(enum_pwm_pol), enum_pwm_pol);

static struct soc_enum soc_enum_pwm_spkr2n_pol =
	SOC_ENUM_SINGLE(PWMCTRL_SPKR2, PWMCTRL_SPKRx_N1_POL_SHIFT,
			ARRAY_SIZE(enum_pwm_pol), enum_pwm_pol);

static struct soc_enum soc_enum_pwm_spkr2p_pol =
	SOC_ENUM_SINGLE(PWMCTRL_SPKR2, PWMCTRL_SPKRx_P1_POL_SHIFT,
			ARRAY_SIZE(enum_pwm_pol), enum_pwm_pol);

static struct snd_kcontrol_new ab5500_snd_controls[] = {
	/* RX Routing */
	SOC_ENUM("RX1 Input Select",	soc_enum_rx1_in_sel),
	SOC_ENUM("RX2 Input Select",	soc_enum_rx2_in_sel),
	SOC_ENUM("RX3 Input Select",	soc_enum_rx3_in_sel),
	SOC_SINGLE("LINE1 Adder", LINE1_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("LINE2 Adder", LINE2_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("EAR Adder", EAR_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("SPKR1 Adder", SPKR1_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("SPKR2 Adder", SPKR2_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("AUXO1 Adder", AUXO1_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("AUXO2 Adder", AUXO2_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("AUXO3 Adder", AUXO3_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("AUXO4 Adder", AUXO4_ADDER, 0, 0x1F, 0),
	SOC_SINGLE("SPKR1 PWM Select", AB5500_VIRTUAL_REG5, 0, 0x03, 0),
	SOC_SINGLE("SPKR2 PWM Select", AB5500_VIRTUAL_REG5, 2, 0x0C, 0),
	/* TX Routing */
	SOC_ENUM("TX1 Input Select",	soc_enum_tx1_in_sel),
	SOC_ENUM("TX2 Input Select",	soc_enum_tx2_in_sel),
	SOC_SINGLE("MIC1 Input Select",	MIC1_INPUT_SELECT, 0, 0xff, 0),
	SOC_SINGLE("MIC2 Input Select",	MIC2_INPUT_SELECT, 0, 0xff, 0),
	SOC_SINGLE("MIC2 to MIC1", MIC2_TO_MIC1, 0, 0x03, 0),
	SOC_ENUM("I2S0 Input Select",	soc_enum_i2s0_input_select),
	SOC_ENUM("I2S1 Input Select",	soc_enum_i2s1_input_select),
	/* Routing of Side Tone and Analop Loop */
	SOC_ENUM("APGA1 Source", soc_enum_apga1_source),
	SOC_ENUM("APGA2 Source", soc_enum_apga2_source),
	SOC_ENUM("APGA1 Enable", soc_enum_apga1_enable),
	SOC_ENUM("APGA2 Enable", soc_enum_apga2_enable),
	SOC_ENUM("DAC1 Side Tone",		soc_enum_dac1_side_tone),
	SOC_ENUM("DAC2 Side Tone",		soc_enum_dac2_side_tone),
	/* RX Volume Control */
	SOC_SINGLE("RX-DPGA1 Gain",		RX1_DPGA, 0, 0x43, 0),
	SOC_SINGLE("RX-DPGA2 Gain",		RX2_DPGA, 0, 0x43, 0),
	SOC_SINGLE("RX-DPGA3 Gain",		RX3_DPGA, 0, 0x43, 0),
	SOC_SINGLE("LINE1 Gain", LINE1, LINEx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("LINE2 Gain", LINE2, LINEx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("SPKR1 Gain",	SPKR1, SPKRx_GAIN_SHIFT, 0x16, 0),
	SOC_SINGLE("SPKR2 Gain",	SPKR2, SPKRx_GAIN_SHIFT, 0x16, 0),
	SOC_SINGLE("EAR Gain", EAR_GAIN, EAR_GAIN_SHIFT, 0x12, 0),
	SOC_SINGLE("AUXO1 Gain", AUXO1, AUXOx_GAIN_SHIFT, 0x0c, 0),
	SOC_SINGLE("AUXO2 Gain", AUXO2, AUXOx_GAIN_SHIFT, 0x0c, 0),
	SOC_SINGLE("AUXO3 Gain", AUXO3, AUXOx_GAIN_SHIFT, 0x0c, 0),
	SOC_SINGLE("AUXO4 Gain", AUXO4, AUXOx_GAIN_SHIFT, 0x0c, 0),
	/* TX Volume Control */
	SOC_SINGLE("MIC1 Gain", MIC1_GAIN, MICx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("MIC2 Gain",	MIC2_GAIN, MICx_GAIN_SHIFT, 0x0a, 0),
	SOC_SINGLE("TX-DPGA1 Gain", TX_DPGA1, TX_DPGAx_SHIFT, 0x0f, 0),
	SOC_SINGLE("TX-DPGA2 Gain", TX_DPGA2, TX_DPGAx_SHIFT, 0x0f, 0),
	/* Volume Control of Side Tone and Analog Loop */
	SOC_SINGLE("ST-PGA1 Gain", ST1_PGA, STx_PGA_SHIFT, 0x0a, 0),
	SOC_SINGLE("ST-PGA2 Gain", ST2_PGA, STx_PGA_SHIFT, 0x0a, 0),
	SOC_SINGLE("APGA1 Gain", ANALOG_LOOP_PGA1, APGAx_GAIN_SHIFT, 0x1d, 0),
	SOC_SINGLE("APGA2 Gain", ANALOG_LOOP_PGA2, APGAx_GAIN_SHIFT, 0x1d, 0),
	/* RX Properties */
	SOC_ENUM("DAC1 Power Mode",		soc_enum_dac1_power_mode),
	SOC_ENUM("DAC2 Power Mode",		soc_enum_dac2_power_mode),
	SOC_ENUM("DAC3 Power Mode",		soc_enum_dac3_power_mode),
	SOC_ENUM("EAR Power Mode",		soc_enum_ear_power_mode),
	SOC_ENUM("AUXO12 Power Mode",		soc_enum_auxo12_power_mode),
	SOC_ENUM("AUXO34 Power Mode",		soc_enum_auxo34_power_mode),
	SOC_ENUM("LINE1 Inversion",		soc_enum_line1_inversion),
	SOC_ENUM("LINE2 Inversion",		soc_enum_line2_inversion),
	SOC_ENUM("AUXO1 Inversion",		soc_enum_auxo1_inversion),
	SOC_ENUM("AUXO2 Inversion",		soc_enum_auxo2_inversion),
	SOC_ENUM("AUXO3 Inversion",		soc_enum_auxo3_inversion),
	SOC_ENUM("AUXO4 Inversion",		soc_enum_auxo4_inversion),
	SOC_ENUM("AUXO1 Pulldown Resistor", soc_enum_auxo1_pulldown_resistor),
	SOC_ENUM("AUXO2 Pulldown Resistor", soc_enum_auxo2_pulldown_resistor),
	SOC_ENUM("SPKR1 Mode", soc_enum_spkr1_mode),
	SOC_ENUM("SPKR2 Mode", soc_enum_spkr2_mode),
	SOC_ENUM("PWM SPKR1N POL", soc_enum_pwm_spkr1n_pol),
	SOC_ENUM("PWM SPKR1P POL", soc_enum_pwm_spkr1p_pol),
	SOC_ENUM("PWM SPKR2N POL", soc_enum_pwm_spkr2n_pol),
	SOC_ENUM("PWM SPKR2P POL", soc_enum_pwm_spkr2p_pol),
	/* TX Properties */
	SOC_SINGLE("MIC1 VMID",	MIC1_VMID_SELECT, 0, 0x3f, 0),
	SOC_SINGLE("MIC2 VMID",	MIC2_VMID_SELECT, 0, 0x3f, 0),
	SOC_ENUM("MBIAS1 PDN Impedance",	soc_enum_mbias1_pdn_imp),
	SOC_ENUM("MBIAS2 PDN Impedance",	soc_enum_mbias2_pdn_imp),
	SOC_ENUM("MBIAS2 Output Voltage", soc_enum_mbias2_out_v),
	SOC_ENUM("MBIAS2 Internal Resistor", soc_enum_mbias2_int_r),
	SOC_ENUM("MIC1 Input Impedance",	soc_enum_mic1_in_imp),
	SOC_ENUM("MIC2 Input Impedance",	soc_enum_mic2_in_imp),
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
	SOC_SINGLE("Interface Swap",		INTERFACE_SWAP, 0, 0x03, 0),
	/* Miscellaneous */
	SOC_SINGLE("Negative Charge Pump", NEG_CHARGE_PUMP, 0, 0x03, 0)
};

/* count the number of 1 */
#define count_ones(x) ({						\
			int num;					\
			typeof(x) y = x;				\
			for (num = 0; y; y &= y - 1, num++)		\
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

static void power_widget_unlocked(enum enum_power onoff, enum enum_widget widget)
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
		dev_dbg(ab5500_dev, "%s: processing widget %s.\n",
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
		dev_dbg(ab5500_dev, "%s: widget %s powered %s.\n",
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
	if (mutex_lock_interruptible(&ab5500_pm_mutex)) {
		dev_warn(ab5500_dev,
			 "%s: Signal received while waiting on the PM mutex.\n",
			 __func__);
		return;
	}
	power_widget_unlocked(onoff, widget);
	mutex_unlock(&ab5500_pm_mutex);
}


static void dump_registers(const char *where, ...)
{
	va_list ap;
	va_start(ap, where);
	do {
		short reg = va_arg(ap, int);
		if (reg < 0)
			break;
		dev_dbg(ab5500_dev, "%s from %s> 0x%02X : 0x%02X.\n",
			__func__, where, reg, read_reg(reg));
	} while (1);
	va_end(ap);
}

/**
 * update the link two widgets.
 * @op: 1 - connect; 0 - disconnect
 * @src: source of the connection
 * @sink: sink of the connection
 */
static int update_widgets_link(enum enum_link op, enum enum_widget src,
			       enum enum_widget sink,
			       u8 reg, u8 mask, u8 newval)
{
	int ret = 0;

	if (mutex_lock_interruptible(&ab5500_pm_mutex)) {
		dev_warn(ab5500_dev, "%s: A signal is received while waiting on"
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
	mutex_unlock(&ab5500_pm_mutex);
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
	case AUXO3_ADDER:
		return widget_auxo3;
	case AUXO4_ADDER:
		return widget_auxo4;
	case SPKR1_ADDER:
		return widget_spkr1;
	case SPKR2_ADDER:
		return widget_spkr2;
	case LINE1_ADDER:
		return widget_line1;
	case LINE2_ADDER:
		return widget_line2;
	default:
		return number_of_widgets;
	}
}

static int ab5500_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(&codec->dapm, ab5500_dapm_widgets,
				  ARRAY_SIZE(ab5500_dapm_widgets));

	snd_soc_dapm_add_routes(&codec->dapm, intercon, ARRAY_SIZE(intercon));

	snd_soc_dapm_new_widgets(&codec->dapm);
	return 0;
}

static void power_for_playback(enum enum_power onoff, int ifsel)
{
	dev_dbg(ab5500_dev, "%s: interface %d power %s.\n", __func__,
		ifsel, onoff == POWER_ON ? "on" : "off");
	if (mutex_lock_interruptible(&ab5500_pm_mutex)) {
		dev_warn(ab5500_dev,
			 "%s: Signal received while waiting on the PM mutex.\n",
			 __func__);
		return;
	}
	mask_set_reg(ENV_THR, ENV_THR_HIGH_MASK, 0x0F << ENV_THR_HIGH_SHIFT);
	mask_set_reg(ENV_THR, ENV_THR_LOW_MASK, 0x00 << ENV_THR_LOW_SHIFT);
	mask_set_reg(DC_CANCEL, DC_CANCEL_AUXO12_MASK,
		0x01 << DC_CANCEL_AUXO12_SHIFT);

	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_dld_l : widget_if1_dld_l);
	power_widget_unlocked(onoff, ifsel == 0 ?
			widget_if0_dld_r : widget_if1_dld_r);

	mutex_unlock(&ab5500_pm_mutex);
}

static int enable_regulator(enum regulator_idx idx)
{
	int ret;

	if (reg_enabled[idx])
		return 0;

	ret = regulator_enable(reg_info[idx].consumer);
	if (ret != 0) {
		pr_err("%s: Failure to enable regulator '%s' (ret = %d)\n",
			__func__, reg_info[idx].supply, ret);
		return ret;
	};

	reg_enabled[idx] = true;
	pr_debug("%s: Enabled regulator '%s', status: %d, %d\n",
		__func__,
		reg_info[idx].supply,
		(int)reg_enabled[0],
		(int)reg_enabled[1]);
	return 0;
}

static void disable_regulator(enum regulator_idx idx)
{
	if (!reg_enabled[idx])
		return;

	regulator_disable(reg_info[idx].consumer);

	reg_enabled[idx] = false;
	pr_debug("%s: Disabled regulator '%s', status: %d, %d\n",
		__func__,
		reg_info[idx].supply,
		(int)reg_enabled[0],
		(int)reg_enabled[1]);
}

static void power_for_capture(enum enum_power onoff, int ifsel)
{
	int err;
	int mask;

	dev_info(ab5500_dev, "%s: interface %d power %s", __func__,
		ifsel, onoff == POWER_ON ? "on" : "off");
	if (mutex_lock_interruptible(&ab5500_pm_mutex)) {
		dev_warn(ab5500_dev,
			 "%s: Signal received while waiting on the PM mutex.\n",
			 __func__);
		return;
	}
	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_uld_l : widget_if1_uld_l);
	power_widget_unlocked(onoff, ifsel == 0 ?
			      widget_if0_uld_r : widget_if1_uld_r);

	mask = (read_reg(TX2) & TXx_MUX_MASK) >> TXx_MUX_SHIFT;

		switch (onoff << 2 | mask) {
		case 0: /* Power off : Amic */
			disable_regulator(REGULATOR_AMIC);
		break;
		case 1: /* Power off : Dmic */
		case 2:
			disable_regulator(REGULATOR_DMIC);
		break;
		case 4: /* Power on : Amic */
			err = enable_regulator(REGULATOR_AMIC);
			if (err < 0)
				goto unlock;
		break;
		case 5: /* Power on : Dmic */
		case 6:
			err = enable_regulator(REGULATOR_DMIC);
			if (err < 0)
				goto unlock;
		break;
		default:
			pr_debug("%s : Not a valid regulator combination\n",
					__func__);
		break;
		}
unlock:
	mutex_unlock(&ab5500_pm_mutex);
}

static int ab5500_add_controls(struct snd_soc_codec *codec)
{
	int err = 0, i, n = ARRAY_SIZE(ab5500_snd_controls);

	pr_info("%s: %s called.\n", __FILE__, __func__);
	for (i = 0; i < n; i++) {
		err = snd_ctl_add(codec->card->snd_card, snd_ctl_new1(
					  &ab5500_snd_controls[i], codec));
		if (err < 0) {
			pr_err("%s failed to add control No.%d of %d.\n",
			       __func__, i, n);
			return err;
		}
	}
	return err;
}

static int ab5500_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
	    dai->playback_active : dai->capture_active) {
		dev_err(dai->dev, "A %s stream is already active.\n",
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			"playback" : "capture");
		return -EBUSY;
	}
	return 0;
}

static int ab5500_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params,
				struct snd_soc_dai *dai)
{
	u8 val;
	u8 reg = dai->id == 0 ? INTERFACE0 : INTERFACE1;

	if (!ab5500_dev) {
		pr_err("%s: The AB5500 codec driver not initialized.\n",
		       __func__);
		return -EAGAIN;
	}
	dev_info(ab5500_dev, "%s called.\n", __func__);
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

static int ab5500_pcm_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	u8 value = (dai->id == 1) ? INTERFACE1 : INTERFACE0;

	dev_info(ab5500_dev, "%s called.\n", __func__);

	/* Configure registers for either playback or capture */
	if ((substream->stream == SNDRV_PCM_STREAM_PLAYBACK) &&
		!(ab5500_codec_privates[dai->id].playback_active == true)) {
		power_for_playback(POWER_ON, dai->id);
		ab5500_codec_privates[dai->id].playback_active = true;
		mask_set_reg(value, I2Sx_TRISTATE_MASK,
						0 << I2Sx_TRISTATE_SHIFT);
	} else if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) &&
		!(ab5500_codec_privates[dai->id].capture_active == true)) {
		power_for_capture(POWER_ON, dai->id);
		ab5500_codec_privates[dai->id].capture_active = true;
		mask_set_reg(value, I2Sx_TRISTATE_MASK,
						0 << I2Sx_TRISTATE_SHIFT);

	}
	mutex_lock(&ab5500_clk_mutex);
	ab5500_clk_request++;
	if (ab5500_clk_request == 1)
		mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 1 << CLOCK_ENABLE_SHIFT);
	mutex_unlock(&ab5500_clk_mutex);

	dump_registers(__func__, RX1, AUXO1_ADDER, RX2,
			AUXO2_ADDER, RX1_DPGA, RX2_DPGA, AUXO1, AUXO2, -1);

	return 0;
}

static void ab5500_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	u8 iface = (dai->id == 0) ? INTERFACE0 : INTERFACE1;
	dev_info(ab5500_dev, "%s called.\n", __func__);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		power_for_playback(POWER_OFF, dai->id);
		ab5500_codec_privates[dai->id].playback_active = false;
	} else {
		power_for_capture(POWER_OFF, dai->id);
		ab5500_codec_privates[dai->id].capture_active = false;
	}
	if (!dai->playback_active && !dai->capture_active &&
	    (read_reg(iface) & I2Sx_MODE_MASK) == 0) {
		mask_set_reg(iface, I2Sx_TRISTATE_MASK,
					1 << I2Sx_TRISTATE_SHIFT);
		mask_set_reg(iface, MASTER_GENx_PWR_MASK, 0);
		}
	mutex_lock(&ab5500_clk_mutex);
	ab5500_clk_request--;
	if (ab5500_clk_request == 0)
		mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0 << CLOCK_ENABLE_SHIFT);
	mutex_unlock(&ab5500_clk_mutex);
}

static int ab5500_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	return 0;
}

static int ab5500_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	u8 iface = (codec_dai->id == 0) ? INTERFACE0 : INTERFACE1;
	u8 val = 0;
	dev_info(ab5500_dev, "%s called.\n", __func__);

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_MASTER_MASK)) {

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
		val |= 1 << I2Sx_MODE_SHIFT;
		mask_set_reg(iface, I2Sx_MODE_MASK, val);
		break;

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
		break;

	default:
		dev_warn(ab5500_dev, "AB5500_dai: unsupported DAI format "
			 "0x%x\n", fmt);
		return -EINVAL;
	}
	if (codec_dai->playback_active && codec_dai->capture_active) {
		if ((read_reg(iface) & I2Sx_MODE_MASK) == val)
			return 0;
		else {
			dev_err(ab5500_dev,
				"%s: DAI format set differently "
				"by an existing stream.\n", __func__);
			return -EINVAL;
		}
	}
	mask_set_reg(iface, I2Sx_MODE_MASK, val);
	return 0;
}

struct snd_soc_dai_driver ab5500_dai_drv[] = {
	{
		.name = "ab5500-codec-dai.0",
		.id = 0,
		.playback = {
			.stream_name = "ab5500.0 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB5500_SUPPORTED_RATE,
			.formats = AB5500_SUPPORTED_FMT,
		},
		.capture = {
			.stream_name = "ab5500.0 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB5500_SUPPORTED_RATE,
			.formats = AB5500_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab5500_pcm_startup,
				.prepare = ab5500_pcm_prepare,
				.hw_params = ab5500_pcm_hw_params,
				.shutdown = ab5500_pcm_shutdown,
				.set_sysclk = ab5500_set_dai_sysclk,
				.set_fmt = ab5500_set_dai_fmt,
			}
		},
		.symmetric_rates = 1,
	},
	{
		.name = "ab5500-codec-dai.1",
		.id = 1,
		.playback = {
			.stream_name = "ab5500.1 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB5500_SUPPORTED_RATE,
			.formats = AB5500_SUPPORTED_FMT,
		},
		.capture = {
			.stream_name = "ab5500.1 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = AB5500_SUPPORTED_RATE,
			.formats = AB5500_SUPPORTED_FMT,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.startup = ab5500_pcm_startup,
				.prepare = ab5500_pcm_prepare,
				.hw_params = ab5500_pcm_hw_params,
				.shutdown = ab5500_pcm_shutdown,
				.set_sysclk = ab5500_set_dai_sysclk,
				.set_fmt = ab5500_set_dai_fmt,
			}
		},
		.symmetric_rates = 1,
	}
};

static int ab5500_codec_probe(struct snd_soc_codec *codec)
{
	int ret = ab5500_add_controls(codec);
	if (ret < 0)
		return ret;
	ab5500_add_widgets(codec);
	return 0;
}

static int ab5500_codec_remove(struct snd_soc_codec *codec)
{
	snd_soc_dapm_free(&codec->dapm);
	return 0;
}

#ifdef CONFIG_PM
static int ab5500_codec_suspend(struct snd_soc_codec *codec,
				pm_message_t state)
{
	if (!ab5500_clk_request)
		mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0);
	return 0;
}

static int ab5500_codec_resume(struct snd_soc_codec *codec)
{
	if (ab5500_clk_request)
		mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0xff);
	return 0;
}
#else
#define ab5500_codec_resume NULL
#define ab5500_codec_suspend NULL
#endif

/**
   This function is only called by the SOC framework to
   set registers associated to the mixer controls.
*/
static int ab5500_codec_write_reg(struct snd_soc_codec *codec,
				  unsigned int reg, unsigned int value)
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

		/*
		 * The APGA is to be turned on/off. The power bit and the
		 * other bits in the same register won't be changed at the
		 * same time since they belong to different controls.
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

	case AUXO3_ADDER:
	case AUXO4_ADDER:
	case SPKR2_ADDER:
	case LINE1_ADDER:
	case LINE2_ADDER: {
		int i;
		enum enum_widget widgets[] = {
			widget_dac1, widget_dac2, widget_dac3,
			widget_apga1, widget_apga2
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
	case AB5500_VIRTUAL_REG3:
		oldval = read_reg(reg);
		diff = value ^ oldval;
		/*
		 * The following changes won't take place in the same call,
		 * since they are arranged into different mixer controls.
		 */

		/* changed between the two amplifier modes */
		if (count_ones(diff & SPKR1_MODE_MASK) == 2) {
			set_reg(reg, value);
			break;
		}

		if (diff & SPKR1_MODE_MASK) {
			update_widgets_link(
				UNLINK,
				(oldval & SPKR1_MODE_MASK) == 0 ?
				widget_pwm_spkr1 : widget_spkr1_adder,
				widget_spkr1,
				reg, SPKR1_MODE_MASK, value);
			update_widgets_link(
				LINK,
				(value & SPKR1_MODE_MASK) == 0 ?
				widget_pwm_spkr1 : widget_spkr1_adder,
				widget_spkr1,
				DUMMY_REG, 0, 0);

		}
		if (diff & SPKR2_MODE_MASK) {
			update_widgets_link(
				UNLINK,
				(oldval & SPKR2_MODE_MASK) == 0 ?
				widget_pwm_spkr2 : widget_spkr2_adder,
				widget_spkr2,
				reg, SPKR2_MODE_MASK, value);
			update_widgets_link(
				LINK,
				(value & SPKR2_MODE_MASK) == 0 ?
				widget_pwm_spkr2 : widget_spkr2_adder,
				widget_spkr2,
				DUMMY_REG, 0, 0);

		}
		break;

	case AB5500_VIRTUAL_REG4:
		/* configure PWMCTRL_SPKR1, PWMCTRL_SPKR2, etc. */
		break;
	default:
		set_reg(reg, value);
	}
	return 0;
}

static unsigned int ab5500_codec_read_reg(struct snd_soc_codec *codec,
					  unsigned int reg)
{
	return read_reg(reg);
}


static struct snd_soc_codec_driver ab5500_codec_drv = {
	.probe =	ab5500_codec_probe,
	.remove =	ab5500_codec_remove,
	.suspend =	ab5500_codec_suspend,
	.resume =	ab5500_codec_resume,
	.read =		ab5500_codec_read_reg,
	.write =	ab5500_codec_write_reg,
};
EXPORT_SYMBOL_GPL(ab5500_codec_drv);

static inline void init_playback_route(void)
{
	/* if0_dld_l -> rx1 -> dac1 -> auxo1 */
	update_widgets_link(LINK, widget_if0_dld_l, widget_rx1, 0, 0, 0);
	update_widgets_link(LINK, widget_rx1, widget_dac1, 0, 0, 0);
	update_widgets_link(LINK, widget_dac1, widget_auxo1, 0, 0, 0);

	/* if0_dld_r -> rx2 -> dac2 -> auxo2 */
	update_widgets_link(LINK, widget_if0_dld_r, widget_rx2, 0, 0, 0);
	update_widgets_link(LINK, widget_rx2, widget_dac2, 0, 0, 0);
	update_widgets_link(LINK, widget_dac2, widget_auxo2, 0, 0, 0);

	/*  Earpiece */
	update_widgets_link(LINK, widget_dac1, widget_ear, 0, 0, 0);

	/* if1_dld_l -> rx3 -> dac3 -> spkr1 */
	update_widgets_link(LINK, widget_if1_dld_l, widget_rx3, 0, 0, 0);
	update_widgets_link(LINK, widget_rx3, widget_dac3, 0, 0, 0);
	update_widgets_link(LINK, widget_dac3, widget_spkr1, 0, 0, 0);

}

static inline void init_capture_route(void)
{
	/* mic bias - > mic2 inputs */
	update_widgets_link(LINK, widget_micbias1, widget_mic2p2, 0, 0, 0);
	update_widgets_link(LINK, widget_micbias1, widget_mic2n2, 0, 0, 0);

	/* mic2 inputs -> mic2 */
	update_widgets_link(LINK, widget_mic2p2, widget_mic2, 0, 0, 0);
	update_widgets_link(LINK, widget_mic2n2, widget_mic2, 0, 0, 0);

	/* mic2 -> adc2 -> tx2 */
	update_widgets_link(LINK, widget_mic2, widget_adc2, 0, 0, 0);
	update_widgets_link(LINK, widget_adc2, widget_tx2, 0, 0, 0);

	/* tx2 -> if0_uld_l & if0_uld_r */
	update_widgets_link(LINK, widget_tx2, widget_if0_uld_l, 0, 0, 0);
	update_widgets_link(LINK, widget_tx2, widget_if0_uld_r, 0, 0, 0);
}

static int create_regulators(void)
{
	int i, status = 0;

	pr_debug("%s: Enter.\n", __func__);

	for (i = 0; i < ARRAY_SIZE(reg_info); ++i)
		reg_info[i].consumer = NULL;

	for (i = 0; i < ARRAY_SIZE(reg_info); ++i) {
		reg_info[i].consumer = regulator_get(ab5500_dev,
							reg_info[i].supply);
		if (IS_ERR(reg_info[i].consumer)) {
			status = PTR_ERR(reg_info[i].consumer);
			pr_err("%s: ERROR: Failed to get regulator '%s' (ret = %d)!\n",
				__func__, reg_info[i].supply, status);
			reg_info[i].consumer = NULL;
			goto err_get;
		}
	}

	return 0;

err_get:

	for (i = 0; i < ARRAY_SIZE(reg_info); ++i) {
		if (reg_info[i].consumer) {
			regulator_put(reg_info[i].consumer);
			reg_info[i].consumer = NULL;
		}
	}

	return status;
}

static int __devinit ab5500_platform_probe(struct platform_device *pdev)
{
	int ret = 0;
	u8 reg;
	struct ab5500_codec_dai_data *codec_drvdata;
	int status;

	pr_info("%s invoked with pdev = %p.\n", __func__, pdev);
	ab5500_dev = &pdev->dev;

	status = create_regulators();
	if (status < 0) {
		pr_err("%s: ERROR: Failed to instantiate regulators (ret = %d)!\n",
			__func__, status);
		return status;
	}
	codec_drvdata = kzalloc(sizeof(struct ab5500_codec_dai_data),
				GFP_KERNEL);
	if (codec_drvdata == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, codec_drvdata);
	ret = snd_soc_register_codec(ab5500_dev, &ab5500_codec_drv,
				     ab5500_dai_drv,
				     ARRAY_SIZE(ab5500_dai_drv));
	if (ret < 0) {
		dev_err(ab5500_dev, "%s: Failed to register codec. "
			"Error %d.\n", __func__, ret);
		snd_soc_unregister_codec(ab5500_dev);
		kfree(codec_drvdata);
	}
	/* Initialize the codec registers */
	for (reg = AB5500_FIRST_REG; reg <= AB5500_LAST_REG; reg++)
		set_reg(reg, 0);

	mask_set_reg(INTERFACE0, I2Sx_TRISTATE_MASK, 1 << I2Sx_TRISTATE_SHIFT);
	mask_set_reg(INTERFACE1, I2Sx_TRISTATE_MASK, 1 << I2Sx_TRISTATE_SHIFT);

	printk(KERN_ERR "Clock Setting ab5500\n");
	init_playback_route();
	init_capture_route();
	memset(&pm_stack, 0, sizeof(pm_stack));
	return ret;
}

static int __devexit ab5500_platform_remove(struct platform_device *pdev)
{
	pr_info("%s called.\n", __func__);
	regulator_bulk_free(ARRAY_SIZE(reg_info), reg_info);
	mask_set_reg(CLOCK, CLOCK_ENABLE_MASK, 0);
	snd_soc_unregister_codec(ab5500_dev);
	kfree(platform_get_drvdata(pdev));
	ab5500_dev = NULL;
	return 0;
}

static int ab5500_platform_suspend(struct platform_device *pdev,
				   pm_message_t state)
{
	return 0;
}

static int ab5500_platform_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ab5500_platform_driver = {
	.driver		= {
		.name		= "ab5500-codec",
		.owner		= THIS_MODULE,
	},
	.probe		= ab5500_platform_probe,
	.remove		= ab5500_platform_remove,
	.suspend	= ab5500_platform_suspend,
	.resume		= ab5500_platform_resume,
};

static int __devinit ab5500_init(void)
{
	int ret;

	pr_info("%s called.\n", __func__);

	/*  Register codec platform driver. */
	ret = platform_driver_register(&ab5500_platform_driver);
	if (ret) {
		pr_err("%s: Error %d: Failed to register codec platform "
			 "driver.\n", __func__, ret);
	}
	return ret;
}

static void __devexit ab5500_exit(void)
{
	pr_info("%s called.\n", __func__);
	platform_driver_unregister(&ab5500_platform_driver);
}

module_init(ab5500_init);
module_exit(ab5500_exit);

MODULE_DESCRIPTION("AB5500 Codec driver");
MODULE_AUTHOR("Xie Xiaolei <xie.xiaolei@stericsson.com>");
MODULE_LICENSE("GPL");
