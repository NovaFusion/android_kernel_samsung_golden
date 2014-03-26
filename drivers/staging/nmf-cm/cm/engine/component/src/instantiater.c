/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/component/inc/bind.h>
#include <cm/engine/component/inc/initializer.h>

#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/configuration/inc/configuration_status.h>

#include <cm/engine/dsp/inc/dsp.h>

#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/trace/inc/xtitrace.h>

#include <cm/engine/memory/inc/domain.h>

#include <cm/engine/utils/inc/string.h>
#include <cm/engine/utils/inc/mem.h>
#include <cm/engine/utils/inc/convert.h>

#include <cm/engine/power_mgt/inc/power.h>


t_nmf_table ComponentTable; /**< list (table) of components */

static t_uint32 cm_getMaxStackValue(t_component_instance *pComponent);
static t_uint16 getNumberOfInstance(t_component_instance* component);
static t_uint16 getNumberOfStart(t_component_instance* component);


t_cm_error cm_COMP_Init(void) {
	t_cm_error error;
	error = cm_initTable(&ComponentTable);
	if (error == CM_OK)
		error = cm_initTable(&Host2MpcBindingTable);
	return error;
}

void cm_COMP_Destroy(void) {
	cm_destroyTable(&ComponentTable);
	cm_destroyTable(&Host2MpcBindingTable);
}

/** cm_addComponent - Add an internal handler to the list
 *
 *  1. Increase the size of the list if it's full
 *  2. Search an empty entry
 *  3. Add the element to the list
 *  4. Compute and return the "user handle" (= t_cm_instance_handle)
 */
static t_cm_instance_handle cm_addComponent(t_component_instance *comp)
{
	OSAL_DisableServiceMessages();
	comp->instance = cm_addEntry(&ComponentTable, comp);
	OSAL_EnableServiceMessages();

	return comp->instance;
}

/** cm_delComponent - remove the given component from the list
 *
 *  1. Check if the handle is valid
 *  2. Search the entry and free it
 */
static void cm_delComponent(t_component_instance *comp)
{
	if (comp == NULL)
		return;

	OSAL_DisableServiceMessages();
	cm_delEntry(&ComponentTable, comp->instance & INDEX_MASK);
	OSAL_EnableServiceMessages();
}

/** cm_lookupComponent - search the component corresponding to
 *                       the component instance.
 *
 * 1. Check if the instance is valid
 * 2. Return a pointer to the component
 */
t_component_instance *cm_lookupComponent(const t_cm_instance_handle hdl)
{
	return cm_lookupEntry(&ComponentTable, hdl);
}

static void cm_DestroyComponentMemory(t_component_instance *component)
{
    int i;

    /*
     * Remove instance from list
     */
    cm_delComponent(component);

    /*
     * Destroy instance
     */
    {
        struct t_client_of_singleton* cur = component->clientOfSingleton;

        for( ; cur != NULL ; )
        {
            struct t_client_of_singleton* tmp = cur;
            cur = cur->next;

            OSAL_Free(tmp);
        }
    }

    for(i = 0; i < component->Template->requireNumber; i++)
    {
        OSAL_Free(component->interfaceReferences[i]);
    }

    cm_StringRelease(component->pathname);

    cm_ELF_FreeInstance(component->Template->dspId, component->Template->memories, component->memories);

    cm_unloadComponent(component->Template);
    OSAL_Free(component);
}

/**
 * Non-Require:
 *    - MMDSP could be sleep (Since we access it only through HSEM)
 */
void cm_delayedDestroyComponent(t_component_instance *component) {
    int i;

    if (osal_debug_ops.component_destroy)
	    osal_debug_ops.component_destroy(component);

    /*
     * Remove component from load map here
     */
    cm_DSPABI_RemoveLoadMap(
            component->domainId,
            component->Template->name,
            component->memories,
            component->pathname,
            component);

    // Generate XTI/STM trace
    cm_TRC_traceLoadMap(TRACE_COMPONENT_COMMAND_REMOVE, component);

    /*
     * disconnect interrupt handler if needed
     */
    for(i = 0; i < component->Template->provideNumber; i++)
    {
        if(component->Template->provides[i].interruptLine)
        {
            cm_unbindInterfaceStaticInterrupt(component->Template->dspId, component->Template->provides[i].interruptLine);
        }
    }

    /*
     * Update dsp stack size if needed
     */
    if (component->Template->minStackSize > MIN_STACK_SIZE)
    {
        if (cm_EEM_isStackUpdateNeed(component->Template->dspId, component->priority, FALSE, component->Template->minStackSize))
        {
            t_uint32 newStackValue;
            t_uint32 maxComponentStackSize;

            maxComponentStackSize = cm_getMaxStackValue(component);
            cm_EEM_UpdateStack(component->Template->dspId, component->priority, maxComponentStackSize, &newStackValue);
	    if (cm_DSP_GetState(component->Template->dspId)->state == MPC_STATE_BOOTED)
		    cm_COMP_UpdateStack(component->Template->dspId, newStackValue);
        }
    }

    cm_DestroyComponentMemory(component);
}

