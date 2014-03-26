/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Components Management internal methods - Instance API.
 *
 */
#ifndef __INC_CM_INSTANCE_H
#define __INC_CM_INSTANCE_H

#include <cm/engine/component/inc/template.h>
#include <cm/engine/repository_mgt/inc/repository_mgt.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/utils/inc/table.h>
#include <cm/engine/utils/inc/string.h>

/*----------------------------------------------------------------------------
 * Component Instance API.
 *----------------------------------------------------------------------------*/
struct _t_interface_reference;

/*!
 * \internal
 * \brief Component life cycle state
 *
 * \ingroup COMPONENT_INTERNAL
 */
typedef enum {
    STATE_NONE,
    STATE_STOPPED,
    STATE_RUNNABLE,
    // STATE_DESTROYED identified when component remove from component list
} t_component_state;

struct t_client_of_singleton
{
    struct t_client_of_singleton    *next;
    t_nmf_client_id                 clientId;
    t_uint16                        numberOfInstance;
    t_uint16                        numberOfStart;
    t_uint16                        numberOfBind;
};

/*!
 * \internal
 * \brief Description of a component instance
 *
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct t_component_instance {
    t_dup_char              pathname;                         //!< Path Name of this component in the components architecture

    t_component_state       state;                            //!< Component state
    t_nmf_ee_priority       priority;                         //!< Executive engine component priority
    t_component_template   *Template;                         //!< Component template

    t_uint32                thisAddress;                      //!< Cached value of cm_DSP_GetDspAddress(component->memories[data], &thisAddress);

    t_memory_handle         memories[NUMBER_OF_MMDSP_MEMORY];  //!<Reference in different memory where datas are (YES, we fix implementation to MMDSP)

    struct _t_interface_reference **interfaceReferences;    /*!< Interface references
                                      (Share same index as template->u.p.requires)
                                      type == targets[interface_index][collection_index] */

    t_uint16                providedItfUsedCount;             //!< Use count to reference the number of components binded to this once, ie count the number of provided interfaces in use
    t_cm_instance_handle    instance;                         //!< index of this component within the ComponentTable
    t_cm_domain_id          domainId;                         //!< Domain where the component has been installed

    struct t_client_of_singleton    *clientOfSingleton;       //!< Client of singleton list
    t_memory_handle         loadMapHandle;       // handle of allocated memory for the loadMap structure and name;
    void *dbgCooky;                                           //!< pointer to OS internal data
} t_component_instance;

t_component_template* cm_lookupTemplate(t_nmf_core_id dspId, t_dup_char str);

/*!
 * \internal
 * \brief Load a component template.
 *
 * ...
 *
 * \param[in] templateName name of the template to load
 * \param[in] coreId DSP where template must be loaded
 * \praem[in] pRepComponent Pointer to the component entry stored in the Component Cache Repository
 * \param[in, out] template reference to put the loaded template (null if first instance)
 *
 * \exception CM_COMPONENT_NOT_FOUND
 * \exception CM_NO_MORE_MEMORY
 *
 * \return exception number.
 *
 * \warning For Component manager use only.
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_loadComponent(
        t_dup_char templateName,
        t_cm_domain_id domainId,
        t_elfdescription* elfhandle,
        t_component_template **reftemplate);

/*!
 * \internal
 * \brief Unload a component template.
 *
 * ...
 *
 * \param[in] template template to be unloaded
 * \praem[in] Private memories that has been created from component binary file
 *
 * \return exception number.
 *
 * \warning For Component manager use only.
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_unloadComponent(
        t_component_template *reftemplate);

/*!
 * \internal
 * \brief Instantiate a component.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_instantiateComponent(const char* templateName,
        t_cm_domain_id domainId,
        t_nmf_ee_priority priority,
        const char* pathName,
        t_elfdescription *elfhandle,
        t_component_instance** refcomponent);

struct t_client_of_singleton* cm_getClientOfSingleton(t_component_instance* component, t_bool createdIfNotExist, t_nmf_client_id clientId);

/*!
 * \internal
 * \brief Start a component.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_startComponent(t_component_instance* component, t_nmf_client_id clientId);

/*!
 * \internal
 * \brief Stop a component.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_stopComponent(t_component_instance* component, t_nmf_client_id clientId);

/*!
 * \internal
 */
typedef enum {
    DESTROY_NORMAL,
    DESTROY_WITHOUT_CHECK,
    DESTROY_WITHOUT_CHECK_CALL
} t_destroy_state;

/*!
 * \internal
 * \brief Destroy a component instance.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_destroyInstance(t_component_instance* component, t_destroy_state forceDestroy);

/*!
 * \internal
 * \brief Destroy a component instance.
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_destroyInstanceForClient(t_component_instance* component, t_destroy_state forceDestroy, t_nmf_client_id clientId);

/*!
 * \internal
 * \brief
 *
 * \ingroup COMPONENT_INTERNAL
 */
void cm_delayedDestroyComponent(t_component_instance *component);

/*!
 * \internal
 * \brief
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_component_instance *cm_lookupComponent(const t_cm_instance_handle hdl);

/*!
 * \internal
 * \brief
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_bool cm_isComponentOnCoreId(t_nmf_core_id coreId);

/*!
 * \internal
 * \brief
 *
 * \ingroup COMPONENT_INTERNAL
 */
t_cm_error cm_COMP_Init(void);

/*!
 * \internal
 * \brief
 *
 * \ingroup COMPONENT_INTERNAL
 */
void cm_COMP_Destroy(void);

/*
 * Table of instantiated components.
 */
extern t_nmf_table ComponentTable; /**< list (table) of components */
#define componentEntry(i) ((t_component_instance *)ComponentTable.entries[i])
#endif
