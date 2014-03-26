/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/api/component_engine.h>
#include <cm/engine/api/communication_engine.h>

#include <cm/engine/component/inc/bind.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/utils/inc/string.h>
#include <cm/engine/memory/inc/domain.h>

#include <cm/engine/configuration/inc/configuration.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>

/*
 * Component mangement wrapping.
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_InstantiateComponent(
        const char* templateName,
        t_cm_domain_id domainId,
        t_nmf_client_id clientId,
        t_nmf_ee_priority priority,
        const char localName[MAX_COMPONENT_NAME_LENGTH],
        const char *dataFile,
        t_cm_instance_handle *instance) {
    t_cm_error error;
    t_nmf_core_id coreId;
    t_component_instance *comp;
    t_elfdescription *elfhandle = NULL;

    OSAL_LOCK_API();

    /*
     * Load Elf File
     */
    if(dataFile != NULL &&
            (error = cm_ELF_CheckFile(
                    dataFile,
                    TRUE,
                    &elfhandle)) != CM_OK)
        goto out;

    //only allow instantiation in non-scratch domains (ie. DOMAIN_NORMAL)!
    if ((error = cm_DM_CheckDomainWithClient(domainId, DOMAIN_NORMAL, clientId)) != CM_OK)
        goto out;

    coreId = cm_DM_GetDomainCoreId(domainId);

    if(coreId < FIRST_MPC_ID || coreId > LAST_CORE_ID)
    {
        error = CM_INVALID_PARAMETER;
        goto out;
    }

    if ((error = cm_CFG_CheckMpcStatus(coreId)) != CM_OK)
        goto out;

    if ((error = cm_EEM_ForceWakeup(coreId)) != CM_OK)
        goto out;

    error = cm_instantiateComponent(
            templateName,
            domainId,
            priority,
            localName,
            elfhandle,
            &comp);
    if(error == CM_OK)
        *instance = comp->instance;

    cm_EEM_AllowSleep(coreId);

