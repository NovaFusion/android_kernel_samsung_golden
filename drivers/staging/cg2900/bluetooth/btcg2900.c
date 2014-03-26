/*
 * Bluetooth driver for ST-Ericsson CG2900 connectivity controller.
 *
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com)
 * Henrik Possung (henrik.possung@stericsson.com)
 * Josef Kindberg (josef.kindberg@stericsson.com)
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com)
 * Kjell Andersson (kjell.k.andersson@stericsson.com)
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <asm/byteorder.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/mfd/core.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/hci_core.h>

#include "cg2900.h"

#define BT_HEADER_LENGTH		0x03

#define NAME				"BTCG2900 "

/* Wait for 5 seconds for a response to our requests */
#define RESP_TIMEOUT			5000

/* Bluetooth error codes */
#define HCI_ERR_NO_ERROR		0x00
#define HCI_ERR_CMD_DISALLOWED		0x0C

/**
 * enum reset_state - RESET-states of the HCI driver.
 *
 * @RESET_IDLE:		No reset in progress.
 * @RESET_ACTIVATED:	Reset in progress.
 * @RESET_UNREGISTERED:	hdev is unregistered.
 */

enum reset_state {
	RESET_IDLE,
	RESET_ACTIVATED,
	RESET_UNREGISTERED
};

/**
 * struct btcg2900_info - Specifies HCI driver private data.
 *
 * This type specifies CG2900 HCI driver private data.
 *
 * @list:		list_head struct.
 * @parent:		Parent to this BT device. All BT channels will have
 *			common parent.
 * @cmd:		Device structure for BT command channel.
 * @evt:		Device structure for BT event channel.
 * @acl:		Device structure for BT ACL channel.
 * @pdev:		Device structure for platform device.
 * @hdev:		Device structure for HCI device.
 * @reset_state:	Device enum for HCI driver reset state.
 */
struct btcg2900_info {
	struct list_head	list;
	struct device		*parent;
	struct device		*cmd;
	struct device		*evt;
	struct device		*acl;
	struct hci_dev		*hdev;
	enum reset_state	reset_state;
};

/**
 * struct dev_info - Specifies private data used when receiving callbacks from CG2900 driver.
 *
 * @hci_data_type:	Type of data according to BlueZ.
 */
struct dev_info {
	u8	hci_data_type;
};

/*
 * hci_wait_queue - Main Wait Queue in HCI driver.
 */
static DECLARE_WAIT_QUEUE_HEAD(hci_wait_queue);

/*
 * btcg2900_devices - List of active CG2900 BT devices.
 */
static LIST_HEAD(btcg2900_devices);

/* Internal function declarations */
static int register_bluetooth(struct btcg2900_info *info);

/* Internal functions */

/**
 * close_bt_users() - Close all BT channels.
 * @info:	HCI driver info structure.
 */
static void close_bt_users(struct btcg2900_info *info)
{
	struct cg2900_user_data *pf_data;

	pf_data = dev_get_platdata(info->cmd);
	if (pf_data->opened)
		pf_data->close(pf_data);

	pf_data = dev_get_platdata(info->acl);
	if (pf_data->opened)
		pf_data->close(pf_data);

	pf_data = dev_get_platdata(info->evt);
	if (pf_data->opened)
		pf_data->close(pf_data);
}

/**
 * handle_rx_evt() - Check if received data is response to internal command.
 * @info:	Info structure.
 * @skb:	Buffer with data coming from device.
 *
 * Returns:
 *   true if data has been handled internally,
 *   false otherwise.
 */
static bool handle_rx_evt(struct btcg2900_info *info, struct sk_buff *skb)
{
	struct hci_event_hdr *evt = (struct hci_event_hdr *)skb->data;
	struct hci_ev_cmd_complete *cmd_complete;
	struct hci_ev_cmd_status *cmd_status;
	u16 op_code;
	u8 status;
	bool pkt_handled = false;

	if (evt->evt == HCI_EV_CMD_COMPLETE) {
		cmd_complete = (struct hci_ev_cmd_complete *)(evt + 1);
		status = *((u8 *)(cmd_complete + 1));
		op_code = le16_to_cpu(cmd_complete->opcode);
	} else if (evt->evt == HCI_EV_CMD_STATUS) {
		cmd_status = (struct hci_ev_cmd_status *)(evt + 1);
		op_code = le16_to_cpu(cmd_status->opcode);
		status = cmd_status->status;
	}

	if (pkt_handled)
		kfree_skb(skb);

	return pkt_handled;
}

