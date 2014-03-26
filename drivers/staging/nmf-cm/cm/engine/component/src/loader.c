/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/executive_engine_mgt/inc/executive_engine_mgt.h>
#include <cm/engine/component/inc/bind.h>

#include <cm/engine/utils/inc/string.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/convert.h>

void START(void);
void END(char*);

#undef NHASH
#define NHASH 79       //Use a prime number!
#define MULT 17

static t_component_template *templates[NB_CORE_IDS][NHASH];

static unsigned int templateHash(const char *str)
{
    unsigned int h = 0;
    for(; *str; str++)
        h = MULT * h + *str;
    return h % NHASH;
}

static void templateAdd(t_component_template *template)
{
    unsigned int h = templateHash(template->name);

    if(templates[template->dspId][h] != NULL)
        templates[template->dspId][h]->prev = template;
    template->next = templates[template->dspId][h];
    template->prev = NULL;
    templates[template->dspId][h] = template;
}

static void templateRemove(t_component_template *template)
{
    unsigned int h = templateHash(template->name);

    if(template->prev != NULL)
        template->prev->next = template->next;
    if(template->next != NULL)
        template->next->prev = template->prev;
    if(template == templates[template->dspId][h])
        templates[template->dspId][h] = template->next;
}


t_component_template* cm_lookupTemplate(t_nmf_core_id dspId, t_dup_char str)
{
    t_component_template *template;

    for(template = templates[dspId][templateHash(str)]; template != NULL; template = template->next)
    {
        if(str == template->name)
            return template;
    }

    return NULL;
}

t_bool cm_isComponentOnCoreId(t_nmf_core_id coreId) {
    t_uint32 i;

    for(i = 0; i < NHASH; i++)
    {
	if ((templates[coreId][i] != NULL)
	    && (templates[coreId][i]->classe != FIRMWARE)) // Skip firmware
            return TRUE;
    }

    return FALSE;
}


static t_dsp_address MemoryToDspAdress(t_component_template *template, t_memory_reference *memory)
{
    if(memory->memory == NULL)
        return (t_dsp_address)memory->offset;
    else
    {
        t_dsp_address address;

        cm_DSP_GetDspAddress(template->memories[memory->memory->id], &address);

        return (t_dsp_address)(address + memory->offset);
    }
}

/*
 * Method callback
 */
t_uint32 cm_resolvSymbol(
        void* context,
        t_uint32 type,
        t_dup_char symbolName,
        char* reloc_addr)
{
    t_component_template *template = (t_component_template*)context;
    t_component_instance* ee = cm_EEM_getExecutiveEngine(template->dspId)->instance;
    int i, j;

    // Search if this method is provided by EE and resolve it directly
    for(i = 0; i < ee->Template->provideNumber; i++)
    {
        t_interface_provide* provide = &ee->Template->provides[i];
        t_interface_provide_loaded* provideLoaded = &ee->Template->providesLoaded[i];

        for(j = 0; j < provide->interface->methodNumber; j++)
        {
            if(provide->interface->methodNames[j] == symbolName)
            {
                return provideLoaded->indexesLoaded[0][j].methodAddresses; // Here we assume no collection provided !!
            }
        }
    }

    // Lookup if the method is statically required, ands delay relocation when bind occur
    for(i = 0; i < template->requireNumber; i++)
    {
        if((template->requires[i].requireTypes & STATIC_REQUIRE) == 0)
            continue;

        for(j = 0; j < template->requires[i].interface->methodNumber; j++)
        {
            if(template->requires[i].interface->methodNames[j] == symbolName)
            {
                t_function_relocation* delayedRelocation = (t_function_relocation*)OSAL_Alloc(sizeof(t_function_relocation));
                if(delayedRelocation == NULL)
                    return 0xFFFFFFFE;

                delayedRelocation->type = type;
                delayedRelocation->symbol_name = cm_StringReference(symbolName);
                delayedRelocation->reloc_addr = reloc_addr;
                delayedRelocation->next = template->delayedRelocation;
                template->delayedRelocation = delayedRelocation;

                return 0xFFFFFFFF;
            }
        }
    }

    //Symbol not found
    return 0x0;
}

