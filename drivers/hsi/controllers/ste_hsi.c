/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com> for ST-Ericsson
 * Author: Lukasz Baj <lukasz.baj@tieto.com> for ST-Ericsson
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hsi/hsi.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/mfd/dbx500-prcmu.h>

#ifdef CONFIG_STE_DMA40
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#endif

#include <mach/hsi.h>

/*
 * Copy of HSIR/HSIT context for restoring after HW reset (Vape power off).
 */
struct ste_hsi_hw_context {
	unsigned int tx_mode;
	unsigned int tx_divisor;
	unsigned int tx_channels;
	unsigned int tx_priority;
	unsigned int rx_mode;
	unsigned int rx_channels;
};

/**
 * struct ste_hsi_controller - STE HSI controller data
 * @dev: device associated to STE HSI controller
 * @tx_dma_base: HSI TX peripheral physical address
 * @rx_dma_base: HSI RX peripheral physical address
 * @rx_base: HSI RX peripheral virtual address
 * @tx_base: HSI TX peripheral virtual address
 * @regulator: STE HSI Vape consumer regulator
 * @context: copy of client-configured HSI TX / HSI RX registers
 * @tx_clk: HSI TX core clock (HSITXCLK)
 * @rx_clk: HSI RX core clock (HSIRXCLK)
 * @ssitx_clk: HSI TX host clock (HCLK)
 * @ssirx_clk: HSI RX host clock (HCLK)
 * @clk_work: structure for delayed HSI clock disabling
 * @overrun_irq: HSI channels overrun IRQ table
 * @ck_refcount: reference count for clock enable operation
 * @ck_lock: locking primitive for HSI clocks
 * @lock: locking primitive for HSI controller
 * @use_dma: flag for DMA enabled
 * @ck_on: flag for HSI clocks enabled
 */
struct ste_hsi_controller {
	struct device *dev;
	dma_addr_t tx_dma_base;
	dma_addr_t rx_dma_base;
	unsigned char __iomem *rx_base;
	unsigned char __iomem *tx_base;
	struct regulator *regulator;
	struct ste_hsi_hw_context *context;
	struct clk *tx_clk;
	struct clk *rx_clk;
	struct clk *ssitx_clk;
	struct clk *ssirx_clk;
	struct delayed_work clk_work;
	int overrun_irq[STE_HSI_MAX_CHANNELS];
	int ck_refcount;
	spinlock_t ck_lock;
	spinlock_t lock;
	unsigned int use_dma:1;
	unsigned int ck_on:1;
};

#ifdef CONFIG_STE_DMA40
struct ste_hsi_channel_dma {
	struct dma_chan *dma_chan;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
};
#endif

struct ste_hsi_port {
	struct device *dev;
	struct list_head txqueue[STE_HSI_MAX_CHANNELS];
	struct list_head rxqueue[STE_HSI_MAX_CHANNELS];
	struct list_head brkqueue;
	int cawake_irq;
	int acwake_gpio;
	int tx_irq;
	int rx_irq;
	int excep_irq;
	struct tasklet_struct cawake_tasklet;
	struct tasklet_struct rx_tasklet;
	struct tasklet_struct tx_tasklet;
	struct tasklet_struct exception_tasklet;
	struct tasklet_struct overrun_tasklet;
	unsigned char channels;
#ifdef CONFIG_STE_DMA40
	struct ste_hsi_channel_dma tx_dma[STE_HSI_MAX_CHANNELS];
	struct ste_hsi_channel_dma rx_dma[STE_HSI_MAX_CHANNELS];
#endif
};

#define hsi_to_ste_port(port) (hsi_port_drvdata(port))
#define hsi_to_ste_controller(con) (hsi_controller_drvdata(con))
#define client_to_ste_port(cl) (hsi_port_drvdata(hsi_get_port(cl)))
#define client_to_hsi(cl) \
	(to_hsi_controller(hsi_get_port(cl)->device.parent))
#define client_to_ste_controller(cl)  \
	(hsi_controller_drvdata(client_to_hsi(cl)))
#define ste_port_to_ste_controller(port) \
	((struct ste_hsi_controller *)hsi_controller_drvdata(	\
		to_hsi_controller(port->dev->parent)))

static u32 ste_hsir_periphid[8] = { 0x2C, 0, 0x8, 0x18, 0xD, 0xF0, 0x5, 0xB1 };
static u32 ste_hsit_periphid[8] = { 0x2B, 0, 0x8, 0x18, 0xD, 0xF0, 0x5, 0xB1 };

/*
 * linux/amba/bus.h macros can not be used, because 8 bytes are validated:
 * PERIPHID0..3 and PCELLID0..3 for HSIR and HSIT.
 */
static inline int compare_periphid(u32 *id1, u32 *id2, int count)
{
	while (count && *id1++ == *id2++)
		count--;

	return count;
}

static void ste_hsi_clk_free(struct clk **pclk)
{
	if (IS_ERR(*pclk) && *pclk != NULL)
		clk_put(*pclk);
	*pclk = NULL;
}

static void ste_hsi_init_registers(struct ste_hsi_controller *ste_hsi)
{
	writel(0, ste_hsi->tx_base + STE_HSI_TX_BUFSTATE);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_FLUSHBITS);
	/* TO DO: TX channel priorities will be implemented later */
	writel(0, ste_hsi->tx_base + STE_HSI_TX_PRIORITY);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_DATASWAP);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_DMAEN);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKID);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIC);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM);

	/* 0x23 is reset value per DB8500 Design Spec */
	writel(0x23, ste_hsi->rx_base + STE_HSI_RX_THRESHOLD);

	writel(0, ste_hsi->rx_base + STE_HSI_RX_BUFSTATE);

	/* HSIR clock recovery mode */
	writel(0, ste_hsi->rx_base + STE_HSI_RX_DETECTOR);

	/* Bits 0,1,2 set to 1 to clear exception flags */
	writel(0x07, ste_hsi->rx_base + STE_HSI_RX_ACK);

	/* Bits 0..7 set to 1 to clear OVERRUN IRQ  */
	writel(0xFF, ste_hsi->rx_base + STE_HSI_RX_OVERRUNACK);

	writel(0, ste_hsi->rx_base + STE_HSI_RX_DMAEN);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIC);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM);

	/* Flush all errors */
	writel(0, ste_hsi->rx_base + STE_HSI_RX_EXCEP);

	/* 2 is Flush state, no RX exception generated afterwards */
	writel(2, ste_hsi->rx_base + STE_HSI_RX_STATE);

	writel(0, ste_hsi->rx_base + STE_HSI_RX_EXCEPIM);
}

