/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_driver.h
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header.
 */
#ifndef _MODEM_PROTOCOL_H_
#define _MODEM_PROTOCOL_H_

#include <linux/spi/spi.h>

void modem_protocol_init(void);
int modem_protocol_probe(struct spi_device *sdev);
void modem_protocol_exit(void);
bool modem_protocol_is_busy(struct spi_device *sdev);
bool modem_protocol_channel_is_open(u8 channel);
int modem_protocol_suspend(struct spi_device *sdev);
int modem_protocol_resume(struct spi_device *sdev);

#endif /* _MODEM_PROTOCOL_H_ */
