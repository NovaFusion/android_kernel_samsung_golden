/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/utils/inc/string.h>

#include <cm/engine/component/inc/component_type.h>
#include <cm/engine/component/inc/bind.h>
#include <cm/engine/configuration/inc/configuration.h>
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/repository_mgt/inc/repository_mgt.h>
#include <cm/engine/api/repository_mgt_engine.h>
#include <cm/engine/trace/inc/trace.h>


#undef NHASH
#define NHASH 157       //Use a prime number!
#define MULT 17

static t_rep_component *componentCaches[NHASH];

static unsigned int repcomponentHash(const char *str)
{
    unsigned int h = 0;
    for(; *str; str++)
        h = MULT * h + *str;
    return h % NHASH;
}

static void repcomponentAdd(t_rep_component *component)
{
    unsigned int h = repcomponentHash(component->name);

    if(componentCaches[h] != NULL)
        componentCaches[h]->prev = component;
    component->next = componentCaches[h];
    component->prev = NULL;
    componentCaches[h] = component;
}

static void repcomponentRemove(t_rep_component *component)
{
    unsigned int h = repcomponentHash(component->name);

    if(component->prev != NULL)
        component->prev->next = component->next;
    if(component->next != NULL)
        component->next->prev = component->prev;
    if(component == componentCaches[h])
        componentCaches[h] = component->next;
}


PUBLIC t_cm_error cm_REP_lookupComponent(const char *name, t_rep_component **component)
{
    t_rep_component *tmp;

    for(tmp = componentCaches[repcomponentHash(name)]; tmp != NULL; tmp = tmp->next)
    {
        if(cm_StringCompare(name, tmp->name, MAX_TEMPLATE_NAME_LENGTH) == 0)
        {
            if(component != NULL)
                *component = tmp;
            return CM_OK;
        }
    }

    return CM_COMPONENT_NOT_FOUND;
}

t_elfdescription* cm_REP_getComponentFile(t_dup_char templateName, t_elfdescription* elfhandle)
{
    if(elfhandle == NULL)
    {
        t_rep_component *pRepComponent;

        for(pRepComponent = componentCaches[repcomponentHash(templateName)]; pRepComponent != NULL; pRepComponent = pRepComponent->next)
        {
            if(pRepComponent->name == templateName)
                return pRepComponent->elfhandle;
        }

        return NULL;
    }

    return elfhandle;
}