/**
 * Pre-Require:
 *    - MMDSP wakeup (when loading in TCM)
 */
t_cm_error cm_instantiateComponent(const char* templateName,
                                   t_cm_domain_id domainId,
                                   t_nmf_ee_priority priority,
                                   const char* pathName,
                                   t_elfdescription *elfhandle,
                                   t_component_instance** refcomponent)
{
    t_nmf_core_id coreId = cm_DM_GetDomainCoreId(domainId);
    t_dup_char templateNameDup;
    t_component_template* template;
    t_component_instance *component;
    /* coverity[var_decl] */
    t_cm_error error;
    int i, j, k;

    *refcomponent = NULL;

    templateNameDup = cm_StringDuplicate(templateName);
    if(templateNameDup == NULL)
        return CM_NO_MORE_MEMORY;

    /*
     * Lookup in template list
     */
    template = cm_lookupTemplate(coreId, templateNameDup);
    if(template != NULL)
    {
        if(template->classe == SINGLETON)
        {
            // Return same handle for singleton component
            struct t_client_of_singleton* cl;

            cm_StringRelease(templateNameDup);

            cl = cm_getClientOfSingleton(template->singletonIfAvaliable, TRUE, domainDesc[domainId].client);
            if(cl == NULL)
                return CM_NO_MORE_MEMORY;
            cl->numberOfInstance++;

            *refcomponent = template->singletonIfAvaliable;
            LOG_INTERNAL(1, "##### Singleton : New handle of %s/%x component on %s provItf=%d#####\n",
                    template->singletonIfAvaliable->pathname, template->singletonIfAvaliable, cm_getDspName(coreId),
                    template->singletonIfAvaliable->providedItfUsedCount, 0, 0);
            return CM_OK;
        }
    }

    // Get the dataFile (identity if already pass as parameter)
    if((elfhandle = cm_REP_getComponentFile(templateNameDup, elfhandle)) == NULL)
    {
        cm_StringRelease(templateNameDup);
        return CM_COMPONENT_NOT_FOUND;
    }

    // Load template
    if((error = cm_loadComponent(templateNameDup, domainId, elfhandle, &template)) != CM_OK)
    {
        cm_StringRelease(templateNameDup);
        return error;
    }

    // templateNameDup no more used, release it
    cm_StringRelease(templateNameDup);

    // Allocated component
    component = (t_component_instance*)OSAL_Alloc_Zero(
            sizeof(t_component_instance) +
            sizeof(t_interface_reference*) * template->requireNumber);
    if(component == NULL)
    {
        cm_unloadComponent(template);
        return CM_NO_MORE_MEMORY;
    }

    component->interfaceReferences = (t_interface_reference**)((char*)component + sizeof(t_component_instance));
    component->Template = template;

    /*
     * Update linked list
     */
    if (cm_addComponent(component) == 0) {
        cm_unloadComponent(template);
        OSAL_Free(component);
        return CM_NO_MORE_MEMORY;
    }

    // NOTE: From here use cm_DestroyComponentMemory

    component->pathname = pathName ? cm_StringDuplicate(pathName) : cm_StringReference(anonymousDup);
    if(component->pathname == NULL)
    {
        cm_DestroyComponentMemory(component);
        return CM_NO_MORE_MEMORY;
    }

    LOG_INTERNAL(1, "\n##### Instantiate %s/%x (%s) component on %s at priority %d #####\n", component->pathname, component, template->name, cm_getDspName(coreId), priority, 0);

    if((error = cm_ELF_LoadInstance(domainId, elfhandle, template->memories, component->memories, template->classe == SINGLETON)) != CM_OK)
    {
        cm_DestroyComponentMemory(component);
        return error;
    }

    if((error = cm_ELF_relocatePrivateSegments(
            elfhandle,
            template)) != CM_OK)
    {
        cm_DestroyComponentMemory(component);
        return error;
    }

    cm_ELF_FlushInstance(coreId, template->memories, component->memories);

    /*
     * Create a new component instance
     */
    component->priority = priority;
    component->thisAddress = 0xFFFFFFFF;
    component->state = STATE_NONE;

    if(component->Template->classe == SINGLETON)
    {   // Return same handle for singleton component
        struct t_client_of_singleton* cl = cm_getClientOfSingleton(component, TRUE, domainDesc[domainId].client);
        if(cl == NULL)
        {
            cm_DestroyComponentMemory(component);
            return CM_NO_MORE_MEMORY;
        }

        cl->numberOfInstance = 1;
        template->singletonIfAvaliable = component;
	if (cm_DM_GetDomainCoreId(domainId) == SVA_CORE_ID)
		component->domainId = DEFAULT_SVA_DOMAIN;
	else
		component->domainId = DEFAULT_SIA_DOMAIN;
    } else {
        component->domainId = domainId;
    }

    if(component->memories[template->thisMemory->id] != INVALID_MEMORY_HANDLE)
        cm_DSP_GetDspAddress(component->memories[template->thisMemory->id], &component->thisAddress);
    else {
        // In case of singleton or component without data
        component->thisAddress = 0;
    }

    /*
     * Create empty required interfaces array and set method interface to Panic
     */
    for(i = 0; i < template->requireNumber; i++) // For all required interface
    {
        component->interfaceReferences[i] =
                (t_interface_reference*)OSAL_Alloc_Zero(sizeof(t_interface_reference) * template->requires[i].collectionSize);
        if(component->interfaceReferences[i] == NULL)
        {
            cm_DestroyComponentMemory(component);
            return CM_NO_MORE_MEMORY;
        }

        for(j = 0; j < template->requires[i].collectionSize; j++) // ... and for each index in collection (set THIS&method for each client)
        {
            component->interfaceReferences[i][j].instance = NULL;
            component->interfaceReferences[i][j].bfInfoID = BF_SYNCHRONOUS; // Just to memorize no Binding component used and unbind ToVoid happy ;-).

            if(template->classe == COMPONENT && template->requires[i].indexes != NULL)
            {
                // If component, fill THIS to itself to detect UNBINDED panic with rigth DSP
                t_interface_require_index *requireindex = &template->requires[i].indexes[j];
                for(k = 0; k < requireindex->numberOfClient; k++)
                {
                    t_uint32 *hostAddr;

                    hostAddr = (t_uint32*)(
                            cm_DSP_GetHostLogicalAddress(
                                    component->memories[requireindex->memories[k].memory->id]) +
                                    requireindex->memories[k].offset * requireindex->memories[k].memory->memEntSize);
                    *hostAddr++ = (t_uint32)component->thisAddress;
                }
            }
        }
    }

    /*
     * Inform debugger about new component
     */
    if ((error = cm_DSPABI_AddLoadMap(
            domainId,
            template->name,
            component->pathname,
            component->memories,
            component)) != CM_OK)
    {
        cm_DestroyComponentMemory(component);
        return error;
    }

    // Generate XTI/STM trace
    cm_TRC_traceLoadMap(TRACE_COMPONENT_COMMAND_ADD, component);

    // NOTE: From here use cm_delayedDestroyComponent

    /*
     * Relocate interrupt if this is an interrupt
     */
    for(i = 0; i < template->provideNumber; i++)
    {
        if(template->provides[i].interruptLine)
        {
            if ((error = cm_bindInterfaceStaticInterrupt(coreId,
                    template->provides[i].interruptLine,
                    component,
                    template->provides[i].name)) != CM_OK)
            {
                cm_delayedDestroyComponent(component);
                return error;
            }
        }
    }

    /*
     * For first instance of a component; Update ee stack size if needed
     */
    if(template->classe != FIRMWARE && template->numberOfInstance == 1 && template->minStackSize > MIN_STACK_SIZE)
    {
        t_uint32 newStackValue;

        if (cm_EEM_isStackUpdateNeed(template->dspId, priority, TRUE, template->minStackSize))
        {
            error = cm_EEM_UpdateStack(template->dspId, priority, template->minStackSize, &newStackValue);
            if (error != CM_OK)
            {
                cm_delayedDestroyComponent(component);
                return error;
            }
            cm_COMP_UpdateStack(template->dspId, newStackValue);
        }
    }


    /*
     * For component or first instance
     */
    if(template->classe == SINGLETON || template->classe == COMPONENT)
    {
        /*
         * Call init function generated by the compiler (one per .elf)
         */
        LOG_INTERNAL(2, "constructor call(s) <%s>\n", template->name, 0, 0, 0, 0, 0);
        if (cm_DSP_GetState(template->dspId)->state != MPC_STATE_BOOTED)
        {
            cm_delayedDestroyComponent(component);
            return CM_MPC_NOT_RESPONDING;
        }
        else if ((error = cm_COMP_CallService(
                (priority > cm_EEM_getExecutiveEngine(coreId)->instance->priority)?NMF_CONSTRUCT_SYNC_INDEX:NMF_CONSTRUCT_INDEX,
                component,
                template->LCCConstructAddress)) != CM_OK)
        {
            if (error == CM_MPC_NOT_RESPONDING)
                ERROR("CM_MPC_NOT_RESPONDING: can't call constructor '%s'\n", component->pathname, 0, 0, 0, 0, 0);
            cm_delayedDestroyComponent(component);
            return error;
        }
    }
    else
    {
        /* be sure everything is write into memory, not required elsewhere since will be done by cm_COMP_CallService */
        OSAL_mb();
    }

    // For firmware; Directly switch to STARTED state, don't need to start it
    if (template->classe == FIRMWARE)
        component->state = STATE_RUNNABLE;
    else
        component->state = STATE_STOPPED;

    if (osal_debug_ops.component_create)
	    osal_debug_ops.component_create(component);

    *refcomponent = component;
    return CM_OK;
}

