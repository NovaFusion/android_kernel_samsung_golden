/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include "../inc/trace.h"
#include "../inc/xtitrace.h"
#include <inc/nmf-tracedescription.h>
#include <inc/nmf-limits.h>
#include <cm/engine/utils/inc/string.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

t_bool cm_trace_enabled = FALSE;

/*
 * STM message dump
 */
#define HEADER(t, s) ((t) | (s << 16))

static void writeN(struct t_nmfTraceChannelHeader* header)
{
    t_uint64* data = (t_uint64*)header;
    t_uint64 *end = (t_uint64*)(((unsigned int)data) + header->traceSize - sizeof(t_uint64));

    while(data < end)
    {
        OSAL_Write64(CM_CHANNEL, 0,  *data++);
    }

    OSAL_Write64(CM_CHANNEL, 1, *data);
}

void cm_TRC_Dump(void)
{
    t_uint32 i;

    cm_TRC_traceReset();

    for (i=0; i<ComponentTable.idxMax; i++)
    {
	if (componentEntry(i) != NULL)
	    cm_TRC_traceLoadMap(TRACE_COMPONENT_COMMAND_ADD, componentEntry(i));
    }
}

void cm_TRC_traceReset(void)
{
    if(cm_trace_enabled)
    {
        struct t_nmfTraceReset   trace;

        trace.header.v = HEADER(TRACE_TYPE_RESET, sizeof(trace));

        trace.minorVersion = TRACE_MINOR_VERSION;
        trace.majorVersion = TRACE_MAJOR_VERSION;

        writeN((struct t_nmfTraceChannelHeader*)&trace);
    }
}

void cm_TRC_traceLoadMap(
        t_nmfTraceComponentCommandDescription command,
        const t_component_instance* component)
{
    if(cm_trace_enabled)
    {
        struct t_nmfTraceComponent   trace;

        /*
         * Generate instantiate trace
         */
        trace.header.v = HEADER(TRACE_TYPE_COMPONENT, sizeof(trace));

        trace.command = (t_uint16)command;
        trace.domainId = (t_uint16)component->Template->dspId + 1;
        trace.componentContext = (t_uint32)component->thisAddress;
        trace.componentUserContext = (t_uint32)component;
        cm_StringCopy((char*)trace.componentLocalName, component->pathname, MAX_COMPONENT_NAME_LENGTH);
        cm_StringCopy((char*)trace.componentTemplateName, component->Template->name, MAX_TEMPLATE_NAME_LENGTH);

        writeN((struct t_nmfTraceChannelHeader*)&trace);

        if(command == TRACE_COMPONENT_COMMAND_ADD)
        {
            struct t_nmfTraceMethod tracemethod;
            int i, j, k;

            /*
             * Generate method trace
             */
            tracemethod.header.v = HEADER(TRACE_TYPE_METHOD, sizeof(tracemethod));

            tracemethod.domainId = (t_uint16)component->Template->dspId + 1;
            tracemethod.componentContext = (t_uint32)component->thisAddress;

            for(i = 0; i < component->Template->provideNumber; i++)
            {
                t_interface_provide* provide = &component->Template->provides[i];
                t_interface_provide_loaded* provideLoaded = &component->Template->providesLoaded[i];

                for(j = 0; j < provide->collectionSize; j++)
                {
                    for(k = 0; k < provide->interface->methodNumber; k++)
                    {
                        tracemethod.methodId = provideLoaded->indexesLoaded[j][k].methodAddresses;

                        cm_StringCopy((char*)tracemethod.methodName, provide->interface->methodNames[k], MAX_INTERFACE_METHOD_NAME_LENGTH);

                        writeN((struct t_nmfTraceChannelHeader*)&tracemethod);
                    }
                }
            }
        }
    }
}

void cm_TRC_traceBinding(
        t_nmfTraceBindCommandDescription command,
        const t_component_instance* clientComponent, const t_component_instance* serverComponent,
        const char *requiredItfName, const char *providedItfName)
{
    if(cm_trace_enabled)
    {
        struct t_nmfTraceBind trace;

        trace.header.v = HEADER(TRACE_TYPE_BIND, sizeof(trace));

        trace.command = (t_uint16)command;

        if(clientComponent == ARM_TRACE_COMPONENT) // ARM
        {
            trace.clientDomainId = 0x1;
            trace.clientComponentContext = 0x0;
        }
        else
        {
            trace.clientDomainId = (t_uint16)clientComponent->Template->dspId + 1;
            trace.clientComponentContext = (t_uint32)clientComponent->thisAddress;
        }
        if(requiredItfName != NULL)
            cm_StringCopy((char*)trace.requiredItfName, requiredItfName, MAX_INTERFACE_NAME_LENGTH);
        else
            trace.requiredItfName[0] = 0;

        if(serverComponent == NULL)
        { // Unbind or VOID
            trace.serverDomainId = 0;
            trace.serverComponentContext = 0x0;
        }
        else if(serverComponent == ARM_TRACE_COMPONENT)
        { // ARM
            trace.serverDomainId = 0x1;
            trace.serverComponentContext = 0x0;
        }
        else
        {
            trace.serverDomainId = (t_uint16)serverComponent->Template->dspId + 1;
            trace.serverComponentContext = (t_uint32)serverComponent->thisAddress;
        }
        if(providedItfName != NULL)
            cm_StringCopy((char*)trace.providedItfName, providedItfName, MAX_INTERFACE_NAME_LENGTH);
        else
            trace.providedItfName[0] = 0;

        writeN((struct t_nmfTraceChannelHeader*)&trace);
    }
}

void cm_TRC_traceCommunication(
        t_nmfTraceCommunicationCommandDescription command,
        t_nmf_core_id coreId,
        t_nmf_core_id remoteCoreId)
{
    if(cm_trace_enabled)
    {
        struct t_nmfTraceCommunication trace;

        trace.header.v = HEADER(TRACE_TYPE_COMMUNICATION, sizeof(trace));

        trace.command = (t_uint16)command;
        trace.domainId = (t_uint16)coreId + 1;
        trace.remoteDomainId = (t_uint16)remoteCoreId + 1;

        writeN((struct t_nmfTraceChannelHeader*)&trace);
    }
}

void cm_TRC_traceMemAlloc(t_nmfTraceAllocatorCommandDescription command, t_uint8 allocId, t_uint32 memorySize, const char *allocname)
{
    if(cm_trace_enabled)
    {
        struct t_nmfTraceAllocator trace;

        trace.header.v = HEADER(TRACE_TYPE_ALLOCATOR, sizeof(trace));

        trace.command = (t_uint16)command;
        trace.allocId = (t_uint16)allocId;
        trace.size = memorySize;
        cm_StringCopy((char*)trace.name, allocname, sizeof(trace.name));

        writeN((struct t_nmfTraceChannelHeader*)&trace);
    }
}

void cm_TRC_traceMem(t_nmfTraceAllocCommandDescription command, t_uint8 allocId, t_uint32 startAddress, t_uint32 memorySize)
{
    if(cm_trace_enabled)
    {
        struct t_nmfTraceAlloc trace;

        trace.header.v = HEADER(TRACE_TYPE_ALLOC, sizeof(trace));

        trace.command = (t_uint16)command;
        trace.allocId = (t_uint16)allocId;
        trace.offset = startAddress;
        trace.size = memorySize;

        writeN((struct t_nmfTraceChannelHeader*)&trace);
    }
}

