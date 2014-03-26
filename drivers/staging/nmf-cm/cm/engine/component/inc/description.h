/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#ifndef __INC_CM_COMPONENT_DESCRIPTION_H
#define __INC_CM_COMPONENT_DESCRIPTION_H

#include <cm/engine/elf/inc/memory.h>
#include <cm/engine/utils/inc/string.h>

#include <inc/nmf-limits.h>

/*!
 * \internal
 * \brief Description of an interface
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct _t_interface_description {
    t_dup_char              type;                  //!< Type of the interface
    t_uint16                referenceCounter;       //!< Number of template referencing the interface
    t_uint8                 methodNumber;           //!< Number of method in the interfaces
    struct _t_interface_description* next;
    t_dup_char              methodNames[1];        //!< Array of method names
} t_interface_description;

/*!
 * \internal
 * \brief Description of a variable memory on a collection index
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_uint32            offset;            //!< Offset in the memory
    const t_elfmemory   *memory;          //!< Memory
} t_memory_reference;

/*!
 * \internal
 * \brief Description of a required interface on a collection index
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_uint32                numberOfClient;                //!< Number of interface descriptor really connected to this interface
    t_memory_reference      *memories;                   /*!< Memory where each interface reference descriptor resides
                                                                                \note memories[numberOfClient] */
} t_interface_require_index;

/*!
 * \internal
 * \brief Description of a required interface
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_dup_char                      name;                                //!< Name of the interface
    t_interface_description         *interface;                           //!< Description of the interface
    t_uint8                         requireTypes;                         //!< Mask of t_elf_interface_require_type
    t_uint8                         collectionSize;                       //!< Size of the collection (1 if not a collection)
    t_interface_require_index       *indexes;         /*!< Require information for each collection index
                                                                \note indexes[collectionSize] */
} t_interface_require;

/*!
 * \internal
 * \brief Description of a provided interface method on a collection index
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_memory_reference     memory;                      //!< Memory of the method
} t_interface_provide_index;

/*!
 * \internal
 * \brief Description of a provided interface
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_dup_char                     name;                                 //!< Name of the interface
    t_interface_description        *interface;                            //!< Description of the interface
    t_uint8                         provideTypes;                         //!< Mask of t_elf_interface_provide_type
    t_uint8                        interruptLine;                         //!< Interrupt line if interrupt (0 if not)
    t_uint8                        collectionSize;                        //!< Size of the collection (1 if not a collection)
    t_interface_provide_index      **indexes;                         //!< Provide information for each collection index
} t_interface_provide;

/*!
 * \internal
 * \brief Description of a attribute
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_dup_char              name;                             //!< Name of the attribute
    t_memory_reference      memory;                            //!< Memory where the attribute reside
} t_attribute;

/*!
 * \internal
 * \brief Description of a property
 * \ingroup COMPONENT_INTERNAL
 */
typedef struct {
    t_dup_char          name;           //!< Name of this attribute
    t_dup_char          value;          //!< String of the value
} t_property;




#endif
