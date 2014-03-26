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
	int	max_x;		/* resolution of touch screen */
	int	max_y;

	bool	invert_x;	/* Allow for physical re-orientation */
	bool	invert_y;
	bool	switch_xy;

	int	num_rx;		/* number of RX channels in chip */
	int	num_tx;		/* number of TX channels in chip */
	int	fw_ver_reg;	/* Register address for Firmware version */
	int	max_fingers;	/* supported by firmware */

	int	gpio_sda;	/* needed for Firmware update */
	int	gpio_scl;
	int	gpio_int;
	int	(*pin_configure)(bool to_gpios);
	const char	*fw_name_ums;
	const char	*fw_name_builtin;

	unsigned int	*key_map;	/* for touch keys */
	unsigned int	key_nums;	/* set to 0 if no touch keys */

	void	(*pin_set_pull)(bool to_up); /* Set the TS pin pull ups for the platform. */
	void	(*vdd_on)(struct device *, bool);
};

extern struct class *sec_class;

#endif /* _LINUX_MMS_TOUCH_H */
