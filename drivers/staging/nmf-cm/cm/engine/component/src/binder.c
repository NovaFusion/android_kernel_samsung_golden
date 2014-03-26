/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include "../inc/bind.h"
#include "../inc/dspevent.h"
#include <cm/engine/communication/fifo/inc/nmf_fifo_arm.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/component/inc/introspection.h>

#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/trace/inc/xtitrace.h>

#include <cm/engine/utils/inc/string.h>

#define CM_IT_NAME_MAX_LENGTH               8

t_nmf_table Host2MpcBindingTable; /**< list (table) of host2mpc bindings */

static void cm_fillItName(int interruptLine, char *itName);
static t_uint16 getNumberOfBind(t_component_instance* component);

/*
 * Bind virtual interface, here we assume that:
 * - client component require this interface as last one and without collection,
 * - server component provide only this interface and without collection.
 * Fixed in loader.c.
 */
static void cm_bindVirtualInterface(
        t_component_instance* client,
        const t_component_instance* server) {
    t_interface_require_description itfRequire;

    if(cm_getRequiredInterface(client, "coms", &itfRequire) == CM_OK)
    {
        t_interface_reference* itfRef = client->interfaceReferences[itfRequire.requireIndex];

        /*
         * Memorise this reference
         */
        itfRef->provideIndex = 0;
        itfRef->collectionIndex = 0;
        itfRef->instance = server;
        itfRef->bfInfoID = (t_bf_info_ID)0;
        itfRef->bfInfo = (void*)-1; // TODO
    }
    else
    {
        ERROR("Internal Error in cm_bindVirtualInterface\n", 0, 0, 0, 0, 0, 0);
    }
}

static void cm_unbindVirtualInterface(
        t_component_instance* client) {
    t_interface_require_description itfRequire;

    if(cm_getRequiredInterface(client, "coms", &itfRequire) == CM_OK)
    {
        t_interface_reference* itfRef = client->interfaceReferences[itfRequire.requireIndex];
        itfRef->instance = NULL;
    }
    else
    {
        ERROR("Internal Error in cm_unbindVirtualInterface\n", 0, 0, 0, 0, 0, 0);
    }
}

/*
 * Bind component
 */
static void cm_bindLowLevelInterface(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfLocalBC,	         /* On the same DSP */
        t_bf_info_ID bfInfoID, void* bfInfo)
{
    const t_component_instance* client = itfRequire->client;
    t_component_instance* server = (t_component_instance*)itfLocalBC->server;
    t_interface_require *require = &client->Template->requires[itfRequire->requireIndex];
    t_interface_provide* provide = &server->Template->provides[itfLocalBC->provideIndex];
    t_interface_provide_loaded* provideLoaded = &server->Template->providesLoaded[itfLocalBC->provideIndex];
    int k, j;

    if(require->indexes != NULL)
    {
        t_interface_require_index *requireindex = &require->indexes[itfRequire->collectionIndex];

        for(k = 0; k < requireindex->numberOfClient; k++) {
            t_uint32 *hostAddr;

            hostAddr = (t_uint32*)(
                    cm_DSP_GetHostLogicalAddress(client->memories[requireindex->memories[k].memory->id]) +
                    requireindex->memories[k].offset * requireindex->memories[k].memory->memEntSize);

            LOG_INTERNAL(2, "Fill ItfRef %s.%s mem=%s Off=%x @=%x\n",
                    client->pathname, require->name,
                    requireindex->memories[k].memory->memoryName,
                    requireindex->memories[k].offset,
                    hostAddr, 0);

            /*
             * Fill the interface references. We start by This then methods in order to keep
             * Unbinded panic as long as possible and not used method with wrong This. This is
             * relevent only for optional since we must go in stop state before rebinding other
             * required interface.
             *
             * Direct write to DSP memory without go through DSP abstraction since we know we are in 24bits
             */
            // Write THIS reference into the Data field of the interface reference
            // Write the interface methods reference

            if(((t_uint32)hostAddr & 0x7) == 0 && require->interface->methodNumber > 0)
            {
                // We are 64word byte aligned, combine this write with first method
                *(volatile t_uint64*)hostAddr =
                        ((t_uint64)server->thisAddress << 0) |
                        ((t_uint64)provideLoaded->indexesLoaded[itfLocalBC->collectionIndex][0].methodAddresses << 32);
                hostAddr += 2;
                j = 1;
            }
            else
            {
                // We are not, write this which will align us
                *hostAddr++ = (t_uint32)server->thisAddress;
                j = 0;
            }

            // Word align copy
            for(; j < require->interface->methodNumber - 1; j+=2) {
                *(volatile t_uint64*)hostAddr =
                        ((t_uint64)provideLoaded->indexesLoaded[itfLocalBC->collectionIndex][j].methodAddresses << 0) |
                        ((t_uint64)provideLoaded->indexesLoaded[itfLocalBC->collectionIndex][j+1].methodAddresses << 32);
                hostAddr += 2;
            }

            // Last word align if required
            if(j < require->interface->methodNumber)
                *hostAddr = provideLoaded->indexesLoaded[itfLocalBC->collectionIndex][j].methodAddresses;
        }
    }
    else
    {
        t_function_relocation *reloc = client->Template->delayedRelocation;
        while(reloc != NULL) {
            for(j = 0; j < provide->interface->methodNumber; j++)
            {
                if(provide->interface->methodNames[j] == reloc->symbol_name) {
                    cm_ELF_performRelocation(
                                        reloc->type,
                                        reloc->symbol_name,
                                        provideLoaded->indexesLoaded[itfLocalBC->collectionIndex][j].methodAddresses,
                                        reloc->reloc_addr);
                    break;
                }
            }

            reloc = reloc -> next;
        }
    }

    /*
     * Memorise this reference
     */
    {
        t_interface_reference* itfRef = &client->interfaceReferences[itfRequire->requireIndex][itfRequire->collectionIndex];

        itfRef->provideIndex = itfLocalBC->provideIndex;
        itfRef->collectionIndex = itfLocalBC->collectionIndex;
        itfRef->instance = itfLocalBC->server;
        itfRef->bfInfoID = bfInfoID;
        itfRef->bfInfo = bfInfo;

        /*
         * Do not count binding from EE (ie interrupt line), as this will prevent
         * cm_destroyInstance() of server to succeed (interrupt line bindings are
         * destroyed after the check in cm_destroyInstance()
         */
        if (client->Template->classe != FIRMWARE)
            server->providedItfUsedCount++;
    }
}

