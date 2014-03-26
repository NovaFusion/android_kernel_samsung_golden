/*
 *
 * Zinitix BT404 touch driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_BT404_TS_H
#define __LINUX_BT404_TS_H

#define BT404_TS_DEVICE		"bt404_ts_device"
#define BT404_ISP_DEVICE	"bt404_isp_device"

/* max 8 */
#define	MAX_SUPPORTED_BUTTON_NUM 8

#define	TOUCH_V_FLIP	0x01
#define	TOUCH_H_FLIP	0x02
#define	TOUCH_XY_SWAP	0x041

enum {
	EX_CLEAR_PANEL = 0,
	GFF_PANEL,
};

enum {
	LDO_CON = 0,
	PMIC_CON,
};

struct bt404_ts_reg_data {
	s16 val;
	u8 valid;
};

struct bt404_ts_platform_data {
	u32		gpio_int;
	u32		gpio_scl;
	u32		gpio_sda;
	u32		gpio_ldo_en;
	u32		gpio_reset;
	u32		orientation;
	u32		x_max;
	u32		y_max;
	u32		num_buttons;
	u32		button_map[MAX_SUPPORTED_BUTTON_NUM];
	u32		num_regs;
	const struct bt404_ts_reg_data	*reg_data;
	u8		panel_type;
	u8		power_con;
	void (*put_isp_i2c_client)(struct i2c_client *client);
	struct i2c_client *(*get_isp_i2c_client)(void);
	void	(*int_set_pull)(bool to_up);
	int (*pin_configure)(bool to_gpios);
};

extern struct class *sec_class;

#endif /* __LINUX_BT404_TS_H */
