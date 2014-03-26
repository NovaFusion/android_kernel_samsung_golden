/*
 * Copyright (C) 2009 ST-Ericsson SA
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _U8500_STE_AUDIO_IO_H_
#define _U8500_STE_AUDIO_IO_H_


struct ab8500_audio_platform_data {
	int (*ste_gpio_altf_init) (void);
	int (*ste_gpio_altf_exit) (void);
};

#endif /* _U8500_STE_AUDIO_IO_H_ */
