/*
 * Data types and interface for TEE application for starting the modem.
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef TEE_TA_START_MODEM_H
#define TEE_TA_START_MODEM_H

#define COMMAND_ID_START_MODEM		0x00000001

#define UUID_TEE_TA_START_MODEM_LOW	0x8AD94107
#define UUID_TEE_TA_START_MODEM_MID	0x6E50
#define UUID_TEE_TA_START_MODEM_HIGH	0x418E
#define UUID_TEE_TA_START_MODEM_CLOCKSEQ \
	{0xB1, 0x14, 0x75, 0x7D, 0x60, 0x21, 0xBD, 0x36}

struct mcore_segment_descr {
	void *segment;
	void *hash;
	u32 size;
};

struct access_image_descr {
	void *elf_hdr;
	void *pgm_hdr_tbl;
	void *signature;
	unsigned long nbr_segment;
	struct mcore_segment_descr *descr;
};

/* TODO: To be redefined with only info needed by Secure world. */
struct tee_ta_start_modem {
	void *access_mem_start;
	u32 shared_mem_size;
	u32 access_private_mem_size;
	struct access_image_descr access_image_descr;
};

/**
 * This is the function to handle the modem release.
 */
int tee_ta_start_modem(struct tee_ta_start_modem *data);

#endif
