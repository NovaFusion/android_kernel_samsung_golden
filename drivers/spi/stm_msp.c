/*
 * drivers/spi/stm_msp.c
 *
 * Copyright (C) 2010 STMicroelectronics Pvt. Ltd.
 *
 * Author: Sachin Verma <sachin.verma@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/amba/bus.h>
#include <linux/spi/stm_msp.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>

/**
 * MSP Controller Register Offsets
 */
#define MSP_DR(r)			(r + 0x000)
#define MSP_GCR(r)			(r + 0x004)
#define MSP_TCF(r)			(r + 0x008)
#define MSP_RCF(r)			(r + 0x00C)
#define MSP_SRG(r)			(r + 0x010)
#define MSP_FLR(r)			(r + 0x014)
#define MSP_DMACR(r)			(r + 0x018)
#define MSP_IMSC(r)			(r + 0x020)
#define MSP_RIS(r)			(r + 0x024)
#define MSP_MIS(r)			(r + 0x028)
#define MSP_ICR(r)			(r + 0x02C)
#define MSP_MCR(r)			(r + 0x030)
#define MSP_RCV(r)			(r + 0x034)
#define MSP_RCM(r)			(r + 0x038)
#define MSP_TCE0(r)			(r + 0x040)
#define MSP_TCE1(r)			(r + 0x044)
#define MSP_TCE2(r)			(r + 0x048)
#define MSP_TCE3(r)			(r + 0x04C)
#define MSP_RCE0(r)			(r + 0x060)
#define MSP_RCE1(r)			(r + 0x064)
#define MSP_RCE2(r)			(r + 0x068)
#define MSP_RCE3(r)			(r + 0x06C)
#define MSP_PID0(r)			(r + 0xFE0)
#define MSP_PID1(r)			(r + 0xFE4)
#define MSP_PID2(r)			(r + 0xFE8)
#define MSP_PID3(r)			(r + 0xFEC)

/**
 * MSP Global Configuration Register - MSP_GCR
 */
#define MSP_GCR_MASK_RXEN		((u32)(0x1UL << 0))
#define MSP_GCR_MASK_RFFEN		((u32)(0x1UL << 1))
#define MSP_GCR_MASK_RFSPOL		((u32)(0x1UL << 2))
#define MSP_GCR_MASK_DCM		((u32)(0x1UL << 3))
#define MSP_GCR_MASK_RFSSEL		((u32)(0x1UL << 4))
#define MSP_GCR_MASK_RCKPOL		((u32)(0x1UL << 5))
#define MSP_GCR_MASK_RCKSEL		((u32)(0x1UL << 6))
#define MSP_GCR_MASK_LBM		((u32)(0x1UL << 7))
#define MSP_GCR_MASK_TXEN		((u32)(0x1UL << 8))
#define MSP_GCR_MASK_TFFEN		((u32)(0x1UL << 9))
#define MSP_GCR_MASK_TFSPOL		((u32)(0x1UL << 10))
#define MSP_GCR_MASK_TFSSEL		((u32)(0x3UL << 11))
#define MSP_GCR_MASK_TCKPOL		((u32)(0x1UL << 13))
#define MSP_GCR_MASK_TCKSEL		((u32)(0x1UL << 14))
#define MSP_GCR_MASK_TXDDL		((u32)(0x1UL << 15))
#define MSP_GCR_MASK_SGEN		((u32)(0x1UL << 16))
#define MSP_GCR_MASK_SCKPOL		((u32)(0x1UL << 17))
#define MSP_GCR_MASK_SCKSEL		((u32)(0x3UL << 18))
#define MSP_GCR_MASK_FGEN		((u32)(0x1UL << 20))
#define MSP_GCR_MASK_SPICKM		((u32)(0x3UL << 21))
#define MSP_GCR_MASK_SPIBME		((u32)(0x1UL << 23))

/**
 * MSP Transmit Configuration Register - MSP_TCF
 */
#define MSP_TCF_MASK_TP1ELEN		((u32)(0x7UL << 0))
#define MSP_TCF_MASK_TP1FLEN		((u32)(0x7FUL << 3))
#define MSP_TCF_MASK_TDTYP		((u32)(0x3UL << 10))
#define MSP_TCF_MASK_TENDN		((u32)(0x1UL << 12))
#define MSP_TCF_MASK_TDDLY		((u32)(0x3UL << 13))
#define MSP_TCF_MASK_TFSIG		((u32)(0x1UL << 15))
#define MSP_TCF_MASK_TP2ELEN		((u32)(0x7UL << 16))
#define MSP_TCF_MASK_TP2FLEN		((u32)(0x7FUL << 19))
#define MSP_TCF_MASK_TP2SM		((u32)(0x1UL << 26))
#define MSP_TCF_MASK_TP2EN		((u32)(0x1UL << 27))
#define MSP_TCF_MASK_TBSWAP		((u32)(0x3UL << 28))

/**
 * MSP Receive Configuration Register - MSP_RCF
 */
#define MSP_RCF_MASK_RP1ELEN		((u32)(0x7UL << 0))
#define MSP_RCF_MASK_RP1FLEN		((u32)(0x7FUL << 3))
#define MSP_RCF_MASK_RDTYP		((u32)(0x3UL << 10))
#define MSP_RCF_MASK_RENDN		((u32)(0x1UL << 12))
#define MSP_RCF_MASK_RDDLY		((u32)(0x3UL << 13))
#define MSP_RCF_MASK_RFSIG		((u32)(0x1UL << 15))
#define MSP_RCF_MASK_RP2ELEN		((u32)(0x7UL << 16))
#define MSP_RCF_MASK_RP2FLEN		((u32)(0x7FUL << 19))
#define MSP_RCF_MASK_RP2SM		((u32)(0x1UL << 26))
#define MSP_RCF_MASK_RP2EN		((u32)(0x1UL << 27))
#define MSP_RCF_MASK_RBSWAP		((u32)(0x3UL << 28))

/**
 * MSP Sample Rate Generator Register - MSP_SRG
 */
#define MSP_SRG_MASK_SCKDIV		((u32)(0x3FFUL << 0))
#define MSP_SRG_MASK_FRWID		((u32)(0x3FUL << 10))
#define MSP_SRG_MASK_FRPER		((u32)(0x1FFFUL << 16))

/**
 * MSP Flag Register - MSP_FLR
 */
#define MSP_FLR_MASK_RBUSY		((u32)(0x1UL << 0))
#define MSP_FLR_MASK_RFE		((u32)(0x1UL << 1))
#define MSP_FLR_MASK_RFU		((u32)(0x1UL << 2))
#define MSP_FLR_MASK_TBUSY		((u32)(0x1UL << 3))
#define MSP_FLR_MASK_TFE		((u32)(0x1UL << 4))
#define MSP_FLR_MASK_TFU		((u32)(0x1UL << 5))

/**
 * MSP DMA Control Register - MSP_DMACR
 */
#define MSP_DMACR_MASK_RDMAE		((u32)(0x1UL << 0))
#define MSP_DMACR_MASK_TDMAE		((u32)(0x1UL << 1))

/**
 * MSP Interrupt Mask Set/Clear Register - MSP_IMSC
 */
#define MSP_IMSC_MASK_RXIM		((u32)(0x1UL << 0))
#define MSP_IMSC_MASK_ROEIM		((u32)(0x1UL << 1))
#define MSP_IMSC_MASK_RSEIM		((u32)(0x1UL << 2))
#define MSP_IMSC_MASK_RFSIM		((u32)(0x1UL << 3))
#define MSP_IMSC_MASK_TXIM		((u32)(0x1UL << 4))
#define MSP_IMSC_MASK_TUEIM		((u32)(0x1UL << 5))
#define MSP_IMSC_MASK_TSEIM		((u32)(0x1UL << 6))
#define MSP_IMSC_MASK_TFSIM		((u32)(0x1UL << 7))
#define MSP_IMSC_MASK_RFOIM		((u32)(0x1UL << 8))
#define MSP_IMSC_MASK_TFOIM		((u32)(0x1UL << 9))

/**
 * MSP Raw Interrupt status Register - MSP_RIS
 */
#define MSP_RIS_MASK_RXRIS		((u32)(0x1UL << 0))
#define MSP_RIS_MASK_ROERIS		((u32)(0x1UL << 1))
#define MSP_RIS_MASK_RSERIS		((u32)(0x1UL << 2))
#define MSP_RIS_MASK_RFSRIS		((u32)(0x1UL << 3))
#define MSP_RIS_MASK_TXRIS		((u32)(0x1UL << 4))
#define MSP_RIS_MASK_TUERIS		((u32)(0x1UL << 5))
#define MSP_RIS_MASK_TSERIS		((u32)(0x1UL << 6))
#define MSP_RIS_MASK_TFSRIS		((u32)(0x1UL << 7))
#define MSP_RIS_MASK_RFORIS		((u32)(0x1UL << 8))
#define MSP_RIS_MASK_TFORIS		((u32)(0x1UL << 9))

/**
 * MSP Masked Interrupt status Register - MSP_MIS
 */
