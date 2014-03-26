/*
 * NETXTCHIP NTS series touchkey driver
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Author: Taeyoon Yoon <tyoony.yoon@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_NTS_TOUCHKEY_H
#define __LINUX_NTS_TOUCHKEY_H

#define NTS_TOUCHKEY_DEVICE	"nts_touchkey"

struct nts_touchkey_platform_data {
	u8	enable;
	u32	gpio_test;
	u32	gpio_reset;
	u32	gpio_scl;
	u32	gpio_sda;
	u32	gpio_int;
	u32	gpio_en;
	int	num_key;
	int	*keycodes;
	void	(*power)(bool);
	void	(*int_set_pull)(bool);
};

#endif /* __LINUX_NTS_TOUCHKEY_H */
