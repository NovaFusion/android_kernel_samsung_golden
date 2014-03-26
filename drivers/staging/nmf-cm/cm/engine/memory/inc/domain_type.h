/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/***************************************************************************/
/* file    : domain.h
 * author  : NMF team
 * version : 1.0
 *
 * brief : NMF domain definitions
 */
/***************************************************************************/

#ifndef DOMAIN_TYPE_H_
#define DOMAIN_TYPE_H_

#include <cm/inc/cm_type.h>
#include <cm/engine/memory/inc/memory_type.h>

/*!
 * \brief Domain identifier
 * \ingroup CM_DOMAIN_API
 */
typedef t_uint8 t_cm_domain_id;

/*!
 * \brief Client identifier
 *        0 (zero) is considered as an invalid or 'NO' client identifier
 * \ingroup CM_DOMAIN_API
 */
typedef t_uint32 t_nmf_client_id;
#define NMF_CORE_CLIENT (t_nmf_client_id)-1
#define NMF_CURRENT_CLIENT (t_nmf_client_id)0

typedef struct {
    t_uint32 offset;       //!< offset relativ to segment start in memory (in bytes)
    t_uint32 size;         //!< size in bytes of the domain segment
} t_cm_domain_segment;

/*!
 * \brief Domain memory description structure
 * \ingroup CM_DOMAIN_API
 */
typedef struct {
    t_nmf_core_id         coreId;     //!< MMDSP Core Id for this domain (used for TCM-X and TCM-Y at instantiate)
    t_cm_domain_segment   esramCode;  //!< ESRAM code segment
    t_cm_domain_segment   esramData;  //!< ESRAM data segment
    t_cm_domain_segment   sdramCode;  //!< SDRAM code segment
    t_cm_domain_segment   sdramData;  //!< SDRAM data segment
} t_cm_domain_memory;

#define INIT_DOMAIN_SEGMENT {0, 0}
#define INIT_DOMAIN {MASK_ALL8, INIT_DOMAIN_SEGMENT, INIT_DOMAIN_SEGMENT, INIT_DOMAIN_SEGMENT, INIT_DOMAIN_SEGMENT}


#endif /* DOMAIN_TYPE_H_ */
