/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Vijaya Kumar K <vijay.kilari@stericsson.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>

/*
 * Manual mode ADC registers
 */
#define AB5500_GPADC_MANUAL_STAT_REG		0x1F
#define AB5500_GPADC_MANDATAL_REG		0x21
#define AB5500_GPADC_MANDATAH_REG		0x20
#define AB5500_GPADC_MANUAL_MUX_CTRL		0x22
#define AB5500_GPADC_MANUAL_MODE_CTRL		0x23
#define AB5500_GPADC_MANUAL_MODE_CTRL2		0x24
/*
 * Auto/Polling mode ADC registers
 */
#define AB5500_GPADC_AUTO_VBAT_MAX		0x26
#define AB5500_GPADC_AUTO_VBAT_MIN_TXON		0x27
#define AB5500_GPADC_AUTO_VBAT_MIN_NOTX		0x28
#define AB5500_GPADC_AUTO_VBAT_AVGH		0x29
#define AB5500_GPADC_AUTO_VBAT_AVGL		0x2A
#define AB5500_GPADC_AUTO_ICHAR_MAX		0x2B
#define AB5500_GPADC_AUTO_ICHAR_MIN		0x2C
#define AB5500_GPADC_AUTO_ICHAR_AVG		0x2D
#define AB5500_GPADC_AUTO_CTRL2			0x2F
#define AB5500_GPADC_AUTO_CTRL1			0x30
#define AB5500_GPADC_AUTO_PWR_CTRL		0x31
#define AB5500_GPADC_AUTO_TRIG_VBAT_MIN_TXON	0x32
#define AB5500_GPADC_AUTO_TRIG_VBAT_MIN_NOTX	0x33
#define AB5500_GPADC_AUTO_TRIG_ADOUT0_CTRL	0x34
#define AB5500_GPADC_AUTO_TRIG_ADOUT1_CTRL	0x35
#define AB5500_GPADC_AUTO_TRIG0_MUX_CTRL	0x37
#define AB5500_GPADC_AUTO_XTALTEMP_CTRL		0x57
#define AB5500_GPADC_KELVIN_CTRL		0xFE

/* gpadc constants */
#define AB5500_INT_ADC_TRIG0		0x0
#define AB5500_INT_ADC_TRIG1		0x1
#define AB5500_INT_ADC_TRIG2		0x2
#define AB5500_INT_ADC_TRIG3		0x3
#define AB5500_INT_ADC_TRIG4		0x4
#define AB5500_INT_ADC_TRIG5		0x5
#define AB5500_INT_ADC_TRIG6		0x6
#define AB5500_INT_ADC_TRIG7		0x7

#define AB5500_GPADC_AUTO_TRIG_INDEX	AB5500_GPADC_AUTO_TRIG0_MUX_CTRL
#define GPADC_MANUAL_READY		0x01
#define GPADC_MANUAL_ADOUT0_MASK	0x30
#define GPADC_MANUAL_ADOUT1_MASK	0xC0
#define GPADC_MANUAL_ADOUT0_ON		0x10
#define GPADC_MANUAL_ADOUT1_ON		0x40
#define MUX_SCALE_GPADC0_MASK		0x08
#define MUX_SCALE_VBAT_MASK		0x02
#define MUX_SCALE_45			0x02
#define MUX_SCALE_BDATA_MASK		0x01
#define MUX_SCALE_BDATA27		0x00
#define MUX_SCALE_BDATA18		0x01
#define MUX_SCALE_ACCDET2_MASK		0x01
#define MUX_SCALE_ACCDET3_MASK		0x02
#define GPADC0_SCALE_VOL27		0x00
#define GPADC0_SCALE_VOL18		0x01
#define ACCDET2_SCALE_VOL27		0x00
#define ACCDET3_SCALE_VOL27		0x00
#define TRIGX_FREQ_MASK			0x07
#define AUTO_VBAT_MASK			0x10
#define AUTO_VBAT_ON			0x10
#define TRIG_VBAT_TXON_ARM_MASK		0x08
#define TRIG_VBAT_NOTX_ARM_MASK		0x04
#define TRIGX_ARM_MASK			0x20
#define TRIGX_ARM			0x20
#define TRIGX_MUX_SELECT		0x1F
#define ADC_CAL_OFF_MASK		0x04
#define ADC_ON_MODE_MASK		0x03
#define ADC_CAL_ON			0x00
#define ADC_FULLPWR			0x03
#define ADC_XTAL_FORCE_MASK		0x80
#define ADC_XTAL_FORCE_EN		0x80
#define ADC_XTAL_FORCE_DI		0x00
#define ADOUT0				0x01
#define ADOUT1				0x02
#define MIN_INDEX			0x02
#define MAX_INDEX			0x03
#define CTRL_INDEX			0x01
#define KELVIN_SCALE_VOL45		0x00