static void ste_hsi_setup_registers(struct ste_hsi_controller *ste_hsi)
{
	unsigned int buffers, i;
	struct ste_hsi_hw_context *pcontext = ste_hsi->context;

	/*
	 * Configure TX
	 */
	writel(pcontext->tx_mode, ste_hsi->tx_base + STE_HSI_TX_MODE);
	writel(pcontext->tx_divisor, ste_hsi->tx_base + STE_HSI_TX_DIVISOR);
	writel(pcontext->tx_channels, ste_hsi->tx_base + STE_HSI_TX_CHANNELS);
	writel(pcontext->tx_priority, ste_hsi->tx_base + STE_HSI_TX_PRIORITY);

	/* Calculate buffers number per channel */
	buffers = STE_HSI_MAX_BUFFERS / pcontext->tx_channels;
	for (i = 0; i < pcontext->tx_channels; i++) {
		/* Set 32 bit long frames */
		writel(31, ste_hsi->tx_base + STE_HSI_TX_FRAMELENX + 4 * i);
		writel(buffers * i,
		       ste_hsi->tx_base + STE_HSI_TX_BASEX + 4 * i);
		writel(buffers - 1,
		       ste_hsi->tx_base + STE_HSI_TX_SPANX + 4 * i);

	/*
	 * The DMA burst request and the buffer occupation interrupt are
	 * asserted when the free space in the corresponding channel buffer
	 * is greater than the value programmed in TX_WATERMARKX field.
	 * The field value must be less than the corresponding SPAN value.
	 */
#ifdef CONFIG_STE_DMA40
		writel(STE_HSI_DMA_MAX_BURST-1,
			ste_hsi->tx_base + STE_HSI_TX_WATERMARKX + 4 * i);
#else /* IRQ mode */
		writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKX + 4 * i);
#endif
	}

	/*
	 * The value read from this register gives the synchronized status
	 * of the transmitter state and this synchronization takes 2 HSITCLK
	 * cycles plus 3 HCLK cycles.
	 */
	while (STE_HSI_STATE_IDLE != readl(ste_hsi->tx_base + STE_HSI_TX_STATE))
		cpu_relax();

	/*
	 * Configure RX
	 */
	writel(pcontext->rx_mode, ste_hsi->rx_base + STE_HSI_RX_MODE);

	if (STE_HSI_MODE_PIPELINED == pcontext->rx_mode)
		/*
		 * 0xFF: The READY line is negated after the start of the
		 * 256th frame reception in PIPELINED mode.
		 */
		writel(0xFF, ste_hsi->rx_base + STE_HSI_RX_FRAMEBURSTCNT);
	else
		writel(0, ste_hsi->rx_base + STE_HSI_RX_FRAMEBURSTCNT);

	writel(pcontext->rx_channels, ste_hsi->rx_base + STE_HSI_RX_CHANNELS);
	/* Calculate buffers number per channel */
	buffers = STE_HSI_MAX_BUFFERS / pcontext->rx_channels;
	for (i = 0; i < pcontext->rx_channels; i++) {
		/* Set 32 bit long frames */
		writel(31, ste_hsi->rx_base + STE_HSI_RX_FRAMELENX + 4 * i);
		writel(buffers * i,
		       ste_hsi->rx_base + STE_HSI_RX_BASEX + 4 * i);
		writel(buffers - 1,
		       ste_hsi->rx_base + STE_HSI_RX_SPANX + 4 * i);

	/*
	 * The DMA burst request and the buffer occupation interrupt are
	 * asserted when the busy space in the corresponding channel buffer
	 * is greater than the value programmed in RX_WATERMARKX field.
	 * The field value must be less than the corresponding SPAN value.
	 */
#ifdef CONFIG_STE_DMA40
		writel(STE_HSI_DMA_MAX_BURST-1,
			ste_hsi->rx_base + STE_HSI_RX_WATERMARKX + 4 * i);
#else /* IRQ mode */
		writel(0, ste_hsi->rx_base + STE_HSI_RX_WATERMARKX + 4 * i);
#endif
	}

	/*
	 * The value read from this register gives the synchronized status
	 * of the receiver state and this synchronization takes 2 HSIRCLK
	 * cycles plus 3 HCLK cycles.
	 */
	while (STE_HSI_STATE_IDLE != readl(ste_hsi->tx_base + STE_HSI_RX_STATE))
		cpu_relax();
}

/*
 * When cpuidle framework is setting the sleep or deep sleep state then
 * the Vape is OFF. This results in re-setting the HSIT/HSIR registers
 * to default (idle) values.
 * Function ste_hsi_context() is checking and restoring the HSI registers
 * to these set by the HSI client by ste_hsi_setup().
 */
static void ste_hsi_context(struct ste_hsi_controller *ste_hsi)
{
	unsigned int tx_channels;
	unsigned int rx_channels;


	tx_channels = readl(ste_hsi->tx_base + STE_HSI_TX_CHANNELS);
	rx_channels = readl(ste_hsi->rx_base + STE_HSI_RX_CHANNELS);

	/*
	 * Checking if the context was lost.
	 * The target config is at least 2 channels for both TX and RX.
	 * TX and RX channels are set to 1 after HW reset.
	 */
	if ((ste_hsi->context->tx_channels != tx_channels) ||
		(ste_hsi->context->rx_channels != rx_channels)) {
		/*
		 * TO DO: remove "dev_info" after thorough testing.
		 * Debug left for getting the statistics how frequently the context
		 * is lost during regular HSI operation.
		 */
		dev_info(ste_hsi->dev, "context\n");

		ste_hsi_init_registers(ste_hsi);
		ste_hsi_setup_registers(ste_hsi);
	}
}

static void ste_hsi_clks_free(struct ste_hsi_controller *ste_hsi)
{
	ste_hsi_clk_free(&ste_hsi->rx_clk);
	ste_hsi_clk_free(&ste_hsi->tx_clk);
	ste_hsi_clk_free(&ste_hsi->ssirx_clk);
	ste_hsi_clk_free(&ste_hsi->ssitx_clk);
}

static int ste_hsi_clock_enable(struct hsi_controller *hsi)
{
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	int err = 0;

	spin_lock_bh(&ste_hsi->ck_lock);
	if (ste_hsi->ck_refcount++ || ste_hsi->ck_on)
		goto out;

	err = clk_enable(ste_hsi->ssirx_clk);
	if (unlikely(err))
		goto out;

	err = clk_enable(ste_hsi->ssitx_clk);
	if (unlikely(err)) {
		clk_disable(ste_hsi->ssirx_clk);
		goto out;
	}

	err = clk_enable(ste_hsi->rx_clk);
	if (unlikely(err)) {
		clk_disable(ste_hsi->ssitx_clk);
		clk_disable(ste_hsi->ssirx_clk);
		goto out;
	}

	err = clk_enable(ste_hsi->tx_clk);
	if (unlikely(err)) {
		clk_disable(ste_hsi->rx_clk);
		clk_disable(ste_hsi->ssitx_clk);
		clk_disable(ste_hsi->ssirx_clk);
		goto out;
	}

	ste_hsi->ck_on = 1;
out:
	if (err)
		ste_hsi->ck_refcount--;

	spin_unlock_bh(&ste_hsi->ck_lock);

	return err;
}

static void ste_hsi_delayed_disable_clock(struct work_struct *work)
{
	struct ste_hsi_controller *ste_hsi;
	ste_hsi = container_of(work, struct ste_hsi_controller, clk_work.work);

	spin_lock_bh(&ste_hsi->ck_lock);

	/*
	 * If clock should not be off (enable clock called in meantime)
	 * or clock is already off nothing to do
	 */
	if (ste_hsi->ck_refcount || !ste_hsi->ck_on)
		goto out;

	if (readl(ste_hsi->tx_base + STE_HSI_TX_STATE) != STE_HSI_STATE_IDLE ||
	    readl(ste_hsi->rx_base + STE_HSI_RX_STATE)
	    != STE_HSI_STATE_IDLE ||
	    readl(ste_hsi->rx_base + STE_HSI_RX_BUFSTATE) != 0) {
		/* Try again later */
		int err = schedule_delayed_work(&ste_hsi->clk_work, HZ);
		if (err < 0)
			dev_err(ste_hsi->dev, "Error scheduling work\n");
		goto out;
	}

	/* Actual clocks disable */
	clk_disable(ste_hsi->tx_clk);
	clk_disable(ste_hsi->rx_clk);
	clk_disable(ste_hsi->ssitx_clk);
	clk_disable(ste_hsi->ssirx_clk);
	ste_hsi->ck_on = 0;

out:
	spin_unlock_bh(&ste_hsi->ck_lock);
}

