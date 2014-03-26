/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>,
 *         Sandeep Kaushik <sandeep.kaushik@st.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <mach/hardware.h>
#include <mach/msp.h>

#include <sound/soc.h>

#include "ux500_msp_i2s.h"

static int
ux500_msp_i2s_enable(struct msp *msp, struct msp_config *config);

 /* Protocol desciptors */
static const struct msp_protocol_desc prot_descs[] = {
	I2S_PROTOCOL_DESC,
	PCM_PROTOCOL_DESC,
	PCM_COMPAND_PROTOCOL_DESC,
	AC97_PROTOCOL_DESC,
	SPI_MASTER_PROTOCOL_DESC,
	SPI_SLAVE_PROTOCOL_DESC,
};

static void ux500_msp_i2s_set_prot_desc_tx(struct msp *msp,
					struct msp_protocol_desc *protocol_desc,
					enum msp_data_size data_size)
{
	u32 temp_reg = 0;

	temp_reg |= MSP_P2_ENABLE_BIT(protocol_desc->tx_phase_mode);
	temp_reg |= MSP_P2_START_MODE_BIT(protocol_desc->tx_phase2_start_mode);
	temp_reg |= MSP_P1_FRAME_LEN_BITS(protocol_desc->tx_frame_length_1);
	temp_reg |= MSP_P2_FRAME_LEN_BITS(protocol_desc->tx_frame_length_2);
	if (msp->def_elem_len) {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(protocol_desc->tx_element_length_1);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(protocol_desc->tx_element_length_2);
		if (protocol_desc->tx_element_length_1 ==
			protocol_desc->tx_element_length_2) {
			msp->actual_data_size = protocol_desc->tx_element_length_1;
		} else {
			msp->actual_data_size = data_size;
		}
	} else {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(data_size);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(data_size);
		msp->actual_data_size = data_size;
	}
	temp_reg |= MSP_DATA_DELAY_BITS(protocol_desc->tx_data_delay);
	temp_reg |= MSP_SET_ENDIANNES_BIT(protocol_desc->tx_bit_transfer_format);
	temp_reg |= MSP_FRAME_SYNC_POL(protocol_desc->tx_frame_sync_pol);
	temp_reg |= MSP_DATA_WORD_SWAP(protocol_desc->tx_half_word_swap);
	temp_reg |= MSP_SET_COMPANDING_MODE(protocol_desc->compression_mode);
	temp_reg |= MSP_SET_FRAME_SYNC_IGNORE(protocol_desc->frame_sync_ignore);

	writel(temp_reg, msp->registers + MSP_TCF);
}

static void ux500_msp_i2s_set_prot_desc_rx(struct msp *msp,
					struct msp_protocol_desc *protocol_desc,
					enum msp_data_size data_size)
{
	u32 temp_reg = 0;

	temp_reg |= MSP_P2_ENABLE_BIT(protocol_desc->rx_phase_mode);
	temp_reg |= MSP_P2_START_MODE_BIT(protocol_desc->rx_phase2_start_mode);
	temp_reg |= MSP_P1_FRAME_LEN_BITS(protocol_desc->rx_frame_length_1);
	temp_reg |= MSP_P2_FRAME_LEN_BITS(protocol_desc->rx_frame_length_2);
	if (msp->def_elem_len) {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(protocol_desc->rx_element_length_1);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(protocol_desc->rx_element_length_2);
		if (protocol_desc->rx_element_length_1 ==
			protocol_desc->rx_element_length_2) {
			msp->actual_data_size = protocol_desc->rx_element_length_1;
		} else {
			msp->actual_data_size = data_size;
		}
	} else {
		temp_reg |= MSP_P1_ELEM_LEN_BITS(data_size);
		temp_reg |= MSP_P2_ELEM_LEN_BITS(data_size);
		msp->actual_data_size = data_size;
	}

