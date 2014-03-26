
/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
*
* File Name	: lps001wp_prs.c
* Authors	: MSH - Motion Mems BU - Application Team
*		: Matteo Dameno (matteo.dameno@st.com)
*		: Carmine Iascone (carmine.iascone@st.com)
*		: Both authors are willing to be considered the contact
*		: and update points for the driver.
* Version	: V 1.1.1
* Date		: 2010/11/22
* Description	: LPS001WP pressure temperature sensor driver
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
******************************************************************************

 Revision 0.9.0 01/10/2010:
	first beta release
 Revision 1.1.0 05/11/2010:
	add sysfs management
 Revision 1.1.1 22/11/2010:
	moved to input/misc
******************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>

#include <linux/input/lps001wp.h>
#include <linux/earlysuspend.h>

#define	DEBUG	1

#define	PR_ABS_MAX	 0xffff
#define	PR_ABS_MIN	 0x0000
#define	PR_DLT_MAX	 0x7ffff
#define	PR_DLT_MIN	-0x80000 /* 16-bit signed value */
#define	TEMP_MAX	 0x7fff
#define TEMP_MIN	-0x80000 /* 16-bit signed value */


#define	SENSITIVITY_T_SHIFT	6	/** =	64 LSB/degrC	*/
#define	SENSITIVITY_P_SHIFT	4	/** =	16 LSB/mbar	*/


#define	OUTDATA_REG	0x28
#define	REF_PRESS_REG	0X30

#define	WHOAMI_LPS001WP_PRS	0xBA	/*	Expctd content for WAI	*/

/*	CONTROL REGISTERS	*/
#define	WHO_AM_I	0x0F		/*	WhoAmI register		*/
#define	CTRL_REG1	0x20		/*	power / ODR control reg	*/
#define	CTRL_REG2	0x21		/*	boot reg		*/
#define	CTRL_REG3	0x22		/*	interrupt control reg	*/

#define	STATUS_REG	0X27		/*	status reg		*/

#define PRESS_OUT_L	OUTDATA_REG


#define	REF_P_L		REF_PRESS_REG	/*	pressure reference	*/
#define	REF_P_H		0x31		/*	pressure reference	*/
#define	THS_P_L		0x32		/*	pressure threshold	*/
#define	THS_P_H		0x33		/*	pressure threshold	*/

#define	INT_CFG		0x34		/*	interrupt config	*/
#define	INT_SRC		0x35		/*	interrupt source	*/
#define	INT_ACK		0x36		/*	interrupt acknoledge	*/
/*	end CONTROL REGISTRES	*/


/* Barometer and Termometer output data rate ODR */
#define	LPS001WP_PRS_ODR_MASK	0x30	/* Mask to access odr bits only	*/
#define	LPS001WP_PRS_ODR_7_1	0x00	/* 7Hz baro and 1Hz term ODR	*/
#define	LPS001WP_PRS_ODR_7_7	0x10	/* 7Hz baro and 7Hz term ODR	*/
#define	LPS001WP_PRS_ODR_12_12	0x30	/* 12.5Hz baro and 12.5Hz term ODR */

#define	LPS001WP_PRS_ENABLE_MASK	0x40	/*  */
#define	LPS001WP_PRS_DIFF_MASK		0x08
#define LPS001WP_PRS_LPOW_MASK		0x80

#define	LPS001WP_PRS_DIFF_ON		0x08
#define	LPS001WP_PRS_DIFF_OFF		0x00

#define	LPS001WP_PRS_LPOW_ON		0x80
#define	LPS001WP_PRS_LPOW_OFF		0x00

#define	FUZZ			0
#define	FLAT			0
#define	I2C_RETRY_DELAY		5
#define	I2C_RETRIES		5
#define	I2C_AUTO_INCREMENT	0x80

/* RESUME STATE INDICES */
#define	RES_CTRL_REG1		0
#define	RES_CTRL_REG2		1
#define	RES_CTRL_REG3		2
#define	RES_REF_P_L		3
#define	RES_REF_P_H		4
#define	RES_THS_P_L		5
#define	RES_THS_P_H		6
#define	RES_INT_CFG		7

#define	RESUME_ENTRIES		8
/* end RESUME STATE INDICES */

/* Pressure Sensor Operating Mode */
#define	LPS001WP_PRS_DIFF_ENABLE	1
#define LPS001WP_PRS_DIFF_DISABLE	0
#define	LPS001WP_PRS_LPOWER_EN		1
#define	LPS001WP_PRS_LPOWER_DIS		0

static const struct {
	unsigned int cutoff_ms;
	unsigned int mask;
} lps001wp_prs_odr_table[] = {
		{80,	LPS001WP_PRS_ODR_12_12 },
		{143,	LPS001WP_PRS_ODR_7_7 },
		{1000,	LPS001WP_PRS_ODR_7_1 },
};