static void ste_hsi_clock_disable(struct hsi_controller *hsi)
{
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);

	spin_lock_bh(&ste_hsi->ck_lock);

	/* Sanity check */
	if (ste_hsi->ck_refcount <= 0)
		WARN_ON(ste_hsi->ck_refcount <= 0);

	/* Need clock to be disable now? */
	if (--ste_hsi->ck_refcount)
		goto out;

	/*
	 * If receiver or transmitter is in the middle something delay clock off
	 */
	if (readl(ste_hsi->tx_base + STE_HSI_TX_STATE) != STE_HSI_STATE_IDLE ||
	    readl(ste_hsi->rx_base + STE_HSI_RX_STATE)
	    != STE_HSI_STATE_IDLE ||
	    readl(ste_hsi->rx_base + STE_HSI_RX_BUFSTATE) != 0) {
		int err = schedule_delayed_work(&ste_hsi->clk_work, HZ);
		if (err < 0)
			dev_err(&hsi->device, "Error scheduling work\n");

		goto out;
	}

	/* Actual clocks disabled */
	clk_disable(ste_hsi->tx_clk);
	clk_disable(ste_hsi->rx_clk);
	clk_disable(ste_hsi->ssitx_clk);
	clk_disable(ste_hsi->ssirx_clk);
	ste_hsi->ck_on = 0;

out:
	spin_unlock_bh(&ste_hsi->ck_lock);
}

static int ste_hsi_start_irq(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	u32 val;
	int err;

	err = ste_hsi_clock_enable(hsi);
	if (unlikely(err))
		return err;

	ste_hsi_context(ste_hsi);

	msg->actual_len = 0;
	msg->status = HSI_STATUS_PROCEEDING;

	if (msg->ttype == HSI_MSG_WRITE) {
		val = readl(ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM) |
		    (1 << msg->channel);
		writel(val, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM);
	} else {
		val = readl(ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM) |
		    (1 << msg->channel);
		writel(val, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM);

		val = readl(ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM) |
		    (1 << msg->channel);
		writel(val, ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM);
	}

	return 0;
}

static int ste_hsi_start_transfer(struct ste_hsi_port *ste_port,
				  struct list_head *queue);
#ifdef CONFIG_STE_DMA40
static void ste_hsi_dma_callback(void *dma_async_param)
{
	struct hsi_msg *msg = dma_async_param;
	struct hsi_controller *hsi = client_to_hsi(msg->cl);
	struct ste_hsi_port *ste_port = client_to_ste_port(msg->cl);
	struct ste_hsi_controller *ste_hsi = client_to_ste_controller(msg->cl);
	struct list_head *queue;
	struct dma_chan *chan;
	struct ste_hsi_channel_dma *hsi_dma_chan;
	char *dma_enable_address;
	enum dma_data_direction direction;
	u32 dma_mask;

	/* Message finished, remove from list and notify client */
	spin_lock_bh(&ste_hsi->lock);
	list_del(&msg->link);

	if (msg->ttype == HSI_MSG_WRITE) {
		queue = &ste_port->txqueue[msg->channel];
		direction = DMA_TO_DEVICE;
		dma_enable_address = ste_hsi->tx_base + STE_HSI_TX_DMAEN;
		hsi_dma_chan = &ste_port->tx_dma[msg->channel];
	} else {
		queue = &ste_port->rxqueue[msg->channel];
		direction = DMA_FROM_DEVICE;
		dma_enable_address = ste_hsi->rx_base + STE_HSI_RX_DMAEN;
		hsi_dma_chan = &ste_port->rx_dma[msg->channel];
	}

	dma_sync_sg_for_cpu(&hsi->device, msg->sgt.sgl,
			    msg->sgt.nents, direction);
	chan = hsi_dma_chan->dma_chan;

	/* disable DMA channel on HSI controller */
	dma_mask = readl(dma_enable_address);
	writel(dma_mask & ~(1 << msg->channel), dma_enable_address);

	hsi_dma_chan->desc = NULL;

	dma_unmap_sg(&hsi->device, msg->sgt.sgl, msg->sgt.nents, direction);

	msg->status = HSI_STATUS_COMPLETED;
	msg->actual_len = sg_dma_len(msg->sgt.sgl);

	spin_unlock_bh(&ste_hsi->lock);

	msg->complete(msg);

	ste_hsi_clock_disable(hsi);

	spin_lock_bh(&ste_hsi->lock);
	ste_hsi_start_transfer(ste_port, queue);
	spin_unlock_bh(&ste_hsi->lock);
}

static void dma_device_control(struct ste_hsi_channel_dma *chan,
			       enum dma_ctrl_cmd cmd, unsigned long arg)
{
	chan->dma_chan->device->device_control(chan->dma_chan, cmd, arg);
}

static void ste_hsi_terminate_dma_chan(struct ste_hsi_channel_dma *chan)
{
	if (chan->desc) {
		dma_device_control(chan, DMA_TERMINATE_ALL, 0);
		chan->desc = NULL;
	}
	chan->cookie = 0;
}

static void ste_hsi_terminate_dma(struct ste_hsi_port *ste_port)
{
	int i;

	for (i = 0; i < ste_port->channels; ++i) {
		ste_hsi_terminate_dma_chan(&ste_port->tx_dma[i]);
		ste_hsi_terminate_dma_chan(&ste_port->rx_dma[i]);
	}
}

static int ste_hsi_start_dma(struct hsi_msg *msg)
{
	struct hsi_controller *hsi = client_to_hsi(msg->cl);
	struct ste_hsi_port *ste_port = client_to_ste_port(msg->cl);
	struct ste_hsi_controller *ste_hsi = client_to_ste_controller(msg->cl);
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan;
	struct ste_hsi_channel_dma *hsi_dma_chan;
	char *dma_enable_address;
	enum dma_data_direction direction;
	u32 dma_mask;
	int err;

	err = ste_hsi_clock_enable(hsi);
	if (unlikely(err))
		return err;

	ste_hsi_context(ste_hsi);

	if (msg->ttype == HSI_MSG_WRITE) {
		direction = DMA_TO_DEVICE;
		dma_enable_address = ste_hsi->tx_base + STE_HSI_TX_DMAEN;
		hsi_dma_chan = &ste_port->tx_dma[msg->channel];
	} else {
		u32 val;
		direction = DMA_FROM_DEVICE;
		dma_enable_address = ste_hsi->rx_base + STE_HSI_RX_DMAEN;
		hsi_dma_chan = &ste_port->rx_dma[msg->channel];

		/* enable overrun for this channel */
		val = readl(ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM) |
		    (1 << msg->channel);
		writel(val, ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM);
	}

	chan = hsi_dma_chan->dma_chan;

	if (0 == dma_map_sg(&hsi->device, msg->sgt.sgl, msg->sgt.nents,
			    direction)) {
		dev_dbg(&hsi->device, "DMA map SG failed !\n");
		err = -ENOMEM;
		goto out;
	}
	/* Prepare the scatterlist */
	desc = chan->device->device_prep_slave_sg(chan,
						  msg->sgt.sgl,
						  msg->sgt.nents,
						  direction,
						  DMA_PREP_INTERRUPT |
						  DMA_CTRL_ACK);

	if (!desc) {
		dma_unmap_sg(&hsi->device, msg->sgt.sgl, msg->sgt.nents,
			     direction);
		/* "Complete" DMA (errorpath) */
		ste_hsi_terminate_dma_chan(hsi_dma_chan);
		err = -EBUSY;
		goto out;
	}
	desc->callback = ste_hsi_dma_callback;
	desc->callback_param = msg;
	hsi_dma_chan->cookie = desc->tx_submit(desc);
	hsi_dma_chan->desc = desc;

	/* Fire the DMA transaction */
	chan->device->device_issue_pending(chan);

	/* Enable DMA channel on HSI controller */
	dma_mask = readl(dma_enable_address);
	writel(dma_mask | 1 << msg->channel, dma_enable_address);

out:
	if (unlikely(err))
		ste_hsi_clock_disable(hsi);

	return err;
}

