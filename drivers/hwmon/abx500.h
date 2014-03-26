/*
 * Copyright (C) ST-Ericsson SA 2010
 * License terms: GNU General Public License v2
 * Author: Martin Persson <martin.persson@stericsson.com>
 */

#ifndef _ABX500_H
#define _ABX500_H

#define NUM_SENSORS 5

struct ab8500_gpadc;
struct ab5500_gpadc;
struct ab8500_btemp;
struct ab5500_btemp;
struct adc_auto_input;
struct abx500_temp;

/**
 * struct abx500_temp_ops - abx500 chip specific ops
 * @read_sensor: reads gpadc output
 * @irq_handler: irq handler
 * @show_name: hwmon device name
 * @show_label: hwmon attribute label
 * @is_visible: is attribute visible
 */
struct abx500_temp_ops {
	int (*read_sensor)(struct abx500_temp *, u8);
	int (*irq_handler)(int, struct abx500_temp *);
	ssize_t (*show_name)(struct device *,
			struct device_attribute *, char *);
	ssize_t (*show_label) (struct device *,
			struct device_attribute *, char *);
	int (*is_visible)(struct attribute *, int);
};

/**
 * struct abx500_temp - representation of temp mon device
 * @pdev: platform device
 * @hwmon_dev: hwmon device
 * @ab8500_gpadc: gpadc interface for ab8500
 * @ab5500_gpadc: gpadc interface for ab5500
 * @btemp: battery temperature interface for ab8500
 * @adc_auto_input: gpadc auto trigger
 * @gpadc_addr: gpadc channel address
 * @temp: sensor temperature input value
 * @min: sensor temperature min value
 * @max: sensor temperature max value
 * @max_hyst: sensor temperature hysteresis value for max limit
 * @crit: sensor temperature critical value
 * @min_alarm: sensor temperature min alarm
 * @max_alarm: sensor temperature max alarm
 * @max_hyst_alarm: sensor temperature hysteresis alarm
 * @crit_alarm: sensor temperature critical value alarm
 * @work: delayed work scheduled to monitor temperature periodically
 * @work_active: True if work is active
 * @power_off_work: delayed work scheduled to power off the system
		when critical temperature is reached
 * @lock: mutex
 * @gpadc_monitor_delay: delay between temperature readings in ms
 * @power_off_delay: delay before power off in ms
 * @monitored_sensors: number of monitored sensors
 */
struct abx500_temp {
	struct platform_device *pdev;
	struct device *hwmon_dev;
	struct ab8500_gpadc *ab8500_gpadc;
	struct ab5500_gpadc *ab5500_gpadc;
	struct ab8500_btemp *ab8500_btemp;
	struct ab5500_btemp *ab5500_btemp;	
	struct adc_auto_input *gpadc_auto;
	struct abx500_temp_ops ops;
	u8 gpadc_addr[NUM_SENSORS];
	unsigned long temp[NUM_SENSORS];
	unsigned long min[NUM_SENSORS];
	unsigned long max[NUM_SENSORS];
	unsigned long max_hyst[NUM_SENSORS];
	unsigned long crit[NUM_SENSORS];
	unsigned long min_alarm[NUM_SENSORS];
	unsigned long max_alarm[NUM_SENSORS];
	unsigned long max_hyst_alarm[NUM_SENSORS];
	unsigned long crit_alarm[NUM_SENSORS];
	struct delayed_work work;
	bool work_active;
	struct delayed_work power_off_work;
	struct mutex lock;
	/* Delay (ms) between temperature readings */
	unsigned long gpadc_monitor_delay;
	/* Delay (ms) before power off */
	unsigned long power_off_delay;
	int monitored_sensors;
};

int abx500_hwmon_init(struct abx500_temp *data) __init;

#endif /* _ABX500_H */
