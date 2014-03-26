/*
 * drivers/usb/musb/ux500_dma.c
 *
 * U8500 and U5500 DMA support code
 *
 * Copyright (C) 2009 STMicroelectronics
 * Copyright (C) 2011 ST-Ericsson SA
 * Authors:
 *	Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 *	Praveena Nadahally <praveen.nadahally@stericsson.com>
 *	Rajaram Regupathy <ragupathy.rajaram@stericsson.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/pfn.h>
#include <mach/usb.h>
#include "musb_core.h"
#undef DBG
#undef WARNING
#undef INFO
#include <linux/usb/composite.h>
#define Ux500_USB_DMA_MIN_TRANSFER_SIZE	512

struct ux500_dma_channel {
	struct dma_channel channel;
	struct ux500_dma_controller *controller;
	struct musb_hw_ep *hw_ep;
	struct work_struct channel_work;
	struct dma_chan *dma_chan;
	unsigned int cur_len;
	dma_cookie_t cookie;
	u8 ch_num;
	u8 is_tx;
	u8 is_allocated;
};

struct ux500_dma_controller {
	struct dma_controller controller;
	struct ux500_dma_channel rx_channel[UX500_MUSB_DMA_NUM_RX_CHANNELS];
	struct ux500_dma_channel tx_channel[UX500_MUSB_DMA_NUM_TX_CHANNELS];
	u32	num_rx_channels;
	u32	num_tx_channels;
	void *private_data;
	dma_addr_t phy_base;
};

/**
 * ux500_tx_work() - Invoked by worker thread
 * @data: worker queue data
 *
 * This function is invoked by worker thread when the DMA transfer
 * is completed in the transmit direction.
*/
static void ux500_tx_work(struct work_struct *data)
{
	struct ux500_dma_channel *ux500_channel = container_of(data,
		struct ux500_dma_channel, channel_work);
	struct musb_hw_ep       *hw_ep = ux500_channel->hw_ep;
	struct musb *musb = hw_ep->musb;
	unsigned long flags;

	pr_debug("DMA tx transfer done on hw_ep=%d\n", hw_ep->epnum);

	spin_lock_irqsave(&musb->lock, flags);
	ux500_channel->channel.actual_len = ux500_channel->cur_len;
	ux500_channel->channel.status = MUSB_DMA_STATUS_FREE;
	musb_dma_completion(musb, hw_ep->epnum,
				ux500_channel->is_tx);
	spin_unlock_irqrestore(&musb->lock, flags);
}

/**
 * ux500_rx_work() - Invoked by worker thread
 * @data: worker queue data
 *
 * This function is invoked by worker thread when the
 * DMA transfer is completed in the receive direction.
*/
static void ux500_rx_work(struct work_struct *data)
{
	struct ux500_dma_channel *ux500_channel = container_of(data,
		struct ux500_dma_channel, channel_work);
	struct musb_hw_ep       *hw_ep = ux500_channel->hw_ep;
	struct musb *musb = hw_ep->musb;
	unsigned long flags;

	pr_debug("DMA rx transfer done on hw_ep=%d\n", hw_ep->epnum);

	spin_lock_irqsave(&musb->lock, flags);
	ux500_channel->channel.actual_len = ux500_channel->cur_len;
	ux500_channel->channel.status = MUSB_DMA_STATUS_FREE;
	musb_dma_completion(musb, hw_ep->epnum,
		ux500_channel->is_tx);
	spin_unlock_irqrestore(&musb->lock, flags);
}

/**
 * ux500_dma_callback() - callback invoked when there is DMA data transfer
 * @private_data:	pointer to DMA channel.
 *
 * This callback is invoked when the DMA tranfer is completed.
*/
void ux500_dma_callback(void *private_data)
{
	struct dma_channel *channel = (struct dma_channel *)private_data;
	struct ux500_dma_channel *ux500_channel = channel->private_data;

	schedule_work(&ux500_channel->channel_work);
}