static void __init ste_hsi_init_dma(struct ste_hsi_platform_data *data,
				    struct hsi_controller *hsi)
{
	struct hsi_port *port;
	struct ste_hsi_port *ste_port;
	struct ste_hsi_controller *ste_hsi = hsi_to_ste_controller(hsi);
	dma_cap_mask_t mask;
	int i, ch;

	ste_hsi->use_dma = 1;
	/* Try to acquire a generic DMA engine slave channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	for (i = 0; i < hsi->num_ports; ++i) {
		port = &hsi->port[i];
		ste_port = hsi_port_drvdata(port);

		for (ch = 0; ch < STE_HSI_MAX_CHANNELS; ++ch) {
			ste_port->tx_dma[ch].dma_chan =
			    dma_request_channel(mask,
						data->port_cfg[i].dma_filter,
						&data->port_cfg[i].
						dma_tx_cfg[ch]);

			ste_port->rx_dma[ch].dma_chan =
			    dma_request_channel(mask,
						data->port_cfg[i].dma_filter,
						&data->port_cfg[i].
						dma_rx_cfg[ch]);
		}
	}
}

static int ste_hsi_setup_dma(struct hsi_client *cl)
{
	int i;
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct dma_slave_config rx_conf = {
		.src_addr = 0,	/* dynamic data */
		.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.direction = DMA_FROM_DEVICE,
		.src_maxburst = STE_HSI_DMA_MAX_BURST,
	};
	struct dma_slave_config tx_conf = {
		.dst_addr = 0,	/* dynamic data */
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.direction = DMA_TO_DEVICE,
		.dst_maxburst = STE_HSI_DMA_MAX_BURST,
	};

	if (!ste_hsi->use_dma)
		return 0;

	for (i = 0; i < ste_port->channels; ++i) {
		struct dma_chan *chan;

		chan = ste_port->tx_dma[i].dma_chan;
		tx_conf.dst_addr = (dma_addr_t) ste_hsi->tx_dma_base +
		    STE_HSI_TX_BUFFERX + 4 * i;
		chan->device->device_control(chan,
					     DMA_SLAVE_CONFIG,
					     (unsigned long)&tx_conf);

		chan = ste_port->rx_dma[i].dma_chan;
		rx_conf.src_addr = (dma_addr_t) ste_hsi->rx_dma_base +
		    STE_HSI_RX_BUFFERX + 4 * i;
		chan->device->device_control(chan,
					     DMA_SLAVE_CONFIG,
					     (unsigned long)&rx_conf);
	}

	return 0;
}

#else
#define ste_hsi_init_dma(data, hsi) do { } while (0)
#define ste_hsi_start_dma ste_hsi_start_irq
#define ste_hsi_terminate_dma(ste_port) do { } while (0)
#define ste_hsi_setup_dma(cl) do { } while (0)
#endif

static int ste_hsi_start_transfer(struct ste_hsi_port *ste_port,
				  struct list_head *queue)
{
	struct hsi_msg *msg;
	int err;

	if (list_empty(queue))
		return 0;

	msg = list_first_entry(queue, struct hsi_msg, link);
	if (msg->status != HSI_STATUS_QUEUED)
		return 0;

	msg->actual_len = 0;
	msg->status = HSI_STATUS_PROCEEDING;

	if (ste_port_to_ste_controller(ste_port)->use_dma)
		err = ste_hsi_start_dma(msg);
	else
		err = ste_hsi_start_irq(msg);

	return err;
}

static void ste_hsi_receive_data(struct hsi_port *port, unsigned int channel)
{
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct list_head *queue = &ste_port->rxqueue[channel];
	struct hsi_msg *msg;
	char *bufferx;
	u8 *buf;
	int span;

	spin_lock_bh(&ste_hsi->lock);

	if (list_empty(queue))
		goto out;

	msg = list_first_entry(queue, struct hsi_msg, link);
	if ((!msg->sgt.nents) || (!msg->sgt.sgl->length)) {
		msg->actual_len = 0;
		msg->status = HSI_STATUS_PENDING;
	}

	if (msg->status == HSI_STATUS_PROCEEDING && msg->ttype == HSI_MSG_READ) {
		unsigned char len;
		bufferx = ste_hsi->rx_base + STE_HSI_RX_BUFFERX + 4 * channel;

		len = readl(ste_hsi->rx_base + STE_HSI_RX_GAUGEX + 4 * channel);
		buf = sg_virt(msg->sgt.sgl);
		buf += msg->actual_len;
		while (len--) {
			*(u32 *) buf = readl(bufferx);
			buf += 4;
			msg->actual_len += 4;
			if (msg->actual_len >= msg->sgt.sgl->length) {
				msg->status = HSI_STATUS_COMPLETED;
				break;
			}
		}
	}

	/* re-enable interrupt by watermark manipulation */
	span = readl(ste_hsi->rx_base + STE_HSI_RX_SPANX + 4 * channel);
	writel(span, ste_hsi->rx_base + STE_HSI_RX_WATERMARKX + 4 * channel);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_WATERMARKX + 4 * channel);

	/*
	 * If message was not transmitted completely enable interrupt for
	 * further work
	 */
	if (msg->status == HSI_STATUS_PROCEEDING) {
		u32 val;
		val = readl(ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM) |
		    (1 << channel);
		writel(val, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM);
		goto out;
	}

	/* Message finished, remove from list and notify client */
	list_del(&msg->link);
	spin_unlock_bh(&ste_hsi->lock);
	msg->complete(msg);

	ste_hsi_clock_disable(hsi);

	spin_lock_bh(&ste_hsi->lock);

	ste_hsi_start_transfer(ste_port, queue);
out:
	spin_unlock_bh(&ste_hsi->lock);
}

static void ste_hsi_transmit_data(struct hsi_port *port, unsigned int channel)
{
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct list_head *queue = &ste_port->txqueue[channel];
	struct hsi_msg *msg;
	u8 *buf;
	int span;

	if (list_empty(queue))
		return;

	spin_lock_bh(&ste_hsi->lock);
	msg = list_first_entry(queue, struct hsi_msg, link);
	if ((!msg->sgt.nents) || (!msg->sgt.sgl->length)) {
		msg->actual_len = 0;
		msg->status = HSI_STATUS_PENDING;
	}

	if (msg->status == HSI_STATUS_PROCEEDING &&
		msg->ttype == HSI_MSG_WRITE) {
		unsigned char free_space;

		free_space = readl(ste_hsi->tx_base +
				   STE_HSI_TX_GAUGEX + 4 * channel);
		buf = sg_virt(msg->sgt.sgl);
		buf += msg->actual_len;
		while (free_space--) {
			writel(*(u32 *) buf, ste_hsi->tx_base +
			       STE_HSI_TX_BUFFERX + 4 * channel);
			buf += 4;
			msg->actual_len += 4;
			if (msg->actual_len >= msg->sgt.sgl->length) {
				msg->status = HSI_STATUS_COMPLETED;
				break;
			}
		}
	}

	span = readl(ste_hsi->tx_base + STE_HSI_TX_SPANX + 4 * channel);
	writel(span, ste_hsi->tx_base + STE_HSI_TX_WATERMARKX + 4 * channel);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKX + 4 * channel);

	if (msg->status == HSI_STATUS_PROCEEDING) {
		u32 val;
		val = readl(ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM) |
		    (1 << channel);
		writel(val, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM);
		goto out;
	}

	list_del(&msg->link);
	spin_unlock_bh(&ste_hsi->lock);
	msg->complete(msg);

	ste_hsi_clock_disable(hsi);

	spin_lock_bh(&ste_hsi->lock);
	ste_hsi_start_transfer(ste_port, queue);
out:
	spin_unlock_bh(&ste_hsi->lock);
}

