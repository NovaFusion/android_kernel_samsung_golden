/*
 * Firmware I/O code for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>

#include "cw1200.h"
#include "fwio.h"
#include "hwio.h"
#include "sbus.h"
#include "bh.h"

static int cw1200_get_hw_type(u32 config_reg_val, int *major_revision)
{
	int hw_type = -1;
	u32 silicon_type = (config_reg_val >> 24) & 0x3;
	u32 silicon_vers = (config_reg_val >> 31) & 0x1;

	/* Check if we have CW1200 or STLC9000 */
	if ((silicon_type == 0x1) || (silicon_type == 0x2)) {
		*major_revision = silicon_type;
		if (silicon_vers)
			hw_type = HIF_8601_VERSATILE;
		else
			hw_type = HIF_8601_SILICON;
	} else {
		*major_revision = 1;
		hw_type = HIF_9000_SILICON_VERSTAILE;
	}

	return hw_type;
}

static int config_reg_read_stlc9000(struct cw1200_common *priv,
				    u16 reg, u32 *val)
{
	u16 val16;
	int ret = cw1200_reg_read_16(priv, reg, &val16);
	if (ret < 0)
		return ret;
	*val = val16;
	return 0;
}

static int config_reg_write_stlc9000(struct cw1200_common *priv,
				     u16 reg, u32 val)
{
	return cw1200_reg_write_16(priv, reg, (u16)val);
}

