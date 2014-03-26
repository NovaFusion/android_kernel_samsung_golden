/*
 * Copyright (C) ST-Ericsson SA 2010,2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * U9500 <-> M6718 IPC protocol implementation using SPI.
 *   state machine definition and functionality.
 */
#include <linux/modem/m6718_spi/modem_driver.h>
#include "modem_statemachine.h"
#include "modem_util.h"
#include "modem_netlink.h"
#include "modem_debug.h"
#include "modem_queue.h"
#include "modem_protocol.h"

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
#include "modem_state.h"
#endif

#define CMD_BOOTREQ  (1)
#define CMD_BOOTRESP (2)
#define CMD_WRITE    (3)
#define CMD_READ     (4)

static u8 sm_init_enter(u8 event, struct ipc_link_context *context)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
	/* if modem is off un-configure the IPC GPIO pins for low-power */
	if (modem_state_get_state() == MODEM_STATE_OFF) {
		dev_info(&context->sdev->dev,
			"link %d: modem is off, un-configuring GPIO\n",
			context->link->id);
		ipc_util_link_gpio_unconfig(context);
	}
#endif
	/* nothing more to do until an event happens */
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_init_exit(u8 event,
	struct ipc_link_context *context)
{
	bool int_active = false;

	/*
	 * For reset event just re-enter init in case the modem has
	 * powered off - we need to reconfigure our GPIO pins
	 */
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_INIT);

	/* re-sample link INT pin */
	int_active = ipc_util_int_is_active(context);
	atomic_set(&context->state_int, int_active);

	dev_info(&context->sdev->dev,
		"link %d: link initialised; SS:INACTIVE(%d) INT:%s(%d)\n",
		context->link->id,
		ipc_util_ss_level_inactive(context),
		int_active ? "ACTIVE" : "INACTIVE",
		int_active ? ipc_util_int_level_active(context) :
				ipc_util_int_level_inactive(context));

	/* handshake is only on link 0 */
	if (context->link->id == 0) {
		if (!int_active) {
			dev_info(&context->sdev->dev,
				"link %d: slave INT signal is inactive\n",
				context->link->id);
			/* start boot handshake */
			return ipc_sm_state(IPC_SM_SLW_TX_BOOTREQ);
		} else {
			/* wait for slave INT signal to stabilise inactive */
			return ipc_sm_state(IPC_SM_WAIT_SLAVE_STABLE);
		}
	} else {
		dev_info(&context->sdev->dev,
			"link %d: boot sync not needed, going idle\n",
			context->link->id);
		return ipc_sm_state(IPC_SM_IDL);
	}
}