PUBLIC void cm_REP_Destroy(void)
{
	t_rep_component *component, *next;
	int i;

	for(i = 0; i < NHASH; i++)
	{
	    for (component = componentCaches[i]; component != NULL; component = next)
	    {
	        next = component->next;
	        cm_ELF_CloseFile(FALSE, component->elfhandle);
	        cm_StringRelease(component->name);
	        OSAL_Free(component);
	    }
	    componentCaches[i] = NULL;
	}
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_GetRequiredComponentFiles(
        // IN
        t_action_to_do action,
        const t_cm_instance_handle client,
        const char *requiredItfClientName,
        const t_cm_instance_handle server,
        const char *providedItfServerName,
        // OUT component to be pushed
        char fileList[][MAX_INTERFACE_TYPE_NAME_LENGTH],
        t_uint32 listSize,
        // OUT interface information
        char type[MAX_INTERFACE_TYPE_NAME_LENGTH],
        t_uint32 *methodNumber)
{
    t_cm_error error;
    t_component_instance* compClient, *compServer;
    int n;

    OSAL_LOCK_API();

    // No component required
    for(n = 0; n < listSize; n++)
        fileList[n][0] = 0;

    compClient = cm_lookupComponent(client);
    compServer = cm_lookupComponent(server);
    switch(action)
    {
    case BIND_FROMUSER:{
        t_interface_provide_description itfProvide;

        // Check server validity
        if((error = cm_checkValidServer(compServer, providedItfServerName,
                &itfProvide)) == CM_OK)
        {
            cm_StringCopy(type, itfProvide.server->Template->provides[itfProvide.provideIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);

            cm_StringCopy(fileList[0], "_sk.", MAX_INTERFACE_TYPE_NAME_LENGTH);
            cm_StringConcatenate(fileList[0], itfProvide.server->Template->provides[itfProvide.provideIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);
        }
    } break;

    case BIND_TOUSER: {
        /* Get Components names for a BindComponentToCMCore */
        t_interface_require_description itfRequire;
        t_bool bindable;

        // Check client validity
        if((error = cm_checkValidClient(compClient, requiredItfClientName,
                &itfRequire, &bindable)) == CM_OK)
        {
            cm_StringCopy(type, itfRequire.client->Template->requires[itfRequire.requireIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);
            *methodNumber = itfRequire.client->Template->requires[itfRequire.requireIndex].interface->methodNumber;

            cm_StringCopy(fileList[0], "_st.", MAX_INTERFACE_TYPE_NAME_LENGTH);
            cm_StringConcatenate(fileList[0], itfRequire.client->Template->requires[itfRequire.requireIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);
        }
    }; break;

    case BIND_ASYNC: {
        /* Get Components names for an asynchronous binding */
        t_interface_require_description itfRequire;
        t_interface_provide_description itfProvide;
        t_bool bindable;

        // Check invalid binding
        if((error = cm_checkValidBinding(compClient, requiredItfClientName,
                compServer, providedItfServerName,
                &itfRequire, &itfProvide, &bindable)) == CM_OK)
        {
                if(compClient->Template->dspId != compServer->Template->dspId)
                {
                    cm_StringCopy(fileList[0], "_sk.", MAX_INTERFACE_TYPE_NAME_LENGTH);
                    cm_StringConcatenate(fileList[0], itfRequire.client->Template->requires[itfRequire.requireIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);

                    cm_StringCopy(fileList[1], "_st.", MAX_INTERFACE_TYPE_NAME_LENGTH);
                    cm_StringConcatenate(fileList[1], itfRequire.client->Template->requires[itfRequire.requireIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);
                }
                else
                {
                    cm_StringCopy(fileList[0], "_ev.", MAX_INTERFACE_TYPE_NAME_LENGTH);
                    cm_StringConcatenate(fileList[0], itfRequire.client->Template->requires[itfRequire.requireIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);
                }
        }
    }; break;

    case BIND_TRACE: {
        /* Get Components names for an asynchronous binding */
        t_interface_require_description itfRequire;
        t_interface_provide_description itfProvide;
        t_bool bindable;

        // Check invalid binding
        if((error = cm_checkValidBinding(compClient, requiredItfClientName,
                compServer, providedItfServerName,
                &itfRequire, &itfProvide, &bindable)) == CM_OK)
        {
            cm_StringCopy(fileList[0], "_tr.", MAX_INTERFACE_TYPE_NAME_LENGTH);
            cm_StringConcatenate(fileList[0], itfRequire.client->Template->requires[itfRequire.requireIndex].interface->type, MAX_INTERFACE_TYPE_NAME_LENGTH);
        }
    }; break;

    default:
        error = CM_OK;
        break;
    }

    if(error == CM_OK)
    {
        for(n = 0; n < listSize; n++)
        {
            t_rep_component *comp;

            // If already loaded, don't ask to load it and put the name to NULL
            if (fileList[n][0] != 0 &&
                    cm_REP_lookupComponent(fileList[n], &comp) == CM_OK)
                fileList[n][0] = 0;
        }
    }


    OSAL_UNLOCK_API();
    return error;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_PushComponent(const char *name, const void *data, t_cm_size size)
{
	t_rep_component *comp;
	t_cm_error error;

	OSAL_LOCK_API();

	if (cm_REP_lookupComponent(name, &comp) == CM_OK) {
		/* Component is already there: silently ignore it */
		OSAL_UNLOCK_API();
		return CM_OK;
	}

	comp = OSAL_Alloc(sizeof(*comp));
	if (comp == NULL) {
		OSAL_UNLOCK_API();
		return CM_NO_MORE_MEMORY;
	}

	comp->name = cm_StringDuplicate(name);
	if(comp->name == NULL)
	{
        OSAL_Free(comp);
        OSAL_UNLOCK_API();
        return CM_NO_MORE_MEMORY;
	}

    if((error = cm_ELF_CheckFile(
            data,
            FALSE,
            &comp->elfhandle)) != CM_OK) {
        ERROR("Failed to load component %s\n", name, 0, 0, 0, 0, 0);
        cm_StringRelease(comp->name);
        OSAL_Free(comp);
        OSAL_UNLOCK_API();
        return error;
    }
/*
	if (OSAL_Copy(comp->data, data, size)) {
		OSAL_Free(comp);
		OSAL_UNLOCK_API();
		return CM_UNKNOWN_MEMORY_HANDLE;
	}*/

    repcomponentAdd(comp);

	OSAL_UNLOCK_API();
	return CM_OK;
}

PUBLIC EXPORT_SHARED t_cm_error CM_ENGINE_ReleaseComponent (const char *name)
{
	t_rep_component *component;
	t_cm_error err;

	OSAL_LOCK_API();
	err = cm_REP_lookupComponent(name , &component);

	if (CM_OK == err)
	{
	    repcomponentRemove(component);

	    cm_ELF_CloseFile(FALSE, component->elfhandle);
	    cm_StringRelease(component->name);
	    OSAL_Free(component);
	}

	OSAL_UNLOCK_API();

	return err;
}

PUBLIC EXPORT_SHARED t_bool CM_ENGINE_IsComponentCacheEmpty(void)
{
	int i;

	OSAL_LOCK_API();
	for(i = 0; i < NHASH; i++) {
		if (componentCaches[i] != NULL) {
			OSAL_UNLOCK_API();
			return FALSE;
		}
	}
	OSAL_UNLOCK_API();
	return TRUE;
}
