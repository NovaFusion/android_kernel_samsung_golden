/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Configuration Component Manager User Engine API.
 *
 * This file contains the Configuration CM Engine API for manipulating CM.
 */

#ifndef CONTROL_CONFIGURATION_ENGINE_H
#define CONTROL_CONFIGURATION_ENGINE_H

#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/memory/inc/memory_type.h>
#include <cm/engine/communication/inc/communication_type.h>

/*****************************************************************************************/
/*  Component Manager dedicated (for Configuration purpose) structured types definition  */
/*****************************************************************************************/

/*!
 * \brief Description of the Nomadik HW mapping configuration
 *
 * Describe the Nomadik mapping that is to say:
 *   - the ESRAM memory managed by the CM (The ESRAM address space SHALL BE declared as non cacheable, non bufferable inside host MMU table)
 *   - the mapping of the System HW Semaphore IP
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef struct {
    t_nmf_memory_segment  esramDesc;                   //!< Description of the ESRAM memory mapping into Nomadik SOC
    t_cm_system_address   hwSemaphoresMappingBaseAddr; //!< Description of the System HW Semaphores IP mapping into Nomadik SOC
} t_nmf_hw_mapping_desc;

/*!
 * @defgroup t_nmf_nomadik_version t_nmf_nomadik_version
 * \brief Description of the various supported Nomadik SOC version
 * @{
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef t_uint8 t_nmf_nomadik_version;                              //!< Fake enumeration type
#define NOMADIK_8810                ((t_nmf_nomadik_version)0)      //!< STn8810 chip (any cut)
#define NOMADIK_8815A0              ((t_nmf_nomadik_version)1)      //!< STn8815 chip (cut A0)
#define NOMADIK_8815                ((t_nmf_nomadik_version)2)      //!< STn8815 chip (other cuts)
#define NOMADIK_8820                ((t_nmf_nomadik_version)3)      //!< STn8820 chip
#define NOMADIK_8500                ((t_nmf_nomadik_version)4)      //!< STn8500 chip
/* @} */

/*!
 * \brief Description of the configuration parameters of the Component Manager
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef struct {
    t_nmf_coms_location     comsLocation;      //!< Configure where CM Communications objects are put (see \ref t_nmf_coms_location)
} t_nmf_config_desc;

/*!
 * @defgroup t_nmf_power_ctx t_nmf_power_ctx
 * \brief Definition of the CM-engine context
 *
 * OS integrator uses this value to known the context where the associated OSAL routine is called
 *
 * @{
 * \ingroup CM_ENGINE_CONTROL_API
 */

typedef t_uint32 t_nmf_power_ctx;                            //!< Fake enumeration type
#define PWR_FLUSH_REQ_INTERRUPT_CTX ((t_nmf_power_ctx)0x00)  //!< Interrupt context - called by \ref CM_ProcessMpcEvent
#define PWR_FLUSH_REQ_NORMAL_CTX    ((t_nmf_power_ctx)0x01)  //!< Normal context (CM user call)

/* @} */


/****************************************************************************************************************/
/*  Component Manager dedicated (for Media Processors Cores Configuration purpose) structured types definition  */
/****************************************************************************************************************/
/*!
 * @defgroup t_nmf_executive_engine_id t_nmf_executive_engine_id
 * \brief Identification of the Media Processor Executive Engine to deploy
 * @{
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef t_uint8 t_nmf_executive_engine_id;                                  //!< Fake enumeration type
#define SYNCHRONOUS_EXECUTIVE_ENGINE        ((t_nmf_executive_engine_id)0)  //!< MPC Synchronous executive engine
#define HYBRID_EXECUTIVE_ENGINE             ((t_nmf_executive_engine_id)1)  //!< MPC Hybrid synchronous executive engine
/* @} */

/*!
 * @defgroup t_nmf_semaphore_type_id t_nmf_semaphore_type_id
 * \brief Definition of which type semaphore shall be used for the given Media Processor communication mechanism
 * @{
 * \ingroup CM_ENGINE_CONTROL_API
 */
