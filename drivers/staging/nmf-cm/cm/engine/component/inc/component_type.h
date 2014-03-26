/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Components Component Manager API type.
 *
 * \defgroup COMPONENT CM Components API
 * \ingroup CM_USER_API
 */

#ifndef COMPONENT_TYPE_H_
#define COMPONENT_TYPE_H_

#include <cm/inc/cm_type.h>
#include <nmf/inc/component_type.h>

/*!
 * @defgroup t_nmf_ee_priority t_nmf_ee_priority
 * \brief Identification of the execution engine priority and sub priority.
 * @{
 * \ingroup COMPONENT
 */
typedef t_uint32 t_nmf_ee_priority;                                      //!< Fake enumeration type

#define NMF_SCHED_BACKGROUND                    ((t_nmf_ee_priority)0)  //!< Background priority
#define NMF_SCHED_NORMAL                        ((t_nmf_ee_priority)1)  //!< Normal priority
#define NMF_SCHED_URGENT                        ((t_nmf_ee_priority)2)  //!< Urgent priority
/* @} */


/*!
 * \brief Identification of host component returned during introspection
 *
 * \ingroup COMPONENT_INTROSPECTION
 */
#define NMF_HOST_COMPONENT		((t_cm_instance_handle)0xFFFFFFFF)

/*!
 * \brief Identification of void component returned during introspection
 *
 * \ingroup COMPONENT_INTROSPECTION
 */
#define NMF_VOID_COMPONENT      ((t_cm_instance_handle)0xFFFFFFFE)


/*!
 * @defgroup t_nmf_ee_priority t_nmf_ee_priority
 * \brief Identification of the execution engine priority and sub priority.
 * @{
 * \ingroup COMPONENT
 */
typedef t_uint8 t_cm_require_state;                                     //!< Fake enumeration type

#define CM_REQUIRE_STATIC                    ((t_cm_require_state)0)  //!< Required interface is static
#define CM_REQUIRE_OPTIONAL                  ((t_cm_require_state)1)  //!< Required interface is optional
#define CM_REQUIRE_COLLECTION                ((t_cm_require_state)2)  //!< Required interface is a collection

/* @} */

#endif