struct t_client_of_singleton* cm_getClientOfSingleton(t_component_instance* component, t_bool createdIfNotExist, t_nmf_client_id clientId)
{
    struct t_client_of_singleton* cur = component->clientOfSingleton;

    for( ; cur != NULL ; cur = cur->next)
    {
        if(cur->clientId == clientId)
        {
            return cur;
        }
    }

    //if(createdIfNotExist)
    {
        cur = OSAL_Alloc(sizeof(struct t_client_of_singleton));
        if(cur != NULL)
        {
            cur->clientId = clientId;
            cur->next = component->clientOfSingleton;
            cur->numberOfBind = 0;
            cur->numberOfInstance= 0;
            cur->numberOfStart = 0;
            component->clientOfSingleton = cur;
        }
    }
    return cur;
}

/**
 * Non-Require:
 *    - MMDSP could be sleep (Since we access it only through HSEM)
 */
t_cm_error cm_startComponent(t_component_instance* component, t_nmf_client_id clientId)
{
    t_cm_error error;
    char  value[MAX_PROPERTY_VALUE_LENGTH];
    int i;

    /*
     * Special code for SINGLETON handling
     */
    if(component->Template->classe == SINGLETON)
    {
        struct t_client_of_singleton* cl = cm_getClientOfSingleton(component, FALSE, clientId);
        if(cl != NULL)
            cl->numberOfStart++;
        // A singleton could be started twice, thus start it only if first client starter
        if(getNumberOfStart(component) > 1)
            return CM_OK;

        // Fall through and start really the singleton.
    }

    if(component->state == STATE_RUNNABLE)
        return CM_COMPONENT_NOT_STOPPED;

    // CM_ASSERT component->state == STATE_STOPPED

    /*
     * Check that all required binding have been binded!
     */
    for(i = 0; i < component->Template->requireNumber; i++)
    {
        int nb = component->Template->requires[i].collectionSize, j;
        for(j = 0; j < nb; j++)
        {
            if(component->interfaceReferences[i][j].instance == NULL  &&
                    (component->Template->requires[i].requireTypes & (OPTIONAL_REQUIRE | INTRINSEC_REQUIRE)) == 0)
            {
                ERROR("CM_REQUIRE_INTERFACE_UNBINDED: Required interface '%s'.'%s' binded\n", component->pathname, component->Template->requires[i].name, 0, 0, 0, 0);
                return CM_REQUIRE_INTERFACE_UNBINDED;
            }
        }
    }

    component->state = STATE_RUNNABLE;

    /*
     * Power on, HW resources if required
     */
    if(cm_getComponentProperty(
            component,
            "hardware",
            value,
            sizeof(value)) == CM_OK)
    {
        error = cm_PWR_EnableMPC(MPC_PWR_HWIP, component->Template->dspId);
        if(error != CM_OK)
            return error;
    }

    /*
     * Call starter if available
     */
    if(component->Template->LCCStartAddress != 0)
    {
        if (cm_DSP_GetState(component->Template->dspId)->state != MPC_STATE_BOOTED)
        {
            return CM_MPC_NOT_RESPONDING;
        }
        else if ((error = cm_COMP_CallService(
                (component->priority > cm_EEM_getExecutiveEngine(component->Template->dspId)->instance->priority)?NMF_START_SYNC_INDEX:NMF_START_INDEX,
                component,
                component->Template->LCCStartAddress)) != CM_OK)
        {
            if (error == CM_MPC_NOT_RESPONDING)
                ERROR("CM_MPC_NOT_RESPONDING: can't call starter '%s'\n", component->pathname, 0, 0, 0, 0, 0);
            return error;
        }
    }

    return CM_OK;
}

