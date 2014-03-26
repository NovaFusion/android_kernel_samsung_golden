/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Component Manager types.
 *
 * This file contains the Component Manager  types.
 *
 * \defgroup CM CM Type Definitions
 * \ingroup CM_USER_API
 */
#ifndef _CM_TYPE_H_
#define _CM_TYPE_H_

#include <share/inc/nmf.h>
#include <share/inc/macros.h>

#include <nmf/inc/channel_type.h>

/*!
 * @defgroup t_cm_error t_cm_error
 * \brief Description of the various errors returned by CM API routines
 * @{
 * \ingroup CM
 */
typedef t_nmf_error t_cm_error;                                             //!< Error type returned by CM API routines

/*********************************************************************************/
/* WARNING: UPDATE CM_StringError() func each time an error is added/removed !!! */
/* CM_StringError() is defined twice in:                                         */
/*   nmf_core/host/cm/proxy/common/wrapper/src/wrapper.c                         */
/*   tests/src/common/nte/src/nte.c                                              */
/*********************************************************************************/
#define CM_LAST_ERROR_ID                    ((t_cm_error)-128)
#define CM_INTEGRATION_ERROR                NMF_INTEGRATION_ERROR0      //!< \ref NMF_INTEGRATION_ERROR0

    /* Communication */
#define CM_FLUSH_MESSAGE                    NMF_FLUSH_MESSAGE           //!< Message send after call to CM_FlushChannel()
#define CM_BUFFER_OVERFLOW                  ((t_cm_error)-105)          //!< Buffer overflow (interface binding message bigger than buffer)
#define CM_USER_NOT_REGISTERED              ((t_cm_error)-104)          //!< User not registered
#define CM_NO_MESSAGE                       NMF_NO_MESSAGE              //!< \ref NMF_NO_MESSAGE
#define CM_PARAM_FIFO_OVERFLOW              ((t_cm_error)-102)          //!< Param fifo overflow
#define CM_INTERNAL_FIFO_OVERFLOW           ((t_cm_error)-101)          //!< Internal services fifo overflow (not returned to user)
#define CM_MPC_NOT_RESPONDING               ((t_cm_error)-100)          //!< MPC not responding (either crash, interrupt handler too long, internal NMF fifo coms overflow, ...).

    /* ELF & File system */
#define CM_FS_ERROR                         ((t_cm_error)-96)           //!< FileSystem error
#define CM_NO_SUCH_FILE                     ((t_cm_error)-95)           //!< No such file or directory
#define CM_INVALID_ELF_FILE                 ((t_cm_error)-94)           //!< File isn't a valid MMDSP ELF file
#define CM_NO_SUCH_BASE                     ((t_cm_error)-93)           //!< The memory base doesn't exist

    /* Introspection */
#define CM_NO_SUCH_ATTRIBUTE                NMF_NO_SUCH_ATTRIBUTE       //!< \ref NMF_NO_SUCH_ATTRIBUTE
#define CM_NO_SUCH_PROPERTY                 NMF_NO_SUCH_PROPERTY        //!< \ref NMF_NO_SUCH_PROPERTY

    /* Component Life Cycle */
#define CM_COMPONENT_NOT_STOPPED            NMF_COMPONENT_NOT_STOPPED   //!< \ref NMF_COMPONENT_NOT_STOPPED
#define CM_COMPONENT_NOT_UNBINDED           ((t_cm_error)-79)           //!< Component must be fully unbinded before perform operation
#define CM_COMPONENT_NOT_STARTED            ((t_cm_error)-78)           //!< Component must be started to perform operation
#define CM_COMPONENT_WAIT_RUNNABLE          ((t_cm_error)-76)           //!< Component need acknowlegdment of life cycle start function before perform operation
#define CM_REQUIRE_INTERFACE_UNBINDED       ((t_cm_error)-75)           //!< Required component interfaces must be binded before perform operation
#define CM_INVALID_COMPONENT_HANDLE         ((t_cm_error)-74)           //!< Try to access a component already destroyed

    /* Binder */
#define CM_NO_SUCH_PROVIDED_INTERFACE       NMF_NO_SUCH_PROVIDED_INTERFACE   //!< \ref NMF_NO_SUCH_PROVIDED_INTERFACE
#define CM_NO_SUCH_REQUIRED_INTERFACE       NMF_NO_SUCH_REQUIRED_INTERFACE   //!< \ref NMF_NO_SUCH_REQUIRED_INTERFACE
#define CM_ILLEGAL_BINDING                  ((t_cm_error)-62)           //!< Client and server interface type mismatch
#define CM_ILLEGAL_UNBINDING                ((t_cm_error)-61)           //!< Try to unbind component with bad binding Factories
#define CM_INTERFACE_ALREADY_BINDED         NMF_INTERFACE_ALREADY_BINDED//!< \ref NMF_INTERFACE_ALREADY_BINDED
#define CM_INTERFACE_NOT_BINDED             NMF_INTERFACE_NOT_BINDED    //!< \ref NMF_INTERFACE_NOT_BINDED

    /* Loader */