/* GPADC constants from AB5500 spec  */
#define GPADC0_MIN		0
#define GPADC0_MAX		1800
#define BTEMP_MIN		0
#define BTEMP_MAX		1800
#define BDATA_MIN		0
#define BDATA_MAX		2750
#define PCBTEMP_MIN		0
#define PCBTEMP_MAX		1800
#define XTALTEMP_MIN		0
#define XTALTEMP_MAX		1800
#define DIETEMP_MIN		0
#define DIETEMP_MAX		1800
#define VBUS_I_MIN		0
#define VBUS_I_MAX		1600
#define VBUS_V_MIN		0
#define VBUS_V_MAX		20000
#define ACCDET2_MIN		0
#define ACCDET2_MAX		2500
#define ACCDET3_MIN		0
#define ACCDET3_MAX		2500
#define VBAT_MIN		2300
#define VBAT_MAX		4500
#define BKBAT_MIN		0
#define BKBAT_MAX		2750
#define USBID_MIN		0
#define USBID_MAX		1800
#define KELVIN_MIN		0
#define KELVIN_MAX		4500
#define VIBRA_MIN		0
#define VIBRA_MAX		4500

/* This is used for calibration */
#define ADC_RESOLUTION			1023
#define AUTO_ADC_RESOLUTION		255

/* ADOUT prestart time */
#define ADOUT0_TRIGX_PRESTART		0x18

enum adc_auto_channels {
	ADC_INPUT_TRIG0 = 0,
	ADC_INPUT_TRIG1,
	ADC_INPUT_TRIG2,
	ADC_INPUT_TRIG3,
	ADC_INPUT_TRIG4,
	ADC_INPUT_TRIG5,
	ADC_INPUT_TRIG6,
	ADC_INPUT_TRIG7,
	ADC_INPUT_VBAT_TXOFF,
	ADC_INPUT_VBAT_TXON,
	N_AUTO_TRIGGER
};

/**
 * struct adc_auto_trigger - AB5500 GPADC auto trigger
 * @adc_mux			Mux input
 * @flag			Status of trigger
 * @freq			Frequency of conversion
 * @adout			Adout to pull
 * @trig_min			trigger minimum value
 * @trig_max			trigger maximum value
 * @auto_adc_callback		notification callback
 */
struct adc_auto_trigger {
	u8 auto_mux;
	u8 flag;
	u8 freq;
	u8 adout;
	u8 trig_min;
	u8 trig_max;
	int (*auto_callb)(int mux);
};

/**
 * struct ab5500_btemp_interrupts - ab5500 interrupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab5500_adc_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

/**
 * struct ab5500_gpadc - AB5500 GPADC device information
 * @chip_id			ABB chip id
 * @dev:			pointer to the struct device
 * @node:			a list of AB5500 GPADCs, hence prepared for
				reentrance
 * @ab5500_gpadc_complete:	pointer to the struct completion, to indicate
 *				the completion of gpadc conversion
 * @ab5500_gpadc_lock:		structure of type mutex
 * @regu:			pointer to the struct regulator
 * @irq:			interrupt number that is used by gpadc
 * @cal_data			array of ADC calibration data structs
 * @auto_trig			auto trigger channel
 * @gpadc_trigX_work		work items for trigger channels
 */
struct ab5500_gpadc {
	u8 chip_id;
	struct device *dev;
	struct list_head node;
	struct mutex ab5500_gpadc_lock;
	struct regulator *regu;
	int irq;
	int prev_bdata;
	spinlock_t gpadc_auto_lock;
	struct adc_auto_trigger adc_trig[N_AUTO_TRIGGER];
	struct workqueue_struct *gpadc_wq;
	struct work_struct gpadc_trig0_work;
	struct work_struct gpadc_trig1_work;
	struct work_struct gpadc_trig2_work;
	struct work_struct gpadc_trig3_work;
	struct work_struct gpadc_trig4_work;
	struct work_struct gpadc_trig5_work;
	struct work_struct gpadc_trig6_work;
	struct work_struct gpadc_trig7_work;
	struct work_struct gpadc_trig_vbat_txon_work;
	struct work_struct gpadc_trig_vbat_txoff_work;
};

static LIST_HEAD(ab5500_gpadc_list);

struct adc_data {
	u8 mux;
	int min;
	int max;
	int adout;
};

#define ADC_DATA(_id, _mux, _min, _max, _adout)	\
	[_id] = {				\
		.mux = _mux,		\
		.min = _min,		\
		.max = _max,		\
		.adout = _adout		\
	}

struct adc_data adc_tab[] = {
	ADC_DATA(GPADC0_V, 0x00, GPADC0_MIN, GPADC0_MAX, 0),
	ADC_DATA(BTEMP_BALL, 0x0D, BTEMP_MIN, BTEMP_MAX, ADOUT0),
	ADC_DATA(BAT_CTRL, 0x0D, BDATA_MIN, BDATA_MAX, 0),
	ADC_DATA(MAIN_BAT_V, 0x0C, VBAT_MIN, VBAT_MAX, 0),
	ADC_DATA(MAIN_BAT_V_TXON, 0x0C, VBAT_MIN, VBAT_MAX, 0),
	ADC_DATA(VBUS_V, 0x10, VBUS_V_MIN, VBUS_V_MAX, 0),
	ADC_DATA(USB_CHARGER_C,	0x0A, VBUS_I_MIN, VBUS_I_MAX, 0),
	ADC_DATA(BK_BAT_V, 0x07, BKBAT_MIN, BKBAT_MAX, 0),
	ADC_DATA(DIE_TEMP, 0x0F, DIETEMP_MIN, DIETEMP_MAX, ADOUT0),
	ADC_DATA(PCB_TEMP, 0x13, PCBTEMP_MIN, PCBTEMP_MAX, ADOUT0),
	ADC_DATA(XTAL_TEMP, 0x06, XTALTEMP_MIN, XTALTEMP_MAX, ADOUT0),
	ADC_DATA(USB_ID, 0x1A, USBID_MIN, USBID_MAX, 0),
	ADC_DATA(ACC_DETECT2, 0x18, ACCDET2_MIN, ACCDET2_MAX, 0),
	ADC_DATA(ACC_DETECT3, 0x19, ACCDET3_MIN, ACCDET3_MAX, 0),
	ADC_DATA(MAIN_BAT_V_TRIG_MIN, 0x0C, VBAT_MIN, VBAT_MAX, 0),
	ADC_DATA(MAIN_BAT_V_TXON_TRIG_MIN, 0x0C, VBAT_MIN, VBAT_MAX, 0),
	ADC_DATA(VIBRA_KELVIN, 0x16, VIBRA_MIN, VIBRA_MAX, 0),
};

