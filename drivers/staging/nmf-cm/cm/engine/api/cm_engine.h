/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief CM Engine API.
 *
 * This file contains the Component Manager Engine API.
 */

/*!
 * \defgroup CM_ENGINE_MODULE CM Engine
 */
/*!
 * \defgroup CM_ENGINE_API CM Engine API
 *
 * \note This API is not for user developers, this API is only an internal API.
 *
 * \warning All parameters in out from this API means that the parameter is a reference to a data that is complete by the call.
 *
 * This API is provided by CM Engine and shall be required by driver kernel part.
 * \ingroup CM_ENGINE_MODULE
 */

#ifndef CM_ENGINE_H_
#define CM_ENGINE_H_

#include <cm/engine/api/configuration_engine.h>

#include <cm/engine/api/component_engine.h>

#include <cm/engine/api/memory_engine.h>

#include <cm/engine/api/communication_engine.h>

#include <cm/engine/api/perfmeter_engine.h>

#include <cm/engine/api/executive_engine_mgt_engine.h>

#include <cm/engine/api/repository_mgt_engine.h>

#include <cm/engine/api/domain_engine.h>

#include <cm/engine/api/migration_engine.h>

#endif /*CM_ENGINE_H_*/

