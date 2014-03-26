/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * This file is a staging solution and shall be integrated into
 * /drivers/bluetooth/hci_uart.h.
 *
 * Original hci_uart.h file:
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
 */

/*
 * Staging CG2900 Bluetooth HCI UART. Will be replaced by normal N_HCI when
 * moved to normal driver folder.
 */
#ifndef N_CG2900_HCI
#define N_CG2900_HCI		23
#endif /* N_CG2900_HCI */

/* Ioctls */
#define HCIUARTSETPROTO		_IOW('U', 200, int)
#define HCIUARTGETPROTO		_IOR('U', 201, int)
#define HCIUARTGETDEVICE	_IOR('U', 202, int)
#define HCIUARTSETFLAGS		_IOW('U', 203, int)
#define HCIUARTGETFLAGS		_IOR('U', 204, int)

/* UART protocols */
#define HCI_UART_MAX_PROTO	7

#define HCI_UART_H4	0
#define HCI_UART_BCSP	1
#define HCI_UART_3WIRE	2
#define HCI_UART_H4DS	3
#define HCI_UART_LL	4
#define HCI_UART_ATH3K	5
#define HCI_UART_STE	6

#define HCI_UART_RAW_DEVICE	0

/* UART break and flow control parameters */
#define BREAK_ON		true
#define BREAK_OFF		false
#define FLOW_ON			true
#define FLOW_OFF		false

struct hci_uart;

struct hci_uart_proto {
	unsigned int id;
	int (*open)(struct hci_uart *hu);
	int (*close)(struct hci_uart *hu);
	int (*flush)(struct hci_uart *hu);
	int (*recv)(struct hci_uart *hu, void *data, int len);
	int (*enqueue)(struct hci_uart *hu, struct sk_buff *skb);
	struct sk_buff *(*dequeue)(struct hci_uart *hu);
	bool register_hci_dev;
	struct device *dev;
};

struct hci_uart {
	struct tty_struct	*tty;
	struct hci_dev		*hdev;
	unsigned long		flags;
	unsigned long		hdev_flags;

	struct hci_uart_proto	*proto;
	void			*priv;

	struct sk_buff		*tx_skb;
	unsigned long		tx_state;
	spinlock_t		rx_lock;

	struct file		*fd;
};

/* HCI_UART proto flag bits */
#define HCI_UART_PROTO_SET	0

/* TX states  */
#define HCI_UART_SENDING	1
#define HCI_UART_TX_WAKEUP	2

int cg2900_hci_uart_register_proto(struct hci_uart_proto *p);
int cg2900_hci_uart_unregister_proto(struct hci_uart_proto *p);
int cg2900_hci_uart_tx_wakeup(struct hci_uart *hu);
int cg2900_hci_uart_set_baudrate(struct hci_uart *hu, int baud);
int cg2900_hci_uart_set_break(struct hci_uart *hu, bool break_on);
int cg2900_hci_uart_tiocmget(struct hci_uart *hu);
void cg2900_hci_uart_flush_buffer(struct hci_uart *hu);
void cg2900_hci_uart_flow_ctrl(struct hci_uart *hu, bool flow_on);
int cg2900_hci_uart_chars_in_buffer(struct hci_uart *hu);

#define hci_uart_register_proto cg2900_hci_uart_register_proto
#define hci_uart_unregister_proto cg2900_hci_uart_unregister_proto
#define hci_uart_tx_wakeup cg2900_hci_uart_tx_wakeup
#define hci_uart_set_baudrate cg2900_hci_uart_set_baudrate
#define hci_uart_set_break cg2900_hci_uart_set_break
#define hci_uart_tiocmget cg2900_hci_uart_tiocmget
#define hci_uart_flush_buffer cg2900_hci_uart_flush_buffer
#define hci_uart_flow_ctrl cg2900_hci_uart_flow_ctrl
#define hci_uart_chars_in_buffer cg2900_hci_uart_chars_in_buffer