/**
 * ab5500_gpadc_get() - returns a reference to the primary AB5500 GPADC
 * (i.e. the first GPADC in the instance list)
 */
struct ab5500_gpadc *ab5500_gpadc_get(const char *name)
{
	struct ab5500_gpadc *gpadc;
	list_for_each_entry(gpadc, &ab5500_gpadc_list, node) {
		if (!strcmp(name, dev_name(gpadc->dev)))
			return gpadc;
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(ab5500_gpadc_get);

#define CONV(min, max, x)\
	((min) + ((((max)-(min))*(x))/ADC_RESOLUTION))

static int ab5500_gpadc_ad_to_voltage(struct ab5500_gpadc *gpadc,
					u8 in, u16 ad_val)
{
	int res;

	switch (in) {
	case VIBRA_KELVIN:
	case GPADC0_V:
	case PCB_TEMP:
	case BTEMP_BALL:
	case MAIN_BAT_V:
	case MAIN_BAT_V_TXON:
	case ACC_DETECT2:
	case ACC_DETECT3:
	case VBUS_V:
	case USB_CHARGER_C:
	case BK_BAT_V:
	case XTAL_TEMP:
	case USB_ID:
	case BAT_CTRL:
		res = CONV(adc_tab[in].min, adc_tab[in].max, ad_val);
		break;
	case DIE_TEMP:
		/*
		 * From the AB5500 product specification
		 * T(deg cel) = 27 - ((ADCode - 709)/2.4213)
		 * 27 + 709/2.4213 - ADCode/2.4213
		 * 320 - (ADCode/2.4213)
		 */
		res = 320 - (((unsigned long)ad_val * 10000) / 24213);
		break;
	default:
		dev_err(gpadc->dev,
			"unknown channel, not possible to convert\n");
		res = -EINVAL;
		break;
	}
	return res;
}

/**
 * ab5500_gpadc_convert() - gpadc conversion
 * @input:	analog input to be converted to digital data
 *
 * This function converts the selected analog i/p to digital
 * data.
 */
int ab5500_gpadc_convert(struct ab5500_gpadc *gpadc, u8 input)
{
	int result, ret = -EINVAL;
	u16 data = 0;
	u8 looplimit = 0;
	u8 status = 0;
	u8 low_data, high_data, adout_mask, adout_val;

	if (!gpadc)
		return -ENODEV;

	mutex_lock(&gpadc->ab5500_gpadc_lock);

	switch (input) {
	case MAIN_BAT_V:
	case MAIN_BAT_V_TXON:
		/*
		 * The value of mux scale volatage depends
		 * on the type of battery
		 * for LI-ion use MUX_SCALE_35 => 2.3-3.5V
		 * for LiFePo4 use MUX_SCALE_45 => 2.3-4.5V
		 * Check type of battery from platform data TODO ???
		 */
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
			MUX_SCALE_VBAT_MASK, MUX_SCALE_45);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: failed to read status\n");
			goto out;
		}
		break;
	case BTEMP_BALL:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
				MUX_SCALE_BDATA_MASK, MUX_SCALE_BDATA18);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set mux scale\n");
			goto out;
		}
		break;
	case BAT_CTRL:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
				MUX_SCALE_BDATA_MASK, MUX_SCALE_BDATA27);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set mux scale\n");
			goto out;
		}
		break;
	case XTAL_TEMP:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC, AB5500_GPADC_AUTO_XTALTEMP_CTRL,
			ADC_XTAL_FORCE_MASK, ADC_XTAL_FORCE_EN);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set xtaltemp\n");
			goto out;
		}
		break;
	case GPADC0_V:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
				MUX_SCALE_GPADC0_MASK, GPADC0_SCALE_VOL18);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set gpadc0\n");
			goto out;
		}
		break;
	case ACC_DETECT2:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL2,
				MUX_SCALE_ACCDET2_MASK, ACCDET2_SCALE_VOL27);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set accdet2\n");
			goto out;
		}
		break;
	case ACC_DETECT3:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL2,
				MUX_SCALE_ACCDET3_MASK, ACCDET3_SCALE_VOL27);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set accdet3\n");
			goto out;
		}
		break;
	case VIBRA_KELVIN:
		ret = abx500_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_KELVIN_CTRL,
				KELVIN_SCALE_VOL45);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set kelv scale\n");
			goto out;
		}
		break;
	case USB_CHARGER_C:
	case VBUS_V:
	case BK_BAT_V:
	case USB_ID:
	case PCB_TEMP:
	case DIE_TEMP:
		break;
	default:
		dev_err(gpadc->dev, "gpadc: Wrong adc\n");
		goto out;
		break;
	}
	if (adc_tab[input].adout) {
		adout_mask = adc_tab[input].adout == ADOUT0 ?
			GPADC_MANUAL_ADOUT0_MASK : GPADC_MANUAL_ADOUT1_MASK;
		adout_val = adc_tab[input].adout == ADOUT0 ?
			GPADC_MANUAL_ADOUT0_ON : GPADC_MANUAL_ADOUT1_ON;
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
				adout_mask, adout_val);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to set ADOUT\n");
			goto out;
		}
		/* delay required to ramp up voltage on BDATA node */
		usleep_range(10000, 12000);
	}
	ret = abx500_set_register_interruptible(gpadc->dev, AB5500_BANK_ADC,
			AB5500_GPADC_MANUAL_MUX_CTRL, adc_tab[input].mux);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc: fail to trigger manual conv\n");
		goto out;
	}
	/* wait for completion of conversion */
	looplimit = 0;
	do {
		msleep(1);
		ret = abx500_get_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC, AB5500_GPADC_MANUAL_STAT_REG,
			&status);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: failed to read status\n");
			goto out;
		}
		if (status & GPADC_MANUAL_READY)
			break;
	} while (++looplimit < 2);
	if (looplimit >= 2) {
		dev_err(gpadc->dev, "timeout:failed to complete conversion\n");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Disable ADOUT for measurement
	 */
	if (adc_tab[input].adout) {
		adout_mask = adc_tab[input].adout == ADOUT0 ?
			GPADC_MANUAL_ADOUT0_MASK : GPADC_MANUAL_ADOUT1_MASK;
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
				adout_mask, 0x0);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to disable ADOUT\n");
			goto out;
		}
	}
	/*
	 * Disable XTAL TEMP
	 */
	if (input == XTAL_TEMP) {
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC, AB5500_GPADC_AUTO_XTALTEMP_CTRL,
			ADC_XTAL_FORCE_MASK, ADC_XTAL_FORCE_DI);
		if (ret < 0) {
			dev_err(gpadc->dev,
				"gpadc: fail to disable xtaltemp\n");
			goto out;
		}
	}
	/* Read the converted RAW data */
	ret = abx500_get_register_interruptible(gpadc->dev, AB5500_BANK_ADC,
			AB5500_GPADC_MANDATAL_REG, &low_data);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: read low data failed\n");
		goto out;
	}

	ret = abx500_get_register_interruptible(gpadc->dev, AB5500_BANK_ADC,
			AB5500_GPADC_MANDATAH_REG, &high_data);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: read high data failed\n");
		goto out;
	}

	data = (high_data << 2) | (low_data >> 6);
	if (input == BAT_CTRL || input == BTEMP_BALL) {
		/*
		 * TODO: Re-check with h/w team
		 * discard null or value < 5, as there is some error
		 * in conversion
		 */
		if (data < 5)
			data = gpadc->prev_bdata;
		else
			gpadc->prev_bdata = data;
	}
	result = ab5500_gpadc_ad_to_voltage(gpadc, input, data);

	mutex_unlock(&gpadc->ab5500_gpadc_lock);
	return result;