/**
 * hci_read_cb() - Callback for handling data received from CG2900 driver.
 * @dev:	Device receiving data.
 * @skb:	Buffer with data coming from device.
 */
static void hci_read_cb(struct cg2900_user_data *user, struct sk_buff *skb)
{
	int err = 0;
	struct dev_info *dev_info;
	struct btcg2900_info *info;

	dev_info = cg2900_get_usr(user);
	info = dev_get_drvdata(user->dev);

	if (user->dev != info->evt || !handle_rx_evt(info, skb)) {
		bt_cb(skb)->pkt_type = dev_info->hci_data_type;
		skb->dev = (struct net_device *)info->hdev;
		/* Update BlueZ stats */
		info->hdev->stat.byte_rx += skb->len;
		if (bt_cb(skb)->pkt_type == HCI_ACLDATA_PKT)
			info->hdev->stat.acl_rx++;
		else
			info->hdev->stat.evt_rx++;

		BT_DBG("Data receive %d bytes", skb->len);

		/* Provide BlueZ with received frame*/
		err = hci_recv_frame(skb);
		/* If err, skb have been freed in hci_recv_frame() */
		if (err)
			BT_ERR(NAME "Failed in supplying packet to Bluetooth"
			       " stack (%d)", err);
	}
}

/**
 * hci_reset_cb() - Callback for handling reset from CG2900 driver.
 * @dev:	CPD device resetting.
 */
static void hci_reset_cb(struct cg2900_user_data *dev)
{
	int err;
	struct btcg2900_info *info;
	struct cg2900_user_data *pf_data;

	BT_INFO(NAME "hci_reset_cb");

	info = dev_get_drvdata(dev->dev);

	BT_DBG("New reset_state: RESET_ACTIVATED");
	info->reset_state = RESET_ACTIVATED;

	/*
	 * Continue to deregister hdev if all channels has been reset else
	 * return.
	 */
	pf_data = dev_get_platdata(info->acl);
	if (pf_data->opened)
		return;
	pf_data = dev_get_platdata(info->cmd);
	if (pf_data->opened)
		return;
	pf_data = dev_get_platdata(info->evt);
	if (pf_data->opened)
		return;

	/*
	 * Deregister HCI device. Close and Destruct functions should
	 * in turn be called by BlueZ.
	 */
	BT_DBG("Deregister HCI device");
	err = hci_unregister_dev(info->hdev);
	if (err)
		BT_ERR(NAME "Can not deregister HCI device! (%d)", err);
		/*
		 * Now we are in trouble. Try to register a new hdev
		 * anyway even though this will cost some memory.
		 */

	wait_event_timeout(hci_wait_queue,
			(RESET_UNREGISTERED == info->reset_state),
			msecs_to_jiffies(RESP_TIMEOUT));
	if (RESET_UNREGISTERED != info->reset_state)
		/*
		 * Now we are in trouble. Try to register a new hdev
		 * anyway even though this will cost some memory.
		 */
		BT_ERR(NAME "Timeout expired. Could not deregister HCI device");

	/* Init and register hdev */
	BT_DBG("Register HCI device");
	err = register_bluetooth(info);
	if (err)
		BT_ERR(NAME "HCI Device registration error (%d)", err);
}

/**
 * btcg2900_open() - Open HCI interface.
 * @hdev:	HCI device being opened.
 *
 * BlueZ callback function for opening HCI interface to device.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL if NULL pointer is supplied.
 *   -EOPNOTSUPP if supplied packet type is not supported.
 *   -EBUSY if device is already opened.
 *   -EACCES if opening of channels failed.
 */
