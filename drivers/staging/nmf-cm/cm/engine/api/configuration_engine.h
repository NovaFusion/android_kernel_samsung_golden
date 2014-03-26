/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Configuration Component Manager User Engine API.
 *
 * This file contains the Configuration CM Engine API for manipulating CM.
 *
 */

#ifndef CONFIGURATION_ENGINE_H
#define CONFIGURATION_ENGINE_H

#include <cm/engine/configuration/inc/configuration_type.h>

/*!
 * \brief Dynamically set some debug parameters of the CM
 *
 * \param[in] aCmdID The command for the parameter to update
 * \param[in] aParam The actual value to set for the given command
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_SetMode(t_cm_cmd_id aCmdID, t_sint32 aParam);

#endif /* CONFIGURATION_ENGINE_H */
