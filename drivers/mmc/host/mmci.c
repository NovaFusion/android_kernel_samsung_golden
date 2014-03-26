/*
 *  linux/drivers/mmc/host/mmci.c - ARM PrimeCell MMCI PL180/1 driver
 *
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *  Copyright (C) 2010 ST-Ericsson SA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/mmc/pm.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/amba/bus.h>
#include <linux/clk.h>
#include <linux/scatterlist.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/amba/mmci.h>
#include <linux/pm_runtime.h>

#include <asm/div64.h>
#include <asm/io.h>
#include <asm/sizes.h>

#include "mmci.h"

#define DRIVER_NAME "mmci-pl18x"

static unsigned int fmax = 515633;

#ifdef CONFIG_MACH_SEC_GOLDEN
#define SD_HW_NODETECTION
#endif

/**
 * struct variant_data - MMCI variant-specific quirks
 * @clkreg: default value for MCICLOCK register
 * @clkreg_enable: enable value for MMCICLOCK register
 * @dma_sdio_req_ctrl: enable value for DMAREQCTL register for SDIO write
 * @datalength_bits: number of bits in the MMCIDATALENGTH register
 * @fifosize: number of bytes that can be written when MMCI_TXFIFOEMPTY
 *	      is asserted (likewise for RX)
 * @fifohalfsize: number of bytes that can be written when MCI_TXFIFOHALFEMPTY
 *		  is asserted (likewise for RX)
 * @sdio: variant supports SDIO
 * @st_clkdiv: true if using a ST-specific clock divider algorithm
 * @blksz_datactrl16: true if Block size is at b16..b30 position in datactrl register
 * @non_power_of_2_blksize: true if block sizes can be other than power of two
 * @pwrreg_powerup: power up value for MMCIPOWER register
 * @signal_direction: input/out direction of bus signals can be indicated
 * @pwrreg_ctrl_power: bits in MMCIPOWER register controls ext. power supply
 */
struct variant_data {
	unsigned int		clkreg;
	unsigned int		clkreg_enable;
	unsigned int		dma_sdio_req_ctrl;
	unsigned int		datalength_bits;
	unsigned int		fifosize;
	unsigned int		fifohalfsize;
	bool			sdio;
	bool			st_clkdiv;
	bool			blksz_datactrl16;
	bool			non_power_of_2_blksize;
	unsigned int		pwrreg_powerup;
	bool			signal_direction;
	bool			pwrreg_ctrl_power;
};

static struct variant_data variant_arm = {
	.fifosize		= 16 * 4,
	.fifohalfsize		= 8 * 4,
	.datalength_bits	= 16,
	.pwrreg_powerup		= MCI_PWR_UP,
	.pwrreg_ctrl_power	= true,
};

static struct variant_data variant_arm_extended_fifo = {
	.fifosize		= 128 * 4,
	.fifohalfsize		= 64 * 4,
	.datalength_bits	= 16,
	.pwrreg_powerup		= MCI_PWR_UP,
	.pwrreg_ctrl_power	= true,
};

static struct variant_data variant_u300 = {
	.fifosize		= 16 * 4,
	.fifohalfsize		= 8 * 4,
	.clkreg_enable		= MCI_ST_U300_HWFCEN,
	.datalength_bits	= 16,
	.sdio			= true,
	.pwrreg_powerup		= MCI_PWR_ON,
	.signal_direction	= true,
};

static struct variant_data variant_ux500 = {
	.fifosize		= 30 * 4,
	.fifohalfsize		= 8 * 4,
	.clkreg			= MCI_CLK_ENABLE,
	.clkreg_enable		= MCI_ST_UX500_HWFCEN,
	.dma_sdio_req_ctrl	= MCI_ST_DPSM_DMAREQCTL,
	.datalength_bits	= 24,
	.sdio			= true,
	.st_clkdiv		= true,
	.pwrreg_powerup		= MCI_PWR_ON,
	.signal_direction	= true,
};

static struct variant_data variant_ux500v2 = {
	.fifosize		= 30 * 4,
	.fifohalfsize		= 8 * 4,
	.clkreg			= MCI_CLK_ENABLE,
	.clkreg_enable		= MCI_ST_UX500_HWFCEN,
	.dma_sdio_req_ctrl	= MCI_ST_DPSM_DMAREQCTL,
	.datalength_bits	= 24,
	.sdio			= true,
	.st_clkdiv		= true,
	.blksz_datactrl16	= true,
	.non_power_of_2_blksize	= true,
	.pwrreg_powerup		= MCI_PWR_ON,
	.signal_direction	= true,
};

static void dump_mmci_regs(struct mmc_host *mmc)
{
	u32 pwr, clk, arg, cmd, rspcmd, r0, r1, r2, r3;
	u32 dtimer, dlength, dctrl, dcnt;
	u32 sta, clear, mask0, mask1, fifocnt;

	struct mmci_host *host = mmc_priv(mmc);

	dev_info(mmc_dev(mmc), "[MMC]base value = %p\n", host->base);
	if (host->base == NULL || host->phybase == 0)
		return ;

	pwr = readl(host->base + MMCIPOWER);
	clk = readl(host->base + MMCICLOCK);
	arg = readl(host->base + MMCIARGUMENT);
	cmd = readl(host->base + MMCICOMMAND);
	rspcmd = readl(host->base + MMCIRESPCMD);
	r0 = readl(host->base + MMCIRESPONSE0);
	r1 = readl(host->base + MMCIRESPONSE1);
	r2 = readl(host->base + MMCIRESPONSE2);
	r3 = readl(host->base + MMCIRESPONSE3);
	dtimer = readl(host->base + MMCIDATATIMER);
	dlength = readl(host->base + MMCIDATALENGTH);
	dctrl = readl(host->base + MMCIDATACTRL);
	dcnt = readl(host->base + MMCIDATACNT);
	sta = readl(host->base + MMCISTATUS);
	clear = readl(host->base + MMCICLEAR);
	mask0 = readl(host->base + MMCIMASK0);
	mask1 = readl(host->base + MMCIMASK1);
	fifocnt = readl(host->base + MMCIFIFOCNT);

	dev_info(mmc_dev(mmc), "Registers of MMCI at 0x%08x\n", host->phybase);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_power", pwr);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_clock", clk);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_arg", arg);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_cmd", cmd);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_respcmd", rspcmd);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_resp0", r0);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_resp1", r1);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_resp2", r2);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_resp3", r3);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_datatimer", dtimer);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_datalen", dlength);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_datactrl", dctrl);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_datacnt", dcnt);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_status", sta);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_iclear", clear);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_imask0", mask0);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_imask1", mask1);
	dev_info(mmc_dev(mmc), "%-20s:0x%08x\n", "mmci_fifocnt", fifocnt);
}

/*
 * Validate mmc prerequisites
 */
static int mmci_validate_data(struct mmci_host *host,
			      struct mmc_data *data)
{
	unsigned int status;

	if( host->mmc && host->mmc->index == 2 ) {
		if (host->plat->status){
			status = host->plat->status(mmc_dev(host->mmc));
			if(!status){
				dev_dbg(mmc_dev(host->mmc), "already card power is OFF. (MMC(%d) status : %d)\n",host->mmc->index ,status);
				return -EINVAL;
			}
		}
	}

	if (!data)
		return 0;

	if (!host->variant->non_power_of_2_blksize &&
	    !is_power_of_2(data->blksz)) {
		dev_err(mmc_dev(host->mmc),
			"unsupported block size (%d bytes)\n", data->blksz);
		return -EINVAL;
	}

	if (data->sg->offset & 3) {
		dev_err(mmc_dev(host->mmc),
			"unsupported alginment (0x%x)\n", data->sg->offset);
		return -EINVAL;
	}

	return 0;
}

/*
 * This must be called with host->lock held
 */
static void mmci_write_clkreg(struct mmci_host *host, u32 clk)
{
	if (host->clk_reg != clk) {
		host->clk_reg = clk;
		writel(clk, host->base + MMCICLOCK);
	}
}

/*
 * This must be called with host->lock held
 */
static void mmci_write_pwrreg(struct mmci_host *host, u32 pwr)
{
	if (host->pwr_reg != pwr) {
		host->pwr_reg = pwr;
		writel(pwr, host->base + MMCIPOWER);
	}
}

/*
 * This must be called with host->lock held
 */
