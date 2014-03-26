/*
 * Copyright (C) ST-Ericsson SA 2011
 * Contact: Sjur Brendeland <sjur.brandeland@stericsson.com>
 * Author:  Daniel Martensson <Daniel.Martensson@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/if_arp.h>
#include <net/caif/caif_hsi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Martensson<daniel.martensson@stericsson.com>");
MODULE_DESCRIPTION("CAIF HSI driver");

/* Returns the number of padding bytes for alignment. */
#define PAD_POW2(x, pow) ((((x)&((pow)-1)) == 0) ? 0 : \
				(((pow)-((x)&((pow)-1)))))

/*
 * HSI padding options.
 * Warning: must be a base of 2 (& operation used) and can not be zero !
 */
static int hsi_head_align = 4;
module_param(hsi_head_align, int, S_IRUGO);
MODULE_PARM_DESC(hsi_head_align, "HSI head alignment.");

static int hsi_tail_align = 4;
module_param(hsi_tail_align, int, S_IRUGO);
MODULE_PARM_DESC(hsi_tail_align, "HSI tail alignment.");

/*
 * HSI link layer flowcontrol thresholds.
 * Warning: A high threshold value migth increase throughput but it will at
 * the same time prevent channel prioritization and increase the risk of
 * flooding the modem. The high threshold should be above the low.
 */
static int hsi_high_threshold = 100;
module_param(hsi_high_threshold, int, S_IRUGO);
MODULE_PARM_DESC(hsi_high_threshold, "HSI high threshold (FLOW OFF).");

static int hsi_low_threshold = 50;
module_param(hsi_low_threshold, int, S_IRUGO);
MODULE_PARM_DESC(hsi_low_threshold, "HSI high threshold (FLOW ON).");

#define ON 1
#define OFF 0

/*
 * Threshold values for the HSI packet queue. Flowcontrol will be asserted
 * when the number of packets exceeds HIGH_WATER_MARK. It will not be
 * de-asserted before the number of packets drops below LOW_WATER_MARK.
 */
#define LOW_WATER_MARK   hsi_low_threshold
#define HIGH_WATER_MARK  hsi_high_threshold

static LIST_HEAD(cfhsi_list);
static spinlock_t cfhsi_list_lock;

static int cfhsi_tx_frm(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int nfrms = 0;
	int pld_len = 0;
	struct sk_buff *skb;
	u8 *pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;

	skb = skb_peek(&cfhsi->qhead);
	if (!skb)
		return 0;

	/* Check if we can embed a CAIF frame. */
	if (skb->len < CFHSI_MAX_EMB_FRM_SZ) {
		struct caif_payload_info *info;
		int hpad = 0;
		int tpad = 0;

		/* Calculate needed head alignment and tail alignment. */
		info = (struct caif_payload_info *)&skb->cb;

		hpad = 1 + PAD_POW2((info->hdr_len + 1), hsi_head_align);
		tpad = PAD_POW2((skb->len + hpad), hsi_tail_align);

		/* Check if frame still fits with added alignment. */
		if ((skb->len + hpad + tpad) <= CFHSI_MAX_EMB_FRM_SZ) {
			u8 *pemb = desc->emb_frm;
			skb = skb_dequeue(&cfhsi->qhead);
			desc->offset = CFHSI_DESC_SHORT_SZ;
			*pemb = (u8)(hpad - 1);
			pemb += hpad;

			/* Copy in embedded CAIF frame. */
			skb_copy_bits(skb, 0, pemb, skb->len);
			kfree_skb(skb);
		}
	} else {
		/* Clear offset. */
		desc->offset = 0;
	}


	/* Create payload CAIF frames. */
	pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;
	while (skb_peek(&cfhsi->qhead) && nfrms < CFHSI_MAX_PKTS) {
		struct caif_payload_info *info;
		int hpad = 0;
		int tpad = 0;

		skb = skb_dequeue(&cfhsi->qhead);

		/* Calculate needed head alignment and tail alignment. */
		info = (struct caif_payload_info *)&skb->cb;

		hpad = 1 + PAD_POW2((info->hdr_len + 1), hsi_head_align);
		tpad = PAD_POW2((skb->len + hpad), hsi_tail_align);

		/* Fill in CAIF frame length in descriptor. */
		desc->cffrm_len[nfrms] = hpad + skb->len + tpad;

		/* Fill head padding information. */
		*pfrm = (u8)(hpad - 1);
		pfrm += hpad;

		/* Copy in CAIF frame. */
		skb_copy_bits(skb, 0, pfrm, skb->len);
		kfree_skb(skb);

		/* Update payload length. */
		pld_len += desc->cffrm_len[nfrms];

		/* Update frame pointer. */
		pfrm += skb->len + tpad;

		/* Update number of frames. */
		nfrms++;
	}

	/* Unused length fields should be zero-filled (according to SPEC). */
	while (nfrms < CFHSI_MAX_PKTS) {
		desc->cffrm_len[nfrms] = 0x0000;
		nfrms++;
	}

	/* Check if we can piggy-back another descriptor. */
	skb = skb_peek(&cfhsi->qhead);
	if (skb)
		desc->header |= CFHSI_PIGGY_DESC;
	else
		desc->header &= ~CFHSI_PIGGY_DESC;

	return CFHSI_DESC_SZ + pld_len;
}