/*
 * Template Management
 */
t_cm_error cm_loadComponent(
        t_dup_char templateName,
        t_cm_domain_id domainId,
        t_elfdescription* elfhandle,
        t_component_template **reftemplate)
{
    t_nmf_core_id coreId = cm_DM_GetDomainCoreId(domainId);
    t_cm_error error;
    int i, j, k;

    /*
     * Allocate new component template if first instance
     */
    if(*reftemplate == NULL)
    {
        t_component_template *template;

        LOG_INTERNAL(1, "\n##### Load template %s on %s #####\n", templateName, cm_getDspName(coreId), 0, 0, 0, 0);

        /*
         * Sanity check
         */
        if(elfhandle->foundedTemplateName != templateName)
        {
            ERROR("CM_INVALID_ELF_FILE: template name %s != %s\n", templateName, elfhandle->foundedTemplateName, 0, 0, 0, 0);
            return CM_INVALID_ELF_FILE;
        }

        // Alloc & Reset variable in order to use unloadComponent either with partial constructed template
        *reftemplate = template = (t_component_template*)OSAL_Alloc_Zero(sizeof(t_component_template));
        if(template == NULL)
            return CM_NO_MORE_MEMORY;
        template->name = cm_StringReference(elfhandle->foundedTemplateName);

        // Get information from elfhandle
        template->descriptionAssociatedWithTemplate = elfhandle->temporaryDescription;
        template->requireNumber = elfhandle->requireNumber;
        template->requires = elfhandle->requires;
        template->attributeNumber = elfhandle->attributeNumber;
        template->attributes = elfhandle->attributes;
        template->propertyNumber = elfhandle->propertyNumber;
        template->properties = elfhandle->properties;
        template->provideNumber = elfhandle->provideNumber;
        template->provides = elfhandle->provides;
        if(template->descriptionAssociatedWithTemplate)
        {
            elfhandle->requires = NULL;
            elfhandle->attributes = NULL;
            elfhandle->properties = NULL;
            elfhandle->provides = NULL;
        }

        // Compute simple information
        template->numberOfInstance = 1;
        template->dspId = coreId;
        LOG_INTERNAL(3, "load<%x> = %s\n", (int)template, template->name, 0, 0, 0, 0);
        switch(elfhandle->magicNumber) {
        case MAGIC_COMPONENT:
            template->classe = COMPONENT;
            break;
        case MAGIC_SINGLETON:
            template->classe = SINGLETON;
            break;
        case MAGIC_FIRMWARE:
            template->classe = FIRMWARE;
            break;
        }
        template->minStackSize = elfhandle->minStackSize;

        /*
         * Load shared memory from file
         */
        // START();
        if((error = cm_ELF_LoadTemplate(domainId, elfhandle, template->memories, template->classe == SINGLETON)) != CM_OK)
            goto out;
        MMDSP_serializeMemories(elfhandle->instanceProperty, &template->codeMemory, &template->thisMemory);
        // END("cm_ELF_LoadTemplate");

        /*
         * Copy LCC functions information
         * Since MMDSP require Constructor & Destructor (for cache flush and debug purpose) to be called
         * either if not provided by user for allowing defered breakpoint, we use Void method if not provided.
         */
        template->LCCConstructAddress = MemoryToDspAdress(template, &elfhandle->memoryForConstruct);
        template->LCCStartAddress = MemoryToDspAdress(template, &elfhandle->memoryForStart);
        template->LCCStopAddress = MemoryToDspAdress(template, &elfhandle->memoryForStop);
        template->LCCDestroyAddress = MemoryToDspAdress(template, &elfhandle->memoryForDestroy);
        if(template->LCCConstructAddress == 0 && template->classe != FIRMWARE)
            template->LCCConstructAddress = cm_EEM_getExecutiveEngine(coreId)->voidAddr;

        // Compute provide methodIndex
        if(template->provideNumber != 0)
        {
            template->providesLoaded =
                    (t_interface_provide_loaded*)OSAL_Alloc_Zero(sizeof(t_interface_provide_loaded) * template->provideNumber);
            if(template->providesLoaded == NULL)
                goto oom;

            for(i = 0; i < template->provideNumber; i++)
            {
                template->providesLoaded[i].indexesLoaded = (t_interface_provide_index_loaded**)OSAL_Alloc_Zero(
                        sizeof(t_interface_provide_index_loaded*) * template->provides[i].collectionSize);
                if(template->providesLoaded[i].indexesLoaded == NULL)
                    goto oom;

                if(template->provides[i].interface->methodNumber != 0)
                {
                    for(j = 0; j < template->provides[i].collectionSize; j++)
                    {
                        template->providesLoaded[i].indexesLoaded[j] = (t_interface_provide_index_loaded*)OSAL_Alloc(
                                sizeof(t_interface_provide_index_loaded) * template->provides[i].interface->methodNumber);
                        if(template->providesLoaded[i].indexesLoaded[j] == NULL)
                            goto oom;

                        for(k = 0; k < template->provides[i].interface->methodNumber; k++)
                        {
                            template->providesLoaded[i].indexesLoaded[j][k].methodAddresses =
                                    MemoryToDspAdress(template, &template->provides[i].indexes[j][k].memory);

                            LOG_INTERNAL(2, "    [%d, %d] method '%s' @ %x\n",
                                    j, k, template->provides[i].interface->methodNames[k],
                                    template->providesLoaded[i].indexesLoaded[j][k].methodAddresses, 0, 0);
                        }

                    }
                }
            }
        }

        /*
         * TODO

        if((error = elfhandle->errorOccured) != CM_OK)
            goto out;
            */

      //  START();
        if(template->classe != FIRMWARE)
        {
            if((error = cm_ELF_relocateSharedSegments(
                    elfhandle,
                    template)) != CM_OK)
                goto out;
        }
       // END("cm_ELF_relocateSharedSegments");

        cm_ELF_FlushTemplate(coreId, template->memories);

        templateAdd(template);

        return CM_OK;
    oom:
        error = CM_NO_MORE_MEMORY;
    out:
        cm_unloadComponent(template);
        return error;
    }
    else
    {
        (*reftemplate)->numberOfInstance++;
    }

    return CM_OK;
}

