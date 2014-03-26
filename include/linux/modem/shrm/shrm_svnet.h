/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __SHRM_SVNET_H
#define __SHRM_SVNET_H

#include <linux/modem/shrm/shrm_driver.h>
#include <linux/modem/shrm/shrm_private.h>
#include <linux/modem/shrm/shrm_config.h>
#include <linux/modem/shrm/shrm_net.h>
#include <linux/modem/shrm/shrm.h>

#define SHRMIPCCTRLCHNL 220
#define SHRMIPCDATACHNL 221


#if defined(CONFIG_U8500_SHRM_SVNET)
// To notify AP-state to CP
typedef struct {
   unsigned short len;
   unsigned char msg_seq;
   unsigned char ack_seq;
   unsigned char main_cmd;
   unsigned char sub_cmd;
   unsigned char cmd_type;
} __attribute__((packed)) ipc_fmt_hdr_type;

typedef struct{
ipc_fmt_hdr_type hdr;
unsigned char state;
}__attribute__((__packed__)) ipc_pwr_ape_state_evt_type;

typedef union{
ipc_fmt_hdr_type hdr;
ipc_pwr_ape_state_evt_type ape_state_evt;
}__attribute__((__packed__)) ipc_pwr_ape_state_type;
#endif

int shrm_register_msr_handlers(void (*modem_crash_cb)(void *),
		void (*modem_reinit_cb)(void *), void *cookie);
int shrm_register_handler(void *, u8 id, void (*rcvhandler)(u16 len, u8 *buf, void *data));
void shrm_unregister_handler(void);
int shrm_host_modem_msg_send(u8 id, void *data, u32 len);
typedef void (*shrm_msg_handler_t)(u16, u8 *, void *);
void shrm_init_svnet_if(struct shrm_dev *shrm);
#endif