static int btcg2900_open(struct hci_dev *hdev)
{
	struct btcg2900_info *info;
	struct cg2900_user_data *pf_data;
	int err;

	BT_INFO("Open ST-Ericsson CG2900 driver");

	if (!hdev) {
		BT_ERR(NAME "NULL supplied for hdev");
		return -EINVAL;
	}

	info = (struct btcg2900_info *)hdev->driver_data;
	if (!info) {
		BT_ERR(NAME "NULL supplied for driver_data");
		return -EINVAL;
	}

	if (test_and_set_bit(HCI_RUNNING, &(hdev->flags))) {
		BT_ERR(NAME "Device already opened!");
		return -EBUSY;
	}

	pf_data = dev_get_platdata(info->acl);
	err = pf_data->open(pf_data);
	if (err) {
		BT_ERR("Couldn't open BT ACL channel (%d)", err);
		goto handle_error;
	}

	pf_data = dev_get_platdata(info->cmd);
	err = pf_data->open(pf_data);
	if (err) {
		BT_ERR("Couldn't open BT CMD channel (%d)", err);
		goto handle_error;
	}

	pf_data = dev_get_platdata(info->evt);
	err = pf_data->open(pf_data);
	if (err) {
		BT_ERR("Couldn't open BT EVT channel (%d)", err);
		goto handle_error;
	}

	if (info->reset_state == RESET_ACTIVATED) {
		BT_DBG("New reset_state: RESET_IDLE");
		info->reset_state = RESET_IDLE;
	}

	return 0;

handle_error:
	close_bt_users(info);
	clear_bit(HCI_RUNNING, &(hdev->flags));
	return err;

}

/**
 * btcg2900_close() - Close HCI interface.
 * @hdev:	HCI device being closed.
 *
 * BlueZ callback function for closing HCI interface.
 * It flushes the interface first.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL if NULL pointer is supplied.
 *   -EOPNOTSUPP if supplied packet type is not supported.
 *   -EBUSY if device is not opened.
 */
static int btcg2900_close(struct hci_dev *hdev)
{
	struct btcg2900_info *info = NULL;

	BT_DBG("btcg2900_close");

	if (!hdev) {
		BT_ERR(NAME "NULL supplied for hdev");
		return -EINVAL;
	}

	info = (struct btcg2900_info *)hdev->driver_data;
	if (!info) {
		BT_ERR(NAME "NULL supplied for driver_data");
		return -EINVAL;
	}

	if (!test_and_clear_bit(HCI_RUNNING, &(hdev->flags))) {
		BT_ERR(NAME "Device already closed!");
		return -EBUSY;
	}

	/* Do not do this if there is an reset ongoing */
	if (info->reset_state == RESET_ACTIVATED)
		goto remove_users;

remove_users:
	/* Finally deregister all users and free allocated data */
	close_bt_users(info);
	return 0;
}

/**
 * btcg2900_send() - Send packet to device.
 * @skb:	sk buffer to be sent.
 *
 * BlueZ callback function for sending sk buffer.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL if NULL pointer is supplied.
 *   -EOPNOTSUPP if supplied packet type is not supported.
 *   Error codes from cg2900_write.
 */
static int btcg2900_send(struct sk_buff *skb)
{
	struct hci_dev *hdev;
	struct btcg2900_info *info;
	struct cg2900_user_data *pf_data;
	int err = 0;

	if (!skb) {
		BT_ERR(NAME "NULL supplied for skb");
		return -EINVAL;
	}

	hdev = (struct hci_dev *)(skb->dev);
	if (!hdev) {
		BT_ERR(NAME "NULL supplied for hdev");
		return -EINVAL;
	}

	info = (struct btcg2900_info *)hdev->driver_data;
	if (!info) {
		BT_ERR(NAME "NULL supplied for info");
		return -EINVAL;
	}

	/* Update BlueZ stats */
	hdev->stat.byte_tx += skb->len;

	BT_DBG("Data transmit %d bytes", skb->len);

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		BT_DBG("Sending HCI_COMMAND_PKT");
		pf_data = dev_get_platdata(info->cmd);
		err = pf_data->write(pf_data, skb);
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		BT_DBG("Sending HCI_ACLDATA_PKT");
		pf_data = dev_get_platdata(info->acl);
		err = pf_data->write(pf_data, skb);
		hdev->stat.acl_tx++;
		break;
	default:
		BT_ERR(NAME "Trying to transmit unsupported packet type"
		       " (0x%.2X)", bt_cb(skb)->pkt_type);
		err = -EOPNOTSUPP;
		break;
	};

	return err;
}

