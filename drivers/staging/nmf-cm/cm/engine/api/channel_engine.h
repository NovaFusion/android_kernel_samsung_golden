/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief Communication Component Manager internal API type.
 */

#ifndef CHANNEL_ENGINE_H
#define CHANNEL_ENGINE_H

#include <nmf/inc/channel_type.h>
#include <nmf/inc/service_type.h>
#include <cm/engine/communication/inc/communication_type.h>

/*!
 * \brief Internal channel identification.
 *
 * Same as t_nmf_channel meaning but this the channel used internaly by
 * OS Integration part
 *
 * \ingroup CM_OS_API
 */
typedef t_uint32 t_os_channel;

/*!
 * \brief Invalid value for os_channel
 *
 * Invalid value for os channel.
 *
 * \ingroup CM_OS_API
 */
#define NMF_OS_CHANNEL_INVALID_HANDLE       0xffffffff

/*!
 * \brief Structure used for storing required parameters for Interface Callback
 * messages.
 *
 * This struture is used internally by CM_GetMessage() and CM_ExecuteMessage() as
 * the message content in the given buffer.
 *
 * \ingroup CM_ENGINE_API
 */
typedef struct {
    t_nmf_mpc2host_handle       THIS;               //!< Context of interface implementation
    t_uint32                    methodIndex;        //!< Method index in interface
    char                        params[1];          //!< Is of variable length concretely
} t_interface_data;

/*!
 * \brief Structure used for storing required parameters for Service Callback
 * messages.
 *
 * This struture is used internally by CM_GetMessage() and CM_ExecuteMessage() as
 * the message content in the given buffer.
 *
 * \ingroup CM_ENGINE_API
 */
typedef struct {
    t_nmf_service_type type;        //!< Type of the service message
    t_nmf_service_data data;
} t_service_data;

typedef enum {
	MSG_INTERFACE,
	MSG_SERVICE
} t_message_type;

/*!
 * \brief Structure used for storing required parameters for the internal NMF
 * messages.
 *
 * This struture is used internally by CM_GetMessage() and CM_ExecuteMessage() as
 * the message content in the given buffer.
 *
 * \ingroup CM_ENGINE_API
 */
typedef struct {
    t_message_type type;           //!< Type of the nmf message
    union {
	t_interface_data    itf;
	t_service_data      srv;
    } data;
} t_os_message;

/*!
 * \brief Structure used for storing required parameters for the internal NMF
 * messages.
 *
 * This struture is used internally by CM_GetMessage() and CM_ExecuteMessage() as
 * the message content in the given buffer.
 *
 * \ingroup CM_ENGINE_API
 */
typedef struct {
    t_nmf_channel               channel;        //!< Channel (required to handle service message)
    t_os_message                osMsg;
} t_nmf_message;

#endif /* CHANNEL_ENGINE_H */