/**
 * struct lps001wp_prs_data - data structure used by lps001wp_prs driver
 * @client: i2c client
 * @pdata: lsm303dlh platform data
 * @lock: mutex lock for sysfs operations
 * @input_work: work queue to read sensor data
 * @input_dev: input device
 * @regulator: regulator
 * @early_suspend: early suspend structure
 * @hw_initialized: saves hw initialisation status
 * @hw_working: saves hw status
 * @diff_enabled: store value of diff enable
 * @lpowmode_enabled: flag to set lowpower mode
 * @enabled: to store mode of device
 * @on_before_suspend: to store status of device during suspend
 * @resume_state:store regester values
 * @reg_addr: stores reg address to debug
 */
struct lps001wp_prs_data {
	struct i2c_client *client;
	struct lps001wp_prs_platform_data *pdata;

	struct mutex lock;
#ifdef CONFIG_LPS001WP_INPUT_DEVICE
	struct delayed_work input_work;
	struct input_dev *input_dev;
#endif
	struct regulator *regulator;
	struct early_suspend early_suspend;

	int hw_initialized;
	/* hw_working=-1 means not tested yet */
	int hw_working;
	u8 diff_enabled;
	u8 lpowmode_enabled ;

	atomic_t enabled;
	int on_before_suspend;

	u8 resume_state[RESUME_ENTRIES];

#ifdef DEBUG
	u8 reg_addr;
#endif
};