	temp_reg |= MSP_DATA_DELAY_BITS(protocol_desc->rx_data_delay);
	temp_reg |= MSP_SET_ENDIANNES_BIT(protocol_desc->rx_bit_transfer_format);
	temp_reg |= MSP_FRAME_SYNC_POL(protocol_desc->rx_frame_sync_pol);
	temp_reg |= MSP_DATA_WORD_SWAP(protocol_desc->rx_half_word_swap);
	temp_reg |= MSP_SET_COMPANDING_MODE(protocol_desc->expansion_mode);
	temp_reg |= MSP_SET_FRAME_SYNC_IGNORE(protocol_desc->frame_sync_ignore);

	writel(temp_reg, msp->registers + MSP_RCF);

}

static int ux500_msp_i2s_configure_protocol(struct msp *msp,
			      struct msp_config *config)
{
	int direction;
	struct msp_protocol_desc *protocol_desc;
	enum msp_data_size data_size;
	u32 temp_reg = 0;

	data_size = config->data_size;
	msp->def_elem_len = config->def_elem_len;
	direction = config->direction;
	if (config->default_protocol_desc == 1) {
		if (config->protocol >= MSP_INVALID_PROTOCOL) {
			pr_err("%s: ERROR: Invalid protocol!\n", __func__);
			return -EINVAL;
		}
		protocol_desc =
		    (struct msp_protocol_desc *)&prot_descs[config->protocol];
	} else {
		protocol_desc = (struct msp_protocol_desc *)&config->protocol_desc;
	}

	if (data_size < MSP_DATA_BITS_DEFAULT || data_size > MSP_DATA_BITS_32) {
		pr_err("%s: ERROR: Invalid data-size requested (data_size = %d)!\n",
			__func__, data_size);
		return -EINVAL;
	}

	switch (direction) {
	case MSP_TRANSMIT_MODE:
		ux500_msp_i2s_set_prot_desc_tx(msp, protocol_desc, data_size);
		break;
	case MSP_RECEIVE_MODE:
		ux500_msp_i2s_set_prot_desc_rx(msp, protocol_desc, data_size);
		break;
	case MSP_BOTH_T_R_MODE:
		ux500_msp_i2s_set_prot_desc_tx(msp, protocol_desc, data_size);
		ux500_msp_i2s_set_prot_desc_rx(msp, protocol_desc, data_size);
		break;
	default:
		pr_err("%s: ERROR: Invalid direction requested (direction = %d)!\n",
			__func__, direction);
		return -EINVAL;
	}

	/* The below code is needed for both Rx and Tx path. Can't separate them. */
	temp_reg = readl(msp->registers + MSP_GCR) & ~TX_CLK_POL_RISING;
	temp_reg |= MSP_TX_CLKPOL_BIT(~protocol_desc->tx_clock_pol);
	writel(temp_reg, msp->registers + MSP_GCR);
	temp_reg = readl(msp->registers + MSP_GCR) & ~RX_CLK_POL_RISING;
	temp_reg |= MSP_RX_CLKPOL_BIT(protocol_desc->rx_clock_pol);
	writel(temp_reg, msp->registers + MSP_GCR);

	return 0;
}