static void cm_registerLowLevelInterfaceToConst(
    const t_interface_require_description *itfRequire,
    const t_component_instance* targetInstance)
{
    const t_component_instance* client = itfRequire->client;

    /*
     * Memorise this no reference
     */
    {
        t_interface_reference* itfRef = &client->interfaceReferences[itfRequire->requireIndex][itfRequire->collectionIndex];

        // This is an unbind from a true component (not to void)
        // Do not count bindings from EE (ie interrupt line)
        if ((targetInstance == NULL)
                && (client->Template->classe != FIRMWARE)
                && (itfRef->instance != (t_component_instance *)NMF_VOID_COMPONENT)
                && (itfRef->instance != NULL))
        {
            ((t_component_instance*)itfRef->instance)->providedItfUsedCount--;
        }

        itfRef->instance = targetInstance;
        itfRef->bfInfoID = BF_SYNCHRONOUS; // Just to memorize no Binding component used and unbind ToVoid happy ;-).
    }
}

static void cm_bindLowLevelInterfaceToConst(
        const t_interface_require_description *itfRequire,
        const t_dsp_address functionAddress,
        const t_component_instance* targetInstance) {
    const t_component_instance* client = itfRequire->client;
    t_interface_require *require = &client->Template->requires[itfRequire->requireIndex];
    int j, k;


    // If DSP is off/panic/... -> write nothing
    if(
            require->indexes != NULL
            && cm_DSP_GetState(client->Template->dspId)->state == MPC_STATE_BOOTED)
    {
        t_interface_require_index *requireindex = &require->indexes[itfRequire->collectionIndex];

        for(k = 0; k < requireindex->numberOfClient; k++) {
            t_uint32 *hostAddr;

            hostAddr = (t_uint32*)(
                    cm_DSP_GetHostLogicalAddress(client->memories[requireindex->memories[k].memory->id]) +
                    requireindex->memories[k].offset * requireindex->memories[k].memory->memEntSize);

            /*
             * Fill the interface references. We start by Methods then This in order to swith to
             * Unbinded panic as fast as possible and not used method with wrong This. This is
             * relevent only for optional since we must go in stop state before rebinding other
             * required interface.
             *
             * Direct write to DSP memory without go through DSP abstraction since we know we are in 24bits
             */
            /*
             * Write THIS reference into the Data field of the interface reference
             * Hack for simplifying debug just to keep THIS reference with caller one
             * (could be removed if __return_address MMDSP intrinsec provided by compiler).
             */
            // Write the interface methods reference

            if(((t_uint32)hostAddr & 0x7) == 0 && require->interface->methodNumber > 0)
            {
                // We are 64word byte aligned, combine this write with first method
                *(volatile t_uint64*)hostAddr =
                        ((t_uint64)client->thisAddress << 0) |
                        ((t_uint64)functionAddress << 32);
                hostAddr += 2;
                j = 1;
            }
            else
            {
                // We are not, write this which will align us
                *hostAddr++ = (t_uint32)client->thisAddress;
                j = 0;
            }

            // Word align copy
            for(; j < require->interface->methodNumber - 1; j+=2) {
                *(volatile t_uint64*)hostAddr =
                        ((t_uint64)functionAddress << 0) |
                        ((t_uint64)functionAddress << 32);
                hostAddr += 2;
            }

            // Last word align if required
            if(j < require->interface->methodNumber)
                *hostAddr = functionAddress;
        }
    }

    cm_registerLowLevelInterfaceToConst(itfRequire, targetInstance);
}

/*
 * Bind User component though primitive binding factory
 */
t_cm_error cm_bindInterface(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide) {

    LOG_INTERNAL(1, "\n##### Bind Synchronous %s/%x.%s -> %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName,
		 itfProvide->server->pathname, itfProvide->server, itfProvide->origName);

    cm_bindLowLevelInterface(
            itfRequire,
            itfProvide,
            BF_SYNCHRONOUS, NULL);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_SYNCHRONOUS,
            itfRequire->client, itfProvide->server,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            itfProvide->server->Template->provides[itfProvide->provideIndex].name);

  	return CM_OK;
}

/*
 *
 */
