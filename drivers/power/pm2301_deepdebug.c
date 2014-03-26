/*
 * Copyright 2012 ST Ericsson.
 *
 * Deepdebug support for ST Ericsson pm2xxx_charger charger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/debugfs.h>
#include <linux/ux500_deepdebug.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/pm2301_charger.h>

#include "pm2301_charger.h"

/* PM2XXX registers list */
static struct ddbg_register ddbg_pm2xxx_registers[] = {
	DDBG_REG("PM2XXX_BATT_CTRL_REG1", PM2XXX_BATT_CTRL_REG1, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG2", PM2XXX_BATT_CTRL_REG2, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG3", PM2XXX_BATT_CTRL_REG3, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG4", PM2XXX_BATT_CTRL_REG4, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG5", PM2XXX_BATT_CTRL_REG5, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG6", PM2XXX_BATT_CTRL_REG6, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG7", PM2XXX_BATT_CTRL_REG7, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG8", PM2XXX_BATT_CTRL_REG8, DDBG_RW),
	DDBG_REG("PM2XXX_NTC_CTRL_REG1", PM2XXX_NTC_CTRL_REG1, DDBG_RW),
	DDBG_REG("PM2XXX_NTC_CTRL_REG2", PM2XXX_NTC_CTRL_REG2, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_CTRL_REG9", PM2XXX_BATT_CTRL_REG9, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_STAT_REG1", PM2XXX_BATT_STAT_REG1, DDBG_RO),
	DDBG_REG("PM2XXX_INP_VOLT_VPWR2", PM2XXX_INP_VOLT_VPWR2, DDBG_RW),
	DDBG_REG("PM2XXX_INP_DROP_VPWR2", PM2XXX_INP_DROP_VPWR2, DDBG_RW),
	DDBG_REG("PM2XXX_INP_VOLT_VPWR1", PM2XXX_INP_VOLT_VPWR1, DDBG_RW),
	DDBG_REG("PM2XXX_INP_DROP_VPWR1", PM2XXX_INP_DROP_VPWR1, DDBG_RW),
	DDBG_REG("PM2XXX_INP_MODE_VPWR", PM2XXX_INP_MODE_VPWR, DDBG_RW),
	DDBG_REG("PM2XXX_BATT_WD_KICK", PM2XXX_BATT_WD_KICK, DDBG_RW),
	DDBG_REG("PM2XXX_DEV_VER_STAT", PM2XXX_DEV_VER_STAT, DDBG_RO),
	DDBG_REG("PM2XXX_THERM_WARN_CTRL", PM2XXX_THERM_WARN_CTRL_REG, DDBG_RW),
	DDBG_REG("PM2XXX_BAT_DISC_REG", PM2XXX_BATT_DISC_REG, DDBG_RW),
	DDBG_REG("PM2XXX_BAT_LWLEV_CMP", PM2XXX_BATT_LOW_LEV_COMP_REG, DDBG_RW),
	DDBG_REG("PM2XXX_BAT_LWLEV_VAL", PM2XXX_BATT_LOW_LEV_VAL_REG, DDBG_RW),
	DDBG_REG("PM2XXX_I2C_PAD_CTRL_REG", PM2XXX_I2C_PAD_CTRL_REG, DDBG_RW),
	DDBG_REG("PM2XXX_SW_CTRL_REG", PM2XXX_SW_CTRL_REG, DDBG_RW),
	DDBG_REG("PM2XXX_LED_CTRL_REG", PM2XXX_LED_CTRL_REG, DDBG_RO),
	DDBG_REG("PM2XXX_REG_INT1", PM2XXX_REG_INT1, DDBG_RO),
	DDBG_REG("PM2XXX_MASK_REG_INT1", PM2XXX_MASK_REG_INT1, DDBG_RW),
	DDBG_REG("PM2XXX_SRCE_REG_INT1", PM2XXX_SRCE_REG_INT1, DDBG_RO),
	DDBG_REG("PM2XXX_REG_INT2", PM2XXX_REG_INT2, DDBG_RO),
	DDBG_REG("PM2XXX_MASK_REG_INT2", PM2XXX_MASK_REG_INT2, DDBG_RW),
	DDBG_REG("PM2XXX_SRCE_REG_INT2", PM2XXX_SRCE_REG_INT2, DDBG_RO),
	DDBG_REG("PM2XXX_REG_INT3", PM2XXX_REG_INT3, DDBG_RO),
	DDBG_REG("PM2XXX_MASK_REG_INT3", PM2XXX_MASK_REG_INT3, DDBG_RW),
	DDBG_REG("PM2XXX_SRCE_REG_INT3", PM2XXX_SRCE_REG_INT3, DDBG_RO),
	DDBG_REG("PM2XXX_REG_INT4", PM2XXX_REG_INT4, DDBG_RO),
	DDBG_REG("PM2XXX_MASK_REG_INT4", PM2XXX_MASK_REG_INT4, DDBG_RW),
	DDBG_REG("PM2XXX_SRCE_REG_INT4", PM2XXX_SRCE_REG_INT4, DDBG_RO),
	DDBG_REG("PM2XXX_REG_INT5", PM2XXX_REG_INT5, DDBG_RO),
	DDBG_REG("PM2XXX_MASK_REG_INT5", PM2XXX_MASK_REG_INT5, DDBG_RW),
	DDBG_REG("PM2XXX_SRCE_REG_INT5", PM2XXX_SRCE_REG_INT5, DDBG_RO),
	DDBG_REG("PM2XXX_REG_INT6", PM2XXX_REG_INT6, DDBG_RO),
	DDBG_REG("PM2XXX_MASK_REG_INT6", PM2XXX_MASK_REG_INT6, DDBG_RW),
	DDBG_REG("PM2XXX_SRCE_REG_INT6", PM2XXX_SRCE_REG_INT6, DDBG_RO),
	DDBG_REG_NULL,
};

static struct pm2xxx_charger *pm2xxx_ddbg;

static int pm2xxx_ddbg_write(u32 addr, u32 val)
{
	int err;

	err = pm2xxx_reg_write(pm2xxx_ddbg, addr, val);

	if (err < 0)
		return -EIO;

	return 0;
}

static int pm2xxx_ddbg_read(u32 addr, u32 *val)
{
	int ret = pm2xxx_reg_read(pm2xxx_ddbg, addr, (u8 *)val);

	if (ret < 0)
		return -EIO;

	return 0;
}

static struct ddbg_target ddbg_pm2xxx = {
	.name = "pm2xxx",
	.phyaddr = 0,
	.reg = ddbg_pm2xxx_registers,
	.read_reg = pm2xxx_ddbg_read,
	.write_reg = pm2xxx_ddbg_write,
};

int __init pm2xxx_ddbg_init(struct pm2xxx_charger *pm2)
{
	int ret;
	pm2xxx_ddbg = pm2;
	ret = deep_debug_regaccess_register(&ddbg_pm2xxx);
	if(ret)
		pr_err("failed to create debugfs entries.\n");

	return ret;
}

int __exit pm2xxx_ddbg_exit(void)
{
	return (deep_debug_regaccess_unregister(&ddbg_pm2xxx));
}

