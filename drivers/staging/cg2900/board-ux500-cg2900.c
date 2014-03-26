/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Par-Gunnar Hjalmdahl <par-gunnar.p.hjalmdahl@stericsson.com>
 * Author: Hemant Gupta <hemant.gupta@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <asm/mach-types.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/mfd/abx500/ab8500-gpio.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
#include <mach/id.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>

#include "board-mop500.h"
#include "cg2900.h"
#include "devices-cg2900.h"
#include "pins-db5500.h"
#include "pins-db8500.h"
#include "pins.h"

#define CG2900_BT_ENABLE_GPIO		222
#define CG2900_GBF_ENA_RESET_GPIO	209
#define WLAN_PMU_EN_GPIO		199
#define WLAN_PMU_EN_GPIO_U9500		AB8500_PIN_GPIO(11)
#define CG2900_UX500_BT_CTS_GPIO		0
#define CG2900_U5500_BT_CTS_GPIO		168

enum cg2900_gpio_pull_sleep ux500_cg2900_sleep_gpio[21] = {
	CG2900_NO_PULL,		/* GPIO 0:  PTA_CONFX */
	CG2900_PULL_DN,		/* GPIO 1:  PTA_STATUS */
	CG2900_NO_PULL,		/* GPIO 2:  UART_CTSN */
	CG2900_PULL_UP,		/* GPIO 3:  UART_RTSN */
	CG2900_PULL_UP,		/* GPIO 4:  UART_TXD */
	CG2900_NO_PULL,		/* GPIO 5:  UART_RXD */
	CG2900_PULL_DN,		/* GPIO 6:  IOM_DOUT */
	CG2900_NO_PULL,		/* GPIO 7:  IOM_FSC */
	CG2900_NO_PULL,		/* GPIO 8:  IOM_CLK */
	CG2900_NO_PULL,		/* GPIO 9:  IOM_DIN */
	CG2900_PULL_DN,		/* GPIO 10: PWR_REQ */
	CG2900_PULL_DN,		/* GPIO 11: HOST_WAKEUP */
	CG2900_PULL_DN,		/* GPIO 12: IIS_DOUT */
	CG2900_NO_PULL,		/* GPIO 13: IIS_WS */
	CG2900_NO_PULL,		/* GPIO 14: IIS_CLK */
	CG2900_NO_PULL,		/* GPIO 15: IIS_DIN */
	CG2900_PULL_DN,		/* GPIO 16: PTA_FREQ */
	CG2900_PULL_DN,		/* GPIO 17: PTA_RF_ACTIVE */
	CG2900_NO_PULL,		/* GPIO 18: NotConnected (J6428) */
	CG2900_NO_PULL,		/* GPIO 19: EXT_DUTY_CYCLE */
	CG2900_NO_PULL,		/* GPIO 20: EXT_FRM_SYNCH */
};

static struct platform_device ux500_cg2900_device = {
	.name = "cg2900",
};

static struct platform_device ux500_cg2900_chip_device = {
	.name = "cg2900-chip",
	.dev = {
		.parent = &ux500_cg2900_device.dev,
	},
};

static struct platform_device ux500_stlc2690_chip_device = {
	.name = "stlc2690-chip",
	.dev = {
		.parent = &ux500_cg2900_device.dev,
	},
};

static struct cg2900_platform_data ux500_cg2900_test_platform_data = {
	.bus = HCI_VIRTUAL,
	.gpio_sleep = ux500_cg2900_sleep_gpio,
};

static struct platform_device ux500_cg2900_test_device = {
	.name = "cg2900-test",
	.dev = {
		.parent = &ux500_cg2900_device.dev,
		.platform_data = &ux500_cg2900_test_platform_data,
	},
};

static struct resource cg2900_uart_resources_pre_v60[] = {
	{
		.start = CG2900_GBF_ENA_RESET_GPIO,
		.end = CG2900_GBF_ENA_RESET_GPIO,
		.flags = IORESOURCE_IO,
		.name = "gbf_ena_reset",
	},
	{
		.start = CG2900_BT_ENABLE_GPIO,
		.end = CG2900_BT_ENABLE_GPIO,
		.flags = IORESOURCE_IO,
		.name = "bt_enable",
	},
	{
		.start = CG2900_UX500_BT_CTS_GPIO,
		.end = CG2900_UX500_BT_CTS_GPIO,
		.flags = IORESOURCE_IO,
		.name = "cts_gpio",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(CG2900_UX500_BT_CTS_GPIO),
		.end = NOMADIK_GPIO_TO_IRQ(CG2900_UX500_BT_CTS_GPIO),
		.flags = IORESOURCE_IRQ,
		.name = "cts_irq",
	},
};

static struct resource cg2900_uart_resources_u5500[] = {
	{
		.start = CG2900_U5500_BT_CTS_GPIO,
		.end = CG2900_U5500_BT_CTS_GPIO,
		.flags = IORESOURCE_IO,
		.name = "cts_gpio",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(CG2900_U5500_BT_CTS_GPIO),
		.end = NOMADIK_GPIO_TO_IRQ(CG2900_U5500_BT_CTS_GPIO),
		.flags = IORESOURCE_IRQ,
		.name = "cts_irq",
	},
};

