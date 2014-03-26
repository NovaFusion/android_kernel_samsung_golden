/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Elf common definition.
 */
#ifndef __INC_CM_ELF_COMMON_H
#define __INC_CM_ELF_COMMON_H

#include <cm/engine/component/inc/nmfheaderabi.h>
#include <cm/engine/elf/inc/elfabi.h>
#include <cm/engine/elf/inc/reloc.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/component/inc/description.h>
#include <cm/engine/utils/inc/string.h>


#define MAX_SEGMENT  20 // Just in order to not allocate them dynamically

struct XXElf;

/**
 * \brief Structure used as database of pushed component.
 */
typedef struct {
    t_instance_property instanceProperty;
    t_uint32            magicNumber;          //!< Magic Number
    t_dup_char          foundedTemplateName;
    t_uint32            minStackSize;           //!< Minimum stack size

    struct XXElf        *ELF;

    t_elfSegment segments[NUMBER_OF_MMDSP_MEMORY];

    t_bool              temporaryDescription;

    t_memory_reference          memoryForConstruct;
    t_memory_reference          memoryForStart;
    t_memory_reference          memoryForStop;
    t_memory_reference          memoryForDestroy;

    t_uint8                     requireNumber;          //!< Number of interface required by this template
    t_uint8                     attributeNumber;        //!< Number of attributes in this template
    t_uint8                     propertyNumber;         //!< Number of properties in this template
    t_uint8                     provideNumber;          //!< Number of interface provided by this template

    t_interface_require         *requires;              //!< Array of interface required by this template
    t_attribute                 *attributes;            //!< Array of attributes in this template
    t_property                  *properties;            //!< Array of properties in this template
    t_interface_provide         *provides;              //!< Array of interface provided by this template

} t_elfdescription;

/**
 * \brief Temporary structure used as database when pushing component.
 */
typedef struct
{
    const char              *elfdata;
    const char              *sectionData[50]; // YES it must be dynamic, but i'm tired.

    t_bool                  isExecutable;

    t_sint32                nmfSectionIndex;
    const void              *relaNmfSegment, *relaNmfSegmentEnd;
    const void              *relaNmfSegmentSymbols;
    const char              *relaNmfSegmentStrings;

    const t_elf_component_header*elfheader;


} t_tmp_elfdescription;


t_cm_error ELF64_LoadComponent(
        t_uint16                    e_machine,
        const char                  *elfdata,
        t_elfdescription            **elfhandlePtr,
        t_tmp_elfdescription        *elftmp);
t_cm_error ELF64_ComputeSegment(
        t_elfdescription            *elfhandle,
        t_tmp_elfdescription        *elftmp);

void ELF64_UnloadComponent(
        t_elfdescription            *elfhandle);

t_cm_error ELF64_loadSegment(
        t_elfdescription            *elfhandle,
        t_memory_property           property);
t_cm_error ELF64_relocateSegments(
        t_elfdescription            *elf,
        t_memory_property           property,
        void                        *cbContext);
t_cm_error ELF64_getRelocationMemory(
        t_elfdescription            *elfhandle,
        t_tmp_elfdescription        *elftmp,
        t_uint32                    offsetInNmf,
        t_memory_reference          *memory);

const t_elfmemory* MMDSP_getMappingById(t_memory_id memId);
const t_elfmemory* MMDSP_getMappingByName(const char* sectionName, t_instance_property property);
void MMDSP_serializeMemories(t_instance_property property,
        const t_elfmemory** codeMemory, const t_elfmemory** thisMemory);
void MMDSP_copySection(t_uint32 origAddr, t_uint32 remoteAddr, t_uint32 sizeInByte);
void MMDSP_bzeroSection(t_uint32 remoteAddr, t_uint32 sizeInByte);
void MMDSP_loadedSection(t_nmf_core_id coreId, t_memory_id memId, t_memory_handle handle);
void MMDSP_unloadedSection(t_nmf_core_id coreId, t_memory_id memId, t_memory_handle handle);

void MMDSP_copyCode(t_uint64 * remoteAddr64, const char* origAddr, int nb);
void MMDSP_copyData24(t_uint32 * remoteAddr32, const char* origAddr, int nb);
void MMDSP_copyData16(t_uint16 * remoteAddr16, const char* origAddr, int nb);

t_uint32 cm_resolvSymbol(
        void* context,
        t_uint32 type,
        t_dup_char symbolName,
        char* reloc_addr);


#endif