static void ste_hsi_cawake_tasklet(unsigned long data)
{
	struct hsi_port *port = (struct hsi_port *)data;
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	u32 prcm_line_value;
	int level;

	prcm_line_value = prcmu_read(DB8500_PRCM_LINE_VALUE);
	level = (prcm_line_value & DB8500_PRCM_LINE_VALUE_HSI_CAWAKE0) ? 1 : 0;

	dev_info(ste_hsi->dev, "cawake %s\n", level ? "HIGH" : "LOW");
	hsi_event(hsi->port, level ? HSI_EVENT_START_RX : HSI_EVENT_STOP_RX);
	enable_irq(ste_port->cawake_irq);
}

static irqreturn_t ste_hsi_cawake_isr(int irq, void *data)
{
	struct hsi_port *port = data;

	/* IRQ processed only if device initialized */
	if ((port->device.parent) && (data)) {
		struct ste_hsi_port *ste_port = hsi_port_drvdata(port);

		disable_irq_nosync(irq);
		tasklet_hi_schedule(&ste_port->cawake_tasklet);
	}

	return IRQ_HANDLED;
}

static void ste_hsi_rx_tasklet(unsigned long data)
{
	struct hsi_port *port = (struct hsi_port *)data;
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	u32 irq_status, irq_mask;
	unsigned int i;

	irq_status = readl(ste_hsi->rx_base + STE_HSI_RX_WATERMARKMIS);
	if (!irq_status)
		goto out;

	irq_mask = readl(ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM);
	writel(irq_mask & ~irq_status,
	       ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM);
	writel(irq_mask, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIC);

	for (i = 0; i < STE_HSI_MAX_CHANNELS; ++i) {
		if (irq_status & (1 << i))
			ste_hsi_receive_data(port, i);
	}
out:
	enable_irq(ste_port->rx_irq);
}

static irqreturn_t ste_hsi_rx_isr(int irq, void *data)
{
	struct hsi_port *port = data;
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);

	disable_irq_nosync(irq);
	tasklet_hi_schedule(&ste_port->rx_tasklet);

	return IRQ_HANDLED;
}

static irqreturn_t ste_hsi_tx_isr(int irq, void *data)
{
	struct hsi_port *port = data;
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);

	disable_irq_nosync(irq);
	tasklet_hi_schedule(&ste_port->tx_tasklet);

	return IRQ_HANDLED;
}

static void ste_hsi_tx_tasklet(unsigned long data)
{
	struct hsi_port *port = (struct hsi_port *)data;
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	u32 irq_status, irq_mask;
	unsigned int i;

	irq_status = readl(ste_hsi->tx_base + STE_HSI_TX_WATERMARKMIS);
	if (!irq_status)
		goto out;

	irq_mask = readl(ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM);
	writel(irq_mask & ~irq_status,
	       ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM);
	writel(irq_mask, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIC);

	for (i = 0; i < STE_HSI_MAX_CHANNELS; ++i) {
		if (irq_status & (1 << i))
			ste_hsi_transmit_data(port, i);
	}
out:
	enable_irq(ste_port->tx_irq);
}

static void ste_hsi_break_complete(struct hsi_port *port,
				   struct ste_hsi_controller *ste_hsi)
{
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_msg *msg, *tmp;
	u32 mask;

	dev_dbg(port->device.parent, "HWBREAK received\n");

	spin_lock_bh(&ste_hsi->lock);

	mask = readl(ste_hsi->rx_base + STE_HSI_RX_EXCEPIM);
	writel(mask & ~STE_HSI_EXCEP_BREAK,
	       ste_hsi->rx_base + STE_HSI_RX_EXCEPIM);

	spin_unlock_bh(&ste_hsi->lock);

	list_for_each_entry_safe(msg, tmp, &ste_port->brkqueue, link) {
		msg->status = HSI_STATUS_COMPLETED;
		list_del(&msg->link);
		msg->complete(msg);
	}
}

static void ste_hsi_error(struct hsi_port *port)
{
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_msg *msg;
	unsigned int i;

	for (i = 0; i < ste_port->channels; i++) {
		if (list_empty(&ste_port->rxqueue[i]))
			continue;
		msg = list_first_entry(&ste_port->rxqueue[i], struct hsi_msg,
				       link);
		list_del(&msg->link);
		msg->status = HSI_STATUS_ERROR;
		msg->complete(msg);
		/* Now restart queued reads if any */
		ste_hsi_start_transfer(ste_port, &ste_port->rxqueue[i]);
	}
}

static void ste_hsi_exception_tasklet(unsigned long data)
{
	struct hsi_port *port = (struct hsi_port *)data;
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	u32 error_status;
	u32 error_interrupts;

	error_status = readl(ste_hsi->rx_base + STE_HSI_RX_EXCEP);
	/*
	 * sometimes interrupt that cause running this tasklet is already
	 * inactive so base handling of exception on masked interrupt status
	 * not on exception state register.
	 */
	error_interrupts = readl(ste_hsi->rx_base + STE_HSI_RX_EXCEPMIS);

	if (error_interrupts & STE_HSI_EXCEP_BREAK)
		ste_hsi_break_complete(port, ste_hsi);

	if (error_interrupts & STE_HSI_EXCEP_TIMEOUT)
		dev_err(&hsi->device, "timeout exception occurred\n");
	if (error_interrupts & STE_HSI_EXCEP_OVERRUN)
		dev_err(&hsi->device, "overrun exception occurred\n");
	if (error_interrupts & STE_HSI_EXCEP_PARITY)
		dev_err(&hsi->device, "parity exception occurred\n");

	if (error_interrupts & ~STE_HSI_EXCEP_BREAK)
		ste_hsi_error(port);

	/* Acknowledge exception interrupts */
	writel(error_status, ste_hsi->rx_base + STE_HSI_RX_ACK);

	enable_irq(ste_port->excep_irq);
}

static irqreturn_t ste_hsi_exception_isr(int irq, void *data)
{
	struct hsi_port *port = data;
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);

	disable_irq_nosync(irq);
	tasklet_hi_schedule(&ste_port->exception_tasklet);

	return IRQ_HANDLED;
}

static void ste_hsi_overrun_tasklet(unsigned long data)
{
	struct hsi_controller *hsi = (struct hsi_controller *)data;
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct hsi_port *hsi_port = &hsi->port[0];
	struct ste_hsi_port *ste_port = hsi_port_drvdata(hsi_port);
	struct hsi_msg *msg;

	unsigned int channel;
	u8 rised_overrun;
	u8 mask;
	u8 blocked = 0;

	rised_overrun = (u8) readl(ste_hsi->rx_base + STE_HSI_RX_OVERRUNMIS);
	mask = rised_overrun;
	for (channel = 0; mask; ++channel, mask >>= 1) {
		if (!(mask & 1))
			continue;

		do {
			/*
			 * No more messages, block interrupt
			 */
			if (list_empty(&ste_port->rxqueue[channel])) {
				blocked |= 1 << channel;
				break;
			}
			/*
			 * Complete with error
			 */
			msg = list_first_entry(&ste_port->rxqueue[channel],
					       struct hsi_msg, link);
			list_del(&msg->link);
			msg->status = HSI_STATUS_ERROR;
			msg->complete(msg);

			/*
			 * Now restart queued reads if any
			 * If start_transfer fails, try with next message
			 */
			if (ste_hsi_start_transfer(ste_port,
						   &ste_port->rxqueue[channel]))
				continue;
		} while (0);
	}

	/* Overrun acknowledge */
	writel(rised_overrun, ste_hsi->rx_base + STE_HSI_RX_OVERRUNACK);
	writel(~blocked & readl(ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM),
	       ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM);

	/*
	 * Enable all that should not be blocked
	 */
	mask = rised_overrun & ~blocked;
	for (channel = 0; mask; ++channel, mask >>= 1)
		enable_irq(ste_hsi->overrun_irq[channel]);
}

