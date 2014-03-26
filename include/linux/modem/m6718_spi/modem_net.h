/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_net.h
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC net device interface header.
 */
#ifndef _MODEM_NET_H_
#define _MODEM_NET_H_

#include <linux/modem/m6718_spi/modem_driver.h>

#define MODEM_HLEN		(1)
#define PHONET_ALEN		(1)

#define PN_PIPE			(0xD9)
#define PN_DEV_HOST		(0x00)
#define PN_LINK_ADDR		(0x26)
#define PN_TX_QUEUE_LEN		(3)

#define RESOURCE_ID_INDEX	(3)
#define SRC_OBJ_INDEX		(7)
#define MSG_ID_INDEX		(9)
#define PIPE_HDL_INDEX		(10)
#define NETLINK_MODEM		(20)

/**
 * struct modem_spi_net_dev - modem net interface device information
 * @modem_spi_dev:	pointer to the modem spi device information structure
 * @iface_num:		flag used to indicate the up/down of netdev
 */
struct modem_spi_net_dev {
	struct modem_spi_dev *modem_spi_dev;
	unsigned int iface_num;
};

int modem_net_init(struct modem_spi_dev *modem_spi_dev);
void modem_net_exit(struct modem_spi_dev *modem_spi_dev);

int modem_net_receive(struct net_device *dev);
int modem_net_suspend(struct net_device *dev);
int modem_net_resume(struct net_device *dev);
int modem_net_start(struct net_device *dev);
int modem_net_restart(struct net_device *dev);
int modem_net_stop(struct net_device *dev);

#endif /* _MODEM_NET_H_ */
