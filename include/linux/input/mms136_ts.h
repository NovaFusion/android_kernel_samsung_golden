/*
 * mms_ts.h - Platform data for Melfas MMS-series touch driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _LINUX_MMS_TOUCH_H
#define _LINUX_MMS_TOUCH_H

struct mms_ts_platform_data {
	int	max_x;
	int	max_y;

	bool	invert_x;
	bool	invert_y;
	bool	switch_xy;

	int	gpio_sda;
	int	gpio_scl;
	int	gpio_int;
	int	gpio_vdd_en;

	int	(*pin_configure)(bool to_gpios);
	const char	*fw_name_ums;
	const char	*fw_name_builtin;
	unsigned int	*key_map;
	unsigned int	key_nums;
	void	(*pin_set_pull)(int pin, bool to_up);
};

extern struct class *sec_class;

#endif /* _LINUX_MMS_TOUCH_H */