/**
 * btcg2900_destruct() - Destruct HCI interface.
 * @hdev:	HCI device being destructed.
 */
static void btcg2900_destruct(struct hci_dev *hdev)
{
	struct btcg2900_info *info;

	BT_DBG("btcg2900_destruct");

	info = hdev->driver_data;
	if (!info) {
		BT_ERR(NAME "NULL supplied for info");
		return;
	}

	/*
	 * When destruct is called it means that the Bluetooth stack is done
	 * with the HCI device and we can now free it.
	 * Normally we do this only when removing the whole module through
	 * btcg2900_remove(), but when being reset we free the device here and
	 * we then set the reset state so that the reset handler can allocate a
	 * new HCI device and then register it to the Bluetooth stack.
	 */
	if (info->reset_state == RESET_ACTIVATED) {
		if (info->hdev) {
			hci_free_dev(info->hdev);
			info->hdev = NULL;
		}
		BT_DBG("New reset_state: RESET_UNREGISTERED");
		info->reset_state = RESET_UNREGISTERED;
		wake_up_all(&hci_wait_queue);
	}
}

/**
 * get_info() - Return info structure for this device.
 * @dev:	Current device.
 *
 * Returns:
 *   Pointer to info struct if there is no error.
 *   ERR_PTR(-ENOMEM) if allocation fails.
 */
static struct btcg2900_info *get_info(struct device *dev)
{
	struct list_head *cursor;
	struct btcg2900_info *tmp;
	struct btcg2900_info *info = NULL;

	/* Find the info structure */
	list_for_each(cursor, &btcg2900_devices) {
		tmp = list_entry(cursor, struct btcg2900_info, list);
		if (tmp->parent == dev->parent) {
			info = tmp;
			break;
		}
	}

	if (info)
		return info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		BT_ERR("Could not allocate info struct");
		return ERR_PTR(-ENOMEM);
	}
	info->parent = dev->parent;
	list_add_tail(&info->list, &btcg2900_devices);
	BT_DBG("CG2900 device added");
	return info;
}

/**
 * device_removed() - Remove device from list if there are no channels left.
 * @info:	BTCG2900 info structure.
 */
static void device_removed(struct btcg2900_info *info)
{
	struct list_head *cursor;
	struct btcg2900_info *tmp;

	if (info->acl || info->cmd || info->evt)
		/* There are still devices active */
		return;

	/* Find the info structure and delete it */
	list_for_each(cursor, &btcg2900_devices) {
		tmp = list_entry(cursor, struct btcg2900_info, list);
		if (tmp == info) {
			list_del(cursor);
			break;
		}
	}
	kfree(info);
	BT_DBG("CG2900 device removed");
}

/**
 * register_bluetooth() - Initialize module.
 *
 * Alloc, init, and register HCI device to BlueZ.
 *
 * Returns:
 *   0 if there is no error.
 *   -ENOMEM if allocation fails.
 *   Error codes from hci_register_dev.
 */