#define MSP_MIS_MASK_RXMIS		((u32)(0x1UL << 0))
#define MSP_MIS_MASK_ROEMIS		((u32)(0x1UL << 1))
#define MSP_MIS_MASK_RSEMIS		((u32)(0x1UL << 2))
#define MSP_MIS_MASK_RFSMIS		((u32)(0x1UL << 3))
#define MSP_MIS_MASK_TXMIS		((u32)(0x1UL << 4))
#define MSP_MIS_MASK_TUEMIS		((u32)(0x1UL << 5))
#define MSP_MIS_MASK_TSEMIS		((u32)(0x1UL << 6))
#define MSP_MIS_MASK_TFSMIS		((u32)(0x1UL << 7))
#define MSP_MIS_MASK_RFOMIS		((u32)(0x1UL << 8))
#define MSP_MIS_MASK_TFOMIS		((u32)(0x1UL << 9))

/**
 * MSP Interrupt Clear Register - MSP_ICR
 */
#define MSP_ICR_MASK_ROEIC		((u32)(0x1UL << 1))
#define MSP_ICR_MASK_RSEIC		((u32)(0x1UL << 2))
#define MSP_ICR_MASK_RFSIC		((u32)(0x1UL << 3))
#define MSP_ICR_MASK_TUEIC		((u32)(0x1UL << 5))
#define MSP_ICR_MASK_TSEIC		((u32)(0x1UL << 6))
#define MSP_ICR_MASK_TFSIC		((u32)(0x1UL << 7))

#define GEN_MASK_BITS(val, mask, sb)	((u32)((((u32)val) << (sb)) & (mask)))
#define MSP_WBITS(reg, val, mask, sb)	((reg) = (((reg) & ~(mask)) |\
					(((val) << (sb)) & (mask))))
#define DEFAULT_MSP_REG_DMACR		0x00000000
#define DEFAULT_MSP_REG_SRG		0x1FFF0000

#define DEFAULT_MSP_REG_GCR	( \
	GEN_MASK_BITS(MSP_RECEIVER_DISABLED, MSP_GCR_MASK_RXEN, 0)	|\
	GEN_MASK_BITS(MSP_RX_FIFO_ENABLED, MSP_GCR_MASK_RFFEN, 1)	|\
	GEN_MASK_BITS(MSP_LOOPBACK_DISABLED, MSP_GCR_MASK_LBM, 7)	|\
	GEN_MASK_BITS(MSP_TRANSMITTER_DISABLED, MSP_GCR_MASK_TXEN, 8)	|\
	GEN_MASK_BITS(MSP_TX_FIFO_ENABLED, MSP_GCR_MASK_TFFEN, 9)	|\
	GEN_MASK_BITS(MSP_TX_FRAME_SYNC_POL_LOW, MSP_GCR_MASK_TFSPOL, 10)|\
	GEN_MASK_BITS(MSP_TX_FRAME_SYNC_INT, MSP_GCR_MASK_TFSSEL, 11)	|\
	GEN_MASK_BITS(MSP_TX_CLOCK_POL_HIGH, MSP_GCR_MASK_TCKPOL, 13)	|\
	GEN_MASK_BITS(MSP_IS_SPI_MASTER, MSP_GCR_MASK_TCKSEL, 14)	|\
	GEN_MASK_BITS(MSP_TRANSMIT_DATA_WITHOUT_DELAY, MSP_GCR_MASK_TXDDL, 15)|\
	GEN_MASK_BITS(MSP_SAMPLE_RATE_GEN_ENABLE, MSP_GCR_MASK_SGEN, 16)|\
	GEN_MASK_BITS(MSP_CLOCK_INTERNAL, MSP_GCR_MASK_SCKSEL, 18)	|\
	GEN_MASK_BITS(MSP_FRAME_GEN_ENABLE, MSP_GCR_MASK_FGEN, 20)	|\
	GEN_MASK_BITS(MSP_SPI_PHASE_ZERO_CYCLE_DELAY, MSP_GCR_MASK_SPICKM, 21)|\
	GEN_MASK_BITS(SPI_BURST_MODE_DISABLE, MSP_GCR_MASK_SPIBME, 23)\
	)
#define DEFAULT_MSP_REG_RCF	( \
	GEN_MASK_BITS(MSP_DATA_BITS_32, MSP_RCF_MASK_RP1ELEN, 0) | \
	GEN_MASK_BITS(MSP_IGNORE_RX_FRAME_SYNC_PULSE, MSP_RCF_MASK_RFSIG, 15) |\
	GEN_MASK_BITS(MSP_RX_1BIT_DATA_DELAY, MSP_RCF_MASK_RDDLY, 13) | \
	GEN_MASK_BITS(MSP_RX_ENDIANESS_LSB, MSP_RCF_MASK_RENDN, 12)  \
	)

#define DEFAULT_MSP_REG_TCF	( \
	GEN_MASK_BITS(MSP_DATA_BITS_32, MSP_TCF_MASK_TP1ELEN, 0) | \
	GEN_MASK_BITS(MSP_IGNORE_TX_FRAME_SYNC_PULSE, MSP_TCF_MASK_TFSIG, 15) |\
	GEN_MASK_BITS(MSP_TX_1BIT_DATA_DELAY, MSP_TCF_MASK_TDDLY, 13) | \
	GEN_MASK_BITS(MSP_TX_ENDIANESS_LSB, MSP_TCF_MASK_TENDN, 12)  \
	)

/**
 * MSP Receiver/Transmitter states (enabled or disabled)
 */
#define MSP_RECEIVER_DISABLED		0
#define MSP_RECEIVER_ENABLED		1
#define MSP_TRANSMITTER_DISABLED	0
#define MSP_TRANSMITTER_ENABLED		1

/**
 * MSP Receiver/Transmitter FIFO constants
 */
#define MSP_LOOPBACK_DISABLED		0
#define MSP_LOOPBACK_ENABLED		1

#define MSP_TX_FIFO_DISABLED		0
#define MSP_TX_FIFO_ENABLED		1
#define MSP_TX_ENDIANESS_MSB		0
#define MSP_TX_ENDIANESS_LSB		1

#define MSP_RX_FIFO_DISABLED		0
#define MSP_RX_FIFO_ENABLED		1
#define MSP_RX_ENDIANESS_MSB		0
#define MSP_RX_ENDIANESS_LSB		1

#define MSP_TX_FRAME_SYNC_EXT		0x0
#define MSP_TX_FRAME_SYNC_INT		0x2
#define MSP_TX_FRAME_SYNC_INT_CFG	0x3

#define MSP_TX_FRAME_SYNC_POL_HIGH	0
#define MSP_TX_FRAME_SYNC_POL_LOW	1

#define MSP_HANDLE_RX_FRAME_SYNC_PULSE	0
#define MSP_IGNORE_RX_FRAME_SYNC_PULSE	1

#define MSP_RX_NO_DATA_DELAY		0x0
#define MSP_RX_1BIT_DATA_DELAY		0x1
#define MSP_RX_2BIT_DATA_DELAY		0x2
#define MSP_RX_3BIT_DATA_DELAY		0x3

#define MSP_HANDLE_TX_FRAME_SYNC_PULSE	0
#define MSP_IGNORE_TX_FRAME_SYNC_PULSE	1

#define MSP_TX_NO_DATA_DELAY		0x0
#define MSP_TX_1BIT_DATA_DELAY		0x1
#define MSP_TX_2BIT_DATA_DELAY		0x2
#define MSP_TX_3BIT_DATA_DELAY		0x3

#define MSP_TX_CLOCK_POL_LOW		0
#define MSP_TX_CLOCK_POL_HIGH		1

#define MSP_SPI_PHASE_ZERO_CYCLE_DELAY	0x2
#define MSP_SPI_PHASE_HALF_CYCLE_DELAY	0x3

#define MSP_IS_SPI_SLAVE		0
#define MSP_IS_SPI_MASTER		1

#define MSP_FRAME_GEN_DISABLE		0
#define MSP_FRAME_GEN_ENABLE		1

#define MSP_SAMPLE_RATE_GEN_DISABLE	0
#define MSP_SAMPLE_RATE_GEN_ENABLE	1

#define SPI_BURST_MODE_DISABLE		0
#define SPI_BURST_MODE_ENABLE		1

#define MSP_TRANSMIT_DATA_WITHOUT_DELAY	0
#define MSP_TRANSMIT_DATA_WITH_DELAY	1

#define MSP_CLOCK_INTERNAL		0x0 /* 48 MHz */

/* SRG is derived from MSPSCK pin but is resynchronized on MSPRFS
 * (Receive Frame Sync signal) */
#define MSP_CLOCK_EXTERNAL		0x2
#define MSP_CLOCK_EXTERNAL_RESYNC	0x3

#define DISABLE_ALL_MSP_INTERRUPTS	(0x0)
#define ENABLE_ALL_MSP_INTERRUPTS	(0x333)
#define CLEAR_ALL_MSP_INTERRUPTS	(0xEE)
#define DEFAULT_MSP_CLK			(48000000)
#define MAX_SCKDIV			(1023)

#define MSP_FIFO_DEPTH			8

/**
 * Queue State
 */
#define QUEUE_RUNNING			(0)
#define QUEUE_STOPPED			(1)

#define START_STATE			((void *)0)
#define RUNNING_STATE			((void *)1)
#define DONE_STATE			((void *)2)
#define ERROR_STATE			((void *)-1)

