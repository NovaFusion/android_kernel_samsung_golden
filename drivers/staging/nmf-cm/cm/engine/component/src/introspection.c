/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/component/inc/introspection.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/string.h>

/*
 *
 */
t_cm_error cm_getComponentProperty(
	const t_component_instance *component,
	const char                 *propName,
    char                       value[MAX_PROPERTY_VALUE_LENGTH],
    t_uint32                   valueLength){
	t_component_template* template = component->Template;
    int i;

    for(i = 0; i < template->propertyNumber; i++) {
        if(cm_StringCompare(template->properties[i].name, propName, MAX_PROPERTY_NAME_LENGTH) == 0) {
        	cm_StringCopy(
                value,
                template->properties[i].value,
                valueLength);
            return CM_OK;
        }
    }

    return CM_NO_SUCH_PROPERTY;
}

/**
 *
 */
static t_attribute* cm_getAttributeDescriptor(
        const t_component_instance  *component,
        const char                  *attrName)
{
    int i;

    for(i = 0; i < component->Template->attributeNumber; i++)
    {
        if(cm_StringCompare(component->Template->attributes[i].name, attrName, MAX_ATTRIBUTE_NAME_LENGTH) == 0)
        {
            return &component->Template->attributes[i];
        }
    }

    return NULL;
}

t_dsp_address cm_getAttributeMpcAddress(
        const t_component_instance  *component,
        const char                  *attrName)
{
    t_attribute* attribute;
    t_uint32 dspAddress;

    if((attribute = cm_getAttributeDescriptor(component, attrName)) == NULL)
        return 0x0;

    cm_DSP_GetDspAddress(component->memories[attribute->memory.memory->id], &dspAddress);

    return (dspAddress +
            attribute->memory.offset);
}

t_cm_logical_address cm_getAttributeHostAddr(
        const t_component_instance  *component,
        const char                  *attrName)
{
    t_attribute* attribute;

    if((attribute = cm_getAttributeDescriptor(component, attrName)) == NULL)
        return 0x0;

    // TODO JPF: component->Template->attributes[i].memory.offset could be converted in byte during load
    return cm_DSP_GetHostLogicalAddress(component->memories[attribute->memory.memory->id]) +
            attribute->memory.offset * attribute->memory.memory->memEntSize;
}


t_cm_error cm_readAttribute(
        const t_component_instance* component,
        const char* attrName,
        t_uint32* value)
{
    t_attribute* attribute;
    t_cm_logical_address hostAddr;

    if((attribute = cm_getAttributeDescriptor(component, attrName)) == NULL)
    {
        ERROR("CM_NO_SUCH_ATTRIBUTE(%s, %s)\n", component->pathname, attrName, 0, 0, 0, 0);
        return CM_NO_SUCH_ATTRIBUTE;
    }

    // TODO JPF: component->Template->attributes[i].memory.offset could be converted in byte during load
    hostAddr = cm_DSP_GetHostLogicalAddress(component->memories[attribute->memory.memory->id]) +
            attribute->memory.offset * attribute->memory.memory->memEntSize;

    if(attribute->memory.memory->memEntSize != 2)
        *value = *((t_uint32 *)hostAddr) & ~MASK_BYTE3;
    else
        *value = *((t_uint16 *)hostAddr);

    LOG_INTERNAL(3, "cm_readAttribute: [%s:%s, %x]=%x\n",
            component->pathname, attrName, hostAddr, *value, 0, 0);

    return CM_OK;
}

t_uint32 cm_readAttributeNoError(
        const t_component_instance* component,
        const char* attrName)
{
    t_uint32 value;

    if(cm_readAttribute(component, attrName, &value) != CM_OK)
        value = 0;

    return value;
}

t_cm_error cm_writeAttribute(
        const t_component_instance* component,
        const char* attrName,
        t_uint32 value)
{
    t_attribute* attribute;
    t_cm_logical_address hostAddr;

    if((attribute = cm_getAttributeDescriptor(component, attrName)) == NULL)
    {
        ERROR("CM_NO_SUCH_ATTRIBUTE(%s, %s)\n", component->pathname, attrName, 0, 0, 0, 0);
        return CM_NO_SUCH_ATTRIBUTE;
    }

    // TODO JPF: component->Template->attributes[i].memory.offset could be converted in byte during load
    hostAddr = cm_DSP_GetHostLogicalAddress(component->memories[attribute->memory.memory->id]) +
            attribute->memory.offset * attribute->memory.memory->memEntSize;

    if(attribute->memory.memory->memEntSize != 2)
        *((t_uint32 *)hostAddr) = value & ~MASK_BYTE3;
    else
        *((t_uint16 *)hostAddr) = value;

    /* be sure attribute is write into memory */
    OSAL_mb();

    LOG_INTERNAL(3, "cm_writeAttribute: [%s:%s, %x]=%x\n",
            component->pathname, attrName, hostAddr, value, 0, 0);

    return CM_OK;
}


/**
 *
 */