static int register_bluetooth(struct btcg2900_info *info)
{
	int err;
	struct cg2900_user_data *pf_data;

	/* Check if all channels have been probed */
	if (!info->acl || !info->cmd || !info->evt)
		return 0;

	pf_data = dev_get_platdata(info->cmd);

	info->hdev = hci_alloc_dev();
	if (!info->hdev) {
		BT_ERR("Could not allocate mem for CG2900 BT driver");
		return -ENOMEM;
	}

	SET_HCIDEV_DEV(info->hdev, info->parent);
	info->hdev->bus = pf_data->channel_data.bt_bus;
	info->hdev->driver_data = info;
	info->hdev->owner = THIS_MODULE;
	info->hdev->open = btcg2900_open;
	info->hdev->close = btcg2900_close;
	info->hdev->send = btcg2900_send;
	info->hdev->destruct = btcg2900_destruct;

	err = hci_register_dev(info->hdev);
	if (err) {
		BT_ERR("Can not register BTCG2900 HCI device (%d)", err);
		hci_free_dev(info->hdev);
		info->hdev = NULL;
	}

	BT_INFO("CG2900 registered");

	BT_DBG("New reset_state: RESET_IDLE");
	info->reset_state = RESET_IDLE;

	return err;
}

/**
 * probe_common() - Initialize channel and register to BT stack.
 * @dev:		Current device.
 * @info:		BTCG2900 info structure.
 * @hci_data_type:	Data type of this channel, e.g. ACL.
 *
 * Allocate and initialize private data. Register to Bluetooth stack.
 *
 * Returns:
 *   0 if there is no error.
 *   -ENOMEM if allocation fails.
 *   Error codes from register_bluetooth.
 */
static int probe_common(struct platform_device *pdev,
			struct btcg2900_info *info,
			u8 hci_data_type)
{
	int err;
	struct cg2900_user_data *pf_data;
	struct dev_info *dev_info;
	struct device *dev = &pdev->dev;

	dev_info = kzalloc(sizeof(*dev_info), GFP_KERNEL);
	if (!dev_info) {
		BT_ERR("Could not allocate dev_info");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, info);

	pf_data = dev_get_platdata(dev);
	pf_data->dev = dev;
	pf_data->read_cb = hci_read_cb;
	pf_data->reset_cb = hci_reset_cb;

	/* Init and register hdev */
	err = register_bluetooth(info);
	if (err) {
		BT_ERR("HCI Device registration error (%d)", err);
		kfree(dev_info);
		return err;
	}
	dev_info->hci_data_type = hci_data_type;
	cg2900_set_usr(pf_data, dev_info);

	return 0;
}

/**
 * btcg2900_cmd_probe() - Initialize command channel.
 * @pdev:	Platform device.
 *
 * Allocate and initialize private data.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from get_info and probe_common.
 */
static int __devinit btcg2900_cmd_probe(struct platform_device *pdev)
{
	int err;
	struct btcg2900_info *info;

	BT_DBG("Starting CG2900 Command channel");

	info = get_info(&pdev->dev);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->cmd = &pdev->dev;

	err = probe_common(pdev, info, HCI_COMMAND_PKT);
	if (err) {
		BT_ERR("Failed to initialize channel");
		info->cmd = NULL;
		device_removed(info);
		return err;
	}

	return 0;
}

/**
 * btcg2900_acl_probe() - Initialize command channel.
 * @pdev:	Platform device.
 *
 * Allocate and initialize private data.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from get_info and probe_common.
 */
static int __devinit btcg2900_acl_probe(struct platform_device *pdev)
{
	int err;
	struct btcg2900_info *info;

	BT_DBG("Starting CG2900 ACL channel");

	info = get_info(&pdev->dev);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->acl = &pdev->dev;

	err = probe_common(pdev, info, HCI_ACLDATA_PKT);
	if (err) {
		BT_ERR("Failed to initialize channel");
		info->acl = NULL;
		device_removed(info);
		return err;
	}

	return 0;
}

/**
 * btcg2900_evt_probe() - Initialize event channel.
 * @pdev:	Platform device.
 *
 * Allocate and initialize private data.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from get_info and probe_common.
 */
static int __devinit btcg2900_evt_probe(struct platform_device *pdev)
{
	int err;
	struct btcg2900_info *info;

	BT_DBG("Starting CG2900 Event channel");

	info = get_info(&pdev->dev);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->evt = &pdev->dev;

	err = probe_common(pdev, info, HCI_EVENT_PKT);
	if (err) {
		BT_ERR("Failed to initialize channel");
		info->evt = NULL;
		device_removed(info);
		return err;
	}

	return 0;
}

