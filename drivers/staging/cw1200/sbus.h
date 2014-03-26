/*
 * Common sbus abstraction layer interface for cw1200 wireless driver
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_SBUS_H
#define CW1200_SBUS_H

/*
 * sbus priv forward definition.
 * Implemented and instantiated in particular modules.
 */
struct sbus_priv;

typedef void (*sbus_irq_handler)(void *priv);

struct sbus_ops {
	int (*sbus_memcpy_fromio)(struct sbus_priv *self, unsigned int addr,
					void *dst, int count);
	int (*sbus_memcpy_toio)(struct sbus_priv *self, unsigned int addr,
					const void *src, int count);
	void (*lock)(struct sbus_priv *self);
	void (*unlock)(struct sbus_priv *self);
	int (*irq_subscribe)(struct sbus_priv *self, sbus_irq_handler handler,
				void *priv);
	int (*irq_unsubscribe)(struct sbus_priv *self);
	int (*reset)(struct sbus_priv *self);
	size_t (*align_size)(struct sbus_priv *self, size_t size);
};

#endif /* CW1200_SBUS_H */
