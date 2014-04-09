/*
 * Copyright (C) Samsung Electornics 2010
 *
 * Samsung Keswick LCD display driver (S6E63M0 or LD9040)
 *
 * Author: Anirban Sarkar <anirban.sarkar@samsung.com>
 * for Samsung Electronics Ltd.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __MCDE_DISPLAY_SSG_DPI__H__
#define __MCDE_DISPLAY_SSG_DPI__H__

#include "mcde_display.h"

#define LCD_DRIVER_NAME_S6E63M0		"pri_lcd_s6e63m0"
#define LCD_DRIVER_NAME_S6D27A1		"pri_lcd_s6d27a1"
#define LCD_DRIVER_NAME_LD9040		"pri_lcd_ld9040"
#define LCD_DRIVER_NAME_GODIN		"pri_lcd_godin"
#define LCD_DRIVER_NAME_I9060		"pri_lcd_i9060"
#define LCD_DRIVER_NAME_GAVINI		"pri_lcd_gavini"
#define LCD_DRIVER_NAME_WS2401		"pri_lcd_ws2401"


#define BL_DEVICE_NAME			"pri_lcd_bl"

#define BL_DRIVER_NAME_S6E63M0		"pri_bl_s6e63m0"
#define BL_DRIVER_NAME_LD9040		"pri_bl_ld9040"
#define BL_DRIVER_NAME_KTD259		"pri_bl_ktd259"
#define BL_DRIVER_NAME_WS2401		"pri_bl_ws2401"

struct ssg_dpi_display_platform_data {
	/* it indicates whether lcd panel was enabled
	   from bootloader or not. */
	bool platform_enabled;

	/* Platform info */
	bool reset_high;
	int reset_gpio;
	int pwr_gpio;
	int bl_en_gpio;
	int bl_ctrl;

	/* Driver data */

	/* Power on sequence */
	int power_on_delay;	/* hw power on to hw reset (ms) */
	int reset_delay;	/* hw reset to sending commands (ms) */
	int sleep_out_delay;	/* EXIT SLEEP cmd to DISPLAY ON cmd (ms) */

	/* Power off sequence */
	int display_off_delay;  /* DISPLAY OFF cmd to ENTER SLEEP cmd (ms) */
	int sleep_in_delay;	/* ENTER SLEEP cmd to hw power off (ms) */

	/* The following is the minimum DDR OPP required when streaming video data.
	     Specify 0 if default minimum is sufficient.
	*/
	int min_ddr_opp;

	struct mcde_video_mode video_mode;

	/* reset lcd panel device. */
	int (*reset)(struct ssg_dpi_display_platform_data *pd);
	void	(*bl_on_off)(bool);
	/* on or off to lcd panel. If 'enable' is 0 then
	lcd power off and 1, lcd power on. */
	void (*lcd_pwr_setup)(struct device *);	
	int (*power_on)(struct ssg_dpi_display_platform_data *pd, int enable);

	int (*gpio_cfg_earlysuspend)(void);
	int (*gpio_cfg_lateresume)(void);
};

#endif /* __MCDE_DISPLAY_SSG_DPI__H__ */