static int cw1200_load_firmware_cw1200(struct cw1200_common *priv)
{
	int ret, block, num_blocks;
	unsigned i;
	u32 val32;
	u32 put = 0, get = 0;
	u8 *buf = NULL;
	const char *fw_path;
	const struct firmware *firmware = NULL;

	/* Macroses are local. */
#define APB_WRITE(reg, val) \
	do { \
		ret = cw1200_apb_write_32(priv, CW12000_APB(reg), (val)); \
		if (ret < 0) { \
			cw1200_dbg(CW1200_DBG_ERROR, \
				"%s: can't write %s at line %d.\n", \
				__func__, #reg, __LINE__); \
			goto error; \
		} \
	} while (0)
#define APB_READ(reg, val) \
	do { \
		ret = cw1200_apb_read_32(priv, CW12000_APB(reg), &(val)); \
		if (ret < 0) { \
			cw1200_dbg(CW1200_DBG_ERROR, \
				"%s: can't read %s at line %d.\n", \
				__func__, #reg, __LINE__); \
			goto error; \
		} \
	} while (0)
#define REG_WRITE(reg, val) \
	do { \
		ret = cw1200_reg_write_32(priv, (reg), (val)); \
		if (ret < 0) { \
			cw1200_dbg(CW1200_DBG_ERROR, \
				"%s: can't write %s at line %d.\n", \
				__func__, #reg, __LINE__); \
			goto error; \
		} \
	} while (0)
#define REG_READ(reg, val) \
	do { \
		ret = cw1200_reg_read_32(priv, (reg), &(val)); \
		if (ret < 0) { \
			cw1200_dbg(CW1200_DBG_ERROR, \
				"%s: can't read %s at line %d.\n", \
				__func__, #reg, __LINE__); \
			goto error; \
		} \
	} while (0)

	switch (priv->hw_revision) {
	case CW1200_HW_REV_CUT10:
		fw_path = FIRMWARE_CUT10;
		break;
	case CW1200_HW_REV_CUT11:
		fw_path = FIRMWARE_CUT11;
		break;
	case CW1200_HW_REV_CUT20:
		fw_path = FIRMWARE_CUT20;
		break;
	case CW1200_HW_REV_CUT22:
		fw_path = FIRMWARE_CUT22;
		break;
	default:
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: invalid silicon revision %d.\n",
			__func__, priv->hw_revision);
		return -EINVAL;
	}

	/* Initialize common registers */
	APB_WRITE(DOWNLOAD_IMAGE_SIZE_REG, DOWNLOAD_ARE_YOU_HERE);
	APB_WRITE(DOWNLOAD_PUT_REG, 0);
	APB_WRITE(DOWNLOAD_GET_REG, 0);
	APB_WRITE(DOWNLOAD_STATUS_REG, DOWNLOAD_PENDING);
	APB_WRITE(DOWNLOAD_FLAGS_REG, 0);

	/* Write the NOP Instruction */
	REG_WRITE(ST90TDS_SRAM_BASE_ADDR_REG_ID, 0xFFF20000);
	REG_WRITE(ST90TDS_AHB_DPORT_REG_ID, 0xEAFFFFFE);

	/* Release CPU from RESET */
	REG_READ(ST90TDS_CONFIG_REG_ID, val32);
	val32 &= ~ST90TDS_CONFIG_CPU_RESET_BIT;
	REG_WRITE(ST90TDS_CONFIG_REG_ID, val32);

	/* Enable Clock */
	val32 &= ~ST90TDS_CONFIG_CPU_CLK_DIS_BIT;
	REG_WRITE(ST90TDS_CONFIG_REG_ID, val32);

	/* Load a firmware file */
	ret = request_firmware(&firmware, fw_path, priv->pdev);
	if (ret) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't load firmware file %s.\n",
			__func__, fw_path);
		goto error;
	}
	BUG_ON(!firmware->data);

	buf = kmalloc(DOWNLOAD_BLOCK_SIZE, GFP_KERNEL | GFP_DMA);
	if (!buf) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't allocate firmware buffer.\n", __func__);
		ret = -ENOMEM;
		goto error;
	}

	/* Check if the bootloader is ready */
	for (i = 0; i < 100; i += 1 + i / 2) {
		APB_READ(DOWNLOAD_IMAGE_SIZE_REG, val32);
		if (val32 == DOWNLOAD_I_AM_HERE)
			break;
		mdelay(i);
	} /* End of for loop */

	if (val32 != DOWNLOAD_I_AM_HERE) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: bootloader is not ready.\n", __func__);
		ret = -ETIMEDOUT;
		goto error;
	}

	/* Calculcate number of download blocks */
	num_blocks = (firmware->size - 1) / DOWNLOAD_BLOCK_SIZE + 1;

	/* Updating the length in Download Ctrl Area */
	val32 = firmware->size; /* Explicit cast from size_t to u32 */
	APB_WRITE(DOWNLOAD_IMAGE_SIZE_REG, val32);

	/* Firmware downloading loop */
	for (block = 0; block < num_blocks ; block++) {
		size_t tx_size;
		size_t block_size;

		/* check the download status */
		APB_READ(DOWNLOAD_STATUS_REG, val32);
		if (val32 != DOWNLOAD_PENDING) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: bootloader reported error %d.\n",
				__func__, val32);
			ret = -EIO;
			goto error;
		}

		/* loop until put - get <= 24K */
		for (i = 0; i < 100; i++) {
			APB_READ(DOWNLOAD_GET_REG, get);
			if ((put - get) <=
			    (DOWNLOAD_FIFO_SIZE - DOWNLOAD_BLOCK_SIZE))
				break;
			mdelay(i);
		}

		if ((put - get) > (DOWNLOAD_FIFO_SIZE - DOWNLOAD_BLOCK_SIZE)) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Timeout waiting for FIFO.\n",
				__func__);
			return -ETIMEDOUT;
		}

		/* calculate the block size */
		tx_size = block_size = min((size_t)(firmware->size - put),
			(size_t)DOWNLOAD_BLOCK_SIZE);

		memcpy(buf, &firmware->data[put], block_size);
		if (block_size < DOWNLOAD_BLOCK_SIZE) {
			memset(&buf[block_size],
				0, DOWNLOAD_BLOCK_SIZE - block_size);
			tx_size = DOWNLOAD_BLOCK_SIZE;
		}

		/* send the block to sram */
		ret = cw1200_apb_write(priv,
			CW12000_APB(DOWNLOAD_FIFO_OFFSET +
				(put & (DOWNLOAD_FIFO_SIZE - 1))),
			buf, tx_size);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: can't write block at line %d.\n",
				__func__, __LINE__);
			goto error;
		}

		/* update the put register */
		put += block_size;
		APB_WRITE(DOWNLOAD_PUT_REG, put);
	} /* End of firmware download loop */

	/* Wait for the download completion */
	for (i = 0; i < 300; i += 1 + i / 2) {
		APB_READ(DOWNLOAD_STATUS_REG, val32);
		if (val32 != DOWNLOAD_PENDING)
			break;
		mdelay(i);
	}
	if (val32 != DOWNLOAD_SUCCESS) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: wait for download completion failed. " \
			"Read: 0x%.8X\n", __func__, val32);
		ret = -ETIMEDOUT;
		goto error;
	} else {
		cw1200_dbg(CW1200_DBG_MSG,
			"Firmware download completed.\n");
		ret = 0;
	}

error:
	kfree(buf);
	if (firmware)
		release_firmware(firmware);
	return ret;

#undef APB_WRITE
#undef APB_READ
#undef REG_WRITE
#undef REG_READ
}

