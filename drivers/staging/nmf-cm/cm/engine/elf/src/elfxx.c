/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/elf/inc/mpcal.h>

#include <cm/engine/utils/inc/string.h>
#include <cm/engine/utils/inc/mem.h>


static t_uint32 max(t_uint32 a, t_uint32 b)
{
    return (a >= b) ? a : b;
}
/*
static t_uint32 min(t_uint32 a, t_uint32 b)
{
    return (a <= b) ? a : b;
}
*/

struct XXrelocation
{
    t_uint32            st_value;
    ElfXX_Half          st_shndx;
    Elf64_Sxword        r_addend;
    t_uint32            OffsetInElf;
    t_uint32            type;

    t_dup_char          symbol_name;    // Valid only if st_shndx == SHN_UNDEF
};

struct XXSection {
    ElfXX_Word      sh_type;        /* Section type */
    t_uint32        sh_size;        /* Section size in bytes */
    ElfXX_Word      sh_info;        /* Additional section information */
    ElfXX_Word      sh_link;        /* Link to another section */
    t_uint32        sh_addralign;   /* Some sections have address alignment constraints */
    t_uint32        sh_addr;        /* Section addr */
    ElfXX_Xword     sh_flags;       /* Section flags */

    const char      *data;
    t_uint32        trueDataSize;   /* Valid if different from sh_size */
    const char      *sectionName;

    t_uint32            offsetInSegment;
    const t_elfmemory   *meminfo;

    t_uint32                relocationNumber;
    struct XXrelocation     *relocations;
};

struct XXElf {
    t_uint32            e_shnum;
    struct XXSection                sectionss[1];
};