void cm_unbindInterface(
        const t_interface_require_description *itfRequire) {

    LOG_INTERNAL(1, "\n##### UnBind synchronous %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_UNBIND_SYNCHRONOUS,
            itfRequire->client, NULL,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

    cm_bindLowLevelInterfaceToConst(itfRequire,
            0x0,
            NULL);
}

/*
 *
 */
t_cm_error cm_bindInterfaceToVoid(
        const t_interface_require_description *itfRequire) {
    LOG_INTERNAL(1, "\n##### Bind %s/%x.%s -> Void #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    cm_bindLowLevelInterfaceToConst(itfRequire,
            cm_EEM_getExecutiveEngine(itfRequire->client->Template->dspId)->voidAddr,
            (t_component_instance*)NMF_VOID_COMPONENT);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_SYNCHRONOUS,
            itfRequire->client, NULL,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

	return CM_OK;
}
/*
 * Find the server and its interface inded to a given required interface for a given component
 */
t_cm_error cm_lookupInterface(
        const t_interface_require_description *itfRequire,
        t_interface_provide_description *itfProvide) {
    const t_component_instance* client = itfRequire->client;
    t_interface_reference* itfRef = &client->interfaceReferences[itfRequire->requireIndex][itfRequire->collectionIndex];

	if(itfRef->instance != NULL)
	{
	    itfProvide->server = itfRef->instance;
    	itfProvide->provideIndex = itfRef->provideIndex;
    	itfProvide->collectionIndex = itfRef->collectionIndex;

	    return CM_OK;
    } else {
        itfProvide->server = NULL;
        return CM_INTERFACE_NOT_BINDED;
    }
}

/*
 *
 */
t_cm_error cm_bindInterfaceTrace(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide,
        t_elfdescription *elfhandleTrace)
{
    t_interface_require *require = &itfRequire->client->Template->requires[itfRequire->requireIndex];
    t_interface_require_description bcitfRequire;
    t_interface_provide_description bcitfProvide;
    t_trace_bf_info *bfInfo;
    t_cm_error error;

    LOG_INTERNAL(1, "\n##### Bind Synchronous Trace %s/%x.%s -> %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName,
		 itfProvide->server->pathname, itfProvide->server, itfProvide->origName);

    /* Allocate aynchronous binding factory information */
    bfInfo = (t_trace_bf_info*)OSAL_Alloc(sizeof(t_trace_bf_info));
    if(bfInfo == 0)
        return CM_NO_MORE_MEMORY;

    /*
     * Instantiate related trace on dsp
     */
    {
        char traceTemplateName[4 + MAX_INTERFACE_TYPE_NAME_LENGTH + 1];

        cm_StringCopy(traceTemplateName,"_tr.", sizeof(traceTemplateName));
        cm_StringConcatenate(traceTemplateName, require->interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);

        if ((error = cm_instantiateComponent(
                traceTemplateName,
                itfRequire->client->domainId,
                itfProvide->server->priority,
                traceDup,
                elfhandleTrace,
                &bfInfo->traceInstance)) != CM_OK) {
            OSAL_Free(bfInfo);
            return (error == CM_COMPONENT_NOT_FOUND)?CM_BINDING_COMPONENT_NOT_FOUND : error;
        }
    }

    /* Bind event to server interface (Error must not occure) */
    CM_ASSERT(cm_getRequiredInterface(bfInfo->traceInstance, "target", &bcitfRequire) == CM_OK);

    cm_bindLowLevelInterface(&bcitfRequire, itfProvide, BF_SYNCHRONOUS, NULL);

    /* Get the event interface (Error must not occure) */
    CM_ASSERT(cm_getProvidedInterface(bfInfo->traceInstance, "target", &bcitfProvide) == CM_OK);

    /* Bind client to event (Error must not occure) */
    cm_bindLowLevelInterface(itfRequire, &bcitfProvide, BF_TRACE, bfInfo);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_SYNCHRONOUS,
            itfRequire->client, itfProvide->server,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            itfProvide->server->Template->provides[itfProvide->provideIndex].name);

    return CM_OK;
}

void cm_unbindInterfaceTrace(
        const t_interface_require_description *itfRequire,
        t_trace_bf_info                       *bfInfo)
{
    t_interface_require_description traceitfRequire;

    LOG_INTERNAL(1, "\n##### UnBind trace synchronous %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_UNBIND_SYNCHRONOUS,
            itfRequire->client, NULL,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

    /* Unbind Client from Event Binding Component */
    cm_bindLowLevelInterfaceToConst(itfRequire, 0x0, NULL);

    /* Unbind explicitly Event from Server Binding Component */
    /* This is mandatory to fix the providedItfUsedCount of the server */
    CM_ASSERT(cm_getRequiredInterface(bfInfo->traceInstance, "target", &traceitfRequire) == CM_OK);

    cm_registerLowLevelInterfaceToConst(&traceitfRequire, NULL);

    /* Destroy Event Binding Component */
    cm_destroyInstance(bfInfo->traceInstance, DESTROY_WITHOUT_CHECK);

    /* Free BF info */
    OSAL_Free(bfInfo);
}


/*
 *
 */
t_cm_error cm_bindInterfaceAsynchronous(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType,
        t_elfdescription *elfhandleEvent) {
    t_interface_require *require = &itfRequire->client->Template->requires[itfRequire->requireIndex];
    t_interface_require_description eventitfRequire;
    t_interface_provide_description eventitfProvide;
    t_async_bf_info *bfInfo;
    t_cm_error error;

    LOG_INTERNAL(1, "\n##### Bind Asynchronous %s/%x.%s -> %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName,
		 itfProvide->server->pathname, itfProvide->server, itfProvide->origName);

    /* Allocate aynchronous binding factory information */
    bfInfo = (t_async_bf_info*)OSAL_Alloc(sizeof(t_async_bf_info));
    if(bfInfo == 0)
        return CM_NO_MORE_MEMORY;

    /*
     * Instantiate related event on dsp
     */
    {
        char eventTemplateName[4 + MAX_INTERFACE_TYPE_NAME_LENGTH + 1];

        cm_StringCopy(eventTemplateName,"_ev.", sizeof(eventTemplateName));
        cm_StringConcatenate(eventTemplateName, require->interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);

        if ((error = cm_instantiateComponent(
                        eventTemplateName,
                        itfRequire->client->domainId,
                        itfProvide->server->priority,
                        eventDup,
                        elfhandleEvent,
                        &bfInfo->eventInstance)) != CM_OK) {
            OSAL_Free(bfInfo);
            return (error == CM_COMPONENT_NOT_FOUND)?CM_BINDING_COMPONENT_NOT_FOUND : error;
        }
    }

    /*
     * Initialize the event component
     */
    {
        unsigned int size;

        // Get fifo elem size (which was store in TOP by convention)
        size = cm_readAttributeNoError(bfInfo->eventInstance, "TOP");
        LOG_INTERNAL(3, "DspEvent Fifo element size = %d\n", size, 0, 0, 0, 0, 0);

        // Allocate fifo
        if ((error = dspevent_createDspEventFifo(bfInfo->eventInstance,
                        "TOP",
                        fifosize, size,
                        dspEventMemType,
                        &bfInfo->dspfifoHandle)) != CM_OK)
        {
            cm_destroyInstance(bfInfo->eventInstance, DESTROY_WITHOUT_CHECK);
            OSAL_Free(bfInfo);
            return error;
        }
    }

    /* Bind event to server interface (Error must not occure) */
    CM_ASSERT(cm_getRequiredInterface(bfInfo->eventInstance, "target", &eventitfRequire) == CM_OK);

    cm_bindLowLevelInterface(&eventitfRequire, itfProvide, BF_SYNCHRONOUS, NULL);

    /* Get the event interface (Error must not occure) */
    CM_ASSERT(cm_getProvidedInterface(bfInfo->eventInstance, "target", &eventitfProvide) == CM_OK);

    /* Bind client to event (Error must not occure) */
    cm_bindLowLevelInterface(itfRequire, &eventitfProvide, BF_ASYNCHRONOUS, bfInfo);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_ASYNCHRONOUS,
            itfRequire->client, itfProvide->server,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            itfProvide->server->Template->provides[itfProvide->provideIndex].name);

    return CM_OK;
}