static int ux500_msp_i2s_configure_clock(struct msp *msp, struct msp_config *config)
{
	u32 reg_val_GCR;
	u32 frame_per = 0;
	u32 sck_div = 0;
	u32 frame_width = 0;
	u32 temp_reg = 0;
	u32 bit_clock = 0;
	struct msp_protocol_desc *protocol_desc = NULL;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR & ~SRG_ENABLE, msp->registers + MSP_GCR);

	if (config->default_protocol_desc)
		protocol_desc =
			(struct msp_protocol_desc *)&prot_descs[config->protocol];
	else
		protocol_desc = (struct msp_protocol_desc *)&config->protocol_desc;

	switch (config->protocol) {
	case MSP_PCM_PROTOCOL:
	case MSP_PCM_COMPAND_PROTOCOL:
		frame_width = protocol_desc->frame_width;
		sck_div = config->input_clock_freq / (config->frame_freq *
			(protocol_desc->total_clocks_for_one_frame));
		frame_per = protocol_desc->frame_period;
		break;
	case MSP_I2S_PROTOCOL:
		frame_width = protocol_desc->frame_width;
		sck_div = config->input_clock_freq / (config->frame_freq *
			(protocol_desc->total_clocks_for_one_frame));
		frame_per = protocol_desc->frame_period;
		break;
	case MSP_AC97_PROTOCOL:
		/* Not supported */
		pr_err("%s: ERROR: AC97 protocol not supported!\n", __func__);
		return -ENOSYS;
	default:
		pr_err("%s: ERROR: Unknown protocol (%d)!\n",
			__func__,
			config->protocol);
		return -EINVAL;
	}

	temp_reg = (sck_div - 1) & SCK_DIV_MASK;
	temp_reg |= FRAME_WIDTH_BITS(frame_width);
	temp_reg |= FRAME_PERIOD_BITS(frame_per);
	writel(temp_reg, msp->registers + MSP_SRG);

	bit_clock = (config->input_clock_freq)/(sck_div + 1);
	/* If the bit clock is higher than 19.2MHz, Vape should be run in 100% OPP
	 * Only consider OPP 100% when bit-clock is used, i.e. MSP master mode
	 */
	if ((bit_clock > 19200000) && ((config->tx_clock_sel != 0) || (config->rx_clock_sel != 0))) {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "ux500-msp-i2s", 100);
		msp->vape_opp_constraint = 1;
	} else {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "ux500-msp-i2s", 50);
		msp->vape_opp_constraint = 0;
	}

	/* Enable clock */
	udelay(100);
	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | SRG_ENABLE, msp->registers + MSP_GCR);
	udelay(100);

	return 0;
}

static int ux500_msp_i2s_configure_multichannel(struct msp *msp, struct msp_config *config)
{
	struct msp_protocol_desc *protocol_desc;
	struct msp_multichannel_config *mcfg;
	u32 reg_val_MCR;

	if (config->default_protocol_desc == 1) {
		if (config->protocol >= MSP_INVALID_PROTOCOL) {
			pr_err("%s: ERROR: Invalid protocol (%d)!\n",
				__func__,
				config->protocol);
			return -EINVAL;
		}
		protocol_desc = (struct msp_protocol_desc *)
				&prot_descs[config->protocol];
	} else {
		protocol_desc = (struct msp_protocol_desc *)&config->protocol_desc;
	}

	mcfg = &config->multichannel_config;
	if (mcfg->tx_multichannel_enable) {
		if (protocol_desc->tx_phase_mode == MSP_SINGLE_PHASE) {
			reg_val_MCR = readl(msp->registers + MSP_MCR);
			writel(reg_val_MCR |
				(mcfg->tx_multichannel_enable ? 1 << TMCEN_BIT : 0),
				msp->registers + MSP_MCR);
			writel(mcfg->tx_channel_0_enable,
					msp->registers + MSP_TCE0);
			writel(mcfg->tx_channel_1_enable,
					msp->registers + MSP_TCE1);
			writel(mcfg->tx_channel_2_enable,
					msp->registers + MSP_TCE2);
			writel(mcfg->tx_channel_3_enable,
					msp->registers + MSP_TCE3);
		} else {
			pr_err("%s: ERROR: Only single-phase supported (TX-mode: %d)!\n",
				__func__, protocol_desc->tx_phase_mode);
			return -EINVAL;
		}
	}
	if (mcfg->rx_multichannel_enable) {
		if (protocol_desc->rx_phase_mode == MSP_SINGLE_PHASE) {
			reg_val_MCR = readl(msp->registers + MSP_MCR);
			writel(reg_val_MCR |
				(mcfg->rx_multichannel_enable ? 1 << RMCEN_BIT : 0),
				msp->registers + MSP_MCR);
			writel(mcfg->rx_channel_0_enable,
					msp->registers + MSP_RCE0);
			writel(mcfg->rx_channel_1_enable,
					msp->registers + MSP_RCE1);
			writel(mcfg->rx_channel_2_enable,
					msp->registers + MSP_RCE2);
			writel(mcfg->rx_channel_3_enable,
					msp->registers + MSP_RCE3);
		} else {
			pr_err("%s: ERROR: Only single-phase supported (RX-mode: %d)!\n",
				__func__, protocol_desc->rx_phase_mode);
			return -EINVAL;
		}
		if (mcfg->rx_comparison_enable_mode) {
			reg_val_MCR = readl(msp->registers + MSP_MCR);
			writel(reg_val_MCR |
					(mcfg->rx_comparison_enable_mode << RCMPM_BIT),
					msp->registers + MSP_MCR);

			writel(mcfg->comparison_mask,
					msp->registers + MSP_RCM);
			writel(mcfg->comparison_value,
					msp->registers + MSP_RCV);

		}
	}

	return 0;
}

