/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Common Nomadik Multiprocessing Framework type definition
 *
 * This file contains the shared between cm and ee type definitions used into NMF for callback.
 */
/*!
 * \defgroup _t_nmf_channel_flag t_nmf_channel_flag
 * \ingroup NMF_COMMON
 */

#ifndef __INC_CHANNEL_TYPE_H
#define __INC_CHANNEL_TYPE_H

#include <inc/typedef.h>
#include <inc/nmf_type.idt>

/*!
 * \brief Define t_nmf_channel_flag type that allow to control if/how a new communication channel is created.
 * \ingroup _t_nmf_channel_flag
 */
typedef t_uint32 t_nmf_channel_flag;

#define NMF_CHANNEL_SHARED                      ((t_nmf_channel_flag)0) //!< \ingroup _t_nmf_channel_flag
#define NMF_CHANNEL_PRIVATE                     ((t_nmf_channel_flag)1) //!< \ingroup _t_nmf_channel_flag

/*!
 * \brief Define t_nmf_virtualInterruptHandler function type to allow to dispatch virtual interrupt
 * \ingroup VIRTUAL_INTERRUPT
 */
typedef void (*t_nmf_virtualInterruptHandler)(void *interruptContext); 

#endif

