/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/***************************************************************************/
/* file    : domain.h
 * author  : NMF team
 * version : 1.0
 *
 * brief : NMF domain definitions
 */
/***************************************************************************/

#ifndef DOMAIN_H_
#define DOMAIN_H_

#include <cm/inc/cm_type.h>
#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/memory/inc/memory.h>
#include <cm/engine/dsp/inc/dsp.h>

/* These default domains are used for singleton only ! */
#define DEFAULT_SVA_DOMAIN (t_cm_domain_id)1
#define DEFAULT_SIA_DOMAIN (t_cm_domain_id)2

/*!
 * \brief Domain type.
 * \internal
 * \ingroup CM_DOMAIN_API
 */
typedef enum {
    DOMAIN_ANY = 0,
    DOMAIN_NORMAL,
    DOMAIN_SCRATCH_PARENT,
    DOMAIN_SCRATCH_CHILD
} t_cm_domain_type;

/*!
 * \brief Domain descriptor. Holds offsets for all memory types present in the system.
 * \internal
 * \ingroup CM_DOMAIN_API
 */
typedef struct {
    t_cm_domain_memory            domain;      // the actual memory ranges
    t_cm_domain_type              type;        // domain type
    t_uint32                      refcount;    // reference counter for scratch domain dependencies
    t_nmf_client_id               client;      // client id for cleaning

    union {
        struct {
            t_memory_handle       handle;      // memory handle of the allocated chunk the covers the esram-data scratch region
        } parent;
        struct {
            t_cm_allocator_desc  *alloc;       //allocator descriptor for the scratch domain
            t_cm_domain_id        parent_ref;  //parent domain reference
        } child;
    } scratch;
    void *dbgCooky;                            //pointer to OS internal data
} t_cm_domain_desc;

#ifdef DEBUG
#define DOMAIN_DEBUG(handle) \
    handle = handle & ~0xc0;
#else
#define DOMAIN_DEBUG(handle)
#endif

/*!
 * \brief Domain descriptor array.
 */
extern t_cm_domain_desc domainDesc[];

typedef struct {
    t_cm_domain_id parentId;
    t_cm_domain_id domainId;
    t_cm_allocator_desc *allocDesc;
} t_cm_domain_scratch_desc;

extern t_cm_domain_scratch_desc domainScratchDesc[];

typedef struct {
    t_cm_system_address sdramCode;
    t_cm_system_address sdramData;
    t_cm_system_address esramCode;
    t_cm_system_address esramData;
} t_cm_domain_info;

/*!
 * \brief Init of the domain subsystem.
 */
PUBLIC t_cm_error cm_DM_Init(void);

/*!
 * \brief Clean-up of the domain subsystem.
 */
PUBLIC void cm_DM_Destroy(void);

/*!
 * \brief Domain creation.
 *
 * Allocates in slot in the domain descriptors array and copies segment infos from the domain
 * parameter to the descriptor. The resulting handle is returned via @param handle.
 *
 * Returns: CM_DOMAIN_INVALID in case of error, otherwise CM_OK.
 */
PUBLIC t_cm_error cm_DM_CreateDomain(const t_nmf_client_id client, const t_cm_domain_memory *domain, t_cm_domain_id *handle);

/*!
 * \brief Scratch (or overlap) domain creation.
 *
 * Create a scratch domain, ie domain where allocation may overlap.
 */
PUBLIC t_cm_error cm_DM_CreateDomainScratch(const t_nmf_client_id client, const t_cm_domain_id parentId, const t_cm_domain_memory *domain, t_cm_domain_id *handle);

/* !
 * \brief Retrieve the coreId from a given domain. Utility.
 */
PUBLIC t_nmf_core_id cm_DM_GetDomainCoreId(const t_cm_domain_id domainId);

/*!
 * \brief Destroy all domains belonging to a given client.
 */
PUBLIC t_cm_error cm_DM_DestroyDomains(const t_nmf_client_id client);

/*!
 * \brief Destroy a given domain.
 */
PUBLIC t_cm_error cm_DM_DestroyDomain(t_cm_domain_id handle);

/*!
 * \brief Check if the handle is valid.
 */
PUBLIC t_cm_error cm_DM_CheckDomain(t_cm_domain_id handle, t_cm_domain_type type);
PUBLIC t_cm_error cm_DM_CheckDomainWithClient(t_cm_domain_id handle, t_cm_domain_type type, t_nmf_client_id client);

/*!
 * \brief Memory allocation in a given domain, for a given memory type (see CM_AllocMpcMemory).
 */
PUBLIC t_memory_handle cm_DM_Alloc(t_cm_domain_id domainId, t_dsp_memory_type_id memType, t_uint32 size, t_cm_mpc_memory_alignment memAlignment, t_bool powerOn);

/*!
 * \brief Memory free using a given domain handle
 */
PUBLIC void cm_DM_FreeWithInfo(t_memory_handle memHandle, t_nmf_core_id *coreId, t_dsp_memory_type_id *memType, t_bool powerOff);

/*!
 * \brief Memory free using a given domain handle
 */
PUBLIC void cm_DM_Free(t_memory_handle memHandle, t_bool powerOff);

/*!
 * \brief Wrapper function for CM_GetMpcMemoryStatus.
 */
PUBLIC t_cm_error cm_DM_GetAllocatorStatus(t_cm_domain_id domainId, t_dsp_memory_type_id memType, t_cm_allocator_status *pStatus);

PUBLIC t_cm_error cm_DM_GetDomainAbsAdresses(t_cm_domain_id domainId, t_cm_domain_info *info);

/*!
 * \brief Change the domain for the given allocated chunk
 */
PUBLIC void cm_DM_SetDefaultDomain(t_memory_handle memHandle, t_nmf_core_id coreId);
#endif /* DOMAIN_H_ */
