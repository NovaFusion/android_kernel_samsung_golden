/*
 * adp1653_plat.h
 * ADP1653 Led Flash Driver platform specific structures
 *
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Rajat Verma <rajat.verma@stericsson.com>
 *
 * License Terms: GNU General Public License v2
 */

#ifndef __LINUX_I2C_ADP1653_PLAT_H__
#define __LINUX_I2C_ADP1653_PLAT_H__

/**
 * struct adp1653_platform_data - platform data structure for adp1653
 * @enable_gpio: gpio for chip enable/disable
 * @irq_no: interrupt line for flash ic
 */
struct adp1653_platform_data {
	u32 enable_gpio;
	u32 irq_no;
};

#endif //__LINUX_I2C_ADP1653_PLAT_H__