/**
 * remove_common() - Remove channel.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from hci_unregister_dev.
 */
static int remove_common(struct platform_device *pdev,
			 struct btcg2900_info *info)
{
	int err = 0;
	struct cg2900_user_data *pf_data;
	struct dev_info *dev_info;

	pf_data = dev_get_platdata(&pdev->dev);
	dev_info = cg2900_get_usr(pf_data);

	kfree(dev_info);
	cg2900_set_usr(pf_data, NULL);

	if (!info->hdev)
		goto finished;

	BT_INFO("Unregistering CG2900");
	info->hdev->driver_data = NULL;
	err = hci_unregister_dev(info->hdev);
	if (err)
		BT_ERR("Can not unregister HCI device (%d)", err);
	hci_free_dev(info->hdev);
	info->hdev = NULL;

finished:
	device_removed(info);
	return err;
}

/**
 * btcg2900_cmd_remove() - Remove command channel.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from remove_common.
 */
static int __devexit btcg2900_cmd_remove(struct platform_device *pdev)
{
	struct btcg2900_info *info;

	BT_DBG("Removing CG2900 Command channel");

	info = dev_get_drvdata(&pdev->dev);
	info->cmd = NULL;
	return remove_common(pdev, info);
}

/**
 * btcg2900_acl_remove() - Remove ACL channel.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from remove_common.
 */
static int __devexit btcg2900_acl_remove(struct platform_device *pdev)
{
	struct btcg2900_info *info;

	BT_DBG("Removing CG2900 ACL channel");

	info = dev_get_drvdata(&pdev->dev);
	info->acl = NULL;
	return remove_common(pdev, info);
}

/**
 * btcg2900_evt_remove() - Remove event channel.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from remove_common.
 */
static int __devexit btcg2900_evt_remove(struct platform_device *pdev)
{
	struct btcg2900_info *info;

	BT_DBG("Removing CG2900 Event channel");

	info = dev_get_drvdata(&pdev->dev);
	info->evt = NULL;
	return remove_common(pdev, info);
}

static struct platform_driver btcg2900_cmd_driver = {
	.driver = {
		.name	= "cg2900-btcmd",
		.owner	= THIS_MODULE,
	},
	.probe	= btcg2900_cmd_probe,
	.remove	= __devexit_p(btcg2900_cmd_remove),
};

static struct platform_driver btcg2900_acl_driver = {
	.driver = {
		.name	= "cg2900-btacl",
		.owner	= THIS_MODULE,
	},
	.probe	= btcg2900_acl_probe,
	.remove	= __devexit_p(btcg2900_acl_remove),
};

static struct platform_driver btcg2900_evt_driver = {
	.driver = {
		.name	= "cg2900-btevt",
		.owner	= THIS_MODULE,
	},
	.probe	= btcg2900_evt_probe,
	.remove	= __devexit_p(btcg2900_evt_remove),
};

/**
 * btcg2900_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init btcg2900_init(void)
{
	int err;

	BT_DBG("btcg2900_init");

	err = platform_driver_register(&btcg2900_cmd_driver);
	if (err) {
		BT_ERR("Failed to register cmd (%d)", err);
		return err;
	}
	err = platform_driver_register(&btcg2900_acl_driver);
	if (err) {
		BT_ERR("Failed to register acl (%d)", err);
		return err;
	}
	err = platform_driver_register(&btcg2900_evt_driver);
	if (err) {
		BT_ERR("Failed to register evt (%d)", err);
		return err;
	}
	return err;
}

/**
 * btcg2900_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit btcg2900_exit(void)
{
	BT_DBG("btcg2900_exit");
	platform_driver_unregister(&btcg2900_cmd_driver);
	platform_driver_unregister(&btcg2900_acl_driver);
	platform_driver_unregister(&btcg2900_evt_driver);
}

module_init(btcg2900_init);
module_exit(btcg2900_exit);

MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_AUTHOR("Henrik Possung ST-Ericsson");
MODULE_AUTHOR("Josef Kindberg ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Linux Bluetooth HCI H:4 Driver for ST-Ericsson controller");
