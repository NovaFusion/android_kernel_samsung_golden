/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_driver.h
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver char interface header.
 */
#ifndef _MODEM_CHAR_H_
#define _MODEM_CHAR_H_

#include <linux/modem/m6718_spi/modem_driver.h>

int modem_isa_init(struct modem_spi_dev *modem_spi_dev);
void modem_isa_exit(struct modem_spi_dev *modem_spi_dev);

int modem_isa_queue_msg(struct message_queue *q, u32 size);
int modem_isa_msg_size(struct message_queue *q);
int modem_isa_unqueue_msg(struct message_queue *q);
void modem_isa_reset(struct modem_spi_dev *modem_spi_dev);
int modem_get_cdev_index(u8 l2_header);
int modem_get_cdev_l2header(u8 idx);

#endif /* _MODEM_CHAR_H_ */
