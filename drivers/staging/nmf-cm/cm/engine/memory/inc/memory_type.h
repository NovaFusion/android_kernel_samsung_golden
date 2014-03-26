/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Public Component Manager Memory API type.
 *
 * This file contains the Component Manager API type for manipulating memory.
 */
#ifndef __INC_MEMORY_TYPE_H
#define __INC_MEMORY_TYPE_H

#include <cm/inc/cm_type.h>

/*!
 * @defgroup t_cm_mpc_memory_type t_cm_mpc_memory_type
 * \brief Definition of symbols used to reference the various type of Media Processor Core adressable memory
 * @{
 * \ingroup MEMORY
 */
typedef t_uint8 t_cm_mpc_memory_type;                            //!< Fake enumeration type
#define CM_MM_MPC_TCM16_X           ((t_cm_mpc_memory_type)0)
#define CM_MM_MPC_TCM24_X           ((t_cm_mpc_memory_type)1)
#define CM_MM_MPC_ESRAM16           ((t_cm_mpc_memory_type)2)
#define CM_MM_MPC_ESRAM24           ((t_cm_mpc_memory_type)3)
#define CM_MM_MPC_SDRAM16           ((t_cm_mpc_memory_type)4)
#define CM_MM_MPC_SDRAM24           ((t_cm_mpc_memory_type)5)
#define CM_MM_MPC_TCM16_Y           ((t_cm_mpc_memory_type)6)
#define CM_MM_MPC_TCM24_Y           ((t_cm_mpc_memory_type)7)
#define CM_MM_MPC_TCM16             CM_MM_MPC_TCM16_X
#define CM_MM_MPC_TCM24             CM_MM_MPC_TCM24_X

/* @} */

/*!
 * @defgroup t_cm_memory_alignment t_cm_memory_alignment
 * \brief Definition of symbols used to constraint the alignment of the allocated memory
 * @{
 * \ingroup MEMORY
 */
typedef t_uint16 t_cm_memory_alignment;                            //!< Fake enumeration type
#define CM_MM_ALIGN_NONE            ((t_cm_memory_alignment)0x00000000)
#define CM_MM_ALIGN_BYTE            ((t_cm_memory_alignment)CM_MM_ALIGN_NONE)
#define CM_MM_ALIGN_HALFWORD        ((t_cm_memory_alignment)0x00000001)
#define CM_MM_ALIGN_WORD            ((t_cm_memory_alignment)0x00000003)
#define CM_MM_ALIGN_2WORDS          ((t_cm_memory_alignment)0x00000007)
#define CM_MM_ALIGN_16BYTES         ((t_cm_memory_alignment)0x0000000F)
#define CM_MM_ALIGN_4WORDS          ((t_cm_memory_alignment)0x0000000F)
#define CM_MM_ALIGN_AHB_BURST       ((t_cm_memory_alignment)0x0000000F)
#define CM_MM_ALIGN_32BYTES         ((t_cm_memory_alignment)0x0000001F)
#define CM_MM_ALIGN_8WORDS          ((t_cm_memory_alignment)0x0000001F)
#define CM_MM_ALIGN_64BYTES         ((t_cm_memory_alignment)0x0000003F)
#define CM_MM_ALIGN_16WORDS         ((t_cm_memory_alignment)0x0000003F)
#define CM_MM_ALIGN_128BYTES        ((t_cm_memory_alignment)0x0000007F)
#define CM_MM_ALIGN_32WORDS         ((t_cm_memory_alignment)0x0000007F)
#define CM_MM_ALIGN_256BYTES        ((t_cm_memory_alignment)0x000000FF)
#define CM_MM_ALIGN_64WORDS         ((t_cm_memory_alignment)0x000000FF)
#define CM_MM_ALIGN_512BYTES        ((t_cm_memory_alignment)0x000001FF)
#define CM_MM_ALIGN_128WORDS        ((t_cm_memory_alignment)0x000001FF)
#define CM_MM_ALIGN_1024BYTES       ((t_cm_memory_alignment)0x000003FF)
#define CM_MM_ALIGN_256WORDS        ((t_cm_memory_alignment)0x000003FF)
#define CM_MM_ALIGN_2048BYTES       ((t_cm_memory_alignment)0x000007FF)
#define CM_MM_ALIGN_512WORDS        ((t_cm_memory_alignment)0x000007FF)
#define CM_MM_ALIGN_4096BYTES       ((t_cm_memory_alignment)0x00000FFF)
#define CM_MM_ALIGN_1024WORDS       ((t_cm_memory_alignment)0x00000FFF)
#define CM_MM_ALIGN_65536BYTES      ((t_cm_memory_alignment)0x0000FFFF)
#define CM_MM_ALIGN_16384WORDS      ((t_cm_memory_alignment)0x0000FFFF)
/* @} */