static irqreturn_t ste_hsi_overrun_isr(int irq, void *data)
{
	struct hsi_port *port = data;
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);

	disable_irq_nosync(irq);
	tasklet_hi_schedule(&ste_port->overrun_tasklet);

	return IRQ_HANDLED;
}

static void __init ste_hsi_queues_init(struct ste_hsi_port *ste_port)
{
	unsigned int ch;

	for (ch = 0; ch < STE_HSI_MAX_CHANNELS; ch++) {
		INIT_LIST_HEAD(&ste_port->txqueue[ch]);
		INIT_LIST_HEAD(&ste_port->rxqueue[ch]);
	}
	INIT_LIST_HEAD(&ste_port->brkqueue);
}

static int __init ste_hsi_get_iomem(struct platform_device *pdev,
				    const char *res_name,
				    unsigned char __iomem **base,
				    dma_addr_t *phy)
{
	struct resource *mem;
	struct resource *ioarea;

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, res_name);
	if (!mem) {
		dev_err(&pdev->dev, "IO memory region missing!\n");
		return -ENXIO;
	}

	ioarea = devm_request_mem_region(&pdev->dev, mem->start,
					 resource_size(mem),
					 dev_name(&pdev->dev));
	if (!ioarea) {
		dev_err(&pdev->dev, "Can't request IO memory region!\n");
		return -ENXIO;
	}

	*base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!base) {
		dev_err(&pdev->dev, "%s IO remap failed!\n", mem->name);
		return -ENXIO;
	}
	if (phy)
		*phy = (dma_addr_t) mem->start;

	return 0;
}

static int __init ste_hsi_acwake_gpio_init(struct platform_device *pdev,
					   int *gpio)
{
	int err = 0;
	const char *gpio_name = "hsi0_acwake";
	struct resource *resource;

	resource = platform_get_resource_byname(pdev, IORESOURCE_IO, gpio_name);
	if (unlikely(!resource)) {
		dev_err(&pdev->dev, "hsi0_acwake does not exist\n");
		return -EINVAL;
	}

	*gpio = resource->start;
	err = gpio_request(*gpio, gpio_name);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't request GPIO %d\n", *gpio);
		return err;
	}

	/* Initial level set to 0 (LOW) */
	err = gpio_direction_output(*gpio, 0);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't init GPIO %d\n", *gpio);
		gpio_free(*gpio);
	}

	return err;
}

static int __init ste_hsi_get_irq(struct platform_device *pdev,
				  const char *res_name,
				  irqreturn_t(*isr) (int, void *), void *data,
				  int *irq_number)
{
	struct resource *irq;
	int err;

	irq = platform_get_resource_byname(pdev, IORESOURCE_IRQ, res_name);
	if (!irq) {
		dev_err(&pdev->dev, "IO memory region missing!\n");
		return -ENXIO;
	}

	err = devm_request_irq(&pdev->dev, irq->start, isr,
			       IRQF_DISABLED, irq->name, data);
	if (err)
		dev_err(&pdev->dev, "%s IRQ request failed!\n", irq->name);

	if (irq_number)
		*irq_number = irq->start;

	return err;
}

static void ste_hsi_flush_queue(struct list_head *queue, struct hsi_client *cl)
{
	struct list_head *node, *tmp;
	struct hsi_msg *msg;

	list_for_each_safe(node, tmp, queue) {
		msg = list_entry(node, struct hsi_msg, link);
		if ((cl) && (cl != msg->cl))
			continue;
		list_del(node);

		if (msg->destructor)
			msg->destructor(msg);
		else
			hsi_free_msg(msg);
	}
}

static int ste_hsi_async_break(struct hsi_msg *msg)
{
	struct hsi_port *port = hsi_get_port(msg->cl);
	struct ste_hsi_port *ste_port = hsi_to_ste_port(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	int err;

	err = ste_hsi_clock_enable(hsi);
	if (unlikely(err))
		return err;

	if (msg->ttype == HSI_MSG_WRITE) {
		if (port->tx_cfg.mode != HSI_MODE_FRAME) {
			err = -EINVAL;
			goto out;
		}
		writel(1, ste_hsi->tx_base + STE_HSI_TX_BREAK);
		msg->status = HSI_STATUS_COMPLETED;
		msg->complete(msg);
	} else {
		u32 mask;
		if (port->rx_cfg.mode != HSI_MODE_FRAME) {
			err = -EINVAL;
			goto out;
		}
		spin_lock_bh(&ste_hsi->lock);
		msg->status = HSI_STATUS_PROCEEDING;
		mask = readl(ste_hsi->rx_base + STE_HSI_RX_EXCEPIM);
		/* Enable break exception on controller */
		if (!(mask & STE_HSI_EXCEP_BREAK))
			writel(mask | STE_HSI_EXCEP_BREAK,
			       ste_hsi->rx_base + STE_HSI_RX_EXCEPIM);

		list_add_tail(&msg->link, &ste_port->brkqueue);
		spin_unlock_bh(&ste_hsi->lock);
	}

out:
	ste_hsi_clock_disable(hsi);
	return err;
}

static int ste_hsi_async(struct hsi_msg *msg)
{
	struct ste_hsi_controller *ste_hsi;
	struct ste_hsi_port *ste_port;
	struct list_head *queue;
	int err = 0;

	if (unlikely(!msg))
		return -ENOSYS;

	if (msg->sgt.nents > 1)
		return -ENOSYS;

	if (unlikely(msg->break_frame))
		return ste_hsi_async_break(msg);

	ste_port = client_to_ste_port(msg->cl);
	ste_hsi = client_to_ste_controller(msg->cl);

	if (msg->ttype == HSI_MSG_WRITE) {
		/* TX transfer */
		BUG_ON(msg->channel >= ste_port->channels);
		queue = &ste_port->txqueue[msg->channel];
	} else {
		/* RX transfer */
		queue = &ste_port->rxqueue[msg->channel];
	}

	spin_lock_bh(&ste_hsi->lock);
	list_add_tail(&msg->link, queue);
	msg->status = HSI_STATUS_QUEUED;

	err = ste_hsi_start_transfer(ste_port, queue);
	if (err)
		list_del(&msg->link);

	spin_unlock_bh(&ste_hsi->lock);

	return err;
}

static int ste_hsi_setup(struct hsi_client *cl)
{
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	int err;
	u32 div = 0;
	int ch;

	if (ste_hsi->regulator)
		regulator_enable(ste_hsi->regulator);

	err = ste_hsi_clock_enable(hsi);
	if (unlikely(err))
		return err;

	if (cl->tx_cfg.speed) {
		div = clk_get_rate(ste_hsi->tx_clk) / 1000 / cl->tx_cfg.speed;
		if (div)
			--div;
	}

	if (!ste_hsi->context)
		ste_hsi->context = kzalloc(sizeof(struct ste_hsi_hw_context), GFP_KERNEL);

	if (!ste_hsi->context) {
		dev_err(ste_hsi->dev, "Not enough memory for context!\n");
		return -ENOMEM;
	} else {
		/* Save HSI context */
		ste_hsi->context->tx_mode = cl->tx_cfg.mode;
		ste_hsi->context->tx_divisor = div;
		ste_hsi->context->tx_channels = cl->tx_cfg.channels;
		ste_hsi->context->tx_priority = 0;
		if (HSI_ARB_PRIO == cl->tx_cfg.arb_mode)
			for (ch = 0; ch < STE_HSI_MAX_CHANNELS; ch++)
				if (cl->tx_cfg.ch_prio[ch])
					ste_hsi->context->tx_priority |=
								(1 << ch);

		if ((HSI_FLOW_PIPE == cl->rx_cfg.flow) &&
			(HSI_MODE_FRAME == cl->rx_cfg.mode))
			ste_hsi->context->rx_mode = STE_HSI_MODE_PIPELINED;
		else
			ste_hsi->context->rx_mode = cl->rx_cfg.mode;

		ste_hsi->context->rx_channels = cl->rx_cfg.channels;
	}

	port->tx_cfg = cl->tx_cfg;
	port->rx_cfg = cl->rx_cfg;

	ste_hsi_setup_registers(ste_hsi);

	ste_port->channels = max(cl->tx_cfg.channels, cl->rx_cfg.channels);

	ste_hsi_setup_dma(cl);

	ste_hsi_clock_disable(hsi);

	if (ste_hsi->regulator)
		regulator_disable(ste_hsi->regulator);

	return err;
}

static int ste_hsi_flush(struct hsi_client *cl)
{
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	int i;

	ste_hsi_clock_enable(hsi);

	/* Enter sleep mode */
	writel(STE_HSI_MODE_SLEEP, ste_hsi->rx_base + STE_HSI_RX_MODE);

	/* Disable DMA, and terminate all outstanding jobs */
	writel(0, ste_hsi->tx_base + STE_HSI_TX_DMAEN);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_DMAEN);
	ste_hsi_terminate_dma(ste_port);

	/* Flush HSIT buffers */
	writel(0, ste_hsi->tx_base + STE_HSI_TX_STATE);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_BUFSTATE);

	/* Flush HSIR pipeline and channel buffers */
	writel(0, ste_hsi->rx_base + STE_HSI_RX_STATE);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_PIPEGAUGE);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_BUFSTATE);

	/* Flush all errors */
	writel(0, ste_hsi->rx_base + STE_HSI_RX_EXCEP);

	/* Clear interrupts */
	writel(0, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIM);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_WATERMARKIC);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIM);
	writel(0, ste_hsi->tx_base + STE_HSI_TX_WATERMARKIC);
	writel(0xFF, ste_hsi->rx_base + STE_HSI_RX_OVERRUNACK);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_OVERRUNIM);
	writel(0, ste_hsi->rx_base + STE_HSI_RX_EXCEPIM);
	writel(0x0F, ste_hsi->rx_base + STE_HSI_RX_ACK);

	/* Dequeue all pending requests */
	for (i = 0; i < ste_port->channels; i++) {
		/* Release write clocks */
		if (!list_empty(&ste_port->txqueue[i]))
			ste_hsi_clock_disable(hsi);
		if (!list_empty(&ste_port->rxqueue[i]))
			ste_hsi_clock_disable(hsi);
		ste_hsi_flush_queue(&ste_port->txqueue[i], NULL);
		ste_hsi_flush_queue(&ste_port->rxqueue[i], NULL);
	}
	ste_hsi_flush_queue(&ste_port->brkqueue, NULL);

	ste_hsi_clock_disable(hsi);

	return 0;
}

