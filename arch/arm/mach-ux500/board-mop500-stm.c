/*
 * Copyright (C) 2011 ST-Ericsson
 *
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * Author: Olivier Germain <olivier.germain@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/regulator/consumer.h>

#include <asm/mach-types.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>
#include <mach/devices.h>
#include <mach/hsi.h>
#include <trace/stm.h>
#include "pins-db8500.h"

#define HREFV60_SDMMC_EN_GPIO 169
#define HREFV60_SDMMC_1V8_3V_GPIO 5

#define U8520_SDMMC_EN_GPIO 78
#define U8520_SDMMC_1V8_3V_GPIO 5

#define STM_DEVICE (&ux500_stm_device.dev)
#define STM_ERR(msg) dev_err(STM_DEVICE, msg)
#define STM_WARN(msg) dev_warn(STM_DEVICE, msg)

static struct regulator *regulator_aux3;
static enum stm_connection_type
	stm_current_connection = STM_STE_INVALID_CONNECTION;

static pin_cfg_t mop500_stm_mipi34_pins[] = {
	GPIO70_STMAPE_CLK | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO71_STMAPE_DAT3 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO72_STMAPE_DAT2 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO73_STMAPE_DAT1 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO74_STMAPE_DAT0 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO75_U2_RXD | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO76_U2_TXD | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
};

static pin_cfg_t mop500_stm_mipi60_pins[] = {
	GPIO153_U2_RXD,
	GPIO154_U2_TXD,
	GPIO155_STMAPE_CLK,
	GPIO156_STMAPE_DAT3,
	GPIO157_STMAPE_DAT2,
	GPIO158_STMAPE_DAT1,
	GPIO159_STMAPE_DAT0,
};

static pin_cfg_t mop500_stm_ape_microsd_pins[] = {
	GPIO23_MS_CLK  | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO24_MS_BS   | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO25_MS_DAT0 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO26_MS_DAT1 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO27_MS_DAT2 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO28_MS_DAT3 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
};

static pin_cfg_t mop500_ske_pins[] = {
	GPIO153_KP_I7 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO154_KP_I6 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO155_KP_I5 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO156_KP_I4 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO161_KP_I3 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO162_KP_I2 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO163_KP_I1 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO164_KP_I0 | PIN_INPUT_PULLDOWN | PIN_SLPM_INPUT_PULLUP,
	GPIO157_KP_O7 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO158_KP_O6 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO159_KP_O5 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO160_KP_O4 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO165_KP_O3 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO166_KP_O2 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO167_KP_O1 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
	GPIO168_KP_O0 | PIN_INPUT_PULLUP | PIN_SLPM_OUTPUT_LOW,
};

static pin_cfg_t mop500_stm_modem_microsd_pins[] = {
	GPIO18_GPIO        | PIN_OUTPUT_LOW,
	GPIO19_GPIO        | PIN_OUTPUT_HIGH,
	GPIO20_GPIO        | PIN_OUTPUT_HIGH,
	GPIO22_GPIO        | PIN_INPUT_PULLUP,
	GPIO23_STMMOD_CLK  | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO24_UARTMOD_RXD | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO25_STMMOD_DAT0 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO26_STMMOD_DAT1 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO27_STMMOD_DAT2 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
	GPIO28_STMMOD_DAT3 | PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP,
};

/* sdi0 (removable MMC/SD/SDIO cards) */
static pin_cfg_t mop500_sdi0_pins[] = {
	GPIO18_MC0_CMDDIR	| PIN_OUTPUT_HIGH,
	GPIO19_MC0_DAT0DIR	| PIN_OUTPUT_HIGH,
	GPIO20_MC0_DAT2DIR	| PIN_OUTPUT_HIGH,

	GPIO22_MC0_FBCLK	| PIN_INPUT_NOPULL,
	GPIO23_MC0_CLK		| PIN_OUTPUT_LOW,
	GPIO24_MC0_CMD		| PIN_INPUT_PULLUP,
	GPIO25_MC0_DAT0		| PIN_INPUT_PULLUP,
	GPIO26_MC0_DAT1		| PIN_INPUT_PULLUP,
	GPIO27_MC0_DAT2		| PIN_INPUT_PULLUP,
	GPIO28_MC0_DAT3		| PIN_INPUT_PULLUP,
};

