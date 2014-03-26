/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/elf/inc/elfapi.h>
#include <cm/engine/elf/inc/mpcal.h>
#include <cm/inc/cm_def.h>

//#include <cm/engine/component/inc/introspection.h>

#include <cm/engine/utils/inc/mem.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/swap.h>
#include <cm/engine/utils/inc/string.h>

static void* getElfHeaderReference(t_tmp_elfdescription *elftmp, void* hdrref)
{
    if(hdrref != NULL)
        return (void*)((int)swap32((t_uint32)hdrref) + (int)elftmp->elfheader);
    else
        return NULL;
}

static t_dup_char copyElfString(t_tmp_elfdescription *elftmp, char* idx)
{
    return cm_StringDuplicate((char*)getElfHeaderReference(elftmp, (void*)idx));
}

static t_cm_error getMemoryOffset(
        t_elfdescription            *elfhandle,
        t_tmp_elfdescription        *elftmp,
        t_memory_purpose            purpose,
        const t_uint32              *addressInNmf,
        t_memory_reference          *memory) {

    if(elftmp->isExecutable) {
        return cm_ELF_GetMemory(elfhandle, elftmp,
                swap32(*addressInNmf),
                purpose,
                memory);
    } else {
        return ELF64_getRelocationMemory(elfhandle, elftmp,
                (t_uint32)addressInNmf - (t_uint32)elftmp->elfheader,
                memory);
    }
}

static t_cm_error getAdressForExecutableOffsetElsewhere(
        t_elfdescription                *elfhandle,
        t_tmp_elfdescription            *elftmp,
        const t_uint32                  *addressInNmf,
        t_memory_reference              *memory) {
    t_uint32 address;

    address = swap32(*addressInNmf);
    if(address == 0xFFFFFFFF)
    {
        memory->offset = 0x0;
        memory->memory = NULL;
        return CM_OK;
    }

    if(elftmp->isExecutable)
    {
        memory->offset = address;
        memory->memory = NULL;
        return CM_OK;
    }

    // Error log in elfhandle by previous call will be check in loadTemplate
    return ELF64_getRelocationMemory(elfhandle, elftmp,
            (t_uint32)addressInNmf - (t_uint32)elftmp->elfheader,
            memory);
}

/*
 * Interface Management
 */
static t_interface_description* interfaceList = NULL;

static t_interface_description* getInterfaceDescription(t_tmp_elfdescription *elftmp, t_elf_interface_description* elfitf) {
    t_dup_char itfType;
    t_interface_description* itf;
    int i;

    itfType = copyElfString(elftmp, elfitf->type);
    if(itfType == NULL)
        return NULL;

    // Search if interfane already loaded
    for(itf = interfaceList; itf != NULL; itf = itf->next) {
        if(itf->type == itfType) {
	    if (itf->methodNumber != elfitf->methodNumber) {
		    ERROR("When loading component template %s:\n\tNumber of methods in interface type %s\n\tdiffers from previous declaration: was %d, found %d\n",
			  getElfHeaderReference(elftmp, (void*)elftmp->elfheader->templateName), itfType, itf->methodNumber, elfitf->methodNumber, 0, 0);
		    //Do not fail for now for compatibility reason
		    //goto out_itf_type;
	    }
	    if (cmIntensiveCheckState) {
		    for(i = 0; i < itf->methodNumber; i++) {
			    if (cm_StringCompare(itf->methodNames[i], getElfHeaderReference(elftmp, (void*)elfitf->methodNames[i]), MAX_INTERNAL_STRING_LENGTH) != 0) {
				    ERROR("When loading component template %s:\n"
					  "\tName of method number %d in interface type %s\n"
					  "\tdiffers from previous declaration: previous name was %s, new name found is %s\n",
					  getElfHeaderReference(elftmp, (void*)elftmp->elfheader->templateName), i,
					  itfType, itf->methodNames[i],
					  getElfHeaderReference(elftmp, (void*)elfitf->methodNames[i]), 0);
				    //Do not fail for now for compatibility reason
				    //goto out_itf_type;
			    }
		    }
	    }
            itf->referenceCounter++;
            cm_StringRelease(itfType);
            return itf;
        }
    }

    // Create a new interface if not exists
    itf = (t_interface_description*)OSAL_Alloc_Zero(sizeof(t_interface_description) + sizeof(t_dup_char) * (elfitf->methodNumber - 1));
    if(itf == NULL)
        goto out_itf_type;
    itf->referenceCounter = 1;
    itf->type = itfType;
    itf->methodNumber = elfitf->methodNumber;
    for(i = 0; i < itf->methodNumber; i++) {
        itf->methodNames[i] = copyElfString(elftmp, elfitf->methodNames[i]);
        if(itf->methodNames[i] == NULL)
            goto out_method;
    }

    // Put it in Top
    itf->next = interfaceList;
    interfaceList = itf;

    return itf;

out_method:
    for(i = 0; i < itf->methodNumber; i++)
        cm_StringRelease(itf->methodNames[i]);
    OSAL_Free(itf);
out_itf_type:
    cm_StringRelease(itfType);
    return NULL;
}