struct outputdata {
	u16 abspress;
	s16 temperature;
	s16 deltapress;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void lps001wp_prs_early_suspend(struct early_suspend *data);
static void lps001wp_prs_late_resume(struct early_suspend *data);
#endif


static int lps001wp_prs_i2c_read(struct lps001wp_prs_data *prs,
				  u8 *buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
		 .addr = prs->client->addr,
		 .flags = prs->client->flags & I2C_M_TEN,
		 .len = 1,
		 .buf = buf,
		 },
		{
		 .addr = prs->client->addr,
		 .flags = (prs->client->flags & I2C_M_TEN) | I2C_M_RD,
		 .len = len,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(prs->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&prs->client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int lps001wp_prs_i2c_write(struct lps001wp_prs_data *prs,
				   u8 *buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
		 .addr = prs->client->addr,
		 .flags = prs->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(prs->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&prs->client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int lps001wp_prs_register_write(struct lps001wp_prs_data *prs, u8 *buf,
		u8 reg_address, u8 new_value)
{
	int err = -EINVAL;

	/* Sets configuration register at reg_address
	 *  NOTE: this is a straight overwrite  */
	buf[0] = reg_address;
	buf[1] = new_value;
	err = lps001wp_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		return err;
	return err;
}

static int lps001wp_prs_register_read(struct lps001wp_prs_data *prs, u8 *buf,
		u8 reg_address)
{

	int err = -EINVAL;
	buf[0] = (reg_address);
	err = lps001wp_prs_i2c_read(prs, buf, 1);

	return err;
}

static int lps001wp_prs_register_update(struct lps001wp_prs_data *prs, u8 *buf,
		u8 reg_address, u8 mask, u8 new_bit_values)
{
	int err = -EINVAL;
	u8 init_val;
	u8 updated_val;
	err = lps001wp_prs_register_read(prs, buf, reg_address);
	if (!(err < 0)) {
		init_val = buf[0];
		updated_val = ((mask & new_bit_values) | ((~mask) & init_val));
		err = lps001wp_prs_register_write(prs, buf, reg_address,
				updated_val);
	}
	return err;
}

/* */


static int lps001wp_prs_hw_init(struct lps001wp_prs_data *prs)
{
	int err = -EINVAL;
	u8 buf[6];

	dev_dbg(&prs->client->dev, "%s: hw init start\n",
					LPS001WP_PRS_DEV_NAME);

	buf[0] = WHO_AM_I;
	err = lps001wp_prs_i2c_read(prs, buf, 1);
	if (err < 0)
		goto error_firstread;
	else
		prs->hw_working = 1;
	if (buf[0] != WHOAMI_LPS001WP_PRS) {
		err = -EINVAL; /* TODO:choose the right coded error */
		goto error_unknown_device;
	}


	buf[0] = (I2C_AUTO_INCREMENT | REF_PRESS_REG);
	buf[1] = prs->resume_state[RES_REF_P_L];
	buf[2] = prs->resume_state[RES_REF_P_H];
	buf[3] = prs->resume_state[RES_THS_P_L];
	buf[4] = prs->resume_state[RES_THS_P_H];
	err = lps001wp_prs_i2c_write(prs, buf, 4);
	if (err < 0)
		goto error1;

	buf[0] = (I2C_AUTO_INCREMENT | CTRL_REG1);
	buf[1] = prs->resume_state[RES_CTRL_REG1];
	buf[2] = prs->resume_state[RES_CTRL_REG2];
	buf[3] = prs->resume_state[RES_CTRL_REG3];
	err = lps001wp_prs_i2c_write(prs, buf, 3);
	if (err < 0)
		goto error1;

	buf[0] = INT_CFG;
	buf[1] = prs->resume_state[RES_INT_CFG];
	err = lps001wp_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		goto error1;


	prs->hw_initialized = 1;
	dev_dbg(&prs->client->dev, "%s: hw init done\n", LPS001WP_PRS_DEV_NAME);
	return 0;

error_firstread:
	prs->hw_working = 0;
	dev_warn(&prs->client->dev, "Error reading WHO_AM_I: is device "
		"available/working?\n");
	goto error1;
error_unknown_device:
	dev_err(&prs->client->dev,
		"device unknown. Expected: 0x%x,"
		" Replies: 0x%x\n", WHOAMI_LPS001WP_PRS, buf[0]);
error1:
	prs->hw_initialized = 0;
	dev_err(&prs->client->dev, "hw init error 0x%x,0x%x: %d\n", buf[0],
			buf[1], err);
	return err;
}

static void lps001wp_prs_device_power_off(struct lps001wp_prs_data *prs)
{
	int err;
	u8 buf[2] = { CTRL_REG1, LPS001WP_PRS_PM_OFF };

	err = lps001wp_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		dev_err(&prs->client->dev, "soft power off failed: %d\n", err);

	if (prs->pdata->power_off) {
		prs->pdata->power_off();
		prs->hw_initialized = 0;
	}
	if (prs->hw_initialized) {
		prs->hw_initialized = 0;
	}

}

static int lps001wp_prs_device_power_on(struct lps001wp_prs_data *prs)
{
	int err = -EINVAL;

	if (prs->pdata->power_on) {
		err = prs->pdata->power_on();
		if (err < 0) {
			dev_err(&prs->client->dev,
					"power_on failed: %d\n", err);
			return err;
		}
	}

	if (!prs->hw_initialized) {
		err = lps001wp_prs_hw_init(prs);
		if (prs->hw_working == 1 && err < 0) {
			lps001wp_prs_device_power_off(prs);
			return err;
		}
	}

	return 0;
}



int lps001wp_prs_update_odr(struct lps001wp_prs_data *prs, int poll_interval_ms)
{
	int err = -EINVAL;
	int i;

	u8 buf[2];
	u8 updated_val;
	u8 init_val;
	u8 new_val;
	u8 mask = LPS001WP_PRS_ODR_MASK;

	/* Following, looks for the longest possible odr interval scrolling the
	 * odr_table vector from the end (shortest interval) backward (longest
	 * interval), to support the poll_interval requested by the system.
	 * It must be the longest interval lower then the poll interval.*/
	for (i = ARRAY_SIZE(lps001wp_prs_odr_table) - 1; i >= 0; i--) {
		if (lps001wp_prs_odr_table[i].cutoff_ms <= poll_interval_ms)
			break;
	}

	new_val = lps001wp_prs_odr_table[i].mask;

	/* If device is currently enabled, we need to write new
	 *  configuration out to it */
	if (atomic_read(&prs->enabled)) {
		buf[0] = CTRL_REG1;
		err = lps001wp_prs_i2c_read(prs, buf, 1);
		if (err < 0)
			goto error;
		init_val = buf[0];
		prs->resume_state[RES_CTRL_REG1] = init_val;

		buf[0] = CTRL_REG1;
		updated_val = ((mask & new_val) | ((~mask) & init_val));
		buf[1] = updated_val;
		buf[0] = CTRL_REG1;
		err = lps001wp_prs_i2c_write(prs, buf, 1);
		if (err < 0)
			goto error;
		prs->resume_state[RES_CTRL_REG1] = updated_val;
	}
	return err;

error:
	dev_err(&prs->client->dev, "update odr failed 0x%x,0x%x: %d\n",
			buf[0], buf[1], err);

	return err;
}

static int lps001wp_prs_set_press_reference(struct lps001wp_prs_data *prs,
				u16 new_reference)
{
	int err = -EINVAL;
	u8 const reg_addressL = REF_P_L;
	u8 const reg_addressH = REF_P_H;
	u8 bit_valuesL, bit_valuesH;
	u8 buf[2];
	/*
	 * We need to set new configurations, only if device
	 * is currently enabled
	 */
	if (!atomic_read(&prs->enabled))
		return err;
	bit_valuesL = (u8) (new_reference & 0x00FF);
	bit_valuesH = (u8)((new_reference & 0xFF00) >> 8);

	err = lps001wp_prs_register_write(prs, buf, reg_addressL,
			bit_valuesL);
	if (err < 0)
		return err;
	err = lps001wp_prs_register_write(prs, buf, reg_addressH,
			bit_valuesH);
	if (err < 0) {
		lps001wp_prs_register_write(prs, buf, reg_addressL,
				prs->resume_state[RES_REF_P_L]);
		return err;
	}
	prs->resume_state[RES_REF_P_L] = bit_valuesL;
	prs->resume_state[RES_REF_P_H] = bit_valuesH;
	return err;
}

static int lps001wp_prs_get_press_reference(struct lps001wp_prs_data *prs,
		u16 *buf16)
{
	int err = -EINVAL;

	u8 bit_valuesL, bit_valuesH;
	u8 buf[2] = {0};
	u16 temp = 0;
	/*
	 * We need to read configurations, only if device
	 * is currently enabled
	 */
	if (!atomic_read(&prs->enabled))
		return err;
	err = lps001wp_prs_register_read(prs, buf, REF_P_L);
	if (err < 0)
		return err;
	bit_valuesL = buf[0];
	err = lps001wp_prs_register_read(prs, buf, REF_P_H);
	if (err < 0)
		return err;
	bit_valuesH = buf[0];

	temp = (((u16) bit_valuesH) << 8);
	*buf16 = (temp | ((u16) bit_valuesL));

	return err;
}

static int lps001wp_prs_lpow_manage(struct lps001wp_prs_data *prs, u8 control)
{
	int err = -EINVAL;
	u8 buf[2] = {0x00, 0x00};
	u8 const mask = LPS001WP_PRS_LPOW_MASK;
	u8 bit_values = LPS001WP_PRS_LPOW_OFF;

	/*
	 * We need to set new configurations, only if device
	 * is currently enabled
	 */
	if (!atomic_read(&prs->enabled))
		return err;
	if (control >= LPS001WP_PRS_LPOWER_EN) {
		bit_values = LPS001WP_PRS_LPOW_ON;
	}

	err = lps001wp_prs_register_update(prs, buf, CTRL_REG1,
			mask, bit_values);

	if (err < 0)
		return err;
	prs->resume_state[RES_CTRL_REG1] = ((mask & bit_values) |
			(~mask & prs->resume_state[RES_CTRL_REG1]));
	if (bit_values == LPS001WP_PRS_LPOW_ON)
		prs->lpowmode_enabled = 1;
	else
		prs->lpowmode_enabled = 0;
	return err;
}

static int lps001wp_prs_diffen_manage(struct lps001wp_prs_data *prs, u8 control)
{
	int err = -EINVAL;
	u8 buf[2] = {0x00, 0x00};
	u8 const mask = LPS001WP_PRS_DIFF_MASK;
	u8 bit_values = LPS001WP_PRS_DIFF_OFF;

	/*
	 * We need to set new configurations, only if device
	 * is currently enabled
	 */
	if (!atomic_read(&prs->enabled))
		return err;
	if (control >= LPS001WP_PRS_DIFF_ENABLE) {
		bit_values = LPS001WP_PRS_DIFF_ON;
	}

	err = lps001wp_prs_register_update(prs, buf, CTRL_REG1,
			mask, bit_values);

	if (err < 0)
		return err;
	prs->resume_state[RES_CTRL_REG1] = ((mask & bit_values) |
			(~mask & prs->resume_state[RES_CTRL_REG1]));
	if (bit_values == LPS001WP_PRS_DIFF_ON)
		prs->diff_enabled = 1;
	else
		prs->diff_enabled = 0;
	return err;
}


static int lps001wp_prs_get_presstemp_data(struct lps001wp_prs_data *prs,
						struct outputdata *out)
{
	int err = -EINVAL;
	/* Data bytes from hardware	PRESS_OUT_L, PRESS_OUT_H,
	 *				TEMP_OUT_L, TEMP_OUT_H,
	 *				DELTA_L, DELTA_H */
	u8 prs_data[6];

	u16 abspr;
	s16 temperature, deltapr;
	int regToRead = 4;
	prs_data[4] = 0;
	prs_data[5] = 0;

	if (prs->diff_enabled)
		regToRead = 6;

	prs_data[0] = (I2C_AUTO_INCREMENT | OUTDATA_REG);
	err = lps001wp_prs_i2c_read(prs, prs_data, regToRead);
	if (err < 0)
		return err;

	abspr = ((((u16) prs_data[1] << 8) | ((u16) prs_data[0])));
	temperature = ((s16) (((u16) prs_data[3] << 8) | ((u16)prs_data[2])));

	out->abspress = (abspr >> SENSITIVITY_P_SHIFT);
	out->temperature = (temperature >> SENSITIVITY_T_SHIFT);

	deltapr = ((s16) (((u16) prs_data[5] << 8) | ((u16)prs_data[4])));
	out->deltapress = deltapr;

	return err;
}

#ifdef  CONFIG_LPS001WP_INPUT_DEVICE
static void lps001wp_prs_report_values(struct lps001wp_prs_data *prs,
					struct outputdata *out)
{
	input_report_abs(prs->input_dev, ABS_PR, out->abspress);
	input_report_abs(prs->input_dev, ABS_TEMP, out->temperature);
	input_report_abs(prs->input_dev, ABS_DLTPR, out->deltapress);
	input_sync(prs->input_dev);
}
#endif

static int lps001wp_prs_enable(struct lps001wp_prs_data *prs)
{
	int err;

	if (!atomic_cmpxchg(&prs->enabled, 0, 1)) {
		if (prs->regulator)
			regulator_enable(prs->regulator);
		err = lps001wp_prs_device_power_on(prs);
		if (err < 0) {
			atomic_set(&prs->enabled, 0);
			return err;
		}
#ifdef CONFIG_LPS001WP_INPUT_DEVICE
		schedule_delayed_work(&prs->input_work,
			msecs_to_jiffies(prs->pdata->poll_interval));
#endif
	}

	return 0;
}

static int lps001wp_prs_disable(struct lps001wp_prs_data *prs)
{
	if (atomic_cmpxchg(&prs->enabled, 1, 0)) {
#ifdef CONFIG_LPS001WP_INPUT_DEVICE
		cancel_delayed_work_sync(&prs->input_work);
#endif
		lps001wp_prs_device_power_off(prs);
		if (prs->regulator)
			regulator_disable(prs->regulator);
	}

	return 0;
}

static ssize_t attr_get_polling_rate(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	int val;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	mutex_lock(&prs->lock);
	val = prs->pdata->poll_interval;
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_polling_rate(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	unsigned long interval_ms;
	int err = -EINVAL;

	if (strict_strtoul(buf, 10, &interval_ms))
		return -EINVAL;
	if (!interval_ms)
		return -EINVAL;
	mutex_lock(&prs->lock);
	prs->pdata->poll_interval = interval_ms;
	err = lps001wp_prs_update_odr(prs, interval_ms);
	if (err < 0) {
		dev_err(&prs->client->dev, "failed to update odr %ld\n",
								interval_ms);
		size = err;
	}
	mutex_unlock(&prs->lock);
	return size;
}

static ssize_t attr_get_diff_enable(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	u8 val;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	mutex_lock(&prs->lock);
	val = prs->diff_enabled;
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_diff_enable(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;
	int err = -EINVAL;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&prs->lock);
	err = lps001wp_prs_diffen_manage(prs, (u8) val);
	if (err < 0) {
		dev_err(&prs->client->dev, "failed to diff enable %ld\n", val);
		mutex_unlock(&prs->lock);
		return err;
	}
	mutex_unlock(&prs->lock);
	return size;
}

static ssize_t attr_get_enable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	int val = atomic_read(&prs->enabled);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		lps001wp_prs_enable(prs);
	else
		lps001wp_prs_disable(prs);

	return size;
}

static ssize_t attr_get_press_ref(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int err = -EINVAL;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	u16 val = 0;

	mutex_lock(&prs->lock);
	err = lps001wp_prs_get_press_reference(prs, &val);
	mutex_unlock(&prs->lock);
	if (err < 0) {
		dev_err(&prs->client->dev, "failed to get ref press\n");
		return err;
	}

	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_press_ref(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int err = -EINVAL;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val = 0;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val < PR_ABS_MIN || val > PR_ABS_MAX)
		return -EINVAL;

	mutex_lock(&prs->lock);
	err = lps001wp_prs_set_press_reference(prs, val);
	mutex_unlock(&prs->lock);
	if (err < 0) {
		dev_err(&prs->client->dev, "failed to set ref press %ld\n",
								val);
		return err;
	}
	return size;
}


static ssize_t attr_get_lowpowmode(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	u8 val;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	mutex_lock(&prs->lock);
	val = prs->lpowmode_enabled;
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_lowpowmode(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int err = -EINVAL;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&prs->lock);
	err = lps001wp_prs_lpow_manage(prs, (u8) val);
	mutex_unlock(&prs->lock);
	if (err < 0) {
		dev_err(&prs->client->dev, "failed to set low powermode\n");
		return err;
	}
	return size;
}
static ssize_t lps001wp_prs_get_press_data(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	struct outputdata out;
	int err = -EINVAL;
	mutex_lock(&prs->lock);
	/*
	 * If device is currently enabled, we need to read
	 *  data from it.
	 */
	if (!atomic_read(&prs->enabled))
		goto out;
	err = lps001wp_prs_get_presstemp_data(prs, &out);
	if (err < 0) {
		dev_err(&prs->client->dev, "get_pressure_data failed\n");
		goto out;
	}
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d", out.abspress);
out:
	mutex_unlock(&prs->lock);
	return err;
}

static ssize_t lps001wp_prs_get_deltapr_data(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	struct outputdata out;
	int err = -EINVAL;
	mutex_lock(&prs->lock);
	/*
	 * If device is currently enabled, we need to read
	 *  data from it.
	 */
	if (!atomic_read(&prs->enabled)) {
		mutex_unlock(&prs->lock);
		return err;
	}
	err = lps001wp_prs_get_presstemp_data(prs, &out);
	if (err < 0) {
		dev_err(&prs->client->dev, "get_deltapress_data failed\n");
		mutex_unlock(&prs->lock);
		return err;
	}
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d", out.deltapress);
}

static ssize_t lps001wp_prs_get_temp_data(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	struct outputdata out;
	int err = -EINVAL;
	mutex_lock(&prs->lock);
	/*
	 * If device is currently enabled, we need to read
	 *  data from it.
	 */
	if (!atomic_read(&prs->enabled)) {
		mutex_unlock(&prs->lock);
		return err;
	}
	err = lps001wp_prs_get_presstemp_data(prs, &out);
	if (err < 0) {
		dev_err(&prs->client->dev, "get_temperature_data failed\n");
		mutex_unlock(&prs->lock);
		return err;
	}
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d", out.temperature);
}
#ifdef DEBUG
static ssize_t attr_reg_set(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	int rc;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	u8 x[2];
	unsigned long val;

	if (strict_strtoul(buf, 16, &val))
		return -EINVAL;
	mutex_lock(&prs->lock);
	x[0] = prs->reg_addr;
	mutex_unlock(&prs->lock);
	x[1] = val;
	rc = lps001wp_prs_i2c_write(prs, x, 1);
	/*TODO: error need to be managed */
	return size;
}

static ssize_t attr_reg_get(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	ssize_t ret;
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	int rc;
	u8 data;

	mutex_lock(&prs->lock);
	data = prs->reg_addr;
	mutex_unlock(&prs->lock);
	rc = lps001wp_prs_i2c_read(prs, &data, 1);
	/*TODO: error need to be managed */
	ret = sprintf(buf, "0x%02x\n", data);
	return ret;
}

static ssize_t attr_addr_set(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;
	if (strict_strtoul(buf, 16, &val))
		return -EINVAL;
	mutex_lock(&prs->lock);
	prs->reg_addr = val;
	mutex_unlock(&prs->lock);
	return size;
}
#endif



static struct device_attribute attributes[] = {
	__ATTR(pollrate_ms, S_IWUSR | S_IRUGO, attr_get_polling_rate,
		attr_set_polling_rate),
	__ATTR(enable, S_IWUSR | S_IRUGO, attr_get_enable, attr_set_enable),
	__ATTR(diff_enable, S_IWUSR | S_IRUGO, attr_get_diff_enable,
		attr_set_diff_enable),
	__ATTR(press_reference, S_IWUSR | S_IRUGO, attr_get_press_ref,
		attr_set_press_ref),
	__ATTR(lowpow_enable, S_IWUSR | S_IRUGO, attr_get_lowpowmode,
		attr_set_lowpowmode),
	__ATTR(press_data, S_IRUGO, lps001wp_prs_get_press_data, NULL),
	__ATTR(temp_data, S_IRUGO, lps001wp_prs_get_temp_data, NULL),
	__ATTR(deltapr_data, S_IRUGO, lps001wp_prs_get_deltapr_data, NULL),
#ifdef DEBUG
	__ATTR(reg_value, S_IWUSR | S_IRUGO, attr_reg_get, attr_reg_set),
	__ATTR(reg_addr, S_IWUSR, NULL, attr_addr_set),
#endif
};

static int create_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto error;
	return 0;

error:
	for ( ; i >= 0; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s:Unable to create interface\n", __func__);
	return -1;
}

static int remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
	return 0;
}

#ifdef CONFIG_LPS001WP_INPUT_DEVICE
static void lps001wp_prs_input_work_func(struct work_struct *work)
{
	struct lps001wp_prs_data *prs;

	struct outputdata output;
	struct outputdata *out = &output;
	int err;

	prs = container_of((struct delayed_work *)work,
				struct lps001wp_prs_data, input_work);

	mutex_lock(&prs->lock);
	err = lps001wp_prs_get_presstemp_data(prs, out);
	if (err < 0)
		dev_err(&prs->client->dev, "get_pressure_data failed\n");
	else
		lps001wp_prs_report_values(prs, out);

	schedule_delayed_work(&prs->input_work,
				msecs_to_jiffies(prs->pdata->poll_interval));
	mutex_unlock(&prs->lock);
}

int lps001wp_prs_input_open(struct input_dev *input)
{
	struct lps001wp_prs_data *prs = input_get_drvdata(input);

	return lps001wp_prs_enable(prs);
}

void lps001wp_prs_input_close(struct input_dev *dev)
{
	struct lps001wp_prs_data *prs = input_get_drvdata(dev);

	lps001wp_prs_disable(prs);
}
#endif

static int lps001wp_prs_validate_pdata(struct lps001wp_prs_data *prs)
{
	prs->pdata->poll_interval = max(prs->pdata->poll_interval,
					prs->pdata->min_interval);

	/* Enforce minimum polling interval */
	if (prs->pdata->poll_interval < prs->pdata->min_interval) {
		dev_err(&prs->client->dev, "minimum poll interval violated\n");
		return -EINVAL;
	}

	return 0;
}
#ifdef CONFIG_LPS001WP_INPUT_DEVICE
static int lps001wp_prs_input_init(struct lps001wp_prs_data *prs)
{
	int err;
	INIT_DELAYED_WORK(&prs->input_work, lps001wp_prs_input_work_func);
	prs->input_dev = input_allocate_device();
	if (!prs->input_dev) {
		err = -ENOMEM;
		dev_err(&prs->client->dev, "input device allocate failed\n");
		goto err0;
	}

	prs->input_dev->open = lps001wp_prs_input_open;
	prs->input_dev->close = lps001wp_prs_input_close;
	prs->input_dev->name = LPS001WP_PRS_DEV_NAME;
	prs->input_dev->id.bustype = BUS_I2C;
	prs->input_dev->dev.parent = &prs->client->dev;

	input_set_drvdata(prs->input_dev, prs);

	set_bit(EV_ABS, prs->input_dev->evbit);

	input_set_abs_params(prs->input_dev, ABS_PR,
			PR_ABS_MIN, PR_ABS_MAX, FUZZ, FLAT);
	input_set_abs_params(prs->input_dev, ABS_TEMP,
			PR_DLT_MIN, PR_DLT_MAX, FUZZ, FLAT);
	input_set_abs_params(prs->input_dev, ABS_DLTPR,
			TEMP_MIN, TEMP_MAX, FUZZ, FLAT);


	prs->input_dev->name = "LPS001WP barometer";

	err = input_register_device(prs->input_dev);
	if (err) {
		dev_err(&prs->client->dev,
			"unable to register input polled device %s\n",
			prs->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(prs->input_dev);
err0:
	return err;
}

static void lps001wp_prs_input_cleanup(struct lps001wp_prs_data *prs)
{
	input_unregister_device(prs->input_dev);
	input_free_device(prs->input_dev);
}
#endif
static int lps001wp_prs_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct lps001wp_prs_data *prs;
	int err = -EINVAL;
	int tempvalue;

	pr_info("%s: probe start.\n", LPS001WP_PRS_DEV_NAME);

	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE |
					I2C_FUNC_SMBUS_BYTE_DATA |
					I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "client not smb-i2c capable:2\n");
		err = -EIO;
		goto exit_check_functionality_failed;
	}


	if (!i2c_check_functionality(client->adapter,
						I2C_FUNC_SMBUS_I2C_BLOCK)){
		dev_err(&client->dev, "client not smb-i2c capable:3\n");
		err = -EIO;
		goto exit_check_functionality_failed;
	}


	prs = kzalloc(sizeof(struct lps001wp_prs_data), GFP_KERNEL);
	if (prs == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for module data: "
					"%d\n", err);
		goto exit_alloc_data_failed;
	}

	mutex_init(&prs->lock);
	mutex_lock(&prs->lock);

	prs->client = client;
	i2c_set_clientdata(client, prs);

	prs->regulator = regulator_get(&client->dev, "vdd");
	if (IS_ERR(prs->regulator)) {
		dev_err(&client->dev, "failed to get regulator\n");
		err = PTR_ERR(prs->regulator);
		prs->regulator = NULL;
	}
	if (prs->regulator)
		regulator_enable(prs->regulator);

	if (i2c_smbus_read_byte(client) < 0) {
		dev_err(&client->dev, "i2c_smbus_read_byte error!!\n");
		goto err_mutexunlockfreedata;
	} else {
		dev_dbg(&client->dev, "%s Device detected!\n",
							LPS001WP_PRS_DEV_NAME);
	}

	/* read chip id */
	tempvalue = i2c_smbus_read_word_data(client, WHO_AM_I);
	if ((tempvalue & 0x00FF) == WHOAMI_LPS001WP_PRS) {
		dev_dbg(&client->dev, "%s I2C driver registered!\n",
							LPS001WP_PRS_DEV_NAME);
	} else {
		prs->client = NULL;
		dev_dbg(&client->dev, "I2C driver not registered!"
				" Device unknown\n");
		goto err_mutexunlockfreedata;
	}

	prs->pdata = kmalloc(sizeof(*prs->pdata), GFP_KERNEL);
	if (prs->pdata == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for pdata: %d\n",
				err);
		goto err_mutexunlockfreedata;
	}

	memcpy(prs->pdata, client->dev.platform_data, sizeof(*prs->pdata));

	err = lps001wp_prs_validate_pdata(prs);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data\n");
		goto exit_kfree_pdata;
	}