static u8 sm_wait_slave_stable_enter(u8 event, struct ipc_link_context *context)
{
	static unsigned long printk_warn_time;
	if (printk_timed_ratelimit(&printk_warn_time, 60 * 1000))
		dev_info(&context->sdev->dev,
			"link %d: waiting for stable inactive slave INT\n",
			context->link->id);
	ipc_util_start_slave_stable_timer(context);
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_wait_slave_stable_exit(u8 event,
	struct ipc_link_context *context)
{
	if (!ipc_util_int_is_active(context)) {
		dev_info(&context->sdev->dev,
			"link %d: slave INT signal is stable inactive\n",
			context->link->id);
		return ipc_sm_state(IPC_SM_SLW_TX_BOOTREQ);
	} else {
		return ipc_sm_state(IPC_SM_WAIT_SLAVE_STABLE);
	}
}

static u8 sm_wait_handshake_inactive_enter(u8 event,
	struct ipc_link_context *context)
{
	dev_info(&context->sdev->dev,
		"link %d: waiting for stable inactive slave INT\n",
		context->link->id);
	ipc_util_start_slave_stable_timer(context);
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_wait_handshake_inactive_exit(u8 event,
	struct ipc_link_context *context)
{
	int i;

	if (!ipc_util_int_is_active(context)) {
		dev_info(&context->sdev->dev,
			"link %d: slave INT signal is inactive, going idle\n",
			context->link->id);

		/* modem sync is done */
		atomic_inc(&l1_context.boot_sync_done);
		ipc_broadcast_modem_online(context);

		/*
		 * Kick the state machine for any initialised links - skip link0
		 * since this link has just completed handshake
		 */
		for (i = 1; i < IPC_NBR_SUPPORTED_SPI_LINKS; i++)
			if (l1_context.device_context[i].state != NULL) {
				dev_dbg(&context->sdev->dev,
					"link %d has already been probed, "
					"kicking state machine\n", i);
				ipc_sm_kick(IPC_SM_RUN_INIT,
					&l1_context.device_context[i]);
			}
		return ipc_sm_state(IPC_SM_IDL);
	} else {
		return ipc_sm_state(IPC_SM_WAIT_HANDSHAKE_INACTIVE);
	}
}

static u8 sm_idl_enter(u8 event, struct ipc_link_context *context)
{
	ipc_util_deactivate_ss(context);
	ipc_dbg_enter_idle(context);

	/* check if tx queue contains items */
	if (atomic_read(&context->tx_q_count) > 0) {
		dev_dbg(&context->sdev->dev,
			"link %d: tx queue contains items\n",
			context->link->id);
		return IPC_SM_RUN_TX_REQ;
	}

	/* check if modem has already requested transaction start */
	if (atomic_read(&context->state_int)) {
		dev_dbg(&context->sdev->dev,
			"link %d: slave has already signalled ready\n",
			context->link->id);
		return IPC_SM_RUN_SLAVE_IRQ;
	}

	dev_dbg(&context->sdev->dev,
		"link %d: going idle\n", context->link->id);
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_idl_exit(u8 event,
	struct ipc_link_context *context)
{
	ipc_dbg_exit_idle(context);
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else if (event == IPC_SM_RUN_TX_REQ)
		return ipc_sm_state(IPC_SM_SLW_TX_WR_CMD);
	else if (event == IPC_SM_RUN_SLAVE_IRQ)
		return ipc_sm_state(IPC_SM_SLW_TX_RD_CMD);
	else
		return ipc_sm_state(IPC_SM_HALT);
}

static u8 sm_slw_tx_wr_cmd_enter(u8 event, struct ipc_link_context *context)
{
	struct ipc_tx_queue *frame;

	/* get the frame from the head of the tx queue */
	if (ipc_queue_is_empty(context)) {
		dev_err(&context->sdev->dev,
			"link %d error: tx queue is empty!\n",
			context->link->id);
		return IPC_SM_RUN_ABORT;
	}
	frame = ipc_queue_get_frame(context);
	ipc_dbg_dump_frame(&context->sdev->dev, context->link->id, frame, true);

	context->cmd = ipc_util_make_l1_header(CMD_WRITE, frame->counter,
		frame->len);

	dev_dbg(&context->sdev->dev,
		"link %d: TX FRAME cmd %08x (type %d counter %d len %d)\n",
		context->link->id,
		context->cmd,
		ipc_util_get_l1_cmd(context->cmd),
		ipc_util_get_l1_counter(context->cmd),
		ipc_util_get_l1_length(context->cmd));

	ipc_util_spi_message_prepare(context, &context->cmd,
		NULL, IPC_L1_HDR_SIZE);
	context->frame = frame;

	/* slave might already have signalled ready to transmit */
	if (atomic_read(&context->state_int)) {
		dev_dbg(&context->sdev->dev,
			"link %d: slave has already signalled ready\n",
			context->link->id);
		ipc_util_activate_ss(context);
		return IPC_SM_RUN_SLAVE_IRQ;
	} else {
		ipc_util_activate_ss_with_tmo(context);
		return IPC_SM_RUN_NONE;
	}
}

static const struct ipc_sm_state *sm_slw_tx_wr_cmd_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else if (event == IPC_SM_RUN_COMMS_TMO)
		return ipc_sm_state(IPC_SM_HALT);
	else
		return ipc_sm_state(IPC_SM_ACT_TX_WR_CMD);
}

static u8 sm_act_tx_wr_cmd_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* slave is ready - start the spi transfer */
	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_tx_wr_cmd_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else
		return ipc_sm_state(IPC_SM_SLW_TX_WR_DAT);
}

static u8 sm_slw_tx_wr_dat_enter(u8 event, struct ipc_link_context *context)
{
	/* prepare to transfer the frame tx data */
	ipc_util_spi_message_prepare(context, context->frame->data,
		NULL, context->frame->len);

	/* slave might already have signalled ready to transmit */
	if (atomic_read(&context->state_int)) {
		dev_dbg(&context->sdev->dev,
			"link %d: slave has already signalled ready\n",
			context->link->id);
		ipc_util_activate_ss(context);
		return IPC_SM_RUN_SLAVE_IRQ;
	} else {
		ipc_util_activate_ss_with_tmo(context);
		return IPC_SM_RUN_NONE;
	}
}

static const struct ipc_sm_state *sm_slw_tx_wr_dat_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else if (event == IPC_SM_RUN_COMMS_TMO)
		return ipc_sm_state(IPC_SM_HALT);
	else
		return ipc_sm_state(IPC_SM_ACT_TX_WR_DAT);
}

