/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header:
 *   debug functionality.
 */
#ifndef _MODEM_DEBUG_H_
#define _MODEM_DEBUG_H_

#include "modem_private.h"

void ipc_dbg_dump_frame(struct device *dev, int linkid,
	struct ipc_tx_queue *frame, bool tx);
void ipc_dbg_dump_spi_tfr(struct ipc_link_context *context);
const char *ipc_dbg_state_id(const struct ipc_sm_state *state);
const char *ipc_dbg_event(u8 event);
char *ipc_dbg_link_state_str(struct ipc_link_context *context);
void ipc_dbg_verify_rx_frame(struct ipc_link_context *context);

void ipc_dbg_debugfs_init(void);
void ipc_dbg_debugfs_link_init(struct ipc_link_context *context);

void ipc_dbg_ignoring_event(struct ipc_link_context *context, u8 event);
void ipc_dbg_handling_event(struct ipc_link_context *context, u8 event);
void ipc_dbg_entering_state(struct ipc_link_context *context);
void ipc_dbg_enter_idle(struct ipc_link_context *context);
void ipc_dbg_exit_idle(struct ipc_link_context *context);
void ipc_dbg_measure_throughput(unsigned long unused);
void ipc_dbg_throughput_init(void);
void ipc_dbg_throughput_link_init(struct ipc_link_context *context);

#endif /* _MODEM_DEBUG_H_ */