/**
 * Non-Require:
 *    - MMDSP could be sleep (Since we access it only through HSEM)
 */
t_cm_error cm_stopComponent(t_component_instance* component, t_nmf_client_id clientId)
{
    char  value[MAX_PROPERTY_VALUE_LENGTH];
    t_cm_error error = CM_OK;
    t_bool isHwProperty;

    /*
     * Special code for SINGLETON handling
     */
    if(component->Template->classe == SINGLETON)
    {
        struct t_client_of_singleton* cl = cm_getClientOfSingleton(component, FALSE, clientId);
        if(cl != NULL)
            cl->numberOfStart--;
        // A singleton could be started twice, thus stop it only if no more client starter
        if(getNumberOfStart(component) > 0)
            return CM_OK;

        // Fall through and stop really the singleton.
    }

    /*
     * Component life cycle sanity check
     */
    if(component->state == STATE_STOPPED)
        return CM_COMPONENT_NOT_STARTED;

    // CM_ASSERT component->state == STATE_RUNNABLE
    component->state = STATE_STOPPED;

    isHwProperty = (cm_getComponentProperty(
                component,
                "hardware",
                value,
                sizeof(value)) == CM_OK);

    if (cm_DSP_GetState(component->Template->dspId)->state != MPC_STATE_BOOTED)
    {
        error = CM_MPC_NOT_RESPONDING;
    }
    else
    {
        /*
         * Call stopper if available
         */
        if(component->Template->LCCStopAddress != 0)
        {
            if ((error = cm_COMP_CallService(
                    isHwProperty ? NMF_STOP_SYNC_INDEX : NMF_STOP_INDEX,
                    component,
                    component->Template->LCCStopAddress)) != CM_OK)
            {
                if (error == CM_MPC_NOT_RESPONDING)
                    ERROR("CM_MPC_NOT_RESPONDING: can't call stopper '%s'\n", component->pathname, 0, 0, 0, 0, 0);
            }
        }
    }

    /*
     * Power on, HW resources if required
     */
    if(isHwProperty)
    {
        cm_PWR_DisableMPC(MPC_PWR_HWIP, component->Template->dspId);
    }

    return error;
}

