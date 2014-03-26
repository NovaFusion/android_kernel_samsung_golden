/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief CM Performance Meter Engine API.
 *
 * This file contains the Component Manager Performance Meter Engine API.
 */
#ifndef CM_ENGINE_PERFMETER_ENGINE_H_
#define CM_ENGINE_PERFMETER_ENGINE_H_

#include <cm/engine/perfmeter/inc/perfmeter_type.h>

/*!
 * \brief MPC cpu load
 *
 * \param[in] coreId identification of mpc from which we want cpu load
 * \param[out] mpcLoadCounter will contain mpc cpu load counters value if success
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_getMpcLoadCounter(
        t_nmf_core_id coreId,
        t_cm_mpc_load_counter *mpcLoadCounter);

/*!
 * \brief MPC cpu load
 * Same as \ref CM_ENGINE_getMpcLoadCounter() without lock
 *
 * \param[in] coreId identification of mpc from which we want cpu load
 * \param[out] mpcLoadCounter will contain mpc cpu load counters value if success
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_GetMpcLoadCounter(
        t_nmf_core_id coreId,
        t_cm_mpc_load_counter *mpcLoadCounter);

#endif /*CM_ENGINE_PERFMETER_ENGINE_H_*/
