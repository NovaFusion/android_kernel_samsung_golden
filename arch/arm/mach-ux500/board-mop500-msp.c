/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/gpio.h>

#include <plat/gpio-nomadik.h>
#include <plat/ste_dma40.h>
#include <plat/pincfg.h>

#include <mach/devices.h>
#include <mach/ste-dma40-db8500.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/msp.h>

#include "board-mop500.h"
#include "devices-db8500.h"
#include "pins-db8500.h"

/* MSP1/3 Tx/Rx usage protection */
static DEFINE_SPINLOCK(msp_rxtx_lock);

/* Reference Count */
static int msp_rxtx_ref;

static pin_cfg_t mop500_msp1_pins_init[] = {
		GPIO33_MSP1_TXD | PIN_OUTPUT_LOW   | PIN_SLPM_WAKEUP_DISABLE,
		GPIO34_MSP1_TFS | PIN_INPUT_NOPULL | PIN_SLPM_WAKEUP_DISABLE,
		GPIO35_MSP1_TCK | PIN_INPUT_NOPULL | PIN_SLPM_WAKEUP_DISABLE,
		GPIO36_MSP1_RXD | PIN_INPUT_NOPULL | PIN_SLPM_WAKEUP_DISABLE,
};

static pin_cfg_t mop500_msp1_pins_exit[] = {
		GPIO33_MSP1_TXD | PIN_OUTPUT_LOW   | PIN_SLPM_WAKEUP_ENABLE,
		GPIO34_MSP1_TFS | PIN_INPUT_NOPULL | PIN_SLPM_WAKEUP_ENABLE,
		GPIO35_MSP1_TCK | PIN_INPUT_NOPULL | PIN_SLPM_WAKEUP_ENABLE,
		GPIO36_MSP1_RXD | PIN_INPUT_NOPULL | PIN_SLPM_WAKEUP_ENABLE,
};

int msp13_i2s_init(void)
{
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&msp_rxtx_lock, flags);
	if (msp_rxtx_ref == 0)
		retval = nmk_config_pins(
				ARRAY_AND_SIZE(mop500_msp1_pins_init));
	if (!retval)
		msp_rxtx_ref++;
	spin_unlock_irqrestore(&msp_rxtx_lock, flags);

	return retval;
}

int msp13_i2s_exit(void)
{
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&msp_rxtx_lock, flags);
	WARN_ON(!msp_rxtx_ref);
	msp_rxtx_ref--;
	if (msp_rxtx_ref == 0)
		retval = nmk_config_pins_sleep(
				ARRAY_AND_SIZE(mop500_msp1_pins_exit));
	spin_unlock_irqrestore(&msp_rxtx_lock, flags);

	return retval;
}

static struct stedma40_chan_cfg msp0_dma_rx = {
	.high_priority = true,
	.dir = STEDMA40_PERIPH_TO_MEM,

	.src_dev_type = DB8500_DMA_DEV31_MSP0_RX_SLIM0_CH0_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,

	.src_info.psize = STEDMA40_PSIZE_LOG_4,
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,

	/* data_width is set during configuration */
};

static struct stedma40_chan_cfg msp0_dma_tx = {
	.high_priority = true,
	.dir = STEDMA40_MEM_TO_PERIPH,

	.src_dev_type = STEDMA40_DEV_DST_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV31_MSP0_TX_SLIM0_CH0_TX,

	.src_info.psize = STEDMA40_PSIZE_LOG_4,
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,

	/* data_width is set during configuration */
};

static struct msp_i2s_platform_data msp0_platform_data = {
	.id = MSP_0_I2S_CONTROLLER,
	.msp_i2s_dma_rx = &msp0_dma_rx,
	.msp_i2s_dma_tx = &msp0_dma_tx,
};

static struct stedma40_chan_cfg msp1_dma_rx = {
	.high_priority = true,
	.dir = STEDMA40_PERIPH_TO_MEM,

	.src_dev_type = DB8500_DMA_DEV30_MSP3_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,

	.src_info.psize = STEDMA40_PSIZE_LOG_4,
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,

	/* data_width is set during configuration */
};

static struct stedma40_chan_cfg msp1_dma_tx = {
	.high_priority = true,
	.dir = STEDMA40_MEM_TO_PERIPH,

	.src_dev_type = STEDMA40_DEV_DST_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV30_MSP1_TX,

	.src_info.psize = STEDMA40_PSIZE_LOG_4,
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,

	/* data_width is set during configuration */
};

static struct msp_i2s_platform_data msp1_platform_data = {
	.id = MSP_1_I2S_CONTROLLER,
	.msp_i2s_dma_rx = NULL,
	.msp_i2s_dma_tx = &msp1_dma_tx,
	.msp_i2s_init = msp13_i2s_init,
	.msp_i2s_exit = msp13_i2s_exit,
};

static struct stedma40_chan_cfg msp2_dma_rx = {
	.high_priority = true,
	.dir = STEDMA40_PERIPH_TO_MEM,

	.src_dev_type = DB8500_DMA_DEV14_MSP2_RX,
	.dst_dev_type = STEDMA40_DEV_DST_MEMORY,

	/* MSP2 DMA doesn't work with PSIZE == 4 on DB8500v2 */
	.src_info.psize = STEDMA40_PSIZE_LOG_1,
	.dst_info.psize = STEDMA40_PSIZE_LOG_1,

	/* data_width is set during configuration */
};

static struct stedma40_chan_cfg msp2_dma_tx = {
	.high_priority = true,
	.dir = STEDMA40_MEM_TO_PERIPH,

	.src_dev_type = STEDMA40_DEV_DST_MEMORY,
	.dst_dev_type = DB8500_DMA_DEV14_MSP2_TX,

	.src_info.psize = STEDMA40_PSIZE_LOG_4,
	.dst_info.psize = STEDMA40_PSIZE_LOG_4,

	.use_fixed_channel = true,
	.phy_channel = 1,

	/* data_width is set during configuration */
};

static struct msp_i2s_platform_data msp2_platform_data = {
	.id = MSP_2_I2S_CONTROLLER,
	.msp_i2s_dma_rx = &msp2_dma_rx,
	.msp_i2s_dma_tx = &msp2_dma_tx,
};

static struct msp_i2s_platform_data msp3_platform_data = {
	.id		= MSP_3_I2S_CONTROLLER,
	.msp_i2s_dma_rx	= &msp1_dma_rx,
	.msp_i2s_dma_tx	= NULL,
	.msp_i2s_init = msp13_i2s_init,
	.msp_i2s_exit = msp13_i2s_exit,
};

void __init mop500_msp_init(void)
{
	db8500_add_msp0_i2s(&msp0_platform_data);
	db8500_add_msp1_i2s(&msp1_platform_data);
	db8500_add_msp2_i2s(&msp2_platform_data);
	db8500_add_msp3_i2s(&msp3_platform_data);
}
