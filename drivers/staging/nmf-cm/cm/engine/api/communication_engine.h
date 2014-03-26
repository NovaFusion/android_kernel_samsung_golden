/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Communication User Engine API.
 *
 * This file contains the Communication Engine API for manipulating components.
 *
 */
#ifndef COMMUNICATION_ENGINE_H_
#define COMMUNICATION_ENGINE_H_

#include <cm/engine/communication/inc/communication_type.h>

/*!
 * \brief Allocate Event buffer where parameters will be marshalled.
 *
 * In order to optimize call, this method don't need to be exported to user space,
 * but must be used by CM driver.
 *
 * See \ref HOST2MPC "Host->MPC binding" for seeing an integration example.
 *
 * \note This method is not called from user space!!!
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_event_params_handle CM_ENGINE_AllocEvent(t_cm_bf_host2mpc_handle host2mpcId);

/*!
 * \brief Push a event in Fifo.
 *
 * In order to optimize call, this method don't need to be exported to user space,
 * but must be used by CM driver.
 *
 * See \ref HOST2MPC "Host->MPC binding" for seeing an integration example.
 *
 * \note This method is not called from user space!!!
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_PushEvent(t_cm_bf_host2mpc_handle host2mpcId, t_event_params_handle h, t_uint32 methodIndex);

/*!
 * \brief Push a event in Fifo.
 *
 * In order to optimize call, this method need to be exported to user space
 * and must be implemented by CM driver.
 *
 * See \ref HOST2MPC "Host->MPC binding" for seeing an integration example.
 *
 * \note No implementation of this method is provided in kernel CM engine!!!
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC t_cm_error CM_ENGINE_PushEventWithSize(t_cm_bf_host2mpc_handle host2mpcId, t_event_params_handle h, t_uint32 size, t_uint32 methodIndex);

/*!
 * \brief Aknowledge a Fifo that the received event has been demarshalled.
 *
 * In order to optimize call, this method don't need to be exported to user space,
 * but must be used by CM driver.
 *
 * See \ref MPC2HOST "MPC->Host binding" for seeing an integration example.
 *
 * \note This method is not called from user space!!!
 *
 * \ingroup CM_ENGINE_API
 */
PUBLIC IMPORT_SHARED void CM_ENGINE_AcknowledgeEvent(t_cm_bf_mpc2host_handle mpc2hostId);

#endif /*COMMUNICATION_ENGINE_H_*/