static int ux500_msp_i2s_enable(struct msp *msp, struct msp_config *config)
{
	int status = 0;
	u32 reg_val_DMACR, reg_val_GCR;

	if (config->work_mode != MSP_DMA_MODE) {
		pr_err("%s: ERROR: Only DMA-mode is supported (msp->work_mode = %d)\n",
			__func__,
			msp->work_mode);
		return -EINVAL;
	}
	msp->work_mode = config->work_mode;

	/* Check msp state whether in RUN or CONFIGURED Mode */
	if (msp->msp_state == MSP_STATE_IDLE) {
		if (msp->plat_init) {
			status = msp->plat_init();
			if (status) {
				pr_err("%s: ERROR: Failed to init MSP (%d)!\n",
					__func__,
					status);
				return status;
			}
		}
	}

	/* Configure msp with protocol dependent settings */
	ux500_msp_i2s_configure_protocol(msp, config);
	ux500_msp_i2s_configure_clock(msp, config);
	if (config->multichannel_configured == 1) {
		status = ux500_msp_i2s_configure_multichannel(msp, config);
		if (status)
			pr_warn("%s: WARN: ux500_msp_i2s_configure_multichannel failed (%d)!\n",
				__func__, status);
	}

	/* Make sure the correct DMA-directions are configured */
	if ((config->direction == MSP_RECEIVE_MODE) ||
		(config->direction == MSP_BOTH_T_R_MODE))
		if (!msp->dma_cfg_rx) {
			pr_err("%s: ERROR: MSP RX-mode is not configured!", __func__);
			return -EINVAL;
		}
	if ((config->direction == MSP_TRANSMIT_MODE) ||
		(config->direction == MSP_BOTH_T_R_MODE))
		if (!msp->dma_cfg_tx) {
			pr_err("%s: ERROR: MSP TX-mode is not configured!", __func__);
			return -EINVAL;
		}

	reg_val_DMACR = readl(msp->registers + MSP_DMACR);
	switch (config->direction) {
	case MSP_TRANSMIT_MODE:
		writel(reg_val_DMACR | TX_DMA_ENABLE,
				msp->registers + MSP_DMACR);

		break;
	case MSP_RECEIVE_MODE:
		writel(reg_val_DMACR | RX_DMA_ENABLE,
				msp->registers + MSP_DMACR);
		break;
	case MSP_BOTH_T_R_MODE:
		writel(reg_val_DMACR | RX_DMA_ENABLE | TX_DMA_ENABLE,
				msp->registers + MSP_DMACR);
		break;
	default:
		pr_err("%s: ERROR: Illegal MSP direction (config->direction = %d)!",
			__func__,
			config->direction);
		if (msp->plat_exit)
			msp->plat_exit();
		return -EINVAL;
	}
	writel(config->iodelay, msp->registers + MSP_IODLY);

	/* Enable frame generation logic */
	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | FRAME_GEN_ENABLE, msp->registers + MSP_GCR);

	return status;
}

