/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Charger driver for AB8500
 *
 * License Terms: GNU General Public License v2
 * Author: Johan Palsson <johan.palsson@stericsson.com>
 * Author: Karl Komierowski <karl.komierowski@stericsson.com>
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/usb/otg.h>
#include <linux/wakelock.h>
#include <linux/mutex.h>
#include <linux/timer.h>

#include <linux/battery/charger/abb_charger.h>

static int ab8500_voltage_to_regval(int voltage)
{
	int i;

	/* Special case for voltage below 3.5V */
	if (voltage < ab8500_chg_voltage_map[0])
		return LOW_VOLT_REG;

	for (i = 1; i < ARRAY_SIZE(ab8500_chg_voltage_map); i++) {
		if (voltage < ab8500_chg_voltage_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_chg_voltage_map) - 1;
	if (voltage == ab8500_chg_voltage_map[i])
		return i;
	else
		return -1;
}

static int ab8500_current_to_regval(int curr)
{
	int i;

	if (curr < ab8500_chg_current_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_chg_current_map); i++) {
		if (curr < ab8500_chg_current_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_chg_current_map) - 1;
	if (curr == ab8500_chg_current_map[i])
		return i;
	else
		return -1;
}

static int ab8500_vbus_in_curr_to_regval(int curr)
{
	int i;

	if (curr < ab8500_chg_vbus_in_curr_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_chg_vbus_in_curr_map); i++) {
		if (curr < ab8500_chg_vbus_in_curr_map[i])
			return i - 1;
	}

	/* If not last element, return error */
	i = ARRAY_SIZE(ab8500_chg_vbus_in_curr_map) - 1;
	if (curr == ab8500_chg_vbus_in_curr_map[i])
		return i;
	else
		return -1;
}

/*
 * Function for enabling and disabling sw fallback mode
 * should always be disabled when no charger is connected.
 */
static void ab8500_enable_disable_sw_fallback(
		struct ab8500_charger_info *di,
		bool fallback)
{
	u8 val;
	u8 reg;
	u8 bank;
	u8 bit;
	int ret;

	dev_dbg(di->dev, "SW Fallback: %d\n", fallback);

	if (is_ab8500(di->parent)) {
		bank = 0x15;
		reg = 0x0;
		bit = 3;

		/* read the register containing fallback bit */
		ret = abx500_get_register_interruptible(di->dev,
							bank, reg, &val);
		if (ret < 0) {
			dev_err(di->dev, "%d read failed\n", __LINE__);
			return;
		}

		/* enable the OPT emulation registers */
		ret = abx500_set_register_interruptible(di->dev,
							0x11, 0x00, 0x2);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			return;
		}

		if (fallback)
			val |= (1 << bit);
		else
			val &= ~(1 << bit);

		/* write back the changed fallback bit value to register */
		ret = abx500_set_register_interruptible(di->dev,
							bank, reg, val);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			return;
		}

		/* disable the set OTP registers again */
		ret = abx500_set_register_interruptible(di->dev,
							0x11, 0x00, 0x0);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			return;
		}
	} else {
		bank = AB8500_SYS_CTRL1_BLOCK;
		reg = AB8500_SW_CONTROL_FALLBACK;
		bit = 0;
	}

	/* read the register containing fallback bit */
	ret = abx500_get_register_interruptible(di->dev, bank, reg, &val);
	if (ret < 0) {
		dev_err(di->dev, "%d read failed\n", __LINE__);
		return;
	}

	if (is_ab8500(di->parent)) {
		/* enable the OPT emulation registers */
		ret = abx500_set_register_interruptible(di->dev,
							0x11, 0x00, 0x2);
		if (ret) {
			dev_err(di->dev, "%d write failed\n", __LINE__);
			goto disable_otp;
		}
	}

	if (fallback)
		val |= (1 << bit);
	else
		val &= ~(1 << bit);

	/* write back the changed fallback bit value to register */
	ret = abx500_set_register_interruptible(di->dev, bank, reg, val);
	if (ret)
		dev_err(di->dev, "%d write failed\n", __LINE__);

disable_otp:
	if (is_ab8500(di->parent)) {
		/* disable the set OTP registers again */
		ret = abx500_set_register_interruptible(di->dev,
							0x11, 0x00, 0x0);
		if (ret)
			dev_err(di->dev, "%d write failed\n", __LINE__);
	}
}

static void ab8500_chg_dump_reg(struct ab8500_charger_info *di)
{
	u8 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(ab8500_chg_register); i++) {
		abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				  ab8500_chg_register[i], &val);
		dev_info(di->dev, "abb addr 0x0b%x, data 0x%x\n",
			 ab8500_chg_register[i], val);
	}
}

static int ab8500_chg_get_vbus_voltage(struct ab8500_charger_info *di)
{
	int vch;

#ifdef CONFIG_CHARGER_AB8500_MAINCHGBLOCK
	vch = ab8500_gpadc_convert(di->gpadc, MAIN_CHARGER_V);
#else
	vch = ab8500_gpadc_convert(di->gpadc, VBUS_V);
#endif
	if (vch < 0) {
		dev_err(di->dev, "%s: gpadc conv failed\n", __func__);
		vch = 0;
	}

	return vch;
}

static int ab8500_chg_get_vbus_current(struct ab8500_charger_info *di)
{
	int ich;

	ich = ab8500_gpadc_convert(di->gpadc, USB_CHARGER_C);
	if (ich < 0) {
		dev_err(di->dev, "%s: gpadc conv failed\n", __func__);
		ich = 0;
	}

	return ich;
}

static int ab8500_chg_cv(struct ab8500_charger_info *di)
{
	u8 val;
	int ret = 0;

#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER,	AB8500_CH_STATUS1_REG, &val);
#else
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_USBCH_STAT1_REG, &val);
#endif
	if (ret < 0) {
		dev_err(di->dev, "%s: ab8500 read failed\n", __func__);
		return 0;
	}

#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
	if (val & MAIN_CH_CV_ON)
#else
	if (val & USB_CH_CV_ON)
#endif
		ret = 1;
	else
		ret = 0;

	return ret;
}

static bool ab8500_chg_get_vbus_status(struct ab8500_charger_info *di)
{
	u8 data;

	abx500_get_register_interruptible(di->dev,
	  AB8500_CHARGER, AB8500_CH_USBCH_STAT1_REG, &data);

	return (data & VBUS_DET_DBNC1) && (data & VBUS_DET_DBNC100);
}

static int ab8500_chg_get_usb_line_state(struct ab8500_charger_info *di)
{
	u8 data;
	int ret;

	if (is_ab8500(di->parent)) {
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
			AB8500_USB_LINE_STAT_REG, &data);
	} else {
		ret = abx500_get_register_interruptible(di->dev, AB8500_USB,
			AB8500_USB_LINK1_STAT_REG, &data);
	}

	if (ret < 0) {
		dev_err(di->dev, "%s: read failed\n", __func__);
		return ret;
	}

	return (data & AB8500_USB_LINK_STATUS) >> 3;
}

