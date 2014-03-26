/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/perfmeter/inc/mpcload.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>

#include <cm/engine/api/perfmeter_engine.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

#include <cm/engine/trace/inc/trace.h>

#define PERFMETER_MAX_RETRIES                           32
#define PERFMETER_DATA_WORD_NB                          7

/* private type */
typedef struct {
    t_memory_handle perfmeterDataHandle;
    t_cm_logical_address perfmeterDataAddr;
} t_mpcLoad;

/* private globals */
t_mpcLoad mpcLoad_i[NB_CORE_IDS];

/* engine api */
PUBLIC EXPORT_SHARED t_cm_error CM_GetMpcLoadCounter(
    t_nmf_core_id coreId,
    t_cm_mpc_load_counter *pMpcLoadCounter
)
{
    t_uint24 data[PERFMETER_DATA_WORD_NB];
    t_uint32 i;
    t_uint64 prcmuBeforeAttributes;
    t_uint32 retryCounter = 0;
    volatile t_uint32 *pData;

    pMpcLoadCounter->totalCounter = 0;
    pMpcLoadCounter->loadCounter = 0;
    /* check core id is an mpc */
    if (coreId < FIRST_MPC_ID || coreId > LAST_CORE_ID) {return CM_INVALID_PARAMETER;}

    /* check core has been booted */
    pData = (t_uint32 *) mpcLoad_i[coreId].perfmeterDataAddr;
    if (pData == NULL) {return CM_OK;}

    do {
        prcmuBeforeAttributes = OSAL_GetPrcmuTimer();
        /* get attributes */
        do
        {
            for(i = 0;i < PERFMETER_DATA_WORD_NB;i++)
                data[i] = pData[i];
        }
        while(((data[0] & 0xff0000) != (data[1] & 0xff0000) || (data[0] & 0xff0000) != (data[2] & 0xff0000) ||
               (data[0] & 0xff0000) != (data[3] & 0xff0000) || (data[0] & 0xff0000) != (data[4] & 0xff0000) ||
               (data[0] & 0xff0000) != (data[5] & 0xff0000) || (data[0] & 0xff0000) != (data[6] & 0xff0000) ||
               (data[0] & 0xff0000) != (data[6] & 0xff0000))
               && retryCounter-- < PERFMETER_MAX_RETRIES); // check data coherence
        if (retryCounter >= PERFMETER_MAX_RETRIES)
            return CM_MPC_NOT_RESPONDING;

        /* read forever counter for totalCounter */
        pMpcLoadCounter->totalCounter = OSAL_GetPrcmuTimer();
    } while(pMpcLoadCounter->totalCounter - prcmuBeforeAttributes >= 32); //we loop until it seems we have not be preempt too long (< 1ms)

    /* we got coherent data, use them */
    pMpcLoadCounter->loadCounter = ((data[0] & (t_uint64)0xffff) << 32) + ((data[1] & (t_uint64)0xffff) << 16) + ((data[2] & (t_uint64)0xffff) << 0);
    //fix load counter if needed
    if ((data[6] & 0xffff) == 1) {
        t_uint64 lastEvent;

        lastEvent = ((data[3] & (t_uint64)0xffff) << 32) + ((data[4] & (t_uint64)0xffff) << 16) + ((data[5] & (t_uint64)0xffff) << 0);
        pMpcLoadCounter->loadCounter += pMpcLoadCounter->totalCounter - lastEvent;
    }

    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_getMpcLoadCounter(
    t_nmf_core_id coreId,
    t_cm_mpc_load_counter *pMpcLoadCounter
)
{
	t_cm_error error;

    OSAL_LOCK_API();
    error = CM_GetMpcLoadCounter(coreId, pMpcLoadCounter);
    OSAL_UNLOCK_API();
    return error;
}

/* internal api */
PUBLIC t_cm_error cm_PFM_allocatePerfmeterDataMemory(t_nmf_core_id coreId, t_cm_domain_id domainId)
{
    t_cm_error error = CM_OK;
    t_mpcLoad *pMpcLoad = (t_mpcLoad *) &mpcLoad_i[coreId];

    pMpcLoad->perfmeterDataHandle = cm_DM_Alloc(domainId, SDRAM_EXT24, PERFMETER_DATA_WORD_NB, CM_MM_ALIGN_WORD, TRUE);
    if (pMpcLoad->perfmeterDataHandle == INVALID_MEMORY_HANDLE) {
        error = CM_NO_MORE_MEMORY;
        ERROR("CM_NO_MORE_MEMORY: Unable to allocate perfmeter\n", 0, 0, 0, 0, 0, 0);
    } else {
        t_uint32 mmdspAddr;

        pMpcLoad->perfmeterDataAddr = cm_DSP_GetHostLogicalAddress(pMpcLoad->perfmeterDataHandle);
        cm_DSP_GetDspAddress(pMpcLoad->perfmeterDataHandle, &mmdspAddr);
        cm_writeAttribute(cm_EEM_getExecutiveEngine(coreId)->instance, "rtos/perfmeter/perfmeterDataAddr", mmdspAddr);
    }

    return error;
}

PUBLIC void cm_PFM_deallocatePerfmeterDataMemory(t_nmf_core_id coreId)
{
    mpcLoad_i[coreId].perfmeterDataAddr = 0;
    cm_DM_Free(mpcLoad_i[coreId].perfmeterDataHandle, TRUE);
}