t_cm_error cm_destroyInstance(t_component_instance* component, t_destroy_state forceDestroy)
{
    int i, j;

    LOG_INTERNAL(1, "\n##### Destroy %s/%x (%s) component on %s #####\n",
                 component->pathname, component, component->Template->name, cm_getDspName(component->Template->dspId), 0, 0);

    /*
     * Component life cycle sanity check; do it only when destroying last reference.
     */
    if(forceDestroy == DESTROY_NORMAL)
    {
        if (component->state == STATE_RUNNABLE)
            return CM_COMPONENT_NOT_STOPPED;

        // CM_ASSERT component->state == STATE_STOPPED

        // Check that all required binding have been unbound!
        for(i = 0; i < component->Template->requireNumber; i++)
        {
            int nb = component->Template->requires[i].collectionSize;
            for(j = 0; j < nb; j++)
            {
                if(component->interfaceReferences[i][j].instance != NULL)
                {
                    ERROR("CM_COMPONENT_NOT_UNBINDED: Required interface %s/%x.%s still binded\n",
                            component->pathname, component, component->Template->requires[i].name, 0, 0, 0);
                    return CM_COMPONENT_NOT_UNBINDED;
                }
            }
        }

        // Check that all provided bindings have been unbound!
        if (component->providedItfUsedCount != 0)
        {
            unsigned idx;

            ERROR("CM_COMPONENT_NOT_UNBINDED: Still %d binding to %s/%x provided interface\n",
                    component->providedItfUsedCount, component->pathname, component, 0, 0, 0);

            /* Find which interface is still bound to gracefully print an error message */
            for (idx=0; idx<ComponentTable.idxMax; idx++)
            {
                if ((componentEntry(idx) == NULL) || (componentEntry(idx) == component))
                    continue;
                for (i = 0; i < componentEntry(idx)->Template->requireNumber; i++)
                {
                    for (j = 0; j < componentEntry(idx)->Template->requires[i].collectionSize; j++)
                    {
                        if(componentEntry(idx)->interfaceReferences[i][j].instance == component
                                && component->Template->provides[componentEntry(idx)->interfaceReferences[i][j].provideIndex].interruptLine == 0)
                        {
                            ERROR("  -> %s/%x.%s still used by %s/%x.%s\n",
                                    component->pathname, component,
                                    component->Template->provides[componentEntry(idx)->interfaceReferences[i][j].provideIndex].name,
                                    componentEntry(idx)->pathname,
                                    componentEntry(idx),
                                    componentEntry(idx)->Template->requires[i].name);
                        }
                    }
                }
            }

            return CM_COMPONENT_NOT_UNBINDED;
        }
    }

    // Sanity check finished, here, we will do the JOB whatever error

    if (cm_DSP_GetState(component->Template->dspId)->state == MPC_STATE_BOOTED)
    {
        /*
         * Call destroy if available
         */
        /* Call the destructor only if we don't want to force the destruction */
        if(forceDestroy != DESTROY_WITHOUT_CHECK_CALL && component->Template->LCCDestroyAddress != 0)
        {
            if (cm_COMP_CallService(
                    NMF_DESTROY_INDEX,
                    component,
                    component->Template->LCCDestroyAddress) != CM_OK)
            {
                ERROR("CM_MPC_NOT_RESPONDING: can't call destroy '%s'\n", component->pathname, 0, 0, 0, 0, 0);
            }
        }
        else
        {
            cm_COMP_Flush(component->Template->dspId);
        }
    }

    cm_delayedDestroyComponent(component);

    return CM_OK;
}

