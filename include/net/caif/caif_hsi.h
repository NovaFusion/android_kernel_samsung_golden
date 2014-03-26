/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author:	Daniel Martensson <Daniel.Martensson@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CAIF_HSI_H_
#define CAIF_HSI_H_

#include <net/caif/caif_layer.h>
#include <net/caif/caif_device.h>

/*
 * Maximum number of CAIF frames that can reside in the same HSI frame.
 */
#define CFHSI_MAX_PKTS 15

/*
 * Maximum number of bytes used for the frame that can be embedded in the
 * HSI descriptor.
 */
#define CFHSI_MAX_EMB_FRM_SZ 96

/*
 * Decides if HSI buffers should be prefilled with 0xFF pattern for easier
 * debugging. Both TX and RX buffers will be filled before the transfer.
 */
#define CFHSI_DBG_PREFILL		0

/* Structure describing a HSI packet descriptor. */
struct cfhsi_desc {
	u8 header;
	u8 offset;
	u16 cffrm_len[CFHSI_MAX_PKTS];
	u8 emb_frm[CFHSI_MAX_EMB_FRM_SZ];
} __packed;

/* Size of the complete HSI packet descriptor. */
#define CFHSI_DESC_SZ (sizeof(struct cfhsi_desc))

/*
 * Size of the complete HSI packet descriptor excluding the optional embedded
 * CAIF frame.
 */
#define CFHSI_DESC_SHORT_SZ (CFHSI_DESC_SZ - CFHSI_MAX_EMB_FRM_SZ)

/*
 * Maximum bytes transferred in one transfer.
 */
/* TODO: 4096 is temporary... */
#define CFHSI_MAX_PAYLOAD_SZ (CFHSI_MAX_PKTS * 4096)

/* Size of the complete HSI TX buffer. */
#define CFHSI_BUF_SZ_TX (CFHSI_DESC_SZ + CFHSI_MAX_PAYLOAD_SZ)

/* Size of the complete HSI RX buffer. */
#define CFHSI_BUF_SZ_RX ((2 * CFHSI_DESC_SZ) + CFHSI_MAX_PAYLOAD_SZ)

/* Bitmasks for the HSI descriptor. */
#define CFHSI_PIGGY_DESC		(0x01 << 7)

#define CFHSI_TX_STATE_IDLE			0
#define CFHSI_TX_STATE_XFER			1

#define CFHSI_RX_STATE_DESC			0
#define CFHSI_RX_STATE_PAYLOAD		1

/* Structure implemented by the CAIF HSI driver. */
struct cfhsi_drv {
	void (*tx_done_cb) (struct cfhsi_drv *drv);
	void (*rx_done_cb) (struct cfhsi_drv *drv);
};

/* Structure implemented by HSI device. */
struct cfhsi_dev {
	int (*cfhsi_tx) (u8 *ptr, int len, struct cfhsi_dev *dev);
	int (*cfhsi_rx) (u8 *ptr, int len, struct cfhsi_dev *dev);
	struct cfhsi_drv *drv;
};

/* Structure implemented by CAIF HSI drivers. */
struct cfhsi {
	struct caif_dev_common cfdev;
	struct net_device *ndev;
	struct platform_device *pdev;
	struct sk_buff_head qhead;
	struct cfhsi_drv drv;
	struct cfhsi_dev *dev;
	int tx_state;
	int rx_state;
	u8 *tx_buf;
	u8 *rx_buf;
	spinlock_t lock;
	int flow_off_sent;
	u32 q_low_mark;
	u32 q_high_mark;
	bool flow_stop;
	struct list_head list;
};

extern struct platform_driver cfhsi_driver;

#endif		/* CAIF_HSI_H_ */