/* Default values */
#define SPI_DEFAULT_MAX_SPEED_HZ	48000
#define SPI_TRANSFER_TIMEOUT_MS		5000

/* CONTROLLER COMMANDS */
enum cntlr_commands {
	DISABLE_CONTROLLER = 0,
	ENABLE_CONTROLLER ,
	DISABLE_ALL_INTERRUPT ,
	ENABLE_ALL_INTERRUPT ,
	FLUSH_FIFO ,
	RESTORE_STATE ,
	LOAD_DEFAULT_CONFIG ,
	CLEAR_ALL_INTERRUPT,
};

struct stm_msp {
	struct amba_device		*adev;
	struct spi_master		*master;
	struct stm_msp_controller	*master_info;
	void __iomem			*regs;
	struct clk			*clk;
#ifdef CONFIG_SPI_WORKQUEUE
	struct workqueue_struct		*workqueue;
#endif
	struct work_struct		spi_work;
	spinlock_t			lock;
	struct list_head		queue;
	int				busy;
	int				run;
	struct tasklet_struct		pump_transfers;
	struct timer_list		spi_notify_timer;
	int				spi_io_error;
	struct spi_message		*cur_msg;
	struct spi_transfer		*cur_transfer;
	struct chip_data		*cur_chip;
	void				*tx;
	void				*tx_end;
	void				*rx;
	void				*rx_end;
	void				(*write)(struct stm_msp *stm_msp);
	void				(*read)(struct stm_msp *stm_msp);
	void				(*delay)(struct stm_msp *stm_msp);
};

/**
 * struct chip_data - To maintain runtime state of SPICntlr for each client chip
 * @ctr_regs: void pointer which is assigned a struct having regs of the cntlr.
 * @chip_id: Chip Id assigned to this client to identify it.
 * @n_bytes: how many bytes(power of 2) reqd for a given data width of client
 * @write: function to be used to write when doing xfer for this chip
 * @null_write: function to be used for dummy write for receiving data.
 * @read: function to be used to read when doing xfer for this chip
 * @null_read: function to be used to for dummy read while writting data.
 * @cs_control: chip select callback provided by chip
 * @xfer_type: polling/interrupt
 *
 * Runtime state of the SPI controller, maintained per chip,
 * This would be set according to the current message that would be served
 */
struct chip_data {
	void *ctr_regs;
	u32 chip_id;
	u8 n_bytes;
	void (*write) (struct stm_msp *stm_msp);
	void (*null_write) (struct stm_msp *stm_msp);
	void (*read) (struct stm_msp *stm_msp);
	void (*null_read) (struct stm_msp *stm_msp);
	void (*delay) (struct stm_msp *stm_msp);
	void (*cs_control) (u32 command);
	int xfer_type;
};

/**
 * struct msp_regs - Used to store MSP controller registry values
 *		used by the driver.
 * @gcr: global configuration register
 * @tcf: transmit configuration register
 * @rcf: receive configuration register
 * @srg: sample rate generator register
 * @dmacr: DMA configuration register
 */
struct msp_regs {
	u32 gcr;
	u32 tcf;
	u32 rcf;
	u32 srg;
	u32 dmacr;
};

/**
 * stm_msp_controller_cmd - To execute controller commands for MSP
 * @stm_msp: SPI driver private data structure
 * @cmd: Command which is to be executed on the controller
 */
static int stm_msp_controller_cmd(struct stm_msp *stm_msp, int cmd)
{
	int retval = 0;
	struct msp_regs *msp_regs = NULL;

	switch (cmd) {
	case DISABLE_CONTROLLER: {
			dev_dbg(&stm_msp->adev->dev,
				"Disabling MSP controller...\n");
			writel((readl(MSP_GCR(stm_msp->regs)) &
				(~(MSP_GCR_MASK_TXEN | MSP_GCR_MASK_RXEN))),
				MSP_GCR(stm_msp->regs));
			break;
		}
	case ENABLE_CONTROLLER: {
			dev_dbg(&stm_msp->adev->dev,
				"Enabling MSP controller...\n");
			writel((readl(MSP_GCR(stm_msp->regs)) |
				(MSP_GCR_MASK_TXEN | MSP_GCR_MASK_RXEN)),
				MSP_GCR(stm_msp->regs));
			break;
		}
	case DISABLE_ALL_INTERRUPT: {
			dev_dbg(&stm_msp->adev->dev,
				"Disabling all MSP interrupts...\n");
			writel(DISABLE_ALL_MSP_INTERRUPTS,
				MSP_IMSC(stm_msp->regs));
			break;
		}
	case ENABLE_ALL_INTERRUPT: {
			dev_dbg(&stm_msp->adev->dev,
				"Enabling all MSP interrupts...\n");
			writel(ENABLE_ALL_MSP_INTERRUPTS,
				MSP_IMSC(stm_msp->regs));
			break;
		}
	case CLEAR_ALL_INTERRUPT: {
			dev_dbg(&stm_msp->adev->dev,
				"Clearing all MSP interrupts...\n");
			writel(CLEAR_ALL_MSP_INTERRUPTS,
				MSP_ICR(stm_msp->regs));
			break;
		}
	case FLUSH_FIFO: {
			unsigned long limit = loops_per_jiffy << 1;

			dev_dbg(&stm_msp->adev->dev, "MSP FIFO flushed\n");

			do {
				while (!(readl(MSP_FLR(stm_msp->regs)) &
					MSP_FLR_MASK_RFE)) {
					readl(MSP_DR(stm_msp->regs));
				}
			} while ((readl(MSP_FLR(stm_msp->regs)) &
				(MSP_FLR_MASK_TBUSY | MSP_FLR_MASK_RBUSY)) &&
				limit--);

			retval = limit;
			break;
		}
	case RESTORE_STATE: {
			msp_regs =
				(struct msp_regs *)stm_msp->cur_chip->ctr_regs;

			dev_dbg(&stm_msp->adev->dev,
				"Restoring MSP state...\n");

			writel(msp_regs->gcr, MSP_GCR(stm_msp->regs));
			writel(msp_regs->tcf, MSP_TCF(stm_msp->regs));
			writel(msp_regs->rcf, MSP_RCF(stm_msp->regs));
			writel(msp_regs->srg, MSP_SRG(stm_msp->regs));
			writel(msp_regs->dmacr, MSP_DMACR(stm_msp->regs));
			writel(DISABLE_ALL_MSP_INTERRUPTS,
			       MSP_IMSC(stm_msp->regs));
			writel(CLEAR_ALL_MSP_INTERRUPTS,
			       MSP_ICR(stm_msp->regs));
			break;
		}
	case LOAD_DEFAULT_CONFIG: {
			dev_dbg(&stm_msp->adev->dev,
				"Loading default MSP config...\n");

			writel(DEFAULT_MSP_REG_GCR, MSP_GCR(stm_msp->regs));
			writel(DEFAULT_MSP_REG_TCF, MSP_TCF(stm_msp->regs));
			writel(DEFAULT_MSP_REG_RCF, MSP_RCF(stm_msp->regs));
			writel(DEFAULT_MSP_REG_SRG, MSP_SRG(stm_msp->regs));
			writel(DEFAULT_MSP_REG_DMACR, MSP_DMACR(stm_msp->regs));
			writel(DISABLE_ALL_MSP_INTERRUPTS,
				MSP_IMSC(stm_msp->regs));
			writel(CLEAR_ALL_MSP_INTERRUPTS,
			       MSP_ICR(stm_msp->regs));
			break;
		}
	default:
		dev_dbg(&stm_msp->adev->dev, "Unknown command\n");
		retval = -1;
		break;
	}

	return retval;
}

/**
 * giveback - current spi_message is over, schedule next spi_message
 * @message: current SPI message
 * @stm_msp: spi driver private data structure
 *
 * current spi_message is over, schedule next spi_message and call
 * callback of this msg.
 */
static void giveback(struct spi_message *message, struct stm_msp *stm_msp)
{
	struct spi_transfer *last_transfer;
	unsigned long flags;
	struct spi_message *msg;
	void (*curr_cs_control)(u32 command);

	spin_lock_irqsave(&stm_msp->lock, flags);
	msg = stm_msp->cur_msg;

	curr_cs_control = stm_msp->cur_chip->cs_control;

	stm_msp->cur_msg = NULL;
	stm_msp->cur_transfer = NULL;
	stm_msp->cur_chip = NULL;
#ifdef CONFIG_SPI_WORKQUEUE
	queue_work(stm_msp->workqueue, &stm_msp->spi_work);
#else
	schedule_work(&stm_msp->spi_work);
#endif
	spin_unlock_irqrestore(&stm_msp->lock, flags);

	last_transfer = list_entry(msg->transfers.prev,
			struct spi_transfer, transfer_list);

	if (!last_transfer->cs_change)
		curr_cs_control(SPI_CHIP_DESELECT);

	msg->state = NULL;

	if (msg->complete)
		msg->complete(msg->context);

	stm_msp_controller_cmd(stm_msp, DISABLE_CONTROLLER);
	clk_disable(stm_msp->clk);
}

