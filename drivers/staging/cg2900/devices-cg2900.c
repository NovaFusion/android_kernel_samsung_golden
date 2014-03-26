/*
 * Copyright (C) ST-Ericsson SA 2011
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * Hemant Gupta (hemant.gupta@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Board specific device support for the Linux Bluetooth HCI H:4 Driver
 * for ST-Ericsson connectivity controller.
 */

#define NAME			"devices-cg2900"
#define pr_fmt(fmt)		NAME ": " fmt "\n"

#include <asm/byteorder.h>
#include <asm-generic/errno-base.h>
#include <asm/mach-types.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mfd/ab8500.h>
#include <linux/regulator/consumer.h>
#include <mach/id.h>
#include <plat/pincfg.h>
#include "cg2900.h"
#include "devices-cg2900.h"

#define BT_VS_POWER_SWITCH_OFF		0xFD40

#define H4_HEADER_LENGTH		0x01
#define BT_HEADER_LENGTH		0x03

struct vs_power_sw_off_cmd {
	__le16	op_code;
	u8	len;
	u8	gpio_0_7_pull_up;
	u8	gpio_8_15_pull_up;
	u8	gpio_16_20_pull_up;
	u8	gpio_0_7_pull_down;
	u8	gpio_8_15_pull_down;
	u8	gpio_16_20_pull_down;
} __packed;

static struct sk_buff *dcg2900_get_power_switch_off_cmd
				(struct cg2900_chip_dev *dev, u16 *op_code)
{
	struct sk_buff *skb;
	struct vs_power_sw_off_cmd *cmd;
	struct dcg2900_info *info;
	int i;

	/* If connected chip does not support the command return NULL */
	if (!check_chip_revision_support(dev->chip.hci_revision))
		return NULL;

	dev_dbg(dev->dev, "Generating PowerSwitchOff command\n");

	info = dev->b_data;

	skb = alloc_skb(sizeof(*cmd) + H4_HEADER_LENGTH, GFP_KERNEL);
	if (!skb) {
		dev_err(dev->dev, "Could not allocate skb\n");
		return NULL;
	}

	skb_reserve(skb, H4_HEADER_LENGTH);
	cmd = (struct vs_power_sw_off_cmd *)skb_put(skb, sizeof(*cmd));
	cmd->op_code = cpu_to_le16(BT_VS_POWER_SWITCH_OFF);
	cmd->len = sizeof(*cmd) - BT_HEADER_LENGTH;
	/*
	 * Enter system specific GPIO settings here:
	 * Section data[3-5] is GPIO pull-up selection
	 * Section data[6-8] is GPIO pull-down selection
	 * Each section is a bitfield where
	 * - byte 0 bit 0 is GPIO 0
	 * - byte 0 bit 1 is GPIO 1
	 * - up to
	 * - byte 2 bit 4 which is GPIO 20
	 * where each bit means:
	 * - 0: No pull-up / no pull-down
	 * - 1: Pull-up / pull-down
	 * All GPIOs are set as input.
	 */
	if (!info->sleep_gpio_set) {
		struct cg2900_platform_data *pf_data;

		pf_data = dev_get_platdata(dev->dev);
		for (i = 0; i < 8; i++) {
			if (pf_data->gpio_sleep[i] == CG2900_PULL_UP)
				info->gpio_0_7_pull_up |= (1 << i);
			else if (pf_data->gpio_sleep[i] == CG2900_PULL_DN)
				info->gpio_0_7_pull_down |= (1 << i);
		}
		for (i = 8; i < 16; i++) {
			if (pf_data->gpio_sleep[i] == CG2900_PULL_UP)
				info->gpio_8_15_pull_up |= (1 << (i - 8));
			else if (pf_data->gpio_sleep[i] == CG2900_PULL_DN)
				info->gpio_8_15_pull_down |= (1 << (i - 8));
		}
		for (i = 16; i < 21; i++) {
			if (pf_data->gpio_sleep[i] == CG2900_PULL_UP)
				info->gpio_16_20_pull_up |= (1 << (i - 16));
			else if (pf_data->gpio_sleep[i] == CG2900_PULL_DN)
				info->gpio_16_20_pull_down |= (1 << (i - 16));
		}
		info->sleep_gpio_set = true;
	}
	cmd->gpio_0_7_pull_up = info->gpio_0_7_pull_up;
	cmd->gpio_8_15_pull_up = info->gpio_8_15_pull_up;
	cmd->gpio_16_20_pull_up = info->gpio_16_20_pull_up;
	cmd->gpio_0_7_pull_down = info->gpio_0_7_pull_down;
	cmd->gpio_8_15_pull_down = info->gpio_8_15_pull_down;
	cmd->gpio_16_20_pull_down = info->gpio_16_20_pull_down;


	if (op_code)
		*op_code = BT_VS_POWER_SWITCH_OFF;

	return skb;
}

