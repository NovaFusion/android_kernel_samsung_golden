/*
 * Copyright (C) ST-Ericsson SA 2009
 *
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __SHRM_NET_H
#define __SHRM_NET_H

#define SHRM_HLEN 1
#define PHONET_ALEN 1

#define PN_PIPE		0xD9
#define PN_DEV_HOST	0x00
#define PN_LINK_ADDR	0x26
#define PN_TX_QUEUE_LEN	3

#define RESOURCE_ID_INDEX	3
#define SRC_OBJ_INDEX		7
#define MSG_ID_INDEX		9
#define PIPE_HDL_INDEX		10
#define NETLINK_SHRM            20

/**
 * struct shrm_net_iface_priv - shrm net interface device information
 * @shrm_device:	pointer to the shrm device information structure
 * @iface_num:		flag used to indicate the up/down of netdev
 */
struct shrm_net_iface_priv {
	struct shrm_dev *shrm_device;
	unsigned int iface_num;
};

int shrm_register_netdev(struct shrm_dev *shrm_dev_data);
int shrm_net_receive(struct net_device *dev);
int shrm_suspend_netdev(struct net_device *dev);
int shrm_resume_netdev(struct net_device *dev);
int shrm_stop_netdev(struct net_device *dev);
int shrm_restart_netdev(struct net_device *dev);
int shrm_start_netdev(struct net_device *dev);
void shrm_unregister_netdev(struct shrm_dev *shrm_dev_data);

#endif /* __SHRM_NET_H */
