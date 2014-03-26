/*
 * Copyright (C) ST-Ericsson SA 2010
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */

#ifndef __CMDMA_H
#define __CMDMA_H
#include "cmioctl.h"

int cmdma_setup_relink_area(
    unsigned int mem_addr,
    unsigned int per_addr,
    unsigned int segments,
    unsigned int segmentsize,
    unsigned int LOS,
    enum cmdma_type type);

void cmdma_stop_dma(void);
int cmdma_init(void);
void cmdma_destroy(void);
#endif
