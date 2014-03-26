/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header:
 *   statemachine functionality.
 */
#ifndef _MODEM_STATEMACHINE_H_
#define _MODEM_STATEMACHINE_H_

#include <linux/kernel.h>

/* valid states for the driver state machine */
enum ipc_sm_state_id {
	IPC_SM_INIT,
	IPC_SM_HALT,
	IPC_SM_RESET,
	IPC_SM_WAIT_SLAVE_STABLE,
	IPC_SM_WAIT_HANDSHAKE_INACTIVE,
	IPC_SM_SLW_TX_BOOTREQ,
	IPC_SM_ACT_TX_BOOTREQ,
	IPC_SM_SLW_RX_BOOTRESP,
	IPC_SM_ACT_RX_BOOTRESP,
	IPC_SM_IDL,
	IPC_SM_SLW_TX_WR_CMD,
	IPC_SM_ACT_TX_WR_CMD,
	IPC_SM_SLW_TX_WR_DAT,
	IPC_SM_ACT_TX_WR_DAT,
	IPC_SM_SLW_TX_RD_CMD,
	IPC_SM_ACT_TX_RD_CMD,
	IPC_SM_SLW_RX_WR_CMD,
	IPC_SM_ACT_RX_WR_CMD,
	IPC_SM_ACT_RX_WR_DAT,
	IPC_SM_STATE_ID_NBR
};

/* state machine trigger causes events */
#define IPC_SM_RUN_NONE         (0x00)
#define IPC_SM_RUN_SLAVE_IRQ    (0x01)
#define IPC_SM_RUN_TFR_COMPLETE (0x02)
#define IPC_SM_RUN_TX_REQ       (0x04)
#define IPC_SM_RUN_INIT         (0x08)
#define IPC_SM_RUN_ABORT        (0x10)
#define IPC_SM_RUN_COMMS_TMO    (0x20)
#define IPC_SM_RUN_STABLE_TMO   (0x40)
#define IPC_SM_RUN_RESET        (0x80)

struct ipc_link_context; /* forward declaration */

typedef u8 (*ipc_sm_enter_func)(u8 event, struct ipc_link_context *context);
typedef const struct ipc_sm_state *(*ipc_sm_exit_func)(u8 event,
					struct ipc_link_context *context);

struct ipc_sm_state {
	enum ipc_sm_state_id id;
	ipc_sm_enter_func enter;
	ipc_sm_exit_func exit;
	u8 events;
};

const struct ipc_sm_state *ipc_sm_idle_state(struct ipc_link_context *context);
const struct ipc_sm_state *ipc_sm_init_state(struct ipc_link_context *context);
const struct ipc_sm_state *ipc_sm_state(u8 id);
bool ipc_sm_valid_for_state(u8 event, const struct ipc_sm_state *state);

void ipc_sm_kick(u8 event, struct ipc_link_context *context);

#endif /* _MODEM_STATEMACHINE_H_ */
