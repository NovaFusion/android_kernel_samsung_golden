/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Communication Component Manager API type.
 */
#ifndef COMMUNICATION_TYPE_H_
#define COMMUNICATION_TYPE_H_

#include <cm/inc/cm_type.h>


/*!
 * \brief Buffer type used for (un)marshalling parameters.
 *
 * This buffer type is used for (un)marshalling paramaters. It can either be a
 * shared memory buffer (ESRAM or SDRAM) or a pure host software memory (stack).

 * \ingroup CM_ENGINE_API
 */
typedef t_uint16 *t_event_params_handle;

/*!
 * \brief Component manager handle to Host -> MPC communication.
 *
 * \ingroup CM_ENGINE_API
 */
typedef t_uint32 t_cm_bf_host2mpc_handle;

/*!
 * \brief Component manager handle to MPC -> Host communication.
 *
 * \ingroup CM_ENGINE_API
 */
typedef t_uint32 t_cm_bf_mpc2host_handle;

/*!
 * \brief Component manager proxy handle to MPC -> Host skeleton context.
 *
 * \ingroup CM_ENGINE_API
 */
typedef t_uint32 t_nmf_mpc2host_handle;

/*!
 * @defgroup t_nmf_coms_location t_nmf_coms_location
 * \brief Definition of the location of the internal CM communication objects
 *
 * @{
 * \ingroup CM_ENGINE_API
 */
typedef t_uint8 t_nmf_coms_location;                                //!< Fake enumeration type
#define COMS_IN_ESRAM               ((t_nmf_coms_location)0)        //!< All coms objects (coms and params fifos) will be in embedded RAM
#define COMS_IN_SDRAM               ((t_nmf_coms_location)1)        //!< All coms objects (coms and params fifos) will be in external RAM
/* @} */

#endif /*COMMUNICATION_TYPE_H_*/