static void mmci_set_clkreg(struct mmci_host *host, unsigned int desired)
{
	struct variant_data *variant = host->variant;
	u32 clk = variant->clkreg;

	if (desired) {
		if (desired >= host->mclk) {
			clk = MCI_CLK_BYPASS;
			if (variant->st_clkdiv)
				clk |= MCI_ST_UX500_NEG_EDGE;
			host->cclk = host->mclk;
		} else if (variant->st_clkdiv) {
			/*
			 * DB8500 TRM says f = mclk / (clkdiv + 2)
			 * => clkdiv = (mclk / f) - 2
			 * Round the divider up so we don't exceed the max
			 * frequency
			 */
			clk = DIV_ROUND_UP(host->mclk, desired) - 2;
			if (clk >= 256)
				clk = 255;
			host->cclk = host->mclk / (clk + 2);
		} else {
			/*
			 * PL180 TRM says f = mclk / (2 * (clkdiv + 1))
			 * => clkdiv = mclk / (2 * f) - 1
			 */
			clk = host->mclk / (2 * desired) - 1;
			if (clk >= 256)
				clk = 255;
			host->cclk = host->mclk / (2 * (clk + 1));
		}

		clk |= variant->clkreg_enable;
		clk |= MCI_CLK_ENABLE;
		/* This hasn't proven to be worthwhile */
		/* clk |= MCI_CLK_PWRSAVE; */
	}

	if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_4)
		clk |= MCI_4BIT_BUS;
	if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_8)
		clk |= MCI_ST_8BIT_BUS;

	if (host->mmc->ios.timing == MMC_TIMING_UHS_DDR50)
		clk |= MCI_ST_UX500_NEG_EDGE;

	mmci_write_clkreg(host, clk);
}

static void
mmci_request_end(struct mmci_host *host, struct mmc_request *mrq)
{
	writel(0, host->base + MMCICOMMAND);

	BUG_ON(host->data);

	host->mrq = NULL;
	host->cmd = NULL;

	mmc_request_done(host->mmc, mrq);

	pm_runtime_mark_last_busy(mmc_dev(host->mmc));
	pm_runtime_put_autosuspend(mmc_dev(host->mmc));
}

static void mmci_set_mask1(struct mmci_host *host, unsigned int mask)
{
	void __iomem *base = host->base;

	if (host->singleirq) {
		unsigned int mask0 = readl(base + MMCIMASK0);

		mask0 &= ~MCI_IRQ1MASK;
		mask0 |= mask;

		writel(mask0, base + MMCIMASK0);
	}

	writel(mask, base + MMCIMASK1);
}

static void mmci_stop_data(struct mmci_host *host)
{
	/* Keep busy mode bit if enabled */
	host->datactrl_reg &= MCI_ST_DPSM_BUSYMODE;

	writel(host->datactrl_reg, host->base + MMCIDATACTRL);
	mmci_set_mask1(host, 0);
	host->data = NULL;
}

static void mmci_init_sg(struct mmci_host *host, struct mmc_data *data)
{
	unsigned int flags = SG_MITER_ATOMIC;

	if (data->flags & MMC_DATA_READ)
		flags |= SG_MITER_TO_SG;
	else
		flags |= SG_MITER_FROM_SG;

	sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
}

/*
 * All the DMA operation mode stuff goes inside this ifdef.
 * This assumes that you have a generic DMA device interface,
 * no custom DMA interfaces are supported.
 */
#ifdef CONFIG_DMA_ENGINE
static void __devinit mmci_dma_setup(struct mmci_host *host)
{
	struct mmci_platform_data *plat = host->plat;
	const char *rxname, *txname;
	dma_cap_mask_t mask;

	if (!plat || !plat->dma_filter) {
		dev_info(mmc_dev(host->mmc), "no DMA platform data\n");
		return;
	}

	/* initialize pre request cookie */
	host->next_data.cookie = 1;

	/* Try to acquire a generic DMA engine slave channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/*
	 * If only an RX channel is specified, the driver will
	 * attempt to use it bidirectionally, however if it is
	 * is specified but cannot be located, DMA will be disabled.
	 */
	if (plat->dma_rx_param) {
		host->dma_rx_channel = dma_request_channel(mask,
							   plat->dma_filter,
							   plat->dma_rx_param);
		/* E.g if no DMA hardware is present */
		if (!host->dma_rx_channel)
			dev_err(mmc_dev(host->mmc), "no RX DMA channel\n");
	}

	if (plat->dma_tx_param) {
		host->dma_tx_channel = dma_request_channel(mask,
							   plat->dma_filter,
							   plat->dma_tx_param);
		if (!host->dma_tx_channel)
			dev_warn(mmc_dev(host->mmc), "no TX DMA channel\n");
	} else {
		host->dma_tx_channel = host->dma_rx_channel;
	}

	if (host->dma_rx_channel)
		rxname = dma_chan_name(host->dma_rx_channel);
	else
		rxname = "none";

	if (host->dma_tx_channel)
		txname = dma_chan_name(host->dma_tx_channel);
	else
		txname = "none";

	dev_info(mmc_dev(host->mmc), "DMA channels RX %s, TX %s\n",
		 rxname, txname);

	/*
	 * Limit the maximum segment size in any SG entry according to
	 * the parameters of the DMA engine device.
	 */
	if (host->dma_tx_channel) {
		struct device *dev = host->dma_tx_channel->device->dev;
		unsigned int max_seg_size = dma_get_max_seg_size(dev);

		if (max_seg_size < host->mmc->max_seg_size)
			host->mmc->max_seg_size = max_seg_size;
	}
	if (host->dma_rx_channel) {
		struct device *dev = host->dma_rx_channel->device->dev;
		unsigned int max_seg_size = dma_get_max_seg_size(dev);

		if (max_seg_size < host->mmc->max_seg_size)
			host->mmc->max_seg_size = max_seg_size;
	}
}

/*
 * This is used in __devinit or __devexit so inline it
 * so it can be discarded.
 */
static inline void mmci_dma_release(struct mmci_host *host)
{
	struct mmci_platform_data *plat = host->plat;

	if (host->dma_rx_channel)
		dma_release_channel(host->dma_rx_channel);
	if (host->dma_tx_channel && plat->dma_tx_param)
		dma_release_channel(host->dma_tx_channel);
	host->dma_rx_channel = host->dma_tx_channel = NULL;
}

static void mmci_dma_data_error(struct mmci_host *host, struct mmc_data *data)
{
	dev_err(mmc_dev(host->mmc), "error during DMA transfer!\n");
	dmaengine_terminate_all(host->dma_current);
	host->dma_current = NULL;
	host->dma_desc_current = NULL;
	data->host_cookie = 0;
}

static void mmci_dma_unmap(struct mmci_host *host, struct mmc_data *data)
{
	struct dma_chan *chan;
	enum dma_data_direction dir;

	if (data->flags & MMC_DATA_READ) {
		dir = DMA_FROM_DEVICE;
		chan = host->dma_rx_channel;
	} else {
		dir = DMA_TO_DEVICE;
		chan = host->dma_tx_channel;
	}

	dma_unmap_sg(chan->device->dev, data->sg, data->sg_len, dir);
}

static void mmci_dma_finalize(struct mmci_host *host, struct mmc_data *data)
{
	u32 status;
	int i;

	/* Wait up to 1ms for the DMA to complete */
	for (i = 0; ; i++) {
		status = readl(host->base + MMCISTATUS);
		if (!(status & MCI_RXDATAAVLBLMASK) || i >= 100)
			break;
		udelay(10);
	}

	/*
	 * Check to see whether we still have some data left in the FIFO -
	 * this catches DMA controllers which are unable to monitor the
	 * DMALBREQ and DMALSREQ signals while allowing us to DMA to non-
	 * contiguous buffers.  On TX, we'll get a FIFO underrun error.
	 */
	if (status & MCI_RXDATAAVLBLMASK) {
		data->error = -EIO;
		mmci_dma_data_error(host, data);
	}

	if (!data->host_cookie)
		mmci_dma_unmap(host, data);

	/*
	 * Use of DMA with scatter-gather is impossible.
	 * Give up with DMA and switch back to PIO mode.
	 */
	if (status & MCI_RXDATAAVLBLMASK) {
		dev_err(mmc_dev(host->mmc), "buggy DMA detected. Taking evasive action.\n");
		mmci_dma_release(host);
	}

	host->dma_current = NULL;
	host->dma_desc_current = NULL;
}