/**
 * spi_notify - Handles Polling hang issue over spi bus.
 * @data: main driver data
 * Context: Process.
 *
 * This is  used to handle error condition in transfer and receive function used
 * in polling mode.
 * Sometimes due to passing wrong protocol desc , polling transfer may hang.
 * To prevent this, timer is added.
 *
 * Returns void.
 */
static void spi_notify(unsigned long data)
{
	struct stm_msp *stm_msp = (struct stm_msp *)data;
	stm_msp->spi_io_error = 1;

	dev_err(&stm_msp->adev->dev,
		"Polling is taking time, maybe device not responding\n");

	del_timer(&stm_msp->spi_notify_timer);
}

/**
 * stm_msp_transfer - transfer function registered to SPI master framework
 * @spi: spi device which is requesting transfer
 * @msg: spi message which is to handled is queued to driver queue
 *
 * This function is registered to the SPI framework for this SPI master
 * controller. It will queue the spi_message in the queue of driver if
 * the queue is not stopped and return.
 */
static int stm_msp_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct stm_msp *stm_msp = spi_master_get_devdata(spi->master);
	unsigned long flags;

	spin_lock_irqsave(&stm_msp->lock, flags);

	if (stm_msp->run == QUEUE_STOPPED) {
		spin_unlock_irqrestore(&stm_msp->lock, flags);
		return -ESHUTDOWN;
	}
	dev_err(&spi->dev, "Regular request (No infinite DMA ongoing)\n");

	msg->actual_length = 0;
	msg->status = -EINPROGRESS;
	msg->state = START_STATE;

	list_add_tail(&msg->queue, &stm_msp->queue);

	if ((stm_msp->run == QUEUE_RUNNING) && (!stm_msp->busy))
#ifdef CONFIG_SPI_WORKQUEUE
		queue_work(stm_msp->workqueue, &stm_msp->spi_work);
#else
		schedule_work(&stm_msp->spi_work);
#endif
	spin_unlock_irqrestore(&stm_msp->lock, flags);
	return 0;
}

/**
 * next_transfer - Move to the Next transfer in the current spi message
 * @stm_msp: spi driver private data structure
 *
 * This function moves though the linked list of spi transfers in the
 * current spi message and returns with the state of current spi
 * message i.e whether its last transfer is done(DONE_STATE) or
 * Next transfer is ready(RUNNING_STATE)
 */
static void *next_transfer(struct stm_msp *stm_msp)
{
	struct spi_message *msg = stm_msp->cur_msg;
	struct spi_transfer *trans = stm_msp->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		stm_msp->cur_transfer = list_entry(trans->transfer_list.next,
					struct spi_transfer,
					transfer_list);
		return RUNNING_STATE;
	}
	return DONE_STATE;
}

static void do_interrupt_transfer(void *data)
{
	struct stm_msp *stm_msp = (struct stm_msp *)data;

	stm_msp->tx = (void *)stm_msp->cur_transfer->tx_buf;
	stm_msp->tx_end = stm_msp->tx + stm_msp->cur_transfer->len;

	stm_msp->rx = (void *)stm_msp->cur_transfer->rx_buf;
	stm_msp->rx_end = stm_msp->rx + stm_msp->cur_transfer->len;

	stm_msp->write = stm_msp->tx ?
		stm_msp->cur_chip->write : stm_msp->cur_chip->null_write;
	stm_msp->read = stm_msp->rx ?
		stm_msp->cur_chip->read : stm_msp->cur_chip->null_read;

	stm_msp->cur_chip->cs_control(SPI_CHIP_SELECT);

	stm_msp_controller_cmd(stm_msp, ENABLE_ALL_INTERRUPT);
	stm_msp_controller_cmd(stm_msp, ENABLE_CONTROLLER);
}

static void do_polling_transfer(void *data)
{
	struct stm_msp *stm_msp = (struct stm_msp *)data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct chip_data *chip;
	unsigned long limit = 0;
	u32 timer_expire = 0;

	chip = stm_msp->cur_chip;
	message = stm_msp->cur_msg;

	while (message->state != DONE_STATE) {
		/* Handle for abort */
		if (message->state == ERROR_STATE)
			break;

		transfer = stm_msp->cur_transfer;

		/* Delay if requested at end of transfer */
		if (message->state == RUNNING_STATE) {
			previous = list_entry(transfer->transfer_list.prev,
					struct spi_transfer,
					transfer_list);

			if (previous->delay_usecs)
				udelay(previous->delay_usecs);

			if (previous->cs_change)
				stm_msp->cur_chip->cs_control(SPI_CHIP_SELECT);
		} else {
			/* START_STATE */
			message->state = RUNNING_STATE;
			stm_msp->cur_chip->cs_control(SPI_CHIP_SELECT);
		}

		/* Configuration Changing Per Transfer */
		stm_msp->tx = (void *)transfer->tx_buf;
		stm_msp->tx_end = stm_msp->tx + stm_msp->cur_transfer->len;
		stm_msp->rx = (void *)transfer->rx_buf;
		stm_msp->rx_end = stm_msp->rx + stm_msp->cur_transfer->len;

		stm_msp->write = stm_msp->tx ?
			stm_msp->cur_chip->write :
				stm_msp->cur_chip->null_write;
		stm_msp->read = stm_msp->rx ?
			stm_msp->cur_chip->read :
				stm_msp->cur_chip->null_read;
		stm_msp->delay = stm_msp->cur_chip->delay;

		stm_msp_controller_cmd(stm_msp, FLUSH_FIFO);
		stm_msp_controller_cmd(stm_msp, ENABLE_CONTROLLER);

		timer_expire = stm_msp->cur_transfer->len / 1024;

		if (!timer_expire)
			timer_expire = SPI_TRANSFER_TIMEOUT_MS;
		else
			timer_expire =
				(stm_msp->cur_transfer->len / 1024) *
				SPI_TRANSFER_TIMEOUT_MS;

		stm_msp->spi_notify_timer.expires =
			jiffies + msecs_to_jiffies(timer_expire);

		add_timer(&stm_msp->spi_notify_timer);

		dev_dbg(&stm_msp->adev->dev, "Polling transfer ongoing...\n");

		while (stm_msp->tx < stm_msp->tx_end) {

			stm_msp_controller_cmd(stm_msp, DISABLE_CONTROLLER);
			stm_msp->read(stm_msp);
			stm_msp->write(stm_msp);

			stm_msp_controller_cmd(stm_msp, ENABLE_CONTROLLER);

			if (stm_msp->delay)
				stm_msp->delay(stm_msp);

			if (stm_msp->spi_io_error == 1)
				break;
		}

		del_timer(&stm_msp->spi_notify_timer);

		if (stm_msp->spi_io_error == 1)
			goto out;

		limit = loops_per_jiffy << 1;

		while ((stm_msp->rx < stm_msp->rx_end) && (limit--))
			stm_msp->read(stm_msp);

		/* Update total byte transfered */
		message->actual_length += stm_msp->cur_transfer->len;

		if (stm_msp->cur_transfer->cs_change)
			stm_msp->cur_chip->cs_control(SPI_CHIP_DESELECT);

		stm_msp_controller_cmd(stm_msp, DISABLE_CONTROLLER);

		/* Move to next transfer */
		message->state = next_transfer(stm_msp);
	}
out:
	/* Handle end of message */
	if (message->state == DONE_STATE)
		message->status = 0;
	else
		message->status = -EIO;

	giveback(message, stm_msp);

	stm_msp->spi_io_error = 0; /* Reset state for further transfers */

	return;
}

/**
 * pump_messages - Workqueue function which processes spi message queue
 * @work: pointer to work
 *
 * This function checks if there is any spi message in the queue that
 * needs processing and delegate control to appropriate function
 * do_polling_transfer()/do_interrupt_transfer()/do_dma_transfer()
 * based on the kind of the transfer
 *
 */
static void pump_messages(struct work_struct *work)
{
	struct stm_msp *stm_msp = container_of(work, struct stm_msp, spi_work);
	unsigned long flags;

	/* Lock queue and check for queue work */
	spin_lock_irqsave(&stm_msp->lock, flags);

	if (list_empty(&stm_msp->queue) || stm_msp->run == QUEUE_STOPPED) {
		dev_dbg(&stm_msp->adev->dev, "work_queue: Queue Empty\n");
		stm_msp->busy = 0;
		spin_unlock_irqrestore(&stm_msp->lock, flags);
		return;
	}
	/* Make sure we are not already running a message */
	if (stm_msp->cur_msg) {
		spin_unlock_irqrestore(&stm_msp->lock, flags);
		return;
	}

	clk_enable(stm_msp->clk);

	/* Extract head of queue */
	stm_msp->cur_msg = list_entry(stm_msp->queue.next,
				struct spi_message,
				queue);

	list_del_init(&stm_msp->cur_msg->queue);
	stm_msp->busy = 1;
	spin_unlock_irqrestore(&stm_msp->lock, flags);

	/* Initial message state */
	stm_msp->cur_msg->state = START_STATE;
	stm_msp->cur_transfer = list_entry(stm_msp->cur_msg->transfers.next,
				struct spi_transfer,
				transfer_list);

	/* Setup the SPI using the per chip configuration */
	stm_msp->cur_chip = spi_get_ctldata(stm_msp->cur_msg->spi);
	stm_msp_controller_cmd(stm_msp, RESTORE_STATE);
	stm_msp_controller_cmd(stm_msp, FLUSH_FIFO);

	if (stm_msp->cur_chip->xfer_type == SPI_POLLING_TRANSFER)
		do_polling_transfer(stm_msp);
	else if (stm_msp->cur_chip->xfer_type == SPI_INTERRUPT_TRANSFER)
		do_interrupt_transfer(stm_msp);
}