static int ab8500_chg_vf_check(struct ab8500_charger_info *di)
{
	int v_batctrl;

	v_batctrl = ab8500_gpadc_convert(di->gpadc, BAT_CTRL);

	if (v_batctrl < 0) {
		dev_err(di->dev,
			"%s gpadc conversion failed, using previous value",
			__func__);
		v_batctrl = di->prev_batctrl;
	} else
		di->prev_batctrl = v_batctrl;

	if (is_ab8500_1p1_or_earlier(di->parent)) {
		/*
		 * For ABB cut1.0 and 1.1 BAT_CTRL is internally
		 * connected to 1.8V through a 450k resistor
		 */
		di->res_batctrl = (450000 * (v_batctrl)) / (1800 - v_batctrl);
	} else {
		/*
		 * BAT_CTRL is internally
		 * connected to 1.8V through a 80k resistor
		 */
		di->res_batctrl = (80000 * (v_batctrl)) / (1800 - v_batctrl);
	}

	if (di->res_batctrl <=
	    get_battery_data(di).bat_info->resis_high &&
	    di->res_batctrl >=
	    get_battery_data(di).bat_info->resis_low)
		goto battery_good;
	else
		goto battery_bad;

battery_good:
	/* if we saw a battery then */
	/* re-enable the battery sense comparator */
	abx500_set_register_interruptible(di->dev,
			  AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			  AB8500_BAT_CTRL_CURRENT_SOURCE_DEFAULT);
	dev_dbg(di->dev, "%s, battery vf res : %d\n", __func__,
		di->res_batctrl);
	return true;

battery_bad:
	dev_info(di->dev, "%s, battery vf res : %d\n", __func__,
		 di->res_batctrl);
	return false;
}

static int ab8500_chg_get_charger_status(struct ab8500_charger_info *di)
{
	u8 reg_value;
	u8 reg_status;
	int ret;

#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
	reg_status = AB8500_CH_STATUS1_REG;
#else
	reg_status = AB8500_CH_USBCH_STAT1_REG;
#endif

	ret = abx500_get_register_interruptible(di->dev,
				AB8500_CHARGER,
				reg_status,
				&reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s: read failed\n", __func__);
		return ret;
	}

#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
	return reg_value & 0x2;
#else
	return reg_value & 0x4;
#endif
}
static int ab8500_chg_set_current(struct ab8500_charger_info *di,
				  int ich, int reg, bool init)
{
	int ret, i;
	int curr_index, prev_curr_index;
	int shift_value;
	u8 reg_value;

	switch (reg) {
	case AB8500_MCH_IPT_CURLVL_REG:
		shift_value = MAIN_CH_INPUT_CURR_SHIFT;
		curr_index = ab8500_current_to_regval(ich);
		break;
	case AB8500_USBCH_IPT_CRNTLVL_REG:
		shift_value = VBUS_IN_CURR_LIM_SHIFT;
		curr_index = ab8500_vbus_in_curr_to_regval(ich);
		break;
	case AB8500_CH_OPT_CRNTLVL_REG:
		shift_value = 0;
		curr_index = ab8500_current_to_regval(ich);
		break;
	default:
		dev_err(di->dev, "%s: current register not valid\n",
			__func__);
		return -ENXIO;
	}

	if (curr_index < 0) {
		dev_err(di->dev,
			"requested current limit out-of-range\n");
		return -ENXIO;
	}

	if (init) {
		/* for soft start charging */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, reg, (u8) curr_index << shift_value);
		if (ret)
			dev_err(di->dev, "%s: write failed\n",
				__func__);
		return ret;
	}

	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER,	reg, &reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s: read failed\n", __func__);
		return ret;
	}

	prev_curr_index = (reg_value >> shift_value);
	if (prev_curr_index == curr_index)
		return 0;

	dev_dbg(di->dev, "%s, REG : 0x0b%x,"
		" prev_index: 0x%x, curr_index : 0x%x\n",
		__func__, reg, prev_curr_index, curr_index);

	if (prev_curr_index > curr_index) {
		for (i = prev_curr_index - 1; i >= curr_index; i--) {

			usleep_range(STEP_UDELAY, STEP_UDELAY * 2);
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER, reg, (u8) i << shift_value);
			if (ret) {
				dev_err(di->dev, "%s: write failed\n",
					__func__);
				return ret;
			}
		}
	} else {
		for (i = prev_curr_index + 1; i <= curr_index; i++) {

			usleep_range(STEP_UDELAY, STEP_UDELAY * 2);
			ret = abx500_set_register_interruptible(di->dev,
				AB8500_CHARGER, reg, (u8) i << shift_value);
			if (ret) {
				dev_err(di->dev, "%s: write failed\n",
					__func__);
				return ret;
			}
		}
	}

	return ret;
}

static int ab8500_chg_set_input_curr(struct ab8500_charger_info *di,
				     int ich_in, bool init)
{
#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
	return ab8500_chg_set_current(di, ich_in,
				      AB8500_MCH_IPT_CURLVL_REG, init);
#else
	return ab8500_chg_set_current(di, ich_in,
				      AB8500_USBCH_IPT_CRNTLVL_REG, init);
#endif
}

static int ab8500_chg_set_output_curr(struct ab8500_charger_info *di,
				      int ich_out, bool init)
{
	return ab8500_chg_set_current(di, ich_out,
				      AB8500_CH_OPT_CRNTLVL_REG, init);
}

static int ab8500_chg_led_en(struct ab8500_charger_info *di, int on)
{
	int ret;

	if (on) {
		/* Power ON charging LED indicator, set LED current to 5mA */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_CTRL,
			(LED_IND_CUR_5MA | LED_INDICATOR_PWM_ENA));
		if (ret) {
			dev_err(di->dev, "Power ON LED failed\n");
			return ret;
		}

		/* LED indicator PWM duty cycle 252/256 */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_DUTY,
			LED_INDICATOR_PWM_DUTY_252_256);
		if (ret) {
			dev_err(di->dev,
				"Set LED PWM duty cycle failed\n");
			return ret;
		}
	} else {
		/* Power off charging LED indicator */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_LED_INDICATOR_PWM_CTRL,
			LED_INDICATOR_PWM_DIS);
		if (ret) {
			dev_err(di->dev, "Power-off LED failed\n");
			return ret;
		}
	}

	return ret;
}

static int ab8500_chg_en(struct ab8500_charger_info *di, int enable)
{
	int ret;
	int float_volt_index;
	u8 overshoot = 0;

	if (enable) {
		/* Enable Charging */

		/*
		 * Due to a bug in AB8500, BTEMP_HIGH/LOW interrupts
		 * will be triggered everytime we enable the VDD ADC supply.
		 * This will turn off charging for a short while.
		 * It can be avoided by having the supply on when
		 * there is a charger enabled. Normally the VDD ADC supply
		 * is enabled everytime a GPADC conversion is triggered. We will
		 * force it to be enabled from this driver to have
		 * the GPADC module independant of the AB8500 chargers
		 */
		if (!di->vddadc_en) {
			regulator_enable(di->regu);
			di->vddadc_en = true;
		}

		float_volt_index = ab8500_voltage_to_regval(
			di->pdata->chg_float_voltage);

		if (float_volt_index < 0) {
			dev_err(di->dev,
				"requested voltage is out of range"
				"charging not started\n");
			return -ENXIO;
		}

		/* float voltage */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_CH_VOLT_LVL_REG,
			(u8)float_volt_index);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		/* input current */
		ret = ab8500_chg_set_input_curr(di, 0, true);
		if (ret) {
			dev_err(di->dev,
				"%s Failed to set input current\n",
				__func__);
			return ret;
		}

		/* output current */
		ret = ab8500_chg_set_output_curr(di, 0, true);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to set output current\n",
				__func__);
			return ret;
		}

#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
		/* Check if VBAT overshoot control should be enabled */
		if (!get_battery_data(di).enable_overshoot)
			overshoot = MAIN_CH_NO_OVERSHOOT_ENA_N;

		/* Enable Main Charger */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_MCH_CTRL1, MAIN_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n",
				__func__);
			return ret;
		}