static void releaseInterfaceDescription(t_interface_description* itf) {
    if(itf == NULL)
        return;

    if(--itf->referenceCounter == 0) {
        int i;

        // Remove it from list
        if(interfaceList == itf) {
            interfaceList = interfaceList->next;
        } else {
            t_interface_description* prev = interfaceList;
            while(prev->next != itf)
                prev = prev->next;
            prev->next = itf->next;
        }

        // Destroy interface description
        for(i = 0; i < itf->methodNumber; i++) {
            cm_StringRelease(itf->methodNames[i]);
        }
        cm_StringRelease(itf->type);
        OSAL_Free(itf);
    }
}


t_cm_error cm_ELF_CheckFile(
        const char              *elfdata,
        t_bool                  temporaryDescription,
        t_elfdescription        **elfhandlePtr)
{
    t_elfdescription        *elfhandle;
    t_tmp_elfdescription    elftmp = {0,};
    t_cm_error error;
    t_uint32 version;
    t_uint32 compatibleVersion;
    int i, j, k;

    /*
     * Sanity check
     */
    if (elfdata[EI_MAG0] != ELFMAG0 ||
            elfdata[EI_MAG1] != ELFMAG1 ||
            elfdata[EI_MAG2] != ELFMAG2 ||
            elfdata[EI_MAG3] != ELFMAG3 ||
            elfdata[EI_CLASS] != ELFCLASS64)
    {
        ERROR("CM_INVALID_ELF_FILE: component file is not a MMDSP ELF file\n", 0, 0, 0, 0, 0, 0);
        return CM_INVALID_ELF_FILE;
    }

    /*
     * Create elf data
     */
    if((error = ELF64_LoadComponent(EM_MMDSP_PLUS, elfdata, elfhandlePtr, &elftmp)) != CM_OK)
        return error;

    elfhandle = *elfhandlePtr;

    elfhandle->temporaryDescription = temporaryDescription;

    version = swap32(elftmp.elfheader->nmfVersion);

    compatibleVersion = (VERSION_MAJOR(version) == VERSION_MAJOR(NMF_VERSION));
    if(compatibleVersion)
    {
        switch(VERSION_MINOR(NMF_VERSION))
        {
        case 10: // Compatible with 2.9, 2.10
            compatibleVersion =
                    (VERSION_MINOR(version) == 9) ||
                    (VERSION_MINOR(version) == 10);
            break;
        default: // Strict compatibility 2.x == 2.x
            compatibleVersion = (VERSION_MINOR(version) == VERSION_MINOR(NMF_VERSION));
        }
    }

    if(! compatibleVersion)
    {
        ERROR("CM_INVALID_ELF_FILE: incompatible version for Component %d.%d.x != CM:%d.%d.x\n",
                VERSION_MAJOR(version), VERSION_MINOR(version),
                VERSION_MAJOR(NMF_VERSION), VERSION_MINOR(NMF_VERSION), 0, 0);
        error = CM_INVALID_ELF_FILE;
        goto onerror;
    }


    /*
     * Commented since to many noise !!!!
    if(VERSION_PATCH(version) != VERSION_PATCH(NMF_VERSION))
    {
        WARNING("CM_INVALID_ELF_FILE: incompatible version, Component:%d.%d.%d != CM:%d.%d.%d\n",
                VERSION_MAJOR(version), VERSION_MINOR(version), VERSION_PATCH(version),
                VERSION_MAJOR(NMF_VERSION), VERSION_MINOR(NMF_VERSION), VERSION_PATCH(NMF_VERSION));
    }
     */

    if((error = ELF64_ComputeSegment(elfhandle, &elftmp)) != CM_OK)
        goto onerror;

    //
    elfhandle->foundedTemplateName =  copyElfString(&elftmp, elftmp.elfheader->templateName);
    if(elfhandle->foundedTemplateName == NULL)
        goto oom;
    elfhandle->minStackSize = swap32(elftmp.elfheader->minStackSize);

    // Get Life-cycle memory
    if((error = getAdressForExecutableOffsetElsewhere(elfhandle, &elftmp, &elftmp.elfheader->LCCConstruct, &elfhandle->memoryForConstruct)) != CM_OK)
        goto onerror;
    if((error = getAdressForExecutableOffsetElsewhere(elfhandle, &elftmp, &elftmp.elfheader->LCCStart, &elfhandle->memoryForStart)) != CM_OK)
        goto onerror;
    if((error = getAdressForExecutableOffsetElsewhere(elfhandle, &elftmp, &elftmp.elfheader->LCCStop, &elfhandle->memoryForStop)) != CM_OK)
        goto onerror;
    if((error = getAdressForExecutableOffsetElsewhere(elfhandle, &elftmp, &elftmp.elfheader->LCCDestroy, &elfhandle->memoryForDestroy)) != CM_OK)
        goto onerror;

    // Copy attributes information
    elfhandle->attributeNumber = swap32(elftmp.elfheader->attributeNumber);
    if(elfhandle->attributeNumber > 0)
    {
        elfhandle->attributes =
                (t_attribute*)OSAL_Alloc_Zero(sizeof(t_attribute) * elfhandle->attributeNumber);
        if(elfhandle->attributes == NULL)
            goto oom;

        if(elfhandle->attributeNumber >  0)
        {
            t_elf_attribute *attributes = (t_elf_attribute*)getElfHeaderReference(&elftmp, (void*)elftmp.elfheader->attributes);

            for(i = 0; i < elfhandle->attributeNumber; i++)
            {
                elfhandle->attributes[i].name = copyElfString(&elftmp, attributes[i].name);
                if(elfhandle->attributes[i].name == NULL)
                    goto oom;

                if((error = getMemoryOffset(elfhandle, &elftmp,
                        MEM_DATA,
                        &attributes[i].symbols,
                        &elfhandle->attributes[i].memory)) != CM_OK)
                        goto onerror;
                LOG_INTERNAL(2, "  attribute %s mem=%s offset=%x\n",
                        elfhandle->attributes[i].name,
                        elfhandle->attributes[i].memory.memory->memoryName,
                        elfhandle->attributes[i].memory.offset,
                        0, 0, 0);
            }
        }
    }

    // Copy properties information
    elfhandle->propertyNumber = swap32(elftmp.elfheader->propertyNumber);
    if(elfhandle->propertyNumber > 0)
    {
        elfhandle->properties =
                (t_property*)OSAL_Alloc_Zero(sizeof(t_property) * elfhandle->propertyNumber);
        if(elfhandle->properties == NULL)
            goto oom;

        if(elfhandle->propertyNumber > 0)
        {
            t_elf_property *properties = (t_elf_property*)getElfHeaderReference(&elftmp, (void*)elftmp.elfheader->properties);

            for(i = 0; i < elfhandle->propertyNumber; i++)
            {
                elfhandle->properties[i].name = copyElfString(&elftmp, properties[i].name);
                if(elfhandle->properties[i].name == NULL)
                    goto oom;

                elfhandle->properties[i].value = copyElfString(&elftmp, properties[i].value);
                if(elfhandle->properties[i].value == NULL)
                    goto oom;

                LOG_INTERNAL(3, "  property %s = %s\n",
                        elfhandle->properties[i].name,
                        elfhandle->properties[i].value,
                        0, 0, 0, 0);
            }
        }
    }

    // Copy requires information
    elfhandle->requireNumber = swap32(elftmp.elfheader->requireNumber);
    if(elfhandle->requireNumber > 0)
    {
        char *ref = getElfHeaderReference(&elftmp, (void*)elftmp.elfheader->requires);

        elfhandle->requires = (t_interface_require*)OSAL_Alloc_Zero(sizeof(t_interface_require) * elfhandle->requireNumber);
        if(elfhandle->requires == NULL)
            goto oom;

        for(i = 0; i < elfhandle->requireNumber; i++)
        {
            t_elf_required_interface *require = (t_elf_required_interface*)ref;
            t_elf_interface_description *interface = (t_elf_interface_description*)getElfHeaderReference(&elftmp, (void*)require->interface);

            elfhandle->requires[i].name = copyElfString(&elftmp, require->name);
            if(elfhandle->requires[i].name == NULL)
                goto oom;

            elfhandle->requires[i].requireTypes = require->requireTypes;
            elfhandle->requires[i].collectionSize = require->collectionSize;
            elfhandle->requires[i].interface = getInterfaceDescription(&elftmp, interface);
            if(elfhandle->requires[i].interface == NULL)
                goto oom;

            LOG_INTERNAL(2, "  require %s <%s> %x\n",
                    elfhandle->requires[i].name,
                    elfhandle->requires[i].interface->type,
                    elfhandle->requires[i].requireTypes, 0, 0, 0);
            CM_ASSERT(elfhandle->requires[i].collectionSize != 0);

            ref = (char*)&require->indexes[0];

            if((elfhandle->requires[i].requireTypes & VIRTUAL_REQUIRE) == 0 &&
                    (elfhandle->requires[i].requireTypes & STATIC_REQUIRE) == 0)
            {
                elfhandle->requires[i].indexes =
                        (t_interface_require_index*)OSAL_Alloc_Zero(sizeof(t_interface_require_index) * elfhandle->requires[i].collectionSize);
                if(elfhandle->requires[i].indexes == NULL)
                    goto oom;

                for(j = 0; j < elfhandle->requires[i].collectionSize; j++)
                {
                    t_elf_interface_require_index* index = (t_elf_interface_require_index*)ref;

                    elfhandle->requires[i].indexes[j].numberOfClient = swap32(index->numberOfClient);
                    if(elfhandle->requires[i].indexes[j].numberOfClient != 0)
                    {
                        elfhandle->requires[i].indexes[j].memories =
                                (t_memory_reference*)OSAL_Alloc(sizeof(t_memory_reference) *  elfhandle->requires[i].indexes[j].numberOfClient);
                        if(elfhandle->requires[i].indexes[j].memories == NULL)
                            goto oom;

                        for(k = 0; k < elfhandle->requires[i].indexes[j].numberOfClient; k++) {
                            if((error = getMemoryOffset(elfhandle,&elftmp,
                                    MEM_DATA,
                                    &index->symbols[k],
                                    &elfhandle->requires[i].indexes[j].memories[k])) != CM_OK)
                                goto onerror;
                            LOG_INTERNAL(2, "    [%d, %d] mem=%s offset=%x\n",
                                    j, k,
                                    elfhandle->requires[i].indexes[j].memories[k].memory->memoryName,
                                    elfhandle->requires[i].indexes[j].memories[k].offset,
                                    0, 0);
                        }
                    }

                    ref += sizeof(index->numberOfClient) + elfhandle->requires[i].indexes[j].numberOfClient * sizeof(index->symbols[0]);
                }
            }
        }
    }

    // Copy provides informations
    elfhandle->provideNumber = swap32(elftmp.elfheader->provideNumber);
    if(elfhandle->provideNumber != 0)
    {
        elfhandle->provides =
                (t_interface_provide*)OSAL_Alloc_Zero(sizeof(t_interface_provide) * elfhandle->provideNumber);
        if(elfhandle->provides == NULL)
            goto oom;

        if(elfhandle->provideNumber > 0)
        {
            char *ref = getElfHeaderReference(&elftmp, (void*)elftmp.elfheader->provides);

            for(i = 0; i < elfhandle->provideNumber; i++)
            {
                t_elf_provided_interface *provide = (t_elf_provided_interface*)ref;
                t_elf_interface_description *interface = (t_elf_interface_description*)getElfHeaderReference(&elftmp, (void*)provide->interface);

                elfhandle->provides[i].name = copyElfString(&elftmp, provide->name);
                if(elfhandle->provides[i].name == NULL)
                    goto oom;

                elfhandle->provides[i].provideTypes = provide->provideTypes;
                elfhandle->provides[i].interruptLine = provide->interruptLine;
                elfhandle->provides[i].collectionSize = provide->collectionSize;
                elfhandle->provides[i].interface = getInterfaceDescription(&elftmp, interface);
                if(elfhandle->provides[i].interface == NULL)
                    goto oom;

                LOG_INTERNAL(2, "  provide %s <%s>\n",
                        elfhandle->provides[i].name,
                        elfhandle->provides[i].interface->type,
                        0,0, 0, 0);
                CM_ASSERT(elfhandle->provides[i].collectionSize != 0);

                ref = (char*)&provide->methodSymbols[0];

                {
                    t_uint32 *methodSymbols = (t_uint32*)ref;

                    elfhandle->provides[i].indexes = (t_interface_provide_index**)OSAL_Alloc_Zero(
                            sizeof(t_interface_provide_index*) * elfhandle->provides[i].collectionSize);
                    if(elfhandle->provides[i].indexes == NULL)
                        goto oom;

                    if(elfhandle->provides[i].interface->methodNumber != 0)
                    {
                        for(j = 0; j < elfhandle->provides[i].collectionSize; j++)
                        {
                            elfhandle->provides[i].indexes[j] = (t_interface_provide_index*)OSAL_Alloc(
                                    sizeof(t_interface_provide_index) * elfhandle->provides[i].interface->methodNumber);
                            if(elfhandle->provides[i].indexes[j] == NULL)
                                goto oom;

                            for(k = 0; k < elfhandle->provides[i].interface->methodNumber; k++)
                            {
                                if((error = getAdressForExecutableOffsetElsewhere(elfhandle, &elftmp,
                                        methodSymbols++,
                                        &elfhandle->provides[i].indexes[j][k].memory)) != CM_OK)
                                        goto onerror;

                                if(elfhandle->provides[i].indexes[j][k].memory.memory != NULL)
                                    LOG_INTERNAL(2, "    [%d, %d] method '%s' mem=%s offset=%x\n",
                                        j, k,
                                        elfhandle->provides[i].interface->methodNames[k],
                                        elfhandle->provides[i].indexes[j][k].memory.memory->memoryName,
                                        elfhandle->provides[i].indexes[j][k].memory.offset,
                                        0);
                                else
                                    LOG_INTERNAL(2, "    [%d, %d] method '%s' address=%x\n",
                                        j, k,
                                        elfhandle->provides[i].interface->methodNames[k],
                                        elfhandle->provides[i].indexes[j][k].memory.offset,
                                        0, 0);
                            }
                        }
                    }

                    ref += elfhandle->provides[i].collectionSize * elfhandle->provides[i].interface->methodNumber * sizeof(methodSymbols[0]);
                }
            }
        }
    }

    return CM_OK;

oom:
    error = CM_NO_MORE_MEMORY;
onerror:
    cm_ELF_CloseFile(temporaryDescription, elfhandle);
    *elfhandlePtr = NULL;
    return error;
}