static void flush_fifo_rx(struct msp *msp)
{
	u32 reg_val_DR, reg_val_GCR, reg_val_FLR;
	u32 limit = 32;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | RX_ENABLE, msp->registers + MSP_GCR);

	reg_val_FLR = readl(msp->registers + MSP_FLR);
	while (!(reg_val_FLR & RX_FIFO_EMPTY) && limit--) {
		reg_val_DR = readl(msp->registers + MSP_DR);
		reg_val_FLR = readl(msp->registers + MSP_FLR);
	}

	writel(reg_val_GCR, msp->registers + MSP_GCR);
}

static void flush_fifo_tx(struct msp *msp)
{
	u32 reg_val_TSTDR, reg_val_GCR, reg_val_FLR;
	u32 limit = 32;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR | TX_ENABLE, msp->registers + MSP_GCR);
	writel(MSP_ITCR_ITEN | MSP_ITCR_TESTFIFO, msp->registers + MSP_ITCR);

	reg_val_FLR = readl(msp->registers + MSP_FLR);
	while (!(reg_val_FLR & TX_FIFO_EMPTY) && limit--) {
		reg_val_TSTDR = readl(msp->registers + MSP_TSTDR);
		reg_val_FLR = readl(msp->registers + MSP_FLR);
	}
	writel(0x0, msp->registers + MSP_ITCR);
	writel(reg_val_GCR, msp->registers + MSP_GCR);
}

int ux500_msp_i2s_open(struct ux500_msp_i2s_drvdata *drvdata, struct msp_config *msp_config)
{
	struct msp *msp = drvdata->msp;
	u32 old_reg, new_reg, mask;
	int res;

	if (in_interrupt()) {
		pr_err("%s: ERROR: Open called in interrupt context!\n", __func__);
		return -1;
	}

	/* Two simultanous configuring msp is avoidable */
	down(&msp->lock);

	/* Don't enable regulator if its msp1 or msp3 */
	if (!(msp->reg_enabled) && msp->id != MSP_1_I2S_CONTROLLER
				&& msp->id != MSP_3_I2S_CONTROLLER) {
		res = regulator_enable(drvdata->reg_vape);
		if (res != 0) {
			pr_err("%s: Failed to enable regulator!\n", __func__);
			up(&msp->lock);
			return res;
		}
		msp->reg_enabled = 1;
	}

	switch (msp->users) {
	case 0:
		clk_enable(msp->clk);
		msp->direction = msp_config->direction;
		break;
	case 1:
		if (msp->direction == MSP_BOTH_T_R_MODE ||
		    msp_config->direction == msp->direction ||
		    msp_config->direction == MSP_BOTH_T_R_MODE) {
			pr_warn("%s: WARN: MSP is in use (direction = %d)!\n",
				__func__, msp_config->direction);
			up(&msp->lock);
			return -EBUSY;
		}
		msp->direction = MSP_BOTH_T_R_MODE;
		break;
	default:
		pr_warn("%s: MSP in use in (both directions)!\n", __func__);
		up(&msp->lock);
		return -EBUSY;
	}
	msp->users++;

	/* First do the global config register */
	mask =
	    RX_CLK_SEL_MASK | TX_CLK_SEL_MASK | RX_FRAME_SYNC_MASK |
	    TX_FRAME_SYNC_MASK | RX_SYNC_SEL_MASK | TX_SYNC_SEL_MASK |
	    RX_FIFO_ENABLE_MASK | TX_FIFO_ENABLE_MASK | SRG_CLK_SEL_MASK |
	    LOOPBACK_MASK | TX_EXTRA_DELAY_MASK;

	new_reg = (msp_config->tx_clock_sel | msp_config->rx_clock_sel |
		msp_config->rx_frame_sync_pol | msp_config->tx_frame_sync_pol |
		msp_config->rx_frame_sync_sel | msp_config->tx_frame_sync_sel |
		msp_config->rx_fifo_config | msp_config->tx_fifo_config |
		msp_config->srg_clock_sel | msp_config->loopback_enable |
		msp_config->tx_data_enable);

	old_reg = readl(msp->registers + MSP_GCR);
	old_reg &= ~mask;
	new_reg |= old_reg;
	writel(new_reg, msp->registers + MSP_GCR);

	if (ux500_msp_i2s_enable(msp, msp_config) != 0) {
		pr_err("%s: ERROR: ux500_msp_i2s_enable failed!\n", __func__);
		return -EBUSY;
	}
	if (msp_config->loopback_enable & 0x80)
		msp->loopback_enable = 1;

	/* Flush FIFOs */
	flush_fifo_tx(msp);
	flush_fifo_rx(msp);

	msp->msp_state = MSP_STATE_CONFIGURED;
	up(&msp->lock);
	return 0;
}

