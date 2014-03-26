/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief NMF component description ABI
 *
 * \defgroup NMF_HEADER NMF Component Description ABI
 * The NMF component description ABI is stored in the nmf_segment in the ELF component file.
 * The NMF component description section start by the t_elf_component_header structure.
 *
 * \warning <B>The format of this section is not fixed and is able to be changed without concerting.</B>
 * \note You can use the nmfHeaderVersion to check if the format has changed.
 * \note Each pointers in this section is relative to the beginning of the section and must be relocated before used.
 * \ingroup NMF_ABI
 */
#ifndef __INC_CM_NMF_HEADERABI_H
#define __INC_CM_NMF_HEADERABI_H

#include <cm/inc/cm_type.h>

/*!
 * \brief Description of a interface
 * \ingroup NMF_HEADER
 */
typedef struct {
    char                    *type;                  //!< Type of this Interface
    t_uint8                 methodNumber;           //!< Number of method in the interfaces
    t_uint8                 reserved1, reserved2, reserved3;
    char                    *methodNames[1];        //!< Array of method names [methodNumber]
} t_elf_interface_description;

/*!
 * \brief Description of required interface type (value could be combinated)
 * \ingroup NMF_HEADER
 */
typedef enum {
    COLLECTION_REQUIRE = 1,                     //!< Required interface is a collection
    OPTIONAL_REQUIRE = 2,                       //!< Required interface if optional
    STATIC_REQUIRE = 4,                         //!< Required interface is static
    VIRTUAL_REQUIRE = 8,                        //!< Required interface is virtual (only for introspection purpose)
    INTRINSEC_REQUIRE = 16                      //!< Required interface is intrinsec (bind automatically done by runtime)
} t_elf_interface_require_type;

/*!
 * \brief Description of a required interface on a collection index
 * \ingroup NMF_HEADER
 */
typedef struct {
    t_uint32     numberOfClient;                //!< Number of interface descriptor really connected to this interface
    t_uint32     symbols[1];          /*!< Symbol of the real name of the attribute
                                        \note Real type symbols[numberOfClient]
                                        \note Use relocation in order to get symbol information */
} t_elf_interface_require_index;

/*!
 * \brief Description of an interface required
 * \ingroup NMF_HEADER
 */
typedef struct {
	char        *name;		                            //!< name of the interface: offset in string segment
    t_uint8     requireTypes;                           //!< Mask of t_elf_interface_require_type
    t_uint8     collectionSize;                         //!< Size of the collection (1 if not a collection)
    t_uint8     reserved1, reserved2;
    t_elf_interface_description     *interface;         //!< Interface description
    t_elf_interface_require_index   indexes[1];           /*!< Require information for each collection index
                                                                        \note Real type: indexes[collectionSize],
                                                                        available only if not static interface */
}  t_elf_required_interface;

/*!
 * \brief Description of provided interface type (value could be combinated)
 * \ingroup NMF_HEADER
 */
typedef enum {
    COLLECTION_PROVIDE = 1,                               //!< Provided interface is a collection
    VIRTUAL_PROVIDE = 2                         //!< Provided interface is virtual (only for introspection purpose)
} t_elf_interface_provide_type;

/*!
 * \brief Description of an interface provided
 * \ingroup NMF_HEADER
 */
typedef struct {
	char* 			                name;		            //!< name of the interface: offset in string segment
    t_uint8                         provideTypes;           //!< Mask of t_elf_interface_provide_type
    t_uint8                         interruptLine;          //!< Interrupt line if interrupt (0 if not)
    t_uint8                         collectionSize;         //!< Size of the collection (1 if not a collection)
    t_uint8                         reserved1;
	t_elf_interface_description     *interface;             //!< Interface description
    t_uint32                        methodSymbols[1];        /*!< Symbol of the real name of methods of the interface for each collection index
                                                                    \note Real type: methodSymbols[collectionSize][methodNumber]
                                                                    \note Use relocation in order to get symbol information*/
} t_elf_provided_interface;

/*!
 * \brief Description of an attribute
 * \ingroup NMF_HEADER
 */
typedef struct {
	char* 	    name;			//!< Name of this attribute
    t_uint32    symbols;          /*!< Symbol of the real name of the attribute
                                        \note Use relocation in order to get symbol information */
} t_elf_attribute;

/*!
 * \brief Description of an property
 * \ingroup NMF_HEADER
 */
typedef struct {
	char* 	name;			//!< Name of this attribute
	char*	value;			//!< String of the value
} t_elf_property;

#define MAGIC_COMPONENT 0x123    //!< Magic Number for a component \ingroup NMF_HEADER
#define MAGIC_SINGLETON 0x321    //!< Magic Number for a singleton component \ingroup NMF_HEADER
#define MAGIC_FIRMWARE 0x456      //!< Magic Number for Execution Engine Component \ingroup NMF_HEADER

/*!
 * \brief Description of a ELF component header
 *
 * The NMF component description section start by this structure.
 *
 * \ingroup NMF_HEADER
 */
typedef struct {
	t_uint32                    magic;		    //!< Magic Number
    t_uint32                    nmfVersion;     //!< Version of the NMF Header

    char*                       templateName;   //!< Name of the component template

    t_uint32                    LCCConstruct;   //!< Life cycle Constructor offset
    t_uint32                    LCCStart;       //!< Life cycle Starter offset
    t_uint32                    LCCStop;        //!< Life cycle Stopper offset
    t_uint32                    LCCDestroy;     //!< Life cycle Destructer offset

    t_uint32                    minStackSize;   //!< Minimum stack size

    t_uint32                    attributeNumber;//!< Number of attributes
    t_elf_attribute             *attributes;    //!< Array of attributes (be careful, this reference must be relocated before use)

    t_uint32                    propertyNumber; //!< Number of properties
    t_elf_property              *properties;    //!< Array of properties (be careful, this reference must be relocated before use)

    t_uint32                    provideNumber;  //!< Number of interfaces provided
    t_elf_provided_interface    *provides;      //!< Array of interfaces provided (be careful, this reference must be relocated before use)

    t_uint32                    requireNumber;     //!< Array of interfaces required
    t_elf_required_interface    *requires;      //!< Array of interfaces required (be careful, this reference must be relocated before use)

} t_elf_component_header;

#endif