void cm_ELF_ReleaseDescription(
        t_uint32 requireNumber, t_interface_require *requires,
        t_uint32 attributeNumber, t_attribute *attributes,
        t_uint32 propertyNumber, t_property *properties,
        t_uint32 provideNumber, t_interface_provide *provides)
{
    int i, j;

    // Free provides (Number set when array allocated)
    if(provides != NULL)
    {
        for(i = 0; i < provideNumber; i++)
        {
            if(provides[i].indexes != NULL)
            {
                for(j = 0; j < provides[i].collectionSize; j++)
                {
                    OSAL_Free(provides[i].indexes[j]);
                }
                OSAL_Free(provides[i].indexes);
            }
            releaseInterfaceDescription(provides[i].interface);
            cm_StringRelease(provides[i].name);
        }
        OSAL_Free(provides);
    }

    // Free requires (Number set when array allocated)
    if(requires != NULL)
    {
        for(i = 0; i < requireNumber; i++)
        {
            if(requires[i].indexes != 0)
            {
                for(j = 0; j < requires[i].collectionSize; j++)
                {
                    OSAL_Free(requires[i].indexes[j].memories);
                }
                OSAL_Free(requires[i].indexes);
            }
            releaseInterfaceDescription(requires[i].interface);
            cm_StringRelease(requires[i].name);
        }
        OSAL_Free(requires);
    }

    // Free properties (Number set when array allocated)
    if(properties != NULL)
    {
        for(i = 0; i < propertyNumber; i++)
        {
            cm_StringRelease(properties[i].value);
            cm_StringRelease(properties[i].name);
        }
        OSAL_Free(properties);
    }

    // Free Attributes (Number set when array allocated)
    if(attributes != NULL)
    {
        for(i = 0; i < attributeNumber; i++)
        {
            cm_StringRelease(attributes[i].name);
        }
        OSAL_Free(attributes);
    }
}