#else
		/* Check if VBAT overshoot control should be enabled */
		if (!get_battery_data(di).enable_overshoot)
			overshoot = USB_CHG_NO_OVERSHOOT_ENA_N;

		/* Enable USB Charger */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, USB_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

#endif
		/* input current */
		di->vbus_drop_count = 0;
		di->input_current_limit =
			di->pdata->charging_current[di->cable_type].
			input_current_limit;
		ret = ab8500_chg_set_input_curr(di,
			di->input_current_limit, false);
		if (ret) {
			dev_err(di->dev,
				"%s Failed to set input current\n",
				__func__);
			return ret;
		}

		/* output current */
		ret = ab8500_chg_set_output_curr(di,
			 di->pdata->charging_current[di->cable_type].
				fast_charging_current, false);
		if (ret) {
			dev_err(di->dev, "%s "
				"Failed to set output current\n",
				__func__);
			return ret;
		}
	} else {
#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
		/* Disable Main charging */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_MCH_CTRL1, 0);
		if (ret) {
			dev_err(di->dev,
				"%s write failed\n", __func__);
			return ret;
		}
#else
		/* Disable USB charging */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, 0);
		if (ret) {
			dev_err(di->dev,
				"%s write failed\n", __func__);
			return ret;
		}
#endif
		/* Disable regulator if enabled */
		if (di->vddadc_en) {
			regulator_disable(di->regu);
			di->vddadc_en = false;
		}

		dev_dbg(di->dev, "%s Disabled charging\n",
			__func__);
	}

	return ret;
}

static struct ab8500_charger_event_list ab8500_event_list[] = {
	{"MAIN_EXT_CH_NOT_OK", F_MAIN_EXT_CH_NOT_OK, M_MAIN_EXT_CH_NOT_OK},
	{"USB_CHARGER_NOT_OK", F_USB_CHARGER_NOT_OK, M_USB_CHARGER_NOT_OK},
	{"MAIN_THERMAL_PROT", F_MAIN_THERMAL_PROT, M_MAIN_THERMAL_PROT},
	{"USB_THERMAL_PROT", F_USB_THERMAL_PROT, M_USB_THERMAL_PROT},
	{"VBUS_OVV", F_VBUS_OVV, M_VBUS_OVV},
	{"BATT_OVV", F_BATT_OVV, M_BATT_OVV},
	{"CH_WD_EXP", F_CHG_WD_EXP, 0},
	{"BATT_REMOVE", F_BATT_REMOVE, 0},
	{"BTEMP_HIGH", F_BTEMP_HIGH, 0},
	{"BTEMP_MEDHIGH", F_BTEMP_MEDHIGH, 0},
	{"BTEMP_LOWMED", F_BTEMP_LOWMED, 0},
	{"BTEMP_LOW", F_BTEMP_LOW, 0},
};

/**
 * ab8500_fg_reenable_charging() - function for re-enabling charger explicitly
 * @di:		pointer to the ab8500_fg structure
 *
 * Sometimes, charger is disabled even though
 * S/W doesn't do it while re-charging.
 * This function is workaround for this issue
 */
#ifdef CONFIG_CHARGER_AB8500_MAINCHGBLOCK
static int ab8500_chg_reenable(struct ab8500_charger_info *di)
{
	u8 mainch_ctrl1;
	u8 usbch_ctrl1;
	u8 mainch_status1;
	u8 overshoot = 0;

	int ret;

	ret = abx500_get_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_MCH_CTRL1, &mainch_ctrl1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_CH_STATUS1_REG, &mainch_status1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	if ((mainch_ctrl1 & 0x1) && !(mainch_status1 & 0x2)) {
		dev_info(di->dev, "%s, try to enable the charger "
			 "from unexpected error\n", __func__);

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_MCH_CTRL1, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		if (!get_battery_data(di).enable_overshoot)
			overshoot = MAIN_CH_NO_OVERSHOOT_ENA_N;

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_MCH_CTRL1, MAIN_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		if (di->charging_reenabled > 2000)
			di->charging_reenabled = 1;
		else
			di->charging_reenabled++;
	}

	if (di->charging_reenabled > 0)
		dev_info(di->dev, "%s, charging is re-enabled %d times\n",
			 __func__, di->charging_reenabled);

	return 0;
}
#else
static int ab8500_chg_reenable(struct ab8500_charger_info *di)
{
	u8 mainch_ctrl1;
	u8 usbch_ctrl1;
	u8 usbch_status1;
	u8 overshoot = 0;

	int ret;

	ret = abx500_get_register_interruptible(di->dev,
				AB8500_CHARGER,
				AB8500_USBCH_CTRL1_REG, &usbch_ctrl1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				AB8500_CH_USBCH_STAT1_REG, &usbch_status1);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return ret;
	}

	if ((usbch_ctrl1 & 0x1) && !(usbch_status1 & 0x4)) {
		dev_info(di->dev, "%s, try to enable the charger "
			 "from unexpected error\n", __func__);

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_MCH_CTRL1, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, 0);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		if (!get_battery_data(di).enable_overshoot)
			overshoot = USB_CHG_NO_OVERSHOOT_ENA_N;

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_USBCH_CTRL1_REG, USB_CH_ENA | overshoot);
		if (ret) {
			dev_err(di->dev, "%s write failed\n", __func__);
			return ret;
		}

		if (di->charging_reenabled > 2000)
			di->charging_reenabled = 1;
		else
			di->charging_reenabled++;
	}

	if (di->charging_reenabled > 0)
		dev_info(di->dev, "%s, charging is re-enabled %d times\n",
			 __func__, di->charging_reenabled);

	return 0;
}
#endif
/**
 * ab8500_chg_kick_watchdog_work() - kick the watchdog
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for kicking the charger watchdog.
 *
 * For ABB revision 1.0 and 1.1 there is a bug in the watchdog
 * logic. That means we have to continously kick the charger
 * watchdog even when no charger is connected. This is only
 * valid once the AC charger has been enabled. This is
 * a bug that is not handled by the algorithm and the
 * watchdog have to be kicked by the charger driver
 * when the AC charger is disabled
 */
static void ab8500_chg_kick_watchdog_work(struct work_struct *work)
{
	int ret;

	struct ab8500_charger_info *di = container_of(work,
		struct ab8500_charger_info, kick_wd_work.work);

	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CHARG_WD_CTRL, CHARG_WD_KICK);
	if (ret)
		dev_err(di->dev, "Failed to kick WD!\n");

	ab8500_chg_reenable(di);

	/* Schedule a new watchdog kick */
	queue_delayed_work(di->charger_wq,
		&di->kick_wd_work, round_jiffies(WD_KICK_INTERVAL));
}

#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
/*
 * [Problem] When disconnected the charger in particular board,
 * vbus disconnection interrupt didn't occur.
 *
 * [Cause]  ABB bug
 *
 * [Solution]
 * When the MainChgDrop bit is 1 and MainChargerDetDbnc is 1.
 * then the s/w will start
 * polling these bits every 10ms for up to 100msec.
 * During this time, If at least one of these bits
 * becomes 0, this short interval polling is stopped.
 * This indicates the normal case where the interrupt
 * is generated by teh ABB.
 * If the bits remain 1 for the entire 100ms period,
 * then the charger is assumed to be disconnected and
 * the charger is disabled.
 */
