/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __ASM_ARCH_USB_H
#define __ASM_ARCH_USB_H

#include <linux/dmaengine.h>

#define UX500_MUSB_DMA_NUM_RX_CHANNELS 8
#define UX500_MUSB_DMA_NUM_TX_CHANNELS 8

struct musb;

struct ux500_musb_board_data {
	void	**dma_rx_param_array;
	void	**dma_tx_param_array;
	u32	num_rx_channels;
	u32	num_tx_channels;
	bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
};

void ux500_add_usb(resource_size_t base, int irq, int *dma_rx_cfg,
	int *dma_tx_cfg);

/* Only used for u5500 */
struct abx500_usbgpio_platform_data {
	int (*get)(struct device *device);
	void (*enable)(void);
	void (*disable)(void);
	void (*put)(void);
	int usb_cs;
};
void ux500_restore_context(struct musb *musb);
#endif