void cm_ELF_CloseFile(
        t_bool                  temporaryDescription,
        t_elfdescription        *elfhandle)
{
    if(elfhandle == NULL)
        return;

    if(temporaryDescription && ! elfhandle->temporaryDescription)
        return;

    // Release description if not moved to template
    cm_ELF_ReleaseDescription(
            elfhandle->requireNumber, elfhandle->requires,
            elfhandle->attributeNumber, elfhandle->attributes,
            elfhandle->propertyNumber, elfhandle->properties,
            elfhandle->provideNumber, elfhandle->provides);

    cm_StringRelease(elfhandle->foundedTemplateName);

    ELF64_UnloadComponent(elfhandle);
}


static t_cm_error allocSegment(
        t_cm_domain_id          domainId,
        t_elfdescription        *elfhandle,
        t_memory_handle         memories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_property       property,
        t_bool                  isSingleton) {
    t_memory_id memId;
    const t_elfmemory           *thisMemory;            //!< Memory used to determine this
    const t_elfmemory           *codeMemory;            //!< Memory used to determine code

    MMDSP_serializeMemories(elfhandle->instanceProperty, &codeMemory, &thisMemory);

    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        const t_elfmemory* mapping;

        if(elfhandle->segments[memId].sumSize == 0x0)
            continue;

        mapping = MMDSP_getMappingById(memId);

        if(
                (mapping->property == property && elfhandle->instanceProperty != MEM_FOR_SINGLETON) ||
                (property == MEM_SHARABLE && elfhandle->instanceProperty == MEM_FOR_SINGLETON) )
        {
            // Allocate segment
            memories[memId] = cm_DM_Alloc(domainId, mapping->dspMemType,
                    elfhandle->segments[memId].sumSize / mapping->fileEntSize,
                    mapping->memAlignement, TRUE);

            if(memories[memId] == INVALID_MEMORY_HANDLE)
            {
                ERROR("CM_NO_MORE_MEMORY(%s): %x too big\n", mapping->memoryName, elfhandle->segments[memId].sumSize / mapping->fileEntSize, 0, 0, 0, 0);
                return CM_NO_MORE_MEMORY;
            }

            // Get reference in memory
            elfhandle->segments[memId].hostAddr = cm_DSP_GetHostLogicalAddress(memories[memId]);

            cm_DSP_GetDspAddress(memories[memId], &elfhandle->segments[memId].mpcAddr);

	    if (isSingleton)
		    cm_DM_SetDefaultDomain(memories[memId], cm_DM_GetDomainCoreId(domainId));

            // Log it
            LOG_INTERNAL(1, "\t%s%s: 0x%x..+0x%x (0x%x)\n",
                    mapping->memoryName,
                    (thisMemory == mapping) ? "(THIS)" : "",
                    elfhandle->segments[memId].mpcAddr,
                    elfhandle->segments[memId].sumSize / mapping->fileEntSize,
                    elfhandle->segments[memId].hostAddr, 0);
        }
        else if(property == MEM_PRIVATE) // Since we allocate private segment, if not allocate, it's a share one
        {
            // In order to allow further relocation based on cached address like mpcAddr & hostAddr,
            // initialize them also !

            // Get reference in memory
            elfhandle->segments[memId].hostAddr = cm_DSP_GetHostLogicalAddress(memories[memId]);

            cm_DSP_GetDspAddress(memories[memId], &elfhandle->segments[memId].mpcAddr);
        }
    }

    return CM_OK;
}