/**
 * pump_transfers - Tasklet function which schedules next interrupt xfer
 * @data: spi driver private data structure
 */
static void pump_transfers(unsigned long data)
{
	struct stm_msp *stm_msp = (struct stm_msp *)data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;

	message = stm_msp->cur_msg;

	/* Handle for abort */
	if (message->state == ERROR_STATE) {
		message->status = -EIO;
		giveback(message, stm_msp);
		return;
	}

	/* Handle end of message */
	if (message->state == DONE_STATE) {
		message->status = 0;
		giveback(message, stm_msp);
		return;
	}
	transfer = stm_msp->cur_transfer;

	/* Delay if requested at end of transfer */
	if (message->state == RUNNING_STATE) {
		previous = list_entry(transfer->transfer_list.prev,
					struct spi_transfer, transfer_list);
		if (previous->delay_usecs)
			udelay(previous->delay_usecs);
		if (previous->cs_change)
			stm_msp->cur_chip->cs_control(SPI_CHIP_SELECT);
	} else {
		/* START_STATE */
		message->state = RUNNING_STATE;
	}
	stm_msp->tx = (void *)transfer->tx_buf;
	stm_msp->tx_end = stm_msp->tx + stm_msp->cur_transfer->len;
	stm_msp->rx = (void *)transfer->rx_buf;
	stm_msp->rx_end = stm_msp->rx + stm_msp->cur_transfer->len;

	stm_msp->write = stm_msp->tx ?
		stm_msp->cur_chip->write : stm_msp->cur_chip->null_write;
	stm_msp->read = stm_msp->rx ?
		stm_msp->cur_chip->read : stm_msp->cur_chip->null_read;

	stm_msp_controller_cmd(stm_msp, FLUSH_FIFO);
	stm_msp_controller_cmd(stm_msp, ENABLE_ALL_INTERRUPT);
}

static int init_queue(struct stm_msp *stm_msp)
{
	INIT_LIST_HEAD(&stm_msp->queue);
	spin_lock_init(&stm_msp->lock);

	stm_msp->run = QUEUE_STOPPED;
	stm_msp->busy = 0;

	tasklet_init(&stm_msp->pump_transfers, pump_transfers,
			(unsigned long)stm_msp);
	INIT_WORK(&stm_msp->spi_work, pump_messages);

#ifdef CONFIG_SPI_WORKQUEUE
	stm_msp->workqueue = create_singlethread_workqueue(
		dev_name(&stm_msp->master->dev));

	if (stm_msp->workqueue == NULL)
		return -EBUSY;
#endif /* CONFIG_SPI_WORKQUEUE */

	init_timer(&stm_msp->spi_notify_timer);

	stm_msp->spi_notify_timer.expires = jiffies + msecs_to_jiffies(1000);
	stm_msp->spi_notify_timer.function = spi_notify;
	stm_msp->spi_notify_timer.data = (unsigned long)stm_msp;

	return 0;
}

static int start_queue(struct stm_msp *stm_msp)
{
	unsigned long flags;

	spin_lock_irqsave(&stm_msp->lock, flags);

	if (stm_msp->run == QUEUE_RUNNING || stm_msp->busy) {
		spin_unlock_irqrestore(&stm_msp->lock, flags);
		return -EBUSY;
	}

	stm_msp->run = QUEUE_RUNNING;
	stm_msp->cur_msg = NULL;
	stm_msp->cur_transfer = NULL;
	stm_msp->cur_chip = NULL;
	spin_unlock_irqrestore(&stm_msp->lock, flags);
	return 0;
}

static int stop_queue(struct stm_msp *stm_msp)
{
	unsigned long flags;
	unsigned limit = 500;
	int status = 0;

	spin_lock_irqsave(&stm_msp->lock, flags);

	/* This is a bit lame, but is optimized for the common execution path.
	 * A wait_queue on the stm_msp->busy could be used, but then the common
	 * execution path (pump_messages) would be required to call wake_up or
	 * friends on every SPI message. Do this instead */

	stm_msp->run = QUEUE_STOPPED;

	while (!list_empty(&stm_msp->queue) && stm_msp->busy && limit--) {
		spin_unlock_irqrestore(&stm_msp->lock, flags);
		msleep(10);
		spin_lock_irqsave(&stm_msp->lock, flags);
	}

	if (!list_empty(&stm_msp->queue) || stm_msp->busy)
		status = -EBUSY;

	spin_unlock_irqrestore(&stm_msp->lock, flags);

	return status;
}

static int destroy_queue(struct stm_msp *stm_msp)
{
	int status;

	status = stop_queue(stm_msp);

	if (status != 0)
		return status;
#ifdef CONFIG_SPI_WORKQUEUE
	destroy_workqueue(stm_msp->workqueue);
#endif
	del_timer_sync(&stm_msp->spi_notify_timer);

	return 0;
}

/**
 * stm_msp_null_writer - To Write Dummy Data in Data register
 * @stm_msp: spi driver private data structure
 *
 * This function is set as a write function for transfer which have
 * Tx transfer buffer as NULL. It simply writes '0' in the Data
 * register
 */
static void stm_msp_null_writer(struct stm_msp *stm_msp)
{
	u32 cur_write = 0;
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_TFU) ||
			(stm_msp->tx >= stm_msp->tx_end))
				return;

		writel(0x0, MSP_DR(stm_msp->regs));
		stm_msp->tx += (stm_msp->cur_chip->n_bytes);
		cur_write++;

		if (cur_write == 8)
			return;
	}
}

/**
 * stm_msp_null_reader - To read data from Data register and discard it
 * @stm_msp: spi driver private data structure
 *
 * This function is set as a reader function for transfer which have
 * Rx Transfer buffer as null. Read Data is rejected
 */
static void stm_msp_null_reader(struct stm_msp *stm_msp)
{
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_RFE) ||
			(stm_msp->rx >= stm_msp->rx_end))
			return;

		readl(MSP_DR(stm_msp->regs));
		stm_msp->rx += (stm_msp->cur_chip->n_bytes);
	}
}

/**
 * stm_msp_u8_writer - Write FIFO data in Data register as a 8 Bit Data
 * @stm_msp: spi driver private data structure
 *
 * This function writes data in Tx FIFO till it is not full
 * which is indicated by the status register or our transfer is complete.
 * It also updates the temporary write ptr tx in stm_msp which maintains
 * current write position in transfer buffer. we do not write data more than
 * FIFO depth
 */
void stm_msp_u8_writer(struct stm_msp *stm_msp)
{
	u32 cur_write = 0;
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_TFU) ||
			(stm_msp->tx >= stm_msp->tx_end))
			return;

		writel((u32)(*(u8 *)(stm_msp->tx)), MSP_DR(stm_msp->regs));
		stm_msp->tx += (stm_msp->cur_chip->n_bytes);
		cur_write++;

		if (cur_write == MSP_FIFO_DEPTH)
			return;
	}
}

/**
 * stm_msp_u8_reader - Read FIFO data in Data register as a 8 Bit Data
 * @stm_msp: spi driver private data structure
 *
 * This function reads data in Rx FIFO till it is not empty
 * which is indicated by the status register or our transfer is complete.
 * It also updates the temporary Read ptr rx in stm_msp which maintains
 * current read position in transfer buffer
 */
void stm_msp_u8_reader(struct stm_msp *stm_msp)
{
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_RFE) ||
			(stm_msp->rx >= stm_msp->rx_end))
			return;

		*(u8 *)(stm_msp->rx) = (u8)readl(MSP_DR(stm_msp->regs));
		stm_msp->rx += (stm_msp->cur_chip->n_bytes);
	}
}

/**
 * stm_msp_u16_writer - Write FIFO data in Data register as a 16 Bit Data
 * @stm_msp: spi driver private data structure
 *
 * This function writes data in Tx FIFO till it is not full
 * which is indicated by the status register or our transfer is complete.
 * It also updates the temporary write ptr tx in stm_msp which maintains
 * current write position in transfer buffer. we do not write data more than
 * FIFO depth
 */
void stm_msp_u16_writer(struct stm_msp *stm_msp)
{
	u32 cur_write = 0;
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_TFU) ||
			(stm_msp->tx >= stm_msp->tx_end))
			return;

		writel((u32)(*(u16 *)(stm_msp->tx)), MSP_DR(stm_msp->regs));
		stm_msp->tx += (stm_msp->cur_chip->n_bytes);
		cur_write++;

		if (cur_write == MSP_FIFO_DEPTH)
			return;
	}
}

/**
 * stm_msp_u16_reader - Read FIFO data in Data register as a 16 Bit Data
 * @stm_msp: spi driver private data structure
 *
 * This function reads data in Rx FIFO till it is not empty
 * which is indicated by the status register or our transfer is complete.
 * It also updates the temporary Read ptr rx in stm_msp which maintains
 * current read position in transfer buffer
 */
