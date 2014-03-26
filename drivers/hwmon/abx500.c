/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Martin Persson <martin.persson@stericsson.com> for
 * ST-Ericsson.
 * License terms: GNU Gereral Public License (GPL) version 2
 *
 * Note:
 *
 * ABX500 does not provide auto ADC, so to monitor the required
 * temperatures, a periodic work is used. It is more important
 * to not wake up the CPU than to perform this job, hence the use
 * of a deferred delay.
 *
 * A deferred delay for thermal monitor is considered safe because:
 * If the chip gets too hot during a sleep state it's most likely
 * due to external factors, such as the surrounding temperature.
 * I.e. no SW decisions will make any difference.
 *
 * If/when the ABX500 thermal warning temperature is reached (threshold
 * cannot be changed by SW), an interrupt is set and the driver
 * notifies user space via a sysfs event.
 *
 * If/when ABX500 thermal shutdown temperature is reached a hardware
 * shutdown of the ABX500 will occur.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include "abx500.h"

#define DEFAULT_MONITOR_DELAY 1000

/*
 * Thresholds are considered inactive if set to 0.
 * To avoid confusion for user space applications,
 * the temp monitor delay is set to 0 if all thresholds
 * are 0.
 */
static bool find_active_thresholds(struct abx500_temp *data)
{
	int i;
	for (i = 0; i < data->monitored_sensors; i++)
		if (data->max[i] != 0 || data->max_hyst[i] != 0
		    || data->min[i] != 0)
			return true;

	dev_dbg(&data->pdev->dev, "No active thresholds,"
		"cancel deferred job (if it exists)"
		"and reset temp monitor delay\n");
	cancel_delayed_work_sync(&data->work);
	data->work_active = false;
	return false;
}

static inline void schedule_monitor(struct abx500_temp *data)
{
	unsigned long delay_in_jiffies;
	delay_in_jiffies = msecs_to_jiffies(data->gpadc_monitor_delay);
	data->work_active = true;
	schedule_delayed_work(&data->work, delay_in_jiffies);
}

static inline void gpadc_monitor_exit(struct abx500_temp *data)
{
	cancel_delayed_work_sync(&data->work);
	data->work_active = false;
}

static void gpadc_monitor(struct work_struct *work)
{
	unsigned long delay_in_jiffies;
	int val, i, ret;
	/* Container for alarm node name */
	char alarm_node[30];

	bool updated_min_alarm = false;
	bool updated_max_alarm = false;
	bool updated_max_hyst_alarm = false;
	struct abx500_temp *data = container_of(work, struct abx500_temp,
						work.work);

	for (i = 0; i < data->monitored_sensors; i++) {
		/* Thresholds are considered inactive if set to 0 */
		if (data->max[i] == 0 && data->max_hyst[i] == 0
		    && data->min[i] == 0)
			continue;

		val = data->ops.read_sensor(data, data->gpadc_addr[i]);
		if (val < 0) {
			dev_err(&data->pdev->dev, "GPADC read failed\n");
				continue;
		}

		mutex_lock(&data->lock);
		if (data->min[i] != 0) {
			if (val < data->min[i]) {
				if (data->min_alarm[i] == 0) {
					data->min_alarm[i] = 1;
					updated_min_alarm = true;
				}
			} else {
				if (data->min_alarm[i] == 1) {
					data->min_alarm[i] = 0;
					updated_min_alarm = true;
				}
			}

		}
		if (data->max[i] != 0) {
			if (val > data->max[i]) {
				if (data->max_alarm[i] == 0) {
					data->max_alarm[i] = 1;
					updated_max_alarm = true;
				}
			} else {
				if (data->max_alarm[i] == 1) {
					data->max_alarm[i] = 0;
					updated_max_alarm = true;
				}
			}

		}
		if (data->max_hyst[i] != 0) {
			if (val > data->max_hyst[i]) {
				if (data->max_hyst_alarm[i] == 0) {
					data->max_hyst_alarm[i] = 1;
					updated_max_hyst_alarm = true;
				}
			} else {
				if (data->max_hyst_alarm[i] == 1) {
					data->max_hyst_alarm[i] = 0;
					updated_max_hyst_alarm = true;
				}
			}
		}
		mutex_unlock(&data->lock);

		/* hwmon attr index starts at 1, thus "i+1" below */
		if (updated_min_alarm) {
			ret = snprintf(alarm_node, 16, "temp%d_min_alarm",
				       (i + 1));
			if (ret < 0) {
				dev_err(&data->pdev->dev,
					"Unable to update alarm node (%d)",
					ret);
				break;
			}
			sysfs_notify(&data->pdev->dev.kobj, NULL, alarm_node);
		}
		if (updated_max_alarm) {
			ret = snprintf(alarm_node, 16, "temp%d_max_alarm",
				       (i + 1));
			if (ret < 0) {
				dev_err(&data->pdev->dev,
					"Unable to update alarm node (%d)",
					ret);
				break;
			}
			hwmon_notify(data->max_alarm[i], NULL);
			sysfs_notify(&data->pdev->dev.kobj, NULL, alarm_node);
		}
		if (updated_max_hyst_alarm) {
			ret = snprintf(alarm_node, 21, "temp%d_max_hyst_alarm",
				       (i + 1));
			if (ret < 0) {
				dev_err(&data->pdev->dev,
					"Unable to update alarm node (%d)",
					ret);
				break;
			}
			sysfs_notify(&data->pdev->dev.kobj, NULL, alarm_node);
		}
	}
	delay_in_jiffies = msecs_to_jiffies(data->gpadc_monitor_delay);
	data->work_active = true;
	schedule_delayed_work(&data->work, delay_in_jiffies);
}