static void ux500_msp_i2s_disable_rx(struct msp *msp)
{
	u32 reg_val_GCR, reg_val_DMACR, reg_val_IMSC;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR & ~RX_ENABLE, msp->registers + MSP_GCR);
	reg_val_DMACR = readl(msp->registers + MSP_DMACR);
	writel(reg_val_DMACR & ~RX_DMA_ENABLE, msp->registers + MSP_DMACR);
	reg_val_IMSC = readl(msp->registers + MSP_IMSC);
	writel(reg_val_IMSC &
			~(RECEIVE_SERVICE_INT | RECEIVE_OVERRUN_ERROR_INT),
			msp->registers + MSP_IMSC);
}

static void ux500_msp_i2s_disable_tx(struct msp *msp)
{
	u32 reg_val_GCR, reg_val_DMACR, reg_val_IMSC;

	reg_val_GCR = readl(msp->registers + MSP_GCR);
	writel(reg_val_GCR & ~TX_ENABLE, msp->registers + MSP_GCR);
	reg_val_DMACR = readl(msp->registers + MSP_DMACR);
	writel(reg_val_DMACR & ~TX_DMA_ENABLE, msp->registers + MSP_DMACR);
	reg_val_IMSC = readl(msp->registers + MSP_IMSC);
	writel(reg_val_IMSC &
			~(TRANSMIT_SERVICE_INT | TRANSMIT_UNDERRUN_ERR_INT),
			msp->registers + MSP_IMSC);
}

static int ux500_msp_i2s_disable(struct msp *msp, int direction, enum i2s_flag flag)
{
	u32 reg_val_GCR;
	int status = 0;

	reg_val_GCR = readl(msp->registers + MSP_GCR);

	if (flag == DISABLE_TRANSMIT)
		ux500_msp_i2s_disable_tx(msp);
	else if (flag == DISABLE_RECEIVE)
		ux500_msp_i2s_disable_rx(msp);
	else {
		reg_val_GCR = readl(msp->registers + MSP_GCR);
		writel(reg_val_GCR | LOOPBACK_MASK,
				msp->registers + MSP_GCR);

		/* Flush TX-FIFO */
		flush_fifo_tx(msp);

		/* Disable TX-channel */
		writel((readl(msp->registers + MSP_GCR) &
			       (~TX_ENABLE)), msp->registers + MSP_GCR);

		/* Flush RX-FIFO */
		flush_fifo_rx(msp);

		/* Disable Loopback and Receive channel */
		writel((readl(msp->registers + MSP_GCR) &
				(~(RX_ENABLE | LOOPBACK_MASK))),
				msp->registers + MSP_GCR);

		ux500_msp_i2s_disable_tx(msp);
		ux500_msp_i2s_disable_rx(msp);

	}

	/* disable sample rate and frame generators */
	if (flag == DISABLE_ALL) {
		msp->msp_state = MSP_STATE_IDLE;
		writel((readl(msp->registers + MSP_GCR) &
			       (~(FRAME_GEN_ENABLE | SRG_ENABLE))),
			      msp->registers + MSP_GCR);
		if (msp->plat_exit)
			status = msp->plat_exit();
			if (status)
				pr_warn("%s: WARN: ux500_msp_i2s_exit failed (%d)!\n",
					__func__, status);
		writel(0, msp->registers + MSP_GCR);
		writel(0, msp->registers + MSP_TCF);
		writel(0, msp->registers + MSP_RCF);
		writel(0, msp->registers + MSP_DMACR);
		writel(0, msp->registers + MSP_SRG);
		writel(0, msp->registers + MSP_MCR);
		writel(0, msp->registers + MSP_RCM);
		writel(0, msp->registers + MSP_RCV);
		writel(0, msp->registers + MSP_TCE0);
		writel(0, msp->registers + MSP_TCE1);
		writel(0, msp->registers + MSP_TCE2);
		writel(0, msp->registers + MSP_TCE3);
		writel(0, msp->registers + MSP_RCE0);
		writel(0, msp->registers + MSP_RCE1);
		writel(0, msp->registers + MSP_RCE2);
		writel(0, msp->registers + MSP_RCE3);
	}

	return status;
}

