/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

/** \file configuration.c
 *
 * Nomadik Multiprocessing Framework Linux Driver
 *
 */

#include "osal-kernel.h"
#include <cm/engine/api/configuration_engine.h>
#include <cm/engine/configuration/inc/configuration_status.h>
#include <cm/engine/power_mgt/inc/power.h>
#include <cm/engine/api/control/irq_engine.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>

/* Embedded Static RAM base address */
/* 8500 config: 0-64k: secure */
#define U8500_ESRAM_CM_BASE (U8500_ESRAM_BASE + U8500_ESRAM_DMA_LCPA_OFFSET)
/* 9540 config: ESRAM base address is the same s on 8500  (incl. DMA part) */
#define U9540_ESRAM_CM_BASE (U8500_ESRAM_BASE + U9540_ESRAM_DMA_LCPA_OFFSET)

/*
 * 8500 Embedded ram size for CM (in Kb)
 * 5 banks of 128k: skip the first half bank (secure) and the last
 * one (used for MCDE/B2R2), but include DMA part (4k after the secure part)
 * to give access from DSP side
 */
#define U8500_ESRAM_CM_SIZE 448
/* 9540 Embedded ram size for CM (in Kb) */
#define U9540_ESRAM_CM_SIZE 406

/* Per-driver environment */
struct OsalEnvironment osalEnv =
{
	.mpc = {
		{
			.coreId           = SVA_CORE_ID,
			.name             = "sva",
			.base_phys        = (void*)U8500_SVA_BASE,
			.interrupt0       = IRQ_DB8500_SVA,
			.interrupt1       = IRQ_DB8500_SVA2,
			.mmdsp_regulator  = NULL,
			.pipe_regulator   = NULL,
			.monitor_tsk      = NULL,
			.hwmem_code       = NULL,
			.hwmem_data       = NULL,
		},
		{
			.coreId           = SIA_CORE_ID,
			.name             = "sia",
			.base_phys        = (void*)U8500_SIA_BASE,
			.interrupt0       = IRQ_DB8500_SIA,
			.interrupt1       = IRQ_DB8500_SIA2,
			.mmdsp_regulator  = NULL,
			.pipe_regulator   = NULL,
			.monitor_tsk      = NULL,
			.hwmem_code       = NULL,
			.hwmem_data       = NULL,
		}
	},
	.esram_regulator = { NULL, NULL},
	.dsp_sleep = {
		.sia_auto_pm_enable = PRCMU_AUTO_PM_OFF,
		.sia_power_on       = 0,
		.sia_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF,
		.sva_auto_pm_enable = PRCMU_AUTO_PM_OFF,
		.sva_power_on       = 0,
		.sva_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF,		
	},
	.dsp_idle = {
		.sia_auto_pm_enable = PRCMU_AUTO_PM_OFF,
		.sia_power_on       = 0,
		.sia_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF,
		.sva_auto_pm_enable = PRCMU_AUTO_PM_OFF,
		.sva_power_on       = 0,
		.sva_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF,		
	},
};