static void ab8500_chg_attached_work(struct work_struct *work)
{

	int i;
	int ret;
	u8 statval;
	union power_supply_propval value;

	struct ab8500_charger_info *di = container_of(work,
					 struct ab8500_charger_info,
					 chg_attached_work.work);

	for (i = 0 ; i < 10; i++) {
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_CHARGER,	AB8500_CH_STATUS2_REG, &statval);
		if (ret < 0) {
			dev_err(di->dev, "abb read failed %d\n",
				__LINE__);
			goto reschedule;
		}
		if ((statval & (MAIN_CH_STATUS2_MAINCHGDROP |
				MAIN_CH_STATUS2_MAINCHARGERDETDBNC)) !=
		     (MAIN_CH_STATUS2_MAINCHGDROP |
		      MAIN_CH_STATUS2_MAINCHARGERDETDBNC))
			goto reschedule;

		msleep(CHARGER_STATUS_POLL);
	}

	ab8500_chg_en(di, false);
	dev_info(di->dev, "%s, cable detach error occur\n", __func__);

	/* we should verify this routine */
	value.intval = POWER_SUPPLY_TYPE_BATTERY;
	psy_do_property("battery", set,
			POWER_SUPPLY_PROP_ONLINE, value);

	mutex_lock(&di->chg_attached_mutex);
	if (wake_lock_active(&di->chg_attached_wake_lock))
		wake_unlock(&di->chg_attached_wake_lock);
	mutex_unlock(&di->chg_attached_mutex);

	return;
reschedule:
	queue_delayed_work(di->charger_wq,
			   &di->chg_attached_work,
			   HZ);
}

#else

static void ab8500_chg_attached_work(struct work_struct *work)
{
	int i;
	int ret;
	u8 statval;
	union power_supply_propval value;

	struct ab8500_charger_info *di = container_of(work,
					 struct ab8500_charger_info,
					 chg_attached_work.work);

	for (i = 0 ; i < 10; i++) {
		ret = abx500_get_register_interruptible(di->dev,
			AB8500_CHARGER,	AB8500_CH_USBCH_STAT1_REG, &statval);
		if (ret < 0) {
			dev_err(di->dev, "abb read failed %d\n",
				__LINE__);
			goto reschedule;
		}
		if ((statval & (USB_CH_VBUSDROP |
				USB_CH_VBUSDETDBNC)) !=
		    (USB_CH_VBUSDROP | USB_CH_VBUSDETDBNC))
			goto reschedule;

		msleep(CHARGER_STATUS_POLL);
	}

	ab8500_chg_en(di, false);

	dev_info(di->dev, "%s, cable detach error occur\n", __func__);

	/* we should verify this routine */
	value.intval = POWER_SUPPLY_TYPE_BATTERY;
	psy_do_property("battery", set,
			POWER_SUPPLY_PROP_ONLINE, value);

	mutex_lock(&di->chg_attached_mutex);
	if (wake_lock_active(&di->chg_attached_wake_lock))
		wake_unlock(&di->chg_attached_wake_lock);
	mutex_unlock(&di->chg_attached_mutex);

	return;
reschedule:
	queue_delayed_work(di->charger_wq,
			   &di->chg_attached_work,
			   HZ);
}
#endif

static void ab8500_chg_fill_reg_data(struct ab8500_charger_info *di)
{
	int var_num = 0;
	int var_num_addr = 0;
	u8 val;
	int i;

	var_num_addr = scnprintf(di->flags.reg_addr, sizeof(di->flags.reg_addr),
			"addr :");
	var_num = scnprintf(di->flags.reg_data, sizeof(di->flags.reg_data),
			"data :");

	for (i = 0; i < ARRAY_SIZE(ab8500_chg_register); i++) {
		abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
				  ab8500_chg_register[i], &val);
		var_num_addr += scnprintf(di->flags.reg_addr+var_num_addr,
				 sizeof(di->flags.reg_addr)-var_num_addr,
				 " 0x%02x", ab8500_chg_register[i]);
		var_num += scnprintf(di->flags.reg_data+var_num,
				 sizeof(di->flags.reg_data)-var_num,
				 " 0x%02x", val);
	}
}

static void ab8500_chg_show_error_log(struct ab8500_charger_info *di)
{
	dev_info(di->dev, "ERROR IRQ_FLAG : 0x%x\n", di->flags.irq_flag);
	dev_info(di->dev, "FIRST_IRQ : 0x%x, time_stamps : %d\n",
		 di->flags.irq_first, di->flags.irq_first_time_stamps);
	pr_info("%s\n", di->flags.reg_addr);
	pr_info("%s\n", di->flags.reg_data);
}

static void ab8500_chg_set_charge(struct ab8500_charger_info *di,
				  bool enable)
{
	dev_info(di->dev, "%s (%s), cable type (%d)\n",
		 __func__, enable ? "enable" : "disable",
		 di->cable_type);

	if (enable) {
		ab8500_chg_en(di, true);
		di->is_charging = true;
		queue_delayed_work(di->charger_wq,
				   &di->kick_wd_work,
				   round_jiffies(WD_KICK_INTERVAL));
		mutex_lock(&di->chg_attached_mutex);
		if (!wake_lock_active(&di->chg_attached_wake_lock))
			wake_lock(&di->chg_attached_wake_lock);
		mutex_unlock(&di->chg_attached_mutex);
		queue_delayed_work(di->charger_wq,
				   &di->chg_attached_work,
				   HZ);
	} else {
		ab8500_chg_en(di, false);
		di->is_charging = false;
		cancel_delayed_work(&di->kick_wd_work);
		cancel_delayed_work_sync(
			&di->chg_attached_work);
		mutex_lock(&di->chg_attached_mutex);
		if (wake_lock_active(&di->chg_attached_wake_lock))
			wake_unlock(&di->chg_attached_wake_lock);
		mutex_unlock(&di->chg_attached_mutex);
	}
}

static bool ab8500_chg_check_ovp_status(struct ab8500_charger_info *di)
{
	int volt = ab8500_chg_get_vbus_voltage(di);
	int ovp_threshold;

	dev_info(di->dev, "VBUS Voltage : %d mV\n", volt);

	if (di->is_charging)
		ovp_threshold = get_battery_data(di).chg_params->volt_ovp;
	else
		ovp_threshold =
			get_battery_data(di).chg_params->volt_ovp_recovery;

	if (volt >= ovp_threshold &&
	    !(di->flags.irq_flag_shadow & F_VBUS_OVV)) {
		di->flags.irq_flag |= F_VBUS_OVV;
		di->flags.irq_flag_shadow |= F_VBUS_OVV;
		ab8500_chg_set_charge(di, false);
	} else if (volt < ovp_threshold &&
		   di->flags.irq_flag_shadow & F_VBUS_OVV) {
		di->flags.irq_flag &= ~F_VBUS_OVV;
		di->flags.irq_flag_shadow &= ~F_VBUS_OVV;
		ab8500_chg_set_charge(di, true);
	}
}



static void ab8500_chg_check_hw_failure_delay_work(struct work_struct *work)
{
	struct ab8500_charger_info *di = container_of(work,
		      struct ab8500_charger_info, check_hw_failure_work.work);

	/* TODO : should be removed after PVR */
	/* panic("ab850x charger bug"); */
	dev_info(di->dev, "%s, ab850x charger bug\n", __func__);

	/* if (di->cable_type != POWER_SUPPLY_TYPE_BATTERY) { */
	/*	di->flags.irq_flag &= ~F_CHG_WD_EXP; */
	/* 	di->flags.irq_flag &= ~F_MAIN_THERMAL_PROT; */
	/* 	di->flags.irq_flag &= ~F_USB_THERMAL_PROT; */
	/* 	di->flags.irq_flag_shadow &= ~F_CHG_WD_EXP; */
	/* 	di->flags.irq_flag_shadow &= ~F_MAIN_THERMAL_PROT; */
	/* 	di->flags.irq_flag_shadow &= ~F_USB_THERMAL_PROT; */

	/* 	ab8500_chg_set_charge(di, true); */
	/* } */
}