static u8 sm_act_tx_wr_dat_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* slave is ready - start the spi transfer */
	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_tx_wr_dat_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	/* frame is sent, increment link tx counter */
	context->tx_bytes += context->frame->actual_len;
#endif
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_VERIFY_FRAMES
	{
		u8 channel;

		channel = ipc_util_get_l2_channel(*(u32 *)context->frame->data);
		if (ipc_util_channel_is_loopback(channel)) {
			context->last_frame = context->frame;
		} else {
			ipc_queue_delete_frame(context->frame);
			context->frame = NULL;
		}
	}
#else
	/* free the sent frame */
	ipc_queue_delete_frame(context->frame);
	context->frame = NULL;
#endif
	return ipc_sm_state(IPC_SM_SLW_TX_RD_CMD);
}

static u8 sm_slw_tx_rd_cmd_enter(u8 event, struct ipc_link_context *context)
{
	context->cmd = ipc_util_make_l1_header(CMD_READ, 0, 0);
	dev_dbg(&context->sdev->dev,
		"link %d: cmd %08x (type %d)\n",
		context->link->id,
		context->cmd,
		ipc_util_get_l1_cmd(context->cmd));

	/* prepare the spi message to transfer */
	ipc_util_spi_message_prepare(context, &context->cmd,
		NULL, IPC_L1_HDR_SIZE);

	/* check if the slave requested this transaction */
	if (event == IPC_SM_RUN_SLAVE_IRQ) {
		dev_dbg(&context->sdev->dev,
			"link %d: slave initiated transaction, continue\n",
			context->link->id);
		ipc_util_activate_ss(context);
		return IPC_SM_RUN_SLAVE_IRQ;
	} else {
		/* slave might already have signalled ready to transmit */
		if (atomic_read(&context->state_int)) {
			dev_dbg(&context->sdev->dev,
				"link %d: slave has already signalled ready\n",
				context->link->id);
			ipc_util_activate_ss(context);
			return IPC_SM_RUN_SLAVE_IRQ;
		} else {
			ipc_util_activate_ss_with_tmo(context);
			return IPC_SM_RUN_NONE;
		}
	}
}

static const struct ipc_sm_state *sm_slw_tx_rd_cmd_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else if (event == IPC_SM_RUN_COMMS_TMO)
		return ipc_sm_state(IPC_SM_HALT);
	else
		return ipc_sm_state(IPC_SM_ACT_TX_RD_CMD);
}

static u8 sm_act_tx_rd_cmd_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* slave is ready - start the spi transfer */
	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_tx_rd_cmd_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else
		return ipc_sm_state(IPC_SM_SLW_RX_WR_CMD);
}

static u8 sm_slw_rx_wr_cmd_enter(u8 event, struct ipc_link_context *context)
{
	/* prepare to receive MESSAGE WRITE frame header */
	ipc_util_spi_message_prepare(context, NULL,
		&context->cmd, IPC_L1_HDR_SIZE);

	/* slave might already have signalled ready to transmit */
	if (atomic_read(&context->state_int)) {
		dev_dbg(&context->sdev->dev,
			"link %d: slave has already signalled ready\n",
			context->link->id);
		ipc_util_activate_ss(context);
		return IPC_SM_RUN_SLAVE_IRQ;
	} else {
		ipc_util_activate_ss_with_tmo(context);
		return IPC_SM_RUN_NONE;
	}
}

static const struct ipc_sm_state *sm_slw_rx_wr_cmd_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);
	else if (event == IPC_SM_RUN_COMMS_TMO)
		return ipc_sm_state(IPC_SM_HALT);
	else
		return ipc_sm_state(IPC_SM_ACT_RX_WR_CMD);
}

static u8 sm_act_rx_wr_cmd_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* slave is ready - start the spi transfer */
	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_rx_wr_cmd_exit(u8 event,
	struct ipc_link_context *context)
{
	u8 cmd_type = ipc_util_get_l1_cmd(context->cmd);
	int counter = ipc_util_get_l1_counter(context->cmd);
	int length  = ipc_util_get_l1_length(context->cmd);