/**
 * ux500_configure_channel() - configures the source, destination addresses and
 *                       starts the transfer
 * @channel:    pointer to DMA channel
 * @packet_sz:  packet size
 * @mode: Dma mode
 * @dma_addr: DMA source address for transmit direction
 *            or DMA destination address for receive direction
 * @len: length
 * This function configures the source and destination addresses for DMA
 * operation and initiates the DMA transfer
*/
static bool ux500_configure_channel(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	struct ux500_dma_channel *ux500_channel = channel->private_data;
	struct musb_hw_ep *hw_ep = ux500_channel->hw_ep;
	struct dma_chan *dma_chan = ux500_channel->dma_chan;
	struct dma_async_tx_descriptor *dma_desc;
	enum dma_data_direction direction;
	struct scatterlist sg;
	struct dma_slave_config slave_conf;
	enum dma_slave_buswidth addr_width;
	dma_addr_t usb_fifo_addr = (MUSB_FIFO_OFFSET(hw_ep->epnum) +
					ux500_channel->controller->phy_base);

	pr_debug("packet_sz=%d, mode=%d, dma_addr=0x%x, len=%d is_tx=%d\n",
			packet_sz, mode, dma_addr, len, ux500_channel->is_tx);

	ux500_channel->cur_len = len;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, pfn_to_page(PFN_DOWN(dma_addr)), len,
					    offset_in_page(dma_addr));
	sg_dma_address(&sg) = dma_addr;
	sg_dma_len(&sg) = len;

	direction = ux500_channel->is_tx ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	addr_width = (len & 0x3) ? DMA_SLAVE_BUSWIDTH_1_BYTE :
					DMA_SLAVE_BUSWIDTH_4_BYTES;

	slave_conf.direction = direction;

	slave_conf.src_addr = usb_fifo_addr;
	slave_conf.src_addr_width = addr_width;
	slave_conf.src_maxburst = 16;
	slave_conf.dst_addr = usb_fifo_addr;
	slave_conf.dst_addr_width = addr_width;
	slave_conf.dst_maxburst = 16;

	dma_chan->device->device_control(dma_chan, DMA_SLAVE_CONFIG,
					     (unsigned long) &slave_conf);

	dma_desc = dma_chan->device->
			device_prep_slave_sg(dma_chan, &sg, 1, direction,
					     DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!dma_desc)
		return false;

	dma_desc->callback = ux500_dma_callback;
	dma_desc->callback_param = channel;
	ux500_channel->cookie = dma_desc->tx_submit(dma_desc);

	dma_async_issue_pending(dma_chan);

	return true;
}

/**
 * ux500_dma_controller_allocate() - allocates the DMA channels
 * @c: pointer to DMA controller
 * @hw_ep: pointer to endpoint
 * @is_tx: transmit or receive direction
 *
 * This function allocates the DMA channel and initializes
 * the channel
*/
static struct dma_channel *ux500_dma_channel_allocate(struct dma_controller *c,
				struct musb_hw_ep *hw_ep, u8 is_tx)
{
	struct ux500_dma_controller *controller = container_of(c,
			struct ux500_dma_controller, controller);
	struct ux500_dma_channel *ux500_channel = NULL;
	u8 ch_num = hw_ep->epnum - 1;
	u32 max_ch;

	/* Max 8 DMA channels (0 - 7). Each DMA channel can only be allocated
	 * to specified hw_ep. For example DMA channel 0 can only be allocated
	 * to hw_ep 1 and 9.
	 */
	if (ch_num > 7)
		ch_num -= 8;

	max_ch = is_tx ? controller->num_tx_channels :
			controller->num_rx_channels;

	if (ch_num >= max_ch)
		return NULL;

	ux500_channel = is_tx ? &(controller->tx_channel[ch_num]) :
				&(controller->rx_channel[ch_num]) ;

	/* Check if channel is already used. */
	if (ux500_channel->is_allocated)
		return NULL;

	ux500_channel->hw_ep = hw_ep;
	ux500_channel->is_allocated = 1;

	pr_devel("hw_ep=%d, is_tx=0x%x, channel=%d\n",
		hw_ep->epnum, is_tx, ch_num);

	return &(ux500_channel->channel);
}
/**
 * ux500_dma_channel_release() - releases the DMA channel
 * @channel:	channel to be released
 *
 * This function releases the DMA channel
 *
*/
static void ux500_dma_channel_release(struct dma_channel *channel)
{
	struct ux500_dma_channel *ux500_channel = channel->private_data;

	pr_devel("channel=%d\n", ux500_channel->ch_num);

	if (ux500_channel->is_allocated) {
		ux500_channel->is_allocated = 0;
		channel->status = MUSB_DMA_STATUS_FREE;
		channel->actual_len = 0;
	}
}