/**
 * ab8500_chg_check_hw_failure_work() - check main charger failure
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the main charger status
 */
static void ab8500_chg_check_hw_failure_work(struct work_struct *work)
{
	int ret;
	u8 chg_status;
	int i;
	int total_size;
	bool vbus_status;
	struct timespec ts;

	struct ab8500_charger_info *di = container_of(work,
		struct ab8500_charger_info, check_hw_failure_work.work);

	dev_info(di->dev, "HW failure handling start, 0x%x\n",
		di->flags.irq_flag);

	vbus_status = ab8500_chg_get_vbus_status(di);
	if (!vbus_status)
		goto end;

	if (di->flags.irq_flag) {
		if (di->flags.irq_first && !di->flags.reg_data[0]) {
			getnstimeofday(&ts);
			di->flags.irq_first_time_stamps = ts.tv_sec;
			ab8500_chg_fill_reg_data(di);
		}

		total_size =
		sizeof(ab8500_event_list) / sizeof(ab8500_event_list[0]);

		for (i = 0; i < total_size; i++) {
			if (di->flags.irq_flag &
			    ab8500_event_list[i].flag_mask)
				break;
		}

		/* all flag off */
		if (i == total_size)
			goto end;

		switch (ab8500_event_list[i].flag_mask) {
		case F_MAIN_EXT_CH_NOT_OK:
		case F_MAIN_THERMAL_PROT:
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_CHARGER, AB8500_CH_STATUS2_REG,
				&chg_status);
			if (ret < 0) {
				dev_err(di->dev, "%s: read failed\n",
					__func__);
				return;
			}
			break;

		case F_USB_CHARGER_NOT_OK:
		case F_USB_THERMAL_PROT:
		case F_VBUS_OVV:
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_CHARGER, AB8500_CH_USBCH_STAT2_REG,
				&chg_status);
			if (ret < 0) {
				dev_err(di->dev, "%s: read failed\n",
					__func__);
				return;
			}
			break;
		case F_BATT_OVV:
			ret = abx500_get_register_interruptible(di->dev,
				AB8500_CHARGER, AB8500_CH_STAT_REG,
				&chg_status);
			if (ret < 0) {
				dev_err(di->dev, "%s: read failed\n",
					__func__);
				return;
			}
			break;

		case F_CHG_WD_EXP:
			break;

		default:
			return;
		}

		dev_info(di->dev, " MASK : 0x%x, REG : 0x%x\n",
			 ab8500_event_list[i].flag_mask, chg_status);

		if ((chg_status & ab8500_event_list[i].reg_mask) &&
		    !(di->flags.irq_flag_shadow &
		     ab8500_event_list[i].flag_mask)) {
			/* first interrupt handling */
			/* we check the irq_flag_shadow
			   to aviod duplicated notification */
			di->flags.irq_flag_shadow |=
				ab8500_event_list[i].flag_mask;
			dev_info(di->dev, "CHARGER STOP by %s\n",
				 ab8500_event_list[i].name);
			ab8500_chg_set_charge(di, false);
			if ((di->flags.irq_flag & F_CHG_WD_EXP) ||
			    (di->flags.irq_flag & F_MAIN_THERMAL_PROT) ||
			    (di->flags.irq_flag & F_USB_THERMAL_PROT)) {
				queue_delayed_work(di->charger_wq,
					&di->check_hw_failure_delay_work,
					round_jiffies(HZ*15));
			}
		} else if (!(chg_status & ab8500_event_list[i].reg_mask) &&
			   (di->flags.irq_flag_shadow &
			    ab8500_event_list[i].flag_mask)) {
			/* error is disapeared */
			di->flags.irq_flag &= ~ab8500_event_list[i].flag_mask;
			di->flags.irq_flag_shadow &=
				~ab8500_event_list[i].flag_mask;
			dev_info(di->dev, "CHARGER RE-START from %s\n",
				 ab8500_event_list[i].name);
			ab8500_chg_set_charge(di, true);
		} else if (!(chg_status & ab8500_event_list[i].reg_mask)) {
			/* error is disapeared */
			di->flags.irq_flag &= ~ab8500_event_list[i].flag_mask;
			di->flags.irq_flag_shadow &=
				~ab8500_event_list[i].flag_mask;
		}

		if (di->flags.irq_flag) {
			if (di->flags.irq_first)
				ab8500_chg_show_error_log(di);
			goto enqueue;
		} else
			goto end;
	} else
		goto end;

enqueue:
	if (di->flags.irq_flag)
		queue_delayed_work(di->charger_wq,
		   &di->check_hw_failure_work, round_jiffies(HZ*5));
	return;
end:
	if (delayed_work_pending(&di->check_hw_failure_work))
		cancel_delayed_work(&di->check_hw_failure_work);

	memset(&di->flags, 0, sizeof(struct ab8500_charger_event_flags));
}

static void ab8500_handle_main_voltage_drop_work(struct work_struct *work)
{
	struct ab8500_charger_info *di = container_of(work,
					struct ab8500_charger_info,
					handle_main_voltage_drop_work);

	return;
}

static void ab8500_handle_vbus_voltage_drop_work(struct work_struct *work)
{
	int regval;
	int ret;

	struct ab8500_charger_info *di = container_of(work,
					struct ab8500_charger_info,
					handle_vbus_voltage_drop_work);

	regval = ab8500_vbus_in_curr_to_regval(di->input_current_limit);
	di->vbus_drop_count++;

	if (di->vbus_drop_count > VBUS_DROP_COUNT_LIMIT) {
		/* Limit current */
		if (regval > 0)
			regval--;
		di->input_current_limit = ab8500_chg_vbus_in_curr_map[regval];
		di->vbus_drop_count = 0;
	}

	dev_info(di->dev,
		 "%s Set input current: %dmA, vbus drop count:%d\n",
		 __func__,
		 di->input_current_limit,
		 di->vbus_drop_count);

	ret = ab8500_chg_set_input_curr(di, di->input_current_limit, true);

	if (ret) {
		dev_err(di->dev, "%s(), setting USBChInputCurr failed\n",
			__func__);
	}
}