/* prepares DMA channel and DMA descriptor, returns non-zero on failure */
static int __mmci_dma_prep_data(struct mmci_host *host, struct mmc_data *data,
				struct dma_chan **dma_chan,
				struct dma_async_tx_descriptor **dma_desc)
{
	struct variant_data *variant = host->variant;
	struct dma_slave_config conf = {
		.src_addr = host->phybase + MMCIFIFO,
		.dst_addr = host->phybase + MMCIFIFO,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.src_maxburst = variant->fifohalfsize >> 2, /* # of words */
		.dst_maxburst = variant->fifohalfsize >> 2, /* # of words */
	};
	struct dma_chan *chan;
	struct dma_device *device;
	struct dma_async_tx_descriptor *desc;
	int nr_sg;

	if (data->flags & MMC_DATA_READ) {
		conf.direction = DMA_FROM_DEVICE;
		chan = host->dma_rx_channel;
	} else {
		conf.direction = DMA_TO_DEVICE;
		chan = host->dma_tx_channel;
	}

	/* If there's no DMA channel, fall back to PIO */
	if (!chan)
		return -EINVAL;

	/*
	 * If less than or equal to the fifo size, don't bother with DMA
	 * SDIO transfers may not be 4 bytes aligned, fall back to PIO
	 */
	if (data->blksz * data->blocks <= variant->fifosize ||
	    (data->blksz * data->blocks) & 3)
		return -EINVAL;

	device = chan->device;
	nr_sg = dma_map_sg(device->dev, data->sg, data->sg_len, conf.direction);
	if (nr_sg == 0)
		return -EINVAL;

	dmaengine_slave_config(chan, &conf);
	desc = device->device_prep_slave_sg(chan, data->sg, nr_sg,
					    conf.direction, DMA_CTRL_ACK);
	if (!desc)
		goto unmap_exit;

	*dma_chan = chan;
	*dma_desc = desc;

	return 0;

 unmap_exit:
	dma_unmap_sg(device->dev, data->sg, data->sg_len, conf.direction);
	return -ENOMEM;
}

static int inline mmci_dma_prep_data(struct mmci_host *host,
				     struct mmc_data *data)
{
	/* Check if next job is already prepared. */
	if (host->dma_current && host->dma_desc_current)
		return 0;

	/* No job were prepared thus do it now. */
	return __mmci_dma_prep_data(host, data, &host->dma_current,
				    &host->dma_desc_current);
}

static inline int mmci_dma_prep_next(struct mmci_host *host,
				     struct mmc_data *data)
{
	struct mmci_host_next *nd = &host->next_data;
	return __mmci_dma_prep_data(host, data, &nd->dma_chan, &nd->dma_desc);
}

static int mmci_dma_start_data(struct mmci_host *host)
{
	int ret;
	struct mmc_data *data = host->data;
	struct variant_data *variant = host->variant;

	ret = mmci_dma_prep_data(host, host->data);
	if (ret)
		return ret;

	/* Okay, go for it. */
	dev_vdbg(mmc_dev(host->mmc),
		 "Submit MMCI DMA job, sglen %d blksz %04x blks %04x flags %08x\n",
		 data->sg_len, data->blksz, data->blocks, data->flags);
	dmaengine_submit(host->dma_desc_current);
	dma_async_issue_pending(host->dma_current);

	host->datactrl_reg |= MCI_DPSM_DMAENABLE;

	/* Some hardware versions need special flags for SDIO DMA write */
	if (variant->sdio && host->mmc->card && mmc_card_sdio(host->mmc->card)
	    && (data->flags & MMC_DATA_WRITE))
		host->datactrl_reg |= variant->dma_sdio_req_ctrl;

	/* Trigger the DMA transfer */
	writel(host->datactrl_reg, host->base + MMCIDATACTRL);

	/*
	 * Let the MMCI say when the data is ended and it's time
	 * to fire next DMA request. When that happens, MMCI will
	 * call mmci_data_end()
	 */
	writel(readl(host->base + MMCIMASK0) | MCI_DATAENDMASK,
	       host->base + MMCIMASK0);
	return 0;
}

static void mmci_get_next_data(struct mmci_host *host, struct mmc_data *data)
{
	struct mmci_host_next *next = &host->next_data;

	WARN_ON(data->host_cookie && data->host_cookie != next->cookie);
	WARN_ON(!data->host_cookie && (next->dma_desc || next->dma_chan));

	host->dma_desc_current = next->dma_desc;
	host->dma_current = next->dma_chan;
	next->dma_desc = NULL;
	next->dma_chan = NULL;
}

static void mmci_pre_request(struct mmc_host *mmc, struct mmc_request *mrq,
			     bool is_first_req)
{
	struct mmci_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;
	struct mmci_host_next *nd = &host->next_data;

	if (!data)
		return;

	BUG_ON(data->host_cookie);

	if (mmci_validate_data(host, data))
		return;

	/*
	 * Don't prepare DMA if there is no previous request,
	 * is_first_req is set. Instead, prepare DMA while
	 * start command is being issued.
	 */
	if (!is_first_req && !mmci_dma_prep_next(host, data))
		data->host_cookie = ++nd->cookie < 0 ? 1 : nd->cookie;
}

static void mmci_post_request(struct mmc_host *mmc, struct mmc_request *mrq,
			      int err)
{
	struct mmci_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data || !data->host_cookie)
		return;

	mmci_dma_unmap(host, data);

	if (err) {
		struct mmci_host_next *next = &host->next_data;
		struct dma_chan *chan;
		if (data->flags & MMC_DATA_READ)
			chan = host->dma_rx_channel;
		else
			chan = host->dma_tx_channel;
		dmaengine_terminate_all(chan);

		next->dma_desc = NULL;
		next->dma_chan = NULL;
	}
}

#else
/* Blank functions if the DMA engine is not available */
static void mmci_get_next_data(struct mmci_host *host, struct mmc_data *data)
{
}
static inline void mmci_dma_setup(struct mmci_host *host)
{
}

static inline void mmci_dma_release(struct mmci_host *host)
{
}

static inline void mmci_dma_unmap(struct mmci_host *host, struct mmc_data *data)
{
}

static inline void mmci_dma_finalize(struct mmci_host *host, struct mmc_data *data)
{
}

static inline void mmci_dma_data_error(struct mmci_host *host, struct mmc_data *data)
{
}

static inline int mmci_dma_start_data(struct mmci_host *host)
{
	return -ENOSYS;
}

static inline int mmci_dma_prep_data(struct mmci_host *host, struct mmc_data *data)
{
	return -ENOSYS;
}

#define mmci_pre_request NULL
#define mmci_post_request NULL

#endif

static void mmci_check_busy(struct mmci_host *host)
{
	/* For SDIO, wait until the device is not busy */
	if (host->variant->sdio && host->mmc->card &&
		mmc_card_sdio(host->mmc->card)) {
		int count = 0;
		while (readl(host->base + MMCISTATUS) & MCI_ST_CARDBUSY) {
			udelay(10);
			if (++count > 50000) {
				dev_err(mmc_dev(host->mmc),
					"Time out 500msec.\n");
				break;
			}
		}
	}
}

static void mmci_setup_datactrl(struct mmci_host *host, struct mmc_data *data)
{
	struct variant_data *variant = host->variant;
	unsigned int datactrl, timeout;
	unsigned long long clks;
	void __iomem *base;
	int blksz_bits;

	dev_dbg(mmc_dev(host->mmc), "blksz %04x blks %04x flags %08x\n",
		data->blksz, data->blocks, data->flags);

	mmci_check_busy(host);

	host->data = data;
	host->size = data->blksz * data->blocks;
	data->bytes_xfered = 0;

	clks = (unsigned long long)data->timeout_ns * host->cclk;
	do_div(clks, 1000000000UL);

	timeout = data->timeout_clks + (unsigned int)clks;

	base = host->base;
	writel(timeout, base + MMCIDATATIMER);
	writel(host->size, base + MMCIDATALENGTH);

	blksz_bits = ffs(data->blksz) - 1;

	if (variant->blksz_datactrl16)
		datactrl = MCI_DPSM_ENABLE | (data->blksz << 16);
	else
		datactrl = MCI_DPSM_ENABLE | blksz_bits << 4;

	if (data->flags & MMC_DATA_READ)
		datactrl |= MCI_DPSM_DIRECTION;

	/* The ST Micro variants has a special bit to enable SDIO */
	if (variant->sdio && host->mmc->card)
		if (mmc_card_sdio(host->mmc->card)) {

			/*
			 * The ST Micro variant for SDIO small write transfers
			 * needs to have clock H/W flow control disabled,
			 * otherwise the transfer will not start. The
			 * threshold size depends on MCLK.
			 */
			u32 clk;
			if (data->flags & MMC_DATA_WRITE &&
			    (host->size < 8 ||
			     (host->size <= 8 && host->mclk > 50000000)))
				clk = host->clk_reg & ~variant->clkreg_enable;
			else
				clk = host->clk_reg | variant->clkreg_enable;

			mmci_write_clkreg(host, clk);

			/*
			 * The ST Micro variants has a special bit
			 * to enable SDIO and busymode detect
			 */
			datactrl |= MCI_ST_DPSM_SDIOEN | MCI_ST_DPSM_BUSYMODE;
		}

	if (host->mmc->ios.timing == MMC_TIMING_UHS_DDR50)
		datactrl |= MCI_ST_DPSM_DDRMODE;

	host->datactrl_reg = datactrl;
	writel(datactrl, base + MMCIDATACTRL);
}

