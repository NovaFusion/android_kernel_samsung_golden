/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Samsung MCDE DSI display driver
 *
 * Author: Robert Teather <robert.teather@samsung.com>
 * for Samsung Electronics.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __MCDE_DISPLAY_SEC_DSI_H__
#define __MCDE_DISPLAY_SEC_DSI_H__

#define SEC_DSI_MTP_DATA_LEN 21

struct  sec_dsi_platform_data {
	/* Platform info */
	int	reset_gpio;
	int	bl_en_gpio;
	bool	bl_ctrl;
	void	(*lcd_pwr_setup)(struct device *);
	void	(*lcd_pwr_onoff)(bool);

	/* The following is the minimum DDR OPP required when streaming video data.
	     Specify 0 if default minimum is sufficient.
	*/
	int	min_ddr_opp;
	bool	mtpAvail;
	u8	lcdId[3];
	u8	mtpData[SEC_DSI_MTP_DATA_LEN];
};

#endif /* __MCDE_DISPLAY_SEC_DSI_H__ */

