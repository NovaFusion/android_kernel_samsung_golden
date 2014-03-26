/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Martin Persson <martin.persson@stericsson.com> for
 * ST-Ericsson.
 * License terms: GNU Gereral Public License (GPL) version 2
 *
 * Note:
 *
 * If/when the AB8500 thermal warning temperature is reached (threshold
 * cannot be changed by SW), an interrupt is set and the driver
 * notifies user space via a sysfs event. If a shut down is not
 * triggered by user space within a certain time frame,
 * pm_power off is called.
 *
 * If/when AB8500 thermal shutdown temperature is reached a hardware
 * shutdown of the AB8500 will occur.
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include "abx500.h"

#define DEFAULT_POWER_OFF_DELAY 10000

/*
 * The driver monitors GPADC - ADC_AUX1, ADC_AUX2, BTEMP_BALL
 * and BAT_CTRL.
 */
#define NUM_MONITORED_SENSORS 4

static int ab8500_read_sensor(struct abx500_temp *data, u8 sensor)
{
	int val;
	/*
	 * Special treatment for the BAT_CTRL node, since this
	 * temperature measurement is more complex than just
	 * an ADC readout
	 */
	if (sensor == BAT_CTRL)
		val = ab8500_btemp_get_batctrl_temp(data->ab8500_btemp);
	else
		val = ab8500_gpadc_convert(data->ab8500_gpadc, sensor);

	return val;
}

static void ab8500_thermal_power_off(struct work_struct *work)
{
	struct abx500_temp *data = container_of(work, struct abx500_temp,
						power_off_work.work);

	dev_warn(&data->pdev->dev, "Power off due to AB8500 thermal warning\n");
	pm_power_off();
}

static ssize_t ab8500_show_name(struct device *dev,
				struct device_attribute *devattr,
				char *buf)
{
	return sprintf(buf, "ab8500\n");
}

static ssize_t ab8500_show_label(struct device *dev,
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
		name = "ext_rtc_xtal";
		break;
	case 2:
		name = "ext_db8500";
		break;
	case 3:
		name = "bat_temp";
		break;
	case 4:
		name = "bat_ctrl";
		break;
	case 5:
		name = "ab8500";
		break;
	default:
		return -EINVAL;
	}
	return sprintf(buf, "%s\n", name);
}

static int ab8500_is_visible(struct attribute *attr, int n)
{
	if (!strcmp(attr->name, "temp5_input") ||
	    !strcmp(attr->name, "temp5_min") ||
	    !strcmp(attr->name, "temp5_max") ||
	    !strcmp(attr->name, "temp5_max_hyst") ||
	    !strcmp(attr->name, "temp5_min_alarm") ||
	    !strcmp(attr->name, "temp5_max_alarm") ||
	    !strcmp(attr->name, "temp5_max_hyst_alarm"))
		return 0;

	return attr->mode;
}

static int ab8500_temp_irq_handler(int irq, struct abx500_temp *data)
{
	unsigned long delay_in_jiffies;
	/*
	 * Make sure the magic numbers below corresponds to the node
	 * used for AB8500 thermal warning from HW.
	 */
	mutex_lock(&data->lock);
	data->crit_alarm[4] = 1;
	mutex_unlock(&data->lock);

	hwmon_notify(data->crit_alarm[4], NULL);
	sysfs_notify(&data->pdev->dev.kobj, NULL, "temp5_crit_alarm");
	dev_info(&data->pdev->dev, "AB8500 thermal warning,"
		" power off in %lu s\n", data->power_off_delay);
	delay_in_jiffies = msecs_to_jiffies(data->power_off_delay);
	schedule_delayed_work(&data->power_off_work, delay_in_jiffies);
	return 0;
}

int __init abx500_hwmon_init(struct abx500_temp *data)
{
	data->ab8500_gpadc = ab8500_gpadc_get();
	if (IS_ERR(data->ab8500_gpadc))
		return PTR_ERR(data->ab8500_gpadc);

	data->ab8500_btemp = ab8500_btemp_get();
	if (IS_ERR(data->ab8500_btemp))
		return PTR_ERR(data->ab8500_btemp);

	INIT_DELAYED_WORK(&data->power_off_work, ab8500_thermal_power_off);

	/*
	 * Setup HW defined data.
	 *
	 * Reference hardware (HREF):
	 *
	 * GPADC - ADC_AUX1, connected to NTC R2148 next to RTC_XTAL on HREF
	 * GPADC - ADC_AUX2, connected to NTC R2150 near DB8500 on HREF
	 * Hence, temp#_min/max/max_hyst refer to millivolts and not
	 * millidegrees
	 * This is not the case for BAT_CTRL where millidegrees is used
	 *
	 * HREF HW does not support reading AB8500 temperature. BUT an
	 * AB8500 IRQ will be launched if die crit temp limit is reached.
	 *
	 * Make sure indexes correspond to the attribute indexes
	 * used when calling SENSOR_DEVICE_ATRR
	 */
	data->gpadc_addr[0] = ADC_AUX1;
	data->gpadc_addr[1] = ADC_AUX2;
	data->gpadc_addr[2] = BTEMP_BALL;
	data->gpadc_addr[3] = BAT_CTRL;
	data->gpadc_addr[4] = DIE_TEMP;
	data->power_off_delay = DEFAULT_POWER_OFF_DELAY;
	data->monitored_sensors = NUM_MONITORED_SENSORS;

	data->ops.read_sensor = ab8500_read_sensor;
	data->ops.irq_handler = ab8500_temp_irq_handler;
	data->ops.show_name  = ab8500_show_name;
	data->ops.show_label = ab8500_show_label;
	data->ops.is_visible = ab8500_is_visible;

	return 0;
}