static int ste_hsi_start_tx(struct hsi_client *cl)
{
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);

	if (ste_hsi->regulator)
		regulator_enable(ste_hsi->regulator);

	gpio_set_value(ste_port->acwake_gpio, 1); /* HIGH */

	return 0;
}

static int ste_hsi_stop_tx(struct hsi_client *cl)
{
	struct hsi_port *port = to_hsi_port(cl->device.parent);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);
	struct hsi_controller *hsi = to_hsi_controller(port->device.parent);
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);

	gpio_set_value(ste_port->acwake_gpio, 0); /* LOW */

	if (ste_hsi->regulator)
		regulator_disable(ste_hsi->regulator);

	return 0;
}

static int ste_hsi_release(struct hsi_client *cl)
{
	int err;
	struct ste_hsi_controller *ste_hsi = client_to_ste_controller(cl);

	err = ste_hsi_flush(cl);
	cancel_delayed_work(&ste_hsi->clk_work);

	return 0;
}

static int ste_hsi_ports_init(struct hsi_controller *hsi,
			      struct platform_device *pdev)
{
	struct hsi_port *port;
	struct ste_hsi_port *ste_port;
	unsigned int i;
	char irq_name[20];
	int err;

	for (i = 0; i < hsi->num_ports; i++) {
		ste_port = devm_kzalloc(&pdev->dev, sizeof *ste_port,
					GFP_KERNEL);
		if (!ste_port)
			return -ENOMEM;

		port = &hsi->port[i];
		port->async = ste_hsi_async;
		port->setup = ste_hsi_setup;
		port->flush = ste_hsi_flush;
		port->start_tx = ste_hsi_start_tx;
		port->stop_tx = ste_hsi_stop_tx;
		port->release = ste_hsi_release;
		hsi_port_set_drvdata(port, ste_port);
		ste_port->dev = &port->device;

		err = ste_hsi_acwake_gpio_init(pdev, &ste_port->acwake_gpio);
		if (err)
			return err;

		sprintf(irq_name, "hsi0_cawake");
		err = ste_hsi_get_irq(pdev, irq_name, ste_hsi_cawake_isr, port,
				      &ste_port->cawake_irq);
		if (err)
			return err;

		sprintf(irq_name, "hsi_rx_irq%d", i);
		err = ste_hsi_get_irq(pdev, irq_name, ste_hsi_rx_isr, port,
				      &ste_port->rx_irq);
		if (err)
			return err;

		sprintf(irq_name, "hsi_tx_irq%d", i);
		err = ste_hsi_get_irq(pdev, irq_name, ste_hsi_tx_isr, port,
				      &ste_port->tx_irq);
		if (err)
			return err;

		tasklet_init(&ste_port->cawake_tasklet, ste_hsi_cawake_tasklet,
			     (unsigned long)port);

		tasklet_init(&ste_port->rx_tasklet, ste_hsi_rx_tasklet,
			     (unsigned long)port);

		tasklet_init(&ste_port->tx_tasklet, ste_hsi_tx_tasklet,
			     (unsigned long)port);

		tasklet_init(&ste_port->exception_tasklet,
			     ste_hsi_exception_tasklet, (unsigned long)port);

		tasklet_init(&ste_port->overrun_tasklet,
			     ste_hsi_overrun_tasklet, (unsigned long)port);

		sprintf(irq_name, "hsi_rx_excep%d", i);
		err = ste_hsi_get_irq(pdev, irq_name, ste_hsi_exception_isr,
				      port, &ste_port->excep_irq);
		if (err)
			return err;

		ste_hsi_queues_init(ste_port);
	}
	return 0;
}

static int __init ste_hsi_hw_init(struct hsi_controller *hsi)
{
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	int err;

	err = ste_hsi_clock_enable(hsi);
	if (unlikely(err))
		return err;

	ste_hsi_init_registers(ste_hsi);

	ste_hsi_clock_disable(hsi);

	return err;
}

static int __init ste_hsi_add_controller(struct hsi_controller *hsi,
					 struct platform_device *pdev)
{
	struct ste_hsi_controller *ste_hsi;
	char overrun_name[] = "hsi_rx_overrun_chxxx";
	unsigned char i;
	int err;
	unsigned long rate;

	ste_hsi = kzalloc(sizeof(struct ste_hsi_controller), GFP_KERNEL);
	if (!ste_hsi) {
		dev_err(&pdev->dev, "Not enough memory for ste_hsi!\n");
		return -ENOMEM;
	}

	spin_lock_init(&ste_hsi->lock);
	spin_lock_init(&ste_hsi->ck_lock);
	INIT_DELAYED_WORK(&ste_hsi->clk_work, ste_hsi_delayed_disable_clock);

	hsi->id = pdev->id;
	hsi->device.parent = &pdev->dev;
	dev_set_name(&hsi->device, "ste-hsi.%d", hsi->id);
	ste_hsi->dev = &hsi->device;
	hsi_controller_set_drvdata(hsi, ste_hsi);

