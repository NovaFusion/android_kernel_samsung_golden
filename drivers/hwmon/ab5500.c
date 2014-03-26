/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Martin Persson <martin.persson@stericsson.com> for
 * ST-Ericsson.
 * License terms: GNU Gereral Public License (GPL) version 2
 *
 * Note:
 *
 * If/when the AB5500 thermal warning temperature is reached (threshold
 * 125C cannot be changed by SW), an interrupt is set and the driver
 * notifies user space via a sysfs event. If a shut down is not
 * triggered by user space and temperature reaches beyond critical
 * limit(130C) pm_power off is called.
 *
 * If/when AB5500 thermal shutdown temperature is reached a hardware
 * shutdown of the AB5500 will occur.
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab5500-gpadc.h>
#include <linux/mfd/abx500/ab5500-bm.h>
#include "abx500.h"

/* AB5500 driver monitors GPADC - XTAL_TEMP, PCB_TEMP,
 * BTEMP_BALL, BAT_CTRL and DIE_TEMP
 */
#define NUM_MONITORED_SENSORS    5

#define SHUTDOWN_AUTO_MIN_LIMIT                 -25
#define SHUTDOWN_AUTO_MAX_LIMIT                 130

static int ab5500_output_convert(int val, u8 sensor)
{
	int res = val;
	/* GPADC returns die temperature in Celsius
	 * convert it to millidegree celsius
	 */
	if (sensor == DIE_TEMP)
		res = val * 1000;

	return res;
}

static int ab5500_read_sensor(struct abx500_temp *data, u8 sensor)
{
	int val;
	/*
	 * Special treatment for BAT_CTRL node, since this
	 * temperature measurement is more complex than just
	 * an ADC readout
	 */
	if (sensor == BAT_CTRL)
		val = ab5500_btemp_get_batctrl_temp(data->ab5500_btemp);
	else
		val = ab5500_gpadc_convert(data->ab5500_gpadc, sensor);

	if (val < 0)
		return val;
	else
		return ab5500_output_convert(val, sensor);
}

static ssize_t ab5500_show_name(struct device *dev,
				struct device_attribute *devattr,
				char *buf)
{
	return sprintf(buf, "ab5500\n");
}

static ssize_t ab5500_show_label(struct device *dev,
				struct device_attribute *devattr,
				char *buf)
{
	char *name;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int index = attr->index;

	/*
	 * Make sure these labels correspond to the attribute indexes
	 * used when calling SENSOR_DEVICE_ATRR.
	 * Temperature sensors outside ab8500 (read via GPADC) are marked
	 * with prefix ext_
	 */
	switch (index) {
	case 1:
		name = "xtal_temp";
		break;
	case 2:
		name = "pcb_temp";
		break;
	case 3:
		name = "bat_temp";
		break;
	case 4:
		name = "bat_ctrl";
		break;
	case 5:
		name = "ab5500";
		break;
	default:
		return -EINVAL;
	}
	return sprintf(buf, "%s\n", name);
}

static int temp_shutdown_trig(int mux)
{
	pm_power_off();
	return 0;
}

static int ab5500_temp_shutdown_auto(struct abx500_temp *data)
{
	int ret;
	struct adc_auto_input *auto_ip;

	auto_ip = kzalloc(sizeof(struct adc_auto_input), GFP_KERNEL);
	if (!auto_ip) {
		dev_err(&data->pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	auto_ip->mux = DIE_TEMP;
	auto_ip->freq = MS500;
	/*
	 * As per product specification, voltage decreases as
	 * temperature increases. Hence the min and max values
	 * should be passed in reverse order.
	 */
	auto_ip->min = SHUTDOWN_AUTO_MAX_LIMIT;
	auto_ip->max = SHUTDOWN_AUTO_MIN_LIMIT;
	auto_ip->auto_adc_callback = temp_shutdown_trig;
	data->gpadc_auto = auto_ip;
	ret = ab5500_gpadc_convert_auto(data->ab5500_gpadc,
					data->gpadc_auto);
	if (ret < 0)
		kfree(auto_ip);

	return ret;
}

static int ab5500_is_visible(struct attribute *attr, int n)
{
	return attr->mode;
}

static int ab5500_temp_irq_handler(int irq, struct abx500_temp *data)
{
	/*
	 * Make sure the magic numbers below corresponds to the node
	 * used for AB5500 thermal warning from HW.
	 */
	mutex_lock(&data->lock);
	data->crit_alarm[4] = 1;
	mutex_unlock(&data->lock);
	sysfs_notify(&data->pdev->dev.kobj, NULL, "temp5_crit_alarm");
	dev_info(&data->pdev->dev, "ABX500 thermal warning,"
				" power off system now!\n");
	return 0;
}

int __init abx500_hwmon_init(struct abx500_temp *data)
{
	int err;

	data->ab5500_gpadc = ab5500_gpadc_get("ab5500-adc.0");
	if (IS_ERR(data->ab5500_gpadc))
		return PTR_ERR(data->ab5500_gpadc);

	data->ab5500_btemp = ab5500_btemp_get();
	if (IS_ERR(data->ab5500_btemp))
		return PTR_ERR(data->ab5500_btemp);

	err = ab5500_temp_shutdown_auto(data);
	if (err < 0) {
		dev_err(&data->pdev->dev, "Failed to register"
				" auto trigger(%d)\n", err);
		return err;
	}

	/*
	 * Setup HW defined data.
	 *
	 * Reference hardware (HREF):
	 *
	 * XTAL_TEMP, PCB_TEMP, BTEMP_BALL refer to millivolts and
	 * BAT_CTRL and DIE_TEMP refer to millidegrees
	 *
	 * Make sure indexes correspond to the attribute indexes
	 * used when calling SENSOR_DEVICE_ATRR
	 */
	data->gpadc_addr[0] = XTAL_TEMP;
	data->gpadc_addr[1] = PCB_TEMP;
	data->gpadc_addr[2] = BTEMP_BALL;
	data->gpadc_addr[3] = BAT_CTRL;
	data->gpadc_addr[4] = DIE_TEMP;
	data->monitored_sensors = NUM_MONITORED_SENSORS;

	data->ops.read_sensor = ab5500_read_sensor;
	data->ops.irq_handler = ab5500_temp_irq_handler;
	data->ops.show_name  = ab5500_show_name;
	data->ops.show_label = ab5500_show_label;
	data->ops.is_visible = ab5500_is_visible;

	return 0;
}