#define CM_BINDING_COMPONENT_NOT_FOUND      ((t_cm_error)-48)           //!< Binding Component template name don't exist on components repository (should be generated thanks nkitf tool)
#define CM_COMPONENT_NOT_FOUND              ((t_cm_error)-47)           //!< Component template name doesn't exist on components repository
#define CM_NO_SUCH_SYMBOL                   ((t_cm_error)-46)           //!< Symbol name doesn't exported by the underlying component
#define CM_COMPONENT_EXIST                  ((t_cm_error)-45)           //!< Component name already exists in the component cache

    /* Fifo management related ones */
#define CM_FIFO_FULL                        ((t_cm_error)-40)           //!< Fifo is full
#define CM_FIFO_EMPTY                       ((t_cm_error)-39)           //!< Fifo is empty
#define CM_UNKNOWN_FIFO_ID                  ((t_cm_error)-38)           //!< Fifo handle doesn't exist

    /* Memory management related ones */
#define CM_DOMAIN_VIOLATION                 ((t_cm_error)-33)           //!< Domain violation
#define CM_CREATE_ALLOC_ERROR               ((t_cm_error)-32)           //!< Error during allocator creation
#define CM_UNKNOWN_MEMORY_HANDLE            ((t_cm_error)-31)           //!< Handle doesn't exists
#define CM_NO_MORE_MEMORY                   NMF_NO_MORE_MEMORY          //!< \ref NMF_NO_MORE_MEMORY
#define CM_BAD_MEMORY_ALIGNMENT             ((t_cm_error)-29)           //!< Memory alignment wanted is not correct
#define CM_MEMORY_HANDLE_FREED              ((t_cm_error)-28)           //!< Handle was alread freed
#define CM_INVALID_DOMAIN_DEFINITION        ((t_cm_error)-27)           //!< Domain to be created is not correctly defined
#define CM_INTERNAL_DOMAIN_OVERFLOW         ((t_cm_error)-26)           //!< Internal domain descriptor overflow (too many domains) //TODO, juraj, remove this error
#define CM_INVALID_DOMAIN_HANDLE            ((t_cm_error)-25)           //!< Invalid domain handle
#define CM_ILLEGAL_DOMAIN_OPERATION         ((t_cm_error)-21)           //!< Operation on a domain is illegal (like destroy of a domain with referenced components)

    /* Media Processor related ones */
#define CM_MPC_INVALID_CONFIGURATION        ((t_cm_error)-24)           //!< Media Processor Core invalid configuration
#define CM_MPC_NOT_INITIALIZED              ((t_cm_error)-23)           //!< Media Processor Core not yet initialized
#define CM_MPC_ALREADY_INITIALIZED          ((t_cm_error)-22)           //!< Media Processor Core already initialized
//ERROR 21 is defined above, with the domains

    /* Power Mgt related ones */
#define CM_PWR_NOT_AVAILABLE                ((t_cm_error)-16)           //!< No modification of the state of the power input

    /* Common errors */
#define CM_INVALID_DATA                     ((t_cm_error)-4)            //!< Invalid internal data encountered
#define CM_OUT_OF_LIMITS                    ((t_cm_error)-3)            //!< User reach an internal nmf limits of limits.h file
#define CM_INVALID_PARAMETER                NMF_INVALID_PARAMETER       //!< \ref NMF_INVALID_PARAMETER
#define CM_NOT_YET_IMPLEMENTED              ((t_cm_error)-1)            //!< CM API not yet implemented
#define CM_OK                               NMF_OK                      //!< \ref NMF_OK

/** @} */

/*!
 * \brief Definition of a physical memory address
 * \ingroup MEMORY
 */
typedef t_uint32 t_cm_physical_address;

/*!
 * \brief Definition of a logical memory address
 * \ingroup MEMORY
 */
typedef t_uint32 t_cm_logical_address;

/*!
 * \brief Definition of a system address into a system with MMU
 * \ingroup MEMORY
 */
typedef struct {
    t_cm_physical_address physical; //!< Physical memory address
    t_cm_logical_address logical;       //!< Logical memory address
} t_cm_system_address;
#define INVALID_SYSTEM_ADDRESS {(t_cm_physical_address)MASK_ALL32, (t_cm_logical_address)MASK_ALL32}


/*!
 * \brief Define a type used to manipulate size of various buffers
 * \ingroup MEMORY
 */
typedef t_uint32 t_cm_size;

#endif /* _CM_TYPE_H_ */

