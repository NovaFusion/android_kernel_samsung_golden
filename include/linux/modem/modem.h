/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 *
 * Heavily adapted from Regulator framework
 */
#ifndef __MODEM_H__
#define __MODEM_H__

#include <linux/device.h>

struct modem_dev;

struct modem_ops {
	void (*request)(struct modem_dev *);
	void (*release)(struct modem_dev *);
	int (*is_requested)(struct modem_dev *);
};

struct modem_desc {
	const char *name;
	int id;
	struct modem_ops *ops;
	struct module *owner;
};

struct modem_dev {
	struct modem_desc *desc;
	int use_count;
	int open_count;
	int exclusive;

	struct list_head modem_list;

	struct list_head client_list;

	struct blocking_notifier_head notifier;
	struct mutex mutex;
	struct module *owner;
	struct device dev;
	void *modem_data;
};

#ifdef CONFIG_MODEM
struct modem_dev *modem_register(struct modem_desc *modem_desc,
		struct device *dev,
		void *driver_data);
void modem_unregister(struct modem_dev *mdev);

#else
static inline struct modem_dev *modem_register(struct modem_desc *modem_desc,
		struct device *dev, void *driver_data)
{
	return NULL;
}

static inline void modem_unregister(struct modem_dev *mdev)
{
}
#endif
#endif /* __MODEM_H__ */