/*
 * Note: in case of error, part of memory could have been allocated and must be free by calling cm_DSPABI_FreeTemplate
 */
t_cm_error cm_ELF_LoadTemplate(
        t_cm_domain_id          domainId,
        t_elfdescription        *elfhandle,
        t_memory_handle         sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_bool                  isSingleton)
{
    t_cm_error error;

    if((error = allocSegment(domainId, elfhandle, sharedMemories, MEM_SHARABLE, isSingleton)) != CM_OK)
        return error;

    // Load each readonly segment
    if((error = ELF64_loadSegment(elfhandle, MEM_SHARABLE)) != CM_OK)
        return error;

    return CM_OK;
}

t_cm_error cm_ELF_LoadInstance(
        t_cm_domain_id          domainId,
        t_elfdescription        *elfhandle,
        t_memory_handle         sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_handle         privateMemories[NUMBER_OF_MMDSP_MEMORY],
        t_bool                  isSingleton)
{
    t_memory_id memId;
    t_cm_error error;

    // Erase whole memories to make free in case of error
    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        privateMemories[memId] = sharedMemories[memId];
    }

    if((error = allocSegment(domainId, elfhandle, privateMemories, MEM_PRIVATE, isSingleton)) != CM_OK)
        return error;

    // Load each writable memory
    if((error = ELF64_loadSegment(elfhandle, MEM_PRIVATE)) != CM_OK)
        return error;

    return CM_OK;
}