void stm_msp_u16_reader(struct stm_msp *stm_msp)
{
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_RFE) ||
			(stm_msp->rx >= stm_msp->rx_end))
			return;

		*(u16 *)(stm_msp->rx) = (u16)readl(MSP_DR(stm_msp->regs));
		stm_msp->rx += (stm_msp->cur_chip->n_bytes);
	}
}

/**
 * stm_msp_u32_writer - Write FIFO data in Data register as a 32 Bit Data
 * @stm_msp: spi driver private data structure
 *
 * This function writes data in Tx FIFO till it is not full
 * which is indicated by the status register or our transfer is complete.
 * It also updates the temporary write ptr tx in stm_msp which maintains
 * current write position in transfer buffer. we do not write data more than
 * FIFO depth
 */
void stm_msp_u32_writer(struct stm_msp *stm_msp)
{
	u32 cur_write = 0;
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_TFU) ||
			(stm_msp->tx >= stm_msp->tx_end))
			return;

		/* Write Data to Data Register */
		writel(*(u32 *)(stm_msp->tx), MSP_DR(stm_msp->regs));
		stm_msp->tx += (stm_msp->cur_chip->n_bytes);
		cur_write++;

		if (cur_write == MSP_FIFO_DEPTH)
			return;
	}
}

/**
 * stm_msp_u32_reader - Read FIFO data in Data register as a 32 Bit Data
 * @stm_msp: spi driver private data structure
 *
 * This function reads data in Rx FIFO till it is not empty
 * which is indicated by the status register or our transfer is complete.
 * It also updates the temporary Read ptr rx in stm_msp which maintains
 * current read position in transfer buffer
 */
void stm_msp_u32_reader(struct stm_msp *stm_msp)
{
	u32 status;

	while (1) {
		status = readl(MSP_FLR(stm_msp->regs));

		if ((status & MSP_FLR_MASK_RFE) ||
			(stm_msp->rx >= stm_msp->rx_end))
			return;

		*(u32 *)(stm_msp->rx) = readl(MSP_DR(stm_msp->regs));
		stm_msp->rx += (stm_msp->cur_chip->n_bytes);
	}
}

/**
 * stm_msp_interrupt_handler - Interrupt hanlder function
 */
static irqreturn_t stm_msp_interrupt_handler(int irq, void *dev_id)
{
	struct stm_msp *stm_msp = (struct stm_msp *)dev_id;
	struct spi_message *msg = stm_msp->cur_msg;
	u32 irq_status = 0;
	u32 flag = 0;

	if (!msg) {
		dev_err(&stm_msp->adev->dev,
			"Bad message state in interrupt handler");
		/* Never fail */
		return IRQ_HANDLED;
	}

	/* Read the Interrupt Status Register */
	irq_status = readl(MSP_MIS(stm_msp->regs));

	if (irq_status) {
		if (irq_status & MSP_MIS_MASK_ROEMIS) {	/* Overrun interrupt */
			/* Bail out our Data has been corrupted */
			dev_dbg(&stm_msp->adev->dev,
				"Received ROR interrupt\n");

			stm_msp_controller_cmd(stm_msp, DISABLE_ALL_INTERRUPT);
			stm_msp_controller_cmd(stm_msp, CLEAR_ALL_INTERRUPT);
			stm_msp_controller_cmd(stm_msp, DISABLE_CONTROLLER);
			msg->state = ERROR_STATE;
			tasklet_schedule(&stm_msp->pump_transfers);
			return IRQ_HANDLED;
		}

		stm_msp->read(stm_msp);
		stm_msp->write(stm_msp);

		if ((stm_msp->tx == stm_msp->tx_end) && (flag == 0)) {
			flag = 1;
			/* Disable Transmit interrupt */
			writel(readl(MSP_IMSC(stm_msp->regs)) &
				(~MSP_IMSC_MASK_TXIM) & (~MSP_IMSC_MASK_TFOIM),
			       (stm_msp->regs + 0x14));
		}

		/* Clearing any Xmit underrun error. Overrun already handled */
		stm_msp_controller_cmd(stm_msp, CLEAR_ALL_INTERRUPT);

		if (stm_msp->rx == stm_msp->rx_end) {
			stm_msp_controller_cmd(stm_msp, DISABLE_ALL_INTERRUPT);
			stm_msp_controller_cmd(stm_msp, CLEAR_ALL_INTERRUPT);

			dev_dbg(&stm_msp->adev->dev,
				"Interrupt transfer completed.\n");

			/* Update total bytes transfered */
			msg->actual_length += stm_msp->cur_transfer->len;

			if (stm_msp->cur_transfer->cs_change)
				stm_msp->cur_chip->cs_control(
					SPI_CHIP_DESELECT);

			/* Move to next transfer */
			msg->state = next_transfer(stm_msp);
			tasklet_schedule(&stm_msp->pump_transfers);
			return IRQ_HANDLED;
		}
	}
	return IRQ_HANDLED;
}

/**
 * stm_msp_cleanup - cleanup function registered to SPI master framework
 * @spi: spi device which is requesting cleanup
 *
 * This function is registered to the SPI framework for this SPI master
 * controller. It will free the runtime state of chip.
 */
static void stm_msp_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata((struct spi_device *)spi);
	struct stm_msp *stm_msp = spi_master_get_devdata(spi->master);
	struct spi_master *master;
	master = stm_msp->master;

	if (chip) {
		kfree(chip->ctr_regs);
		kfree(chip);
		spi_set_ctldata(spi, NULL);
	}
}

/**
 * null_cs_control - Dummy chip select function
 * @command: select/delect the chip
 *
 * If no chip select function is provided by client this is used as dummy
 * chip select
 */
static void null_cs_control(u32 command)
{
	/* Nothing to do */
	(void)command;
}

static int verify_msp_controller_parameters(struct stm_msp_config_chip
		*chip_info)
{

	/* FIXME: check clock params */
	if ((chip_info->lbm != SPI_LOOPBACK_ENABLED) &&
		(chip_info->lbm != SPI_LOOPBACK_DISABLED)) {
		dev_dbg(chip_info->dev,
			"Loopback Mode is configured incorrectly\n");
		return -1;
	}
	if ((chip_info->hierarchy != SPI_MASTER) &&
		(chip_info->hierarchy != SPI_SLAVE)) {
		dev_dbg(chip_info->dev,
			"hierarchy is configured incorrectly\n");
		return -1;
	}
	if ((chip_info->endian_rx != SPI_FIFO_MSB) &&
		(chip_info->endian_rx != SPI_FIFO_LSB)) {
		dev_dbg(chip_info->dev,
			"Rx FIFO endianess is configured incorrectly\n");
		return -1;
	}
	if ((chip_info->endian_tx != SPI_FIFO_MSB) &&
		(chip_info->endian_tx != SPI_FIFO_LSB)) {
		dev_dbg(chip_info->dev,
			"Tx FIFO endianess is configured incorrectly\n");
		return -1;
	}
	if ((chip_info->data_size < MSP_DATA_BITS_8) ||
		(chip_info->data_size > MSP_DATA_BITS_32)) {
		dev_dbg(chip_info->dev,
			"MSP DATA Size is configured incorrectly\n");
		return -1;
	}
	if ((chip_info->com_mode != SPI_INTERRUPT_TRANSFER) &&
		(chip_info->com_mode != SPI_POLLING_TRANSFER)) {
		dev_dbg(chip_info->dev,
			"Communication mode is configured incorrectly\n");
		return -1;
	}
	if (((chip_info->proto_params).clk_phase !=
	     SPI_CLK_ZERO_CYCLE_DELAY) &&
		((chip_info->proto_params).clk_phase !=
		SPI_CLK_HALF_CYCLE_DELAY)) {
		dev_dbg(chip_info->dev,
			"Clock Phase is configured incorrectly\n");
		return -1;
	}
	if (((chip_info->proto_params).clk_pol !=
	     SPI_CLK_POL_IDLE_LOW) &&
		((chip_info->proto_params).clk_pol !=
		SPI_CLK_POL_IDLE_HIGH)) {
		dev_dbg(chip_info->dev,
			"Clk Polarity configured incorrectly\n");
		return -1;
	}
	if (chip_info->cs_control == NULL) {
		dev_dbg(chip_info->dev,
			"Chip Select Function is NULL for this chip\n");
		chip_info->cs_control = null_cs_control;
	}
	return 0;
}

static struct stm_msp_config_chip *allocate_default_msp_chip_cfg(
		struct spi_device *spi)
{
	struct stm_msp_config_chip *chip_info;

	chip_info = kzalloc(sizeof(struct stm_msp_config_chip), GFP_KERNEL);

	if (!chip_info) {
		dev_err(&spi->dev, "setup - cannot allocate controller data");
		return NULL;
	}
	dev_dbg(&spi->dev, "Allocated Memory for controller data\n");

	chip_info->lbm = SPI_LOOPBACK_DISABLED;
	chip_info->com_mode = SPI_POLLING_TRANSFER;
	chip_info->hierarchy = SPI_MASTER;
	chip_info->endian_tx = SPI_FIFO_LSB;
	chip_info->endian_rx = SPI_FIFO_LSB;
	chip_info->data_size = MSP_DATA_BITS_32;

