/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson CG2900 GPS/BT/FM controller.
 */

#ifndef _CG2900_CORE_H_
#define _CG2900_CORE_H_

#include <linux/device.h>
#include <linux/skbuff.h>

/* Reserve 1 byte for the HCI H:4 header */
#define HCI_H4_SIZE				1
#define CG2900_SKB_RESERVE			HCI_H4_SIZE

/* Number of bytes to reserve at start of sk_buffer when receiving packet */
#define RX_SKB_RESERVE				8

#define BT_BDADDR_SIZE				6

/* Standardized Bluetooth H:4 channels */
#define HCI_BT_CMD_H4_CHANNEL			0x01
#define HCI_BT_ACL_H4_CHANNEL			0x02
#define HCI_BT_SCO_H4_CHANNEL			0x03
#define HCI_BT_EVT_H4_CHANNEL			0x04

/* Default H4 channels which may change depending on connected controller */
#define HCI_FM_RADIO_H4_CHANNEL			0x08
#define HCI_GNSS_H4_CHANNEL			0x09

/* Bluetooth error codes */
#define HCI_BT_ERROR_NO_ERROR			0x00
#define HCI_BT_WRONG_SEQ_ERROR			0xF1

/* Bluetooth lengths */
#define HCI_BT_SEND_FILE_MAX_CHUNK_SIZE		254

#define LOGGER_DIRECTION_TX			0
#define LOGGER_DIRECTION_RX			1

/* module_param declared in cg2900_core.c */
extern u8 bd_address[BT_BDADDR_SIZE];

#endif /* _CG2900_CORE_H_ */