static ssize_t set_temp_monitor_delay(struct device *dev,
				      struct device_attribute *devattr,
				      const char *buf, size_t count)
{
	int res;
	unsigned long delay_in_s;
	struct abx500_temp *data = dev_get_drvdata(dev);

	res = strict_strtoul(buf, 10, &delay_in_s);
	if (res < 0)
		return res;

	mutex_lock(&data->lock);
	data->gpadc_monitor_delay = delay_in_s * 1000;

	if (find_active_thresholds(data))
		schedule_monitor(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t set_temp_power_off_delay(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	int res;
	unsigned long delay_in_s;
	struct abx500_temp *data = dev_get_drvdata(dev);

	res = strict_strtoul(buf, 10, &delay_in_s);
	if (res < 0)
		return res;

	mutex_lock(&data->lock);
	data->power_off_delay = delay_in_s * 1000;
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_temp_monitor_delay(struct device *dev,
				       struct device_attribute *devattr,
				       char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	/* return time in s, not ms */
	return sprintf(buf, "%lu\n", (data->gpadc_monitor_delay) / 1000);
}

static ssize_t show_temp_power_off_delay(struct device *dev,
					 struct device_attribute *devattr,
					 char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	/* return time in s, not ms */
	return sprintf(buf, "%lu\n", (data->power_off_delay) / 1000);
}

/* HWMON sysfs interface */
static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	/*
	 * To avoid confusion between sensor label and chip name, the function
	 * "show_label" is not used to return the chip name.
	 */
	struct abx500_temp *data = dev_get_drvdata(dev);
	return data->ops.show_name(dev, devattr, buf);
}

static ssize_t show_label(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	return data->ops.show_label(dev, devattr, buf);
}

static ssize_t show_input(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	int val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	u8 gpadc_addr = data->gpadc_addr[attr->index - 1];

	val = data->ops.read_sensor(data, gpadc_addr);
	if (val < 0)
		dev_err(&data->pdev->dev, "GPADC read failed\n");

	return sprintf(buf, "%d\n", val);
}

/* set functions (RW nodes) */
static ssize_t set_min(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	unsigned long val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int res = strict_strtoul(buf, 10, &val);
	if (res < 0)
		return res;

	mutex_lock(&data->lock);
	/*
	 * Threshold is considered inactive if set to 0
	 * hwmon attr index starts at 1, thus "attr->index-1" below
	 */
	if (val == 0)
		data->min_alarm[attr->index - 1] = 0;

	data->min[attr->index - 1] = val;

	if (val == 0)
		(void) find_active_thresholds(data);
	else
		schedule_monitor(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t set_max(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	unsigned long val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int res = strict_strtoul(buf, 10, &val);
	if (res < 0)
		return res;

	mutex_lock(&data->lock);
	/*
	 * Threshold is considered inactive if set to 0
	 * hwmon attr index starts at 1, thus "attr->index-1" below
	 */
	if (val == 0)
		data->max_alarm[attr->index - 1] = 0;

	data->max[attr->index - 1] = val;

	if (val == 0)
		(void) find_active_thresholds(data);
	else
		schedule_monitor(data);

	mutex_unlock(&data->lock);

	return count;
}

static ssize_t set_max_hyst(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	unsigned long val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int res = strict_strtoul(buf, 10, &val);
	if (res < 0)
		return res;

	mutex_lock(&data->lock);
	/*
	 * Threshold is considered inactive if set to 0
	 * hwmon attr index starts at 1, thus "attr->index-1" below
	 */
	if (val == 0)
		data->max_hyst_alarm[attr->index - 1] = 0;

	data->max_hyst[attr->index - 1] = val;

	if (val == 0)
		(void) find_active_thresholds(data);
	else
		schedule_monitor(data);

	mutex_unlock(&data->lock);

	return count;
}

/*
 * show functions (RO nodes)
 */
static ssize_t show_min(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->min[attr->index - 1]);
}

static ssize_t show_max(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->max[attr->index - 1]);
}

static ssize_t show_max_hyst(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->max_hyst[attr->index - 1]);
}

/* Alarms */
static ssize_t show_min_alarm(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->min_alarm[attr->index - 1]);
}