	dev_dbg(&context->sdev->dev,
		"link %d: RX HEADER %08x (type %d counter %d length %d)\n",
		context->link->id,
		context->cmd,
		cmd_type,
		counter,
		length);

	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);

	if (cmd_type == CMD_WRITE) {
		/* slave has data to send - allocate a frame to hold it */
		context->frame = ipc_queue_new_frame(context, length);
		if (context->frame == NULL)
			return ipc_sm_state(IPC_SM_IDL);

		context->frame->counter = counter;
		ipc_util_spi_message_prepare(context, NULL,
			context->frame->data, context->frame->len);
		return ipc_sm_state(IPC_SM_ACT_RX_WR_DAT);
	} else {
		if (cmd_type != 0)
			dev_err(&context->sdev->dev,
				"link %d error: received invalid frame type %x "
				"(%08x)! assuming TRANSACTION_END...\n",
				context->link->id,
				cmd_type,
				context->cmd);

		/* slave has no data to send */
		dev_dbg(&context->sdev->dev,
			"link %d: slave has no data to send\n",
			context->link->id);
		return ipc_sm_state(IPC_SM_IDL);
	}
}

static u8 sm_act_rx_wr_dat_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* assume slave is still ready - prepare and start the spi transfer */
	ipc_util_spi_message_prepare(context, NULL,
		context->frame->data, context->frame->len);

	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_rx_wr_dat_exit(u8 event,
	struct ipc_link_context *context)
{
	u32           frame_hdr;
	unsigned char l2_header;
	unsigned int  l2_length;
	u8            *l2_data;

	if (event == IPC_SM_RUN_RESET)
		return ipc_sm_state(IPC_SM_RESET);

	dev_dbg(&context->sdev->dev,
		"link %d: RX PAYLOAD %d bytes\n",
		context->link->id, context->frame->len);

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	/* frame is received, increment link rx counter */
	context->rx_bytes += context->frame->len;
#endif
	/* decode L2 header */
	frame_hdr = *(u32 *)context->frame->data;
	l2_header = ipc_util_get_l2_channel(frame_hdr);
	l2_length = ipc_util_get_l2_length(frame_hdr);
	l2_data   = (u8 *)context->frame->data + IPC_L2_HDR_SIZE;

	context->frame->actual_len = l2_length + IPC_L2_HDR_SIZE;
	ipc_dbg_dump_frame(&context->sdev->dev, context->link->id,
		context->frame, false);

	if (l2_length > (context->frame->len - 4)) {
		dev_err(&context->sdev->dev,
			"link %d: suspicious frame: L1 len %d L2 len %d\n",
			context->link->id, context->frame->len, l2_length);
	}

	dev_dbg(&context->sdev->dev,
		"link %d: L2 PDU decode: header 0x%08x channel %d length %d "
		"data[%02x%02x%02x...]\n",
		context->link->id, frame_hdr, l2_header, l2_length,
		l2_data[0], l2_data[1], l2_data[2]);

	if (ipc_util_channel_is_loopback(l2_header))
		ipc_dbg_verify_rx_frame(context);

	/* pass received frame up to L2mux layer */
	if (!modem_protocol_channel_is_open(l2_header)) {
		dev_err(&context->sdev->dev,
			"link %d error: received frame on invalid channel %d, "
			"frame discarded\n",
			context->link->id, l2_header);
	} else {
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
		/*
		 * Discard loopback frames if we are taking throughput
		 * measurements - we'll be loading the links and so will likely
		 * overload the buffers.
		 */
		if (!ipc_util_channel_is_loopback(l2_header))
#endif
			modem_m6718_spi_receive(context->sdev,
				l2_header, l2_length, l2_data);
	}

	/* data is copied by L2mux so free the frame here */
	ipc_queue_delete_frame(context->frame);
	context->frame = NULL;

	/* check tx queue for content */
	if (!ipc_queue_is_empty(context)) {
		dev_dbg(&context->sdev->dev,
			"link %d: tx queue not empty\n", context->link->id);
		return ipc_sm_state(IPC_SM_SLW_TX_WR_CMD);
	} else {
		dev_dbg(&context->sdev->dev,
			"link %d: tx queue empty\n", context->link->id);
		return ipc_sm_state(IPC_SM_SLW_TX_RD_CMD);
	}
}

