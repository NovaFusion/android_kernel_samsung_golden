/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include "../inc/bind.h"
#include <cm/engine/trace/inc/trace.h>

#include <cm/engine/utils/inc/string.h>

t_cm_error cm_checkValidClient(
        const t_component_instance* client,
        const char* requiredItfClientName,
        t_interface_require_description *itfRequire,
        t_bool *bindable) {
    t_cm_error error;

    // Component LC state check
    if (NULL == client)
        return  CM_INVALID_COMPONENT_HANDLE;

    // Check if the requiredItfClientName is required by client component
    if ((error = cm_getRequiredInterface(client, requiredItfClientName, itfRequire)) != CM_OK)
        return error;

    // Check required interface not already binded
    {
        t_interface_reference* itfRef = &client->interfaceReferences[itfRequire->requireIndex][itfRequire->collectionIndex];

        if(itfRef->instance != (t_component_instance*)NULL)
        {
            if(client->Template->classe == SINGLETON)
            {
                // Singleton is immutable thus we can't rebind it, nevertheless it's not an issue
                *bindable = FALSE;
                return CM_OK;
            }
            else
            {
                t_interface_reference* itfRef = &client->interfaceReferences[itfRequire->requireIndex][itfRequire->collectionIndex];

                if(itfRef->instance == (const t_component_instance*)NMF_VOID_COMPONENT)
                    ERROR("CM_INTERFACE_ALREADY_BINDED(): Component (%s<%s>.%s) already bound to VOID\n",
                            client->pathname, client->Template->name, requiredItfClientName, 0, 0, 0);
                else
                    ERROR("CM_INTERFACE_ALREADY_BINDED(): Component (%s<%s>.%s) already bound to another server (%s<%s>.%s)\n",
                            client->pathname, client->Template->name, requiredItfClientName,
                            itfRef->instance->pathname, itfRef->instance->Template->name, itfRef->instance->Template->provides[itfRef->provideIndex].name);
                return CM_INTERFACE_ALREADY_BINDED;
            }
        }
    }

    // Delayed Component LC state check done only if not optional required interface or intrinsic one that has been solved by loader
    {
        t_interface_require* itfReq = &client->Template->requires[itfRequire->requireIndex];

        if((itfReq->requireTypes & (OPTIONAL_REQUIRE | INTRINSEC_REQUIRE)) == 0) {
            if(client->state == STATE_RUNNABLE)
                return CM_COMPONENT_NOT_STOPPED;
        }
    }

    *bindable = TRUE;

    return CM_OK;
}

t_cm_error cm_checkValidServer(
        const t_component_instance* server,
        const char* providedItfServerName,
        t_interface_provide_description *itfProvide) {
    t_cm_error error;

    // Check if the components are initialized
    //if (server->state == STATE_INSTANCIATED)
    //    return CM_COMPONENT_NOT_INITIALIZED;
    if(NULL == server)
        return  CM_INVALID_COMPONENT_HANDLE;

    // Check if the providedItfServerName is provided by server component
    if((error = cm_getProvidedInterface(server, providedItfServerName, itfProvide)) != CM_OK)
        return error;

    return CM_OK;
}