static int stm_ste_disable_ape_on_mipi60(void)
{
	int retval;

	retval = nmk_config_pins_sleep(ARRAY_AND_SIZE(mop500_stm_mipi60_pins));
	if (retval)
		STM_ERR("Failed to disable MIPI60\n");
	else {
		retval = nmk_config_pins(ARRAY_AND_SIZE(mop500_ske_pins));
		if (retval)
			STM_ERR("Failed to enable SKE gpio\n");
	}
	return retval;
}

static int stm_enable_ape_microsd(void)
{
	int retval;

	/*
	 * Configure STM APE on GPIO23,GPIO28,GPIO27,GPIO26,GPIO25
	 * On HREF board an external SD buffer exist (ST6G3244ME)
	 * to perform level conversion from 1.8v to 3.3V on SD card signals
	 * When STM is redirected on micro SD connector GPIO18,GP19,GPIO20
	 * are configured in standard GPIO mode and are used to configure
	 * direction on external SD buffer ST6G3244ME.
	 */

	retval = nmk_config_pins(ARRAY_AND_SIZE(mop500_stm_ape_microsd_pins));
	if (retval)
		STM_ERR("Failed to enable STM APE on MICRO SD\n");

	/* Enable altC1 on GPIO23-28 (STMAPE) */
	prcmu_enable_stm_ape();

	return retval;
}

static int stm_disable_ape_microsd(void)
{
	int retval;

	/* Disable altC1 on GPIO23-28 (STMAPE) */
	prcmu_disable_stm_ape();

	/* Reconfigure GPIO for SD */
	retval = nmk_config_pins_sleep(ARRAY_AND_SIZE(mop500_sdi0_pins));
	if (retval)
		STM_ERR("Failed to disable STM APE on MICRO SD "
		"and to reconfigure GPIO for SD\n");

	return retval;
}

static int stm_enable_modem_microsd(void)
{
	int retval;

	/*
	 * Configure STM APE on GPIO23,GPIO28,GPIO27,GPIO26,GPIO25
	 * On HREF board an external SD buffer exist (ST6G3244ME)
	 * to perform level conversion from 1.8v to 3.3V on SD card
	 * signals. When STM is redirected on micro SD connector
	 * GPIO18,GP19,GPIO20 are configured in standard GPIO mode
	 * and are used to configure direction on external SD buffer
	 * ST6G3244ME.
	 */

	retval = nmk_config_pins(ARRAY_AND_SIZE(mop500_stm_modem_microsd_pins));
	if (retval)
		STM_ERR("Failed to enable STM MODEM on MICRO SD\n");

	return retval;
}

static int stm_disable_modem_microsd(void)
{
	int retval;

	/* Reconfigure GPIO for SD */
	retval = nmk_config_pins_sleep(ARRAY_AND_SIZE(mop500_sdi0_pins));
	if (retval)
		STM_ERR("Failed to disable STM MODEM on MICRO SD "
		"and to reconfigure GPIO for SD\n");

	return retval;
}

/* Enable or disable micro sd card buffers on HREF */
static void control_level_shifter_for_microsd(int gpio_dir)
{
	int gpio[2];

	if (machine_is_hrefv60() || machine_is_u9540()) {
		gpio[0] = HREFV60_SDMMC_EN_GPIO;
		gpio[1] = HREFV60_SDMMC_1V8_3V_GPIO;
	} else if (machine_is_u8520()) {
		gpio[0] = U8520_SDMMC_EN_GPIO;
		gpio[1] = U8520_SDMMC_1V8_3V_GPIO;
	} else	{
		gpio[0] = MOP500_EGPIO(17);
		gpio[1] = MOP500_EGPIO(18);
	}

	/* Select the default 2.9V and enable / disable level shifter */
	gpio_direction_output(gpio[1], 0);
	gpio_direction_output(gpio[0], gpio_dir);
}

