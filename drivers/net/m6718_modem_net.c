/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on u8500_shrm.c
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * M6718 modem net device interface.
 */
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/phonet.h>
#include <linux/if_phonet.h>
#include <linux/if_arp.h>
#include <net/sock.h>
#include <net/phonet/phonet.h>
#include <net/phonet/pep.h>
#include <linux/modem/m6718_spi/modem_net.h>
#include <linux/modem/m6718_spi/modem_driver.h>
#include <linux/modem/m6718_spi/modem_char.h>
#include <linux/ratelimit.h>


/**
 * modem_net_receive() - receive data and copy to user space buffer
 * @dev: pointer to the network device structure
 *
 * Copy data from ISI queue to the user space buffer.
 */
int modem_net_receive(struct net_device *dev)
{
	struct sk_buff *skb;
	struct isa_device_context *isadev;
	struct message_queue *q;
	u32 msgsize;
	u32 size = 0;
	struct modem_spi_net_dev *net_iface_priv =
		(struct modem_spi_net_dev *)netdev_priv(dev);
	struct modem_spi_dev *modem_spi_dev = net_iface_priv->modem_spi_dev;

	isadev = &modem_spi_dev->isa_context->isadev[MODEM_M6718_SPI_CHN_ISI];
	q = &isadev->dl_queue;

	spin_lock_bh(&q->update_lock);
	if (list_empty(&q->msg_list)) {
		spin_unlock_bh(&q->update_lock);
		dev_dbg(modem_spi_dev->dev, "empty queue!\n");
		return 0;
	}
	spin_unlock_bh(&q->update_lock);

	msgsize = modem_isa_msg_size(q);
	if (msgsize <= 0)
		return msgsize;

	/*
	 * The packet has been retrieved from the transmission
	 * medium. Build an skb around it, so upper layers can handle it
	 */
	skb = dev_alloc_skb(msgsize);
	if (!skb) {
		pr_notice_ratelimited("isa rx: low on mem - packet dropped\n");
		dev->stats.rx_dropped++;
		return -ENOMEM;
	}

	if ((q->readptr + msgsize) >= q->size) {
		size = (q->size - q->readptr);
		/* copy first part of msg */
		skb_copy_to_linear_data(skb,
			(u8 *)(q->fifo_base + q->readptr), size);
		skb_put(skb, size);

		/* copy second part of msg at the top of fifo */
		skb_copy_to_linear_data_offset(skb, size,
				(u8 *)(q->fifo_base), (msgsize - size));
		skb_put(skb, msgsize - size);

	} else {
		skb_copy_to_linear_data(skb,
				(u8 *)(q->fifo_base + q->readptr), msgsize);
		skb_put(skb, msgsize);
	}

	spin_lock_bh(&q->update_lock);
	modem_isa_unqueue_msg(q);
	spin_unlock_bh(&q->update_lock);

	skb_reset_mac_header(skb);
	__skb_pull(skb, dev->hard_header_len);
	/* write metadata and then pass to the receive level */
	skb->dev = dev;
	skb->protocol = htons(ETH_P_PHONET);
	skb->priority = 0;
	skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
	if (likely(netif_rx_ni(skb) == NET_RX_SUCCESS)) {
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += msgsize;
	} else {
		dev->stats.rx_dropped++;
	}

	return msgsize;
}
EXPORT_SYMBOL_GPL(modem_net_receive);

static int netdev_isa_open(struct net_device *dev)
{
	struct modem_spi_net_dev *net_iface_priv =
			(struct modem_spi_net_dev *)netdev_priv(dev);
	struct modem_spi_dev *modem_spi_dev = net_iface_priv->modem_spi_dev;

	modem_spi_dev->netdev_flag_up = 1;
	if (!netif_carrier_ok(dev))
		netif_carrier_on(dev);
	netif_wake_queue(dev);
	return 0;
}

static int netdev_isa_close(struct net_device *dev)
{
	struct modem_spi_net_dev *net_iface_priv =
			(struct modem_spi_net_dev *)netdev_priv(dev);
	struct modem_spi_dev *modem_spi_dev = net_iface_priv->modem_spi_dev;

	modem_spi_dev->netdev_flag_up = 0;
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	return 0;
}

static int netdev_isa_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct if_phonet_req *req = (struct if_phonet_req *)ifr;

	switch (cmd) {
	case SIOCPNGAUTOCONF:
		req->ifr_phonet_autoconf.device = PN_DEV_HOST;
		return 0;
	}
	return -ENOIOCTLCMD;
}

static struct net_device_stats *netdev_isa_stats(struct net_device *dev)
{
	return &dev->stats;
}

/**
 * netdev_isa_write() - write through the net interface
 * @skb:	pointer to the socket buffer
 * @dev:	pointer to the network device structure
 *
 * Copies data(ISI message) from the user buffer to the kernel buffer and
 * schedule transfer thread to transmit the message to the modem via FIFO.
 */
