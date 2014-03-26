/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/elf/inc/bfd.h>
#include <cm/engine/elf/inc/mpcal.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/utils/inc/string.h>

t_cm_error cm_ELF_relocateSharedSegments(
        t_elfdescription *elfhandle,
        void                        *cbContext)
{
    return ELF64_relocateSegments(
        elfhandle,
        MEM_SHARABLE,
        cbContext);
}

t_cm_error cm_ELF_relocatePrivateSegments(
        t_elfdescription *elfhandle,
        void                        *cbContext)
{
    return ELF64_relocateSegments(
        elfhandle,
        MEM_PRIVATE,
        cbContext);
}

void cm_ELF_performRelocation(
        t_uint32 type,
        const char* symbol_name,
        t_uint32 symbol_addr,
        char* reloc_addr)
{
    MMDSP_performRelocation(
                        type,
                        symbol_name,
                        symbol_addr,
                        reloc_addr,
                        reloc_addr,
                        0xBEEF);

    OSAL_CleanDCache((t_uint32)reloc_addr, 8);
}

t_cm_error cm_ELF_GetMemory(
        t_elfdescription            *elf,
        t_tmp_elfdescription        *elftmp,
        t_uint32                    address,
        t_memory_purpose            purpose,
        t_memory_reference          *memory) {
    t_memory_id memId;

    for(memId = 0; memId < NUMBER_OF_MMDSP_MEMORY; memId++)
    {
        const t_elfmemory* mem = MMDSP_getMappingById(memId);

        if(mem->purpose == purpose &&    // Memory correspond
                elf->segments[mem->id].sumSize != 0 && // Segment allocated
                (elf->segments[mem->id].mpcAddr <= address) &&
                (address < elf->segments[mem->id].mpcAddr + elf->segments[mem->id].sumSize / mem->fileEntSize)) {
            memory->memory = mem;
            memory->offset = address - elf->segments[mem->id].mpcAddr;
            return CM_OK;
        }
    }

    ERROR("Memory %x,%d not found\n", address, purpose, 0, 0, 0, 0);
    return CM_INVALID_ELF_FILE;
}
