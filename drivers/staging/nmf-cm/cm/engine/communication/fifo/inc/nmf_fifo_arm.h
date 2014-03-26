/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#ifndef __INC_NMF_FIFO_ARM
#define __INC_NMF_FIFO_ARM

#include <cm/inc/cm_type.h>
#include <share/communication/inc/nmf_fifo_desc.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/memory/inc/domain.h>

/*
 * ARM Fifo descriptor (encapsulate the share one)
 */
typedef struct
{
    t_uint32 magic;
    t_memory_handle chunkHandle;
    t_nmf_core_id pusherCoreId;
    t_nmf_core_id poperCoreId;
    t_shared_addr dspAdress;
    t_dsp_address_info dspAddressInfo;
    t_nmf_fifo_desc *fifoDesc;       //used for all fifo operations and systematically updated by the migrated offset (see cm_AllocEvent)
    t_nmf_fifo_desc *fifoDescShadow; //shadow desc, is used to restore state after migration and perform the update of the real desc

    // ExtendedField
    t_memory_handle extendedFieldHandle;
    t_shared_field  *extendedField;
} t_nmf_fifo_arm_desc;

PUBLIC t_uint32 fifo_isFifoIdValid(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC t_nmf_fifo_arm_desc* fifo_alloc(
        t_nmf_core_id pusherCoreId, t_nmf_core_id poperCoreId,
        t_uint16 size_in_16bit, t_uint16 nbElem, t_uint16 nbExtendedSharedFields,
        t_dsp_memory_type_id memType, t_dsp_memory_type_id memExtendedFieldType, t_cm_domain_id domainId);
PUBLIC void fifo_free(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC t_uint16 fifo_normalizeDepth(t_uint16 requestedDepth);

PUBLIC t_shared_addr fifo_getAndAckNextElemToWritePointer(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC t_shared_addr fifo_getAndAckNextElemToReadPointer(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC t_shared_addr fifo_getNextElemToWritePointer(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC t_shared_addr fifo_getNextElemToReadPointer(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC void fifo_acknowledgeRead(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC void fifo_acknowledgeWrite(t_nmf_fifo_arm_desc *pArmFifo);
PUBLIC void fifo_coms_acknowledgeWriteAndInterruptGeneration(t_nmf_fifo_arm_desc *pArmFifo);

PUBLIC t_cm_error fifo_params_setSharedField(t_nmf_fifo_arm_desc *pArmFifo, t_uint32 sharedFieldIndex, t_shared_field value);

#endif /* __INC_NMF_FIFO_ARM */