static ssize_t show_max_alarm(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->max_alarm[attr->index - 1]);
}

static ssize_t show_max_hyst_alarm(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->max_hyst_alarm[attr->index - 1]);
}

static ssize_t show_crit_alarm(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	/* hwmon attr index starts at 1, thus "attr->index-1" below */
	return sprintf(buf, "%ld\n", data->crit_alarm[attr->index - 1]);
}

static mode_t abx500_attrs_visible(struct kobject *kobj,
				   struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct abx500_temp *data = dev_get_drvdata(dev);
	return data->ops.is_visible(a, n);
}

static SENSOR_DEVICE_ATTR(temp_monitor_delay, S_IRUGO | S_IWUSR,
			  show_temp_monitor_delay, set_temp_monitor_delay, 0);
static SENSOR_DEVICE_ATTR(temp_power_off_delay, S_IRUGO | S_IWUSR,
			  show_temp_power_off_delay,
			  set_temp_power_off_delay, 0);

/* Chip name, required by hwmon*/
static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, 0);

/* GPADC - SENSOR1 */
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_label, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_input, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_min, set_min, 1);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_max, set_max, 1);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 1);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_min_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_max_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_max_hyst_alarm, S_IRUGO,
			  show_max_hyst_alarm, NULL, 1);

/* GPADC - SENSOR2 */
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, show_label, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_input, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_min, set_min, 2);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_max, set_max, 2);
static SENSOR_DEVICE_ATTR(temp2_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 2);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_min_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_max_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_max_hyst_alarm, S_IRUGO,
			  show_max_hyst_alarm, NULL, 2);

/* GPADC - SENSOR3 */
static SENSOR_DEVICE_ATTR(temp3_label, S_IRUGO, show_label, NULL, 3);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_input, NULL, 3);
static SENSOR_DEVICE_ATTR(temp3_min, S_IWUSR | S_IRUGO, show_min, set_min, 3);
static SENSOR_DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_max, set_max, 3);
static SENSOR_DEVICE_ATTR(temp3_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 3);
static SENSOR_DEVICE_ATTR(temp3_min_alarm, S_IRUGO, show_min_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_max_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp3_max_hyst_alarm, S_IRUGO,
			  show_max_hyst_alarm, NULL, 3);

/* GPADC - SENSOR4 */
static SENSOR_DEVICE_ATTR(temp4_label, S_IRUGO, show_label, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_input, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_min, S_IWUSR | S_IRUGO, show_min, set_min, 4);
static SENSOR_DEVICE_ATTR(temp4_max, S_IWUSR | S_IRUGO, show_max, set_max, 4);
static SENSOR_DEVICE_ATTR(temp4_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 4);
static SENSOR_DEVICE_ATTR(temp4_min_alarm, S_IRUGO, show_min_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_max_alarm, S_IRUGO, show_max_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_max_hyst_alarm, S_IRUGO,
			  show_max_hyst_alarm, NULL, 4);

/* GPADC - SENSOR5 */
static SENSOR_DEVICE_ATTR(temp5_label, S_IRUGO, show_label, NULL, 5);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO, show_input, NULL, 5);
static SENSOR_DEVICE_ATTR(temp5_min, S_IWUSR | S_IRUGO, show_min, set_min, 5);
static SENSOR_DEVICE_ATTR(temp5_max, S_IWUSR | S_IRUGO, show_max, set_max, 5);
static SENSOR_DEVICE_ATTR(temp5_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 5);
static SENSOR_DEVICE_ATTR(temp5_min_alarm, S_IRUGO, show_min_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp5_max_alarm, S_IRUGO, show_max_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp5_max_hyst_alarm, S_IRUGO,
			  show_max_hyst_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp5_crit_alarm, S_IRUGO,
			  show_crit_alarm, NULL, 5);