void cm_unbindInterfaceAsynchronous(
        const t_interface_require_description   *itfRequire,
        t_async_bf_info                         *bfInfo)
{
    t_interface_require_description eventitfRequire;

    LOG_INTERNAL(1, "\n##### UnBind asynchronous %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_UNBIND_ASYNCHRONOUS,
            itfRequire->client, NULL,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

    /* Unbind Client from Event Binding Component */
    cm_bindLowLevelInterfaceToConst(itfRequire, 0x0, NULL);

    /* Unbind explicitly Event from Server Binding Component */
    /* This is mandatory to fix the providedItfUsedCount of the server */
    CM_ASSERT(cm_getRequiredInterface(bfInfo->eventInstance, "target", &eventitfRequire) == CM_OK);

    cm_registerLowLevelInterfaceToConst(&eventitfRequire, NULL);

    /* Destroy Event fifo */
    dspevent_destroyDspEventFifo(bfInfo->dspfifoHandle);

    /* Destroy Event Binding Component */
    cm_destroyInstance(bfInfo->eventInstance, DESTROY_WITHOUT_CHECK);

    /* Free BF info */
    OSAL_Free(bfInfo);
}

/*!
 * Create Shared FIFO and set stub and skeleton to it
 */
PRIVATE t_cm_error cm_createParamsFifo(t_component_instance *stub,
        t_component_instance *skeleton,
        t_cm_domain_id domainId,
        t_uint32 fifosize,
        t_nmf_fifo_arm_desc **fifo,
        t_uint32 *fifoElemSize,
        t_uint32 bcDescSize)
{
    t_nmf_core_id stubcore = (stub != NULL) ?(stub->Template->dspId): ARM_CORE_ID;
    t_nmf_core_id skelcore = (skeleton != NULL) ?(skeleton->Template->dspId) : ARM_CORE_ID;
    t_component_instance *bcnotnull = (stub != NULL) ? stub : skeleton;
    int _fifoelemsize;

    CM_ASSERT(bcnotnull != NULL);

    /* Get fifo param elem size (which was store in FIFO by convention) */
    _fifoelemsize = cm_readAttributeNoError(bcnotnull, "FIFO");
    LOG_INTERNAL(3, "Fifo Params element size = %d\n", _fifoelemsize, 0, 0, 0, 0, 0);
    if(fifoElemSize != NULL)
        *fifoElemSize = _fifoelemsize;

    /* Allocation of the fifo params */
    *fifo = fifo_alloc(stubcore, skelcore, _fifoelemsize, fifosize, 1+bcDescSize, paramsLocation, extendedFieldLocation, domainId); /* 1+nbMethods fro hostBCThis_or_TOP space */
    if(*fifo == NULL) {
        ERROR("CM_NO_MORE_MEMORY: fifo_alloc() failed in cm_createParamsFifo()\n", 0, 0, 0, 0, 0, 0);
        return CM_NO_MORE_MEMORY;
    }

    if(stub != NULL)
    {
        /* Set stub FIFO attribute (Error mut not occure) */
        cm_writeAttribute(stub, "FIFO", (*fifo)->dspAdress);

        LOG_INTERNAL(2, "  FIFO param %x:%x\n", *fifo, (*fifo)->dspAdress, 0, 0, 0, 0);
    }

    if(skeleton != NULL)
    {
        /* Set Skeleton FIFO attribute (Error mut not occure) */
        cm_writeAttribute(skeleton, "FIFO", (*fifo)->dspAdress);

        LOG_INTERNAL(2, "  FIFO param %x:%x\n", *fifo, (*fifo)->dspAdress, 0, 0, 0, 0);
    }

    return CM_OK;
}
/**
 *
 */
static void cm_destroyParamsFifo(t_nmf_fifo_arm_desc *fifo) {
    fifo_free(fifo);
}

/*!
 * Create DSP skeleton
 */
PRIVATE t_cm_error cm_createDSPSkeleton(
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType, //INTERNAL_XRAM24
        t_elfdescription *elfhandleSkeleton,
        t_dspskel_bf_info *bfInfo)
{
    t_interface_provide *provide = &itfProvide->server->Template->provides[itfProvide->provideIndex];
    t_interface_require_description skelitfRequire;
    t_cm_error error;
    unsigned int fifoeventsize = 0;

    /* Instantiate related stub on dsp */
    {
        char stubTemplateName[4 + MAX_INTERFACE_TYPE_NAME_LENGTH + 1];

        cm_StringCopy(stubTemplateName,"_sk.", sizeof(stubTemplateName));
        cm_StringConcatenate(stubTemplateName, provide->interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);

        if ((error = cm_instantiateComponent(
                        stubTemplateName,
                        itfProvide->server->domainId,
                        itfProvide->server->priority,
                        skeletonDup,
                        elfhandleSkeleton,
                        &bfInfo->skelInstance)) != CM_OK) {
            return ((error == CM_COMPONENT_NOT_FOUND)?CM_BINDING_COMPONENT_NOT_FOUND:error);
        }
    }

    /* Get fifo elem size (which was store in TOP by convention) */
    fifoeventsize = cm_readAttributeNoError(bfInfo->skelInstance, "TOP");
    LOG_INTERNAL(3, "DspEvent Fifo element size = %d\n", fifoeventsize, 0, 0, 0, 0, 0);

    /* Allocation of the itf event dsp fifo */
    if ((error = dspevent_createDspEventFifo(
                    bfInfo->skelInstance,
                    "TOP",
                    fifosize,
                    fifoeventsize,
                    dspEventMemType,
                    &bfInfo->dspfifoHandle)) != CM_OK)
    {
	    cm_destroyInstance(bfInfo->skelInstance, DESTROY_WITHOUT_CHECK);
        return error;
    }

    /* Bind stub to server component (Error must not occure) */
    CM_ASSERT(cm_getRequiredInterface(bfInfo->skelInstance, "target", &skelitfRequire) == CM_OK);

    cm_bindLowLevelInterface(&skelitfRequire, itfProvide, BF_SYNCHRONOUS, NULL);

    return CM_OK;
}

/**
 * Destroy DSP Skeleton
 */
PRIVATE t_cm_error cm_destroyDSPSkeleton(t_dspskel_bf_info *bfInfo) {
    t_interface_require_description skelitfRequire;

    /* Unbind explicitly stub from server component (Error must not occure) */
    /* This is mandatory to fix the providedItfUsedCount of the server */
    CM_ASSERT(cm_getRequiredInterface(bfInfo->skelInstance, "target", &skelitfRequire) == CM_OK);

    cm_registerLowLevelInterfaceToConst(&skelitfRequire, NULL);

    /* Destroy Event fifo */
    dspevent_destroyDspEventFifo(bfInfo->dspfifoHandle);

    /* Destroy Event Binding Component */
    return cm_destroyInstance(bfInfo->skelInstance, DESTROY_WITHOUT_CHECK);
}

/*
 *
 */
t_cm_error cm_bindComponentFromCMCore(
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType,
        t_elfdescription *elfhandleSkeleton,
        t_host2mpc_bf_info **bfInfo) {
    t_interface_provide *provide = &itfProvide->server->Template->provides[itfProvide->provideIndex];
    t_dsp_offset shareVarOffset;
    t_cm_error error;

    LOG_INTERNAL(1, "\n##### Bind HOST -> %s/%x.%s #####\n",
		 itfProvide->server->pathname, itfProvide->server, itfProvide->origName, 0, 0, 0);

    /* Allocate host2dsp binding factory information */
    *bfInfo = (t_host2mpc_bf_info*)OSAL_Alloc(sizeof(t_host2mpc_bf_info));
    if((*bfInfo) == 0)
        return CM_NO_MORE_MEMORY;

    /* Create the Skeleton */
    if ((error = cm_createDSPSkeleton(itfProvide,
                    fifo_normalizeDepth(fifosize), /* We SHALL create DSP Skeleton before creating the Params Fifo, but we need in advance the real depth of this fifo */
                    dspEventMemType,
                    elfhandleSkeleton,
                    &(*bfInfo)->dspskeleton)) != CM_OK)
    {
        OSAL_Free((*bfInfo));
        return error;
    }

    /* Create the FIFO Params */
    if ((error = cm_createParamsFifo(NULL,
                    (*bfInfo)->dspskeleton.skelInstance,
                    itfProvide->server->domainId,
                    fifosize,
                    &(*bfInfo)->fifo,
                    NULL,
                    provide->interface->methodNumber)) != CM_OK)
    {
	cm_destroyDSPSkeleton(&(*bfInfo)->dspskeleton);
        OSAL_Free((*bfInfo));
        return error;
    }

    /* Set Target info in FIFO param to TOP */
    shareVarOffset = cm_getAttributeMpcAddress((*bfInfo)->dspskeleton.skelInstance, "TOP");

    /*
     * Set Target info in FIFO param to armThis
     * Should not return any error
     */
    fifo_params_setSharedField((*bfInfo)->fifo, 0, (t_shared_field)shareVarOffset /* ArmBCThis_or_TOP */);

    /* Initialise FIFO Param bcDesc with Skeleton methods */
    {
        int i;
        t_component_instance *skel = (*bfInfo)->dspskeleton.skelInstance;
        for (i=0; i < provide->interface->methodNumber; i++)
        {
            /* should not return error */
            fifo_params_setSharedField(
                    (*bfInfo)->fifo,
                    1+i,
                    skel->Template->providesLoaded[0].indexesLoaded[0][i].methodAddresses
                    );
        }
    }

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_ASYNCHRONOUS,
            ARM_TRACE_COMPONENT, itfProvide->server,
            NULL,
            itfProvide->server->Template->provides[itfProvide->provideIndex].name);

    return CM_OK;
}