	if (spi->max_speed_hz != 0)
		chip_info->freq = spi->max_speed_hz;
	else
		chip_info->freq = SPI_DEFAULT_MAX_SPEED_HZ;

	chip_info->proto_params.clk_phase = SPI_CLK_HALF_CYCLE_DELAY;
	chip_info->proto_params.clk_pol = SPI_CLK_POL_IDLE_LOW;
	chip_info->cs_control = null_cs_control;

	return chip_info;
}

static void stm_msp_delay(struct stm_msp *stm_msp)
{
	udelay(15);

	while (readl(MSP_FLR(stm_msp->regs)) &
		(MSP_FLR_MASK_RBUSY | MSP_FLR_MASK_TBUSY))
			udelay(1);
}

/**
 * stm_msp_setup - setup function registered to SPI master framework
 * @spi: spi device which is requesting setup
 *
 * This function is registered to the SPI framework for this SPI master
 * controller. If it is the first time when setup is called by this device,
 * this function will initialize the runtime state for this chip and save
 * the same in the device structure. Else it will update the runtime info
 * with the updated chip info.
 */
static int stm_msp_setup(struct spi_device *spi)
{
	struct stm_msp_config_chip *chip_info;
	struct chip_data *curr_cfg;
	struct spi_master *master;
	int status = 0;
	u16 sckdiv = 0;
	s16 bus_num = 0;
	struct stm_msp *stm_msp = spi_master_get_devdata(spi->master);
	struct msp_regs *msp_regs;
	master = stm_msp->master;
	bus_num = master->bus_num - 1;

	/* Get controller data */
	chip_info = spi->controller_data;
	/* Get controller_state */
	curr_cfg = spi_get_ctldata(spi);

	if (curr_cfg == NULL) {
		curr_cfg = kzalloc(sizeof(struct chip_data), GFP_KERNEL);

		if (!curr_cfg) {
			dev_err(&stm_msp->adev->dev,
				"setup - cannot allocate controller state");
			return -ENOMEM;
		}

		curr_cfg->chip_id = spi->chip_select;
		curr_cfg->ctr_regs = kzalloc(sizeof(struct msp_regs),
						GFP_KERNEL);

		if (curr_cfg->ctr_regs == NULL) {
			dev_err(&stm_msp->adev->dev,
				"setup - cannot allocate mem for regs");
			goto err_first_setup;
		}

		dev_err(&stm_msp->adev->dev,
			"chip Id = %d\n", curr_cfg->chip_id);

		if (chip_info == NULL) {
			chip_info = allocate_default_msp_chip_cfg(spi);

			if (!chip_info) {
				dev_err(&stm_msp->adev->dev,
					"setup - cannot allocate cntlr data");
				status = -ENOMEM;
				goto err_first_setup;
			}

			spi->controller_data = chip_info;
		}
	}

	/* Pointer back to the SPI device */
	chip_info->dev = &spi->dev;

	if (chip_info->freq == 0) {
		/* Calculate Specific Freq. */
		if ((MSP_INTERNAL_CLK == chip_info->clk_freq.clk_src) ||
			(MSP_EXTERNAL_CLK == chip_info->clk_freq.clk_src)) {
			sckdiv = chip_info->clk_freq.sckdiv;
		} else {
			status = -1;
			dev_err(&stm_msp->adev->dev,
				"setup - controller clock data is incorrect");
			goto err_config_params;
		}
	} else {
		/* Calculate Effective Freq. */
		sckdiv = (DEFAULT_MSP_CLK / (chip_info->freq)) - 1;

		if (sckdiv > MAX_SCKDIV) {
			dev_dbg(&stm_msp->adev->dev,
				"SPI: Cannot set frequency less than 48Khz,"
				"setting lowest(48 Khz)\n");
			sckdiv = MAX_SCKDIV;
		}
	}

	status = verify_msp_controller_parameters(chip_info);

	if (status) {
		dev_err(&stm_msp->adev->dev,
			"setup - controller data is incorrect");
		goto err_config_params;
	}

	/* Now set controller state based on controller data */
	curr_cfg->xfer_type = chip_info->com_mode;
	curr_cfg->cs_control = chip_info->cs_control;
	curr_cfg->delay = stm_msp_delay;

	curr_cfg->null_write = stm_msp_null_writer;
	curr_cfg->null_read = stm_msp_null_reader;

	if (chip_info->data_size <= MSP_DATA_BITS_8) {
		dev_dbg(&stm_msp->adev->dev, "Less than 8 bits per word...\n");

		curr_cfg->n_bytes = 1;
		curr_cfg->read = stm_msp_u8_reader;
		curr_cfg->write = stm_msp_u8_writer;
	} else if (chip_info->data_size <= MSP_DATA_BITS_16) {
		dev_dbg(&stm_msp->adev->dev, "Less than 16 bits per word...\n");

		curr_cfg->n_bytes = 2;
		curr_cfg->read = stm_msp_u16_reader;
		curr_cfg->write = stm_msp_u16_writer;
	} else {
		dev_dbg(&stm_msp->adev->dev, "Less than 32 bits per word...\n");

		curr_cfg->n_bytes = 4;
		curr_cfg->read = stm_msp_u32_reader;
		curr_cfg->write = stm_msp_u32_writer;
	}

	/* Now initialize all register settings reqd. for this chip */

	msp_regs = (struct msp_regs *)(curr_cfg->ctr_regs);
	msp_regs->gcr = 0x0;
	msp_regs->tcf = 0x0;
	msp_regs->rcf = 0x0;
	msp_regs->srg = 0x0;
	msp_regs->dmacr = 0x0;

	MSP_WBITS(msp_regs->dmacr, 0x0, MSP_DMACR_MASK_RDMAE, 0);
	MSP_WBITS(msp_regs->dmacr, 0x0, MSP_DMACR_MASK_TDMAE, 1);

	/* GCR Reg Config */

	MSP_WBITS(msp_regs->gcr,
			MSP_RECEIVER_DISABLED, MSP_GCR_MASK_RXEN, 0);
	MSP_WBITS(msp_regs->gcr,
			MSP_RX_FIFO_ENABLED, MSP_GCR_MASK_RFFEN, 1);
	MSP_WBITS(msp_regs->gcr,
			MSP_TRANSMITTER_DISABLED, MSP_GCR_MASK_TXEN, 8);
	MSP_WBITS(msp_regs->gcr,
			MSP_TX_FIFO_ENABLED, MSP_GCR_MASK_TFFEN, 9);
	MSP_WBITS(msp_regs->gcr,
			MSP_TX_FRAME_SYNC_POL_LOW, MSP_GCR_MASK_TFSPOL, 10);
	MSP_WBITS(msp_regs->gcr,
			MSP_TX_FRAME_SYNC_INT, MSP_GCR_MASK_TFSSEL, 11);
	MSP_WBITS(msp_regs->gcr,
			MSP_TRANSMIT_DATA_WITH_DELAY, MSP_GCR_MASK_TXDDL, 15);
	MSP_WBITS(msp_regs->gcr,
			MSP_SAMPLE_RATE_GEN_ENABLE, MSP_GCR_MASK_SGEN, 16);
	MSP_WBITS(msp_regs->gcr,
			MSP_CLOCK_INTERNAL, MSP_GCR_MASK_SCKSEL, 18);
	MSP_WBITS(msp_regs->gcr,
			MSP_FRAME_GEN_ENABLE, MSP_GCR_MASK_FGEN, 20);
	MSP_WBITS(msp_regs->gcr,
			SPI_BURST_MODE_DISABLE, MSP_GCR_MASK_SPIBME, 23);

	if (chip_info->lbm == SPI_LOOPBACK_ENABLED)
		MSP_WBITS(msp_regs->gcr,
				MSP_LOOPBACK_ENABLED, MSP_GCR_MASK_LBM, 7);
	else
		MSP_WBITS(msp_regs->gcr,
				MSP_LOOPBACK_DISABLED, MSP_GCR_MASK_LBM, 7);

	if (chip_info->hierarchy == SPI_MASTER)
		MSP_WBITS(msp_regs->gcr,
				MSP_IS_SPI_MASTER, MSP_GCR_MASK_TCKSEL, 14);
	else
		MSP_WBITS(msp_regs->gcr,
				MSP_IS_SPI_SLAVE, MSP_GCR_MASK_TCKSEL, 14);

	if (chip_info->proto_params.clk_phase == SPI_CLK_ZERO_CYCLE_DELAY)
		MSP_WBITS(msp_regs->gcr,
				MSP_SPI_PHASE_ZERO_CYCLE_DELAY,
				MSP_GCR_MASK_SPICKM, 21);
	else
		MSP_WBITS(msp_regs->gcr,
				MSP_SPI_PHASE_HALF_CYCLE_DELAY,
				MSP_GCR_MASK_SPICKM, 21);

	if (chip_info->proto_params.clk_pol == SPI_CLK_POL_IDLE_HIGH)
		MSP_WBITS(msp_regs->gcr,
				MSP_TX_CLOCK_POL_HIGH, MSP_GCR_MASK_TCKPOL, 13);
	else
		MSP_WBITS(msp_regs->gcr,
				MSP_TX_CLOCK_POL_LOW, MSP_GCR_MASK_TCKPOL, 13);

	/* RCF Reg Config */
	MSP_WBITS(msp_regs->rcf,
			MSP_IGNORE_RX_FRAME_SYNC_PULSE, MSP_RCF_MASK_RFSIG, 15);
	MSP_WBITS(msp_regs->rcf,
			MSP_RX_1BIT_DATA_DELAY, MSP_RCF_MASK_RDDLY, 13);

	if (chip_info->endian_rx == SPI_FIFO_LSB)
		MSP_WBITS(msp_regs->rcf,
				MSP_RX_ENDIANESS_LSB, MSP_RCF_MASK_RENDN, 12);
	else
		MSP_WBITS(msp_regs->rcf,
				MSP_RX_ENDIANESS_MSB, MSP_RCF_MASK_RENDN, 12);

	MSP_WBITS(msp_regs->rcf, chip_info->data_size, MSP_RCF_MASK_RP1ELEN, 0);

	/* TCF Reg Config */

	MSP_WBITS(msp_regs->tcf,
			MSP_IGNORE_TX_FRAME_SYNC_PULSE, MSP_TCF_MASK_TFSIG, 15);
	MSP_WBITS(msp_regs->tcf,
			MSP_TX_1BIT_DATA_DELAY, MSP_TCF_MASK_TDDLY, 13);

	if (chip_info->endian_rx == SPI_FIFO_LSB)
		MSP_WBITS(msp_regs->tcf,
				MSP_TX_ENDIANESS_LSB, MSP_TCF_MASK_TENDN, 12);
	else
		MSP_WBITS(msp_regs->tcf,
				MSP_TX_ENDIANESS_MSB, MSP_TCF_MASK_TENDN, 12);
	MSP_WBITS(msp_regs->tcf, chip_info->data_size, MSP_TCF_MASK_TP1ELEN, 0);

	/* SRG Reg Config */

	MSP_WBITS(msp_regs->srg, sckdiv, MSP_SRG_MASK_SCKDIV, 0);

	/* Save controller_state */
	spi_set_ctldata(spi, curr_cfg);

	return status;

err_config_params:
err_first_setup:

	kfree(curr_cfg);
	return status;
}