static int ux500_dma_is_compatible(struct dma_channel *channel,
		u16 maxpacket, void *buf, u32 length)
{
	struct ux500_dma_channel *ux500_channel = channel->private_data;
	struct musb_hw_ep       *hw_ep = ux500_channel->hw_ep;
	struct musb *musb = hw_ep->musb;
	struct usb_descriptor_header **descriptors;
	struct usb_function		*f;
	struct usb_gadget		*gadget = &musb->g;
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);

	if (length < Ux500_USB_DMA_MIN_TRANSFER_SIZE)
		return 0;

	list_for_each_entry(f, &cdev->config->functions, list) {
		if (!strcmp(f->name, "cdc_ethernet") ||
			!strcmp(f->name, "rndis") ||
			!strcmp(f->name, "mtp") ||
			!strcmp(f->name, "phonet") ||
			!strcmp(f->name, "adb") ||
			!strncmp(f->name, "acm", 3)) {
			if (gadget->speed == USB_SPEED_HIGH)
				descriptors = f->hs_descriptors;
			else
				descriptors = f->descriptors;

			for (; *descriptors; ++descriptors) {
				struct usb_endpoint_descriptor *ep;

				if ((*descriptors)->bDescriptorType !=
					USB_DT_ENDPOINT)
					continue;

				ep = (struct usb_endpoint_descriptor *)
					*descriptors;
				if (ep->bEndpointAddress ==
					ux500_channel->hw_ep->epnum)
					return 0;
			}
		}
	}

	return 1;
}

/**
 * ux500_dma_channel_program() - Configures the channel and initiates transfer
 * @channel:	pointer to DMA channel
 * @packet_sz:	packet size
 * @mode: mode
 * @dma_addr: physical address of memory
 * @len: length
 *
 * This function configures the channel and initiates the DMA transfer
*/
static int ux500_dma_channel_program(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	int ret;
	struct ux500_dma_channel *ux500_dma_channel = channel->private_data;

	BUG_ON(channel->status == MUSB_DMA_STATUS_UNKNOWN ||
		channel->status == MUSB_DMA_STATUS_BUSY);

	if (len < Ux500_USB_DMA_MIN_TRANSFER_SIZE)
		return 0;
	if (!ux500_dma_channel->is_tx && len < packet_sz)
		return 0;

	channel->status = MUSB_DMA_STATUS_BUSY;
	channel->actual_len = 0;
	ret = ux500_configure_channel(channel, packet_sz, mode, dma_addr, len);
	if (!ret)
		channel->status = MUSB_DMA_STATUS_FREE;

	return ret;
}

/**
 * ux500_dma_channel_abort() - aborts the DMA transfer
 * @channel:	pointer to DMA channel.
 *
 * This function aborts the DMA transfer.
*/
static int ux500_dma_channel_abort(struct dma_channel *channel)
{
	struct ux500_dma_channel *ux500_channel = channel->private_data;
	struct ux500_dma_controller *controller = ux500_channel->controller;
	struct musb *musb = controller->private_data;
	void __iomem *epio = musb->endpoints[ux500_channel->hw_ep->epnum].regs;
	u16 csr;

	pr_debug("channel=%d, is_tx=%d\n", ux500_channel->ch_num,
						ux500_channel->is_tx);

	if (channel->status == MUSB_DMA_STATUS_BUSY) {
		if (ux500_channel->is_tx) {
			csr = musb_readw(epio, MUSB_TXCSR);
			csr &= ~(MUSB_TXCSR_AUTOSET |
				 MUSB_TXCSR_DMAENAB |
				 MUSB_TXCSR_DMAMODE);
			musb_writew(epio, MUSB_TXCSR, csr);
		} else {
			csr = musb_readw(epio, MUSB_RXCSR);
			csr &= ~(MUSB_RXCSR_AUTOCLEAR |
				 MUSB_RXCSR_DMAENAB |
				 MUSB_RXCSR_DMAMODE);
			musb_writew(epio, MUSB_RXCSR, csr);
		}

		ux500_channel->dma_chan->device->
				device_control(ux500_channel->dma_chan,
					DMA_TERMINATE_ALL, 0);
		channel->status = MUSB_DMA_STATUS_FREE;
	}
	return 0;
}

/**
 * ux500_dma_controller_stop() - releases all the channels and frees the DMA pipes
 * @c: pointer to DMA controller
 *
 * This function frees all of the logical channels and frees the DMA pipes
*/
static int ux500_dma_controller_stop(struct dma_controller *c)
{
	struct ux500_dma_controller *controller = container_of(c,
			struct ux500_dma_controller, controller);
	struct ux500_dma_channel *ux500_channel;
	struct dma_channel *channel;
	u8 ch_num;

	for (ch_num = 0; ch_num < controller->num_rx_channels; ch_num++) {
		channel = &controller->rx_channel[ch_num].channel;
		ux500_channel = channel->private_data;

		ux500_dma_channel_release(channel);

		if (ux500_channel->dma_chan)
			dma_release_channel(ux500_channel->dma_chan);
	}

	for (ch_num = 0; ch_num < controller->num_tx_channels; ch_num++) {
		channel = &controller->tx_channel[ch_num].channel;
		ux500_channel = channel->private_data;

		ux500_dma_channel_release(channel);

		if (ux500_channel->dma_chan)
			dma_release_channel(ux500_channel->dma_chan);
	}

	return 0;
}


