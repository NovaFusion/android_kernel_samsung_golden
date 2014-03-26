/*
 * Device handling thread interface for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_BH_H
#define CW1200_BH_H

/* extern */ struct cw1200_common;

/* TODO: 512, actually. Was increased to 1024
 * for compatibility with particular FW. */
#define SDIO_BLOCK_SIZE (1024)

int cw1200_register_bh(struct cw1200_common *priv);
void cw1200_unregister_bh(struct cw1200_common *priv);
void cw1200_irq_handler(struct cw1200_common *priv);
void cw1200_bh_wakeup(struct cw1200_common *priv);
/* Must be called from BH thread. */
void cw1200_enable_powersave(struct cw1200_common *priv,
			     bool enable);
int wsm_release_tx_buffer(struct cw1200_common *priv, int count);

#endif /* CW1200_BH_H */