int cw1200_load_firmware(struct cw1200_common *priv)
{
	int ret;
	int i;
	u32 val32;
	u16 val16;
	u32 dpll = 0;
	int major_revision;
	int (*config_reg_read)(struct cw1200_common *priv, u16 reg, u32 *val);
	int (*config_reg_write)(struct cw1200_common *priv, u16 reg, u32 val);

	BUG_ON(!priv);

	/* Read CONFIG Register Value - We will read 32 bits */
	ret = cw1200_reg_read_32(priv, ST90TDS_CONFIG_REG_ID, &val32);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't read config register.\n", __func__);
		goto out;
	}

	priv->hw_type = cw1200_get_hw_type(val32, &major_revision);
	if (priv->hw_type < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't deduct hardware type.\n", __func__);
		ret = -ENOTSUPP;
		goto out;
	}

	switch (priv->hw_type) {
	case HIF_8601_VERSATILE:
	case HIF_8601_SILICON:
		dpll = DPLL_INIT_VAL_CW1200;
		config_reg_read = cw1200_reg_read_32;
		config_reg_write = cw1200_reg_write_32;
		break;
	case HIF_9000_SILICON_VERSTAILE:
		dpll = DPLL_INIT_VAL_9000;
		config_reg_read = config_reg_read_stlc9000;
		config_reg_write = config_reg_write_stlc9000;
		break;
	default:
		BUG_ON(1);
	}

	ret = cw1200_reg_write_32(priv, ST90TDS_TSET_GEN_R_W_REG_ID, dpll);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't write DPLL register.\n", __func__);
		goto out;
	}

	msleep(20);

	/* Read DPLL Reg value and compare with value written */
	ret = cw1200_reg_read_32(priv,
		ST90TDS_TSET_GEN_R_W_REG_ID, &val32);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't read DPLL register.\n", __func__);
		goto out;
	}

	if (val32 != dpll) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: unable to initialise " \
			"DPLL register. Wrote 0x%.8X, read 0x%.8X.\n",
			__func__, dpll, val32);
		ret = -EIO;
		goto out;
	}

	/* Set wakeup bit in device */
	ret = cw1200_reg_read_16(priv, ST90TDS_CONTROL_REG_ID, &val16);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: set_wakeup: can't read " \
			"control register.\n", __func__);
		goto out;
	}

	ret = cw1200_reg_write_16(priv, ST90TDS_CONTROL_REG_ID,
		val16 | ST90TDS_CONT_WUP_BIT);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: set_wakeup: can't write " \
			"control register.\n", __func__);
		goto out;
	}

	/* Wait for wakeup */
	for (i = 0 ; i < 300 ; i += 1 + i / 2) {
		ret = cw1200_reg_read_16(priv,
			ST90TDS_CONTROL_REG_ID, &val16);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: wait_for_wakeup: can't read " \
				"control register.\n", __func__);
			goto out;
		}

		if (val16 & ST90TDS_CONT_RDY_BIT) {
			cw1200_dbg(CW1200_DBG_MSG,
				"WLAN device is ready.\n");
			break;
		}
		msleep(i);
	}

	if ((val16 & ST90TDS_CONT_RDY_BIT) == 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: wait_for_wakeup: device is not responding.\n",
			__func__);
		ret = -ETIMEDOUT;
		goto out;
	}

	if (major_revision == 1) {
		/* CW1200 Hardware detection logic : Check for CUT1.1 */
		ret = cw1200_ahb_read_32(priv, CW1200_CUT_ID_ADDR, &val32);
		if (ret) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: HW detection: can't read CUT ID.\n",
				__func__);
			goto out;
		}

		switch (val32) {
		case CW1200_CUT_11_ID_STR:
			cw1200_dbg(CW1200_DBG_MSG,
				   "Cut 1.1 silicon is detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT11;
			break;
		default:
			cw1200_dbg(CW1200_DBG_MSG,
				   "Cut 1.0 silicon is detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT10;
			break;
		}
	} else if (major_revision == 2) {
		u32 ar1, ar2, ar3;
		cw1200_dbg(CW1200_DBG_MSG, "Cut 2.x silicon is detected.\n");

		ret = cw1200_ahb_read_32(priv, CW1200_CUT2_ID_ADDR, &ar1);
		if (ret) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: (1) HW detection: can't read CUT ID.\n",
				__func__);
			goto out;
		}
		ret = cw1200_ahb_read_32(priv, CW1200_CUT2_ID_ADDR + 4, &ar2);
		if (ret) {
			cw1200_dbg(CW1200_DBG_ERROR,
			"%s: (2) HW detection: can't read CUT ID.\n",
				__func__);
			goto out;
		}

		ret = cw1200_ahb_read_32(priv, CW1200_CUT2_ID_ADDR + 8, &ar3);
		if (ret) {
			cw1200_dbg(CW1200_DBG_ERROR,
			"%s: (3) HW detection: can't read CUT ID.\n",
				__func__);
			goto out;
		}

		if (ar1 == CW1200_CUT_22_ID_STR1 &&
		    ar2 == CW1200_CUT_22_ID_STR2 &&
		    ar3 == CW1200_CUT_22_ID_STR3) {
			cw1200_dbg(CW1200_DBG_MSG, "Cut 2.2 detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT22;
		} else {
			cw1200_dbg(CW1200_DBG_MSG, "Cut 2.0 detected.\n");
			priv->hw_revision = CW1200_HW_REV_CUT20;
		}
	} else {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: unsupported silicon major revision %d.\n",
			__func__, major_revision);
		ret = -ENOTSUPP;
		goto out;
	}

	/* Checking for access mode */
	ret = config_reg_read(priv, ST90TDS_CONFIG_REG_ID, &val32);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: check_access_mode: can't read " \
			"config register.\n", __func__);
		goto out;
	}

	if (val32 & ST90TDS_CONFIG_ACCESS_MODE_BIT) {
		switch (priv->hw_type)  {
		case HIF_8601_SILICON:
			cw1200_dbg(CW1200_DBG_MSG,
				"%s: CW1200 detected.\n", __func__);
			ret = cw1200_load_firmware_cw1200(priv);
			break;
		case HIF_8601_VERSATILE:
			/* TODO: Not implemented yet!
			ret = cw1200_load_firmware_cw1100(priv);
			*/
			ret = -ENOTSUPP;
			goto out;
		case HIF_9000_SILICON_VERSTAILE:
			/* TODO: Not implemented yet!
			ret = cw1200_load_firmware_stlc9000(priv);
			*/
			ret = -ENOTSUPP;
			goto out;
		default:
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Unknown hardware: %d.\n",
				__func__, priv->hw_type);
		}
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: can't download firmware.\n", __func__);
			goto out;
		}
	} else {
		cw1200_dbg(CW1200_DBG_MSG,
			"%s: check_access_mode: device is already " \
			"in QUEUE mode.\n", __func__);
		/* TODO: verify this branch. Do we need something to do? */
	}

	/* Register Interrupt Handler */
	ret = priv->sbus_ops->irq_subscribe(priv->sbus_priv,
		(sbus_irq_handler)cw1200_irq_handler, priv);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: can't register IRQ handler.\n", __func__);
		goto out;
	}

	if (HIF_8601_SILICON  == priv->hw_type) {
		/* If device is CW1200 the IRQ enable/disable bits
		 * are in CONFIG register */
		ret = config_reg_read(priv, ST90TDS_CONFIG_REG_ID, &val32);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: enable_irq: can't read " \
				"config register.\n", __func__);
			goto unsubscribe;
		}
		ret = config_reg_write(priv, ST90TDS_CONFIG_REG_ID,
			val32 | ST90TDS_CONF_IRQ_RDY_ENABLE);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: enable_irq: can't write " \
				"config register.\n", __func__);
			goto unsubscribe;
		}
	} else {
		/* If device is STLC9000 the IRQ enable/disable bits
		 * are in CONTROL register */
		/* Enable device interrupts - Both DATA_RDY and WLAN_RDY */
		ret = cw1200_reg_read_16(priv, ST90TDS_CONFIG_REG_ID, &val16);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: enable_irq: can't read " \
				"control register.\n", __func__);
			goto unsubscribe;
		}
		ret = cw1200_reg_write_16(priv, ST90TDS_CONFIG_REG_ID,
			val16 | ST90TDS_CONT_IRQ_RDY_ENABLE);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: enable_irq: can't write " \
				"control register.\n", __func__);
			goto unsubscribe;
		}

	}

	/* Configure device for MESSSAGE MODE */
	ret = config_reg_read(priv, ST90TDS_CONFIG_REG_ID, &val32);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: set_mode: can't read config register.\n",
			__func__);
		goto unsubscribe;
	}
	ret = config_reg_write(priv, ST90TDS_CONFIG_REG_ID,
		val32 & ~ST90TDS_CONFIG_ACCESS_MODE_BIT);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
			"%s: set_mode: can't write config register.\n",
			__func__);
		goto unsubscribe;
	}

	/* Unless we read the CONFIG Register we are
	 * not able to get an interrupt */
	mdelay(10);
	config_reg_read(priv, ST90TDS_CONFIG_REG_ID, &val32);

out:
	return ret;

unsubscribe:
	priv->sbus_ops->irq_unsubscribe(priv->sbus_priv);
	return ret;
}

