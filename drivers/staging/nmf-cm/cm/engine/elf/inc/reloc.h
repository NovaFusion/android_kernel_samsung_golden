/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Elf relocation.
 */
#ifndef __INC_CM_ELF_RELOC_H
#define __INC_CM_ELF_RELOC_H


void MMDSP_performRelocation(
        t_uint32 type,
        const char* symbol_name,
        t_uint32 symbol_addr,
        char* reloc_addr,
        const char* inPlaceAddr,
        t_uint32 reloc_offset);

/*
 *
 * Return:
 * 0x0 returned if symbol not found
 * 0xFFFFFFFE returned if out of memory
 * 0xFFFFFFFF returned if symbol found in static required binding
 */
typedef t_uint32 (*CBresolvSymbol)(
        void* context,
        t_uint32 type,
        const char* symbolName,
        char* reloc_addr);

#endif