t_cm_error ELF64_LoadComponent(
        t_uint16                    e_machine,
        const char                  *elfdata,
        t_elfdescription            **elfhandlePtr,
        t_tmp_elfdescription        *elftmp)
{
    t_elfdescription *elfhandle;
    const ElfXX_Ehdr *header = (ElfXX_Ehdr*)elfdata;
    const ElfXX_Shdr *sections;
    const char *strings;
    struct XXElf* ELF;
    int i, nb;

    elftmp->elfdata = elfdata;

    /* Sanity check */
    if (swapHalf(header->e_machine) != e_machine)
    {
        ERROR("This is not a executable for such MPC\n", 0, 0, 0, 0, 0, 0);
        return CM_INVALID_ELF_FILE;
    }

    // Cache elf file informations
    nb = swapHalf(header->e_shnum);
    elftmp->isExecutable = (swapHalf(header->e_type) == ET_EXEC);

    elfhandle = (t_elfdescription*)OSAL_Alloc_Zero(
            sizeof(t_elfdescription) + sizeof(struct XXElf) + sizeof(struct XXSection) * (nb - 1));
    if(elfhandle == NULL)
        return CM_NO_MORE_MEMORY;

    ELF = elfhandle->ELF = (struct XXElf*)(elfhandle + 1);

    ELF->e_shnum = nb;

    sections = (ElfXX_Shdr*)&elfdata[swapXword(header->e_shoff)];
    // Compute and swap section infromation
    for(i = 0; i < ELF->e_shnum; i++)
    {
        ELF->sectionss[i].sh_type = swapWord(sections[i].sh_type);
        ELF->sectionss[i].sh_info = swapWord(sections[i].sh_info);
        ELF->sectionss[i].sh_link = swapWord(sections[i].sh_link);
        ELF->sectionss[i].sh_size = (t_uint32)swapXword(sections[i].sh_size);
        ELF->sectionss[i].sh_addralign = (t_uint32)swapXword(sections[i].sh_addralign);
        ELF->sectionss[i].sh_addr = (t_uint32)swapXword(sections[i].sh_addr);
        ELF->sectionss[i].sh_flags = swapXword(sections[i].sh_flags);

        elftmp->sectionData[i] = &elfdata[(t_uint32)swapXword(sections[i].sh_offset)];
    }

    /*
     * search nmf_segment
     */
    strings = elftmp->sectionData[swapHalf(header->e_shstrndx)];
    for(i = 0; i < ELF->e_shnum; i++)
    {
        ELF->sectionss[i].sectionName = &strings[swapWord(sections[i].sh_name)];

        // Found nmf_segment to see if it's
        if(cm_StringCompare("nmf_segment", ELF->sectionss[i].sectionName, 11) == 0) {
            elftmp->nmfSectionIndex = i;
            elftmp->elfheader = (const t_elf_component_header*)elftmp->sectionData[i];
        }
    }

    if(elftmp->nmfSectionIndex == 0)
    {
        ERROR("This is not a NMF component\n", 0, 0, 0, 0, 0, 0);
        goto invalid;
    }

    /*
     * Determine component type
     */
    elfhandle->magicNumber = swap32(elftmp->elfheader->magic);
    switch(elfhandle->magicNumber) {
    case MAGIC_COMPONENT:
        elfhandle->instanceProperty = MEM_FOR_MULTIINSTANCE;
        break;
    case MAGIC_SINGLETON:
    case MAGIC_FIRMWARE:
        elfhandle->instanceProperty = MEM_FOR_SINGLETON;
        break;
    }

    // Copy content
    for(i = 0; i < ELF->e_shnum; i++)
    {
        ELF->sectionss[i].meminfo = MMDSP_getMappingByName(
                ELF->sectionss[i].sectionName,
                elfhandle->instanceProperty);

        if(ELF->sectionss[i].meminfo != NULL)
            ELF->sectionss[i].trueDataSize = (ELF->sectionss[i].sh_size / ELF->sectionss[i].meminfo->fileEntSize) * ELF->sectionss[i].meminfo->memEntSize;

        if(ELF->sectionss[i].sh_size != 0 &&
                ELF->sectionss[i].sh_type == SHT_PROGBITS &&
                (ELF->sectionss[i].sh_flags & SHF_ALLOC) != 0)
        {
            const char* elfAddr = elftmp->sectionData[i];

            ELF->sectionss[i].data = OSAL_Alloc(ELF->sectionss[i].trueDataSize);
            if(ELF->sectionss[i].data == NULL)
                goto oom;

            if(ELF->sectionss[i].meminfo->purpose == MEM_CODE)
            {
                MMDSP_copyCode(
                        (t_uint64*)ELF->sectionss[i].data,
                        elfAddr,
                        ELF->sectionss[i].trueDataSize);
            }
            else if(ELF->sectionss[i].meminfo->purpose == MEM_DATA &&
                    // Always 3 for data ELF->sectionss[i].meminfo->fileEntSize == 3 &&
                    ELF->sectionss[i].meminfo->memEntSize == 4)
            {
                MMDSP_copyData24(
                        (t_uint32*)ELF->sectionss[i].data,
                        elfAddr,
                        ELF->sectionss[i].trueDataSize);
            }
            else if(ELF->sectionss[i].meminfo->purpose == MEM_DATA &&
                    // Always 3 for data ELF->sectionss[i].meminfo->fileEntSize == 3 &&
                    ELF->sectionss[i].meminfo->memEntSize == 2)
            {
                MMDSP_copyData16(
                        (t_uint16*)ELF->sectionss[i].data,
                        elfAddr,
                        ELF->sectionss[i].trueDataSize);
            }
            else
                CM_ASSERT(0);
        }
    }

    // Copy relocation
    // Loop on all relocation section
    for(i=0; i < ELF->e_shnum; i++)
    {
        int sh_info;

        // Does this section is a relocation table (only RELA supported)
        if((ELF->sectionss[i].sh_type != SHT_RELA) ||
                ELF->sectionss[i].sh_size == 0) continue;

        // Copy only relocation for loaded section
        sh_info = ELF->sectionss[i].sh_info;
        if(ELF->sectionss[sh_info].meminfo != NULL)
        {
            const ElfXX_Sym* symtab;
            const char* strtab;
            ElfXX_Rela* rel_start;
            int n;

            ELF->sectionss[sh_info].relocationNumber = ELF->sectionss[i].sh_size / sizeof(ElfXX_Rela);
            ELF->sectionss[sh_info].relocations = (struct XXrelocation*)OSAL_Alloc_Zero(sizeof(struct XXrelocation) * ELF->sectionss[sh_info].relocationNumber);
            if(ELF->sectionss[sh_info].relocations == NULL)
                goto oom;

            symtab = (ElfXX_Sym *)elftmp->sectionData[ELF->sectionss[i].sh_link];
            strtab = elftmp->sectionData[ELF->sectionss[ELF->sectionss[i].sh_link].sh_link];
            rel_start = (ElfXX_Rela*)elftmp->sectionData[i];
            for(n = 0; n < ELF->sectionss[sh_info].relocationNumber; n++, rel_start++)
            {
                struct XXrelocation* relocation = &ELF->sectionss[sh_info].relocations[n];
                ElfXX_Xword r_info = swapXword(rel_start->r_info);
                int strtab_index = ELFXX_R_SYM(r_info);
                const char* symbol_name = &strtab[swapWord(symtab[strtab_index].st_name)];

                relocation->st_shndx = swapHalf(symtab[strtab_index].st_shndx);
                relocation->st_value = (t_uint32)swapXword(symtab[strtab_index].st_value);
                relocation->r_addend = swapXword(rel_start->r_addend);
                relocation->OffsetInElf = (t_uint32)swapXword(rel_start->r_offset) / ELF->sectionss[sh_info].meminfo->fileEntSize;
                relocation->type = ELFXX_R_TYPE(r_info);

                switch(relocation->st_shndx) {
                case SHN_UNDEF:
                    relocation->symbol_name = cm_StringDuplicate(symbol_name + 1); /* Remove '_' prefix */
                    if(relocation->symbol_name == NULL)
                        goto oom;
                    break;
                case SHN_COMMON:
                    ERROR("SHN_COMMON not handle for %s\n", symbol_name, 0, 0, 0, 0, 0);
                    goto invalid;
                }
            }
        }
    }

    *elfhandlePtr = elfhandle;
    return CM_OK;
invalid:
    ELF64_UnloadComponent(elfhandle);
    return CM_INVALID_ELF_FILE;
oom:
    ELF64_UnloadComponent(elfhandle);
    return CM_NO_MORE_MEMORY;
}