static struct resource cg2900_uart_resources_u8500[] = {
	{
		.start = CG2900_GBF_ENA_RESET_GPIO,
		.end = CG2900_GBF_ENA_RESET_GPIO,
		.flags = IORESOURCE_IO,
		.name = "gbf_ena_reset",
	},
	{
		.start = WLAN_PMU_EN_GPIO,
		.end = WLAN_PMU_EN_GPIO,
		.flags = IORESOURCE_IO,
		.name = "pmu_en",
	},
	{
		.start = CG2900_UX500_BT_CTS_GPIO,
		.end = CG2900_UX500_BT_CTS_GPIO,
		.flags = IORESOURCE_IO,
		.name = "cts_gpio",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(CG2900_UX500_BT_CTS_GPIO),
		.end = NOMADIK_GPIO_TO_IRQ(CG2900_UX500_BT_CTS_GPIO),
		.flags = IORESOURCE_IRQ,
		.name = "cts_irq",
	},
};

static struct resource cg2900_uart_resources_u9500[] = {
	{
		.start = CG2900_GBF_ENA_RESET_GPIO,
		.end = CG2900_GBF_ENA_RESET_GPIO,
		.flags = IORESOURCE_IO,
		.name = "gbf_ena_reset",
	},
	{
		.start = WLAN_PMU_EN_GPIO_U9500,
		.end = WLAN_PMU_EN_GPIO_U9500,
		.flags = IORESOURCE_IO,
		.name = "pmu_en",
	},
	{
		.start = CG2900_UX500_BT_CTS_GPIO,
		.end = CG2900_UX500_BT_CTS_GPIO,
		.flags = IORESOURCE_IO,
		.name = "cts_gpio",
	},
	{
		.start = NOMADIK_GPIO_TO_IRQ(CG2900_UX500_BT_CTS_GPIO),
		.end = NOMADIK_GPIO_TO_IRQ(CG2900_UX500_BT_CTS_GPIO),
		.flags = IORESOURCE_IRQ,
		.name = "cts_irq",
	},
};

static pin_cfg_t u5500_cg2900_uart_enabled[] = {
	GPIO165_U3_RXD    | PIN_INPUT_PULLUP,
	GPIO166_U3_TXD    | PIN_OUTPUT_HIGH,
	GPIO167_U3_RTSn   | PIN_OUTPUT_HIGH,
	GPIO168_U3_CTSn   | PIN_INPUT_PULLUP,
};

static pin_cfg_t u5500_cg2900_uart_disabled[] = {
	GPIO165_GPIO   | PIN_INPUT_PULLUP,	/* RX pull down. */
	GPIO166_GPIO   | PIN_OUTPUT_LOW,	/* TX low - break on. */
	GPIO167_GPIO   | PIN_OUTPUT_HIGH,	/* RTS high-flow off. */
	GPIO168_GPIO   | PIN_INPUT_PULLUP,	/* CTS pull up. */
};

static pin_cfg_t ux500_cg2900_uart_enabled[] = {
	GPIO0_U0_CTSn   | PIN_INPUT_PULLUP,
	GPIO1_U0_RTSn   | PIN_OUTPUT_HIGH,
	GPIO2_U0_RXD    | PIN_INPUT_PULLUP,
	GPIO3_U0_TXD    | PIN_OUTPUT_HIGH
};

static pin_cfg_t ux500_cg2900_uart_disabled[] = {
	GPIO0_GPIO   | PIN_INPUT_PULLUP,	/* CTS pull up. */
	GPIO1_GPIO   | PIN_OUTPUT_HIGH,		/* RTS high-flow off. */
	GPIO2_GPIO   | PIN_INPUT_PULLUP,	/* RX pull down. */
	GPIO3_GPIO   | PIN_OUTPUT_LOW		/* TX low - break on. */
};

static struct cg2900_platform_data ux500_cg2900_uart_platform_data = {
	.bus = HCI_UART,
	.gpio_sleep = ux500_cg2900_sleep_gpio,
	.uart = {
		.n_uart_gpios = 4,
	},
};

static struct platform_device ux500_cg2900_uart_device = {
	.name = "cg2900-uart",
	.dev = {
		.platform_data = &ux500_cg2900_uart_platform_data,
		.parent = &ux500_cg2900_device.dev,
	},
};

static int __init board_cg2900_init(void)
{
	int err;

	dcg2900_init_platdata(&ux500_cg2900_test_platform_data);

		ux500_cg2900_uart_platform_data.uart.uart_enabled =
						ux500_cg2900_uart_enabled;
		ux500_cg2900_uart_platform_data.uart.uart_disabled =
						ux500_cg2900_uart_disabled;
		ux500_cg2900_uart_platform_data.regulator_id = "vdd";
	dcg2900_init_platdata(&ux500_cg2900_uart_platform_data);
			/* u8500 */
			ux500_cg2900_uart_device.num_resources =
					ARRAY_SIZE(cg2900_uart_resources_u8500);
			ux500_cg2900_uart_device.resource =
					cg2900_uart_resources_u8500;

	err = platform_device_register(&ux500_cg2900_device);
	if (err)
		return err;
	err = platform_device_register(&ux500_cg2900_uart_device);
	if (err)
		return err;
	err = platform_device_register(&ux500_cg2900_test_device);
	if (err)
		return err;
	err = platform_device_register(&ux500_cg2900_chip_device);
	if (err)
		return err;
	err = platform_device_register(&ux500_stlc2690_chip_device);
	if (err)
		return err;

	dev_info(&ux500_cg2900_device.dev, "CG2900 initialized\n");
	return 0;
}

static void __exit board_cg2900_exit(void)
{
	platform_device_unregister(&ux500_stlc2690_chip_device);
	platform_device_unregister(&ux500_cg2900_chip_device);
	platform_device_unregister(&ux500_cg2900_test_device);
	platform_device_unregister(&ux500_cg2900_uart_device);
	platform_device_unregister(&ux500_cg2900_device);

	dev_info(&ux500_cg2900_device.dev, "CG2900 removed\n");
}

module_init(board_cg2900_init);
module_exit(board_cg2900_exit);