static u8 sm_halt_enter(u8 event, struct ipc_link_context *context)
{
	dev_err(&context->sdev->dev,
		"link %d error: HALTED\n", context->link->id);

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
	/*
	 * Force modem reset, this will cause a reset event from the modemstate
	 * driver which will reset the links. If debugfs is enabled then there
	 * is a userspace file which controls whether MSR is enabled or not.
	 */
#ifdef CONFIG_DEBUG_FS
	if (l1_context.msr_disable) {
		dev_info(&context->sdev->dev,
			"link %d: MSR is disabled by user, "
			"not requesting modem reset\n", context->link->id);
		return IPC_SM_RUN_RESET;
	}
#endif
	modem_state_force_reset();
#endif
	return IPC_SM_RUN_RESET;
}

static const struct ipc_sm_state *sm_halt_exit(u8 event,
	struct ipc_link_context *context)
{
	return ipc_sm_state(IPC_SM_RESET);
}

static u8 sm_reset_enter(u8 event, struct ipc_link_context *context)
{
	dev_err(&context->sdev->dev,
		"link %d resetting\n", context->link->id);

	if (context->link->id == 0)
		ipc_broadcast_modem_reset(context);

	ipc_util_deactivate_ss(context);
	ipc_queue_reset(context);
	if (context->frame != NULL) {
		ipc_queue_delete_frame(context->frame);
		context->frame = NULL;
	}
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_VERIFY_FRAMES
	if (context->last_frame != NULL) {
		ipc_queue_delete_frame(context->last_frame);
		context->last_frame = NULL;
	}
#endif
	dev_dbg(&context->sdev->dev,
		"link %d reset completed\n", context->link->id);

	return IPC_SM_RUN_RESET;
}

static const struct ipc_sm_state *sm_reset_exit(u8 event,
	struct ipc_link_context *context)
{
	return ipc_sm_state(IPC_SM_INIT);
}