t_cm_error ELF64_ComputeSegment(
        t_elfdescription            *elfhandle,
        t_tmp_elfdescription        *elftmp)
{
    struct XXElf* ELF = elfhandle->ELF;
    int i;

    for(i = 0; i < ELF->e_shnum; i++)
    {
        ELF->sectionss[i].offsetInSegment =  0xFFFFFFFF;

        if(ELF->sectionss[i].sh_type == SHT_PROGBITS || ELF->sectionss[i].sh_type == SHT_NOBITS) {
            // This is a loadable memory (memory size could be zero since we can have symbol on it)...
            const t_elfmemory* meminfo = ELF->sectionss[i].meminfo;

            if(meminfo != NULL) {
                // Which correspond to MPC memory

                if(elftmp->isExecutable)
                {
                    if(! elfhandle->segments[meminfo->id].sumSizeSetted)
                    {
                        CM_ASSERT(ELF->sectionss[i].sh_addr >= meminfo->startAddr * meminfo->fileEntSize);

                        elfhandle->segments[meminfo->id].sumSizeSetted = TRUE;
                        elfhandle->segments[meminfo->id].sumSize = ELF->sectionss[i].sh_addr - meminfo->startAddr * meminfo->fileEntSize;
                    }
                    else
                        CM_ASSERT(elfhandle->segments[meminfo->id].sumSize == ELF->sectionss[i].sh_addr - meminfo->startAddr * meminfo->fileEntSize);
                }
                else
                {
                    while(elfhandle->segments[meminfo->id].sumSize % ELF->sectionss[i].sh_addralign != 0)
                        elfhandle->segments[meminfo->id].sumSize++;
                }

                elfhandle->segments[meminfo->id].maxAlign = max(elfhandle->segments[meminfo->id].maxAlign, ELF->sectionss[i].sh_addralign);
                ELF->sectionss[i].offsetInSegment = elfhandle->segments[meminfo->id].sumSize / meminfo->fileEntSize;
                elfhandle->segments[meminfo->id].sumSize += ELF->sectionss[i].sh_size;
            }
         } else if(ELF->sectionss[i].sh_type == SHT_RELA && ELF->sectionss[i].sh_info == elftmp->nmfSectionIndex) {
             int secsym = ELF->sectionss[i].sh_link;
             elftmp->relaNmfSegment = (ElfXX_Rela*)elftmp->sectionData[i];
             elftmp->relaNmfSegmentEnd = (ElfXX_Rela*)((t_uint32)elftmp->relaNmfSegment + ELF->sectionss[i].sh_size);
             elftmp->relaNmfSegmentSymbols = (ElfXX_Sym*)elftmp->sectionData[secsym];
             elftmp->relaNmfSegmentStrings = elftmp->sectionData[ELF->sectionss[secsym].sh_link];
         }
    }

    return CM_OK;
}