int
ux500_msp_i2s_trigger(
	struct ux500_msp_i2s_drvdata *drvdata,
	int cmd,
	int direction)
{
	struct msp *msp = drvdata->msp;
	u32 reg_val_GCR, enable_bit;

	if (msp->msp_state == MSP_STATE_IDLE) {
		pr_err("%s: ERROR: MSP is not configured!\n", __func__);
		return -EINVAL;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			enable_bit = TX_ENABLE;
		} else {
			enable_bit = RX_ENABLE;
		}
		reg_val_GCR = readl(msp->registers + MSP_GCR);
		writel(reg_val_GCR | enable_bit, msp->registers + MSP_GCR);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			ux500_msp_i2s_disable_tx(msp);
		} else {
			ux500_msp_i2s_disable_rx(msp);
		}
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

int ux500_msp_i2s_close(struct ux500_msp_i2s_drvdata *drvdata, enum i2s_flag flag)
{
	struct msp *msp = drvdata->msp;
	int status = 0;

	pr_debug("%s: Enter.\n", __func__);

	down(&msp->lock);

	if (msp->users == 0) {
		pr_err("%s: ERROR: MSP already closed!\n", __func__);
		status = -EINVAL;
		goto end;
	}
	pr_debug("%s: msp->users = %d, flag = %d\n", __func__, msp->users, flag);

	/* We need to call it twice for DISABLE_ALL*/
	msp->users = flag == DISABLE_ALL ? 0 : msp->users - 1;
	if (msp->users)
		status = ux500_msp_i2s_disable(msp, MSP_BOTH_T_R_MODE, flag);
	else {
		status = ux500_msp_i2s_disable(msp, MSP_BOTH_T_R_MODE, DISABLE_ALL);
		clk_disable(msp->clk);
		if (msp->reg_enabled) {
			status = regulator_disable(drvdata->reg_vape);
			msp->reg_enabled = 0;
		}
		if (status != 0) {
			pr_err("%s: ERROR: Failed to disable regulator (%d)!\n",
				__func__, status);
			clk_enable(msp->clk);
			goto end;
		}
	}
	if (status)
		goto end;
	if (msp->users)
		msp->direction = flag == DISABLE_TRANSMIT ?
			MSP_RECEIVE_MODE : MSP_TRANSMIT_MODE;

	if (msp->vape_opp_constraint == 1) {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP, "ux500_msp_i2s", 50);
		msp->vape_opp_constraint = 0;
	}
end:
	up(&msp->lock);
	return status;

}

struct ux500_msp_i2s_drvdata *ux500_msp_i2s_init(struct platform_device *pdev,
					struct msp_i2s_platform_data *platform_data)
{
	struct ux500_msp_i2s_drvdata *msp_i2s_drvdata;
	int irq;
	struct resource *res = NULL;
	struct i2s_controller *i2s_cont;
	struct msp *msp;

	pr_debug("%s: Enter (pdev->name = %s).\n", __func__, pdev->name);