static void mmci_start_data(struct mmci_host *host, struct mmc_data *data)
{
	unsigned int irqmask;
	struct variant_data *variant = host->variant;
	void __iomem *base = host->base;

	mmci_check_busy(host);

	/*
	 * Attempt to use DMA operation mode, if this
	 * should fail, fall back to PIO mode
	 */
	if (!mmci_dma_start_data(host))
		return;

	/* IRQ mode, map the SG list for CPU reading/writing */
	mmci_init_sg(host, data);

	if (data->flags & MMC_DATA_READ) {
		irqmask = MCI_RXFIFOHALFFULLMASK;

		/*
		 * If we have less than the fifo 'half-full' threshold to
		 * transfer, trigger a PIO interrupt as soon as any data
		 * is available.
		 */
		if (host->size < variant->fifohalfsize)
			irqmask |= MCI_RXDATAAVLBLMASK;
	} else {
		/*
		 * We don't actually need to include "FIFO empty" here
		 * since its implicit in "FIFO half empty".
		 */
		irqmask = MCI_TXFIFOHALFEMPTYMASK;
	}

	writel(readl(base + MMCIMASK0) & ~MCI_DATAENDMASK, base + MMCIMASK0);
	mmci_set_mask1(host, irqmask);
}

static void
mmci_start_command(struct mmci_host *host, struct mmc_command *cmd, u32 c)
{
	unsigned int count = 0;

	void __iomem *base = host->base;

	dev_dbg(mmc_dev(host->mmc), "op %02x arg %08x flags %08x\n",
	    cmd->opcode, cmd->arg, cmd->flags);

	mmci_check_busy(host);

	if (readl(base + MMCICOMMAND) & MCI_CPSM_ENABLE) {
		writel(0, base + MMCICOMMAND);
		udelay(1);
	}

	c |= cmd->opcode | MCI_CPSM_ENABLE;
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			c |= MCI_CPSM_LONGRSP;
		c |= MCI_CPSM_RESPONSE;
	}
	if (/*interrupt*/0)
		c |= MCI_CPSM_INTERRUPT;

	/*
	 * For levelshifters we must not use more than 25MHz when
	 * sending commands.
	 */
	host->cclk_desired = host->cclk;
	if (host->plat->levelshifter && (host->cclk_desired > 25000000))
		mmci_set_clkreg(host, 25000000);

	host->cmd = cmd;

	if (host->variant->sdio &&
		host->mmc->card &&
		mmc_card_sdio(host->mmc->card))	{
		while (!gpio_get_value(211)) {
	    	udelay(10);
    
	    	if (count > 50000) {
				printk("Time out 500msec.\n");
				break;
			}
			count++;
		}
	}

	writel(cmd->arg, base + MMCIARGUMENT);
	writel(c, base + MMCICOMMAND);
}

static void
mmci_data_irq(struct mmci_host *host, struct mmc_data *data,
	      unsigned int status)
{
	u32 cmd_print;

	cmd_print = readl(host->base + MMCICOMMAND);
	/* First check for errors */
	if (status & (MCI_DATACRCFAIL|MCI_DATATIMEOUT|MCI_STARTBITERR|
		      MCI_TXUNDERRUN|MCI_RXOVERRUN)) {
		u32 remain, success;

		/* Terminate the DMA transfer */
		if (dma_inprogress(host)) {
			mmci_dma_data_error(host, data);
			mmci_dma_unmap(host, data);
		}

		/*
		 * Calculate how far we are into the transfer.  Note that
		 * the data counter gives the number of bytes transferred
		 * on the MMC bus, not on the host side.  On reads, this
		 * can be as much as a FIFO-worth of data ahead.  This
		 * matters for FIFO overruns only.
		 */
		remain = readl(host->base + MMCIDATACNT);
		success = data->blksz * data->blocks - remain;

		dev_dbg(mmc_dev(host->mmc), "MCI ERROR IRQ, status 0x%08x at 0x%08x\n",
			status, success);
		if (status & MCI_DATACRCFAIL) {
			/* Last block was not successful */
			success -= 1;
			data->error = -EILSEQ;
			printk(KERN_ERR"%s: [MMC] DATA CRC STATUS: %x, CMD: %d\n",
				mmc_hostname(host->mmc), status, cmd_print & 0x3F);
		} else if (status & MCI_DATATIMEOUT) {
			data->error = -ETIMEDOUT;
			printk(KERN_ERR"%s: [MMC] DATA TIMEOUT STATUS: %x, CMD: %d\n",
				mmc_hostname(host->mmc), status, cmd_print & 0x3F);
		} else if (status & MCI_STARTBITERR) {
			data->error = -ECOMM;
			printk(KERN_ERR"%s: [MMC] DATA STARTBIT STATUS: %x, CMD: %d\n",
				mmc_hostname(host->mmc), status, cmd_print & 0x3F);
		} else if (status & MCI_TXUNDERRUN) {
			data->error = -EIO;
			printk(KERN_ERR"%s: [MMC] DATA TX STATUS: %x, CMD: %d\n",
				mmc_hostname(host->mmc), status, cmd_print & 0x3F);
		} else if (status & MCI_RXOVERRUN) {
			if (success > host->variant->fifosize)
				success -= host->variant->fifosize;
			else
				success = 0;
			data->error = -EIO;
			printk(KERN_ERR"%s: [MMC] DATA RX STATUS: %x, CMD: %d\n",
				mmc_hostname(host->mmc), status, cmd_print & 0x3F);
		}
		data->bytes_xfered = round_down(success, data->blksz);
	}

	if (status & MCI_DATABLOCKEND)
		dev_err(mmc_dev(host->mmc), "stray MCI_DATABLOCKEND interrupt\n");

	if (status & MCI_DATAEND || data->error) {
		if (dma_inprogress(host))
			mmci_dma_finalize(host, data);
		mmci_stop_data(host);

		if (!data->error)
			/* The error clause is handled above, success! */
			data->bytes_xfered = data->blksz * data->blocks;

		if (!data->stop || host->mrq->sbc) {
			mmci_request_end(host, data->mrq);
		} else {
			mmci_start_command(host, data->stop, 0);
		}
	}
}

static void
mmci_cmd_irq(struct mmci_host *host, struct mmc_command *cmd,
	     unsigned int status)
{
	void __iomem *base = host->base;
	u32 cmd_print;
	bool sbc = (cmd == host->mrq->sbc);

	cmd_print = readl(host->base + MMCICOMMAND);

	host->cmd = NULL;

	if (status & MCI_CMDTIMEOUT) {
		cmd->error = -ETIMEDOUT;
		printk(KERN_ERR"%s: [MMC] CMD TIMEOUT STATUS: %x, CMD: %d\n",
			mmc_hostname(host->mmc), status, cmd_print & 0x3F);		
	} else if (status & MCI_CMDCRCFAIL && cmd->flags & MMC_RSP_CRC) {
		cmd->error = -EILSEQ;
		printk(KERN_ERR"%s: [MMC] CMD Command CRC STATUS: %x, CMD: %d\n",
			mmc_hostname(host->mmc), status, cmd_print & 0x3F);		
	} else {
		cmd->resp[0] = readl(base + MMCIRESPONSE0);
		cmd->resp[1] = readl(base + MMCIRESPONSE1);
		cmd->resp[2] = readl(base + MMCIRESPONSE2);
		cmd->resp[3] = readl(base + MMCIRESPONSE3);
	}

	/*
	 * For levelshifters we might have decreased cclk to 25MHz when
	 * sending commands, then we restore the frequency here.
	 */
	if (host->plat->levelshifter && (host->cclk_desired > host->cclk))
		mmci_set_clkreg(host, host->cclk_desired);

	if ((!sbc && !cmd->data) || cmd->error) {
		/* Terminate the DMA transfer */
		if (dma_inprogress(host)) {
			mmci_dma_data_error(host, host->mrq->data);
			mmci_dma_unmap(host, host->mrq->data);
		}
		if (host->data)
			mmci_stop_data(host);
		mmci_request_end(host, host->mrq);
	} else if (sbc) {
		mmci_start_command(host, host->mrq->cmd, 0);
	} else if (!(cmd->data->flags & MMC_DATA_READ)) {
		mmci_setup_datactrl(host, cmd->data);
		mmci_start_data(host, cmd->data);
	}
}