void cm_unbindComponentFromCMCore(
        t_host2mpc_bf_info* bfInfo) {
    t_component_instance *skel = bfInfo->dspskeleton.skelInstance;
    t_interface_reference* itfProvide = &skel->interfaceReferences[0][0];
    t_interface_provide *provide = &itfProvide->instance->Template->provides[itfProvide->provideIndex];

    LOG_INTERNAL(1, "\n##### UnBind HOST -> %s/%x.%s #####\n",
		 itfProvide->instance->pathname, itfProvide->instance, provide->name, 0, 0, 0);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_UNBIND_ASYNCHRONOUS,
            ARM_TRACE_COMPONENT, itfProvide->instance,
            NULL,
            itfProvide->instance->Template->provides[itfProvide->provideIndex].name);

    // Destroy FIFO params
    cm_destroyParamsFifo(bfInfo->fifo);

    // Destory Skeleton
    cm_destroyDSPSkeleton(&bfInfo->dspskeleton);

    // Free BF info (which contains bcDecr(==dspfct) and arm This)
    OSAL_Free(bfInfo);
}

/**
 * Create DSP Stub
 */
PRIVATE t_cm_error cm_createDSPStub(
        const t_interface_require_description *itfRequire,
        const char* itfType,
        t_dspstub_bf_info* bfInfo,
        t_elfdescription *elfhandleStub,
        t_interface_provide_description *itfstubProvide) {
    t_cm_error error;

    /*
     * Instantiate related skel on dsp
     */
    {
        char skelTemplateName[4 + MAX_INTERFACE_TYPE_NAME_LENGTH + 1];

        cm_StringCopy(skelTemplateName, "_st.", sizeof(skelTemplateName));
        cm_StringConcatenate(skelTemplateName, itfType, MAX_INTERFACE_TYPE_NAME_LENGTH);

        if ((error = cm_instantiateComponent(
                        skelTemplateName,
                        itfRequire->client->domainId,
                        itfRequire->client->priority,
                        stubDup,
                        elfhandleStub,
                        &bfInfo->stubInstance)) != CM_OK) {
            return (error == CM_COMPONENT_NOT_FOUND)?CM_BINDING_COMPONENT_NOT_FOUND : error;
        }
    }

    /* Get the internal component that serve this interface (Error must not occure) */
    (void)cm_getProvidedInterface(bfInfo->stubInstance, "source", itfstubProvide);

    return CM_OK;
}