PUBLIC t_cm_error cm_unloadComponent(
        t_component_template *template)
{
    /*
     * Destroy template if last instance
     */
    if(--template->numberOfInstance == 0) {
        t_function_relocation* reloc;

        LOG_INTERNAL(3, "unload<%s>\n", template->name, 0, 0, 0, 0, 0);

        templateRemove(template);

        // Free delayedRelocation
        reloc = template->delayedRelocation;
        while(reloc != NULL)
        {
            t_function_relocation *tofree = reloc;
            reloc = reloc->next;
            cm_StringRelease(tofree->symbol_name);
            OSAL_Free(tofree);
        }

        if(template->providesLoaded != NULL)
        {
            int i, j;

            for(i = 0; i < template->provideNumber; i++)
            {
                if(template->providesLoaded[i].indexesLoaded != NULL)
                {
                    for(j = 0; j < template->provides[i].collectionSize; j++)
                    {
                        OSAL_Free(template->providesLoaded[i].indexesLoaded[j]);
                    }
                    OSAL_Free(template->providesLoaded[i].indexesLoaded);
                }
            }

            OSAL_Free(template->providesLoaded);
        }

        if(template->descriptionAssociatedWithTemplate)
        {
            cm_ELF_ReleaseDescription(
                    template->requireNumber, template->requires,
                    template->attributeNumber, template->attributes,
                    template->propertyNumber, template->properties,
                    template->provideNumber, template->provides);
        }

        // Free shared memories
        cm_ELF_FreeTemplate(template->dspId, template->memories);

        cm_StringRelease(template->name);

        OSAL_Free(template);
    }

    return CM_OK;
}