void ELF64_UnloadComponent(
        t_elfdescription            *elfhandle)
{
    struct XXElf* ELF = elfhandle->ELF;
    int i, n;

    for(i = 0; i < ELF->e_shnum; i++)
    {
        if(ELF->sectionss[i].relocations != NULL)
        {
            for(n = 0; n < ELF->sectionss[i].relocationNumber; n++)
                cm_StringRelease(ELF->sectionss[i].relocations[n].symbol_name);
            OSAL_Free(ELF->sectionss[i].relocations);
        }

        OSAL_Free((void*)ELF->sectionss[i].data);
    }
    OSAL_Free(elfhandle);
}

t_cm_error ELF64_loadSegment(
        t_elfdescription            *elfhandle,
        t_memory_property           property)
{
    struct XXElf* ELF = elfhandle->ELF;
    int i;

    /*
     * Copy ELF data in this segment
     */
    for(i = 0; i < ELF->e_shnum; i++)
    {
        const t_elfmemory* mapping = ELF->sectionss[i].meminfo;

        if(mapping == NULL)
            continue;
        if((! (ELF->sectionss[i].sh_flags & SHF_ALLOC)) || (ELF->sectionss[i].sh_size == 0))
            continue;

        // This is a loadable memory ...
        if(
                (mapping->property == property && elfhandle->instanceProperty != MEM_FOR_SINGLETON) ||
                (property == MEM_SHARABLE && elfhandle->instanceProperty == MEM_FOR_SINGLETON) )
        {
            // Where memory exist and waited share/private correspond
            t_uint32 remoteData = elfhandle->segments[mapping->id].hostAddr +
                    ELF->sectionss[i].offsetInSegment * mapping->memEntSize;

            if(ELF->sectionss[i].sh_type != SHT_NOBITS)
            {
                LOG_INTERNAL(2, "loadSection(%s, 0x%x, 0x%x, 0x%08x)\n",
                        ELF->sectionss[i].sectionName, remoteData, ELF->sectionss[i].trueDataSize,
                        (t_uint32)ELF->sectionss[i].data, 0, 0);

                MMDSP_copySection((t_uint32)ELF->sectionss[i].data, remoteData, ELF->sectionss[i].trueDataSize);
            }
            else
            {
                LOG_INTERNAL(2, "bzeroSection(%s, 0x%x, 0x%x)\n",
                        ELF->sectionss[i].sectionName, remoteData, ELF->sectionss[i].trueDataSize, 0, 0, 0);

                MMDSP_bzeroSection(remoteData, ELF->sectionss[i].trueDataSize);
            }
        }
    }

    return CM_OK;
}



static const t_elfmemory* getSectionAddress(
        t_elfdescription *elfhandle,
        t_uint32 sectionIdx,
        t_uint32 *sectionOffset,
        t_cm_logical_address *sectionAddr) {
    struct XXElf* ELF = elfhandle->ELF;
    const t_elfmemory* mapping = ELF->sectionss[sectionIdx].meminfo;

    if(mapping != NULL) {
        *sectionOffset = (elfhandle->segments[mapping->id].mpcAddr +
                ELF->sectionss[sectionIdx].offsetInSegment);

        *sectionAddr = (t_cm_logical_address)(elfhandle->segments[mapping->id].hostAddr +
                ELF->sectionss[sectionIdx].offsetInSegment * mapping->memEntSize);
    }

    return mapping;
}

