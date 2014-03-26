/*
 * Copyright (C) 2011 ST-Ericsson SA
 * Author: Avinash A <avinash.a@stericsson.com> for ST-Ericsson
 * License terms:GNU General Public License (GPL) version 2
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/cyttsp.h>
#include <linux/delay.h>
#include <linux/amba/pl022.h>
#include <plat/pincfg.h>
#include <mach/hardware.h>

#include "pins-db5500.h"
#include "board-u5500.h"

/* cyttsp_gpio_board_init : configures the touch panel. */
static int cyttsp_plat_init(int on)
{
	int ret;

	ret = gpio_direction_output(CYPRESS_SLAVE_SELECT_GPIO, 1);
	if (ret < 0) {
		pr_err("slave select gpio direction failed\n");
		gpio_free(CYPRESS_SLAVE_SELECT_GPIO);
		return ret;
	}

	return 0;
}

static int cyttsp_wakeup(void)
{
	int ret;

	ret = gpio_request(CYPRESS_TOUCH_INT_PIN, "Wakeup_pin");
	if (ret < 0) {
		pr_err("touch gpio failed\n");
		return ret;
	}
	ret = gpio_direction_output(CYPRESS_TOUCH_INT_PIN, 1);
	if (ret < 0) {
		pr_err("touch gpio direction failed\n");
		goto out;
	}
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 0);
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 1);
	/*
	 * To wake up the controller from sleep
	 * state the interrupt pin needs to be
	 * pulsed twice with a delay greater
	 * than 2 micro seconds.
	 */
	udelay(3);
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 0);
	gpio_set_value(CYPRESS_TOUCH_INT_PIN, 1);
	ret = gpio_direction_input(CYPRESS_TOUCH_INT_PIN);
	if (ret < 0) {
		pr_err("touch gpio direction IN config failed\n");
		goto out;
	}
out:
	gpio_free(CYPRESS_TOUCH_INT_PIN);
	return 0;
}
static struct cyttsp_platform_data cyttsp_spi_platdata = {
	.maxx = 480,
	.maxy = 854,
	.flags = 0,
	.gen = CY_GEN3,
	.use_st = 0,
	.use_mt = 1,
	.use_trk_id = 0,
	.use_hndshk = 0,
	.use_sleep = 1,
	.use_gestures = 0,
	.use_load_file = 0,
	.use_force_fw_update = 0,
	.use_virtual_keys = 0,
	/* activate up to 4 groups and set active distance */
	.gest_set = CY_GEST_GRP_NONE | CY_ACT_DIST,
	/* change scn_type to enable finger and/or stylus detection */
	.scn_typ = 0xA5, /* autodetect finger+stylus; balanced mutual scan */
	.act_intrvl = CY_ACT_INTRVL_DFLT,  /* Active refresh interval; ms */
	.tch_tmout = CY_TCH_TMOUT_DFLT,   /* Active touch timeout; ms */
	.lp_intrvl = CY_LP_INTRVL_DFLT,   /* Low power refresh interval; ms */
	.init = cyttsp_plat_init,
	.mt_sync = input_mt_sync,
	.wakeup = cyttsp_wakeup,
	.name = CY_SPI_NAME,
	.irq_gpio = CYPRESS_TOUCH_INT_PIN,
	.rst_gpio = CYPRESS_TOUCH_RST_GPIO,
};

static void cyttsp_spi_cs_control(u32 command)
{
	if (command == SSP_CHIP_SELECT)
		gpio_set_value(CYPRESS_SLAVE_SELECT_GPIO, 0);
	else if (command == SSP_CHIP_DESELECT)
		gpio_set_value(CYPRESS_SLAVE_SELECT_GPIO, 1);
}

static struct pl022_config_chip cyttsp_ssp_config_chip = {
	.com_mode = INTERRUPT_TRANSFER,
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	/* we can act as master only */
	.hierarchy = SSP_MASTER,
	.slave_tx_disable = 0,
	.rx_lev_trig = SSP_RX_1_OR_MORE_ELEM,
	.tx_lev_trig = SSP_TX_16_OR_MORE_EMPTY_LOC,
	.ctrl_len = SSP_BITS_16,
	.wait_state = SSP_MWIRE_WAIT_ZERO,
	.duplex = SSP_MICROWIRE_CHANNEL_FULL_DUPLEX,
	.cs_control = cyttsp_spi_cs_control,
};

static struct spi_board_info cypress_spi_devices[] = {
	{
		.modalias = CY_SPI_NAME,
		.controller_data = &cyttsp_ssp_config_chip,
		.platform_data = &cyttsp_spi_platdata,
		.max_speed_hz = 1000000,
		.bus_num = 1,
		.chip_select = 0,
		.mode = SPI_MODE_0,
	}
};

void u5500_cyttsp_init(void)
{
	int ret = 0;

	ret = gpio_request(CYPRESS_SLAVE_SELECT_GPIO, "slave_select_gpio");
	if (ret < 0) {
		pr_err("slave select gpio failed\n");
		return;
	}
	if (cpu_is_u5500v2())
		cyttsp_spi_platdata.invert = true;
	spi_register_board_info(cypress_spi_devices,
			ARRAY_SIZE(cypress_spi_devices));
}
