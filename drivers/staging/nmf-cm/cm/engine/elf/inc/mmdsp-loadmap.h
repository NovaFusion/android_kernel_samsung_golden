/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Elf writer internal methods.
 *
 * \defgroup LOADMAP MMDSP ELF writer (a linker in fact).
 */
#ifndef __INC_CM_LOADMAP_H
#define __INC_CM_LOADMAP_H

#include <cm/inc/cm_type.h>

/*
 * Align with loadmap :
 * https://codex.cro.st.com/wiki/index.php?pagename=Specification%2FLoadmap%2Fv1.2&group_id=310
 */
#define LOADMAP_MAGIC_NUMBER 0xFBBF

#define LOADMAP_VERSION_MSB 1
#define LOADMAP_VERSION_LSB 2

struct LoadMapItem
{
  const char*  pSolibFilename;  // Filename of shared library object
  void*        pAddrProg;       // Load address of program section
  void*        pAddrEmbProg;    // Load address of embedded program section
  void*        pThis;           // Data base address of component instance
  void*        pARMThis;        // ARM component debug ID
  const char*  pComponentName;  // Pretty name of the component instance, NULL if none.
  struct LoadMapItem* pNextItem;// Pointer on the next list item, NULL if last one.
  void*        pXROM;           // Start address of XROM
  void*        pYROM;           // Start address of YROM
};

struct LoadMapHdr
{
  t_uint16      nMagicNumber;    // Equal to 0xFBBF.
  t_uint16      nVersion;        // The version of the load map format.
  t_uint32      nRevision;       // A counter incremented at each load map list modification.
  struct LoadMapItem* pFirstItem;// Pointer on the first item, NULL if no shared library loaded.
};

#endif /* __INC_CM_LOADMAP_H */
