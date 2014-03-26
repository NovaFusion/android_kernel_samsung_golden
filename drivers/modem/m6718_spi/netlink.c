/*
 * Copyright (C) ST-Ericsson SA 2010,2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_protocol.c
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * U9500 <-> M6718 IPC protocol implementation using SPI:
 *   netlink related functionality
 */
#include <linux/netlink.h>
#include <linux/spi/spi.h>
#include <linux/modem/m6718_spi/modem_net.h>
#include <linux/modem/m6718_spi/modem_char.h>
#include "modem_protocol.h"
#include "modem_private.h"
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
#include "modem_state.h"
#endif

static struct sock *netlink_sk;
struct modem_spi_dev *modem_dev;

#define MAX_PAYLOAD 1024

/*
 * Netlink broadcast message values: this must correspond to those values
 * expected by userspace for the appropriate message.
 */
enum netlink_msg_id {
	NETLINK_MODEM_RESET = 1,
	NETLINK_MODEM_QUERY_STATE,
	NETLINK_USER_REQUEST_MODEM_RESET,
	NETLINK_MODEM_STATUS_ONLINE,
	NETLINK_MODEM_STATUS_OFFLINE
};

static void netlink_multicast_tasklet(unsigned long data)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	enum netlink_msg_id nlmsg = (enum netlink_msg_id)data;

	if (netlink_sk == NULL) {
		pr_err("could not send multicast, no socket\n");
		return;
	}

	/* prepare netlink message */
	skb = alloc_skb(NLMSG_SPACE(MAX_PAYLOAD), GFP_ATOMIC);
	if (!skb) {
		pr_err("failed to allocate socket buffer\n");
		return;
	}

	if (nlmsg == NETLINK_MODEM_RESET)
		modem_isa_reset(modem_dev);

	nlh = (struct nlmsghdr *)skb->data;
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = 0;  /* from kernel */
	nlh->nlmsg_flags = 0;
	*(int *)NLMSG_DATA(nlh) = nlmsg;
	skb_put(skb, MAX_PAYLOAD);
	/* sender is in group 1<<0 */
	NETLINK_CB(skb).pid = 0;  /* from kernel */
	/* to mcast group 1<<0 */
	NETLINK_CB(skb).dst_group = 1;

	/* multicast the message to all listening processes */
	pr_debug("sending netlink multicast message %d\n", nlmsg);
	netlink_broadcast(netlink_sk, skb, 0, 1, GFP_ATOMIC);

}

static void send_unicast(int dst_pid)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	if (netlink_sk == NULL) {
		pr_err("could not send unicast, no socket\n");
		return;
	}

	/* prepare the message for unicast */
	skb = alloc_skb(NLMSG_SPACE(MAX_PAYLOAD), GFP_KERNEL);
	if (!skb) {
		pr_err("failed to allocate socket buffer\n");
		return;
	}

	nlh = (struct nlmsghdr *)skb->data;
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = 0;  /* from kernel */
	nlh->nlmsg_flags = 0;

	if (modem_m6718_spi_is_boot_done()) {
		pr_debug("sending netlink unicast message %d\n",
			NETLINK_MODEM_STATUS_ONLINE);
		*(int *)NLMSG_DATA(nlh) = NETLINK_MODEM_STATUS_ONLINE;
	} else {
		pr_debug("sending netlink unicast message %d\n",
			NETLINK_MODEM_STATUS_OFFLINE);
		*(int *)NLMSG_DATA(nlh) = NETLINK_MODEM_STATUS_OFFLINE;
	}

	skb_put(skb, MAX_PAYLOAD);
	/* sender is in group 1<<0 */
	NETLINK_CB(skb).pid = 0;  /* from kernel */
	NETLINK_CB(skb).dst_group = 0;

	/* unicast the message to the querying process */
	netlink_unicast(netlink_sk, skb, dst_pid, MSG_DONTWAIT);
}

static void netlink_receive(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	int msg;

	nlh = (struct nlmsghdr *)skb->data;
	msg = *((int *)(NLMSG_DATA(nlh)));
	switch (msg) {
	case NETLINK_MODEM_QUERY_STATE:
		send_unicast(nlh->nlmsg_pid);
		break;
	case NETLINK_USER_REQUEST_MODEM_RESET:
		pr_info("user requested modem reset!\n");
#ifdef CONFIG_DEBUG_FS
		if (l1_context.msr_disable) {
			pr_info("MSR is disabled, ignoring reset request\n");
			break;
		}
#endif
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
		modem_state_force_reset();
#else
		pr_err("modestate integration is not enabled in IPC, "
			"unable to reset modem\n");
#endif
		break;
	default:
		pr_debug("ignoring invalid netlink message\n");
		break;
	}
}

bool ipc_create_netlink_socket(struct ipc_link_context *context)
{
	if (netlink_sk != NULL)
		return true;

	netlink_sk = netlink_kernel_create(NULL, NETLINK_MODEM, 1,
		netlink_receive, NULL, THIS_MODULE);
	if (netlink_sk == NULL) {
		dev_err(&context->sdev->dev,
			"failed to create netlink socket\n");
		return false;
	}
	modem_dev = spi_get_drvdata(context->sdev);
	return true;
}

DECLARE_TASKLET(modem_online_tasklet, netlink_multicast_tasklet,
	NETLINK_MODEM_STATUS_ONLINE);
DECLARE_TASKLET(modem_reset_tasklet, netlink_multicast_tasklet,
	NETLINK_MODEM_RESET);

void ipc_broadcast_modem_online(struct ipc_link_context *context)
{
	dev_info(&context->sdev->dev, "broadcast modem online event!\n");
	tasklet_schedule(&modem_online_tasklet);
}

void ipc_broadcast_modem_reset(struct ipc_link_context *context)
{
	dev_info(&context->sdev->dev, "broadcast modem reset event!\n");
	tasklet_schedule(&modem_reset_tasklet);
}