/**
 * Pre-Require:
 *    - MMDSP wakeup (when accessing loadmap)
 */
t_cm_error cm_destroyInstanceForClient(t_component_instance* component, t_destroy_state forceDestroy, t_nmf_client_id clientId)
{
    /*
     * Special code for SINGLETON handling
     */
    if(component->Template->classe == SINGLETON)
    {
        struct t_client_of_singleton* cl = cm_getClientOfSingleton(component, FALSE, clientId);
        int nbinstance;
        if(cl != NULL)
            cl->numberOfInstance--;

        // A singleton could be instantiate twice, thus destroy it only if no more client constructor
        nbinstance = getNumberOfInstance(component);
        if(nbinstance > 0)
        {
            LOG_INTERNAL(1, "##### Singleton : Delete handle of %s/%x (%s) component on %s [%d] provItf=%d #####\n",
                 component->pathname, component, component->Template->name, cm_getDspName(component->Template->dspId),
                 nbinstance, component->providedItfUsedCount);
            return CM_OK;
        }

        // Fall through
    }

    return cm_destroyInstance(component, forceDestroy);
}


static t_uint32 cm_getMaxStackValue(t_component_instance *pComponent)
{
    t_nmf_executive_engine_id executiveEngineId = cm_EEM_getExecutiveEngine(pComponent->Template->dspId)->executiveEngineId;
    t_uint32 res = MIN_STACK_SIZE;
    unsigned int i;

    for (i=0; i<ComponentTable.idxMax; i++)
    {
	if ((componentEntry(i) != NULL) &&
	    (componentEntry(i) != pComponent) &&
            (pComponent->Template->dspId == componentEntry(i)->Template->dspId) &&
            (executiveEngineId == SYNCHRONOUS_EXECUTIVE_ENGINE || componentEntry(i)->priority == pComponent->priority))
        {
		if (componentEntry(i)->Template->minStackSize > res)
                res = componentEntry(i)->Template->minStackSize;
        }
    }

    return res;
}

static t_uint16 getNumberOfInstance(t_component_instance* component)
{
    t_uint16 instanceNumber = 0;
    struct t_client_of_singleton* cur = component->clientOfSingleton;

    for( ; cur != NULL ; cur = cur->next)
    {
        instanceNumber += cur->numberOfInstance;
    }

    return instanceNumber;
}

static t_uint16 getNumberOfStart(t_component_instance* component)
{
    t_uint16 startNumber = 0;
    struct t_client_of_singleton* cur = component->clientOfSingleton;

    for( ; cur != NULL ; cur = cur->next)
    {
        startNumber += cur->numberOfStart;
    }

    return startNumber;
}