/**
 * ab8500_chg_mainchunplugdet_handler() - main charger unplugged
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_mainchunplugdet_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "%s Main charger unplugged\n", __func__);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_mainchplugdet_handler() - main charger plugged
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_mainchplugdet_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "%s Main charger plugged\n", __func__);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_mainextchnotok_handler() - main charger not ok
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_mainextchnotok_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "Main charger not ok\n");
	di->flags.irq_flag |= F_MAIN_EXT_CH_NOT_OK;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_MAIN_EXT_CH_NOT_OK;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_mainchthprotr_handler() - Die temp is above main charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_mainchthprotr_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev,
		"Die temp above Main charger thermal protection threshold\n");

	di->flags.irq_flag |= F_MAIN_THERMAL_PROT;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_MAIN_THERMAL_PROT;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_mainchthprotf_handler() - Die temp is below main charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_mainchthprotf_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev,
		"Die temp ok for Main charger thermal protection threshold\n");

	di->flags.irq_flag |= F_MAIN_THERMAL_PROT;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_MAIN_THERMAL_PROT;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_vbusdetf_handler() - VBUS falling detected
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_vbusdetf_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "VBUS falling detected\n");

	get_battery_data(di).abb_set_vbus_state(false);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_vbusdetr_handler() - VBUS rising detected
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_vbusdetr_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "VBUS rising detected\n");

	get_battery_data(di).abb_set_vbus_state(true);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_usblinkstatus_handler() - USB link status has changed
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_usblinkstatus_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "USB link status changed\n");

	return IRQ_HANDLED;

}

/**
 * ab8500_chg_usbchthprotr_handler() - Die temp is above usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_usbchthprotr_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev,
		"Die temp above USB charger thermal protection threshold\n");

	di->flags.irq_flag |= F_USB_THERMAL_PROT;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_USB_THERMAL_PROT;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_usbchthprotf_handler() - Die temp is below usb charger
 * thermal protection threshold
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_usbchthprotf_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev,
		"Die temp ok for USB charger thermal protection threshold\n");

	di->flags.irq_flag |= F_USB_THERMAL_PROT;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_USB_THERMAL_PROT;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_usbchargernotokr_handler() - USB charger not ok detected
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_usbchargernotokr_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "Not allowed USB charger detected\n");

	di->flags.irq_flag |= F_USB_CHARGER_NOT_OK;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_USB_CHARGER_NOT_OK;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_chwdexp_handler() - Charger watchdog expired
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_chwdexp_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "Charger watchdog expired\n");

	di->flags.irq_flag |= F_CHG_WD_EXP;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_CHG_WD_EXP;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

static irqreturn_t ab8500_chg_main_drop_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;
	dev_err(di->dev, "Main charger voltage drop seen\n");
#if 0
	queue_work(di->charger_wq,
		   &di->handle_main_voltage_drop_work);
#endif
	return IRQ_HANDLED;
}


static irqreturn_t ab8500_chg_vbus_drop_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "Usb charger voltage drop seen\n");
	/* Wait 10s before trying to set current */
	queue_delayed_work(di->charger_wq,
			   &di->handle_vbus_voltage_drop_work, 10 * HZ);
	return IRQ_HANDLED;
}

/**
 * ab8500_chg_vbusovv_handler() - VBUS overvoltage detected
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_chg structure
 *
 * returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_vbusovv_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "VBUS overvoltage detected\n");

	di->flags.irq_flag |= F_VBUS_OVV;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_VBUS_OVV;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_batt_ovv_handler() - Battery OVV occured
 * @irq:       interrupt number
 * @data:       pointer to the ab8500_charger_info structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_batt_ovv_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_crit(di->dev, "Battery OVV\n");

	di->flags.irq_flag |= F_BATT_OVV;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_BATT_OVV;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_batctrlindb_handler() - battery removal detected
 * @irq:       interrupt number
 * @data:       void pointer that has to address of ab8500_chg
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_batctrlindb_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_err(di->dev, "Battery removal detected!\n");

	/* We will check the battery VF res for deciding whether
	   battery is valid or invalid */

#if 0
	di->flags.irq_flag |= F_BATT_REMOVE;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_BATT_REMOVE;

	queue_delayed_work(di->charger_wq, &di->check_hw_failure_work, 0);
#endif
	return IRQ_HANDLED;
}

/**
 * ab8500_chg_templow_handler() - battery temp lower than 10 degrees
 * @irq:       interrupt number
 * @data:       void pointer that has to address of ab8500_chg
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_templow_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	if (is_ab8500_3p3_or_earlier(di->parent)) {
		dev_dbg(di->dev, "Ignore false btemp low irq"
			" for ABB cut 1.0, 1.1, 2.0 and 3.3\n");
	} else {
		dev_crit(di->dev, "Battery temperature lower than -10deg c\n");

#if 0
		di->flags.irq_flag |= F_BTEMP_LOW;
		if (!di->flags.irq_first)
			di->flags.irq_first |= F_BTEMP_LOW;

		queue_delayed_work(di->charger_wq,
				   &di->check_hw_failure_work, 0);
#endif
	}

	return IRQ_HANDLED;
}

/**
 * ab8500_chg_temphigh_handler() - battery temp higher than max temp
 * @irq:       interrupt number
 * @data:       void pointer that has to address of ab8500_chg
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_temphigh_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_crit(di->dev, "Battery temperature is higher than MAX temp\n");

#if 0
	di->flags.irq_flag |= F_BTEMP_HIGH;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_BTEMP_HIGH;

	queue_delayed_work(di->charger_wq,
			   &di->check_hw_failure_work, 0);
#endif
	return IRQ_HANDLED;
}

/**
 * ab8500_chg_lowmed_handler() - battery temp between low and medium
 * @irq:       interrupt number
 * @data:       void pointer that has to address of ab8500_chg
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_lowmed_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_info(di->dev, "Battery temperature is between low and medium\n");

#if 0
	di->flags.irq_flag |= F_BTEMP_LOWMED;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_BTEMP_LOWMED;

	queue_delayed_work(di->charger_wq,
			   &di->check_hw_failure_work, 0);
#endif
	return IRQ_HANDLED;
}

/**
 * ab8500_chg_medhigh_handler() - battery temp between medium and high
 * @irq:       interrupt number
 * @data:       void pointer that has to address of ab8500_chg
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_chg_medhigh_handler(int irq, void *data)
{
	struct ab8500_charger_info *di = data;

	dev_info(di->dev, "Battery temperature is between medium and high\n");

#if 0
	di->flags.irq_flag |= F_BTEMP_MEDHIGH;
	if (!di->flags.irq_first)
		di->flags.irq_first |= F_BTEMP_MEDHIGH;

	queue_delayed_work(di->charger_wq,
			   &di->check_hw_failure_work, 0);
#endif
	return IRQ_HANDLED;
}

static int ab8500_chg_init_hw_registers(struct ab8500_charger_info *di)
{
	int ret = 0;

	/* Setup maximum charger current and voltage for ABB cut2.0 or
	   devices not an AB8500 */
	if (!is_ab8500_1p1_or_earlier(di->parent) ||
	    !is_ab8500(di->parent)) {
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_CH_VOLT_LVL_MAX_REG, CH_VOL_LVL_4P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_VOLT_LVL_MAX_REG\n");
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER,
			AB8500_CH_OPT_CRNTLVL_MAX_REG, CH_OP_CUR_LVL_1P6);
		if (ret) {
			dev_err(di->dev,
				"failed to set CH_OPT_CRNTLVL_MAX_REG\n");
			goto out;
		}
	}

	/* Set VBAT OVV threshold */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_BATT_OVV,
		BATT_OVV_TH_4P75,
		BATT_OVV_TH_4P75);
	if (ret) {
		dev_err(di->dev, "failed to set BATT_OVV\n");
		goto out;
	}

	/* Enable VBAT OVV detection */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_BATT_OVV,
		BATT_OVV_ENA,
		BATT_OVV_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable BATT_OVV\n");
		goto out;
	}

	/* VBUS OVV set to 6.6V and enable automatic current limitiation */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_USBCH_CTRL2_REG,
#ifdef CONFIG_CHARGER_AB8500_USE_MAINCHGBLK
		VBUS_OVV_SELECT_6P3V | VBUS_AUTO_IN_CURR_LIM_ENA);
#else
		0x44); /* 10V */