void cm_ELF_FlushTemplate(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY])
{
    t_memory_id memId;

    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        if(sharedMemories[memId] != INVALID_MEMORY_HANDLE)
            MMDSP_loadedSection(
                    coreId, memId,
                    sharedMemories[memId]);
    }
}

void cm_ELF_FlushInstance(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_handle privateMemories[NUMBER_OF_MMDSP_MEMORY])
{
    t_memory_id memId;

    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        if(privateMemories[memId] != INVALID_MEMORY_HANDLE && privateMemories[memId] != sharedMemories[memId])
            MMDSP_loadedSection(
                    coreId, memId,
                    privateMemories[memId]);
    }
}

void cm_ELF_FreeInstance(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_handle privateMemories[NUMBER_OF_MMDSP_MEMORY])
{
    t_memory_id memId;

    if(privateMemories == NULL)
        return;

    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        if(privateMemories[memId] != INVALID_MEMORY_HANDLE && privateMemories[memId] != sharedMemories[memId])
        {
            MMDSP_unloadedSection(coreId, memId, privateMemories[memId]);
            cm_DM_Free(privateMemories[memId], TRUE);
        }
    }
}

void cm_ELF_FreeTemplate(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY])
{
    t_memory_id memId;

    if(sharedMemories == NULL)
        return;

    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        if(sharedMemories[memId] != INVALID_MEMORY_HANDLE)
        {
            MMDSP_unloadedSection(coreId, memId, sharedMemories[memId]);
            cm_DM_Free(sharedMemories[memId], TRUE);
        }
    }
}