	i2c_set_clientdata(client, prs);


	if (prs->pdata->init) {
		err = prs->pdata->init();
		if (err < 0) {
			dev_err(&client->dev, "init failed: %d\n", err);
			goto err2;
		}
	}

	memset(prs->resume_state, 0, ARRAY_SIZE(prs->resume_state));

	prs->resume_state[RES_CTRL_REG1] = LPS001WP_PRS_PM_NORMAL;
	prs->resume_state[RES_CTRL_REG2] = 0x00;
	prs->resume_state[RES_CTRL_REG3] = 0x00;
	prs->resume_state[RES_REF_P_L] = 0x00;
	prs->resume_state[RES_REF_P_H] = 0x00;
	prs->resume_state[RES_THS_P_L] = 0x00;
	prs->resume_state[RES_THS_P_H] = 0x00;
	prs->resume_state[RES_INT_CFG] = 0x00;

	err = lps001wp_prs_device_power_on(prs);
	if (err < 0) {
		dev_err(&client->dev, "power on failed: %d\n", err);
		goto err2;
	}

	prs->diff_enabled = 0;
	prs->lpowmode_enabled = 0;
	atomic_set(&prs->enabled, 1);

	err = lps001wp_prs_update_odr(prs, prs->pdata->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr failed\n");
		goto err_power_off;
	}
#ifdef CONFIG_LPS001WP_INPUT_DEVICE
	err = lps001wp_prs_input_init(prs);
	if (err < 0) {
		dev_err(&client->dev, "input init failed\n");
		goto err_power_off;
	}
#endif