static netdev_tx_t netdev_isa_write(struct sk_buff *skb, struct net_device *dev)
{
	int err;
	int retval = 0;
	struct modem_spi_net_dev *net_iface_priv =
			(struct modem_spi_net_dev *)netdev_priv(dev);
	struct modem_spi_dev *modem_spi_dev = net_iface_priv->modem_spi_dev;

	/*
	 * FIXME:
	 * U8500 modem requires that Pipe created/enabled Indication should
	 * be sent from the port corresponding to GPRS socket.
	 * Also, the U8500 modem does not implement Pipe controller
	 * which takes care of port manipulations for GPRS traffic.
	 *
	 * Now, APE has GPRS socket and the socket for sending
	 * Indication msgs bound to different ports.
	 * Phonet stack does not allow an indication msg to be sent
	 * from GPRS socket, since Phonet stack assumes the presence
	 * of Pipe controller in modem.
	 *
	 * So, due to lack of Pipe controller implementation in the
	 * U8500 modem, carry out the port manipulation related to
	 * GPRS traffic here.
	 * Ideally, it should be done either by Pipe controller in
	 * modem OR some implementation of Pipe controller on APE side
	 */
	if (skb->data[RESOURCE_ID_INDEX] == PN_PIPE) {
		if ((skb->data[MSG_ID_INDEX] == PNS_PIPE_CREATED_IND) ||
			(skb->data[MSG_ID_INDEX] == PNS_PIPE_ENABLED_IND) ||
			(skb->data[MSG_ID_INDEX] == PNS_PIPE_DISABLED_IND))
			skb->data[SRC_OBJ_INDEX] = skb->data[PIPE_HDL_INDEX];
	}

	spin_lock_bh(&modem_spi_dev->isa_context->common_tx_lock);
	err = modem_m6718_spi_send(modem_spi_dev, MODEM_M6718_SPI_CHN_ISI,
		skb->len, skb->data);
	if (!err) {
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb->len;
		retval = NETDEV_TX_OK;
		dev_kfree_skb(skb);
	} else {
		dev->stats.tx_dropped++;
		retval = NETDEV_TX_BUSY;
	}
	spin_unlock_bh(&modem_spi_dev->isa_context->common_tx_lock);

	return retval;
}

static const struct net_device_ops modem_netdev_ops = {
	.ndo_open = netdev_isa_open,
	.ndo_stop = netdev_isa_close,
	.ndo_do_ioctl = netdev_isa_ioctl,
	.ndo_start_xmit = netdev_isa_write,
	.ndo_get_stats = netdev_isa_stats,
};

static void net_device_init(struct net_device *dev)
{
	struct modem_spi_net_dev *net_iface_priv;

	dev->netdev_ops = &modem_netdev_ops;
	dev->header_ops = &phonet_header_ops;
	dev->type = ARPHRD_PHONET;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = PHONET_MAX_MTU;
	dev->hard_header_len = MODEM_HLEN;
	dev->addr_len = PHONET_ALEN;
	dev->tx_queue_len = PN_TX_QUEUE_LEN;
	dev->destructor = free_netdev;
	dev->dev_addr[0] = PN_LINK_ADDR;
	net_iface_priv = netdev_priv(dev);
	memset(net_iface_priv, 0 , sizeof(struct modem_spi_net_dev));
}

int modem_net_init(struct modem_spi_dev *modem_spi_dev)
{
	struct net_device *nw_device;
	struct modem_spi_net_dev *net_iface_priv;
	int err;
	/*
	 * keep the same net device name as U8500 to allow userspace clients
	 * to remain unchanged and use the same interfaces
	 */
	char *devname = "shrm%d";

	/* allocate the net device */
	nw_device = modem_spi_dev->ndev =
		alloc_netdev(sizeof(struct modem_spi_net_dev),
			devname, net_device_init);
	if (nw_device == NULL) {
		dev_err(modem_spi_dev->dev,
			"failed to allocate modem net device\n");
		return -ENOMEM;
	}
	err = register_netdev(modem_spi_dev->ndev);
	if (err) {
		dev_err(modem_spi_dev->dev,
			"failed to register modem net device: error %d\n", err);
		free_netdev(modem_spi_dev->ndev);
		return -ENODEV;
	}
	dev_dbg(modem_spi_dev->dev, "registered modem net device\n");

	net_iface_priv = (struct modem_spi_net_dev *)netdev_priv(nw_device);
	net_iface_priv->modem_spi_dev = modem_spi_dev;
	net_iface_priv->iface_num = 0;
	return err;
}
EXPORT_SYMBOL_GPL(modem_net_init);

int modem_net_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

int modem_net_restart(struct net_device *dev)
{
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(modem_net_restart);

int modem_net_start(struct net_device *dev)
{
	struct modem_spi_net_dev *net_iface_priv =
			(struct modem_spi_net_dev *)netdev_priv(dev);
	struct modem_spi_dev *modem_spi_dev = net_iface_priv->modem_spi_dev;

	if (!netif_carrier_ok(dev))
		netif_carrier_on(dev);
	netif_start_queue(dev);
	modem_spi_dev->netdev_flag_up = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(modem_net_start);

int modem_net_suspend(struct net_device *dev)
{
	if (netif_running(dev)) {
		netif_stop_queue(dev);
		netif_carrier_off(dev);
	}
	netif_device_detach(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(modem_net_suspend);

int modem_net_resume(struct net_device *dev)
{
	netif_device_attach(dev);
	if (netif_running(dev)) {
		netif_carrier_on(dev);
		netif_wake_queue(dev);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(modem_net_resume);

void modem_net_exit(struct modem_spi_dev *modem_spi_dev)
{
	if (modem_spi_dev && modem_spi_dev->ndev) {
		unregister_netdev(modem_spi_dev->ndev);
		modem_spi_dev->ndev = NULL;
		dev_dbg(modem_spi_dev->dev, "removed modem net device\n");
	}
}
EXPORT_SYMBOL_GPL(modem_net_exit);

MODULE_AUTHOR("Chris Blair <chris.blair@stericsson.com>");
MODULE_DESCRIPTION("M6718 modem IPC net device interface");
MODULE_LICENSE("GPL v2");