out:
    cm_ELF_CloseFile(TRUE, elfhandle);

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_StartComponent(
        t_cm_instance_handle instance,
        t_nmf_client_id clientId) {
    t_cm_error error;
    t_component_instance *component;

    OSAL_LOCK_API();

    component = cm_lookupComponent(instance);
    if (NULL == component)
        error = CM_INVALID_COMPONENT_HANDLE;
    else
    {
        error = cm_startComponent(component, clientId);
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_StopComponent(
        t_cm_instance_handle instance,
        t_nmf_client_id clientId) {
    t_cm_error error;
    t_component_instance *component;

    OSAL_LOCK_API();

    component = cm_lookupComponent(instance);
    if (NULL == component)
        error = CM_INVALID_COMPONENT_HANDLE;
    else
    {
        error = cm_stopComponent(component, clientId);
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_DestroyComponent(
        t_cm_instance_handle instance,
        t_nmf_client_id clientId)
{
    t_cm_error error;
    t_component_instance *component;

    OSAL_LOCK_API();

    component = cm_lookupComponent(instance);
    if (NULL == component)
    {
        error = CM_INVALID_COMPONENT_HANDLE;
    }
    else
    {
        t_nmf_core_id coreId = component->Template->dspId;

        (void)cm_EEM_ForceWakeup(coreId);

        error = cm_destroyInstanceForClient(component, DESTROY_NORMAL, clientId);

        cm_CFG_ReleaseMpc(coreId);

        cm_EEM_AllowSleep(coreId);
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_FlushComponents(t_nmf_client_id clientId)
{
    t_cm_error error = CM_OK;
    t_component_instance *instance;
    t_uint32 i;

    if (clientId == 0)
    	return CM_INVALID_PARAMETER;

    OSAL_LOCK_API();

    // We don't know exactly where components will be, wake up everybody !!
    (void)cm_EEM_ForceWakeup(SVA_CORE_ID);
    (void)cm_EEM_ForceWakeup(SIA_CORE_ID);

    /* Destroy all host2mpc bindings */
    OSAL_LOCK_COM();
    for (i=0; i<Host2MpcBindingTable.idxMax; i++)
    {
	    t_host2mpc_bf_info* bfInfo;
	    bfInfo = Host2MpcBindingTable.entries[i];
	    if ((bfInfo != NULL) && (bfInfo->clientId == clientId)) {
		    cm_delEntry(&Host2MpcBindingTable, i);
		    OSAL_UNLOCK_COM();
		    cm_unbindComponentFromCMCore(bfInfo);
                    OSAL_LOCK_COM();
	    }
    }
    OSAL_UNLOCK_COM();

    /* First, stop all remaining components for this client */
    for (i=0; i<ComponentTable.idxMax; i++)
    {
	if ((instance = componentEntry(i)) == NULL)
		continue;
        if (/* skip EE */
                (instance->Template->classe == FIRMWARE) ||
                /* Skip all binding components */
                (cm_StringCompare(instance->Template->name, "_ev.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_st.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_sk.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_tr.", 4) == 0))
            continue;

        /*
         * Special code for SINGLETON handling
         */
        if(instance->Template->classe == SINGLETON)
        {
            struct t_client_of_singleton* cl = cm_getClientOfSingleton(instance, FALSE, clientId);
            if(cl == NULL)
                continue;

            cl->numberOfStart = 1; // == 1 since it will go to 0 in cm_stopComponent
            cl->numberOfInstance = 1; // == 1 since it will go to 0 in cm_destroyInstanceForClient
            cl->numberOfBind = 0; // == 0 since we don't want anymore binding for this component
        }
        else if(domainDesc[instance->domainId].client != clientId)
            /* Skip all components not belonging to our client */
            continue;

        // Stop the component
        error = cm_stopComponent(instance, clientId);
        if (error != CM_OK && error != CM_COMPONENT_NOT_STARTED)
            LOG_INTERNAL(0, "Error stopping component %s/%x (%s, error=%d, client=%u)\n", instance->pathname, instance, instance->Template->name, error, clientId, 0);
    }

    /* Then, unbind all these components */
    for (i=0; i<ComponentTable.idxMax; i++)
    {
	if ((instance = componentEntry(i)) == NULL)
		continue;
        if (/* skip EE */
                (instance->Template->classe == FIRMWARE) ||
                /* Skip all binding components */
                (cm_StringCompare(instance->Template->name, "_ev.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_st.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_sk.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_tr.", 4) == 0))
            continue;

        /*
         * Special code for SINGLETON handling
         */
        if(instance->Template->classe == SINGLETON)
        {
            struct t_client_of_singleton* cl = cm_getClientOfSingleton(instance, FALSE, clientId);
            if(cl == NULL)
                continue;
        }
        else if(domainDesc[instance->domainId].client != clientId)
            /* Skip all components not belonging to our client */
            continue;

        // Destroy dependencies
        cm_destroyRequireInterface(instance, clientId);
    }

    /* Destroy all remaining components for this client */
    for (i=0; i<ComponentTable.idxMax; i++)
    {
	if ((instance = componentEntry(i)) == NULL)
		continue;
        if (/* skip EE */
                (instance->Template->classe == FIRMWARE) ||
                /* Skip all binding components */
                (cm_StringCompare(instance->Template->name, "_ev.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_st.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_sk.", 4) == 0) ||
                (cm_StringCompare(instance->Template->name, "_tr.", 4) == 0)) {
		continue;
        }


       /*
        * Special code for SINGLETON handling
        */
       if(instance->Template->classe == SINGLETON)
       {
           struct t_client_of_singleton* cl = cm_getClientOfSingleton(instance, FALSE, clientId);
           if(cl == NULL)
               continue;
       }
       else if(domainDesc[instance->domainId].client != clientId)
           /* Skip all components not belonging to our client */
           continue;


        // Destroy the component
        error = cm_destroyInstanceForClient(instance, DESTROY_WITHOUT_CHECK, clientId);

        if (error != CM_OK)
        {
            /* FIXME : add component name instance in log message but need to make a copy before cm_flushComponent()
             *         because it's no more available after.
             */
            LOG_INTERNAL(0, "Error flushing component (error=%d, client=%u)\n", error, clientId, 0, 0, 0, 0);
        }
    }

    cm_CFG_ReleaseMpc(SVA_CORE_ID);
    cm_CFG_ReleaseMpc(SIA_CORE_ID);

    cm_EEM_AllowSleep(SVA_CORE_ID);
    cm_EEM_AllowSleep(SIA_CORE_ID);

    OSAL_UNLOCK_API();

    return error;
}

/*
 * Component binding wrapping.
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_BindComponent(
        const t_cm_instance_handle clientInstance,
        const char* requiredItfClientName,
        const t_cm_instance_handle serverInstance,
        const char* providedItfServerName,
        t_bool traced,
        t_nmf_client_id clientId,
        const char *dataFileTrace) {
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_bool bindable;
    t_cm_error error;
    t_component_instance *client, *server;
    t_elfdescription *elfhandleTrace = NULL;

    OSAL_LOCK_API();

    /*
     * Load Elf File
     */
    if(dataFileTrace != NULL &&
            (error = cm_ELF_CheckFile(
                    dataFileTrace,
                    TRUE,
                    &elfhandleTrace)) != CM_OK)
        goto out;

    client = cm_lookupComponent(clientInstance);
    server = cm_lookupComponent(serverInstance);
    // Sanity check
    if((error = cm_checkValidBinding(client, requiredItfClientName,
				     server, providedItfServerName,
				     &itfRequire, &itfProvide, &bindable)) != CM_OK)
        goto out;

    // Check that client and server component run on same DSP
    if (itfRequire.client->Template->dspId != itfProvide.server->Template->dspId)
    {
        error = CM_ILLEGAL_BINDING;
        goto out;
    }

    // Check if we really need to bind
    if(bindable)
    {
        if ((error = cm_EEM_ForceWakeup(itfRequire.client->Template->dspId)) != CM_OK)
            goto out;

        /*
         * Synchronous binding, so no binding component
         */
        if(traced)
            error = cm_bindInterfaceTrace(&itfRequire, &itfProvide, elfhandleTrace);
        else
            error = cm_bindInterface(&itfRequire, &itfProvide);

        cm_EEM_AllowSleep(itfRequire.client->Template->dspId);
    }

    cm_registerSingletonBinding(client, &itfRequire, &itfProvide, clientId);

out:
    cm_ELF_CloseFile(TRUE, elfhandleTrace);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_UnbindComponent(
        const t_cm_instance_handle clientInstance,
        const char* requiredItfClientName,
        t_nmf_client_id clientId) {
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_bf_info_ID bfInfoID;
    t_cm_error error;
    t_component_instance *client;

    OSAL_LOCK_API();

    client = cm_lookupComponent(clientInstance);
    // Sanity check
    if((error = cm_checkValidUnbinding(client, requiredItfClientName,
				       &itfRequire, &itfProvide)) != CM_OK)
        goto out;

    // Check if this is a Primitive binding
    bfInfoID = itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfoID;
    if(bfInfoID != BF_SYNCHRONOUS && bfInfoID != BF_TRACE)
    {
        error = CM_ILLEGAL_UNBINDING;
        goto out;
    }

    // Check if we really need to unbind
    if(cm_unregisterSingletonBinding(client, &itfRequire, &itfProvide, clientId))
    {
        (void)cm_EEM_ForceWakeup(itfRequire.client->Template->dspId);

        if(bfInfoID == BF_SYNCHRONOUS)
            cm_unbindInterface(&itfRequire);
        else
            cm_unbindInterfaceTrace(
                    &itfRequire,
                    (t_trace_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);

        cm_EEM_AllowSleep(itfRequire.client->Template->dspId);

        error = CM_OK;
    }

out:
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_BindComponentToVoid(
    const t_cm_instance_handle clientInstance,
    const char                 requiredItfClientName[MAX_INTERFACE_NAME_LENGTH],
    t_nmf_client_id clientId)
{
    t_interface_require_description itfRequire;
    t_bool bindable;
    t_cm_error error;
    t_component_instance *client;

    OSAL_LOCK_API();

    client = cm_lookupComponent(clientInstance);
    // Check invalid binding
    if((error = cm_checkValidClient(client, requiredItfClientName,
                    &itfRequire, &bindable)) != CM_OK)
        goto out;

   // Check if we really need to bind
    if(bindable)
    {
        if ((error = cm_EEM_ForceWakeup(itfRequire.client->Template->dspId)) != CM_OK)
            goto out;

        error = cm_bindInterfaceToVoid(&itfRequire);

        cm_EEM_AllowSleep(itfRequire.client->Template->dspId);
    }

    cm_registerSingletonBinding(client, &itfRequire, NULL, clientId);

out:
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_BindComponentAsynchronous(
        const t_cm_instance_handle clientInstance,
        const char* requiredItfClientName,
        const t_cm_instance_handle serverInstance,
        const char* providedItfServerName,
        t_uint32 fifosize,
        t_cm_mpc_memory_type eventMemType,
        t_nmf_client_id clientId,
        const char *dataFileSkeletonOrEvent,
        const char *dataFileStub) {
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_dsp_memory_type_id dspEventMemType;
    t_bool bindable;
    t_cm_error error;
    t_component_instance *client, *server;
    t_elfdescription *elfhandleSkeletonOrEvent = NULL;
    t_elfdescription *elfhandleStub = NULL;

    OSAL_LOCK_API();

    /*
     * Load Elf File
     */
    if(dataFileSkeletonOrEvent != NULL &&
            (error = cm_ELF_CheckFile(
                    dataFileSkeletonOrEvent,
                    TRUE,
                    &elfhandleSkeletonOrEvent)) != CM_OK)
        goto out;
    if(dataFileStub != NULL &&
            (error = cm_ELF_CheckFile(
                    dataFileStub,
                    TRUE,
                    &elfhandleStub)) != CM_OK)
        goto out;

    client = cm_lookupComponent(clientInstance);
    server = cm_lookupComponent(serverInstance);
    // Check invalid binding
    if((error = cm_checkValidBinding(client, requiredItfClientName,
				     server, providedItfServerName,
				     &itfRequire, &itfProvide, &bindable)) != CM_OK)
        goto out;

    switch(eventMemType)
    {
    case CM_MM_MPC_TCM24_X:
        dspEventMemType = INTERNAL_XRAM24;
        break;
    case CM_MM_MPC_ESRAM24:
        dspEventMemType = ESRAM_EXT24;
        break;
    case CM_MM_MPC_SDRAM24:
        dspEventMemType = SDRAM_EXT24;
        break;
    default:
        error = CM_INVALID_PARAMETER;
        goto out;
    }

    // Check if we really need to bind
    if(bindable)
    {
        // Create the binding and bind it to the client (or all sub-components clients ....)
        if (itfRequire.client->Template->dspId != itfProvide.server->Template->dspId)
        {
            if ((error = cm_EEM_ForceWakeup(itfRequire.client->Template->dspId)) != CM_OK)
                goto out;
            if ((error = cm_EEM_ForceWakeup(itfProvide.server->Template->dspId)) != CM_OK)
            {
                cm_EEM_AllowSleep(itfRequire.client->Template->dspId);
                goto out;
            }

            // This is a distribute communication
            error = cm_bindInterfaceDistributed(
                    &itfRequire,
                    &itfProvide,
                    fifosize,
                    dspEventMemType,
                    elfhandleSkeletonOrEvent,
                    elfhandleStub);

            cm_EEM_AllowSleep(itfRequire.client->Template->dspId);
            cm_EEM_AllowSleep(itfProvide.server->Template->dspId);
        }
        else
        {
            if ((error = cm_EEM_ForceWakeup(itfRequire.client->Template->dspId)) != CM_OK)
                goto out;

            // This is a acynchronous communication
            error = cm_bindInterfaceAsynchronous(
                    &itfRequire,
                    &itfProvide,
                    fifosize,
                    dspEventMemType,
                    elfhandleSkeletonOrEvent);

            cm_EEM_AllowSleep(itfRequire.client->Template->dspId);
        }
    }

    cm_registerSingletonBinding(client, &itfRequire, &itfProvide, clientId);

out:
    cm_ELF_CloseFile(TRUE, elfhandleSkeletonOrEvent);
    cm_ELF_CloseFile(TRUE, elfhandleStub);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_UnbindComponentAsynchronous(
        const t_cm_instance_handle instance,
        const char* requiredItfClientName,
        t_nmf_client_id clientId) {
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_bf_info_ID bfInfoID;
    t_cm_error error;
    t_component_instance *client;

    OSAL_LOCK_API();

    client = cm_lookupComponent(instance);
    // Sanity check
    if((error = cm_checkValidUnbinding(client, requiredItfClientName,
				       &itfRequire, &itfProvide)) != CM_OK)
        goto out;

    bfInfoID = itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfoID;

    // Check if we really need to unbind
    if(cm_unregisterSingletonBinding(client, &itfRequire, &itfProvide, clientId))
    {
        // Check if this is a Asynchronous binding
        if(bfInfoID == BF_DSP2DSP)
        {
            t_nmf_core_id clientDsp = itfRequire.client->Template->dspId;
            t_nmf_core_id serverDsp = itfProvide.server->Template->dspId;

            (void)cm_EEM_ForceWakeup(clientDsp);
            (void)cm_EEM_ForceWakeup(serverDsp);

            cm_unbindInterfaceDistributed(
                    &itfRequire,
                    (t_mpc2mpc_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);

            cm_EEM_AllowSleep(clientDsp);
            cm_EEM_AllowSleep(serverDsp);

            error = CM_OK;
        }
        else if(bfInfoID == BF_ASYNCHRONOUS)
        {
            t_nmf_core_id clientDsp = itfRequire.client->Template->dspId;

            (void)cm_EEM_ForceWakeup(clientDsp);

            cm_unbindInterfaceAsynchronous(
                    &itfRequire,
                    (t_async_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);

            cm_EEM_AllowSleep(clientDsp);

            error = CM_OK;
        }
        else
            error = CM_ILLEGAL_UNBINDING;
    }

 out:
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_BindComponentFromCMCore(
        const t_cm_instance_handle server,
        const char* providedItfServerName,
        t_uint32 fifosize,
        t_cm_mpc_memory_type eventMemType,
        t_cm_bf_host2mpc_handle *bfHost2mpcHdl,
        t_nmf_client_id clientId,
        const char *dataFileSkeleton) {
    t_interface_provide_description itfProvide;
    t_dsp_memory_type_id dspEventMemType;
    t_cm_error error;
    t_component_instance* component;
    t_host2mpc_bf_info *bfInfo;
    t_elfdescription *elfhandleSkeleton = NULL;

    OSAL_LOCK_API();

    /*
     * Load Elf File
     */
    if(dataFileSkeleton != NULL &&
            (error = cm_ELF_CheckFile(
                    dataFileSkeleton,
                    TRUE,
                    &elfhandleSkeleton)) != CM_OK)
        goto out;

    component = cm_lookupComponent(server);
    // Check server validity
    if((error = cm_checkValidServer(component, providedItfServerName,
                    &itfProvide)) != CM_OK)
        goto out;

    if ((error = cm_EEM_ForceWakeup(itfProvide.server->Template->dspId)) != CM_OK)
        goto out;

    switch(eventMemType)
    {
    case CM_MM_MPC_TCM24_X:
        dspEventMemType = INTERNAL_XRAM24;
        break;
    case CM_MM_MPC_ESRAM24:
        dspEventMemType = ESRAM_EXT24;
        break;
    case CM_MM_MPC_SDRAM24:
        dspEventMemType = SDRAM_EXT24;
        break;
    default:
        error = CM_INVALID_PARAMETER;
        goto out;
    }

    error = cm_bindComponentFromCMCore(&itfProvide,
				       fifosize,
				       dspEventMemType,
				       elfhandleSkeleton,
				       &bfInfo);

    cm_EEM_AllowSleep(itfProvide.server->Template->dspId);

out:
    cm_ELF_CloseFile(TRUE, elfhandleSkeleton);
    OSAL_UNLOCK_API();

    if (error == CM_OK) {
	    bfInfo->clientId = clientId;
	    OSAL_LOCK_COM();
	    *bfHost2mpcHdl = cm_addEntry(&Host2MpcBindingTable, bfInfo);
	    if (*bfHost2mpcHdl == 0)
		    error = CM_NO_MORE_MEMORY;
	    OSAL_UNLOCK_COM();

	    if (error != CM_OK) {
		    OSAL_LOCK_API();
		    (void)cm_EEM_ForceWakeup(itfProvide.server->Template->dspId);
		    cm_unbindComponentFromCMCore(bfInfo);
		    cm_EEM_AllowSleep(itfProvide.server->Template->dspId);
		    OSAL_UNLOCK_API();
	    }
    }

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_UnbindComponentFromCMCore(
        t_cm_bf_host2mpc_handle bfHost2mpcId) {
	t_host2mpc_bf_info* bfInfo;
	t_nmf_core_id coreId;

    OSAL_LOCK_COM();
    bfInfo = cm_lookupEntry(&Host2MpcBindingTable, bfHost2mpcId);
    if (bfInfo)
	    cm_delEntry(&Host2MpcBindingTable, bfHost2mpcId & INDEX_MASK);
    OSAL_UNLOCK_COM();
    if (NULL == bfInfo)
	    return  CM_INVALID_PARAMETER;

    OSAL_LOCK_API();

    // Check if this is a DSP to Host binding
    //if(bfInfo->id != BF_HOST2DSP)
    //    return CM_ILLEGAL_UNBINDING;
    coreId = bfInfo->dspskeleton.skelInstance->Template->dspId;

    (void)cm_EEM_ForceWakeup(coreId);

    cm_unbindComponentFromCMCore(bfInfo);

    cm_EEM_AllowSleep(coreId);

    OSAL_UNLOCK_API();
    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_BindComponentToCMCore(
        const t_cm_instance_handle  instance,
        const char                  *requiredItfClientName,
        t_uint32                    fifosize,
        t_nmf_mpc2host_handle       upLayerThis,
        const char                  *dataFileStub,
        t_cm_bf_mpc2host_handle     *mpc2hostId,
        t_nmf_client_id             clientId) {
    t_interface_require_description itfRequire;
    t_bool bindable;
    t_cm_error error;
    t_component_instance* client;
    t_elfdescription *elfhandleStub = NULL;

    OSAL_LOCK_API();

    /*
     * Load Elf File
     */
    if(dataFileStub != NULL &&
            (error = cm_ELF_CheckFile(
                    dataFileStub,
                    TRUE,
                    &elfhandleStub)) != CM_OK)
        goto out;

    client = cm_lookupComponent(instance);
    // Check invalid binding
    if((error = cm_checkValidClient(client, requiredItfClientName,
                    &itfRequire, &bindable)) != CM_OK)
        goto out;

    // Check if we really need to bind
    if(bindable)
    {
        if ((error = cm_EEM_ForceWakeup(itfRequire.client->Template->dspId)) != CM_OK)
            goto out;

        error = cm_bindComponentToCMCore(
                &itfRequire,
                fifosize,
                upLayerThis,
                elfhandleStub,
                (t_mpc2host_bf_info**)mpc2hostId);

        cm_EEM_AllowSleep(itfRequire.client->Template->dspId);

        if (error == CM_OK)
		cm_registerSingletonBinding(client, &itfRequire, NULL, clientId);
    }
    else
    {
        /*
	 * bindable = FALSE means client is SINGLETON
	 * We don't allow multiple binding of singleton in this case.
	 */
        error = CM_INTERFACE_ALREADY_BINDED;
    }

out:
    cm_ELF_CloseFile(TRUE, elfhandleStub);
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_UnbindComponentToCMCore(
        const t_cm_instance_handle      instance,
        const char                      *requiredItfClientName,
        t_nmf_mpc2host_handle           *upLayerThis,
        t_nmf_client_id clientId) {
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_cm_error error;
    t_mpc2host_bf_info *bfInfo;
    t_component_instance* client;

    OSAL_LOCK_API();

    client = cm_lookupComponent(instance);
    // Sanity check
    if((error = cm_checkValidUnbinding(client, requiredItfClientName,
				       &itfRequire, &itfProvide)) != CM_OK)
        goto out;

    if (itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].instance == NULL)
    {
        error = CM_INTERFACE_NOT_BINDED;
        goto out;
    }

    // Check if this is a DSP to Host binding
    if(itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfoID != BF_DSP2HOST)
    {
        error = CM_ILLEGAL_UNBINDING;
        goto out;
    }

    bfInfo = (t_mpc2host_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo;

    // Get client information
    *upLayerThis = bfInfo->context;

    // Check if we really need to unbind
    if(cm_unregisterSingletonBinding(client, &itfRequire, &itfProvide, clientId))
    {
        (void)cm_EEM_ForceWakeup(itfRequire.client->Template->dspId);

        cm_unbindComponentToCMCore(&itfRequire, bfInfo);
        cm_EEM_AllowSleep(itfRequire.client->Template->dspId);

        error = CM_OK;
    }
out:
    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_event_params_handle CM_ENGINE_AllocEvent(t_cm_bf_host2mpc_handle host2mpcId) {
    t_host2mpc_bf_info* bfInfo;
    t_event_params_handle eventHandle;

    OSAL_LOCK_COM();
    bfInfo = cm_lookupEntry(&Host2MpcBindingTable, host2mpcId);
    if (NULL == bfInfo) {
	    OSAL_UNLOCK_COM();
	    return NULL;
    }

    if(bfInfo->dspskeleton.skelInstance->interfaceReferences[0][0].instance->state != STATE_RUNNABLE) {
        ERROR("CM_COMPONENT_NOT_STARTED: Call interface before start component %s<%s>\n",
                bfInfo->dspskeleton.skelInstance->pathname,
                bfInfo->dspskeleton.skelInstance->Template->name, 0, 0, 0, 0);
    }

    eventHandle = cm_AllocEvent(bfInfo->fifo);

    OSAL_UNLOCK_COM();

    return eventHandle;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_PushEvent(t_cm_bf_host2mpc_handle host2mpcId, t_event_params_handle h, t_uint32 methodIndex) {
	t_host2mpc_bf_info* bfInfo;
    t_cm_error error;

    OSAL_LOCK_COM();
    bfInfo = cm_lookupEntry(&Host2MpcBindingTable, host2mpcId);
    if (NULL == bfInfo) {
	    OSAL_UNLOCK_COM();
	    return CM_INVALID_PARAMETER;
    }
    error = cm_PushEvent(bfInfo->fifo, h, methodIndex);
    OSAL_UNLOCK_COM();

    return error;
}

PUBLIC EXPORT_SHARED void CM_ENGINE_AcknowledgeEvent(t_cm_bf_mpc2host_handle mpc2hostId) {
    t_mpc2host_bf_info* bfInfo = (t_mpc2host_bf_info*)mpc2hostId;

    //t_dsp2host_bf_info* bfInfo = (t_host2mpc_bf_info*)mpc2hostId;
    OSAL_LOCK_COM();
    cm_AcknowledgeEvent(bfInfo->fifo);
    OSAL_UNLOCK_COM();
}

/*
 * Get a reference on a given attribute of a given component
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_ReadComponentAttribute(
        const t_cm_instance_handle instance,
        const char* attrName,
        t_uint24 *attrValue)
{
    t_cm_error error;
    t_component_instance* component;

    OSAL_LOCK_API();

    component = cm_lookupComponent(instance);
    if (NULL == component)
        error = CM_INVALID_COMPONENT_HANDLE;
    else
    {
        if ((error = cm_EEM_ForceWakeup(component->Template->dspId)) != CM_OK)
            goto out;

        // t_uint24 -> t_uint32 possible since we know it same size
        error = cm_readAttribute(component, attrName, (t_uint32*)attrValue);

        cm_EEM_AllowSleep(component->Template->dspId);
    }

out:
    OSAL_UNLOCK_API();
    return error;
}

/*
 * Get a reference on a given attribute of a given component
 */

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_WriteComponentAttribute(
        const t_cm_instance_handle instance,
        const char* attrName,
        t_uint24 attrValue)
{
    t_cm_error error;
    t_component_instance* component;

    OSAL_LOCK_API();

    component = cm_lookupComponent(instance);
    if (NULL == component)
        error = CM_INVALID_COMPONENT_HANDLE;
    else
    {
        if ((error = cm_EEM_ForceWakeup(component->Template->dspId)) != CM_OK)
            goto out;

        //t_uint24 -> t_uint32 possible since we know it same size
        error = cm_writeAttribute(component, attrName, attrValue);

        cm_EEM_AllowSleep(component->Template->dspId);
    }

out:
    OSAL_UNLOCK_API();
    return error;
}

/*===============================================================================
 * Introspection API
 *===============================================================================*/
/*
 * Component
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentListHeader(
    const t_nmf_client_id       client,
    t_cm_instance_handle        *headerComponent) {
    t_uint32 i;

    OSAL_LOCK_API();

    *headerComponent = 0;
    for (i=0; i < ComponentTable.idxMax; i++) {
	    if ((componentEntry(i) != NULL) &&
		(componentEntry(i)->Template->classe != FIRMWARE) &&
		(domainDesc[componentEntry(i)->domainId].client == client)) {
		    *headerComponent = ENTRY2HANDLE(componentEntry(i), i);;
		    break;  
	    }
    }

    OSAL_UNLOCK_API();

    return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentListNext(
    const t_nmf_client_id       client,
    const t_cm_instance_handle  prevComponent,
    t_cm_instance_handle        *nextComponent){
    t_cm_error error;
    t_uint32 i = prevComponent & INDEX_MASK;

    OSAL_LOCK_API();

    // Sanity check
    if ((i >= ComponentTable.idxMax)
	|| (((unsigned int)componentEntry(i) << INDEX_SHIFT) != (prevComponent & ~INDEX_MASK)))
        error =  CM_INVALID_COMPONENT_HANDLE;
    else {
	*nextComponent = 0;
	for (i++; i < ComponentTable.idxMax; i++) {
	    if ((componentEntry(i) != NULL) &&
		(componentEntry(i)->Template->classe != FIRMWARE) &&
		(domainDesc[componentEntry(i)->domainId].client == client)) {
		    *nextComponent = ENTRY2HANDLE(componentEntry(i), i);;
		    break;  
	    }
	}

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentDescription(
        const t_cm_instance_handle  instance,
        char                        *templateName,
        t_uint32                    templateNameLength,
        t_nmf_core_id               *coreId,
        char                        *localName,
        t_uint32                    localNameLength,
	t_nmf_ee_priority           *priority) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else {
        cm_StringCopy(
                templateName,
                comp->Template->name,
                templateNameLength);
        *coreId = comp->Template->dspId;
        cm_StringCopy(
                localName,
                comp->pathname,
                localNameLength);
	if (priority)
		*priority = comp->priority;
        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

/*
 * Require interface
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentRequiredInterfaceNumber(
    const t_cm_instance_handle  instance,
    t_uint8                     *numberRequiredInterfaces) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else {
        *numberRequiredInterfaces = comp->Template->requireNumber;

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentRequiredInterface(
        const t_cm_instance_handle  instance,
        const t_uint8               index,
        char                        *itfName,
        t_uint32                    itfNameLength,
        char                        *itfType,
        t_uint32                    itfTypeLength,
        t_cm_require_state          *requireState,
        t_sint16                    *collectionSize) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else if(index >= comp->Template->requireNumber) {
        error = CM_NO_SUCH_REQUIRED_INTERFACE;
    } else {
        cm_StringCopy(
                itfName,
                comp->Template->requires[index].name,
                itfNameLength);
        cm_StringCopy(
                itfType,
                comp->Template->requires[index].interface->type,
                itfTypeLength);
        if(comp->Template->requires[index].requireTypes & COLLECTION_REQUIRE)
            *collectionSize = comp->Template->requires[index].collectionSize;
        else
            *collectionSize = -1;

	if(requireState != NULL) {
		*requireState = 0;
		if(comp->Template->requires[index].requireTypes & COLLECTION_REQUIRE)
			*requireState |= CM_REQUIRE_COLLECTION;
		if(comp->Template->requires[index].requireTypes & OPTIONAL_REQUIRE)
                	*requireState |= CM_REQUIRE_OPTIONAL;
		if(comp->Template->requires[index].requireTypes & STATIC_REQUIRE)
                	*requireState |= CM_REQUIRE_STATIC;
	}

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentRequiredInterfaceBinding(
        const t_cm_instance_handle  instance,
        const char                  *itfName,
        t_cm_instance_handle        *server,
        char                        *serverItfName,
        t_uint32                    serverItfNameLength) {
    t_component_instance *comp;
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if(NULL == comp) {
         error = CM_INVALID_COMPONENT_HANDLE;
    } else if ((error = cm_getRequiredInterface(comp, itfName, &itfRequire)) != CM_OK) {
        // Check if the requiredItfClientName is required by client component
    } else if ((error = cm_lookupInterface(&itfRequire, &itfProvide)) != CM_OK) {
        // Check if the requiredItfClientName is required by client component
    } else {
        if ((t_cm_instance_handle)itfProvide.server == NMF_HOST_COMPONENT
               || (t_cm_instance_handle)itfProvide.server == NMF_VOID_COMPONENT)
            *server = (t_cm_instance_handle)itfProvide.server;
        else
            *server = itfProvide.server->instance;
        if(*server == NMF_HOST_COMPONENT) {
            cm_StringCopy(
                    serverItfName,
                    "unknown",
                    serverItfNameLength);
        } else if(*server == NMF_VOID_COMPONENT) {
            cm_StringCopy(
                    serverItfName,
                    "void",
                    serverItfNameLength);
        } else if(*server != 0) {
            cm_StringCopy(
                    serverItfName,
                    itfProvide.server->Template->provides[itfProvide.provideIndex].name,
                    serverItfNameLength);
            if(itfProvide.server->Template->provides[itfProvide.provideIndex].provideTypes & COLLECTION_PROVIDE) {
                int len = cm_StringLength(serverItfName, serverItfNameLength);
                serverItfName[len++] = '[';
                if(itfProvide.collectionIndex >= 100)
                    serverItfName[len++] = '0' + (itfProvide.collectionIndex / 100);
                if(itfProvide.collectionIndex >= 10)
                    serverItfName[len++] = '0' + ((itfProvide.collectionIndex % 100) / 10);
                serverItfName[len++] = '0' + (itfProvide.collectionIndex % 10);
                serverItfName[len++] = ']';
                serverItfName[len] = 0;
            }
        }

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

/*
 * Provide interface
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentProvidedInterfaceNumber(
    const t_cm_instance_handle  instance,
    t_uint8                     *numberProvidedInterfaces) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else {
        *numberProvidedInterfaces = comp->Template->provideNumber;

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentProvidedInterface(
        const t_cm_instance_handle  instance,
        const t_uint8               index,
        char                        *itfName,
        t_uint32                    itfNameLength,
        char                        *itfType,
        t_uint32                    itfTypeLength,
        t_sint16                    *collectionSize) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else if(index >= comp->Template->provideNumber) {
        error = CM_NO_SUCH_PROVIDED_INTERFACE;
    } else {
        cm_StringCopy(
                itfName,
                comp->Template->provides[index].name,
                itfNameLength);
        cm_StringCopy(
                itfType,
                comp->Template->provides[index].interface->type,
                itfTypeLength);
        if(comp->Template->provides[index].provideTypes & COLLECTION_PROVIDE)
            *collectionSize = comp->Template->provides[index].collectionSize;
        else
            *collectionSize = -1;

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

/*
 * Component Property
 */
PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentPropertyNumber(
    const t_cm_instance_handle  instance,
    t_uint8                     *numberProperties) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else {
        *numberProperties = comp->Template->propertyNumber;

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentPropertyName(
        const t_cm_instance_handle  instance,
        const t_uint8               index,
        char                        *propertyName,
        t_uint32                    propertyNameLength) {
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    // Sanity check
    if (NULL == comp) {
        error = CM_INVALID_COMPONENT_HANDLE;
    } else if(index >= comp->Template->propertyNumber) {
        error = CM_NO_SUCH_PROPERTY;
    } else {
        cm_StringCopy(
                propertyName,
                comp->Template->properties[index].name,
                propertyNameLength);

        error = CM_OK;
    }

    OSAL_UNLOCK_API();

    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetComponentPropertyValue(
        const t_cm_instance_handle  instance,
        const char                  *propertyName,
        char                        *propertyValue,
        t_uint32                    propertyValueLength)
{
    t_component_instance *comp;
    t_cm_error error;

    OSAL_LOCK_API();

    comp = cm_lookupComponent(instance);
    if (NULL == comp)
        error = CM_INVALID_COMPONENT_HANDLE;
    else
    {
        error = cm_getComponentProperty(
            comp,
            propertyName,
            propertyValue,
            propertyValueLength);

        if(error == CM_NO_SUCH_PROPERTY)
            ERROR("CM_NO_SUCH_PROPERTY(%s, %s)\n", comp->pathname, propertyName, 0, 0, 0, 0);
    }

    OSAL_UNLOCK_API();

    return error;
}