t_cm_error cm_checkValidBinding(
        const t_component_instance* client,
        const char* requiredItfClientName,
        const t_component_instance* server,
        const char* providedItfServerName,
        t_interface_require_description *itfRequire,
        t_interface_provide_description *itfProvide,
        t_bool *bindable) {
    t_interface_require *require;
    t_interface_provide *provide;
    t_cm_error error;

    // Check Server
    if((error = cm_checkValidServer(server, providedItfServerName, itfProvide)) != CM_OK)
        return error;

    // Check Client
    if((error = cm_checkValidClient(client, requiredItfClientName, itfRequire, bindable)) != CM_OK)
        return error;

    // If this is a singleton which has been already bound check that next binding is at the same server
    if(*bindable == FALSE
            && client->Template->classe == SINGLETON)
    {
        t_interface_reference* itfRef = &client->interfaceReferences[itfRequire->requireIndex][itfRequire->collectionIndex];
        while( itfRef->instance != server
                || itfRef->provideIndex != itfProvide->provideIndex
                || itfRef->collectionIndex != itfProvide->collectionIndex )
        {
            if(itfRef->instance == (const t_component_instance*)NMF_VOID_COMPONENT)
            {
                ERROR("CM_INTERFACE_ALREADY_BINDED(): Singleton (%s<%s>.%s) already bound to VOID\n",
                        client->pathname, client->Template->name, requiredItfClientName, 0, 0, 0);
                return CM_INTERFACE_ALREADY_BINDED;
            }
            else if(itfRef->bfInfoID == BF_ASYNCHRONOUS || itfRef->bfInfoID == BF_TRACE)
            {
                t_interface_require_description eventitfRequire;
                CM_ASSERT(cm_getRequiredInterface(itfRef->instance, "target", &eventitfRequire) == CM_OK);
                itfRef = &itfRef->instance->interfaceReferences[eventitfRequire.requireIndex][eventitfRequire.collectionIndex];

                // Go to see client of event if the same
            }
            else
            {
                ERROR("CM_INTERFACE_ALREADY_BINDED(): Singleton (%s<%s>.%s) already bound to different server (%s<%s>.%s)\n",
                        client->pathname, client->Template->name, requiredItfClientName,
                        itfRef->instance->pathname, itfRef->instance->Template->name, itfRef->instance->Template->provides[itfRef->provideIndex].name);
                return CM_INTERFACE_ALREADY_BINDED;
            }
        }
    }

    // Check if provided and required type matches
    require = &client->Template->requires[itfRequire->requireIndex];
    provide = &server->Template->provides[itfProvide->provideIndex];
    if(require->interface != provide->interface)
    {
        ERROR("CM_ILLEGAL_BINDING(%s, %s)\n", require->interface->type, provide->interface->type, 0, 0, 0, 0);
        return CM_ILLEGAL_BINDING;
    }

    // Check if static required interface binded to singleton component
    if((require->requireTypes & STATIC_REQUIRE) &&
            (server->Template->classe != SINGLETON))
    {
        ERROR("CM_ILLEGAL_BINDING(): Can't bind static required interface to not singleton component\n",
                0, 0, 0, 0, 0, 0);
        return CM_ILLEGAL_BINDING;
    }

    return CM_OK;
}

t_cm_error cm_checkValidUnbinding(
        const t_component_instance* client,
        const char* requiredItfClientName,
        t_interface_require_description *itfRequire,
        t_interface_provide_description *itfProvide) {
    t_cm_error error;
    t_interface_require* itfReq;

    // Component LC state check
    if (NULL == client)
        return CM_INVALID_COMPONENT_HANDLE;

    // Check if the requiredItfClientName is required by client component
    if ((error = cm_getRequiredInterface(client, requiredItfClientName, itfRequire)) != CM_OK)
        return error;

    itfReq = &client->Template->requires[itfRequire->requireIndex];

    // Check if the requiredItfClientName is required by client component
    if ((error = cm_lookupInterface(itfRequire, itfProvide)) != CM_OK)
    {
        // We allow to unbind optional required of singleton even if not binded, since it could have been unbound previously but we don't
        // want to break bind singleton reference counter
        if((client->Template->classe == SINGLETON) &&
                (itfReq->requireTypes & OPTIONAL_REQUIRE) != 0x0)
            return CM_OK;

        return error;
    }

    // Singleton is immutable, don't unbind it
    if(client->Template->classe == SINGLETON)
        return CM_OK;

    /* if interface is optionnal then allow unbinding even if not stop */
    if((itfReq->requireTypes & OPTIONAL_REQUIRE) == 0x0)
    {
        if(client->state == STATE_RUNNABLE)
            return CM_COMPONENT_NOT_STOPPED;
    }

    return CM_OK;
}

