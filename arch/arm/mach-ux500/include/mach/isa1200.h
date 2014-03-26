/*
 * ISA1200 Haptic controller platform-specific data.
 *
 * Copyright (c) Samsung 2011
 */

#ifndef _ISA1200_h_
#define	_ISA1200_h_

#include <linux/clk.h>

#define	ISA1200_I2C_DEVICE_NAME  "isa1200"



struct isa1200_platform_data
{
	unsigned int	mot_hen_gpio;
	unsigned int	mot_len_gpio;
	struct clk *mot_clk;
	int (* hw_setup)( void );
};


#endif