PRIVATE t_cm_error cm_destroyDSPStub(
        const t_interface_require_description *itfRequire,
        t_dspstub_bf_info* bfInfo) {

    /* Unbind Client from Event Binding Component */
    cm_bindLowLevelInterfaceToConst(itfRequire,
            0x0,
            NULL);

    /* Destroy Event Binding Component */
    return cm_destroyInstance(bfInfo->stubInstance, DESTROY_WITHOUT_CHECK);
}
/*
 *
 */
t_cm_error cm_bindComponentToCMCore(
        const t_interface_require_description   *itfRequire,
        t_uint32                                fifosize,
        t_uint32                                context,
        t_elfdescription                        *elfhandleStub,
        t_mpc2host_bf_info                      ** bfInfo) {
    t_interface_require *require = &itfRequire->client->Template->requires[itfRequire->requireIndex];
    t_interface_provide_description itfstubProvide;
    t_cm_error error;
    t_uint32 fifoelemsize;

    LOG_INTERNAL(1, "\n##### Bind %s/%x.%s -> HOST #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    /* Allocate dsp2host binding factory information */
    *bfInfo = (t_mpc2host_bf_info*)OSAL_Alloc(sizeof(t_mpc2host_bf_info));
    if(*bfInfo == 0)
        return CM_NO_MORE_MEMORY;
    (*bfInfo)->context = context;

    if ((error = cm_createDSPStub(itfRequire,
                    require->interface->type,
                    &(*bfInfo)->dspstub,
                    elfhandleStub,
                    &itfstubProvide)) != CM_OK)
    {
        OSAL_Free(*bfInfo);
        return error;
    }

    /* Create the FIFO Params */
    if ((error = cm_createParamsFifo(
                    (*bfInfo)->dspstub.stubInstance,
                    NULL,
                    itfRequire->client->domainId,
                    fifosize,
                    &(*bfInfo)->fifo,
                    &fifoelemsize,
                    1)) != CM_OK) /* 1 => we used first field as max params size */
    {
	    cm_destroyDSPStub(itfRequire, &(*bfInfo)->dspstub);
        OSAL_Free(*bfInfo);
        return error;
    }

    /* Bind client to stub component (Error must not occure) */
    cm_bindLowLevelInterface(itfRequire, &itfstubProvide, BF_DSP2HOST, *bfInfo);

    /* Bind stub component to host (virtual bind) */
    cm_bindVirtualInterface((*bfInfo)->dspstub.stubInstance, (t_component_instance*)NMF_HOST_COMPONENT);

    /*
     * Set Target info in FIFO param to armThis
     * Initialise FIFO Param bcDesc with  Jumptable
     * Should not return any error
     */
    fifo_params_setSharedField((*bfInfo)->fifo, 0, (t_shared_field)context /* ArmBCThis_or_TOP */);
    fifo_params_setSharedField((*bfInfo)->fifo, 1, (t_shared_field)fifoelemsize * 2/* bcDescRef */);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_ASYNCHRONOUS,
            itfRequire->client, ARM_TRACE_COMPONENT,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

    return error;
}

void cm_unbindComponentToCMCore(
        const t_interface_require_description   *itfRequire,
        t_mpc2host_bf_info                      *bfInfo)
{
    LOG_INTERNAL(1, "\n##### UnBind %s/%x.%s -> HOST #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_UNBIND_ASYNCHRONOUS,
            itfRequire->client, ARM_TRACE_COMPONENT,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

    /* Unbind virtual interface coms */
    cm_unbindVirtualInterface(bfInfo->dspstub.stubInstance);

    // Destroy FIFO params
    cm_destroyParamsFifo(bfInfo->fifo);

    // Destroy DSP Stub
    cm_destroyDSPStub(itfRequire, &bfInfo->dspstub);

    /* Free BF info */
    OSAL_Free(bfInfo);
}

/*!
 *
 */
t_cm_error cm_bindInterfaceDistributed(
        const t_interface_require_description *itfRequire,
        const t_interface_provide_description *itfProvide,
        t_uint32 fifosize,
        t_dsp_memory_type_id dspEventMemType,
        t_elfdescription                        *elfhandleSkeleton,
        t_elfdescription                        *elfhandleStub) {
    t_interface_require *require = &itfRequire->client->Template->requires[itfRequire->requireIndex];
    t_interface_provide_description itfstubProvide;
    t_cm_error error;
    t_mpc2mpc_bf_info *bfInfo;
    t_dsp_offset shareVarOffset;

    LOG_INTERNAL(1, "\n##### Bind Distributed %s/%x.%s -> %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName,
		 itfProvide->server->pathname, itfProvide->server, itfProvide->origName);

    /* Allocate aynchronous binding factory information */
    bfInfo = (t_mpc2mpc_bf_info*)OSAL_Alloc(sizeof(t_mpc2mpc_bf_info));
    if(bfInfo == 0)
        return CM_NO_MORE_MEMORY;

    /* Create the Skeleton */
    if ((error = cm_createDSPSkeleton(itfProvide,
                    fifo_normalizeDepth(fifosize), /* We SHALL create DSP Skeleton before creating the Params Fifo, but we need in advance the real depth of this fifo */
                    dspEventMemType,
                    elfhandleSkeleton,
                    &bfInfo->dspskeleton)) != CM_OK)
    {
        OSAL_Free(bfInfo);
        return error;
    }

    // Create DSP Stub
    if ((error = cm_createDSPStub(itfRequire,
                    require->interface->type,
                    &bfInfo->dspstub,
                    elfhandleStub,
                    &itfstubProvide)) != CM_OK)
    {
	cm_destroyDSPSkeleton(&bfInfo->dspskeleton);
        OSAL_Free(bfInfo);
        return error;
    }

    /* Bind client to stub component (Error must not occure) */
    cm_bindLowLevelInterface(itfRequire, &itfstubProvide, BF_DSP2DSP, bfInfo);

    /* Create the FIFO Params */
    if ((error = cm_createParamsFifo(
                    bfInfo->dspstub.stubInstance,
                    bfInfo->dspskeleton.skelInstance,
                    itfProvide->server->domainId,
                    fifosize,
                    &bfInfo->fifo,
                    NULL,
                    require->interface->methodNumber)) != CM_OK)
    {
	cm_destroyDSPStub(itfRequire, &bfInfo->dspstub);
	cm_destroyDSPSkeleton(&bfInfo->dspskeleton);
        OSAL_Free(bfInfo);
        return error;
    }

    /* Bind stub component to host (virtual bind) */
    cm_bindVirtualInterface(bfInfo->dspstub.stubInstance, bfInfo->dspskeleton.skelInstance);

    /* Set Target info in FIFO param to TOP */
    shareVarOffset = cm_getAttributeMpcAddress(bfInfo->dspskeleton.skelInstance, "TOP");

    /*
     * Set Target info in FIFO param to armThis
     * Should not return any error
     */
    fifo_params_setSharedField(bfInfo->fifo, 0, (t_shared_field)shareVarOffset /* ArmBCThis_or_TOP */);

    /* Initialise FIFO Param bcDesc with Skeleton methods */
    {
        int i;
        t_component_instance *skel = bfInfo->dspskeleton.skelInstance;
        for (i=0; i < require->interface->methodNumber; i++)
        {
            /* should not return error */
            fifo_params_setSharedField(
                    bfInfo->fifo,
                    1+i,
                    skel->Template->providesLoaded[0].indexesLoaded[0][i].methodAddresses
                    );
        }
    }

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_BIND_ASYNCHRONOUS,
            itfRequire->client, itfProvide->server,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            itfProvide->server->Template->provides[itfProvide->provideIndex].name);

    return CM_OK;
}

/*!
 *
 */
void cm_unbindInterfaceDistributed(
        const t_interface_require_description   *itfRequire,
        t_mpc2mpc_bf_info                       *bfInfo)
{
    LOG_INTERNAL(1, "\n##### UnBind distributed %s/%x.%s #####\n",
		 itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0, 0);

    cm_TRC_traceBinding(TRACE_BIND_COMMAND_UNBIND_ASYNCHRONOUS,
            itfRequire->client, NULL,
            itfRequire->client->Template->requires[itfRequire->requireIndex].name,
            NULL);

    /* Unbind virtual interface */
    cm_unbindVirtualInterface(bfInfo->dspstub.stubInstance);

    // Destroy FIFO params
    cm_destroyParamsFifo(bfInfo->fifo);

    // Destroy DSP Stub
    cm_destroyDSPStub(itfRequire, &bfInfo->dspstub);

    // Destory DSP Skeleton
    cm_destroyDSPSkeleton(&bfInfo->dspskeleton);

    // Destroy BF Info
    OSAL_Free(bfInfo);
}

t_cm_error cm_bindInterfaceStaticInterrupt(
        const t_nmf_core_id coreId,
        const int interruptLine,
        const t_component_instance *server,
        const char* providedItfServerName
)
{
    char requiredItfClientName[CM_IT_NAME_MAX_LENGTH];
    t_component_instance *client = cm_EEM_getExecutiveEngine(coreId)->instance;
    t_interface_require_description itfRequire;
    t_interface_provide_description itfProvide;
    t_cm_error error;

    //build it[%d] name
    if (interruptLine < 0 || interruptLine > 255) {return CM_OUT_OF_LIMITS;}
    cm_fillItName(interruptLine, requiredItfClientName);

    //do binding
    if ((error = cm_getRequiredInterface(client,requiredItfClientName,&itfRequire)) !=  CM_OK) {return error;}
    if ((error = cm_getProvidedInterface(server,providedItfServerName,&itfProvide)) !=  CM_OK) {return error;}
    if((error = cm_bindInterface(&itfRequire, &itfProvide)) != CM_OK) {return error;}

    return CM_OK;
}

t_cm_error cm_unbindInterfaceStaticInterrupt(
        const t_nmf_core_id coreId,
        const int interruptLine
)
{
    char requiredItfClientName[CM_IT_NAME_MAX_LENGTH];
    t_component_instance *client = cm_EEM_getExecutiveEngine(coreId)->instance;
    t_interface_require_description itfRequire;
    t_cm_error error;

    //build it[%d] name
    if (interruptLine < 0 || interruptLine > 255) {return CM_OUT_OF_LIMITS;}
    cm_fillItName(interruptLine, requiredItfClientName);

    //do unbinding
    if ((error = cm_getRequiredInterface(client,requiredItfClientName,&itfRequire)) !=  CM_OK) {return error;}
    cm_unbindInterface(&itfRequire);

    return CM_OK;
}

void cm_destroyRequireInterface(t_component_instance* component, t_nmf_client_id clientId)
{
    int i, j;

    /*
     * Special code for SINGLETON handling
     */
    if(component->Template->classe == SINGLETON)
    {
        if(getNumberOfBind(component) > 0)
            return;
    }

    for(i = 0; i < component->Template->requireNumber; i++)
    {
        int nb = component->Template->requires[i].collectionSize;
        for(j = 0; j < nb; j++)
        {
            if(component->interfaceReferences[i][j].instance != NULL)
            {
                t_interface_reference* itfRef = &component->interfaceReferences[i][j];
                t_interface_require_description itfRequire;

                itfRequire.client = component;
                itfRequire.requireIndex = i;
                itfRequire.collectionIndex = j;
                itfRequire.origName = component->Template->requires[i].name;

                switch (itfRef->bfInfoID) {
                case BF_SYNCHRONOUS:
                    /* Error ignored as it is always OK */
                    cm_unbindInterface(&itfRequire);
                    break;
                case BF_TRACE:
                    cm_unbindInterfaceTrace(&itfRequire,
                            (t_trace_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);
                    break;
                case BF_ASYNCHRONOUS:
                    cm_unbindInterfaceAsynchronous(&itfRequire,
                            (t_async_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);
                    break;
                case BF_DSP2HOST: 
                    /* This 'mpc2host handle' is provided by the host at OS Integration level.
                           It must then be handled and released in OS specific part.
                     */
                    cm_unbindComponentToCMCore(&itfRequire,
                            (t_mpc2host_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);
                    break;
                case BF_HOST2DSP:
                    /* These bindings are from CM Core to DSP, they are not listed
		       here and must be handled/freed by host at OS Integration level
                     */
                    break;
                case BF_DSP2DSP:
                    cm_unbindInterfaceDistributed(&itfRequire,
                            (t_mpc2mpc_bf_info*)itfRequire.client->interfaceReferences[itfRequire.requireIndex][itfRequire.collectionIndex].bfInfo);
                    break;
                default:
                    break;
                }
            }
        }
    }
}

void cm_registerSingletonBinding(
        t_component_instance*                   component,
        t_interface_require_description*        itfRequire,
        t_interface_provide_description*        itfProvide,
        t_nmf_client_id                         clientId)
{
    if(component->Template->classe == SINGLETON)
    {
        struct t_client_of_singleton* cl = cm_getClientOfSingleton(component, FALSE, clientId);
        if(cl != NULL)
            cl->numberOfBind++;

        if(itfProvide != NULL)
            LOG_INTERNAL(1, "  -> Singleton[%d] : Register binding %s/%x.%s -> %s/%x\n",
                    clientId,
                    itfRequire->client->pathname, itfRequire->client, itfRequire->origName,
                    itfProvide->server->pathname, itfProvide->server);
        else
            LOG_INTERNAL(1, "  -> Singleton[%d] : Register binding %s/%x.%s -> ARM/VOID\n",
                    clientId,
                    itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0);
    }
}

t_bool cm_unregisterSingletonBinding(
        t_component_instance*                   component,
        t_interface_require_description*        itfRequire,
        t_interface_provide_description*        itfProvide,
        t_nmf_client_id                         clientId)
{
    if(component->Template->classe == SINGLETON)
    {
        struct t_client_of_singleton* cl = cm_getClientOfSingleton(component, FALSE, clientId);
        if(cl != NULL)
            cl->numberOfBind--;

        if(itfProvide->server == (t_component_instance *)NMF_VOID_COMPONENT)
            LOG_INTERNAL(1, "  -> Singleton[%d] : Unregister binding %s/%x.%s -> ARM/VOID\n",
                    clientId,
                    itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0);
        else if(itfProvide->server == NULL)
            LOG_INTERNAL(1, "  -> Singleton[%d] : Unregister binding %s/%x.%s -> ?? <already unbound>\n",
                    clientId,
                    itfRequire->client->pathname, itfRequire->client, itfRequire->origName, 0, 0);
        else
            LOG_INTERNAL(1, "  -> Singleton[%d] : Unregister binding %s/%x.%s -> %s/%x\n",
                    clientId,
                    itfRequire->client->pathname, itfRequire->client, itfRequire->origName,
                    itfProvide->server->pathname, itfProvide->server);

        if(getNumberOfBind(component) == 0)
        {
            LOG_INTERNAL(1, "  -> Singleton[%d] : All required of %s/%x logically unbound, perform physical unbind\n",
                    clientId, itfRequire->client->pathname, itfRequire->client, 0, 0, 0);

            (void)cm_EEM_ForceWakeup(component->Template->dspId);

            // This is the last binding unbind all !!!
            cm_destroyRequireInterface(component, clientId);

            cm_EEM_AllowSleep(component->Template->dspId);
        }
        else if(itfProvide->server != NULL)
        {
            t_interface_require* itfReq;
            itfReq = &itfRequire->client->Template->requires[itfRequire->requireIndex];
            if((itfReq->requireTypes & OPTIONAL_REQUIRE) != 0x0)
                return TRUE;
        }

        return FALSE;
    }

    return TRUE;
}

static t_uint16 getNumberOfBind(t_component_instance* component)
{
    t_uint16 bindNumber = 0;
    struct t_client_of_singleton* cur = component->clientOfSingleton;

    for( ; cur != NULL ; cur = cur->next)
    {
        bindNumber += cur->numberOfBind;
    }

    return bindNumber;
}

static void cm_fillItName(int interruptLine, char *itName)
{
    int divider = 10000;

    *itName++ = 'i';
    *itName++ = 't';
    *itName++ = '[';

    // Find first significant divider
    while(divider > interruptLine)
        divider /= 10;

    // Compute number
    do
    {
        *itName++ = "0123456789"[interruptLine / divider];
        interruptLine %= divider;
        divider /= 10;
    } while(divider != 0);

    *itName++ = ']';
    *itName++ = '\0';
}
