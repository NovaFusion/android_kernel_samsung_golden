/*
 * GP2A proximity sensor platform-specific data.
 *
 * Copyright (c) Samsung 2010
 */

#ifndef _GP2A_h_
#define	_GP2A_h_


#define	GP2A_I2C_DEVICE_NAME	"gp2a"

struct gp2a_platform_data
{
	unsigned int	ps_vout_gpio;
	bool	als_supported;
	int	alsout;
	int (* hw_setup)( struct device * );
	int (* hw_teardown)( void );
	void (* hw_pwr)( bool );
};


#endif
