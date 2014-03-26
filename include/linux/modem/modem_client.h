/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 *
 * Heavily adapted from Regulator framework
 */
#ifndef __MODEM_CLIENT_H__
#define __MODEM_CLIENT_H__

#include <linux/device.h>

struct modem;

#ifdef CONFIG_MODEM
struct modem *modem_get(struct device *dev, const char *id);
void modem_put(struct modem *modem);
void modem_request(struct modem *modem);
void modem_release(struct modem *modem);
int modem_is_requested(struct modem *modem);
int modem_get_usage(struct modem *modem);

#else

static inline struct modem *modem_get(struct device *dev, const char *id)
{
	return NULL;
}

static inline void modem_put(struct modem *modem)
{
}

static inline void modem_request(struct modem *modem)
{
}

static inline void modem_release(struct modem *modem)
{
}

static inline int modem_is_requested(struct modem *modem)
{
	return 0;
}

static inline int modem_get_usage(struct modem *modem)
{
	return 0;
}
#endif
#endif /* __MODEM_CLIENT_H__ */
