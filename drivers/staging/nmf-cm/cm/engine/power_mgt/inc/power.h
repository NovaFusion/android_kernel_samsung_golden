/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Enable a CM power domain by CoreID.
 *
 * \ingroup COMPONENT_INTERNAL
 */
#ifndef __INC_NMF_POWER
#define __INC_NMF_POWER

#include <cm/inc/cm_type.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/dsp/inc/dsp.h>

typedef enum
{
    DISABLE_PWR_MODE = 0x0, //!< Disable mode - CM Power management is disabled. CM Power domain are always enabled and the EEs are loaded by default
    NORMAL_PWR_MODE = 0x1   //!< Normal mode
} t_nmf_power_mode;

extern t_nmf_power_mode powerMode;

PUBLIC t_cm_error cm_PWR_Init(void);
void cm_PWR_SetMode(t_nmf_power_mode aMode);
t_nmf_power_mode cm_PWR_GetMode(void);
t_uint32 cm_PWR_GetMPCMemoryCount(t_nmf_core_id coreId);

typedef enum
{
    MPC_PWR_CLOCK,
    MPC_PWR_AUTOIDLE,
    MPC_PWR_HWIP
} t_mpc_power_request;

PUBLIC t_cm_error cm_PWR_EnableMPC(
        t_mpc_power_request             request,
        t_nmf_core_id                   coreId);
PUBLIC void cm_PWR_DisableMPC(
        t_mpc_power_request             request,
        t_nmf_core_id                   coreId);

PUBLIC t_cm_error cm_PWR_EnableHSEM(void);
PUBLIC void cm_PWR_DisableHSEM(void);

PUBLIC t_cm_error cm_PWR_EnableMemory(
        t_nmf_core_id                   coreId,
        t_dsp_memory_type_id            dspMemType,
        t_cm_physical_address           address,
        t_cm_size                       size);
PUBLIC void cm_PWR_DisableMemory(
        t_nmf_core_id                   coreId,
        t_dsp_memory_type_id            dspMemType,
        t_cm_physical_address           address,
        t_cm_size                       size);


#endif /* __INC_NMF_POWER */
