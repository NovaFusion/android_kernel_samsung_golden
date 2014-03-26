/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author:	Daniel Martensson <Daniel.Martensson@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>

#include <net/caif/caif_hsi.h>

#include <linux/hsi/hsi.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Martensson<daniel.martensson@stericsson.com>");
MODULE_DESCRIPTION("CAIF HSI V3 glue");

#define NR_OF_CAIF_HSI_CHANNELS	2

struct cfhsi_v3 {
	struct list_head list;
	struct cfhsi_dev dev;
	struct platform_device pdev;
	struct hsi_msg *tx_msg;
	struct hsi_msg *rx_msg;
};

/* TODO: Lists are not protected with regards to device removal. */
static LIST_HEAD(cfhsi_dev_list);

static struct hsi_client *cfhsi_client;

static int cfhsi_tx(u8 *ptr, int len, struct cfhsi_dev *dev)
{
	int res;
	struct cfhsi_v3 *cfhsi = NULL;

	/* Check length and alignment. */
	BUG_ON(((int)ptr)%4);
	BUG_ON(len%4);

	cfhsi = container_of(dev, struct cfhsi_v3, dev);

	sg_init_one(cfhsi->tx_msg->sgt.sgl, (const void *)ptr,
		    (unsigned int)len);

	/* Write on HSI device. */
	res = hsi_async_write(cfhsi_client, cfhsi->tx_msg);

	return res;
}

static int cfhsi_rx(u8 *ptr, int len, struct cfhsi_dev *dev)
{
	int res;
	struct cfhsi_v3 *cfhsi = NULL;

	/* Check length and alignment. */
	BUG_ON(((int)ptr)%4);
	BUG_ON(len%4);

	cfhsi = container_of(dev, struct cfhsi_v3, dev);

	sg_init_one(cfhsi->rx_msg->sgt.sgl, (const void *)ptr,
		    (unsigned int)len);

	/* Read from HSI device. */
	res = hsi_async_read(cfhsi_client, cfhsi->rx_msg);

	return res;
}

void cfhsi_v3_release(struct device *dev)
{
	pr_warning("%s:%d cfhsi_v3_release called\n", __FILE__, __LINE__);
}

static inline void cfhsi_v3_destructor(struct hsi_msg *msg)
{
	pr_warning("%s:%d cfhsi_v3_destructor called\n", __FILE__, __LINE__);
}

static inline void cfhsi_v3_read_cb(struct hsi_msg *msg)
{
	struct cfhsi_v3 *cfhsi = (struct cfhsi_v3 *)msg->context;

	/* TODO: Error checking. */
	BUG_ON(!cfhsi->dev.drv);
	BUG_ON(!cfhsi->dev.drv->rx_done_cb);

	cfhsi->dev.drv->rx_done_cb(cfhsi->dev.drv);
}

static inline void cfhsi_v3_write_cb(struct hsi_msg *msg)
{
	struct cfhsi_v3 *cfhsi = (struct cfhsi_v3 *)msg->context;

	/* TODO: Error checking. */
	BUG_ON(!cfhsi->dev.drv);
	BUG_ON(!cfhsi->dev.drv->tx_done_cb);

	cfhsi->dev.drv->tx_done_cb(cfhsi->dev.drv);
}