	err = create_sysfs_interfaces(&client->dev);
	if (err < 0) {
		dev_err(&client->dev,
			"device LPS001WP_PRS_DEV_NAME sysfs register failed\n");
		goto err_input_cleanup;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	prs->early_suspend.level =
			EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	prs->early_suspend.suspend = lps001wp_prs_early_suspend;
	prs->early_suspend.resume = lps001wp_prs_late_resume;
	register_early_suspend(&prs->early_suspend);
#endif


	lps001wp_prs_device_power_off(prs);

	if (prs->regulator)
		regulator_disable(prs->regulator);

	/* As default, do not report information */
	atomic_set(&prs->enabled, 0);


	mutex_unlock(&prs->lock);

	dev_info(&client->dev, "%s: probed\n", LPS001WP_PRS_DEV_NAME);

	return 0;

/*
remove_sysfs_int:
	remove_sysfs_interfaces(&client->dev);
*/
err_input_cleanup:
#ifdef CONFIG_LPS001WP_INPUT_DEVICE
	lps001wp_prs_input_cleanup(prs);
#endif
err_power_off:
	lps001wp_prs_device_power_off(prs);
err2:
	if (prs->pdata->exit)
		prs->pdata->exit();
exit_kfree_pdata:
	kfree(prs->pdata);

err_mutexunlockfreedata:
	mutex_unlock(&prs->lock);
	if (prs->regulator) {
		regulator_disable(prs->regulator);
		regulator_put(prs->regulator);
	}
	kfree(prs);
exit_alloc_data_failed:
exit_check_functionality_failed:
	dev_err(&client->dev, "%s: Driver Init failed\n",
					LPS001WP_PRS_DEV_NAME);
	return err;
}

static int __devexit lps001wp_prs_remove(struct i2c_client *client)
{
	struct lps001wp_prs_data *prs = i2c_get_clientdata(client);

#ifdef CONFIG_LPS001WP_INPUT_DEVICE
	lps001wp_prs_input_cleanup(prs);
#endif
	lps001wp_prs_device_power_off(prs);
	remove_sysfs_interfaces(&client->dev);
	if (prs->regulator) {
		/* Disable the regulator if device is enabled. */
		if (atomic_read(&prs->enabled))
			regulator_disable(prs->regulator);
		regulator_put(prs->regulator);
	}

	if (prs->pdata->exit)
		prs->pdata->exit();
	kfree(prs->pdata);
	kfree(prs);

	return 0;
}
#if (!defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM))
static int lps001wp_prs_resume(struct device *dev)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);

	if (prs->on_before_suspend)
		return lps001wp_prs_enable(prs);
	return 0;
}

