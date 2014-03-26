/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/elf/inc/common.h>
#include <cm/engine/elf/inc/elfabi.h>

#include <cm/engine/utils/inc/swap.h>
#include <cm/engine/trace/inc/trace.h>

typedef Elf64_Half ElfXX_Half;
typedef Elf64_Word ElfXX_Word;
typedef Elf64_Addr ElfXX_Addr;
typedef Elf64_Off ElfXX_Off;

typedef Elf64_Xword ElfXX_Xword;

typedef Elf64_Ehdr ElfXX_Ehdr;
typedef Elf64_Shdr ElfXX_Shdr;
typedef Elf64_Sym ElfXX_Sym;
typedef Elf64_Rela ElfXX_Rela;

#undef ELFXX_R_SYM
#define ELFXX_R_SYM ELF64_R_SYM
#undef ELFXX_R_TYPE
#define ELFXX_R_TYPE ELF64_R_TYPE
#undef ELFXX_R_INFO
#define ELFXX_R_INFO ELF64_R_INFO

// TODO Here we assume big endian (MMDSP !)
static Elf64_Half swapHalf(Elf64_Half half)
{
    return (Elf64_Half)swap16(half);
}

static Elf64_Word swapWord(Elf64_Word word)
{
    return (Elf64_Word)swap32(word);
}

static Elf64_Xword swapXword(Elf64_Xword xword)
{
    return (Elf64_Xword)swap64(xword);
}

#include "elfxx.c"
