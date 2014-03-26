/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Public Component Manager Memory User Engine API.
 *
 * This file contains the Component Manager Engine API for manipulating memory.
 *
 */

#ifndef CM_MEMORY_ENGINE_H_
#define CM_MEMORY_ENGINE_H_

#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/memory/inc/memory_type.h>

/*!
 * \brief Allocate memory in a Media Processor Core memory
 *
 * \param[in] domainId
 * \param[in] memType
 * \param[in] size
 * \param[in] memAlignment
 * \param[out] pHandle
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_AllocMpcMemory(
        t_cm_domain_id domainId,
        t_nmf_client_id clientId,                                   //!< [in] Client ID (aka PID)
        t_cm_mpc_memory_type memType,
        t_cm_size size,
        t_cm_mpc_memory_alignment memAlignment,
        t_cm_memory_handle *pHandle
        );


/*!
 * \brief Free a MPC memory block.
 *
 * \param[in] handle
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_FreeMpcMemory(t_cm_memory_handle handle);

/*!
 * \brief Get the start address of the MPC memory block seen by the host CPU (physical and logical)
 *
 * The logical system address returned by this method is valid only in kernel space and the physical
 * address is accessible only from kernel space too.
 *
 * \see OSMem "OS Memory management" for seeing an integration example.
 *
 * \param[in] handle
 * \param[out] pSystemAddress
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetMpcMemorySystemAddress(
        t_cm_memory_handle handle,
        t_cm_system_address *pSystemAddress);

/*!
 * \brief Get the start address of the memory block seen by the Media Processor Core
 *
 * \param[in] handle
 * \param[out] pMpcAddress
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetMpcMemoryMpcAddress(
        t_cm_memory_handle handle,
        t_uint32 *pMpcAddress);

/*!
 * \brief Get the memory status for given memory type of a given Media Processor Core
 *
 * \param[in] domainId
 * \param[in] memType
 * \param[out] pStatus
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetMpcMemoryStatus(
        t_cm_domain_id domainId,
        t_cm_mpc_memory_type memType,
        t_cm_allocator_status *pStatus);

#endif /* CM_MEMORY_ENGINE_H_ */