static int mmci_pio_read(struct mmci_host *host, char *buffer, unsigned int remain)
{
	void __iomem *base = host->base;
	char *ptr = buffer;
	u32 status;
	int host_remain = host->size;

	do {
		int count = host_remain - (readl(base + MMCIFIFOCNT) << 2);

		if (count > remain)
			count = remain;

		if (count <= 0)
			break;

		/*
		 * SDIO especially may want to send something that is
		 * not divisible by 4 (as opposed to card sectors
		 * etc). Therefore make sure to always read the last bytes
		 * while only doing full 32-bit reads towards the FIFO.
		 */
		if (unlikely(count & 0x3)) {
			if (count < 4) {
				unsigned char buf[4];
				readsl(base + MMCIFIFO, buf, 1);
				memcpy(ptr, buf, count);
			} else {
				readsl(base + MMCIFIFO, ptr, count >> 2);
				count &= ~0x3;
			}
		} else {
			readsl(base + MMCIFIFO, ptr, count >> 2);
		}

		ptr += count;
		remain -= count;
		host_remain -= count;

		if (remain == 0)
			break;

		status = readl(base + MMCISTATUS);
	} while (status & MCI_RXDATAAVLBL);

	return ptr - buffer;
}

static int mmci_pio_write(struct mmci_host *host, char *buffer, unsigned int remain, u32 status)
{
	struct variant_data *variant = host->variant;
	void __iomem *base = host->base;
	char *ptr = buffer;

	do {
		unsigned int count, maxcnt;

		maxcnt = status & MCI_TXFIFOEMPTY ?
			 variant->fifosize : variant->fifohalfsize;
		count = min(remain, maxcnt);

		/*
		 * SDIO especially may want to send something that is
		 * not divisible by 4 (as opposed to card sectors
		 * etc), and the FIFO only accept full 32-bit writes.
		 * So compensate by adding +3 on the count, a single
		 * byte become a 32bit write, 7 bytes will be two
		 * 32bit writes etc.
		 */
		writesl(base + MMCIFIFO, ptr, (count + 3) >> 2);

		ptr += count;
		remain -= count;

		if (remain == 0)
			break;

		status = readl(base + MMCISTATUS);
	} while (status & MCI_TXFIFOHALFEMPTY);

	return ptr - buffer;
}

/*
 * PIO data transfer IRQ handler.
 */
static irqreturn_t mmci_pio_irq(int irq, void *dev_id)
{
	struct mmci_host *host = dev_id;
	struct sg_mapping_iter *sg_miter = &host->sg_miter;
	struct variant_data *variant = host->variant;
	void __iomem *base = host->base;
	unsigned long flags;
	u32 status;

	status = readl(base + MMCISTATUS);

	dev_dbg(mmc_dev(host->mmc), "irq1 (pio) %08x\n", status);

	local_irq_save(flags);

	do {
		unsigned int remain, len;
		char *buffer;

		/*
		 * For write, we only need to test the half-empty flag
		 * here - if the FIFO is completely empty, then by
		 * definition it is more than half empty.
		 *
		 * For read, check for data available.
		 */
		if (!(status & (MCI_TXFIFOHALFEMPTY|MCI_RXDATAAVLBL)))
			break;

		if (!sg_miter_next(sg_miter))
			break;

		buffer = sg_miter->addr;
		remain = sg_miter->length;

		len = 0;
		if (status & MCI_RXACTIVE)
			len = mmci_pio_read(host, buffer, remain);
		if (status & MCI_TXACTIVE)
			len = mmci_pio_write(host, buffer, remain, status);

		sg_miter->consumed = len;

		host->size -= len;
		remain -= len;

		if (remain)
			break;

		status = readl(base + MMCISTATUS);
	} while (1);

	sg_miter_stop(sg_miter);

	local_irq_restore(flags);

	/*
	 * If we have less than the fifo 'half-full' threshold to transfer,
	 * trigger a PIO interrupt as soon as any data is available.
	 */
	if (status & MCI_RXACTIVE && host->size < variant->fifohalfsize)
		mmci_set_mask1(host, MCI_RXDATAAVLBLMASK);

	/*
	 * If we run out of data, disable the data IRQs; this
	 * prevents a race where the FIFO becomes empty before
	 * the chip itself has disabled the data path, and
	 * stops us racing with our data end IRQ.
	 */
	if (host->size == 0) {
		mmci_set_mask1(host, 0);
		writel(readl(base + MMCIMASK0) | MCI_DATAENDMASK, base + MMCIMASK0);
	}

	return IRQ_HANDLED;
}

/*
 * Handle completion of command and data transfers.
 */
static irqreturn_t mmci_irq(int irq, void *dev_id)
{
	struct mmci_host *host = dev_id;
	u32 status;
	int ret = 0;

	spin_lock(&host->lock);

	do {
		struct mmc_command *cmd;
		struct mmc_data *data;

		status = readl(host->base + MMCISTATUS);

		if (host->singleirq) {
			if (status & readl(host->base + MMCIMASK1))
				mmci_pio_irq(irq, dev_id);

			status &= ~MCI_IRQ1MASK;
		}

		status &= readl(host->base + MMCIMASK0);
		writel(status, host->base + MMCICLEAR);

		dev_dbg(mmc_dev(host->mmc), "irq0 (data+cmd) %08x\n", status);

		data = host->data;
		if (status & (MCI_DATACRCFAIL|MCI_DATATIMEOUT|MCI_STARTBITERR|
			      MCI_TXUNDERRUN|MCI_RXOVERRUN|MCI_DATAEND|
			      MCI_DATABLOCKEND) && data)
			mmci_data_irq(host, data, status);

		cmd = host->cmd;
		if (status & (MCI_CMDCRCFAIL|MCI_CMDTIMEOUT|MCI_CMDSENT|MCI_CMDRESPEND) && cmd)
			mmci_cmd_irq(host, cmd, status);

		ret = 1;
	} while (status);

	spin_unlock(&host->lock);

	return IRQ_RETVAL(ret);
}

static void mmci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmci_host *host = mmc_priv(mmc);
	unsigned long flags;
	bool dmaprep_after_cmd = false;

	WARN_ON(host->mrq != NULL);

	mrq->cmd->error = mmci_validate_data(host, mrq->data);
	if (mrq->cmd->error) {
		mmc_request_done(mmc, mrq);
		return;
	}

	pm_runtime_get_sync(mmc_dev(mmc));

	spin_lock_irqsave(&host->lock, flags);

	host->mrq = mrq;

	if (mrq->data) {
		dmaprep_after_cmd =
			(host->variant->clkreg_enable &&
			 (mrq->data->flags & MMC_DATA_READ)) ||
			!(mrq->data->flags & MMC_DATA_READ);
		mmci_get_next_data(host, mrq->data);
		if (mrq->data->flags & MMC_DATA_READ) {
			mmci_setup_datactrl(host, mrq->data);
			if (!dmaprep_after_cmd)
				mmci_start_data(host, mrq->data);
		}
	}

	if (mrq->sbc)
		mmci_start_command(host, mrq->sbc, 0);
	else
		mmci_start_command(host, mrq->cmd, 0);

	if (mrq->data && dmaprep_after_cmd) {
		mmci_dma_prep_data(host, mrq->data);

		if (mrq->data->flags & MMC_DATA_READ)
			mmci_start_data(host, mrq->data);
	}

	spin_unlock_irqrestore(&host->lock, flags);
}