	msp_i2s_drvdata = kzalloc(sizeof(struct ux500_msp_i2s_drvdata), GFP_KERNEL);
	msp_i2s_drvdata->msp = kzalloc(sizeof(struct msp), GFP_KERNEL);
	msp = msp_i2s_drvdata->msp;

	msp->id = platform_data->id;
	msp_i2s_drvdata->id = msp->id;
	pr_debug("msp_i2s_drvdata->id = %d\n", msp_i2s_drvdata->id);

	msp->plat_init = platform_data->msp_i2s_init;
	msp->plat_exit = platform_data->msp_i2s_exit;
	msp->dma_cfg_rx = platform_data->msp_i2s_dma_rx;
	msp->dma_cfg_tx = platform_data->msp_i2s_dma_tx;

	sema_init(&msp->lock, 1);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_err("%s: ERROR: Unable to get resource!\n", __func__);
		goto free_msp;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		goto free_msp;
	msp->irq = irq;

	msp->registers = ioremap(res->start, (res->end - res->start + 1));
	if (msp->registers == NULL)
		goto free_msp;

	msp_i2s_drvdata->reg_vape = regulator_get(NULL, "v-ape");
	if (IS_ERR(msp_i2s_drvdata->reg_vape)) {
		pr_err("%s: ERROR: Failed to get Vape supply (%d)!\n",
			__func__, (int)PTR_ERR(msp_i2s_drvdata->reg_vape));
		goto free_irq;
	}
	dev_set_drvdata(&pdev->dev, msp_i2s_drvdata);

	prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP, (char *)pdev->name,
				  PRCMU_QOS_DEFAULT_VALUE);
	msp->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(msp->clk)) {
		pr_err("%s: ERROR: clk_get failed (%d)!\n",
			__func__, (int)PTR_ERR(msp->clk));
		goto free_irq;
	}

	msp->msp_state = MSP_STATE_IDLE;
	msp->loopback_enable = 0;

	/* I2S Controller is allocated and added in I2S controller class. */
	i2s_cont = kzalloc(sizeof(*i2s_cont), GFP_KERNEL);
	if (!i2s_cont) {
		pr_err("%s: ERROR: Failed to allocate struct i2s_cont (kzalloc)!\n",
			__func__);
		goto del_clk;
	}
	i2s_cont->dev.parent = &pdev->dev;
	i2s_cont->data = (void *)msp;
	i2s_cont->id = (s16)msp->id;
	snprintf(i2s_cont->name,
		sizeof(i2s_cont->name),
		"ux500-msp-i2s.%04x",
		msp->id);
	pr_debug("I2S device-name :%s\n", i2s_cont->name);
	msp->i2s_cont = i2s_cont;

	return msp_i2s_drvdata;

del_clk:
	clk_put(msp->clk);
free_irq:
	iounmap(msp->registers);
free_msp:
	kfree(msp);
	return NULL;
}

int ux500_msp_i2s_exit(struct ux500_msp_i2s_drvdata *drvdata)
{
	struct msp *msp = drvdata->msp;
	int status = 0;

	pr_debug("%s: Enter (drvdata->id = %d).\n", __func__, drvdata->id);

	device_unregister(&msp->i2s_cont->dev);
	clk_put(msp->clk);
	iounmap(msp->registers);
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "ux500_msp_i2s");
	regulator_put(drvdata->reg_vape);
	kfree(msp);

	return status;
}

int ux500_msp_i2s_suspend(struct ux500_msp_i2s_drvdata *drvdata)
{
	struct msp *msp = drvdata->msp;

	pr_debug("%s: Enter (drvdata->id = %d).\n", __func__, drvdata->id);

	down(&msp->lock);
	if (msp->users > 0) {
		up(&msp->lock);
		return -EBUSY;
	}
	up(&msp->lock);

	return 0;
}

int ux500_msp_i2s_resume(struct ux500_msp_i2s_drvdata *drvdata)
{
	pr_debug("%s: Enter (drvdata->id = %d).\n", __func__, drvdata->id);

	return 0;
}

MODULE_LICENSE("GPLv2");
