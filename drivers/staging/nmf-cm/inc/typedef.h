/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \defgroup COMMON Common types and definitions
 *
 * \defgroup NMF_COMMON NMF common definition
 * \ingroup COMMON
 *
 * \defgroup NMF_ABI NMF ABI specification
 * \warning This page is not for multimedia developers !
 */
/*!
 * \brief Primitive Type Definition
 *
 * \defgroup NMF_PRIMITIVE_TYPE Primitive type definition
 * \ingroup COMMON
 */

#ifndef NMF_TYPEDEF_H_
#define NMF_TYPEDEF_H_

#undef PRIVATE
#define PRIVATE  static                 //!< Private macro declaration \ingroup NMF_PRIMITIVE_TYPE

#undef PUBLIC
#ifdef __cplusplus
#define PUBLIC extern "C"               //!< Public macro declaration \ingroup NMF_PRIMITIVE_TYPE
#else
#define PUBLIC extern                   //!< Public macro declaration \ingroup NMF_PRIMITIVE_TYPE
#endif

#if defined(__SYMBIAN32__)
/*!
 * \brief Declared IMPORT_SHARED to allow dll/shared library creation
 *
 * \note Value depend on OS.
 *
 * \ingroup NMF_PRIMITIVE_TYPE
 */
    #ifndef IMPORT_SHARED
        #define IMPORT_SHARED   IMPORT_C
    #endif
/*!
 * \brief Declared EXPORT_SHARED to allow dll/shared library creation
 *
 * \note Value depend on OS.
 *
 * \ingroup NMF_PRIMITIVE_TYPE
 */
    #ifndef EXPORT_SHARED
        #define EXPORT_SHARED   EXPORT_C
    #endif
#elif defined(LINUX)
    #ifndef IMPORT_SHARED
        #define IMPORT_SHARED
    #endif
    #ifndef EXPORT_SHARED
        #define EXPORT_SHARED   __attribute__ ((visibility ("default")))
    #endif
#else
    #ifndef IMPORT_SHARED
        #define IMPORT_SHARED
    #endif

    #ifndef EXPORT_SHARED
        #define EXPORT_SHARED
    #endif
#endif

/*
 * Definition of type that are used by interface.
 */

typedef unsigned int t_uword;
typedef signed int t_sword;

#ifdef __flexcc2__

typedef unsigned char t_bool;

#ifdef __mode16__

typedef signed char t_sint8;
typedef signed int t_sint16;
typedef signed long t_sint24;
typedef signed long t_sint32;
typedef signed long long t_sint40;
// bigger type are not handle on this mode

typedef unsigned char t_uint8;
typedef unsigned int t_uint16;
typedef unsigned long t_uint24;
typedef unsigned long t_uint32;
typedef unsigned long long t_uint40;
// bigger type are not handle on this mode

// shared addr type definition
//typedef __SHARED16 t_uint16 * t_shared_addr;
typedef void * t_shared_field;

#else /* __mode16__ -> __mode24__ */

typedef signed char t_sint8;
typedef signed short t_sint16;
typedef signed int t_sint24;
typedef signed long t_sint32;
typedef signed long t_sint40;
typedef signed long t_sint48;
typedef signed long long t_sint56;

typedef unsigned char t_uint8;
typedef unsigned short t_uint16;
typedef unsigned int t_uint24;
typedef unsigned long t_uint32;
typedef unsigned long t_uint40;
typedef unsigned long t_uint48;
typedef unsigned long long t_uint56;

// shared addr type definition
//typedef __SHARED16 t_uint16 * t_shared_addr;
typedef t_uint24 t_shared_field;

#endif /* MMDSP mode24 */

// shared register (ARM world) type definition
#if 0
typedef struct {
    t_uint16 lsb;
    t_uint16 msb;
} t_shared_reg;
#endif
typedef t_uint32 t_shared_reg;

typedef t_uint32 t_physical_address;

#include <stwdsp.h>

#else /* __flexcc2__ -> RISC 32 Bits */

#ifndef _HCL_DEFS_H
typedef unsigned char t_bool;                   //!< Boolean primitive type \ingroup NMF_PRIMITIVE_TYPE

typedef unsigned char t_uint8;                  //!< Unsigned 8 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef signed char t_sint8;                    //!< Signed 8 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef unsigned short t_uint16;                //!< Unsigned 16 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef signed short t_sint16;                  //!< Signed 16 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef unsigned long t_uint32;                 //!< Unsigned 32 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef signed long t_sint32;                   //!< Signed 32 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef unsigned long long t_uint64;            //!< Unsigned 64 bits primitive type \ingroup NMF_PRIMITIVE_TYPE
typedef signed long long t_sint64;              //!< Signed 64 bits primitive type \ingroup NMF_PRIMITIVE_TYPE

typedef t_uint32 t_physical_address;
#endif /* _HCL_DEFS_H */

typedef unsigned long t_uint24;
typedef signed long t_sint24;
typedef unsigned long long t_uint48;
typedef signed long long t_sint48;

// shared addr type definition
typedef t_uint32 t_shared_addr;

// shared register (ARM world) type definition
typedef t_uint32 t_shared_reg;
typedef t_uint32 t_shared_field;

#endif /* RISC 32 Bits */

/*
 * Define boolean type
 */
#undef FALSE
#define FALSE  0                                            //!< Boolean FALSE value
#undef TRUE
#define TRUE  1                                             //!< Boolean TRUE value

#ifndef NULL
    #if defined __flexcc2__ || defined __SYMBIAN32__
        #define NULL (0x0)                                  //!< Null type \ingroup NMF_PRIMITIVE_TYPE
    #else
        #define NULL ((void*)0x0)                           //!< Null type \ingroup NMF_PRIMITIVE_TYPE
    #endif
#endif

typedef t_uint32 t_nmf_component_handle;

#endif /* NMF_TYPEDEF_H_ */
