/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief CM executive engine management Engine API.
 *
 * This file contains the Component Manager executive engine management Engine API.
 */
#ifndef CM_EXECUTIVE_ENGINE_MANAGEMENT_ENGINE_H_
#define CM_EXECUTIVE_ENGINE_MANAGEMENT_ENGINE_H_

#include <cm/inc/cm_type.h>

/*!
 * \brief Return executive engine handle for given core
 *
 * \param[in] coreId The core for which we want executive engine handle.
 * \param[out] executiveEngineHandle executive engine instance (null if the executive engine is not loaded)
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_GetExecutiveEngineHandle(
        t_cm_domain_id domainId,
        t_cm_instance_handle *executiveEngineHandle);

#endif /*CM_EXECUTIVE_ENGINE_MANAGEMENT_ENGINE_H_*/