static u8 sm_slw_tx_bootreq_enter(u8 event, struct ipc_link_context *context)
{
	dev_info(&context->sdev->dev,
		"link %d: waiting for boot sync\n", context->link->id);

	ipc_util_activate_ss(context);
	context->cmd = ipc_util_make_l1_header(CMD_BOOTREQ, 0,
		IPC_DRIVER_VERSION);
	dev_dbg(&context->sdev->dev,
		"link %d: TX HEADER cmd %08x (type %x)\n",
		context->link->id,
		context->cmd,
		ipc_util_get_l1_cmd(context->cmd));
	ipc_util_spi_message_prepare(context, &context->cmd,
		NULL, IPC_L1_HDR_SIZE);

	/* wait now for the slave to indicate ready... */
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_slw_tx_bootreq_exit(u8 event,
	struct ipc_link_context *context)
{
	return ipc_sm_state(IPC_SM_ACT_TX_BOOTREQ);
}

static u8 sm_act_tx_bootreq_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* slave is ready - start the spi transfer */
	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_tx_bootreq_exit(u8 event,
	struct ipc_link_context *context)
{
	return ipc_sm_state(IPC_SM_SLW_RX_BOOTRESP);
}

static u8 sm_slw_rx_bootresp_enter(u8 event, struct ipc_link_context *context)
{
	/* prepare to receive BOOTRESP frame header */
	ipc_util_spi_message_prepare(context, NULL,
		&context->cmd, IPC_L1_HDR_SIZE);

	/* slave might already have signalled ready to transmit */
	if (atomic_read(&context->state_int)) {
		dev_dbg(&context->sdev->dev,
			"link %d: slave has already signalled ready\n",
			context->link->id);
		ipc_util_activate_ss(context);
		return IPC_SM_RUN_SLAVE_IRQ;
	} else {
		ipc_util_activate_ss_with_tmo(context);
		return IPC_SM_RUN_NONE;
	}
}

static const struct ipc_sm_state *sm_slw_rx_bootresp_exit(u8 event,
	struct ipc_link_context *context)
{
	if (event == IPC_SM_RUN_COMMS_TMO) {
		/*
		 * Modem timeout: was it really ready or just noise?
		 * Revert to waiting for handshake to start.
		 */
		ipc_util_deactivate_ss(context);
		return ipc_sm_state(IPC_SM_SLW_TX_BOOTREQ);
	} else {
		return ipc_sm_state(IPC_SM_ACT_RX_BOOTRESP);
	}
}

static u8 sm_act_rx_bootresp_enter(u8 event, struct ipc_link_context *context)
{
	int err;

	/* slave is ready - start the spi transfer */
	dev_dbg(&context->sdev->dev,
		"link %d: starting spi tfr\n", context->link->id);
	err = spi_async(context->sdev, &context->spi_message);
	if (err < 0) {
		dev_err(&context->sdev->dev,
			"link %d error: spi tfr start failed, error %d\n",
			context->link->id, err);
		return IPC_SM_RUN_ABORT;
	}
	return IPC_SM_RUN_NONE;
}

static const struct ipc_sm_state *sm_act_rx_bootresp_exit(u8 event,
	struct ipc_link_context *context)
{
	u8 cmd_type = ipc_util_get_l1_cmd(context->cmd);
	u8 modem_ver;

	dev_dbg(&context->sdev->dev,
		"link %d: RX HEADER %08x (type %d)\n",
		context->link->id, context->cmd, cmd_type);

	if (cmd_type == CMD_BOOTRESP) {
		modem_ver = ipc_util_get_l1_bootresp_ver(context->cmd);

		dev_info(&context->sdev->dev,
			"link %d: boot sync done; "
			"APE version %02x, MODEM version %02x\n",
			context->link->id, IPC_DRIVER_VERSION, modem_ver);

		/* check for minimum required modem version */
		if (modem_ver != IPC_DRIVER_MODEM_MIN_VER) {
			dev_warn(&context->sdev->dev,
				"link %d warning: modem version mismatch! "
				"required version is %02x\n",
				context->link->id,
				IPC_DRIVER_MODEM_MIN_VER);
		}

		return ipc_sm_state(IPC_SM_WAIT_HANDSHAKE_INACTIVE);
	} else {
		/* invalid response... this is not our slave */
		dev_err(&context->sdev->dev,
			"link %d error: expected %x (BOOTRESP), received %x.\n",
			context->link->id,
			CMD_BOOTRESP,
			cmd_type);
		return ipc_sm_state(IPC_SM_HALT);
	}
}

/* the driver protocol state machine */
static const struct ipc_sm_state state_machine[IPC_SM_STATE_ID_NBR] = {
	[IPC_SM_INIT] = {
		.id     = IPC_SM_INIT,
		.enter  = sm_init_enter,
		.exit   = sm_init_exit,
		.events = IPC_SM_RUN_INIT | IPC_SM_RUN_RESET
	},
	[IPC_SM_HALT] = {
		.id     = IPC_SM_HALT,
		.enter  = sm_halt_enter,
		.exit   = sm_halt_exit,
		.events = IPC_SM_RUN_RESET
	},
	[IPC_SM_RESET] = {
		.id     = IPC_SM_RESET,
		.enter  = sm_reset_enter,
		.exit   = sm_reset_exit,
		.events = IPC_SM_RUN_RESET
	},
	[IPC_SM_WAIT_SLAVE_STABLE] = {
		.id     = IPC_SM_WAIT_SLAVE_STABLE,
		.enter  = sm_wait_slave_stable_enter,
		.exit   = sm_wait_slave_stable_exit,
		.events = IPC_SM_RUN_STABLE_TMO
	},
	[IPC_SM_WAIT_HANDSHAKE_INACTIVE] = {
		.id     = IPC_SM_WAIT_HANDSHAKE_INACTIVE,
		.enter  = sm_wait_handshake_inactive_enter,
		.exit   = sm_wait_handshake_inactive_exit,
		.events = IPC_SM_RUN_STABLE_TMO
	},
	[IPC_SM_SLW_TX_BOOTREQ] = {
		.id     = IPC_SM_SLW_TX_BOOTREQ,
		.enter  = sm_slw_tx_bootreq_enter,
		.exit   = sm_slw_tx_bootreq_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ
	},
	[IPC_SM_ACT_TX_BOOTREQ] = {
		.id     = IPC_SM_ACT_TX_BOOTREQ,
		.enter  = sm_act_tx_bootreq_enter,
		.exit   = sm_act_tx_bootreq_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE
	},
	[IPC_SM_SLW_RX_BOOTRESP] = {
		.id     = IPC_SM_SLW_RX_BOOTRESP,
		.enter  = sm_slw_rx_bootresp_enter,
		.exit   = sm_slw_rx_bootresp_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ | IPC_SM_RUN_COMMS_TMO
	},
	[IPC_SM_ACT_RX_BOOTRESP] = {
		.id     = IPC_SM_ACT_RX_BOOTRESP,
		.enter  = sm_act_rx_bootresp_enter,
		.exit   = sm_act_rx_bootresp_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE
	},
	[IPC_SM_IDL] = {
		.id     = IPC_SM_IDL,
		.enter  = sm_idl_enter,
		.exit   = sm_idl_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ | IPC_SM_RUN_TX_REQ |
				IPC_SM_RUN_RESET
	},
	[IPC_SM_SLW_TX_WR_CMD] = {
		.id     = IPC_SM_SLW_TX_WR_CMD,
		.enter  = sm_slw_tx_wr_cmd_enter,
		.exit   = sm_slw_tx_wr_cmd_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ | IPC_SM_RUN_COMMS_TMO |
				IPC_SM_RUN_RESET
	},
	[IPC_SM_ACT_TX_WR_CMD] = {
		.id     = IPC_SM_ACT_TX_WR_CMD,
		.enter  = sm_act_tx_wr_cmd_enter,
		.exit   = sm_act_tx_wr_cmd_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE | IPC_SM_RUN_RESET
	},
	[IPC_SM_SLW_TX_WR_DAT] = {
		.id     = IPC_SM_SLW_TX_WR_DAT,
		.enter  = sm_slw_tx_wr_dat_enter,
		.exit   = sm_slw_tx_wr_dat_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ | IPC_SM_RUN_COMMS_TMO |
				IPC_SM_RUN_RESET
	},
	[IPC_SM_ACT_TX_WR_DAT] = {
		.id     = IPC_SM_ACT_TX_WR_DAT,
		.enter  = sm_act_tx_wr_dat_enter,
		.exit   = sm_act_tx_wr_dat_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE | IPC_SM_RUN_RESET
	},
	[IPC_SM_SLW_TX_RD_CMD] = {
		.id     = IPC_SM_SLW_TX_RD_CMD,
		.enter  = sm_slw_tx_rd_cmd_enter,
		.exit   = sm_slw_tx_rd_cmd_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ | IPC_SM_RUN_COMMS_TMO |
				IPC_SM_RUN_RESET
	},
	[IPC_SM_ACT_TX_RD_CMD] = {
		.id     = IPC_SM_ACT_TX_RD_CMD,
		.enter  = sm_act_tx_rd_cmd_enter,
		.exit   = sm_act_tx_rd_cmd_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE | IPC_SM_RUN_RESET
	},
	[IPC_SM_SLW_RX_WR_CMD] = {
		.id     = IPC_SM_SLW_RX_WR_CMD,
		.enter  = sm_slw_rx_wr_cmd_enter,
		.exit   = sm_slw_rx_wr_cmd_exit,
		.events = IPC_SM_RUN_SLAVE_IRQ | IPC_SM_RUN_COMMS_TMO |
				IPC_SM_RUN_RESET
	},
	[IPC_SM_ACT_RX_WR_CMD] = {
		.id     = IPC_SM_ACT_RX_WR_CMD,
		.enter  = sm_act_rx_wr_cmd_enter,
		.exit   = sm_act_rx_wr_cmd_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE | IPC_SM_RUN_RESET
	},
	[IPC_SM_ACT_RX_WR_DAT] = {
		.id     = IPC_SM_ACT_RX_WR_DAT,
		.enter  = sm_act_rx_wr_dat_enter,
		.exit   = sm_act_rx_wr_dat_exit,
		.events = IPC_SM_RUN_TFR_COMPLETE | IPC_SM_RUN_RESET
	},
};

const struct ipc_sm_state *ipc_sm_idle_state(struct ipc_link_context *context)
{
	return ipc_sm_state(IPC_SM_IDL);
}

const struct ipc_sm_state *ipc_sm_init_state(struct ipc_link_context *context)
{
	return ipc_sm_state(IPC_SM_INIT);
}

const struct ipc_sm_state *ipc_sm_state(u8 id)
{
	BUG_ON(id >= IPC_SM_STATE_ID_NBR);
	return &state_machine[id];
}

bool ipc_sm_valid_for_state(u8 event, const struct ipc_sm_state *state)
{
	return (state->events & event) == event;
}

static void state_machine_run(struct ipc_link_context *context, u8 event)
{
	struct modem_m6718_spi_link_platform_data *link = context->link;
	struct spi_device *sdev = context->sdev;
	const struct ipc_sm_state *cur_state = context->state;

	/* some sanity checking */
	if (context == NULL || link == NULL || cur_state == NULL) {
		pr_err("M6718 IPC protocol error: "
			"inconsistent driver state, ignoring event\n");
		return;
	}

	dev_dbg(&sdev->dev, "link %d: RUNNING in %s (%s)\n", link->id,
		ipc_dbg_state_id(cur_state), ipc_dbg_event(event));

	/* valid trigger event for current state? */
	if (!ipc_sm_valid_for_state(event, cur_state)) {
		dev_dbg(&sdev->dev,
			"link %d: ignoring invalid event\n", link->id);
		ipc_dbg_ignoring_event(context, event);
		return;
	}
	ipc_dbg_handling_event(context, event);

	/* run machine while state entry functions trigger new changes */
	do {
		if (event == IPC_SM_RUN_SLAVE_IRQ &&
			!ipc_util_int_is_active(context)) {
			dev_err(&sdev->dev,
				"link %d error: slave is not ready! (%s)",
				link->id,
				ipc_dbg_state_id(cur_state));
		}

		if (event == IPC_SM_RUN_ABORT) {
			dev_err(&sdev->dev,
				"link %d error: abort event\n", link->id);
			/* reset state to idle */
			context->state = ipc_sm_idle_state(context);
			break;
		} else {
			/* exit current state */
			dev_dbg(&sdev->dev, "link %d: exit %s (%s)\n",
				link->id, ipc_dbg_state_id(cur_state),
				ipc_dbg_event(event));
			cur_state = cur_state->exit(event, context);
			context->state = cur_state;
		}

		/* reset state of slave irq to prepare for next event */
		if (event == IPC_SM_RUN_SLAVE_IRQ)
			atomic_set(&context->state_int, 0);

		/* enter new state */
		dev_dbg(&sdev->dev, "link %d: enter %s (%s)\n", link->id,
			ipc_dbg_state_id(cur_state), ipc_dbg_event(event));
		event = context->state->enter(event, context);
		ipc_dbg_entering_state(context);
	} while (event != IPC_SM_RUN_NONE);

	dev_dbg(&sdev->dev, "link %d: STOPPED in %s\n", link->id,
		ipc_dbg_state_id(cur_state));
}

void ipc_sm_kick(u8 event, struct ipc_link_context *context)
{
	unsigned long flags;
	struct modem_m6718_spi_link_platform_data *link = context->link;
	struct spi_device *sdev = context->sdev;
	struct spi_message *msg = &context->spi_message;
	u8 i;

	spin_lock_irqsave(&context->sm_lock, flags);
	switch (event) {
	case IPC_SM_RUN_SLAVE_IRQ:
		dev_dbg(&sdev->dev,
			"link %d EVENT: slave-ready irq\n", link->id);
		del_timer(&context->comms_timer);
		atomic_set(&context->state_int,
			ipc_util_int_is_active(context));
		break;

	case IPC_SM_RUN_TFR_COMPLETE:
		dev_dbg(&sdev->dev,
			"link %d EVENT: spi tfr complete (status %d len %d)\n",
			link->id, msg->status, msg->actual_length);
		ipc_dbg_dump_spi_tfr(context);
		break;

	case IPC_SM_RUN_COMMS_TMO:
	{
		char *statestr;
		struct ipc_link_context *contexts = l1_context.device_context;

		statestr = ipc_dbg_link_state_str(context);
		dev_err(&sdev->dev,
			"link %d EVENT: modem comms timeout (%s)!\n",
			link->id, ipc_dbg_state_id(context->state));
		if (statestr != NULL) {
			dev_err(&sdev->dev, "%s", statestr);
			kfree(statestr);
		}

		/* cancel all link timeout timers except this one */
		for (i = 0; i < IPC_NBR_SUPPORTED_SPI_LINKS; i++)
			if (contexts[i].link->id != link->id)
				del_timer(&contexts[i].comms_timer);
		break;
	}

	case IPC_SM_RUN_STABLE_TMO:
		dev_dbg(&sdev->dev,
			"link %d EVENT: slave-stable timeout\n", link->id);
		break;

	case IPC_SM_RUN_RESET:
		dev_dbg(&sdev->dev,
			"link %d EVENT: reset\n", link->id);
		del_timer(&context->comms_timer);
		break;

	default:
		break;
	}

	if (!ipc_util_link_is_suspended(context))
		state_machine_run(context, event);
	else
		dev_dbg(&sdev->dev,
			"link %d is suspended, waiting for resume\n", link->id);
	spin_unlock_irqrestore(&context->sm_lock, flags);
}