#endif
	if (ret) {
		dev_err(di->dev, "failed to set VBUS OVV\n");
		goto out;
	}

	/* Enable main watchdog in OTP */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_OTP_EMUL, AB8500_OTP_CONF_15, OTP_ENABLE_WD);
	if (ret) {
		dev_err(di->dev, "failed to enable main WD in OTP\n");
		goto out;
	}

	/* Enable main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG, MAIN_WDOG_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable main watchdog\n");
		goto out;
	}

	/*
	 * Due to internal synchronisation, Enable and Kick watchdog bits
	 * cannot be enabled in a single write.
	 * A minimum delay of 2*32 kHz period (62.5µs) must be inserted
	 * between writing Enable then Kick bits.
	 */
	udelay(63);

	/* Kick main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG,
		(MAIN_WDOG_ENA | MAIN_WDOG_KICK));
	if (ret) {
		dev_err(di->dev, "failed to kick main watchdog\n");
		goto out;
	}

	/* Disable main watchdog */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WDOG_CTRL_REG, MAIN_WDOG_DIS);
	if (ret) {
		dev_err(di->dev, "failed to disable main watchdog\n");
		goto out;
	}

	/* Set watchdog timeout */
	ret = abx500_set_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_CH_WD_TIMER_REG, WD_TIMER);
	if (ret) {
		dev_err(di->dev, "failed to set charger watchdog timeout\n");
		goto out;
	}

	/* Backup battery voltage and current */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_RTC,
		AB8500_RTC_BACKUP_CHG_REG,
		get_battery_data(di).bkup_bat_v |
		get_battery_data(di).bkup_bat_i);
	if (ret) {
		dev_err(di->dev, "failed to setup backup battery charging\n");
		goto out;
	}

	/* Enable backup battery charging */
	abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG,
		(RTC_BUP_CH_ENA | RTC_BUP_CH_OFF_VALID),
		(RTC_BUP_CH_ENA | RTC_BUP_CH_OFF_VALID));

	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

out:
	return ret;
}

static struct ab8500_charger_interrupts ab8500_chg_irq[] = {
	{"MAIN_CH_UNPLUG_DET", ab8500_chg_mainchunplugdet_handler},
	{"MAIN_CHARGE_PLUG_DET", ab8500_chg_mainchplugdet_handler},
	{"MAIN_EXT_CH_NOT_OK", ab8500_chg_mainextchnotok_handler},
	{"MAIN_CH_TH_PROT_R", ab8500_chg_mainchthprotr_handler},
	{"MAIN_CH_TH_PROT_F", ab8500_chg_mainchthprotf_handler},
	{"VBUS_DET_F", ab8500_chg_vbusdetf_handler},
	{"VBUS_DET_R", ab8500_chg_vbusdetr_handler},
	{"USB_LINK_STATUS", ab8500_chg_usblinkstatus_handler},
	{"USB_CH_TH_PROT_R", ab8500_chg_usbchthprotr_handler},
	{"USB_CH_TH_PROT_F", ab8500_chg_usbchthprotf_handler},
	{"USB_CHARGER_NOT_OKR", ab8500_chg_usbchargernotokr_handler},
	{"VBUS_OVV", ab8500_chg_vbusovv_handler},
	{"CH_WD_EXP", ab8500_chg_chwdexp_handler},
	{"MAIN_CH_DROP_END", ab8500_chg_main_drop_handler},
	{"VBUS_CH_DROP_END", ab8500_chg_vbus_drop_handler},
	{"BAT_CTRL_INDB", ab8500_chg_batctrlindb_handler},
	{"BATT_OVV", ab8500_chg_batt_ovv_handler},
	{"BTEMP_LOW", ab8500_chg_templow_handler},
	{"BTEMP_HIGH", ab8500_chg_temphigh_handler},
	{"BTEMP_LOW_MEDIUM", ab8500_chg_lowmed_handler},
	{"BTEMP_MEDIUM_HIGH", ab8500_chg_medhigh_handler},
};

#if defined(CONFIG_PM)
static int ab8500_chg_resume(struct platform_device *pdev)
{
	int ret;
	struct ab8500_charger_info *di = platform_get_drvdata(pdev);

	/* If we still have a HW failure, schedule a new check */
	if (di->flags.irq_flag) {
		queue_delayed_work(di->charger_wq,
			&di->check_hw_failure_work, 0);
	}

	flush_delayed_work_sync(&di->chg_attached_work);
	flush_delayed_work_sync(&di->kick_wd_work);

	return 0;
}

static int ab8500_chg_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_charger_info *di = platform_get_drvdata(pdev);

	/* Cancel any pending HW failure check */
	if (delayed_work_pending(&di->check_hw_failure_work))
		cancel_delayed_work(&di->check_hw_failure_work);

	flush_delayed_work_sync(&di->chg_attached_work);
	flush_delayed_work_sync(&di->kick_wd_work);

	return 0;
}
#else
#define ab8500_chg_suspend      NULL
#define ab8500_chg_resume       NULL
#endif


static int __devexit ab8500_chg_remove(struct platform_device *pdev)
{
	struct ab8500_charger_info *di = platform_get_drvdata(pdev);
	int i, irq, ret;

	/* Disable AC charging */
	ab8500_chg_en(di, false);

	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_chg_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_chg_irq[i].name);
		free_irq(irq, di);
	}

	/* disable the regulator */
	regulator_put(di->regu);

	/* Backup battery voltage and current disable */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_RTC, AB8500_RTC_CTRL_REG, RTC_BUP_CH_ENA, 0);
	if (ret < 0)
		dev_err(di->dev, "%s mask and set failed\n", __func__);

	/* Delete the work queue */
	destroy_workqueue(di->charger_wq);

	wake_lock_destroy(&di->chg_attached_wake_lock);

	flush_scheduled_work();

	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return 0;
}