/* Enable micro sd card buffers on HREF */
static int enable_level_shifter_for_microsd(void)
{
	control_level_shifter_for_microsd(1);
	STM_WARN("Level Shifter for SD card connector on.\n");
	return 0;
}

/* Disable micro sd card buffers on HREF */
static int disable_level_shifter_for_microsd(void)
{
	control_level_shifter_for_microsd(0);
	STM_WARN("Level Shifter for SD card connector off.\n");
	return 0;
}

/* Enable VAUX3 to power on buffer on STM MICRO SD cable */
static int enable_vaux3_for_microsd_cable(void)
{
	int error;

	regulator_aux3 = regulator_get(&ux500_stm_device.dev, "v-SD-STM");

	if (IS_ERR(regulator_aux3)) {
		error = PTR_ERR(regulator_aux3);
		STM_ERR("Failed to get regulator, supply: v-SD-STM\n");
		return error;
	}

	error = regulator_enable(regulator_aux3);

	if (error) {
		STM_ERR("Unable to enable regulator on SD card connector\n");
		return error;
	}

	STM_WARN("Regulator on SD card connector power on.\n");
	return error;
}

/* Disable VAUX3 to power off buffer on STM MICRO SD cable */
static int disable_vaux3_for_microsd_cable(void)
{
	int error = 0;

	error = regulator_disable(regulator_aux3);

	if (regulator_aux3)
		regulator_put(regulator_aux3);

	STM_WARN("Regulator for stm on SD card connector power off.\n");

	return error;

}

static int stm_ste_connection(enum stm_connection_type con_type)
{
	int retval = -EINVAL;

	/* Check if connection type has been changed */
	if (con_type == stm_current_connection)
		return 0;

	if (con_type != STM_DISCONNECT) {
		/*  Always enable MIPI34 GPIO pins */
		retval = nmk_config_pins(
				ARRAY_AND_SIZE(mop500_stm_mipi34_pins));
		if (retval) {
			STM_ERR("Failed to enable MIPI34\n");
			goto stm_ste_connection_error;
		}
	}

	switch (con_type) {
	case STM_DEFAULT_CONNECTION:
	case STM_STE_MODEM_ON_MIPI34_NONE_ON_MIPI60:
		/* Enable altC3 on GPIO70-74 (STMMOD) & GPIO75-76 (UARTMOD) */
		prcmu_enable_stm_mod_uart();
		retval = stm_ste_disable_ape_on_mipi60();
		break;

	case STM_STE_APE_ON_MIPI34_NONE_ON_MIPI60:
		/* Disable altC3 on GPIO70-74 (STMMOD) & GPIO75-76 (UARTMOD) */
		prcmu_disable_stm_mod_uart();
		retval = stm_ste_disable_ape_on_mipi60();
		break;

	case STM_STE_MODEM_ON_MIPI34_APE_ON_MIPI60:
		/* Enable altC3 on GPIO70-74 (STMMOD) and GPIO75-76 (UARTMOD) */
		prcmu_enable_stm_mod_uart();
		/* Enable APE on MIPI60 */
		retval = nmk_config_pins_sleep(ARRAY_AND_SIZE(mop500_ske_pins));
		if (retval)
			STM_ERR("Failed to disable SKE GPIO\n");
		else {
			retval = nmk_config_pins(
					ARRAY_AND_SIZE(mop500_stm_mipi60_pins));
			if (retval)
				STM_ERR("Failed to enable MIPI60\n");
		}
		break;

	case STM_STE_MODEM_ON_MICROSD:
		/* Disable APE on micro SD */
		retval = stm_disable_ape_microsd();
		/* Enable modem on micro SD */
		if (!retval)
			retval = stm_enable_modem_microsd();
		/* Enable SD card buffer and regulator on href */
		if (!retval && (stm_current_connection
			!= STM_STE_APE_ON_MICROSD)) {
			enable_level_shifter_for_microsd();
			enable_vaux3_for_microsd_cable();
		}
		break;

	case STM_STE_APE_ON_MICROSD:
		/* Disable modem on micro SD */
		retval = stm_disable_modem_microsd();
		/* Enable ape on micro SD */
		if (!retval)
			retval = stm_enable_ape_microsd();
		/* Enable SD card buffer and regulator on href */
		if (!retval && (stm_current_connection
			!= STM_STE_MODEM_ON_MICROSD)) {
			enable_level_shifter_for_microsd();
			enable_vaux3_for_microsd_cable();
		}
		break;

	case STM_DISCONNECT:
		retval = nmk_config_pins_sleep(
				ARRAY_AND_SIZE(mop500_stm_mipi34_pins));
		if (retval)
			STM_ERR("Failed to disable MIPI34\n");

		retval = stm_ste_disable_ape_on_mipi60();
		if (retval)
			STM_ERR("Failed to disable MIPI60\n");

		retval = stm_disable_modem_microsd();
		if (retval)
			STM_ERR("Failed to disable modem on microsd\n");

		retval = stm_disable_ape_microsd();
		if (retval)
			STM_ERR("Failed to disable ape on microsd\n");
		break;

	default:
		STM_ERR("Bad connection type\n");
		goto stm_ste_connection_error;
	}

	/* Disable power for microsd */
	if ((stm_current_connection == STM_STE_MODEM_ON_MICROSD)
	|| (stm_current_connection == STM_STE_APE_ON_MICROSD)) {
		if ((con_type != STM_STE_MODEM_ON_MICROSD)
		&& (con_type != STM_STE_APE_ON_MICROSD)) {
			disable_vaux3_for_microsd_cable();
			disable_level_shifter_for_microsd();
		}
	}

	stm_current_connection = con_type;

stm_ste_connection_error:
	return retval;
}

