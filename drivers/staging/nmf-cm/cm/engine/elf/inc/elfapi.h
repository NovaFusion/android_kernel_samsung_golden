/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Elf loder internal methods.
 *
 * \defgroup ELFLOADER MMDSP ELF loader.
 */
#ifndef __INC_CM_ELFLOADER_H
#define __INC_CM_ELFLOADER_H

#include <cm/engine/elf/inc/common.h>

/*!
 * \internal
 * \brief ELF Parsing & checking
 * \ingroup ELFLOADER
 */
t_cm_error cm_ELF_CheckFile(
        const char              *elfdata,
        t_bool                  temporaryDescription,
        t_elfdescription        **elfhandlePtr);

void cm_ELF_ReleaseDescription(
        t_uint32 requireNumber, t_interface_require *requires,
        t_uint32 attributeNumber, t_attribute *attributes,
        t_uint32 propertyNumber, t_property *properties,
        t_uint32 provideNumber, t_interface_provide *provides);

/*!
 * \internal
 * \brief ELF closing
 * \ingroup ELFLOADER
 */
void cm_ELF_CloseFile(
        t_bool                  temporaryDescription,
        t_elfdescription        *elfhandle);

/*!
 * \internal
 * \brief Load a component template shared memories.
 *
 * \note In case of error, part of memory could have been allocated and must be free by calling cm_DSPABI_FreeTemplate.
 */
t_cm_error cm_ELF_LoadTemplate(
        t_cm_domain_id          domainId,
        t_elfdescription        *elfhandle,
        t_memory_handle         sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_bool                  isSingleton);

/*!
 * \internal
 * \brief Clean cache memory of a component template shared code.
 */
void cm_ELF_FlushTemplate(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY]);

void cm_ELF_FlushInstance(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_handle privateMemories[NUMBER_OF_MMDSP_MEMORY]);

/*!
 * \internal
 * \brief Load a component instance private memories.
 *
 * \note In case of error, part of memory could have been allocated and must be free by calling cm_DSPABI_FreeInstance.
 */
t_cm_error cm_ELF_LoadInstance(
        t_cm_domain_id          domainId,
        t_elfdescription        *elfhandle,
        t_memory_handle         sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_handle         privateMemories[NUMBER_OF_MMDSP_MEMORY],
        t_bool                  isSingleton);

void cm_ELF_FreeInstance(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY],
        t_memory_handle privateMemories[NUMBER_OF_MMDSP_MEMORY]);
void cm_ELF_FreeTemplate(
        t_nmf_core_id coreId,
        t_memory_handle sharedMemories[NUMBER_OF_MMDSP_MEMORY]);


t_cm_error cm_ELF_relocateSharedSegments(
        t_elfdescription *elfhandle,
        void                        *cbContext);
t_cm_error cm_ELF_relocatePrivateSegments(
        t_elfdescription *elfhandle,
        void                        *cbContext);
void cm_ELF_performRelocation(
        t_uint32                    type,
        const char                  *symbol_name,
        t_uint32                    symbol_addr,
        char                        *reloc_addr);
t_cm_error cm_ELF_GetMemory(
        t_elfdescription            *elf,
        t_tmp_elfdescription        *elftmp,
        t_uint32                    address,
        t_memory_purpose            purpose,
        t_memory_reference          *memory);


#include <cm/engine/component/inc/component_type.h>

t_cm_error cm_DSPABI_AddLoadMap(
        t_cm_domain_id domainId,
        const char* templateName,
        const char* localname,
        t_memory_handle *memories,
        void *componentHandle);
t_cm_error cm_DSPABI_RemoveLoadMap(
        t_cm_domain_id domainId,
        const char* templateName,
        t_memory_handle *memories,
        const char* localname,
        void *componentHandle);

#endif