static void cfhsi_tx_done_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_desc *desc = NULL;
	struct sk_buff *skb;
	unsigned long flags;
	int len = 0;

	cfhsi = container_of(drv, struct cfhsi, drv);
	desc = (struct cfhsi_desc *)cfhsi->tx_buf;

	spin_lock_irqsave(&cfhsi->lock, flags);

	/*
	 * Send flow on if flow off has been previously signalled
	 * and number of packets is below low water mark.
	 */
	if (cfhsi->flow_off_sent && cfhsi->qhead.qlen <= cfhsi->q_low_mark &&
		cfhsi->cfdev.flowctrl) {
		cfhsi->flow_off_sent = 0;
		cfhsi->cfdev.flowctrl(cfhsi->ndev, ON);
	}

	skb = skb_peek(&cfhsi->qhead);
	if (!skb) {
		cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
		spin_unlock_irqrestore(&cfhsi->lock, flags);
		return;
	}


	/* Create HSI frame. */
	len = cfhsi_tx_frm(desc, cfhsi);
	BUG_ON(!len);
	spin_unlock_irqrestore(&cfhsi->lock, flags);

	/* Set up new transfer. */
	cfhsi->dev->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->dev);
}

static int cfhsi_rx_desc(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int xfer_sz = 0;
	int nfrms = 0;
	u16 *plen = NULL;
	u8 *pfrm = NULL;

	/* Sanity check header and offset. */
	BUG_ON(desc->header & ~CFHSI_PIGGY_DESC);
	BUG_ON(desc->offset > CFHSI_MAX_EMB_FRM_SZ);

	/* Check for embedded CAIF frame. */
	if (desc->offset) {
		struct sk_buff *skb = NULL;
		u8 *dst = NULL;
		int len = 0;
		pfrm = ((u8 *)desc) + desc->offset;

		/* Remove offset padding. */
		pfrm += *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pfrm;
		len |= ((*(pfrm+1)) << 8) & 0xFF00;
		/* Add FCS fields. */
		len += 2;

		/* Allocate SKB (OK even in IRQ context). */
		skb = netdev_alloc_skb(cfhsi->ndev, len + 1);
		if (skb == NULL)
			goto err;

		dst = skb_put(skb, len);
		memcpy(dst, pfrm, len);

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);
		skb->dev = cfhsi->ndev;

		/*
		 * We are called from a arch specific platform device.
		 * Unfortunately we don't know what context we're
		 * running in. HSI might well run in a work queue as
		 * the HSI protocol might require the driver to sleep.
		 */
		if (in_interrupt())
			(void)netif_rx(skb);
		else
			(void)netif_rx_ni(skb);

		/* Update statistics. */
		cfhsi->ndev->stats.rx_packets++;
		cfhsi->ndev->stats.rx_bytes += len;
	}

	/* Calculate transfer length. */
	plen = desc->cffrm_len;
	while (nfrms < CFHSI_MAX_PKTS && *plen) {
		xfer_sz += *plen;
		plen++;
		nfrms++;
	}

	/* Check for piggy-backed descriptor. */
	if (desc->header & CFHSI_PIGGY_DESC)
		xfer_sz += CFHSI_DESC_SZ;

