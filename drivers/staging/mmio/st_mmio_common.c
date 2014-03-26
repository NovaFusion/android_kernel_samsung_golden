/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson mmio utils
 *
 * Author: Vincent Abriou <vincent.abriou@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include "st_mmio_common.h"

int copy_user_buffer(
		void __iomem **dest_buf,
		void __iomem *src_buf,
		u32 size)
{
	int err = 0;

	if (!src_buf)
		return -EINVAL;

	if (!dest_buf) {
		err = -ENOMEM;
		goto nomem;
	}

	*dest_buf = kmalloc(size, GFP_KERNEL);

	if (copy_from_user(*dest_buf, src_buf, size)) {
		err = -EFAULT;
		goto cp_failed;
	}

	return err;
cp_failed:
	kfree(*dest_buf);
nomem:
	return err;
}

int mmio_cam_init_mmdsp_timer(
		struct mmio_info *info)
{
	/* Disabling Accelerators timers */
	clrbits32(info->crbase, CR_REG0_HTIMEN);
	/* Write MMDSPTimer */
	writel(0, info->siabase + SIA_TIMER_ITC);
	/* Enabling Accelerators timers */
	setbits32(info->crbase, CR_REG0_HTIMEN);
	return 0;
}

int mmio_cam_initboard(
		struct mmio_info *info)
{
	int err = 0;

	dev_dbg(info->dev, "%s\n", __func__);

	err = prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
			(char *)info->misc_dev.name,
			MAX_PRCMU_QOS_APP);

	if (err) {
		dev_err(info->dev, "Error adding PRCMU QoS requirement %d\n",
				err);
		goto out;
	}

	/* Initialize platform specific data */
	err = info->pdata->platform_init(info->pdata);

	if (err) {
		dev_err(info->dev,
				"Failed to execute platform init: %d\n",
				err);
		goto out;
	}

	/* Enable I2C */
	err = info->pdata->config_i2c_pins(info->pdata, MMIO_ACTIVATE_I2C);

	if (err) {
		dev_err(info->dev,
				"Failed to enable I2C: %d\n",
				err);
		goto out;
	}

out:
	return err;
}

int mmio_cam_desinitboard(
		struct mmio_info *info)
{
	int err = 0;

	dev_dbg(info->dev, "%s\n", __func__);

	err = info->pdata->config_i2c_pins(info->pdata, MMIO_DEACTIVATE_I2C);

	if (err) {
		dev_err(info->dev,
				"Failed to disable I2C: %d\n",
				err);
		goto out;
	}

	info->pdata->platform_exit(info->pdata);

	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
			(char *)info->misc_dev.name);

out:
	return err;
}

int mmio_cam_pwr_sensor(
		struct mmio_info *info,
		enum mmio_bool_t on)
{
	int err = 0;

	if (on) {
		err = info->pdata->power_enable(info->pdata);

		if (err)
			dev_err(info->dev,
					"power_enable failed. err = %d\n", err);
	} else {
		info->pdata->power_disable(info->pdata);
	}

	return err;
}

int mmio_cam_control_clocks(
		struct mmio_info *info,
		enum mmio_bool_t on)
{
	int err = 0;

	if (on) {
		err = info->pdata->clock_enable(info->pdata);

		if (err)
			dev_err(info->dev,
					"clock_enable failed, err = %d\n",
					err);
	} else {
		info->pdata->clock_disable(info->pdata);
	}

	return err;
}
