/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Public Component Manager Performance Meter API type.
 *
 * This file contains the Component Manager API type for performance meter.
 *
 * \defgroup PERFMETER CM Monitoring API
 * \ingroup CM_USER_API
 */
#ifndef CM_COMMON_PERFMETER_TYPE_H_
#define CM_COMMON_PERFMETER_TYPE_H_

#include <cm/inc/cm_type.h>
/*!
 * \brief Description of mpc load structure.
 *
 * This contain mpc load value.
 *
 * \ingroup PERFMETER
 */
typedef struct {
    t_uint64 totalCounter;
    t_uint64 loadCounter;
} t_cm_mpc_load_counter;


#endif /* CM_COMMON_PERFMETER_TYPE_H_ */
