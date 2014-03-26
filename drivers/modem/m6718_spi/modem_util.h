/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header:
 *   utility functionality.
 */
#ifndef _MODEM_UTIL_H_
#define _MODEM_UTIL_H_

#include <linux/kernel.h>
#include "modem_private.h"

bool ipc_util_channel_is_loopback(u8 channel);

u32 ipc_util_make_l2_header(u8 channel, u32 len);
u8 ipc_util_get_l2_channel(u32 hdr);
u32 ipc_util_get_l2_length(u32 hdr);
u32 ipc_util_make_l1_header(u8 cmd, u8 counter, u32 len);
u8 ipc_util_get_l1_cmd(u32 hdr);
u8 ipc_util_get_l1_counter(u32 hdr);
u32 ipc_util_get_l1_length(u32 hdr);
u8 ipc_util_get_l1_bootresp_ver(u32 bootresp);

int ipc_util_ss_level_active(struct ipc_link_context *context);
int ipc_util_ss_level_inactive(struct ipc_link_context *context);
int ipc_util_int_level_active(struct ipc_link_context *context);
int ipc_util_int_level_inactive(struct ipc_link_context *context);

void ipc_util_deactivate_ss(struct ipc_link_context *context);
void ipc_util_activate_ss(struct ipc_link_context *context);
void ipc_util_activate_ss_with_tmo(struct ipc_link_context *context);

bool ipc_util_int_is_active(struct ipc_link_context *context);

bool ipc_util_link_is_idle(struct ipc_link_context *context);

void ipc_util_start_slave_stable_timer(struct ipc_link_context *context);

void ipc_util_spi_message_prepare(struct ipc_link_context *link_context,
	void *tx_buf, void *rx_buf, int len);
void ipc_util_spi_message_init(struct ipc_link_context *link_context,
	void (*complete)(void *));

bool ipc_util_link_gpio_request(struct ipc_link_context *context,
	irqreturn_t (*irqhnd)(int, void *));
bool ipc_util_link_gpio_config(struct ipc_link_context *context);
bool ipc_util_link_gpio_unconfig(struct ipc_link_context *context);

bool ipc_util_link_is_suspended(struct ipc_link_context *context);
void ipc_util_suspend_link(struct ipc_link_context *context);
void ipc_util_resume_link(struct ipc_link_context *context);

#endif /* _MODEM_UTIL_H_ */
