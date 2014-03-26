/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * This file is a staging solution and shall be integrated into
 * /include/net/bluetooth/hci.h.
 */

#ifndef __CG2900_HCI_H
#define __CG2900_HCI_H

#define HCI_EV_HW_ERROR			0x10
struct hci_ev_hw_error {
	__u8	hw_code;
} __packed;

#endif /* __CG2900_HCI_H */