out:
	mutex_unlock(&gpadc->ab5500_gpadc_lock);
	dev_err(gpadc->dev,
		"gpadc: Failed to AD convert channel %d\n", input);
	return ret;
}
EXPORT_SYMBOL(ab5500_gpadc_convert);

/**
 * ab5500_gpadc_program_auto() - gpadc conversion auto conversion
 * @trig_index:	Generic trigger channel for conversion
 *
 * This function program the auto trigger channel
 */
static int ab5500_gpadc_program_auto(struct ab5500_gpadc *gpadc, int trig)
{
	int ret;
	u8 adout;
#define MIN_INDEX	0x02
#define MAX_INDEX	0x03
#define CTRL_INDEX	0x01

	ret = abx500_set_register_interruptible(gpadc->dev, AB5500_BANK_ADC,
			AB5500_GPADC_AUTO_TRIG_INDEX + (trig << 2) + MIN_INDEX,
			gpadc->adc_trig[trig].trig_min);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: fail to program min\n");
		return ret;
	}
	ret = abx500_set_register_interruptible(gpadc->dev, AB5500_BANK_ADC,
			AB5500_GPADC_AUTO_TRIG_INDEX + (trig << 2) + MAX_INDEX,
			gpadc->adc_trig[trig].trig_max);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: fail to program max\n");
		return ret;
	}
	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
		AB5500_BANK_ADC, AB5500_GPADC_AUTO_TRIG_INDEX + (trig << 2),
		TRIGX_MUX_SELECT, gpadc->adc_trig[trig].auto_mux);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: fail to select mux\n");
		return ret;
	}
	if (gpadc->adc_trig[trig].adout) {
		adout = gpadc->adc_trig[trig].adout == ADOUT0 ?
				gpadc->adc_trig[trig].adout << 6 :
				gpadc->adc_trig[trig].adout << 5;
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC,
			AB5500_GPADC_AUTO_TRIG_INDEX + (trig << 2) + CTRL_INDEX,
			adout, adout);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to program adout\n");
			return ret;
		}
	}
	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC,
			AB5500_GPADC_AUTO_TRIG_INDEX + (trig << 2) + CTRL_INDEX,
			TRIGX_FREQ_MASK, gpadc->adc_trig[trig].freq);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: fail to program freq\n");
		return ret;
	}
	return ret;

}

