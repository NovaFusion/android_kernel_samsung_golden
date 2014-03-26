/*
*	YAS529 Compass sensor specific header file.
*
*
*/
#ifndef _YAS529_H_
#define  _YAS529_H_


#define	YAS529_I2C_DEVICE_NAME	"yas529"

/* Name of the regulator of the YAS529 power supply */
#define	YAS529_REGULATOR 	"v-compass"

struct yas529_platform_data {
	unsigned int	nRST_gpio;
	const char	*regulator_name;
};

extern struct yas529_platform_data yas529_plat_data;

#endif