static t_uint32 getSymbolAddress(
        t_elfdescription            *elfhandle,
        t_uint32                    symbolSectionIdx,
        t_uint32                    symbolOffet) {
    struct XXElf* ELF = elfhandle->ELF;
    const t_elfmemory* mapping = ELF->sectionss[symbolSectionIdx].meminfo;

    if(mapping == NULL)
        return 0xFFFFFFFF;
    // CM_ASSERT(elfhandle->segments[mapping->id].sumSize != 0);
    // CM_ASSERT(elfhandle->sections[symbolSectionIdx].offsetInSegment != 0xFFFFFFFF);

    return elfhandle->segments[mapping->id].mpcAddr +
        ELF->sectionss[symbolSectionIdx].offsetInSegment +
        symbolOffet;
}

#if 0
t_bool ELFXX_getSymbolLocation(
        const t_mpcal_memory    *mpcalmemory,
        t_elfdescription        *elf,
        char                    *symbolName,
        const t_elfmemory       **memory,
        t_uint32                *offset) {
    const ElfXX_Ehdr *header = (ElfXX_Ehdr*)elf->elfdata;
    const ElfXX_Shdr *sections = (ElfXX_Shdr*)&elf->elfdata[swapXword(header->e_shoff)];
    const char *strings = &elf->elfdata[swapXword(sections[swapHalf(header->e_shstrndx)].sh_offset)];
    int len = cm_StringLength(symbolName, 256); // TO BE FIXED
    int i;

    for(i = 0; i < ELF->e_shnum; i++)
    {
        ElfXX_Sym* symtab;
        const char* strtab;
        unsigned int size, j;

        if(ELF->sectionss[i].sh_type != SHT_SYMTAB && ELF->sectionss[i].sh_type != SHT_DYNSYM) continue;

        // Section is a symbol table
        symtab = (ElfXX_Sym*)&elf->elfdata[swapXword(sections[i].sh_offset)];
        strtab = &elf->elfdata[swapXword(sections[swapWord(sections[i].sh_link)].sh_offset)];
        size = ELF->sectionss[i].sh_size / (unsigned int)swapXword(sections[i].sh_entsize);

        for(j = 0; j < size; j++) {
            const char* foundName = &strtab[swapWord(symtab[j].st_name)];

            if(cm_StringCompare(symbolName, foundName, len) == 0) {
                if(swapHalf(symtab[j].st_shndx) != SHN_UNDEF) {
                    int sectionIdx = (int)swapHalf(symtab[j].st_shndx);
                    ElfXX_Xword sh_flags = swapXword(sections[sectionIdx].sh_flags);

                    *memory = mpcalmemory->getMappingByName(&strings[swapWord(sections[sectionIdx].sh_name)],
                            sh_flags & SHF_WRITE ? MEM_RW : (sh_flags & SHF_EXECINSTR ? MEM_X : MEM_RO));
                    *offset = (t_uint32)swapXword(symtab[j].st_value);

                    return 1;
                }
            }
        }
    }
    return 0;
}
#endif

