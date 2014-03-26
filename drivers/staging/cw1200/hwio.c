/*
 * Low-level device IO routines for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver, which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>

#include "cw1200.h"
#include "hwio.h"
#include "sbus.h"

 /* Sdio addr is 4*spi_addr */
#define SPI_REG_ADDR_TO_SDIO(spi_reg_addr) ((spi_reg_addr) << 2)
#define SDIO_ADDR17BIT(buf_id, mpf, rfu, reg_id_ofs) \
				((((buf_id)    & 0x1F) << 7) \
				| (((mpf)        & 1) << 6) \
				| (((rfu)        & 1) << 5) \
				| (((reg_id_ofs) & 0x1F) << 0))


static int __cw1200_reg_read(struct cw1200_common *priv, u16 addr,
				void *buf, size_t buf_len, int buf_id)
{
	u16 addr_sdio;
	u32 sdio_reg_addr_17bit ;

	/* Check if buffer is aligned to 4 byte boundary */
	if (WARN_ON(((unsigned long)buf & 3) && (buf_len > 4))) {
		cw1200_dbg(CW1200_DBG_ERROR,
			   "%s: buffer is not aligned.\n", __func__);
		return -EINVAL;
	}

	/* Convert to SDIO Register Address */
	addr_sdio = SPI_REG_ADDR_TO_SDIO(addr);
	sdio_reg_addr_17bit = SDIO_ADDR17BIT(buf_id, 0, 0, addr_sdio);

	BUG_ON(!priv->sbus_ops);
	return priv->sbus_ops->sbus_memcpy_fromio(priv->sbus_priv,
						  sdio_reg_addr_17bit,
						  buf, buf_len);
}

static int __cw1200_reg_write(struct cw1200_common *priv, u16 addr,
				const void *buf, size_t buf_len, int buf_id)
{
	u16 addr_sdio;
	u32 sdio_reg_addr_17bit ;

#if 0
	/* Check if buffer is aligned to 4 byte boundary */
	if (WARN_ON(((unsigned long)buf & 3) && (buf_len > 4))) {
		cw1200_dbg(CW1200_DBG_ERROR, "%s: buffer is not aligned.\n",
				__func__);
		return -EINVAL;
	}
#endif

	/* Convert to SDIO Register Address */
	addr_sdio = SPI_REG_ADDR_TO_SDIO(addr);
	sdio_reg_addr_17bit = SDIO_ADDR17BIT(buf_id, 0, 0, addr_sdio);

	BUG_ON(!priv->sbus_ops);
	return priv->sbus_ops->sbus_memcpy_toio(priv->sbus_priv,
						sdio_reg_addr_17bit,
						buf, buf_len);
}

static inline int __cw1200_reg_read_32(struct cw1200_common *priv,
					u16 addr, u32 *val)
{
	return __cw1200_reg_read(priv, addr, val, sizeof(val), 0);
}

static inline int __cw1200_reg_write_32(struct cw1200_common *priv,
					u16 addr, u32 val)
{
	return __cw1200_reg_write(priv, addr, &val, sizeof(val), 0);
}

int cw1200_reg_read(struct cw1200_common *priv, u16 addr, void *buf,
			size_t buf_len)
{
	int ret;
	BUG_ON(!priv->sbus_ops);
	priv->sbus_ops->lock(priv->sbus_priv);
	ret = __cw1200_reg_read(priv, addr, buf, buf_len, 0);
	priv->sbus_ops->unlock(priv->sbus_priv);
	return ret;
}

int cw1200_reg_write(struct cw1200_common *priv, u16 addr, const void *buf,
			size_t buf_len)
{
	int ret;
	BUG_ON(!priv->sbus_ops);
	priv->sbus_ops->lock(priv->sbus_priv);
	ret = __cw1200_reg_write(priv, addr, buf, buf_len, 0);
	priv->sbus_ops->unlock(priv->sbus_priv);
	return ret;
}