static int dcg2900_init(struct cg2900_chip_dev *dev)
{
	int err = 0;
	struct dcg2900_info *info;
	struct cg2900_platform_data *pdata = dev_get_platdata(dev->dev);

	/* First retrieve and save the resources */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev->dev, "Could not allocate dcg2900_info\n");
		return -ENOMEM;
	}

	info->gbf_gpio = -1;
	info->pmuen_gpio = -1;
	info->bt_gpio = -1;

	if (!dev->pdev->num_resources) {
		dev_dbg(dev->dev, "No resources available\n");
		goto finished;
	}

	if (cpu_is_u5500())
		err = dcg2900_u5500_setup(dev, info);
	else
		err = dcg2900_u8500_setup(dev, info);

	if (err)
		goto err_handling;

	/*
	 * Enable the power on snowball
	 */
	if (machine_is_snowball()) {
		/* Take the regulator */
		if (pdata->regulator_id) {
			info->regulator_wlan = regulator_get(dev->dev,
					pdata->regulator_id);
			if (IS_ERR(info->regulator_wlan)) {
				err = PTR_ERR(info->regulator_wlan);
				dev_warn(dev->dev,
					"%s: Failed to get regulator '%s'\n",
					__func__, pdata->regulator_id);
				info->regulator_wlan = NULL;
				goto err_handling_free_gpios;
			}
			/* Enable it also */
			err = regulator_enable(info->regulator_wlan);
			if (err < 0) {
				dev_warn(dev->dev, "%s: regulator_enable failed\n",
						__func__);
				goto err_handling_put_reg;
			}
		} else {
			dev_warn(dev->dev, "%s: no regulator defined for snowball.\n",
					__func__);
		}
	}

finished:
	dev->b_data = info;
	return 0;
err_handling_put_reg:
	regulator_put(info->regulator_wlan);
err_handling_free_gpios:
	if (info->bt_gpio != -1)
		gpio_free(info->bt_gpio);
	if (info->pmuen_gpio != -1)
		gpio_free(info->pmuen_gpio);
	if (info->gbf_gpio != -1)
		gpio_free(info->gbf_gpio);
err_handling:
	kfree(info);
	return err;
}

static void dcg2900_exit(struct cg2900_chip_dev *dev)
{
	struct dcg2900_info *info = dev->b_data;

	if (machine_is_snowball()) {
		/* Turn off power if we have any */
		if (info->regulator_wlan) {
			regulator_disable(info->regulator_wlan);
			regulator_put(info->regulator_wlan);
		}
	}

	if (cpu_is_u5500())
		dcg2900_u5500_disable_chip(dev);
	else
		dcg2900_u8500_disable_chip(dev);

	if (info->bt_gpio != -1)
		gpio_free(info->bt_gpio);
	if (info->pmuen_gpio != -1)
		gpio_free(info->pmuen_gpio);
	if (info->gbf_gpio != -1)
		gpio_free(info->gbf_gpio);
	kfree(info);
	dev->b_data = NULL;
}

static int dcg2900_disable_uart(struct cg2900_chip_dev *dev)
{
	int err;
	struct cg2900_platform_data *pdata = dev_get_platdata(dev->dev);

	/*
	 * Without this delay we get interrupt on CTS immediately
	 * due to some turbulences on this line.
	 */
	mdelay(4);

	/* Disable UART functions. */
	err = nmk_config_pins(pdata->uart.uart_disabled,
			      pdata->uart.n_uart_gpios);
	if (err)
		goto error;

	return 0;

error:
	(void)nmk_config_pins(pdata->uart.uart_enabled,
			      pdata->uart.n_uart_gpios);
	dev_err(dev->dev, "Cannot set interrupt (%d)\n", err);
	return err;
}

static int dcg2900_enable_uart(struct cg2900_chip_dev *dev)
{
	int err;
	struct cg2900_platform_data *pdata = dev_get_platdata(dev->dev);

	/* Restore UART settings. */
	err = nmk_config_pins(pdata->uart.uart_enabled,
			      pdata->uart.n_uart_gpios);
	if (err)
		dev_err(dev->dev, "Unable to enable UART (%d)\n", err);

	return err;
}

void dcg2900_init_platdata(struct cg2900_platform_data *data)
{
	data->init = dcg2900_init;
	data->exit = dcg2900_exit;
	if (cpu_is_u5500()) {
		data->enable_chip = dcg2900_u5500_enable_chip;
		data->disable_chip = dcg2900_u5500_disable_chip;
	} else {
		data->enable_chip = dcg2900_u8500_enable_chip;
		data->disable_chip = dcg2900_u8500_disable_chip;
	}
	data->get_power_switch_off_cmd = dcg2900_get_power_switch_off_cmd;

	data->uart.enable_uart = dcg2900_enable_uart;
	data->uart.disable_uart = dcg2900_disable_uart;
}