err:
	return xfer_sz;
}

static int cfhsi_rx_pld(struct cfhsi_desc *desc, struct cfhsi *cfhsi)
{
	int rx_sz = 0;
	int nfrms = 0;
	u16 *plen = NULL;
	u8 *pfrm = NULL;

	/* Sanity check header and offset. */
	BUG_ON(desc->header & ~CFHSI_PIGGY_DESC);
	BUG_ON(desc->offset > CFHSI_MAX_EMB_FRM_SZ);

	/* Set frame pointer to start of payload. */
	pfrm = desc->emb_frm + CFHSI_MAX_EMB_FRM_SZ;
	plen = desc->cffrm_len;
	while (nfrms < CFHSI_MAX_PKTS && *plen) {
		struct sk_buff *skb;
		u8 *dst = NULL;
		u8 *pcffrm = NULL;
		int len = 0;

		BUG_ON(desc->cffrm_len[nfrms] > CFHSI_MAX_PAYLOAD_SZ);

		/* CAIF frame starts after head padding. */
		pcffrm = pfrm + *pfrm + 1;

		/* Read length of CAIF frame (little endian). */
		len = *pcffrm;
		len |= ((*(pcffrm + 1)) << 8) & 0xFF00;
		/* Add FCS fields. */
		len += 2;

		/* Allocate SKB (OK even in IRQ context). */
		skb = netdev_alloc_skb(cfhsi->ndev, len + 1);
		if (skb == NULL)
			goto err;

		dst = skb_put(skb, len);
		memcpy(dst, pcffrm, len);

		skb->protocol = htons(ETH_P_CAIF);
		skb_reset_mac_header(skb);
		skb->dev = cfhsi->ndev;

		/*
		 * As explained above we're called from a platform
		 * device, and don't know the context we're running in.
		 */
		if (in_interrupt())
			(void)netif_rx(skb);
		else
			(void)netif_rx_ni(skb);

		/* Update statistics. */
		cfhsi->ndev->stats.rx_packets++;
		cfhsi->ndev->stats.rx_bytes += len;

		pfrm += *plen;
		rx_sz += *plen;
		plen++;
		nfrms++;
	}

err:
	return rx_sz;
}

static void cfhsi_rx_done_cb(struct cfhsi_drv *drv)
{
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_desc *desc = NULL;
	int len = 0;
	u8 *ptr = NULL;

	cfhsi = container_of(drv, struct cfhsi, drv);
	desc = (struct cfhsi_desc *)cfhsi->rx_buf;

	if (cfhsi->rx_state == CFHSI_RX_STATE_DESC)
		len = cfhsi_rx_desc(desc, cfhsi);
	else {
		int pld_len = cfhsi_rx_pld(desc, cfhsi);

		if (desc->header & CFHSI_PIGGY_DESC) {
			struct cfhsi_desc *piggy_desc;
			piggy_desc = (struct cfhsi_desc *)(desc->emb_frm +
						CFHSI_MAX_EMB_FRM_SZ + pld_len);

			/* Extract piggy-backed descriptor. */
			len = cfhsi_rx_desc(piggy_desc, cfhsi);

			/*
			 * Copy needed information from the piggy-backed
			 * descriptor to the descriptor in the start.
			 */
			memcpy((u8 *)desc, (u8 *)piggy_desc,
					CFHSI_DESC_SHORT_SZ);
		}
	}

	if (len) {
		cfhsi->rx_state = CFHSI_RX_STATE_PAYLOAD;
		ptr = cfhsi->rx_buf + CFHSI_DESC_SZ;
	} else {
		len = CFHSI_DESC_SZ;
		cfhsi->rx_state = CFHSI_RX_STATE_DESC;
		ptr = cfhsi->rx_buf;
	}

	/* Set up new transfer. */
	cfhsi->dev->cfhsi_rx(ptr, len, cfhsi->dev);
}