/*!
 * @defgroup t_cm_mpc_memory_alignment t_cm_mpc_memory_alignment
 * \brief Definition of symbols used to constraint the alignment of the allocated mpc memory
 * @{
 * \ingroup MEMORY
 */
typedef t_uint16 t_cm_mpc_memory_alignment;                            //!< Fake enumeration type
#define CM_MM_MPC_ALIGN_NONE        ((t_cm_mpc_memory_alignment)0x00000000)
#define CM_MM_MPC_ALIGN_HALFWORD    ((t_cm_mpc_memory_alignment)0x00000001)
#define CM_MM_MPC_ALIGN_WORD        ((t_cm_mpc_memory_alignment)0x00000003)
#define CM_MM_MPC_ALIGN_2WORDS      ((t_cm_mpc_memory_alignment)0x00000007)
#define CM_MM_MPC_ALIGN_4WORDS      ((t_cm_mpc_memory_alignment)0x0000000F)
#define CM_MM_MPC_ALIGN_8WORDS      ((t_cm_mpc_memory_alignment)0x0000001F)
#define CM_MM_MPC_ALIGN_16WORDS     ((t_cm_mpc_memory_alignment)0x0000003F)
#define CM_MM_MPC_ALIGN_32WORDS     ((t_cm_mpc_memory_alignment)0x0000007F)
#define CM_MM_MPC_ALIGN_64WORDS     ((t_cm_mpc_memory_alignment)0x000000FF)
#define CM_MM_MPC_ALIGN_128WORDS    ((t_cm_mpc_memory_alignment)0x000001FF)
#define CM_MM_MPC_ALIGN_256WORDS    ((t_cm_mpc_memory_alignment)0x000003FF)
#define CM_MM_MPC_ALIGN_512WORDS    ((t_cm_mpc_memory_alignment)0x000007FF)
#define CM_MM_MPC_ALIGN_1024WORDS   ((t_cm_mpc_memory_alignment)0x00000FFF)
#define CM_MM_MPC_ALIGN_65536BYTES  ((t_cm_mpc_memory_alignment)0x0000FFFF)
#define CM_MM_MPC_ALIGN_16384WORDS  ((t_cm_mpc_memory_alignment)0x0000FFFF)
/* @} */

/*!
 * \brief Identifier of a memory handle
 * \ingroup MEMORY
 */
typedef t_uint32 t_cm_memory_handle;

/*!
 * \brief Description of a memory segment
 *
 * <=> allocable addressable space
 * \ingroup MEMORY
 */
typedef struct {
    t_cm_system_address systemAddr; //!< Logical AND physical segment start address
    t_uint32 size;                //!< segment size (in bytes)
} t_nmf_memory_segment;
#define INIT_MEMORY_SEGMENT {{0, 0}, 0}

/*!
 * \brief Definition of structure used for an allocator status
 * \ingroup MEMORY
 */
typedef struct
{
    struct {
        t_uint32 size;              //!< size of the allocator
        /* Block counters */
        t_uint16 used_block_number; //!< used block number
        t_uint16 free_block_number; //!< free block number

        /* Free memory min/max */
        t_uint32 maximum_free_size; //!< maximum free size
        t_uint32 minimum_free_size; //!< minimum free size

        /* Accumulation of free and used memory */
        t_uint32 accumulate_free_memory; //!< accumulate free memory
        t_uint32 accumulate_used_memory; //!< accumulate used memory
    } global;

    struct {
        t_uint32 size;       //!< size of the domain
        t_uint32 maximum_free_size; //!< maximum free size in the given domain
        t_uint32 minimum_free_size; //!< minimum free size in the given domain
        t_uint32 accumulate_free_memory; //all free memory of the given domain
        t_uint32 accumulate_used_memory; //all used memory of the given domain
    } domain;

    struct {
        t_uint32 sizes[3];
    } stack[NB_CORE_IDS];

} t_cm_allocator_status;

#endif /* __INC_MEMORY_TYPE_H */

