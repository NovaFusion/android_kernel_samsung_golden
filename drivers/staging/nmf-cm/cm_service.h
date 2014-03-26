/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

/** \file cm_service.c
 *
 * Nomadik Multiprocessing Framework Linux Driver
 *
 */

#ifndef CM_SERVICE_H
#define CM_SERVICE_H

#include <linux/interrupt.h>

extern unsigned long service_tasklet_data;
extern struct tasklet_struct cmld_service_tasklet;
void dispatch_service_msg(struct osal_msg *msg);

#endif
