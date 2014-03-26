/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief NMF API for interrupt handler.
 *
 * This file contains the Component Manager API for interrupt handler.
 */
#ifndef CONTROL_IRQ_ENGINE_H
#define CONTROL_IRQ_ENGINE_H

#include <share/inc/nmf.h>
#include <cm/inc/cm_type.h>
#include <nmf/inc/service_type.h>
#include <ee/api/trace.idt>

/*!
 * \brief MPCs -> HOST communication handler
 *
 * This routine shall be integrated as interrupt handler into the OS
 *
 * If the given Media Processor Core has been configured (through CM_ConfigureMediaProcessorCore()) as using \ref LOCAL_SEMAPHORES, then
 * the NMF communication mechanism will use the embedded MMDSP macrocell semaphore,
 * so CM_ProcessMpcEvent(<\e coreId>) shall be called under ISR connected to local MMDSP IRQ0, with the related \e coreId as parameter.
 *
 * If the given Media Processor Core has been configured (through CM_ConfigureMediaProcessorCore()) as using \ref SYSTEM_SEMAPHORES, then
 * the NMF communication mechanism will use the shared system HW Semaphores,
 * so CM_ProcessMpcEvent(\ref ARM_CORE_ID) shall be called under ISR connected to shared HW Sem Host IRQ, with \ref ARM_CORE_ID as parameter.
 *
 * NB: A Media Processor Core belonging to the distribution pool shall be configured with \ref SYSTEM_SEMAPHORES
 *
 * \see t_nmf_semaphore_type_id description
 *
 * \param[in] coreId identification of the source of the interrupt
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC IMPORT_SHARED void CM_ProcessMpcEvent(t_nmf_core_id coreId);

/*!
 * \brief Service type
 *
 * \note We used an enumeration in structure since this description remain inside the kernel
 *  and we assume that everything in the kernel is compile with same compiler and option.
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef enum { // Allowed since i
        CM_MPC_SERVICE_NONE = 0,                                //!< No service found
        CM_MPC_SERVICE_PANIC = 1,                               //!< Panic service found
        CM_MPC_SERVICE_PRINT = 2,                               //!< Print service found
        CM_MPC_SERVICE_TRACE = 3                                //!< Trace service found
} t_cm_service_type;
                                          //!< Service description type
/*!
 * \brief Service description data
 *
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef struct {
    union {
       t_nmf_panic_data panic;                                                 //!< Panic description
       struct {
           t_uint32                     dspAddress;
           t_uint32                     value1;
           t_uint32                     value2;
       } print;                                                 //!< Printf like description
    } u;                                                    //!< Union of service description
} t_cm_service_description;

/*!
 * \brief MPC Panic handler
 *
 * This routine shall be called as interrupt handler into the OS.
 *
 * So CM_getPanicDescription shall be called under ISR connected to local MMDSP IRQ1, with the related \e coreId as parameter.
 *
 * \param[in] coreId                identification of the source of the interrupt
 * \param[out] srcType              Pointer on service type
 * \param[out] srcDescr             Pointer on service description
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_getServiceDescription(
        t_nmf_core_id               coreId,
        t_cm_service_type           *srcType,
	t_cm_service_description    *srcDescr);

/*!
 * \brief Read a null terminated string inside an MPC
 *
 * This routine could be used to read the MPC string give as parameter during an CM_NMF_SERVICE_PRINT
 *
 * \param[in] coreId                Identification of the code where read string
 * \param[in] dspAddress            Address of the string in the MPC
 * \param[out] buffer             Buffer pointer where returning null terminated string
 * \param[in] bufferSize            Buffer size
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ReadMPCString(
        t_nmf_core_id               coreId,
        t_uint32                    dspAddress,
        char *                      buffer,
        t_uint32                    bufferSize);

typedef enum {
        CM_MPC_TRACE_NONE = 0,
        CM_MPC_TRACE_READ = 1,
        CM_MPC_TRACE_READ_OVERRUN = 2
} t_cm_trace_type;

PUBLIC IMPORT_SHARED t_cm_trace_type CM_ENGINE_GetNextTrace(
        t_nmf_core_id               coreId,
        struct t_nmf_trace          *trace,
        int *readIdx,
        int *lastRev);

PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_resizeTraceBuffer(
	t_nmf_core_id coreId,
	t_uint32 oldSize);
#endif /* CONTROL_IRQ_ENGINE_H */
