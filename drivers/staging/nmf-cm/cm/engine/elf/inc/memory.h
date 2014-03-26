/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Elf memory.
 */
#ifndef __INC_CM_ELF_MEMORY_H
#define __INC_CM_ELF_MEMORY_H

#include <cm/engine/dsp/inc/dsp.h>

/**
 * \brief Memory identifier
 */
typedef t_uint8 t_memory_id;

/**
 * \brief Memory property
 */
typedef enum {
    MEM_FOR_MULTIINSTANCE,
    MEM_FOR_SINGLETON,
    MEM_FOR_LAST
} t_instance_property;

/**
 * \brief Memory prupose (for processor with different address space for code and data/
 */
typedef enum {
    MEM_CODE,
    MEM_DATA
} t_memory_purpose;

/**
 * \brief Memory property
 */
typedef enum {
    MEM_PRIVATE,
    MEM_SHARABLE,
} t_memory_property;

/**
 * \brief Elf memory mapping description
 */
typedef struct
{
    t_memory_id             id;
    t_dsp_memory_type_id    dspMemType;
    t_uint32                startAddr;
    t_cm_memory_alignment   memAlignement;
    t_memory_property       property;
    t_memory_purpose        purpose;
    t_uint8                 fileEntSize;
    t_uint8                 memEntSize;
    char*                   memoryName;
} t_elfmemory;

#define NUMBER_OF_MMDSP_MEMORY  15

/*
 * \brief Elf segment description
 */
typedef struct {
    // Data in Bytes
    t_uint32                sumSize;
    t_bool                  sumSizeSetted;
    t_cm_logical_address    hostAddr;   // Valid only if section Load in memory
    t_uint32                maxAlign;
    // Data in word
    t_uint32                mpcAddr;    // Valid only if section Load in memory
} t_elfSegment;


#endif