static int __init stm_msp_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct stm_msp_controller *platform_info = adev->dev.platform_data;
	struct spi_master *master;
	struct stm_msp *stm_msp = NULL;	/* Data for this driver */
	int irq, status = 0;

	dev_info(dev, "STM MSP driver, device ID: 0x%08x\n", adev->periphid);

	if (platform_info == NULL) {
		dev_err(dev, "probe - no platform data supplied\n");
		status = -ENODEV;
		goto err_no_pdata;
	}

	/* Allocate master with space for data */
	master = spi_alloc_master(dev, sizeof(struct stm_msp));

	if (master == NULL) {
		dev_err(dev, "probe - cannot alloc spi_master\n");
		status = -ENOMEM;
		goto err_no_mem;
	}

	stm_msp = spi_master_get_devdata(master);
	stm_msp->master = master;
	stm_msp->master_info = platform_info;
	stm_msp->adev = adev;

	stm_msp->clk = clk_get(&adev->dev, NULL);

	if (IS_ERR(stm_msp->clk)) {
		dev_err(dev, "probe - cannot find clock\n");
		status = PTR_ERR(stm_msp->clk);
		goto free_master;
	}

	/* Fetch the Resources, using platform data */
	status = amba_request_regions(adev, NULL);

	if (status) {
		status = -ENODEV;
		goto disable_clk;
	}

	/* Get Hold of Device Register Area... */
	stm_msp->regs = ioremap(adev->res.start, resource_size(&adev->res));

	if (stm_msp->regs == NULL) {
		status = -ENODEV;
		goto disable_clk;
	}

	irq = adev->irq[0];

	if (irq <= 0) {
		status = -ENODEV;
		goto err_no_iores;
	}

	stm_msp_controller_cmd(stm_msp, LOAD_DEFAULT_CONFIG);

	/* Required Info for an SPI controller */
	/* Bus Number Which Assigned to this SPI controller on this board */
	master->bus_num = (u16) platform_info->id;
	master->num_chipselect = platform_info->num_chipselect;
	master->setup = stm_msp_setup;
	master->cleanup = (void *)stm_msp_cleanup;
	master->transfer = stm_msp_transfer;

	dev_dbg(dev, "BUSNO: %d\n", master->bus_num);

	/* Initialize and start queue */
	status = init_queue(stm_msp);

	if (status != 0) {
		dev_err(dev, "probe - problem initializing queue\n");
		goto err_init_queue;
	}

	status = start_queue(stm_msp);

	if (status != 0) {
		dev_err(dev, "probe - problem starting queue\n");
		goto err_start_queue;
	}

	amba_set_drvdata(adev, stm_msp);

	dev_dbg(dev, "probe succeded\n");
	dev_dbg(dev, "Bus No = %d, IRQ Line = %d, Virtual Addr: %x\n",
		master->bus_num, irq, (u32)(stm_msp->regs));

	status = request_irq(stm_msp->adev->irq[0],
			stm_msp_interrupt_handler,
			0, stm_msp->master_info->device_name,
			stm_msp);

	if (status < 0) {
		dev_err(dev, "probe - cannot get IRQ (%d)\n", status);
		goto err_irq;
	}

	/* Register with the SPI framework */
	status = spi_register_master(master);

	if (status != 0) {
		dev_err(dev, "probe - problem registering spi master\n");
		goto err_spi_register;
	}

	return 0;

err_spi_register:
	free_irq(stm_msp->adev->irq[0], stm_msp);
err_irq:
err_init_queue:
err_start_queue:
	destroy_queue(stm_msp);
err_no_iores:
	iounmap(stm_msp->regs);
disable_clk:
	clk_put(stm_msp->clk);
free_master:
	spi_master_put(master);
err_no_mem:
err_no_pdata:
	return status;
}

static int __exit stm_msp_remove(struct amba_device *adev)
{
	struct stm_msp *stm_msp = amba_get_drvdata(adev);
	int status = 0;

	if (!stm_msp)
		return 0;

	/* Remove the queue */
	status = destroy_queue(stm_msp);

	if (status != 0) {
		dev_err(&adev->dev, "queue remove failed (%d)\n", status);
		return status;
	}

	stm_msp_controller_cmd(stm_msp, LOAD_DEFAULT_CONFIG);

	/* Release map resources */
	iounmap(stm_msp->regs);
	amba_release_regions(adev);
	tasklet_disable(&stm_msp->pump_transfers);
	free_irq(stm_msp->adev->irq[0], stm_msp);

	/* Disconnect from the SPI framework */
	spi_unregister_master(stm_msp->master);

	clk_put(stm_msp->clk);

	/* Prevent double remove */
	amba_set_drvdata(adev, NULL);
	dev_dbg(&adev->dev, "remove succeded\n");
	return status;
}

#ifdef CONFIG_PM

/**
 * stm_msp_suspend - MSP suspend function registered with PM framework.
 * @dev: Reference to amba device structure of the device
 * @state: power mgmt state.
 *
 * This function is invoked when the system is going into sleep, called
 * by the power management framework of the linux kernel.
 */
static int stm_msp_suspend(struct amba_device *adev, pm_message_t state)
{
	struct stm_msp *stm_msp = amba_get_drvdata(adev);
	int status = 0;

	status = stop_queue(stm_msp);

	if (status != 0) {
		dev_warn(&adev->dev, "suspend cannot stop queue\n");
		return status;
	}

	dev_dbg(&adev->dev, "suspended\n");
	return 0;
}

/**
 * stm_msp_resume - MSP Resume function registered with PM framework.
 * @dev: Reference to amba device structure of the device
 *
 * This function is invoked when the system is coming out of sleep, called
 * by the power management framework of the linux kernel.
 */
static int stm_msp_resume(struct amba_device *adev)
{
	struct stm_msp *stm_msp = amba_get_drvdata(adev);
	int status = 0;

	/* Start the queue running */
	status = start_queue(stm_msp);

	if (status)
		dev_err(&adev->dev, "problem starting queue (%d)\n", status);
	else
		dev_dbg(&adev->dev, "resumed\n");

	return status;
}

#else
#define stm_msp_suspend	NULL
#define stm_msp_resume	NULL
#endif /* CONFIG_PM */

static struct amba_id stm_msp_ids[] = {
	{
		.id = 0x00280021,
		.mask = 0x00ffffff,
	},
	{
		0,
		0,
	},
};

static struct amba_driver __refdata stm_msp_driver = {
	.drv = {
		.name	= "MSP",
	},
	.id_table	= stm_msp_ids,
	.probe		= stm_msp_probe,
	.remove		= __exit_p(stm_msp_remove),
	.resume		= stm_msp_resume,
	.suspend	= stm_msp_suspend,
};

static int __init stm_msp_init(void)
{
	return amba_driver_register(&stm_msp_driver);
}

static void __exit stm_msp_exit(void)
{
	amba_driver_unregister(&stm_msp_driver);
}

module_init(stm_msp_init);
module_exit(stm_msp_exit);

MODULE_AUTHOR("Sachin Verma <sachin.verma@st.com>");
MODULE_DESCRIPTION("STM MSP (SPI protocol) Driver");
MODULE_LICENSE("GPL");