#define TRIG_V(trigval, min, max) \
	((((trigval) - (min)) * AUTO_ADC_RESOLUTION) / ((max) - (min)))

static int ab5500_gpadc_vbat_auto_conf(struct ab5500_gpadc *gpadc,
					struct adc_auto_input *in)
{
	int trig_min, ret;
	u8 trig_reg, trig_arm;

	/* Scale mux voltage */
	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC,
			AB5500_GPADC_MANUAL_MODE_CTRL,
			MUX_SCALE_VBAT_MASK, MUX_SCALE_45);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: failed to set vbat scale\n");
		return ret;
	}

	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC,
			AB5500_GPADC_AUTO_CTRL1,
			AUTO_VBAT_MASK, AUTO_VBAT_ON);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: failed to set vbat on\n");
		return ret;
	}

	trig_min = TRIG_V(in->min, adc_tab[in->mux].min, adc_tab[in->mux].max);

	if (in->mux == MAIN_BAT_V_TRIG_MIN) {
		trig_reg = AB5500_GPADC_AUTO_TRIG_VBAT_MIN_NOTX;
		trig_arm = TRIG_VBAT_NOTX_ARM_MASK;
	} else {
		trig_reg = AB5500_GPADC_AUTO_TRIG_VBAT_MIN_TXON;
		trig_arm = TRIG_VBAT_TXON_ARM_MASK;
	}
	ret = abx500_set_register_interruptible(gpadc->dev, AB5500_BANK_ADC,
			trig_reg, trig_min);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: fail to program vbat min\n");
		return ret;
	}
	/*
	 * arm the trigger
	 */
	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
		AB5500_BANK_ADC, AB5500_GPADC_AUTO_CTRL1, trig_arm, trig_arm);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: failed to trig vbat\n");
		return ret;
	}
	return ret;
}
/**
 * ab5500_gpadc_convert_auto() - gpadc conversion
 * @auto_input:	input trigger for conversion
 *
 * This function converts the selected channel from
 * analog to digital data in auto mode
 */

int ab5500_gpadc_convert_auto(struct ab5500_gpadc *gpadc,
				struct adc_auto_input *in)
{
	int ret, trig;
	unsigned long flags;

	if (!gpadc)
		return -ENODEV;
	mutex_lock(&gpadc->ab5500_gpadc_lock);

