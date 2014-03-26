/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <share/communication/inc/nmf_fifo_desc.h>
#include <cm/engine/semaphores/inc/semaphores.h>
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include "../inc/nmf_fifo_arm.h"

#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/trace/inc/trace.h>

/* define value of fifo magic number */
#define NMF_FIFO_MAGIC_NB                   0xF1F0BEEF

PRIVATE t_uint16 fifo_getCount(
    t_uint16 writeIndex,
    t_uint16 readIndex,
    t_uint16 fifoSize
)
{
    if (writeIndex >= readIndex) {return writeIndex - readIndex;}
    else {return fifoSize - readIndex + writeIndex;}
}

PRIVATE t_uint16 fifo_incrementIndex(
    t_uint16 index,
    t_uint16 wrappingValue
)
{
    if (++index == wrappingValue) {index = 0;}

    return index;
}

PUBLIC t_uint16 fifo_normalizeDepth(t_uint16 requestedDepth)
{
    /* with new implementation we don't align on power of two */
    return requestedDepth;
}

PUBLIC t_nmf_fifo_arm_desc* fifo_alloc(
        t_nmf_core_id pusherCoreId, t_nmf_core_id poperCoreId,
        t_uint16 size_in_16bit, t_uint16 nbElem, t_uint16 nbExtendedSharedFields,
        t_dsp_memory_type_id memType, t_dsp_memory_type_id memExtendedFieldType, t_cm_domain_id domainId)
{
    t_uint16 realNbElem = nbElem + 1;/* we need one more elem in new implementation */
    t_uint16 sizeToAlloc = sizeof(t_nmf_fifo_desc) + ((size_in_16bit<<1)*realNbElem);
    t_nmf_fifo_arm_desc *pArmFifoDesc;

    pArmFifoDesc = (t_nmf_fifo_arm_desc*)OSAL_Alloc(sizeof (t_nmf_fifo_arm_desc));
    if (pArmFifoDesc == NULL)
        goto errorde;

    pArmFifoDesc->chunkHandle = cm_DM_Alloc(domainId, memType,
            (sizeToAlloc/2), CM_MM_ALIGN_2WORDS, TRUE); /* size in 16-bit since we use EXT16 memory */
    if (pArmFifoDesc->chunkHandle == INVALID_MEMORY_HANDLE)
        goto errorsh;

    pArmFifoDesc->magic = NMF_FIFO_MAGIC_NB;
    pArmFifoDesc->pusherCoreId = pusherCoreId;
    pArmFifoDesc->poperCoreId = poperCoreId;

    pArmFifoDesc->fifoDesc = (t_nmf_fifo_desc *)cm_DSP_GetHostLogicalAddress(pArmFifoDesc->chunkHandle);
    cm_DSP_GetDspAddress(pArmFifoDesc->chunkHandle, &pArmFifoDesc->dspAdress);

    pArmFifoDesc->fifoDescShadow = pArmFifoDesc->fifoDesc;
    cm_DSP_GetDspDataAddressInfo(cm_DM_GetDomainCoreId(domainId), pArmFifoDesc->dspAdress, &pArmFifoDesc->dspAddressInfo);

    pArmFifoDesc->extendedFieldHandle = INVALID_MEMORY_HANDLE;
    pArmFifoDesc->extendedField = NULL;

    pArmFifoDesc->fifoDesc->elemSize = size_in_16bit;
    pArmFifoDesc->fifoDesc->fifoFullValue = nbElem;
    pArmFifoDesc->fifoDesc->wrappingValue = realNbElem;

    pArmFifoDesc->fifoDesc->semId = cm_SEM_Alloc(pusherCoreId, poperCoreId);
    pArmFifoDesc->fifoDesc->readIndex = 0;
    pArmFifoDesc->fifoDesc->writeIndex = 0;

    LOG_INTERNAL(2, "\n##### Fifo alloc 0x%x (0x%x)\n\n", pArmFifoDesc, pArmFifoDesc->fifoDesc, 0, 0, 0, 0);

    if (nbExtendedSharedFields >= 1)
    {
        if(poperCoreId == ARM_CORE_ID)
        {
            /* Optimization: Don't put extended Field in DSP memory since use only by ARM if popper */
            pArmFifoDesc->extendedField = (t_shared_field*)OSAL_Alloc(nbExtendedSharedFields * sizeof(t_shared_field));
            if (pArmFifoDesc->extendedField == NULL)
                goto errorex;

            pArmFifoDesc->fifoDesc->extendedField = (t_uint32)pArmFifoDesc->extendedField;
        }
        else
        {
            pArmFifoDesc->extendedFieldHandle = cm_DM_Alloc(domainId, memExtendedFieldType,
                    nbExtendedSharedFields * sizeof(t_shared_field) / 4, CM_MM_ALIGN_WORD, TRUE);
            if (pArmFifoDesc->extendedFieldHandle == INVALID_MEMORY_HANDLE)
                goto errorex;

            pArmFifoDesc->extendedField = (t_shared_field*)cm_DSP_GetHostLogicalAddress(pArmFifoDesc->extendedFieldHandle);
            cm_DSP_GetDspAddress(pArmFifoDesc->extendedFieldHandle, (t_uint32*)&pArmFifoDesc->fifoDesc->extendedField);
        }

        pArmFifoDesc->extendedField[EXTENDED_FIELD_BCTHIS_OR_TOP] = (t_shared_field)0;
    }

    return pArmFifoDesc;

errorex:
    (void)cm_DM_Free(pArmFifoDesc->chunkHandle, TRUE);
errorsh:
    OSAL_Free(pArmFifoDesc);
errorde:
    return NULL;
}