static void mmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmci_host *host = mmc_priv(mmc);
	struct variant_data *variant = host->variant;
	u32 pwr = 0;
	unsigned long flags;
	int ret;

	pm_runtime_get_sync(mmc_dev(mmc));

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		if (host->vcc) 
			ret = mmc_regulator_set_ocr(mmc, host->vcc, 0);

		if (host->plat->ios_handler &&
			host->plat->ios_handler(mmc_dev(mmc), ios, RPM_ACTIVE))
				dev_err(mmc_dev(mmc), "platform ios_handler failed\n");

		mdelay(60);
		break;
	case MMC_POWER_UP:
		if (host->vcc) {
			ret = mmc_regulator_set_ocr(mmc, host->vcc, ios->vdd);
			if (ret) {
				dev_err(mmc_dev(mmc), "unable to set OCR\n");
				/*
				 * The .set_ios() function in the mmc_host_ops
				 * struct return void, and failing to set the
				 * power should be rare so we print an error
				 * and return here.
				 */
				goto out;
			}else
				mdelay(10);
		}
		/*
		 * The ST Micro variant doesn't have the PL180s MCI_PWR_UP
		 * and instead uses MCI_PWR_ON so apply whatever value is
		 * configured in the variant data.
		 */
		pwr |= variant->pwrreg_powerup;
		mdelay(1);

		if (host->plat->ios_handler &&
			host->plat->ios_handler(mmc_dev(mmc), ios, RPM_ACTIVE))
				dev_err(mmc_dev(mmc), "platform ios_handler failed\n");

		break;
	case MMC_POWER_ON:
		pwr |= MCI_PWR_ON;

		if (host->plat->ios_handler &&
			host->plat->ios_handler(mmc_dev(mmc), ios, RPM_ACTIVE))
				dev_err(mmc_dev(mmc), "platform ios_handler failed\n");

		break;
	}

	if (variant->signal_direction && ios->power_mode != MMC_POWER_OFF) {
		/*
		 * The ST Micro variant has some additional bits
		 * indicating signal direction for the signals in
		 * the SD/MMC bus and feedback-clock usage.
		 */
		pwr |= host->plat->sigdir;

		if (ios->bus_width == MMC_BUS_WIDTH_4)
			pwr &= ~MCI_ST_DATA74DIREN;
		else if (ios->bus_width == MMC_BUS_WIDTH_1)
			pwr &= (~MCI_ST_DATA74DIREN &
				~MCI_ST_DATA31DIREN &
				~MCI_ST_DATA2DIREN);
	}

	if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN) {
		if (host->hw_designer != AMBA_VENDOR_ST)
			pwr |= MCI_ROD;
		else {
			/*
			 * The ST Micro variant use the ROD bit for something
			 * else and only has OD (Open Drain).
			 */
			pwr |= MCI_OD;
		}
	}

	spin_lock_irqsave(&host->lock, flags);

	mmci_set_clkreg(host, ios->clock);
	mmci_write_pwrreg(host, pwr);

	spin_unlock_irqrestore(&host->lock, flags);

 out:
	pm_runtime_mark_last_busy(mmc_dev(mmc));
	pm_runtime_put_autosuspend(mmc_dev(mmc));
}

static int mmci_get_ro(struct mmc_host *mmc)
{
	struct mmci_host *host = mmc_priv(mmc);

	if (host->gpio_wp == -ENOSYS)
		return -ENOSYS;

	return gpio_get_value_cansleep(host->gpio_wp);
}

static int mmci_get_cd(struct mmc_host *mmc)
{
	struct mmci_host *host = mmc_priv(mmc);
	struct mmci_platform_data *plat = host->plat;
	unsigned int status;

	if (host->gpio_cd == -ENOSYS) {
		if (!plat->status)
			return 1; /* Assume always present */

		status = plat->status(mmc_dev(host->mmc));
	} else {
		status = !!gpio_get_value_cansleep(host->gpio_cd)
			^ plat->cd_invert;
		}

	/*
	 * Use positive logic throughout - status is zero for no card,
	 * non-zero for card inserted.
	 */
	return status;
}

static int mmci_sig_volt_switch(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmci_host *host = mmc_priv(mmc);
	int ret = 0;

	if (host->plat->ios_handler) {
		pm_runtime_get_sync(mmc_dev(mmc));
		ret = host->plat->ios_handler(mmc_dev(mmc), ios, RPM_ACTIVE);
		pm_runtime_mark_last_busy(mmc_dev(mmc));
		pm_runtime_put_autosuspend(mmc_dev(mmc));
	}

	return ret;
}

#ifndef CONFIG_STE_WLAN
static void mmci_status_notify_cb(int card_present, void *dev_id)
{
	struct mmci_host *host = (struct mmci_host *)dev_id;

	mmc_detect_change(host->mmc, 0);
}
#endif

static irqreturn_t mmci_cd_irq(int irq, void *dev_id)
{
	struct mmci_host *host = dev_id;

	if (host->plat->gpio_cd) {
		if (gpio_get_value(host->plat->gpio_cd) == 0) {
			printk(KERN_ERR"[MMC]card inserted\n");
			mmc_detect_change(host->mmc, msecs_to_jiffies(200));
		} else {
			printk(KERN_ERR"[MMC]card removed\n");
			mmc_detect_change(host->mmc, msecs_to_jiffies(50));
		}
      } else
		printk(KERN_ERR"gpio_cd is not defined\n");

	return IRQ_HANDLED;
}

static void mmci_abort_request(struct mmc_host *mmc)
{
	struct mmci_host *host = mmc_priv(mmc);
	unsigned long flags;

	pm_runtime_get_sync(mmc_dev(mmc));
	spin_lock_irqsave(&host->lock, flags);

	dev_warn(mmc_dev(mmc), "Aborting request, %s%s\n",
		host->data ? "data" : "",
		host->cmd ? "cmd" : "");

	if (host->mrq) {
		/* Terminate the DMA transfer */
		if (dma_inprogress(host)) {
			mmci_dma_data_error(host, host->mrq->data);
			mmci_dma_unmap(host, host->mrq->data);
		}

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			mmci_stop_data(host);
		} else if (host->cmd) {
			host->cmd->error = -ETIMEDOUT;
		} else {
			host->mrq->cmd->error = -ETIMEDOUT;
		}

		host->cmd = NULL;
		host->mrq = NULL;
		host->pwr_reg = 0;
		host->clk_reg = 0;
		host->datactrl_reg = 0;

		if (host->plat->reset)
			host->plat->reset(mmc_dev(mmc));

		writel(0, host->base + MMCIPOWER);
		writel(0, host->base + MMCICLOCK);
		writel(0, host->base + MMCIARGUMENT);
		writel(0, host->base + MMCICOMMAND);
		writel(0, host->base + MMCIDATACTRL);
		writel(0, host->base + MMCIMASK0);
		writel(0, host->base + MMCIMASK1);
		writel(0xfff, host->base + MMCICLEAR);
		writel(MCI_IRQENABLE, host->base + MMCIMASK0);
	} else {
		dev_warn(mmc_dev(mmc), "Nothing to abort, "
			"request already completed?\n");
	}

	/* Disable DMA, use PIO */
	mmci_dma_release(host);
	dev_warn(mmc_dev(mmc), "Disabling DMA for this host\n");
	host->dma_was_disabled = 1;

	spin_unlock_irqrestore(&host->lock, flags);

	pm_runtime_mark_last_busy(mmc_dev(mmc));
	pm_runtime_put_autosuspend(mmc_dev(mmc));
}

static const struct mmc_host_ops mmci_ops = {
	.request	= mmci_request,
	.pre_req	= mmci_pre_request,
	.post_req	= mmci_post_request,
	.set_ios	= mmci_set_ios,
	.get_ro		= mmci_get_ro,
	.get_cd		= mmci_get_cd,
	.dump_regs	= dump_mmci_regs,
	.start_signal_voltage_switch = mmci_sig_volt_switch,
	.abort_request  = mmci_abort_request,
};

extern struct class *sec_class;
static struct device *sd_detection_cmd_dev;

static ssize_t mmci_sd_detection_cmd_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmci_host *host = dev_get_drvdata(dev);
	unsigned int detect;
	
#ifdef SD_HW_NODETECTION
	 if (host->mmc->card) {
                printk(KERN_DEBUG "sdcc3: card inserted.\n");
                return sprintf(buf, "Insert\n");
        } else {
                printk(KERN_DEBUG "sdcc3: card removed.\n");
                return sprintf(buf, "Remove\n");
        }
#else
	if (host->plat->gpio_cd)
		detect = gpio_get_value(host->plat->gpio_cd);
	else {
		pr_info("%s : External SD detect pin Error\n", __func__);
		return  sprintf(buf, "Error\n");
	}

	pr_info("%s : detect = %d.\n", __func__,  detect);
	if (!detect) {
		pr_debug("mmci: card inserted.\n");
		return sprintf(buf, "Insert\n");
	} else {
		pr_debug("mmci: card removed.\n");
		return sprintf(buf, "Remove\n");
	}
