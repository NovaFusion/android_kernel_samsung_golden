/*
 * Copyright (C) ST-Ericsson SA 2012
 * Author: Maxime Coquelin <maxime.coquelin@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */

struct ux500_pasr_data {
	char name[20];
	phys_addr_t base_addr;
	u8 mailbox;
	void (*apply_mask)(u8 channel, long unsigned int *mr17);
	struct device *dev;
};

