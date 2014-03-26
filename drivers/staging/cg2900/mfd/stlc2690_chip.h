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
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson STLC2690 BT/FM controller.
 */

#ifndef _STLC2690_CHIP_H_
#define _STLC2690_CHIP_H_

/* Supported chips */
#define STLC2690_SUPP_MANUFACTURER		0x30
#define STLC2690_SUPP_REVISION_MIN		0x0500
#define STLC2690_SUPP_REVISION_MAX		0x06FF

#define BT_SIZE_OF_HDR				(sizeof(__le16) + sizeof(__u8))
#define BT_PARAM_LEN(__pkt_len)			(__pkt_len - BT_SIZE_OF_HDR)

/* BT VS Store In FS command */
#define STLC2690_BT_OP_VS_STORE_IN_FS		0xFC22
struct bt_vs_store_in_fs_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	user_id;
	__u8	len;
	__u8	data[];
} __packed;

/* BT VS Write File Block command */
#define STLC2690_BT_OP_VS_WRITE_FILE_BLOCK	0xFC2E
struct bt_vs_write_file_block_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	id;
	__u8	data[];
} __packed;

/* User ID for storing BD address in chip using Store_In_FS command */
#define STLC2690_VS_STORE_IN_FS_USR_ID_BD_ADDR	0xFE

#endif /* _STLC2690_CHIP_H_ */