static int lps001wp_prs_suspend(struct device *dev)
{
	struct lps001wp_prs_data *prs = dev_get_drvdata(dev);
	prs->on_before_suspend = atomic_read(&prs->enabled);
	return lps001wp_prs_disable(prs);
}

static const struct dev_pm_ops lps001wp_prs_dev_pm_ops = {
	.suspend = lps001wp_prs_suspend,
	.resume  = lps001wp_prs_resume,
};
#else
static void lps001wp_prs_early_suspend(struct early_suspend *data)
{
	struct lps001wp_prs_data *prs =
		container_of(data, struct lps001wp_prs_data, early_suspend);
	prs->on_before_suspend = atomic_read(&prs->enabled);
	lps001wp_prs_disable(prs);
}

static void lps001wp_prs_late_resume(struct early_suspend *data)
{
	struct lps001wp_prs_data *prs =
		container_of(data, struct lps001wp_prs_data, early_suspend);
	if (prs->on_before_suspend)
		lps001wp_prs_enable(prs);
}
#endif
static const struct i2c_device_id lps001wp_prs_id[]
		= { { LPS001WP_PRS_DEV_NAME, 0}, { },};

MODULE_DEVICE_TABLE(i2c, lps001wp_prs_id);

static struct i2c_driver lps001wp_prs_driver = {
	.driver = {
			.name = LPS001WP_PRS_DEV_NAME,
			.owner = THIS_MODULE,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
			.pm = &lps001wp_prs_dev_pm_ops,
#endif
	},
	.probe = lps001wp_prs_probe,
	.remove = __devexit_p(lps001wp_prs_remove),
	.id_table = lps001wp_prs_id,
};

static int __init lps001wp_prs_init(void)
{
	printk(KERN_DEBUG "%s barometer driver: init\n",
						LPS001WP_PRS_DEV_NAME);
	return i2c_add_driver(&lps001wp_prs_driver);
}

static void __exit lps001wp_prs_exit(void)
{
	#if DEBUG
	printk(KERN_DEBUG "%s barometer driver exit\n",
						LPS001WP_PRS_DEV_NAME);
	#endif
	i2c_del_driver(&lps001wp_prs_driver);
	return;
}

module_init(lps001wp_prs_init);
module_exit(lps001wp_prs_exit);

MODULE_DESCRIPTION("STMicrolelectronics lps001wp pressure sensor sysfs driver");
MODULE_AUTHOR("Matteo Dameno, Carmine Iascone, STMicroelectronics");
MODULE_LICENSE("GPL");