#endif
}

static DEVICE_ATTR(status, 0444, mmci_sd_detection_cmd_show, NULL);
static int __devinit mmci_probe(struct amba_device *dev,
	const struct amba_id *id)
{
	struct mmci_platform_data *plat = dev->dev.platform_data;
	struct variant_data *variant = id->data;
	struct mmci_host *host;
	struct mmc_host *mmc;
	int ret;

	/* must have platform data */
	if (!plat) {
		ret = -EINVAL;
		goto out;
	}

	ret = amba_request_regions(dev, DRIVER_NAME);
	if (ret)
		goto out;

	mmc = mmc_alloc_host(sizeof(struct mmci_host), &dev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto rel_regions;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;

	host->gpio_wp = -ENOSYS;
	host->gpio_cd = -ENOSYS;
	host->gpio_cd_irq = -1;

	host->dma_was_disabled = 0;

	host->hw_designer = amba_manf(dev);
	host->hw_revision = amba_rev(dev);
	dev_dbg(mmc_dev(mmc), "designer ID = 0x%02x\n", host->hw_designer);
	dev_dbg(mmc_dev(mmc), "revision = 0x%01x\n", host->hw_revision);

	host->clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		host->clk = NULL;
		goto host_free;
	}

	ret = clk_enable(host->clk);
	if (ret)
		goto clk_free;

	host->plat = plat;
	host->variant = variant;
	host->mclk = clk_get_rate(host->clk);
	/*
	 * According to the spec, mclk is max 100 MHz,
	 * so we try to adjust the clock down to this,
	 * (if possible).
	 */
	if (host->mclk > 100000000) {
		ret = clk_set_rate(host->clk, 100000000);
		if (ret < 0)
			goto clk_disable;
		host->mclk = clk_get_rate(host->clk);
		dev_dbg(mmc_dev(mmc), "eventual mclk rate: %u Hz\n",
			host->mclk);
	}
	host->phybase = dev->res.start;
	host->base = ioremap(dev->res.start, resource_size(&dev->res));
	if (!host->base) {
		ret = -ENOMEM;
		goto clk_disable;
	}

	mmc->ops = &mmci_ops;
	/*
	 * The ARM and ST versions of the block have slightly different
	 * clock divider equations which means that the minimum divider
	 * differs too.
	 */
	if (variant->st_clkdiv)
		mmc->f_min = DIV_ROUND_UP(host->mclk, 257);
	else
		mmc->f_min = DIV_ROUND_UP(host->mclk, 512);
	/*
	 * If the platform data supplies a maximum operating
	 * frequency, this takes precedence. Else, we fall back
	 * to using the module parameter, which has a (low)
	 * default value in case it is not specified. Either
	 * value must not exceed the clock rate into the block,
	 * of course.
	 */
	if (plat->f_max)
		mmc->f_max = min(host->mclk, plat->f_max);
	else
		mmc->f_max = min(host->mclk, fmax);
	dev_dbg(mmc_dev(mmc), "clocking block at %u Hz\n", mmc->f_max);

#ifdef CONFIG_REGULATOR
	/* If we're using the regulator framework, try to fetch a regulator */
	host->vcc = regulator_get(&dev->dev, "vmmc");
	if (IS_ERR(host->vcc))
		host->vcc = NULL;
	else {
		int mask = mmc_regulator_get_ocrmask(host->vcc);

		if (mask < 0)
			dev_err(&dev->dev, "error getting OCR mask (%d)\n",
				mask);
		else {
			host->mmc->ocr_avail = (u32) mask;
			if (plat->ocr_mask)
				dev_warn(&dev->dev,
				 "Provided ocr_mask/setpower will not be used "
				 "(using regulator instead)\n");
		}
	}
#endif
	/* Fall back to platform data if no regulator is found */
	if (host->vcc == NULL)
		mmc->ocr_avail = plat->ocr_mask;
	mmc->caps = plat->capabilities;
	mmc->caps2 = plat->capabilities2;

	/* We support these PM capabilities. */
	mmc->pm_caps = MMC_PM_KEEP_POWER;

	/*
	 * We can do SGIO
	 */
	mmc->max_segs = NR_SG;

	/*
	 * Since only a certain number of bits are valid in the data length
	 * register, we must ensure that we don't exceed 2^num-1 bytes in a
	 * single request.
	 */
	mmc->max_req_size = (1 << variant->datalength_bits) - 1;

	/*
	 * Set the maximum segment size.  Since we aren't doing DMA
	 * (yet) we are only limited by the data length register.
	 */
	mmc->max_seg_size = mmc->max_req_size;

	/*
	 * Block size can be up to 2048 bytes, but must be a power of two.
	 */
	mmc->max_blk_size = 1 << 11;

	/*
	 * Limit the number of blocks transferred so that we don't overflow
	 * the maximum request size.
	 */
	mmc->max_blk_count = mmc->max_req_size >> 11;

	spin_lock_init(&host->lock);

	writel(0, host->base + MMCIMASK0);
	writel(0, host->base + MMCIMASK1);
	writel(0xfff, host->base + MMCICLEAR);
	
#ifdef SD_HW_NODETECTION
		if (host->mmc->index==1 && sd_detection_cmd_dev == NULL) {
			sd_detection_cmd_dev = device_create(sec_class,
				NULL, 0, NULL, "sdcard");
			if (IS_ERR(sd_detection_cmd_dev))
				pr_err("Failed to create sdcard sysfs dev\n");

			if (device_create_file(sd_detection_cmd_dev,
					&dev_attr_status) < 0)
				pr_err("Fail to create sysfs sdcard/status sysfs file\n");

			dev_set_drvdata(sd_detection_cmd_dev, host);
		}
#endif

	if (gpio_is_valid(plat->gpio_cd)) {
		ret = gpio_request(plat->gpio_cd, DRIVER_NAME " (cd)");
		if (ret == 0)
			ret = gpio_direction_input(plat->gpio_cd);
		if (ret == 0)
			host->gpio_cd = plat->gpio_cd;
		else if (ret != -ENOSYS)
			goto err_gpio_cd;

		/*
		 * A gpio pin that will detect cards when inserted and removed
		 * will most likely want to trigger on the edges if it is
		 * 0 when ejected and 1 when inserted (or mutatis mutandis
		 * for the inverted case) so we request triggers on both
		 * edges.
		 */
		ret = request_any_context_irq(gpio_to_irq(plat->gpio_cd),
				mmci_cd_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				DRIVER_NAME " (cd)", host);
		if (ret >= 0)
			host->gpio_cd_irq = gpio_to_irq(plat->gpio_cd);

		if (sd_detection_cmd_dev == NULL) {
			/*create sysfs file for detect pin*/
			sd_detection_cmd_dev = device_create(sec_class,
				NULL, 0, NULL, "sdcard");
			if (IS_ERR(sd_detection_cmd_dev))
				pr_err("Failed to create sdcard sysfs dev\n");

			if (device_create_file(sd_detection_cmd_dev,
					&dev_attr_status) < 0)
				pr_err("Fail to create sysfs sdcard/status sysfs file\n");

			dev_set_drvdata(sd_detection_cmd_dev, host);
		}
	}
#ifndef CONFIG_STE_WLAN
	else if(plat->register_status_notify){
		plat->register_status_notify(mmci_status_notify_cb, host);
	}
#endif
	if (gpio_is_valid(plat->gpio_wp)) {
		ret = gpio_request(plat->gpio_wp, DRIVER_NAME " (wp)");
		if (ret == 0)
			ret = gpio_direction_input(plat->gpio_wp);
		if (ret == 0)
			host->gpio_wp = plat->gpio_wp;
		else if (ret != -ENOSYS)
			goto err_gpio_wp;
	}

	if ((host->plat->status || host->gpio_cd != -ENOSYS)
		&& host->gpio_cd_irq < 0
		&& !(mmc->caps & MMC_CAP_NONREMOVABLE)){

		if(mmc->index == 2)
			printk("WLAN is not setting MMC_CAP_NEEDS_POLL\n");
		else
		mmc->caps |= MMC_CAP_NEEDS_POLL;
	}

	ret = request_irq(dev->irq[0], mmci_irq, IRQF_SHARED, DRIVER_NAME " (cmd)", host);
	if (ret)
		goto unmap;

	if (dev->irq[1] == NO_IRQ)
		host->singleirq = true;
	else {
		ret = request_irq(dev->irq[1], mmci_pio_irq, IRQF_SHARED,
				  DRIVER_NAME " (pio)", host);
		if (ret)
			goto irq0_free;
	}

	writel(MCI_IRQENABLE, host->base + MMCIMASK0);

	amba_set_drvdata(dev, mmc);

	dev_info(&dev->dev, "%s: PL%03x manf %x rev%u at 0x%08llx irq %d,%d (pio)\n",
		 mmc_hostname(mmc), amba_part(dev), amba_manf(dev),
		 amba_rev(dev), (unsigned long long)dev->res.start,
		 dev->irq[0], dev->irq[1]);

	mmci_dma_setup(host);

	pm_runtime_set_autosuspend_delay(&dev->dev, 50);
	pm_runtime_use_autosuspend(&dev->dev);
	pm_runtime_put(&dev->dev);

	mmc_add_host(mmc);

	return 0;

 irq0_free:
	free_irq(dev->irq[0], host);
 unmap:
	if (host->gpio_wp != -ENOSYS)
		gpio_free(host->gpio_wp);
 err_gpio_wp:
	if (host->gpio_cd_irq >= 0)
		free_irq(host->gpio_cd_irq, host);
	if (host->gpio_cd != -ENOSYS)
		gpio_free(host->gpio_cd);
 err_gpio_cd:
	iounmap(host->base);
 clk_disable:
	clk_disable(host->clk);
 clk_free:
	clk_put(host->clk);
 host_free:
	mmc_free_host(mmc);
 rel_regions:
	amba_release_regions(dev);
 out:
	return ret;
}