module_param_cb(cm_debug_level, &param_ops_int,
                  &cm_debug_level, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(cm_debug_level, "Debug level of NMF Core");

module_param_cb(cm_error_break, &param_ops_bool,
                  &cm_error_break, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(cm_error_break, "Stop on error (in an infinite loop, for debugging purpose)");

module_param_cb(cmIntensiveCheckState, &param_ops_bool,
                  &cmIntensiveCheckState, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(cmIntensiveCheckState, "Add additional intensive checks");

DECLARE_MPC_PARAM(SVA, SDRAM_DATA_SIZE, "", 1);

DECLARE_MPC_PARAM(SIA, 0, "\n\t\t\t(0 means shared with SVA)", 2);

bool cfgCommunicationLocationInSDRAM = true;
module_param(cfgCommunicationLocationInSDRAM, bool, S_IRUGO);
MODULE_PARM_DESC(cfgCommunicationLocationInSDRAM, "Location of communications (SDRAM or ESRAM)");

bool cfgSemaphoreTypeHSEM = true;
module_param(cfgSemaphoreTypeHSEM, bool, S_IRUGO);
MODULE_PARM_DESC(cfgSemaphoreTypeHSEM, "Semaphore used (HSEM or LSEM)");

int cfgESRAMSize;
module_param(cfgESRAMSize, uint, S_IRUGO);
MODULE_PARM_DESC(cfgESRAMSize, "Size of ESRAM used in the CM (in Kb)");

static int param_set_osttrace_nb(const char *val, const struct kernel_param *kp)
{
	t_ee_state *state;
	int coreId, rv;
	u32 oldSize;

	for (coreId = FIRST_MPC_ID; coreId <= LAST_MPC_ID; coreId++) {
		state = cm_EEM_getExecutiveEngine(coreId);
		if (&(state->traceBufferSize) == kp->arg)
			break;
	}

	oldSize = state->traceBufferSize;
	rv = param_set_uint(val, kp);
	if (rv)
		return rv;

	if (CM_ENGINE_resizeTraceBuffer(coreId, oldSize) == CM_OK) {
		pr_info("[CM]: OST Trace buffer resizing done successfully "
			"for %s DSP\n", osalEnv.mpc[COREIDX(coreId)].name);
		return 0;
	} else {
		return -EINVAL;
	}
}

static struct kernel_param_ops param_ops_osttrace_nb = {
        .set = param_set_osttrace_nb,
        .get = param_get_uint,
};

module_param_cb(sva_osttrace_nb,
		&param_ops_osttrace_nb,
		&eeState[SVA_CORE_ID].traceBufferSize, S_IWUSR|S_IRUGO);
module_param_cb(sia_osttrace_nb,
		&param_ops_osttrace_nb,
		&eeState[SIA_CORE_ID].traceBufferSize, S_IWUSR|S_IRUGO);

static int param_set_powerMode(const char *val, const struct kernel_param *kp)
{
	/* No equals means "set"... */
	if (!val) val = "1";

	/* One of =[yYnN01] */
	switch (val[0]) {
	case 'y': case 'Y': case '1':
		CM_ENGINE_SetMode(CM_CMD_DBG_MODE, 0);
		break;
	case 'n': case 'N': case '0':
		CM_ENGINE_SetMode(CM_CMD_DBG_MODE, 1);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct kernel_param_ops param_ops_powerMode = {
        .set = param_set_powerMode,
        .get = param_get_bool,
};

module_param_cb(powerMode, &param_ops_powerMode, &powerMode, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(powerMode, "DSP power mode enable");

int init_config(void)
{
	if (cfgMpcSDRAMCodeSize_SVA == 0 || cfgMpcSDRAMCodeSize_SIA == 0) {
		pr_err("SDRAM code size must be greater than 0\n");
		return -EINVAL;
	}

	if (cfgMpcSDRAMDataSize_SVA == 0) {
		pr_err("SDRAM data size for SVA must be greater than 0\n");
		return -EINVAL;
	}

	if (cpu_is_u9540()) {
		osalEnv.esram_base_phys = U9540_ESRAM_CM_BASE;
		cfgESRAMSize = U9540_ESRAM_CM_SIZE;
	} else {
		osalEnv.esram_base_phys = U8500_ESRAM_CM_BASE;
		cfgESRAMSize = U8500_ESRAM_CM_SIZE;
	}

	osalEnv.mpc[SVA].nbYramBanks     = cfgMpcYBanks_SVA;
	osalEnv.mpc[SVA].eeId            = cfgSchedulerTypeHybrid_SVA ? HYBRID_EXECUTIVE_ENGINE : SYNCHRONOUS_EXECUTIVE_ENGINE;
	osalEnv.mpc[SVA].sdram_code.size = cfgMpcSDRAMCodeSize_SVA * ONE_KB;
	osalEnv.mpc[SVA].sdram_data.size = cfgMpcSDRAMDataSize_SVA * ONE_KB;
	osalEnv.mpc[SVA].base.size       = 128*ONE_KB; //we expose only TCM24
	init_waitqueue_head(&osalEnv.mpc[SVA].trace_waitq);

	osalEnv.mpc[SIA].nbYramBanks     = cfgMpcYBanks_SIA;
	osalEnv.mpc[SIA].eeId            = cfgSchedulerTypeHybrid_SIA ? HYBRID_EXECUTIVE_ENGINE : SYNCHRONOUS_EXECUTIVE_ENGINE;
	osalEnv.mpc[SIA].sdram_code.size = cfgMpcSDRAMCodeSize_SIA * ONE_KB;
	osalEnv.mpc[SIA].sdram_data.size = cfgMpcSDRAMDataSize_SIA * ONE_KB;
	osalEnv.mpc[SIA].base.size       = 128*ONE_KB; //we expose only TCM24
	init_waitqueue_head(&osalEnv.mpc[SIA].trace_waitq);

	return 0;
}