static int ab8500_chg_set_property(struct power_supply *psy,
			      enum power_supply_property psp,
			      const union power_supply_propval *val)
{
	struct ab8500_charger_info *di =
		container_of(psy, struct ab8500_charger_info, psy);

	switch (psp) {
	/* val->intval : type */
	case POWER_SUPPLY_PROP_ONLINE:
		di->cable_type = val->intval;
		di->charging_current =
			di->pdata->charging_current[val->intval].
			fast_charging_current;

		switch (val->intval) {
		case POWER_SUPPLY_TYPE_BATTERY:
			if (delayed_work_pending(
				    &di->check_hw_failure_work))
				cancel_delayed_work(
					&di->check_hw_failure_work);
			if (delayed_work_pending(
				    &di->check_hw_failure_delay_work))
				cancel_delayed_work(
					&di->check_hw_failure_delay_work);
			memset(&di->flags, 0,
			       sizeof(struct ab8500_charger_event_flags));

			ab8500_chg_set_charge(di, false);
			if (get_battery_data(di).autopower_cfg)
				ab8500_enable_disable_sw_fallback(di, false);
			break;

		case POWER_SUPPLY_TYPE_MISC:
		case POWER_SUPPLY_TYPE_MAINS:
		case POWER_SUPPLY_TYPE_USB:
			ab8500_chg_set_charge(di, true);
			if (get_battery_data(di).autopower_cfg)
				ab8500_enable_disable_sw_fallback(di, true);
			break;
		default:
			return -EINVAL;
		}

		break;
	/* val->intval : change charging current */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ab8500_chg_set_output_curr(di, val->intval, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ab8500_chg_get_property(struct power_supply *psy,
			      enum power_supply_property psp,
			      union power_supply_propval *val)
{
	struct ab8500_charger_info *di =
		container_of(psy, struct ab8500_charger_info, psy);

	int vbus_status = ab8500_chg_get_vbus_status(di);
	int charger_state = ab8500_chg_get_charger_status(di);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (vbus_status && charger_state)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (vbus_status)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	/* Battery VF check */
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = ab8500_chg_vf_check(di);
		break;
	/* OVP Polling */
	case POWER_SUPPLY_PROP_HEALTH:
		ab8500_chg_check_ovp_status(di);
		if (di->flags.irq_flag_shadow & F_VBUS_OVV)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	/* Charging Current */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->charging_current;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int ab8500_chg_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(ab8500_charger_attrs); i++) {
		rc = device_create_file(dev, &ab8500_charger_attrs[i]);
		if (rc)
			goto create_attrs_failed;
	}
	goto create_attrs_succeed;

create_attrs_failed:
	dev_err(dev, "%s: failed (%d)\n", __func__, rc);
	while (i--)
		device_remove_file(dev, &ab8500_charger_attrs[i]);
create_attrs_succeed:
	return rc;
}

ssize_t ab8500_chg_show_attrs(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	const ptrdiff_t offset = attr - ab8500_charger_attrs;

	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_charger_info *di =
		container_of(psy, struct ab8500_charger_info, psy);

	int i = 0;
	char *str = NULL;

	switch (offset) {
	case CHG_REG:
		break;
	case CHG_DATA:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%02x\n",
			di->reg_data);
		break;
	case CHG_REGS:
		/* TODO : implement this */
		/* str = kzalloc(sizeof(char)*1024, GFP_KERNEL); */
		/* if (!str) */
		/*	return -ENOMEM; */

		/* smb328_read_regs(chg->client, str); */
		/* i += scnprintf(buf + i, PAGE_SIZE - i, "%s\n", */
		/*	str); */

		/* kfree(str); */
		break;
	default:
		i = -EINVAL;
	}

	return i;
}

ssize_t ab8500_chg_store_attrs(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	const ptrdiff_t offset = attr - ab8500_charger_attrs;

	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_charger_info *di =
		container_of(psy, struct ab8500_charger_info, psy);

	int ret = 0;
	int x = 0;
	u8 data = 0;

	switch (offset) {
	case CHG_REG:
		if (sscanf(buf, "%x\n", &x) == 1) {
			di->reg_addr = x;
			abx500_get_register_interruptible(di->dev,
				  AB8500_CHARGER, di->reg_addr, &data);
			di->reg_data = data;
			pr_debug("%s: (read) addr = 0x%x, data = 0x%x\n",
				__func__, di->reg_addr, di->reg_data);
		}
		ret = count;
		break;
	case CHG_DATA:
		if (sscanf(buf, "%x\n", &x) == 1) {
			data = (u8)x;
			pr_debug("%s: (write) addr = 0x%x, data = 0x%x\n",
				__func__, di->reg_addr, data);
			abx500_set_register_interruptible(di->dev,
				  AB8500_CHARGER, di->reg_addr, data);
		}
		ret = count;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __devinit ab8500_chg_probe(struct platform_device *pdev)
{
	struct ab8500_charger_info *di;
	struct ab8500_platform_data *plat;

	int ret = 0;
	int i, irq;

	dev_info(&pdev->dev,
		"%s : ABB Charger Driver Loading\n", __func__);
	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	platform_set_drvdata(pdev, di);
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	plat = dev_get_platdata(di->parent->dev);
	if (!plat) {
		dev_err(di->dev, "%s, platform data for abb chg is null\n",
			__func__);
		return -ENODEV;
	}

	di->pdata = plat->sec_bat;
	di->gpadc = ab8500_gpadc_get();

	di->psy.name		= "sec-charger";
	di->psy.type		= POWER_SUPPLY_TYPE_BATTERY;
	di->psy.get_property	= ab8500_chg_get_property;
	di->psy.set_property	= ab8500_chg_set_property;
	di->psy.properties		= ab8500_chg_props;

	mutex_init(&di->chg_attached_mutex);
	wake_lock_init(&di->chg_attached_wake_lock, WAKE_LOCK_SUSPEND,
		       "chg_attached_wake_lock");

	/* Create a work queue for the charger */
	di->charger_wq =
		create_singlethread_workqueue("ab8500_charger_wq");

	if (di->charger_wq == NULL) {
		dev_err(di->dev, "%s: failed to create work queue\n",
			__func__);
		goto err_wake_lock;
	}

	/* Init work for HW failure check */
	INIT_DELAYED_WORK_DEFERRABLE(&di->check_hw_failure_work,
		ab8500_chg_check_hw_failure_work);

	INIT_DELAYED_WORK_DEFERRABLE(&di->check_hw_failure_delay_work,
		ab8500_chg_check_hw_failure_delay_work);

	INIT_DELAYED_WORK(&di->chg_attached_work,
			  ab8500_chg_attached_work);

	INIT_DELAYED_WORK_DEFERRABLE(&di->kick_wd_work,
		ab8500_chg_kick_watchdog_work);

	/*
	 * VDD ADC supply needs to be enabled from this driver when there
	 * is a charger connected to avoid erroneous BTEMP_HIGH/LOW
	 * interrupts during charging
	 */
	di->regu = regulator_get(di->dev, "vddadc");
	if (IS_ERR(di->regu)) {
		ret = PTR_ERR(di->regu);
		dev_err(di->dev, "failed to get vddadc regulator\n");
		goto err_workqueue;
	}

	/* Init work for handling charger voltage drop  */
	INIT_WORK(&di->handle_main_voltage_drop_work,
		ab8500_handle_main_voltage_drop_work);
	INIT_DELAYED_WORK(&di->handle_vbus_voltage_drop_work,
			  ab8500_handle_vbus_voltage_drop_work);

	/* Initialize OVV, and other registers */
	ret = ab8500_chg_init_hw_registers(di);
	if (ret) {
		dev_err(di->dev,
			"%s: failed to initialize ABB registers\n", __func__);
		goto err_regulator;
	}

	/* Disable led control */
	ret = ab8500_chg_led_en(di, false);
	if (ret) {
		dev_err(di->dev,
			"%s: failed to disable led controller\n", __func__);
		goto err_regulator;
	}

	if (ab8500_chg_get_vbus_status(di))
		get_battery_data(di).abb_set_vbus_state(true);

	ret = power_supply_register(di->dev, &di->psy);
	if (ret) {
		dev_err(di->dev,
			"%s: failed to register psy\n", __func__);
		goto err_regulator;
	}

	ret = ab8500_chg_create_attrs(di->psy.dev);
	if (ret) {
		dev_err(&di->dev,
			"%s : Failed to create_attrs\n", __func__);
		goto err_supply_unreg;
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_chg_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_chg_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab8500_chg_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab8500_chg_irq[i].name, di);

		if (ret != 0) {
			dev_err(di->dev,
				"%s: failed to request %s IRQ %d: %d\n",
				__func__, ab8500_chg_irq[i].name, irq, ret);
			goto err_req_irq;
		}
		dev_dbg(di->dev, "%s: Requested %s IRQ %d: %d\n",
			__func__, ab8500_chg_irq[i].name, irq, ret);
	}

	return ret;

err_req_irq:
	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab8500_chg_irq[i].name);
		free_irq(irq, di);
	}
err_supply_unreg:
	power_supply_unregister(&di->psy);
err_regulator:
	regulator_put(di->regu);
err_workqueue:
	destroy_workqueue(di->charger_wq);
err_wake_lock:
	wake_lock_destroy(&di->chg_attached_wake_lock);
	mutex_destroy(&di->chg_attached_mutex);
	kfree(di);

	return ret;
}

static struct platform_driver ab8500_charger_driver = {
	.probe = ab8500_chg_probe,
	.remove = __devexit_p(ab8500_chg_remove),
	.suspend = ab8500_chg_suspend,
	.resume = ab8500_chg_resume,
	.driver = {
		.name = "ab8500-charger",
		.owner = THIS_MODULE,
	},
};

static int __init ab8500_charger_init(void)
{
	return platform_driver_register(&ab8500_charger_driver);
}

static void __exit ab8500_charger_exit(void)
{
	platform_driver_unregister(&ab8500_charger_driver);
}

subsys_initcall_sync(ab8500_charger_init);
module_exit(ab8500_charger_exit);