static int __devexit mmci_remove(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		/*
		 * Undo pm_runtime_put() in probe.  We use the _sync
		 * version here so that we can access the primecell.
		 */
		pm_runtime_get_sync(&dev->dev);

		mmc_remove_host(mmc);

		writel(0, host->base + MMCIMASK0);
		writel(0, host->base + MMCIMASK1);

		writel(0, host->base + MMCICOMMAND);
		writel(0, host->base + MMCIDATACTRL);

		mmci_dma_release(host);
		free_irq(dev->irq[0], host);
		if (!host->singleirq)
			free_irq(dev->irq[1], host);

		if (host->gpio_wp != -ENOSYS)
			gpio_free(host->gpio_wp);
		if (host->gpio_cd_irq >= 0)
			free_irq(host->gpio_cd_irq, host);
		if (host->gpio_cd != -ENOSYS)
			gpio_free(host->gpio_cd);

		iounmap(host->base);
		clk_disable(host->clk);
		clk_put(host->clk);

		if (host->vcc)
			mmc_regulator_set_ocr(mmc, host->vcc, 0);
		regulator_put(host->vcc);

		mmc_free_host(mmc);

		amba_release_regions(dev);
	}

	return 0;
}

#if defined(CONFIG_SUSPEND) || defined(CONFIG_PM_RUNTIME)
static int mmci_save(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);
	unsigned long flags;
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		/* Let the ios_handler do power save actions. */
		if (host->plat->ios_handler) {
			ret = host->plat->ios_handler(mmc_dev(mmc),
						      &mmc->ios,
						      RPM_SUSPENDING);
			if (ret)
				return ret;
		}

		spin_lock_irqsave(&host->lock, flags);

		/*
		 * Make sure we do not get any interrupts when we disabled the
		 * clock and the regulator and as well make sure to clear the
		 * registers for clock and power.
		 */
		writel(0, host->base + MMCIMASK0);
		writel(0, host->base + MMCIPOWER);
		writel(0, host->base + MMCICLOCK);
		writel(0, host->base + MMCIDATACTRL);

		spin_unlock_irqrestore(&host->lock, flags);

		clk_disable(host->clk);
		amba_vcore_disable(dev);
	}

	return ret;
}

static int mmci_restore(struct amba_device *dev)
{
	struct mmc_host *mmc = amba_get_drvdata(dev);
	unsigned long flags;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);

		amba_vcore_enable(dev);
		clk_enable(host->clk);

		spin_lock_irqsave(&host->lock, flags);

		/* Restore registers and re-enable interrupts. */
		writel(host->datactrl_reg, host->base + MMCIDATACTRL);
		writel(host->clk_reg, host->base + MMCICLOCK);
		writel(host->pwr_reg, host->base + MMCIPOWER);
		writel(MCI_IRQENABLE, host->base + MMCIMASK0);

		if (host->dma_was_disabled) {
			mmci_dma_setup(host);
			host->dma_was_disabled = 0;
		}

		spin_unlock_irqrestore(&host->lock, flags);

		/* Restore power save settings done by the ios_handler. */
		if (host->plat->ios_handler)
			host->plat->ios_handler(mmc_dev(mmc),
						&mmc->ios,
						RPM_RESUMING);
	}

	return 0;
}
#endif

#ifdef CONFIG_SUSPEND
static int mmci_suspend(struct device *dev)
{
	struct amba_device *adev = to_amba_device(dev);
	struct mmc_host *mmc = amba_get_drvdata(adev);
	int ret = 0;

	if (mmc) {
#ifndef CONFIG_STE_WLAN
		if(mmc->index == 2)
			mmc->pm_flags |= MMC_PM_KEEP_POWER;
#endif
		ret = mmc_suspend_host(mmc);
		if (ret == 0) {
			pm_runtime_get_sync(dev);
			mmci_save(adev);
			amba_pclk_disable(adev);
		}
	}

	return ret;
}

static int mmci_resume(struct device *dev)
{
	struct amba_device *adev = to_amba_device(dev);
	struct mmc_host *mmc = amba_get_drvdata(adev);
	int ret = 0;

	if (mmc) {
		amba_pclk_enable(adev);
		mmci_restore(adev);
		pm_runtime_put(dev);

		ret = mmc_resume_host(mmc);
	}

	return ret;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int mmci_runtime_suspend(struct device *dev)
{
	struct amba_device *adev = to_amba_device(dev);
	struct mmc_host *mmc = amba_get_drvdata(adev);
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);
		struct variant_data *variant = host->variant;
		if (!variant->pwrreg_ctrl_power)
			ret = mmci_save(adev);
	}

	return ret;
}

static int mmci_runtime_resume(struct device *dev)
{
	struct amba_device *adev = to_amba_device(dev);
	struct mmc_host *mmc = amba_get_drvdata(adev);
	int ret = 0;

	if (mmc) {
		struct mmci_host *host = mmc_priv(mmc);
		struct variant_data *variant = host->variant;
		if (!variant->pwrreg_ctrl_power)
			ret = mmci_restore(adev);
	}

	return ret;
}
#endif

static const struct dev_pm_ops mmci_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mmci_suspend, mmci_resume)
	SET_RUNTIME_PM_OPS(mmci_runtime_suspend, mmci_runtime_resume, NULL)
};

static struct amba_id mmci_ids[] = {
	{
		.id	= 0x00041180,
		.mask	= 0xff0fffff,
		.data	= &variant_arm,
	},
	{
		.id	= 0x01041180,
		.mask	= 0xff0fffff,
		.data	= &variant_arm_extended_fifo,
	},
	{
		.id	= 0x00041181,
		.mask	= 0x000fffff,
		.data	= &variant_arm,
	},
	/* ST Micro variants */
	{
		.id     = 0x00180180,
		.mask   = 0x00ffffff,
		.data	= &variant_u300,
	},
	{
		.id     = 0x00280180,
		.mask   = 0x00ffffff,
		.data	= &variant_u300,
	},
	{
		.id     = 0x00480180,
		.mask   = 0xf0ffffff,
		.data	= &variant_ux500,
	},
	{
		.id     = 0x10480180,
		.mask   = 0xf0ffffff,
		.data	= &variant_ux500v2,
	},
	{ 0, 0 },
};

static struct amba_driver mmci_driver = {
	.drv		= {
		.name	= DRIVER_NAME,
		.pm	= &mmci_dev_pm_ops,
	},
	.probe		= mmci_probe,
	.remove		= __devexit_p(mmci_remove),
	.id_table	= mmci_ids,
};

static int __init mmci_init(void)
{
	return amba_driver_register(&mmci_driver);
}

static void __exit mmci_exit(void)
{
	amba_driver_unregister(&mmci_driver);
}

module_init(mmci_init);
module_exit(mmci_exit);
module_param(fmax, uint, 0444);

MODULE_DESCRIPTION("ARM PrimeCell PL180/181 Multimedia Card Interface driver");
MODULE_LICENSE("GPL");