static int cfhsi_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct cfhsi *cfhsi = NULL;
	unsigned long flags;
	int start_xfer = 0;

	if (!dev)
		return -EINVAL;

	cfhsi = netdev_priv(dev);

	spin_lock_irqsave(&cfhsi->lock, flags);

	skb_queue_tail(&cfhsi->qhead, skb);

	/* Send flow off if number of packets is above high water mark. */
	if (!cfhsi->flow_off_sent &&
		cfhsi->qhead.qlen > cfhsi->q_high_mark &&
		cfhsi->cfdev.flowctrl) {
		cfhsi->flow_off_sent = 1;
		cfhsi->cfdev.flowctrl(cfhsi->ndev, OFF);
	}

	if (cfhsi->tx_state == CFHSI_TX_STATE_IDLE) {
		cfhsi->tx_state = CFHSI_TX_STATE_XFER;
		start_xfer = 1;
	}

	if (start_xfer) {
		struct cfhsi_desc *desc = (struct cfhsi_desc *) cfhsi->tx_buf;
		int len;

		/* Create HSI frame. */
		len = cfhsi_tx_frm(desc, cfhsi);
		BUG_ON(!len);

		spin_unlock_irqrestore(&cfhsi->lock, flags);

		/* Set up new transfer. */
		cfhsi->dev->cfhsi_tx(cfhsi->tx_buf, len, cfhsi->dev);
		return 0;
	}

	spin_unlock_irqrestore(&cfhsi->lock, flags);
	return 0;
}

static int cfhsi_open(struct net_device *dev)
{
	netif_wake_queue(dev);

	return 0;
}

static int cfhsi_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}
static const struct net_device_ops cfhsi_ops = {
	.ndo_open = cfhsi_open,
	.ndo_stop = cfhsi_close,
	.ndo_start_xmit = cfhsi_xmit
};

static void cfhsi_setup(struct net_device *dev)
{
	struct cfhsi *cfhsi = netdev_priv(dev);
	dev->features = 0;
	dev->netdev_ops = &cfhsi_ops;
	dev->type = ARPHRD_CAIF;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = CFHSI_MAX_PAYLOAD_SZ;
	dev->tx_queue_len = 0;
	dev->destructor = free_netdev;
	skb_queue_head_init(&cfhsi->qhead);
	cfhsi->cfdev.link_select = CAIF_LINK_HIGH_BANDW;
	cfhsi->cfdev.use_frag = false;
	cfhsi->cfdev.use_stx = false;
	cfhsi->cfdev.use_fcs = false;
	cfhsi->ndev = dev;
}

