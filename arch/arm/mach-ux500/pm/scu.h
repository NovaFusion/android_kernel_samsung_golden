/*
 * Copyright (C) 2009 ST-Ericsson SA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASMARM_ARCH_SCU_H
#define __ASMARM_ARCH_SCU_H

#include <mach/hardware.h>

#define SCU_BASE	U8500_SCU_BASE
/*
 *  * SCU registers
 *   */
#define SCU_CTRL        0x00
#define SCU_CONFIG      0x04
#define SCU_CPU_STATUS      0x08
#define SCU_INVALIDATE      0x0c
#define SCU_FPGA_REVISION   0x10

#endif
