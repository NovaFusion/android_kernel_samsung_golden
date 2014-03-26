/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header:
 *   netlink related functionality.
 */
#ifndef _MODEM_NETLINK_H_
#define _MODEM_NETLINK_H_

#include "modem_protocol.h"

bool ipc_create_netlink_socket(struct ipc_link_context *context);
void ipc_broadcast_modem_online(struct ipc_link_context *context);
void ipc_broadcast_modem_reset(struct ipc_link_context *context);

#endif /* _MODEM_NETLINK_H_ */
