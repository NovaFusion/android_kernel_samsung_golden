/*
*	MMC3280 Compass sensor specific header file.
*
*
*/
#ifndef _MMC3280_H_
#define  _MMC3280_H_


#define	MMC3280_I2C_DEVICE_NAME	"mmc3280"

/* Name of the regulator of the MMC3280 power supply */
/* At present there is no regulator assigned to MMC3280. Change below  when there is one */
#define	MMC3280_REGULATOR 	NULL

struct mmc3280_platform_data {
	const char	*regulator_name;
};

extern struct mmc3280_platform_data mmc3280_plat_data;


#endif