int cw1200_data_read(struct cw1200_common *priv, void *buf, size_t buf_len)
{
	int ret;
	BUG_ON(!priv->sbus_ops);
	priv->sbus_ops->lock(priv->sbus_priv);
	{
		int buf_id_rx = priv->buf_id_rx;
		ret = __cw1200_reg_read(priv, ST90TDS_IN_OUT_QUEUE_REG_ID, buf,
						buf_len, buf_id_rx + 1);
		if (!ret) {
			buf_id_rx = (buf_id_rx + 1) & 3;
			priv->buf_id_rx = buf_id_rx;
		}
	}
	priv->sbus_ops->unlock(priv->sbus_priv);
	return ret;
}

int cw1200_data_write(struct cw1200_common *priv, const void *buf,
			size_t buf_len)
{
	int ret;
	BUG_ON(!priv->sbus_ops);
	priv->sbus_ops->lock(priv->sbus_priv);
	{
		int buf_id_tx = priv->buf_id_tx;
		ret = __cw1200_reg_write(priv, ST90TDS_IN_OUT_QUEUE_REG_ID, buf,
						buf_len, buf_id_tx);
		if (!ret) {
			buf_id_tx = (buf_id_tx + 1) & 31;
			priv->buf_id_tx = buf_id_tx;
		}
	}
	priv->sbus_ops->unlock(priv->sbus_priv);
	return ret;
}

int cw1200_indirect_read(struct cw1200_common *priv, u32 addr, void *buf,
			 size_t buf_len, u32 prefetch, u16 port_addr)
{
	u32 val32 = 0;
	int i, ret;

	if ((buf_len / 2) >= 0x1000) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't read more than 0xfff words.\n",
				__func__);
		WARN_ON(1);
		return -EINVAL;
		goto out;
	}

	priv->sbus_ops->lock(priv->sbus_priv);
	/* Write address */
	ret = __cw1200_reg_write_32(priv, ST90TDS_SRAM_BASE_ADDR_REG_ID, addr);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't write address register.\n",
				__func__);
		goto out;
	}

	/* Read CONFIG Register Value - We will read 32 bits */
	ret = __cw1200_reg_read_32(priv, ST90TDS_CONFIG_REG_ID, &val32);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't read config register.\n",
				__func__);
		goto out;
	}

	/* Set PREFETCH bit */
	ret = __cw1200_reg_write_32(priv, ST90TDS_CONFIG_REG_ID,
					val32 | prefetch);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't write prefetch bit.\n",
				__func__);
		goto out;
	}

	/* Check for PRE-FETCH bit to be cleared */
	for (i = 0; i < 20; i++) {
		ret = __cw1200_reg_read_32(priv, ST90TDS_CONFIG_REG_ID, &val32);
		if (ret < 0) {
			cw1200_dbg(CW1200_DBG_ERROR,
					"%s: Can't check prefetch bit.\n",
					__func__);
			goto out;
		}
		if (!(val32 & prefetch))
			break;

		mdelay(i);
	}

	if (val32 & prefetch) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Prefetch bit is not cleared.\n",
				__func__);
		goto out;
	}

	/* Read data port */
	ret = __cw1200_reg_read(priv, port_addr, buf, buf_len, 0);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't read data port.\n",
				__func__);
		goto out;
	}

out:
	priv->sbus_ops->unlock(priv->sbus_priv);
	return ret;
}

int cw1200_apb_write(struct cw1200_common *priv, u32 addr, const void *buf,
			size_t buf_len)
{
	int ret;

	if ((buf_len / 2) >= 0x1000) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't wrire more than 0xfff words.\n",
				__func__);
		WARN_ON(1);
		return -EINVAL;
	}

	priv->sbus_ops->lock(priv->sbus_priv);

	/* Write address */
	ret = __cw1200_reg_write_32(priv, ST90TDS_SRAM_BASE_ADDR_REG_ID, addr);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR,
				"%s: Can't write address register.\n",
				__func__);
		goto out;
	}

	/* Write data port */
	ret = __cw1200_reg_write(priv, ST90TDS_SRAM_DPORT_REG_ID,
					buf, buf_len, 0);
	if (ret < 0) {
		cw1200_dbg(CW1200_DBG_ERROR, "%s: Can't write data port.\n",
				__func__);
		goto out;
	}

out:
	priv->sbus_ops->unlock(priv->sbus_priv);
	return ret;
}