t_dsp_address cm_getFunction(
        const t_component_instance* component,
        const char* interfaceName,
        const char* methodName)
{
    t_interface_provide_description itfProvide;
    t_interface_provide* provide;
    t_interface_provide_loaded* provideLoaded;
    t_cm_error error;
    int i;

    // Get interface description
    if((error = cm_getProvidedInterface(component, interfaceName, &itfProvide)) != CM_OK)
        return error;

    provide = &component->Template->provides[itfProvide.provideIndex];
    provideLoaded = &component->Template->providesLoaded[itfProvide.provideIndex];

    for(i = 0; i < provide->interface->methodNumber; i++)
    {
        if(cm_StringCompare(provide->interface->methodNames[i], methodName, MAX_INTERFACE_METHOD_NAME_LENGTH) == 0)
        {
            return provideLoaded->indexesLoaded[itfProvide.collectionIndex][i].methodAddresses;
        }
    }

    return 0x0;
}

/**
 *
 */
PRIVATE t_uint8 compareItfName(const char* simplename, const char* complexname, int *collectionIndex) {
    int i;

    // Search if simplename is a prefix of complexname ??
    for(i = 0; simplename[i] != 0; i++) {
        if(simplename[i] != complexname[i])
            return 1;                       // NO
    }

    // YES
    if(complexname[i] == '[') {
        // This is a collection
        int value = 0;
        i++;
        if(complexname[i] < '0' || complexname[i] > '9') {
            return 1;
        }
        for(; complexname[i] >= '0' && complexname[i] <= '9'; i++) {
            value = value * 10 + (complexname[i] - '0');
        }
        if(complexname[i++] != ']')
            return 1;
        *collectionIndex = value;
    } else
        *collectionIndex = -1;

    if(complexname[i] != 0) {
        // Complexe name has not been fully parsed -> different name
        return 1;
    }

    return 0;
}


/**
 *
 */
PUBLIC t_cm_error cm_getProvidedInterface(const t_component_instance* server,
        const char* itfName,
        t_interface_provide_description *itfProvide){
    int i;

    for(i = 0; i < server->Template->provideNumber; i++)
    {
        int collectionIndex;
        if(compareItfName(server->Template->provides[i].name, itfName, &collectionIndex) == 0)
        {
            t_interface_provide *provide = &server->Template->provides[i];
            if(collectionIndex >= 0)
            {
                if(! (provide->provideTypes & COLLECTION_PROVIDE)) {
                    ERROR("CM_NO_SUCH_PROVIDED_INTERFACE(%s, %s)\n",
                            server->pathname, itfName, 0, 0, 0, 0);
                    goto out;
                }
                if(collectionIndex >= provide->collectionSize) {
                    ERROR("CM_NO_SUCH_PROVIDED_INTERFACE(%s, %s): out of range [0..%d[\n",
                            server->pathname, itfName, provide->collectionSize,
                            0, 0, 0);
                    goto out;
                }
            }
            else
            {
                if(provide->provideTypes & COLLECTION_PROVIDE) {
                    ERROR("CM_NO_SUCH_PROVIDED_INTERFACE(%s, %s): interface is a collection [0..%d[\n",
                            server->pathname, itfName, provide->collectionSize,
                            0, 0, 0);
                    goto out;
                }
                collectionIndex = 0;
            }
            itfProvide->provideIndex = i;
            itfProvide->server = server;
            itfProvide->collectionIndex = collectionIndex;
            itfProvide->origName = itfName;
            return CM_OK;
        }
    }

    ERROR("CM_NO_SUCH_PROVIDED_INTERFACE(%s, %s)\n", server->pathname, itfName, 0, 0, 0, 0);
out:
    itfProvide->provideIndex = 0;
    itfProvide->server = NULL;
    itfProvide->collectionIndex = 0;
    itfProvide->origName = NULL;
    return CM_NO_SUCH_PROVIDED_INTERFACE;
}

/**
 *
 */
t_cm_error cm_getRequiredInterface(const t_component_instance* client,
        const char* itfName,
        t_interface_require_description *itfRequire){
    int i;

    for(i = 0; i < client->Template->requireNumber; i++) {
        int collectionIndex;
        if(compareItfName(client->Template->requires[i].name, itfName, &collectionIndex) == 0) {
            t_interface_require *require = &client->Template->requires[i];
             if(collectionIndex >= 0) {
                if(! (require->requireTypes & COLLECTION_REQUIRE)) {
                    ERROR("CM_NO_SUCH_REQUIRED_INTERFACE(%s, %s)\n",
                        client->pathname, itfName, 0, 0, 0, 0);
                    return CM_NO_SUCH_REQUIRED_INTERFACE;
                }
                if(collectionIndex >= require->collectionSize) {
                    ERROR("CM_NO_SUCH_REQUIRED_INTERFACE(%s, %s): out of range [0..%d[\n",
                        client->pathname, itfName, require->collectionSize,
                        0, 0, 0);
                    return CM_NO_SUCH_REQUIRED_INTERFACE;
                }
            } else {
                if(require->requireTypes & COLLECTION_REQUIRE) {
                    ERROR("CM_NO_SUCH_REQUIRED_INTERFACE(%s, %s): interface is a collection [0..%d[\n",
                        client->pathname, itfName, require->collectionSize,
                        0, 0, 0);
                    return CM_NO_SUCH_REQUIRED_INTERFACE;
                }
                collectionIndex = 0;
            }
            itfRequire->client = client;
            itfRequire->requireIndex = i;
            itfRequire->collectionIndex = collectionIndex;
            itfRequire->origName = itfName;
            return CM_OK;
        }
    }

    ERROR("CM_NO_SUCH_REQUIRED_INTERFACE(%s, %s)\n", client->pathname, itfName, 0, 0, 0, 0);
    return CM_NO_SUCH_REQUIRED_INTERFACE;
}