/**
 * ux500_dma_controller_start() - creates the logical channels pool and registers callbacks
 * @c:	pointer to DMA Controller
 *
 * This function requests the logical channels from the DMA driver and creates
 * logical channels based on event lines and also registers the callbacks which
 * are invoked after data transfer in the transmit or receive direction.
*/
static int ux500_dma_controller_start(struct dma_controller *c)
{
	struct ux500_dma_controller *controller = container_of(c,
			struct ux500_dma_controller, controller);
	struct ux500_dma_channel *ux500_channel = NULL;
	struct musb *musb = controller->private_data;
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct ux500_musb_board_data *data = plat->board_data;
	struct dma_channel *dma_channel = NULL;
	u32 ch_num;
	u8 dir;
	u8 is_tx = 0;

	void **param_array;
	struct ux500_dma_channel *channel_array;
	u32 ch_count;
	void (*musb_channel_work)(struct work_struct *);
	dma_cap_mask_t mask;

	if ((data->num_rx_channels > UX500_MUSB_DMA_NUM_RX_CHANNELS) ||
		(data->num_tx_channels > UX500_MUSB_DMA_NUM_TX_CHANNELS))
		return -EINVAL;

	controller->num_rx_channels = data->num_rx_channels;
	controller->num_tx_channels = data->num_tx_channels;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Prepare the loop for RX channels */
	channel_array = controller->rx_channel;
	ch_count = data->num_rx_channels;
	param_array = data->dma_rx_param_array;
	musb_channel_work = ux500_rx_work;

	for (dir = 0; dir < 2; dir++) {
		for (ch_num = 0; ch_num < ch_count; ch_num++) {
			ux500_channel = &channel_array[ch_num];
			ux500_channel->controller = controller;
			ux500_channel->ch_num = ch_num;
			ux500_channel->is_tx = is_tx;

			dma_channel = &(ux500_channel->channel);
			dma_channel->private_data = ux500_channel;
			dma_channel->status = MUSB_DMA_STATUS_FREE;
			dma_channel->max_len = SZ_16M;

			ux500_channel->dma_chan = dma_request_channel(mask,
							data->dma_filter,
							param_array[ch_num]);
			if (!ux500_channel->dma_chan) {
				ERR("Dma pipe allocation error dir=%d ch=%d\n",
					dir, ch_num);

				/* Release already allocated channels */
				ux500_dma_controller_stop(c);

				return -EBUSY;
			}

			INIT_WORK(&ux500_channel->channel_work,
				musb_channel_work);
		}

		/* Prepare the loop for TX channels */
		channel_array = controller->tx_channel;
		ch_count = data->num_tx_channels;
		param_array = data->dma_tx_param_array;
		musb_channel_work = ux500_tx_work;
		is_tx = 1;
	}

	return 0;
}

/**
 * dma_controller_destroy() - deallocates the DMA controller
 * @c:	pointer to dma controller.
 *
 * This function deallocates the DMA controller.
*/
void dma_controller_destroy(struct dma_controller *c)
{
	struct ux500_dma_controller *controller = container_of(c,
			struct ux500_dma_controller, controller);

	kfree(controller);
}

/**
 * dma_controller_create() - creates the dma controller and initializes callbacks
 *
 * @musb:	pointer to mentor core driver data instance|
 * @base:	base address of musb registers.
 *
 * This function creates the DMA controller and initializes the callbacks
 * that are invoked from the Mentor IP core.
*/
struct dma_controller *__init
dma_controller_create(struct musb *musb, void __iomem *base)
{
	struct ux500_dma_controller *controller;
	struct platform_device *pdev = to_platform_device(musb->controller);
	struct resource	*iomem;

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	controller->private_data = musb;

	/* Save physical address for DMA controller. */
	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	controller->phy_base = (dma_addr_t) iomem->start;

	controller->controller.start = ux500_dma_controller_start;
	controller->controller.stop = ux500_dma_controller_stop;
	controller->controller.channel_alloc = ux500_dma_channel_allocate;
	controller->controller.channel_release = ux500_dma_channel_release;
	controller->controller.channel_program = ux500_dma_channel_program;
	controller->controller.channel_abort = ux500_dma_channel_abort;
	controller->controller.is_compatible = ux500_dma_is_compatible;

	return &controller->controller;
}
