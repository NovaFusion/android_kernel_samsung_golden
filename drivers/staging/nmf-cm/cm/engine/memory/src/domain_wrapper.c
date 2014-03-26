/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/api/domain_engine.h>
#include <cm/engine/api/migration_engine.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/memory/inc/migration.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_CreateMemoryDomain(
		const t_nmf_client_id     client,
        const t_cm_domain_memory  *domain,
        t_cm_domain_id            *handle
        )
{
    t_cm_error error;

    OSAL_LOCK_API();
    error = cm_DM_CreateDomain(client, domain, handle);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_CreateMemoryDomainScratch(
        const t_nmf_client_id     client,
        const t_cm_domain_id parentId,
        const t_cm_domain_memory  *domain,
        t_cm_domain_id            *handle
        )
{
    t_cm_error error;

    OSAL_LOCK_API();
    error = cm_DM_CreateDomainScratch(client, parentId, domain, handle);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_DestroyMemoryDomain(
        t_cm_domain_id handle)
{
    t_cm_error error;

    OSAL_LOCK_API();
    error = cm_DM_DestroyDomain(handle);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_FlushMemoryDomains(
        t_nmf_client_id client)
{
    t_cm_error error;

    OSAL_LOCK_API();
    error = cm_DM_DestroyDomains(client);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetDomainCoreId(const t_cm_domain_id domainId, t_nmf_core_id *coreId)
{
    t_cm_error error;
    OSAL_LOCK_API();
    //TODO, scratch
    error = cm_DM_CheckDomain(domainId, DOMAIN_NORMAL);
    if (error != CM_OK) {
        OSAL_UNLOCK_API();
        return error;
    }

    *coreId = cm_DM_GetDomainCoreId(domainId);
    OSAL_UNLOCK_API();
    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_Migrate(const t_cm_domain_id srcShared, const t_cm_domain_id src, const t_cm_domain_id dst)
{
    t_cm_error error;
    OSAL_LOCK_API();
    error = cm_migrate(srcShared, src, dst);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_Unmigrate(void)
{
    t_cm_error error;
    OSAL_LOCK_API();
    error = cm_unmigrate();
    OSAL_UNLOCK_API();
    return error;
}