/* Possible STM sources (masters) on ux500 */
enum stm_master {
	STM_ARM0 =	0,
	STM_ARM1 =	1,
	STM_SVA =	2,
	STM_SIA =	3,
	STM_SIA_XP70 =	4,
	STM_PRCMU =	5,
	STM_MCSBAG =	9
};

#define STM_ENABLE_ARM0		BIT(STM_ARM0)
#define STM_ENABLE_ARM1		BIT(STM_ARM1)
#define STM_ENABLE_SVA		BIT(STM_SVA)
#define STM_ENABLE_SIA		BIT(STM_SIA)
#define STM_ENABLE_SIA_XP70	BIT(STM_SIA_XP70)
#define STM_ENABLE_PRCMU	BIT(STM_PRCMU)
#define STM_ENABLE_MCSBAG	BIT(STM_MCSBAG)

/*
 * These are the channels used by NMF and some external softwares
 * expect the NMF traces to be output on these channels
 * For legacy reason, we need to reserve them.
 */
static const s16 stm_channels_reserved[] = {
	100,	/* NMF MPCEE channel */
	101,	/* NMF CM channel */
	151,	/* NMF HOSTEE channel */
};

/* On Ux500 we 2 consecutive STMs therefore 512 channels available */
static struct stm_platform_data stm_pdata = {
	.regs_phys_base       = U8500_STM_REG_BASE,
	.channels_phys_base   = U8500_STM_BASE,
	.id_mask              = 0x000fffff,   /* Ignore revisions differences */
	.channels_reserved    = stm_channels_reserved,
	.channels_reserved_sz = ARRAY_SIZE(stm_channels_reserved),
	/* Enable all except MCSBAG */
	.masters_enabled      = STM_ENABLE_ARM0 | STM_ENABLE_ARM1 |
				STM_ENABLE_SVA | STM_ENABLE_PRCMU |
				STM_ENABLE_SIA | STM_ENABLE_SIA_XP70,
	/* Provide function for MIPI34/MIPI60 STM connection */
	.stm_connection       = stm_ste_connection,
};

struct platform_device ux500_stm_device = {
	.name = "stm",
	.id = -1,
	.dev = {
		.platform_data = &stm_pdata,
	},
};