static int hsi_proto_probe(struct device *dev)
{
	int res;
	int i;
	struct cfhsi_v3 *cfhsi = NULL;

	if (cfhsi_client)
		return -ENODEV; /* TODO: Not correct return. */

	cfhsi_client = to_hsi_client(dev);

	res = hsi_claim_port(cfhsi_client, 0);
	if (res) {
		pr_warning("hsi_proto_probe: hsi_claim_port:%d.\n", res);
		goto err_hsi_claim;
	}

	/* Right now we don't care about AC_WAKE (No power management). */
	cfhsi_client->hsi_start_rx = NULL;
	cfhsi_client->hsi_stop_rx = NULL;

	/* CAIF HSI TX configuration. */
	cfhsi_client->tx_cfg.mode =  HSI_MODE_STREAM;
	cfhsi_client->tx_cfg.flow = HSI_FLOW_SYNC;
	cfhsi_client->tx_cfg.channels = NR_OF_CAIF_HSI_CHANNELS;
	cfhsi_client->tx_cfg.speed = 100000; /* TODO: What speed should be used. */
	cfhsi_client->tx_cfg.arb_mode = HSI_ARB_RR;

	/* CAIF HSI RX configuration. */
	cfhsi_client->rx_cfg.mode =  HSI_MODE_STREAM;
	cfhsi_client->rx_cfg.flow = HSI_FLOW_SYNC;
	cfhsi_client->rx_cfg.channels = NR_OF_CAIF_HSI_CHANNELS;
	cfhsi_client->rx_cfg.speed = 200000; /* TODO: What speed should be used. */
	cfhsi_client->rx_cfg.arb_mode = HSI_ARB_RR;

	res = hsi_setup(cfhsi_client);
	if (res) {
		pr_warning("hsi_proto_probe: hsi_setup:%d.\n", res);
		goto err_hsi_setup;
	}

	/* Make sure that AC_WAKE is high (No power management). */
	res = hsi_start_tx(cfhsi_client);
	if (res) {
		pr_warning("hsi_proto_probe: hsi_start_tx:%d.\n", res);
		goto err_hsi_start_tx;
	}

	/* Connect channels to CAIF HSI devices. */
	for (i = 0; i < NR_OF_CAIF_HSI_CHANNELS; i++) {
		cfhsi = kzalloc(sizeof(struct cfhsi_v3), GFP_KERNEL);
		if (!cfhsi) {
			res = -ENOMEM;
			/* TODO: Error handling. */
		}

		/* Assign HSI client to this CAIF HSI device. */
		cfhsi->dev.cfhsi_tx = cfhsi_tx;
		cfhsi->dev.cfhsi_rx = cfhsi_rx;

		/* Allocate HSI messages. */
		cfhsi->tx_msg = hsi_alloc_msg(1, GFP_KERNEL);
		cfhsi->rx_msg = hsi_alloc_msg(1, GFP_KERNEL);
		if (!cfhsi->tx_msg || !cfhsi->rx_msg) {
			res = -ENOMEM;
			/* TODO: Error handling. */
		}

		/* Set up TX message. */
		cfhsi->tx_msg->cl = cfhsi_client;
		cfhsi->tx_msg->context = (void *)cfhsi;
		cfhsi->tx_msg->complete = cfhsi_v3_write_cb;
		cfhsi->tx_msg->destructor = cfhsi_v3_destructor;
		cfhsi->tx_msg->channel = i;
		cfhsi->tx_msg->ttype = HSI_MSG_WRITE;
		cfhsi->tx_msg->break_frame = 0; /* No break frame. */

		/* Set up RX message. */
		cfhsi->rx_msg->cl = cfhsi_client;
		cfhsi->rx_msg->context = (void *)cfhsi;
		cfhsi->rx_msg->complete = cfhsi_v3_read_cb;
		cfhsi->rx_msg->destructor = cfhsi_v3_destructor;
		cfhsi->rx_msg->channel = i;
		cfhsi->rx_msg->ttype = HSI_MSG_READ;
		cfhsi->rx_msg->break_frame = 0; /* No break frame. */

		/* Initialize CAIF HSI platform device. */
		cfhsi->pdev.name = "cfhsi";
		cfhsi->pdev.dev.platform_data = &cfhsi->dev;
		cfhsi->pdev.dev.release = cfhsi_v3_release;
		/* Use channel number as id. */
		cfhsi->pdev.id = i;
		/* Register platform device. */
		res = platform_device_register(&cfhsi->pdev);
		if (res) {
			pr_warning("hsi_proto_probe: plat_dev_reg:%d.\n", res);
			res = -ENODEV;
			/* TODO: Error handling. */
		}

		/* Add HSI device to device list. */
		list_add_tail(&cfhsi->list, &cfhsi_dev_list);
	}

	return res;

 err_hsi_start_tx:
 err_hsi_setup:
	hsi_release_port(cfhsi_client);
 err_hsi_claim:
	cfhsi_client = NULL;

	return res;
}

static int hsi_proto_remove(struct device *dev)
{
	struct cfhsi_v3 *cfhsi = NULL;
	struct list_head *list_node;
	struct list_head *n;

	if (!cfhsi_client)
		return -ENODEV;

	list_for_each_safe(list_node, n, &cfhsi_dev_list) {
		cfhsi = list_entry(list_node, struct cfhsi_v3, list);
		/* Remove from list. */
		list_del(list_node);
		/* Our HSI device is gone, unregister CAIF HSI device. */
		platform_device_del(&cfhsi->pdev);
		hsi_free_msg(cfhsi->tx_msg);
		hsi_free_msg(cfhsi->rx_msg);
		/* Free memory. */
		kfree(cfhsi);
	}

	hsi_stop_tx(cfhsi_client);
	hsi_release_port(cfhsi_client);

	cfhsi_client = NULL;

	return 0;
}

static int hsi_proto_suspend(struct device *dev, pm_message_t mesg)
{
	/* Not handled. */
	pr_info("hsi_proto_suspend.\n");

	return 0;
}

static int hsi_proto_resume(struct device *dev)
{
	/* Not handled. */
	pr_info("hsi_proto_resume.\n");

	return 0;
}

static struct hsi_client_driver cfhsi_v3_driver = {
	.driver = {
		.name = "cfhsi_v3_driver",
		.owner	= THIS_MODULE,
		.probe = hsi_proto_probe,
		.remove = __devexit_p(hsi_proto_remove),
		.suspend = hsi_proto_suspend,
		.resume = hsi_proto_resume,
	},
};

static int __init cfhsi_v3_init(void)
{
	int res;

	/* Register protocol driver for HSI interface. */
	res =  hsi_register_client_driver(&cfhsi_v3_driver);
	if (res)
		pr_warning("Failed to register CAIF HSI V3 driver.\n");

	return res;
}

static void __exit cfhsi_v3_exit(void)
{
	struct cfhsi_v3 *cfhsi = NULL;
	struct list_head *list_node;
	struct list_head *n;

	/* Unregister driver. */
	hsi_unregister_client_driver(&cfhsi_v3_driver);

	if (!cfhsi_client)
		return;

	list_for_each_safe(list_node, n, &cfhsi_dev_list) {
		cfhsi = list_entry(list_node, struct cfhsi_v3, list);
		platform_device_del(&cfhsi->pdev);
		hsi_free_msg(cfhsi->tx_msg);
		hsi_free_msg(cfhsi->rx_msg);
		kfree(cfhsi);
	}

	hsi_stop_tx(cfhsi_client);
	hsi_release_port(cfhsi_client);

	cfhsi_client = NULL;
}

module_init(cfhsi_v3_init);
module_exit(cfhsi_v3_exit);