t_cm_error ELF64_relocateSegments(
        t_elfdescription            *elfhandle,
        t_memory_property           property,
        void                        *cbContext) {
    struct XXElf* ELF = elfhandle->ELF;
    int sec, n;

    // Loop on all relocation section
    for(sec=0; sec < ELF->e_shnum; sec++)
    {
        t_cm_logical_address sectionAddr = 0;
        t_uint32 sectionOffset = 0;
        const t_elfmemory* mapping;

        if(ELF->sectionss[sec].relocations == NULL)
            continue;

        // Relocate only section in memory
        mapping = getSectionAddress(
                elfhandle,
                sec,
                &sectionOffset,
                &sectionAddr);
        if(mapping == NULL)
            continue;

        if(
                (mapping->property == property && elfhandle->instanceProperty != MEM_FOR_SINGLETON) ||
                (property == MEM_SHARABLE && elfhandle->instanceProperty == MEM_FOR_SINGLETON) )
        {
            LOG_INTERNAL(2, "relocSection(%s)\n", ELF->sectionss[sec].sectionName, 0, 0, 0, 0, 0);

            for(n = 0; n < ELF->sectionss[sec].relocationNumber; n++)
            {
                struct XXrelocation* relocation = &ELF->sectionss[sec].relocations[n];
                t_uint32 symbol_addr;
                char* relocAddr = (char*)(sectionAddr + relocation->OffsetInElf * mapping->memEntSize);

                switch(relocation->st_shndx) {
                case SHN_ABS:              // Absolute external reference
                    symbol_addr = relocation->st_value;
                    break;
                case SHN_UNDEF:            // External reference
                     // LOG_INTERNAL(0, "cm_resolvSymbol(%d, %s)\n", relocation->type, relocation->symbol_name, 0,0, 0, 0);
                    symbol_addr = cm_resolvSymbol(cbContext,
                            relocation->type,
                            relocation->symbol_name,
                            relocAddr);
                    if(symbol_addr == 0x0) { // Not defined symbol
                        ERROR("Symbol %s not found\n", relocation->symbol_name, 0, 0, 0, 0, 0);
                        return CM_INVALID_ELF_FILE;
                    } else if(symbol_addr == 0xFFFFFFFE) { // OOM
                        return CM_NO_MORE_MEMORY;
                    } else if(symbol_addr == 0xFFFFFFFF) { // Defined inside static binding
                        continue;
                    }
                    break;
                default:                   // Internal reference in loaded section
                    symbol_addr = getSymbolAddress(
                            elfhandle,
                            (t_uint32)relocation->st_shndx,
                            relocation->st_value);
                    if(symbol_addr == 0xFFFFFFFF) {
                        ERROR("Symbol in section %s+%d not loaded\n",
                                ELF->sectionss[relocation->st_shndx].sectionName,
                                relocation->st_value, 0, 0, 0, 0);
                        return CM_INVALID_ELF_FILE;
                    }
                    break;
                }

                symbol_addr += relocation->r_addend;

                MMDSP_performRelocation(
                        relocation->type,
                        relocation->symbol_name,
                        symbol_addr,
                        relocAddr,
                        ELF->sectionss[sec].data + relocation->OffsetInElf * mapping->memEntSize,
                        sectionOffset + relocation->OffsetInElf);
            }
        }
    }

    return CM_OK;
}

t_cm_error ELF64_getRelocationMemory(
        t_elfdescription            *elfhandle,
        t_tmp_elfdescription        *elftmp,
        t_uint32                    offsetInNmf,
        t_memory_reference          *memory) {
    struct XXElf* ELF = elfhandle->ELF;
    const ElfXX_Rela* rel_start;
    const ElfXX_Sym* relaNmfSegmentSymbols = (ElfXX_Sym*)elftmp->relaNmfSegmentSymbols;

    for(rel_start = (ElfXX_Rela*)elftmp->relaNmfSegment; rel_start < (ElfXX_Rela*)elftmp->relaNmfSegmentEnd; rel_start++)
    {
        if((t_uint32)swapXword(rel_start->r_offset) == offsetInNmf)
        {
            int strtab_index = ELFXX_R_SYM(swapXword(rel_start->r_info));
            int sectionIdx = (int)swapHalf(relaNmfSegmentSymbols[strtab_index].st_shndx);

            memory->memory = ELF->sectionss[sectionIdx].meminfo;

            if(memory->memory != NULL) {
                memory->offset = (
                        ELF->sectionss[sectionIdx].offsetInSegment +                           // Offset in Segment
                        (t_uint32)swapXword(relaNmfSegmentSymbols[strtab_index].st_value) +    // Offset in Elf Section
                        (t_uint32)swapXword(rel_start->r_addend));                                  // Addend

                return CM_OK;
            } else {
                const char* symbol_name = &elftmp->relaNmfSegmentStrings[swapWord(relaNmfSegmentSymbols[strtab_index].st_name)];
                ERROR("Symbol %s not found\n", symbol_name, 0, 0, 0, 0, 0);
                return CM_INVALID_ELF_FILE;
            }
        }
    }

    ERROR("Unknown relocation error\n", 0, 0, 0, 0, 0, 0);
    return CM_INVALID_ELF_FILE;
}
