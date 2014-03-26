/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/modem/shrm/shrm_driver.h>
#include <linux/modem/shrm/shrm_private.h>
#include <linux/modem/shrm/shrm_config.h>
#include <linux/modem/shrm/shrm.h>

int u8500_kernel_client(u8 l2_header, void *data);
int u8500_kernel_client_init(struct shrm_dev *shrm);
void u8500_kernel_client_exit(void);