typedef t_uint8 t_nmf_semaphore_type_id;                                    //!< Fake enumeration type
#define LOCAL_SEMAPHORES                    ((t_nmf_semaphore_type_id)0)    //!< Embedded MMDSP macrocell semaphore, so CM_ProcessMpcEvent(<coreId>) shall be called under ISR connected to local MMDSP IRQ0
#define SYSTEM_SEMAPHORES                   ((t_nmf_semaphore_type_id)1)    //!< Shared system HW Semaphores, so CM_ProcessMpcEvent(ARM_CORE_ID) shall be called under ISR connected to shared HW Sem Host IRQ
/* @} */


/*!
 * \brief Opaque type for allocator, returned at CM configuration.
 */
typedef t_uint32 t_cfg_allocator_id;

/********************************************************************************/
/*       Configuration Component Manager API prototypes                         */
/********************************************************************************/

/*!
 * \brief Initialisation part
 *
 * This routine initialize and configure the Component Manager.
 *
 * \param[in] pNmfHwMappingDesc hardware mapping description
 * \param[in] pNmfConfigDesc NMF (mainly CM) Configuration description
 *
 * \exception TBD
 * \return exception number.
 *
 * \warning The ESRAM address space SHALL BE declared as non cacheable, non bufferable inside host MMU table
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC t_cm_error CM_ENGINE_Init(
        const t_nmf_hw_mapping_desc *pNmfHwMappingDesc,
        const t_nmf_config_desc *pNmfConfigDesc
        );


/*!
 * \brief Media Processor core initialisation part
 *
 * This routine configures a given Media Processor core
 *
 * \param[in] coreId Media Processor identifier
 * \param[in] executiveEngineId Media Processor Executive Engine identifier
 * \param[in] semaphoreTypeId Media Processor semaphores (to be used by communication mechanism) identifier
 * \param[in] nbYramBanks is the number of tcm ram banks to reserved for y memory
 * \param[in] mediaProcessorMappingBaseAddr Media Processor mapping into host CPU addressable space
 * \param[in] commDomain Domain for allocating communication FIFOs
 * \param[in] eeDomain Domain for EE instantiation
 * \param[in] sdramCodeAllocId Allocator Id for the SDRAM Code segment
 * \param[in] sdramDataAllocId Allocator Id for the SDRAM Data segment
 *
 * \exception TBD
 * \return exception number.
 *
 * \warning The Media Processor mapping address space SHALL BE declared as non cacheable, non bufferable inside host MMU table
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC t_cm_error CM_ENGINE_ConfigureMediaProcessorCore(
        t_nmf_core_id coreId,
        t_nmf_executive_engine_id executiveEngineId,
        t_nmf_semaphore_type_id semaphoreTypeId,
        t_uint8 nbYramBanks,
        const t_cm_system_address *mediaProcessorMappingBaseAddr,
        const t_cm_domain_id eeDomain,
        const t_cfg_allocator_id sdramCodeAllocId,
        const t_cfg_allocator_id sdramDataAllocId
        );

/*!
 * \brief Configure a memory segment for later
 *
 * \exception TBD
 * \return TBD
 *
 * \warning
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC t_cm_error CM_ENGINE_AddMpcSdramSegment(
        const t_nmf_memory_segment          *pDesc,                     //!< [in]  Memory segment description.
        t_cfg_allocator_id                  *allocId,                   //!< [out] Identifier of the created allocator.
        const char                          *memoryname                 //!< [in]  Memory purpose name
        );

/********************************************************************************/
/*       Destruction Component Manager API prototypes                         */
/********************************************************************************/
/*!
 * \brief Destruction part
 *
 * This routine destroyes and releases all resources used by the Component Manager.
 *
 * \ingroup CM_ENGINE_CONTROL_API
 */
PUBLIC void CM_ENGINE_Destroy(void);


#endif /* CONTROL_CONFIGURATION_ENGINE_H */