	if (in->mux == MAIN_BAT_V_TXON_TRIG_MIN) {
		spin_lock_irqsave(&gpadc->gpadc_auto_lock, flags);
		if (gpadc->adc_trig[ADC_INPUT_VBAT_TXON].flag == true) {
			spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);
			ret = -EBUSY;
			dev_err(gpadc->dev, "gpadc: Auto vbat txon busy");
			goto out;
		}
		spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);

		ret = ab5500_gpadc_vbat_auto_conf(gpadc, in);
		if (ret < 0)
			goto out;

		gpadc->adc_trig[ADC_INPUT_VBAT_TXON].auto_mux = in->mux;
		gpadc->adc_trig[ADC_INPUT_VBAT_TXON].auto_callb =
				in->auto_adc_callback;
		spin_lock_irqsave(&gpadc->gpadc_auto_lock, flags);
		gpadc->adc_trig[ADC_INPUT_VBAT_TXON].flag = true;
		spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);
	} else if (in->mux == MAIN_BAT_V_TRIG_MIN) {

		spin_lock_irqsave(&gpadc->gpadc_auto_lock, flags);
		if (gpadc->adc_trig[ADC_INPUT_VBAT_TXOFF].flag == true) {
			spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);
			ret = -EBUSY;
			dev_err(gpadc->dev, "gpadc: Auto vbat busy");
			goto out;
		}
		spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);

		ret = ab5500_gpadc_vbat_auto_conf(gpadc, in);
		if (ret < 0)
			goto out;

		gpadc->adc_trig[ADC_INPUT_VBAT_TXOFF].auto_mux = in->mux;
		gpadc->adc_trig[ADC_INPUT_VBAT_TXOFF].auto_callb =
				in->auto_adc_callback;
		spin_lock_irqsave(&gpadc->gpadc_auto_lock, flags);
		gpadc->adc_trig[ADC_INPUT_VBAT_TXOFF].flag = true;
		spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);
	} else {
		/*
		 * check if free trigger is available
		 */
		trig = ADC_INPUT_TRIG0;
		spin_lock_irqsave(&gpadc->gpadc_auto_lock, flags);
		while (gpadc->adc_trig[trig].flag == true &&
					trig <= ADC_INPUT_TRIG7)
			trig++;

		spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);
		if (trig > ADC_INPUT_TRIG7) {
			ret = -EBUSY;
			dev_err(gpadc->dev, "gpadc: no free channel\n");
			goto out;
		}
		switch (in->mux) {
		case MAIN_BAT_V:
			/*
			 * The value of mux scale volatage depends
			 * on the type of battery
			 * for LI-ion use MUX_SCALE_35 => 2.3-3.5V
			 * for LiFePo4 use MUX_SCALE_45 => 2.3-4.5V
			 * Check type of battery from platform data TODO ???
			 */
			ret = abx500_mask_and_set_register_interruptible(
				gpadc->dev,
				AB5500_BANK_ADC, AB5500_GPADC_MANUAL_MODE_CTRL,
				MUX_SCALE_VBAT_MASK, MUX_SCALE_45);
			if (ret < 0) {
				dev_err(gpadc->dev,
					"gpadc: failed to read status\n");
				goto out;
			}

		case BTEMP_BALL:
			ret = abx500_set_register_interruptible(
				gpadc->dev, AB5500_BANK_ADC,
				AB5500_GPADC_AUTO_TRIG_ADOUT0_CTRL,
				ADOUT0_TRIGX_PRESTART);
			if (ret < 0) {
				dev_err(gpadc->dev,
					"gpadc: failed to set prestart\n");
				goto out;
			}

		case ACC_DETECT2:
		case ACC_DETECT3:
		case VBUS_V:
		case USB_CHARGER_C:
		case BK_BAT_V:
		case PCB_TEMP:
		case USB_ID:
		case BAT_CTRL:
			gpadc->adc_trig[trig].trig_min =
			(u8)TRIG_V(in->min, adc_tab[in->mux].min,
					adc_tab[in->mux].max);
			gpadc->adc_trig[trig].trig_max =
			(u8)TRIG_V(in->max, adc_tab[in->mux].min,
					adc_tab[in->mux].max);
			gpadc->adc_trig[trig].adout =
				adc_tab[in->mux].adout;
			break;
		case DIE_TEMP:
			/*
			 * From the AB5500 product specification
			 * T(deg_cel) = 27 - (ADCode - 709)/2.4213)
			 * ADCode = 709 + (2.4213 * (27 - T))
			 * Auto trigger min/max level is of 8bit precision.
			 * Hence use AB5500_GPADC_MANDATAH_REG value
			 * obtained by 2 bit right shift of ADCode.
			 */
			gpadc->adc_trig[trig].trig_min =
				(709 + ((24213 * (27 - in->min))/10000))>>2;
			gpadc->adc_trig[trig].trig_max =
				(709 + ((24213 * (27 - in->max))/10000))>>2;
			gpadc->adc_trig[trig].adout =
				adc_tab[in->mux].adout;
			break;
		default:
			dev_err(gpadc->dev, "Unknow GPADC request\n");
			break;
		}
		gpadc->adc_trig[trig].freq = in->freq;
		gpadc->adc_trig[trig].auto_mux =
			adc_tab[in->mux].mux;
		gpadc->adc_trig[trig].auto_callb = in->auto_adc_callback;

		ret = ab5500_gpadc_program_auto(gpadc, trig);
		if (ret < 0) {
			dev_err(gpadc->dev,
					"gpadc: fail to program auto ch\n");
			goto out;
		}
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
				AB5500_BANK_ADC,
				AB5500_GPADC_AUTO_TRIG_INDEX + (trig * 4),
				TRIGX_ARM_MASK, TRIGX_ARM);
		if (ret < 0) {
			dev_err(gpadc->dev, "gpadc: fail to trigger\n");
			goto out;
		}
		spin_lock_irqsave(&gpadc->gpadc_auto_lock, flags);
		gpadc->adc_trig[trig].flag = true;
		spin_unlock_irqrestore(&gpadc->gpadc_auto_lock, flags);
	}
out:
	mutex_unlock(&gpadc->ab5500_gpadc_lock);
	return ret;

}
EXPORT_SYMBOL(ab5500_gpadc_convert_auto);

/* sysfs interface for GPADC0 */
static ssize_t ab5500_gpadc0_get(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int voltage;
	struct ab5500_gpadc *gpadc = dev_get_drvdata(dev);

	voltage = ab5500_gpadc_convert(gpadc, GPADC0_V);

	return sprintf(buf, "%d\n", voltage);
}
static DEVICE_ATTR(adc0volt, 0644, ab5500_gpadc0_get, NULL);

static void ab5500_gpadc_trigx_work(struct ab5500_gpadc *gp, int trig)
{
	unsigned long flags;
	if (gp->adc_trig[trig].auto_callb != NULL) {
		gp->adc_trig[trig].auto_callb(gp->adc_trig[trig].auto_mux);
		spin_lock_irqsave(&gp->gpadc_auto_lock, flags);
		gp->adc_trig[trig].flag = false;
		spin_unlock_irqrestore(&gp->gpadc_auto_lock, flags);
	} else {
		dev_err(gp->dev, "Unknown trig for %d\n", trig);
	}
}
/**
 * ab5500_gpadc_trig0_work() - work item for trig0 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 0 auto conversion.
 */
static void ab5500_gpadc_trig0_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig0_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG0);
}

/**
 * ab5500_gpadc_trig1_work() - work item for trig1 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig1 auto conversion.
 */
static void ab5500_gpadc_trig1_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig1_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG1);
}

/**
 * ab5500_gpadc_trig2_work() - work item for trig2 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 2 auto conversion.
 */
static void ab5500_gpadc_trig2_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig2_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG2);
}

/**
 * ab5500_gpadc_trig3_work() - work item for trig3 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 3 auto conversion.
 */
static void ab5500_gpadc_trig3_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig3_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG3);
}

/**
 * ab5500_gpadc_trig4_work() - work item for trig4 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 4 auto conversion.
 */
static void ab5500_gpadc_trig4_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig4_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG4);
}

/**
 * ab5500_gpadc_trig5_work() - work item for trig5 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 5 auto conversion.
 */
static void ab5500_gpadc_trig5_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig5_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG5);
}

