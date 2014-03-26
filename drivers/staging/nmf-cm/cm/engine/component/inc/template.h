/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Components Management internal methods - Template API.
 *
 * \defgroup COMPONENT_INTERNAL Private component instances API
 */
#ifndef __INC_CM_TEMPLATE_H
#define __INC_CM_TEMPLATE_H

#include <cm/engine/dsp/inc/dsp.h>
#include <cm/engine/component/inc/description.h>
#include <cm/engine/elf/inc/elfapi.h>
#include <cm/engine/utils/inc/string.h>


/*!
 * \internal
 * \brief Class of a component
 * \ingroup COMPONENT_INTERNAL
 */
typedef enum {
	COMPONENT, 			//!< Primitive component
	SINGLETON,          //!< Singleton component
	FIRMWARE,			//!< Firmware composite component
} t_component_classe;

/*!
 * \internal
 * \brief Description of delayed relocation
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct _t_function_relocation {
    t_dup_char                      symbol_name;
    t_uint32                        type;
    char                            *reloc_addr;
    struct _t_function_relocation   *next;
} t_function_relocation;

struct t_component_instance;

/*!
 * \internal
 * \brief Description of a provided interface method on a collection index ; Available only when template loaded
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_uint32               methodAddresses;             //!< Address of each method
} t_interface_provide_index_loaded;

/*!
 * \internal
 * \brief Description of a provided interface ; Available only when template loaded
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_interface_provide_index_loaded    **indexesLoaded;                  //!< Provide information for each collection index
} t_interface_provide_loaded;


/*!
 * \internal
 * \brief Description of a component template
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct _t_component_template {
    t_dup_char                  name;                   //!< Template name (a.k.a component type)

    t_component_classe          classe;                 //!< Class of the component
    //TODO, juraj, remove dspId
    t_nmf_core_id               dspId;                  //!< Reference on DSP where template is loaded

    t_uint8                     numberOfInstance;       //!< Number of same instance (or singleton copy) create from this template

    t_uint8                     propertyNumber;         //!< Number of properties in this template
    t_uint8                     attributeNumber;        //!< Number of attributes in this template
    t_uint8                     provideNumber;          //!< Number of interface provided by this template
    t_uint8                     requireNumber;          //!< Number of interface required by this template

    t_uint32                    LCCConstructAddress;    //!< Life cycle Constructor address
    t_uint32                    LCCStartAddress;        //!< Life cycle Starter address
    t_uint32                    LCCStopAddress;         //!< Life cycle Stopper address
    t_uint32                    LCCDestroyAddress;      //!< Life cycle Destructer address

    t_uint32                    minStackSize;           //!< Minimum stack size

    t_memory_handle             memories[NUMBER_OF_MMDSP_MEMORY];   //!< Reference in different memory where datas are (YES, we fix implementation to MMDSP)
    const t_elfmemory           *thisMemory;            //!< Memory used to determine this
    const t_elfmemory           *codeMemory;            //!< Memory used to determine code

    t_function_relocation       *delayedRelocation;     //!< List of reference that can't been relocatable while appropritae binding done.

    t_property	        *properties;            //!< Array of properties in this template
    t_attribute         *attributes;            //!< Array of attributes in this template
    t_interface_provide *provides;              //!< Array of interface provided by this template
    t_interface_require *requires;              //!< Array of interface required by this template

    t_interface_provide_loaded  *providesLoaded;    //!< Array of interface provided by this template ; Available when loaded

    t_bool                      descriptionAssociatedWithTemplate;

    struct _t_component_template *prev, *next;
    struct t_component_instance  *singletonIfAvaliable;
} t_component_template;

#endif
