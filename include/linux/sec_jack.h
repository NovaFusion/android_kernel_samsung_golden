/*
 * Copyright (C) 2008 Samsung Electronics, Inc.
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

#ifndef __ASM_ARCH_SEC_HEADSET_H
#define __ASM_ARCH_SEC_HEADSET_H

#ifdef __KERNEL__

enum {
	SEC_JACK_NO_DEVICE		= 0x0,
	SEC_HEADSET_4POLE		= 0x01 << 0,
	SEC_HEADSET_3POLE		= 0x01 << 1,
	SEC_UNKNOWN_DEVICE		= 0x01 << 2,
};

struct sec_jack_zone {
	unsigned int adc_high;
	unsigned int delay_ms;
	unsigned int check_count;
	unsigned int jack_type;
};

struct sec_jack_buttons_zone {
	unsigned int code;
	unsigned int adc_low;
	unsigned int adc_high;
};

struct sec_jack_platform_data {
	void	(*set_micbias_state) (bool);
	int	(*get_adc_value) (void);
	int	(*get_det_level) (struct platform_device *pdev);
#if defined(CONFIG_MACH_GAVINI)
	void (*set_earspk_sel) (bool);
#endif
	void	(*mach_init) (struct platform_device *pdev);
	struct 	sec_jack_zone	*zones;
	struct 	sec_jack_buttons_zone	*buttons_zones;
	int	num_zones;
	int	num_buttons_zones;
	int	det_gpio;
	char 	*det_r;
	char 	*det_f;
	int	send_end_gpio;
	char 	*buttons_r;
	char 	*buttons_f;
	char 	*regulator_mic_source;
	bool	det_active_high;
	bool	send_end_active_high;
#ifdef CONFIG_MACH_SEC_GOLDEN
	int     ear_reselector_zone;
#endif
};
#endif

#endif