/**
 * ab5500_gpadc_trig6_work() - work item for trig6 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 6 auto conversion.
 */
static void ab5500_gpadc_trig6_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig6_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG6);
}

/**
 * ab5500_gpadc_trig7_work() - work item for trig7 auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for trig 7 auto conversion.
 */
static void ab5500_gpadc_trig7_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig7_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_TRIG7);
}

/**
 * ab5500_gpadc_vbat_txon_work() - work item for vbat_txon trigger auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for vbat_txon trigger auto adc.
 */
static void ab5500_gpadc_vbat_txon_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig_vbat_txon_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_VBAT_TXON);
}

/**
 * ab5500_gpadc_vbat_txoff_work() - work item for vbat_txoff trigger auto adc
 * @irq:	irq number
 * @work:	work pointer
 *
 * This is a work handler for vbat_txoff trigger auto adc.
 */
static void ab5500_gpadc_vbat_txoff_work(struct work_struct *work)
{
	struct ab5500_gpadc *gpadc = container_of(work,
		struct ab5500_gpadc, gpadc_trig_vbat_txoff_work);
	ab5500_gpadc_trigx_work(gpadc, ADC_INPUT_VBAT_TXOFF);
}

/**
 * ab5500_adc_trigx_handler() - isr for auto gpadc conversion trigger
 * @irq:	irq number
 * @data:	pointer to the data passed during request irq
 *
 * This is a interrupt service routine for auto gpadc conversion.
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_adc_trigx_handler(int irq, void *_gpadc)
{
	struct ab5500_platform_data *plat;
	struct ab5500_gpadc *gpadc = _gpadc;
	int dev_irq;

	plat = dev_get_platdata(gpadc->dev->parent);
	dev_irq = irq - plat->irq.base;

	switch (dev_irq) {
	case AB5500_INT_ADC_TRIG0:
		dev_dbg(gpadc->dev, "Trigger 0 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig0_work);
		break;
	case AB5500_INT_ADC_TRIG1:
		dev_dbg(gpadc->dev, "Trigger 1 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig1_work);
		break;
	case AB5500_INT_ADC_TRIG2:
		dev_dbg(gpadc->dev, "Trigger 2 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig2_work);
		break;
	case AB5500_INT_ADC_TRIG3:
		dev_dbg(gpadc->dev, "Trigger 3 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig3_work);
		break;
	case AB5500_INT_ADC_TRIG4:
		dev_dbg(gpadc->dev, "Trigger 4 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig4_work);
		break;
	case AB5500_INT_ADC_TRIG5:
		dev_dbg(gpadc->dev, "Trigger 5 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig5_work);
		break;
	case AB5500_INT_ADC_TRIG6:
		dev_dbg(gpadc->dev, "Trigger 6 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig6_work);
		break;
	case AB5500_INT_ADC_TRIG7:
		dev_dbg(gpadc->dev, "Trigger 7 received\n");
		queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig7_work);
		break;
	default:
		dev_dbg(gpadc->dev, "unknown trigx handler input\n");
		break;
	}
	return IRQ_HANDLED;
}

/**
 * ab5500_adc_vbat_txon_handler() - isr for auto vbat_txon conversion trigger
 * @irq:	irq number
 * @data:	pointer to the data passed during request irq
 *
 * This is a interrupt service routine for auto vbat_txon conversion
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_adc_vbat_txon_handler(int irq, void *_gpadc)
{
	struct ab5500_gpadc *gpadc = _gpadc;

	queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig_vbat_txon_work);
	return IRQ_HANDLED;
}

/**
 * ab5500_adc_vbat_txoff_handler() - isr for auto vbat_txoff conversion trigger
 * @irq:	irq number
 * @data:	pointer to the data passed during request irq
 *
 * This is a interrupt service routine for auto vbat_txoff conversion
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab5500_adc_vbat_txoff_handler(int irq, void *_gpadc)
{
	struct ab5500_gpadc *gpadc = _gpadc;

	queue_work(gpadc->gpadc_wq, &gpadc->gpadc_trig_vbat_txoff_work);
	return IRQ_HANDLED;
}

/**
 * ab5500_gpadc_configuration() - function for gpadc conversion
 * @irq:	irq number
 * @data:	pointer to the data passed during request irq
 *
 * This function configures the gpadc
 */
static int ab5500_gpadc_configuration(struct ab5500_gpadc *gpadc)
{
	int ret;
	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB5500_BANK_ADC, AB5500_GPADC_AUTO_CTRL2,
			ADC_CAL_OFF_MASK | ADC_ON_MODE_MASK,
			ADC_CAL_ON | ADC_FULLPWR);
	return ret;
}

/* ab5500 btemp driver interrupts and their respective isr */
static struct ab5500_adc_interrupts ab5500_adc_irq[] = {
	{"TRIGGER-0", ab5500_adc_trigx_handler},
	{"TRIGGER-1", ab5500_adc_trigx_handler},
	{"TRIGGER-2", ab5500_adc_trigx_handler},
	{"TRIGGER-3", ab5500_adc_trigx_handler},
	{"TRIGGER-4", ab5500_adc_trigx_handler},
	{"TRIGGER-5", ab5500_adc_trigx_handler},
	{"TRIGGER-6", ab5500_adc_trigx_handler},
	{"TRIGGER-7", ab5500_adc_trigx_handler},
	{"TRIGGER-VBAT-TXON", ab5500_adc_vbat_txon_handler},
	{"TRIGGER-VBAT", ab5500_adc_vbat_txoff_handler},
};

