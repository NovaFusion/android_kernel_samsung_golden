/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

/** Peripherals description.
 * Some of these values are taken from kernel header description (which should be the
 * right place of these definition); the missing ones are defined here.
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
#include <generated/autoconf.h>
#else
#include <linux/autoconf.h>
#endif

enum {
	ESRAM_12,
	ESRAM_34,
	NB_ESRAM,
};

/** MPCs */
enum {
	SVA,
	SIA,
	NB_MPC,
};
#define COREIDX(id) (id-1)

/** Base address of shared SDRAM: use upper SDRAM. We reserve a rather */
#define SDRAM_CODE_SIZE_SVA (2*ONE_KB)
#define SDRAM_CODE_SIZE_SIA (2*ONE_KB)
#define SDRAM_DATA_SIZE     (8*ONE_KB)

extern bool cfgCommunicationLocationInSDRAM;
extern bool cfgSemaphoreTypeHSEM;
extern int cfgESRAMSize;

int init_config(void);

#define DECLARE_MPC_PARAM(mpc, sdramDataSize, extension, ybank)		\
	static unsigned int cfgMpcYBanks_##mpc = ybank;			\
	module_param(cfgMpcYBanks_##mpc, uint, S_IRUGO);		\
	MODULE_PARM_DESC(cfgMpcYBanks_##mpc, "Nb of Y-Ram banks used on " #mpc); \
									\
	static int          cfgSchedulerTypeHybrid_##mpc = 1;		\
	module_param(cfgSchedulerTypeHybrid_##mpc, bool, S_IRUGO);	\
	MODULE_PARM_DESC(cfgSchedulerTypeHybrid_##mpc, "Scheduler used on " #mpc " (Hybrid or Synchronous)"); \
									\
	static unsigned int cfgMpcSDRAMCodeSize_##mpc = SDRAM_CODE_SIZE_##mpc; \
	module_param(cfgMpcSDRAMCodeSize_##mpc, uint, S_IRUGO);		\
	MODULE_PARM_DESC(cfgMpcSDRAMCodeSize_##mpc, "Size of code segment on " #mpc " (in Kb)"); \
									\
	static unsigned int cfgMpcSDRAMDataSize_##mpc = sdramDataSize;	\
	module_param(cfgMpcSDRAMDataSize_##mpc, uint, S_IRUGO);		\
	MODULE_PARM_DESC(cfgMpcSDRAMDataSize_##mpc, "Size of data segment on " #mpc " (in Kb)" extension)

#endif