int cfhsi_probe(struct platform_device *pdev)
{
	struct cfhsi *cfhsi = NULL;
	struct net_device *ndev;
	struct cfhsi_dev *dev;
	int res;

	ndev = alloc_netdev(sizeof(struct cfhsi), "cfhsi%d", cfhsi_setup);
	if (!ndev)
		return -ENODEV;

	cfhsi = netdev_priv(ndev);
	netif_stop_queue(ndev);
	cfhsi->ndev = ndev;
	cfhsi->pdev = pdev;

	/* Initialize state vaiables. */
	cfhsi->tx_state = CFHSI_TX_STATE_IDLE;
	cfhsi->rx_state = CFHSI_RX_STATE_DESC;

	/* Set flow info */
	cfhsi->flow_off_sent = 0;
	cfhsi->q_low_mark = LOW_WATER_MARK;
	cfhsi->q_high_mark = HIGH_WATER_MARK;

	/* Assign the HSI device. */
	dev = (struct cfhsi_dev *)pdev->dev.platform_data;
	cfhsi->dev = dev;

	/* Assign the driver to this HSI device. */
	dev->drv = &cfhsi->drv;

	/*
	 * Allocate a TX buffer with the size of a HSI packet descriptors
	 * and the necessary room for CAIF payload frames.
	 */
	cfhsi->tx_buf = kzalloc(CFHSI_BUF_SZ_TX, GFP_KERNEL);
	if (!cfhsi->tx_buf) {
		res = -ENODEV;
		goto err_alloc_tx;
	}

	/*
	 * Allocate a RX buffer with the size of two HSI packet descriptors and
	 * the necessary room for CAIF payload frames.
	 */
	cfhsi->rx_buf = kzalloc(CFHSI_BUF_SZ_RX, GFP_KERNEL);
	if (!cfhsi->rx_buf) {
		res = -ENODEV;
		goto err_alloc_rx;
	}

	/* Initialize spin locks. */
	spin_lock_init(&cfhsi->lock);

	/* Set up the driver. */
	cfhsi->drv.tx_done_cb = cfhsi_tx_done_cb;
	cfhsi->drv.rx_done_cb = cfhsi_rx_done_cb;

	/* Add CAIF HSI device to list. */
	spin_lock(&cfhsi_list_lock);
	list_add_tail(&cfhsi->list, &cfhsi_list);
	spin_unlock(&cfhsi_list_lock);

	/* Register network device. */
	res = register_netdev(ndev);
	if (res) {
		netdev_err(cfhsi->ndev, "cfhsi: Reg. error: %d.\n", res);
		goto err_net_reg;
	}

	/* Start an initial read operation. */
	cfhsi->dev->cfhsi_rx(cfhsi->rx_buf, CFHSI_DESC_SZ, cfhsi->dev);

	return res;

err_net_reg:
	kfree(cfhsi->rx_buf);
err_alloc_rx:
	kfree(cfhsi->tx_buf);
err_alloc_tx:
	free_netdev(ndev);

	return res;
}

int cfhsi_remove(struct platform_device *pdev)
{
	struct list_head *list_node;
	struct list_head *n;
	struct cfhsi *cfhsi = NULL;
	struct cfhsi_dev *dev;

	dev = (struct cfhsi_dev *)pdev->dev.platform_data;
	spin_lock(&cfhsi_list_lock);
	list_for_each_safe(list_node, n, &cfhsi_list) {
		cfhsi = list_entry(list_node, struct cfhsi, list);
		/* Find the corresponding device. */
		if (cfhsi->dev == dev) {
			/* Remove from list. */
			list_del(list_node);
			/* Free buffers. */
			kfree(cfhsi->tx_buf);
			kfree(cfhsi->rx_buf);
			unregister_netdev(cfhsi->ndev);
			spin_unlock(&cfhsi_list_lock);
			return 0;
		}
	}
	spin_unlock(&cfhsi_list_lock);
	return -ENODEV;
}

struct platform_driver cfhsi_plat_drv = {
	.probe = cfhsi_probe,
	.remove = cfhsi_remove,
	.driver = {
		   .name = "cfhsi",
		   .owner = THIS_MODULE,
		   },
};

static void __exit cfhsi_exit_module(void)
{
	struct list_head *list_node;
	struct list_head *n;
	struct cfhsi *cfhsi = NULL;

	list_for_each_safe(list_node, n, &cfhsi_list) {
		cfhsi = list_entry(list_node, struct cfhsi, list);
		platform_device_unregister(cfhsi->pdev);
	}

	/* Unregister platform driver. */
	platform_driver_unregister(&cfhsi_plat_drv);
}

static int __init cfhsi_init_module(void)
{
	int result;

	/* Initialize spin lock. */
	spin_lock_init(&cfhsi_list_lock);

	/* Register platform driver. */
	result = platform_driver_register(&cfhsi_plat_drv);
	if (result) {
		pr_warning("Could not register platform HSI driver.\n");
		goto err_dev_register;
	}

	return result;

err_dev_register:
	return result;
}

module_init(cfhsi_init_module);
module_exit(cfhsi_exit_module);