	/* Get and enable regulator */
	ste_hsi->regulator = regulator_get(&pdev->dev, "v-hsi");
	if (IS_ERR(ste_hsi->regulator)) {
		dev_err(&pdev->dev, "could not get v-hsi regulator\n");
		ste_hsi->regulator = NULL;
	} else {
		regulator_enable(ste_hsi->regulator);
	}

	/* Get and reserve resources for receiver */
	err = ste_hsi_get_iomem(pdev, "hsi_rx_base", &ste_hsi->rx_base,
				&ste_hsi->rx_dma_base);
	if (err)
		goto err_free_mem;
	dev_info(&pdev->dev, "hsi_rx_base = %p\n", ste_hsi->rx_base);

	/* Get and reserve resources for transmitter */
	err = ste_hsi_get_iomem(pdev, "hsi_tx_base", &ste_hsi->tx_base,
				&ste_hsi->tx_dma_base);
	if (err)
		goto err_free_mem;
	dev_info(&pdev->dev, "hsi_tx_base = %p\n", ste_hsi->tx_base);

	/* Get HSIT HSITXCLK clock */
	ste_hsi->tx_clk = clk_get(&pdev->dev, "hsit_hsitxclk");
	if (IS_ERR(ste_hsi->tx_clk)) {
		dev_err(&hsi->device, "Couldn't get HSIT HSITXCLK clock\n");
		err = PTR_ERR(ste_hsi->tx_clk);
		goto err_free_mem;
	}

	/* Get HSIR HSIRXCLK clock */
	ste_hsi->rx_clk = clk_get(&pdev->dev, "hsir_hsirxclk");
	if (IS_ERR(ste_hsi->rx_clk)) {
		dev_err(&hsi->device, "Couldn't get HSIR HSIRXCLK clock\n");
		err = PTR_ERR(ste_hsi->rx_clk);
		goto err_clk_free;
	}

	/* Get HSIT HCLK clock */
	ste_hsi->ssitx_clk = clk_get(&pdev->dev, "hsit_hclk");
	if (IS_ERR(ste_hsi->ssitx_clk)) {
		dev_err(&hsi->device, "Couldn't get HSIT HCLK clock\n");
		err = PTR_ERR(ste_hsi->ssitx_clk);
		goto err_clk_free;
	}

	/* Get HSIR HCLK clock */
	ste_hsi->ssirx_clk = clk_get(&pdev->dev, "hsir_hclk");
	if (IS_ERR(ste_hsi->ssirx_clk)) {
		dev_err(&hsi->device, "Couldn't get HSIR HCLK clock\n");
		err = PTR_ERR(ste_hsi->ssirx_clk);
		goto err_clk_free;
	}

	/* Set HSITXCLK rate to 100 MHz */
	rate = clk_round_rate(ste_hsi->tx_clk, 100000000);
	err = clk_set_rate(ste_hsi->tx_clk, rate);
	if (unlikely(err)) {
		dev_err(&hsi->device, "Couldn't set HSIT clock rate\n");
		goto err_clk_free;
	}

	/* Set HSIRXCLK rate to 200 MHz */
	rate = clk_round_rate(ste_hsi->rx_clk, 200000000);
	err = clk_set_rate(ste_hsi->rx_clk, rate);
	if (unlikely(err)) {
		dev_err(&hsi->device, "Couldn't set HSIR clock rate\n");
		goto err_clk_free;
	}

	err = ste_hsi_clock_enable(hsi);
	if (unlikely(err))
		goto err_clk_free;

	/* Check if controller is at specified address */
	if (compare_periphid(ste_hsir_periphid,
			     (u32 *) (ste_hsi->rx_base + 0xFE0), 8)) {
		dev_err(&pdev->dev, "No hsir controller at = %p\n",
			ste_hsi->rx_base);
		err = -ENXIO;
		goto err_clk_free;
	}

	/* Check if controller is at specified address */
	if (compare_periphid(ste_hsit_periphid,
			     (u32 *) (ste_hsi->tx_base + 0xFE0), 8)) {
		dev_err(&pdev->dev, "No hsit controller at = %p\n",
			ste_hsi->tx_base);
		err = -ENXIO;
		goto err_clk_free;
	}
	ste_hsi_clock_disable(hsi);

	err = ste_hsi_hw_init(hsi);
	if (err) {
		dev_err(&pdev->dev, "Failed to init HSI controller!\n");
		goto err_clk_free;
	}

	for (i = 0; i < STE_HSI_MAX_CHANNELS; i++) {
		sprintf(overrun_name, "hsi_rx_overrun_ch%d", i);
		err = ste_hsi_get_irq(pdev, overrun_name, ste_hsi_overrun_isr,
				      hsi, &ste_hsi->overrun_irq[i]);
		if (err)
			goto err_clk_free;
	}

	err = ste_hsi_ports_init(hsi, pdev);
	if (err)
		goto err_clk_free;

	err = hsi_register_controller(hsi);

	if (ste_hsi->regulator)
		regulator_disable(ste_hsi->regulator);

	if (err)
		goto err_clk_free;

	return 0;

err_clk_free:
	ste_hsi_clks_free(ste_hsi);
err_free_mem:
	kfree(ste_hsi);
	return err;
}

static int ste_hsi_remove_controller(struct hsi_controller *hsi,
				     struct platform_device *pdev)
{
	struct ste_hsi_controller *ste_hsi = hsi_controller_drvdata(hsi);
	struct hsi_port *port = to_hsi_port(&pdev->dev);
	struct ste_hsi_port *ste_port = hsi_port_drvdata(port);

	if (ste_hsi->regulator)
		regulator_put(ste_hsi->regulator);

	gpio_free(ste_port->acwake_gpio);

	ste_hsi_clks_free(ste_hsi);
	hsi_unregister_controller(hsi);

	kfree(ste_hsi->context);
	kfree(ste_hsi);

	return 0;
}

static int __init ste_hsi_probe(struct platform_device *pdev)
{
	struct hsi_controller *hsi;
	struct ste_hsi_platform_data *pdata = pdev->dev.platform_data;
	int err;

	if (!pdata) {
		dev_err(&pdev->dev, "No HSI platform data!\n");
		return -EINVAL;
	}

	hsi = hsi_alloc_controller(pdata->num_ports, GFP_KERNEL);
	if (!hsi) {
		dev_err(&pdev->dev, "No memory to allocate HSI controller!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, hsi);

	err = ste_hsi_add_controller(hsi, pdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't add HSI controller!\n");
		goto err_free_controller;
	}

	if (pdata->use_dma)
		ste_hsi_init_dma(pdata, hsi);

	return 0;

err_free_controller:
	platform_set_drvdata(pdev, NULL);
	hsi_free_controller(hsi);

	return err;
}

static int __exit ste_hsi_remove(struct platform_device *pdev)
{
	struct hsi_controller *hsi = platform_get_drvdata(pdev);

	ste_hsi_remove_controller(hsi, pdev);
	hsi_free_controller(hsi);

	return 0;
}

static struct platform_driver ste_hsi_driver __refdata = {
	.driver = {
		   .name = "ste_hsi",
		   .owner = THIS_MODULE,
		   },
	.remove = __exit_p(ste_hsi_remove),
};

static int __init ste_hsi_init(void)
{
	return platform_driver_probe(&ste_hsi_driver, ste_hsi_probe);
}
module_init(ste_hsi_init)

static void __exit ste_hsi_exit(void)
{
	platform_driver_unregister(&ste_hsi_driver);
}
module_exit(ste_hsi_exit)

MODULE_AUTHOR("Lukasz Baj <lukasz.baj@tieto.com");
MODULE_DESCRIPTION("STE HSI driver.");
MODULE_LICENSE("GPL");