struct attribute *abx500_temp_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_temp_monitor_delay.dev_attr.attr,
	&sensor_dev_attr_temp_power_off_delay.dev_attr.attr,
	/* GPADC SENSOR1 */
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst_alarm.dev_attr.attr,
	/* GPADC SENSOR2 */
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst_alarm.dev_attr.attr,
	/* GPADC SENSOR3 */
	&sensor_dev_attr_temp3_label.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_hyst_alarm.dev_attr.attr,
	/* GPADC SENSOR4 */
	&sensor_dev_attr_temp4_label.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp4_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_max_hyst_alarm.dev_attr.attr,
	/* GPADC SENSOR5*/
	&sensor_dev_attr_temp5_label.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp5_min.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp5_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp5_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_max_hyst_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_crit_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group abx500_temp_group = {
	.attrs = abx500_temp_attributes,
	.is_visible = abx500_attrs_visible,
};

static irqreturn_t abx500_temp_irq_handler(int irq, void *irq_data)
{
	struct platform_device *pdev = irq_data;
	struct abx500_temp *data = platform_get_drvdata(pdev);
	data->ops.irq_handler(irq, data);
	return IRQ_HANDLED;
}

static int setup_irqs(struct platform_device *pdev)
{
	int ret;
	int irq = platform_get_irq_byname(pdev, "ABX500_TEMP_WARM");

	if (irq < 0)
		dev_err(&pdev->dev, "Get irq by name failed\n");

	ret = request_threaded_irq(irq, NULL, abx500_temp_irq_handler,
				   IRQF_NO_SUSPEND, "abx500-temp", pdev);
	if (ret < 0)
		dev_err(&pdev->dev, "Request threaded irq failed (%d)\n", ret);

	return ret;
}

static int __devinit abx500_temp_probe(struct platform_device *pdev)
{
	struct abx500_temp *data;
	int err;

	data = kzalloc(sizeof(struct abx500_temp), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	mutex_init(&data->lock);

	/* Chip specific initialization */
	err = abx500_hwmon_init(data);
	if (err	< 0) {
		dev_err(&pdev->dev, "abx500 init failed");
		goto exit;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n", err);
		goto exit;
	}

	INIT_DELAYED_WORK_DEFERRABLE(&data->work, gpadc_monitor);
	data->gpadc_monitor_delay =  DEFAULT_MONITOR_DELAY;

	platform_set_drvdata(pdev, data);

	err = sysfs_create_group(&pdev->dev.kobj, &abx500_temp_group);
	if (err < 0) {
		dev_err(&pdev->dev, "Create sysfs group failed (%d)\n", err);
		goto exit_platform_data;
	}

	err = setup_irqs(pdev);
	if (err < 0) {
		dev_err(&pdev->dev, "irq setup failed (%d)\n", err);
		goto exit_sysfs_group;
	}
	return 0;

exit_sysfs_group:
	sysfs_remove_group(&pdev->dev.kobj, &abx500_temp_group);
exit_platform_data:
	hwmon_device_unregister(data->hwmon_dev);
	platform_set_drvdata(pdev, NULL);
exit:
	kfree(data->gpadc_auto);
	kfree(data);
	return err;
}

static int __devexit abx500_temp_remove(struct platform_device *pdev)
{
	struct abx500_temp *data = platform_get_drvdata(pdev);

	gpadc_monitor_exit(data);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &abx500_temp_group);
	platform_set_drvdata(pdev, NULL);
	kfree(data->gpadc_auto);
	kfree(data);
	return 0;
}

static int abx500_temp_suspend(struct platform_device *pdev,
			       pm_message_t state)
{
	struct abx500_temp *data = platform_get_drvdata(pdev);

	if (data->work_active)
		cancel_delayed_work_sync(&data->work);
	return 0;
}

static int abx500_temp_resume(struct platform_device *pdev)
{
	struct abx500_temp *data = platform_get_drvdata(pdev);

	if (data->work_active)
		schedule_monitor(data);
	return 0;
}

static struct platform_driver abx500_temp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "abx500-temp",
	},
	.suspend = abx500_temp_suspend,
	.resume = abx500_temp_resume,
	.probe = abx500_temp_probe,
	.remove = __devexit_p(abx500_temp_remove),
};

static int __init abx500_temp_init(void)
{
	return platform_driver_register(&abx500_temp_driver);
}

static void __exit abx500_temp_exit(void)
{
	platform_driver_unregister(&abx500_temp_driver);
}

MODULE_AUTHOR("Martin Persson <martin.persson@stericsson.com>");
MODULE_DESCRIPTION("ABX500 temperature driver");
MODULE_LICENSE("GPL");

module_init(abx500_temp_init)
module_exit(abx500_temp_exit)