PUBLIC t_uint32 fifo_isFifoIdValid(t_nmf_fifo_arm_desc *pArmFifo)
{
    if (((t_uint32)pArmFifo & CM_MM_ALIGN_WORD) != 0) {return FALSE;}
    if (pArmFifo->magic == NMF_FIFO_MAGIC_NB)  {return TRUE;}
    else {return FALSE;}
}

PUBLIC void fifo_free(t_nmf_fifo_arm_desc *pArmFifo)
{
    CM_ASSERT(pArmFifo->pusherCoreId != ARM_CORE_ID || pArmFifo->poperCoreId != ARM_CORE_ID);

    pArmFifo->magic = ~NMF_FIFO_MAGIC_NB;

    if(pArmFifo->extendedFieldHandle != INVALID_MEMORY_HANDLE)
        (void)cm_DM_Free(pArmFifo->extendedFieldHandle, TRUE);
    else if(pArmFifo->extendedField != NULL)
        OSAL_Free(pArmFifo->extendedField);

    (void)cm_DM_Free(pArmFifo->chunkHandle, TRUE);
    OSAL_Free(pArmFifo);
}

PUBLIC t_shared_addr fifo_getAndAckNextElemToWritePointer(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_shared_addr retValue;

    retValue = fifo_getNextElemToWritePointer(pArmFifo);
    if (retValue != 0)
    {
        fifo_acknowledgeWrite(pArmFifo);
    }

    return retValue;
}

PUBLIC t_shared_addr fifo_getAndAckNextElemToReadPointer(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_shared_addr retValue;

    retValue = fifo_getNextElemToReadPointer(pArmFifo);
    if (retValue != 0)
    {
        fifo_acknowledgeRead(pArmFifo);
    }

    return retValue;
}

PUBLIC t_shared_addr fifo_getNextElemToWritePointer(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_shared_addr retValue = 0;
    t_nmf_fifo_desc *pDesc;
    t_uint16 count;

    if ((NULL == pArmFifo) || (NULL == (pDesc = pArmFifo->fifoDesc)))
	    return 0;

    count = fifo_getCount(pDesc->writeIndex, pDesc->readIndex,pDesc->wrappingValue);
    if (count < pDesc->fifoFullValue)
    {
        retValue = ((t_shared_addr)pDesc + sizeof(t_nmf_fifo_desc) + (pDesc->writeIndex*(pDesc->elemSize<<1)));
    }

    return retValue;
}

PUBLIC t_shared_addr fifo_getNextElemToReadPointer(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_shared_addr retValue = 0;
    t_nmf_fifo_desc *pDesc;
    t_uint16 count;

    if ((NULL == pArmFifo) || (NULL == (pDesc = pArmFifo->fifoDesc)))
	    return 0;

    count = fifo_getCount(pDesc->writeIndex, pDesc->readIndex,pDesc->wrappingValue);
    if (count != 0)
    {
        retValue = ((t_shared_addr)pDesc+ sizeof(t_nmf_fifo_desc) + (pDesc->readIndex*(pDesc->elemSize<<1)));
    }

    return retValue;
}

PUBLIC void fifo_acknowledgeRead(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_nmf_fifo_desc *pDesc = pArmFifo->fifoDesc;

    pDesc->readIndex = fifo_incrementIndex(pDesc->readIndex, pDesc->wrappingValue);
}

PUBLIC void fifo_acknowledgeWrite(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_nmf_fifo_desc *pDesc = pArmFifo->fifoDesc;

    pDesc->writeIndex = fifo_incrementIndex(pDesc->writeIndex, pDesc->wrappingValue);
}

PUBLIC void fifo_coms_acknowledgeWriteAndInterruptGeneration(t_nmf_fifo_arm_desc *pArmFifo)
{
    t_nmf_fifo_desc *pDesc = pArmFifo->fifoDesc;

    fifo_acknowledgeWrite(pArmFifo);
    //Be sure before generate irq that fifo has been updated
    OSAL_mb();
    cm_SEM_GenerateIrq[pArmFifo->poperCoreId](pArmFifo->poperCoreId, pDesc->semId);
    //cm_SEM_Take[pArmFifo->poperCoreId](pArmFifo->poperCoreId, pDesc->semId);
    //cm_SEM_GiveWithInterruptGeneration[pArmFifo->poperCoreId](pArmFifo->poperCoreId, pDesc->semId);
}

PUBLIC t_cm_error fifo_params_setSharedField(t_nmf_fifo_arm_desc *pArmFifo, t_uint32 sharedFieldIndex, t_shared_field value)
{
    pArmFifo->extendedField[sharedFieldIndex] = value;

    return CM_OK;
}

