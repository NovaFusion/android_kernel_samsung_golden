/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Public Component Manager Memory User SYSCALL API.
 *
 * This file contains the Component Manager SYSCALL API for manipulating domains.
 *
 */

#ifndef __INC_DOMAIN_ENGINE_H
#define __INC_DOMAIN_ENGINE_H

#include <cm/engine/memory/inc/domain_type.h>

/*!
 * \brief Create a domain.
 *
 * Create a memory domain for use in the CM for component instantiation and memory allocation.
 *
 * \param[in]  client Id of the client.
 * \param[in]  domain Description of domain memories.
 * \param[out] handle Idetifier of the created domain
 *
 * \exception CM_INVALID_DOMAIN_DEFINITION
 * \exception CM_INTERNAL_DOMAIN_OVERFLOW
 * \exception CM_OK
 *
 * \return Error code.
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_CreateMemoryDomain(
        const t_nmf_client_id     client,
        const t_cm_domain_memory  *domain,
        t_cm_domain_id            *handle
        );

/*!
 * \brief Create a scratch domain.
 *
 * Create a scratch memory domain. Scratch domains
 * are used to perform overlapping allocations.
 *
 * \param[in]  client Id of the client.
 * \param[in]  parentId Identifier of the parent domain.
 * \param[in]  domain Description of domain memories.
 * \param[out] handle Idetifier of the created domain
 *
 * \exception CM_INVALID_DOMAIN_DEFINITION
 * \exception CM_INTERNAL_DOMAIN_OVERFLOW
 * \exception CM_NO_MORE_MEMORY
 * \exception CM_OK
 *
 * \return Error code.
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_CreateMemoryDomainScratch(
        const t_nmf_client_id      client,
        const t_cm_domain_id       parentId,
        const t_cm_domain_memory  *domain,
        t_cm_domain_id            *handle
        );

/*!
 * \brief Destroy a memory domain.

 * \param[in] handle Domain identifier to destroy.
 *
 * \exception CM_INVALID_DOMAIN_HANDLE
 * \exception CM_OK
 *
 * \return Error code.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_DestroyMemoryDomain(
        t_cm_domain_id handle);

/*!
 * \brief Destroy all domains belonging to a given client.
 *
 * \param[in] client
 *
 * \return Error code.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_FlushMemoryDomains(
        t_nmf_client_id client);

/*!
 * \brief Retrieve the coreId for a given domain. Utility.

 * \param[in]  domainId   Domain identifier.
 * \param[out] coreId     Core identifier.
 *
 * \exception CM_INVALID_DOMAIN_HANDLE  Invalid domain handle
 * \exception CM_OK
 *
 * \return Error code.
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetDomainCoreId(const t_cm_domain_id domainId, t_nmf_core_id *coreId);

#endif /* __INC_DOMAIN_ENGINE_H */