static int __devinit ab5500_gpadc_probe(struct platform_device *pdev)
{
	int ret, irq, i, j;
	struct ab5500_gpadc *gpadc;

	gpadc = kzalloc(sizeof(struct ab5500_gpadc), GFP_KERNEL);
	if (!gpadc) {
		dev_err(&pdev->dev, "Error: No memory\n");
		return -ENOMEM;
	}
	gpadc->dev = &pdev->dev;
	mutex_init(&gpadc->ab5500_gpadc_lock);
	spin_lock_init(&gpadc->gpadc_auto_lock);

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab5500_adc_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab5500_adc_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab5500_adc_irq[i].isr,
			IRQF_NO_SUSPEND,
			ab5500_adc_irq[i].name, gpadc);

		if (ret) {
			dev_err(gpadc->dev, "failed to request %s IRQ %d: %d\n"
				, ab5500_adc_irq[i].name, irq, ret);
			goto fail_irq;
		}
		dev_dbg(gpadc->dev, "Requested %s IRQ %d: %d\n",
			ab5500_adc_irq[i].name, irq, ret);
	}

	/* Get Chip ID of the ABB ASIC  */
	ret = abx500_get_chip_id(gpadc->dev);
	if (ret < 0) {
		dev_err(gpadc->dev, "failed to get chip ID\n");
		goto fail_irq;
	}
	gpadc->chip_id = (u8) ret;

	/* Create a work queue for gpadc auto */
	gpadc->gpadc_wq =
		create_singlethread_workqueue("ab5500_gpadc_wq");
	if (gpadc->gpadc_wq == NULL) {
		dev_err(gpadc->dev, "failed to create work queue\n");
		goto fail_irq;
	}

	INIT_WORK(&gpadc->gpadc_trig0_work, ab5500_gpadc_trig0_work);
	INIT_WORK(&gpadc->gpadc_trig1_work, ab5500_gpadc_trig1_work);
	INIT_WORK(&gpadc->gpadc_trig2_work, ab5500_gpadc_trig2_work);
	INIT_WORK(&gpadc->gpadc_trig3_work, ab5500_gpadc_trig3_work);
	INIT_WORK(&gpadc->gpadc_trig4_work, ab5500_gpadc_trig4_work);
	INIT_WORK(&gpadc->gpadc_trig5_work, ab5500_gpadc_trig5_work);
	INIT_WORK(&gpadc->gpadc_trig6_work, ab5500_gpadc_trig6_work);
	INIT_WORK(&gpadc->gpadc_trig7_work, ab5500_gpadc_trig7_work);
	INIT_WORK(&gpadc->gpadc_trig_vbat_txon_work,
				ab5500_gpadc_vbat_txon_work);
	INIT_WORK(&gpadc->gpadc_trig_vbat_txoff_work,
				ab5500_gpadc_vbat_txoff_work);

	for (j = 0; j < N_AUTO_TRIGGER; j++)
		gpadc->adc_trig[j].flag = false;

	ret = ab5500_gpadc_configuration(gpadc);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc: configuration failed\n");
		goto free_wq;
	}

	ret = device_create_file(gpadc->dev, &dev_attr_adc0volt);
	if (ret < 0) {
		dev_err(gpadc->dev, "File device creation failed: %d\n", ret);
		ret = -ENODEV;
		goto fail_sysfs;
	}
	list_add_tail(&gpadc->node, &ab5500_gpadc_list);

	platform_set_drvdata(pdev, gpadc);

	return 0;
fail_sysfs:
free_wq:
	destroy_workqueue(gpadc->gpadc_wq);
fail_irq:
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab5500_adc_irq[i].name);
		free_irq(irq, gpadc);
	}
	kfree(gpadc);
	gpadc = NULL;
	return ret;
}

static int __devexit ab5500_gpadc_remove(struct platform_device *pdev)
{
	int i, irq;
	struct ab5500_gpadc *gpadc = platform_get_drvdata(pdev);

	device_remove_file(gpadc->dev, &dev_attr_adc0volt);

	/* remove this gpadc entry from the list */
	list_del(&gpadc->node);
	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab5500_adc_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab5500_adc_irq[i].name);
		free_irq(irq, gpadc);
	}
	/* Flush work */
	flush_workqueue(gpadc->gpadc_wq);

	/* Delete the work queue */
	destroy_workqueue(gpadc->gpadc_wq);

	kfree(gpadc);
	gpadc = NULL;
	return 0;
}

static struct platform_driver ab5500_gpadc_driver = {
	.probe = ab5500_gpadc_probe,
	.remove = __devexit_p(ab5500_gpadc_remove),
	.driver = {
		.name = "ab5500-adc",
		.owner = THIS_MODULE,
	},
};

static int __init ab5500_gpadc_init(void)
{
	return platform_driver_register(&ab5500_gpadc_driver);
}

static void __exit ab5500_gpadc_exit(void)
{
	platform_driver_unregister(&ab5500_gpadc_driver);
}

subsys_initcall_sync(ab5500_gpadc_init);
module_exit(ab5500_gpadc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vijaya Kumar K");
MODULE_ALIAS("platform:ab5500_adc");
MODULE_DESCRIPTION("AB5500 GPADC driver");
