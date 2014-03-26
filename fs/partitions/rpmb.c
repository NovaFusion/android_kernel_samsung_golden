/*
 *
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Alex Macro <alex.macro@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#include "check.h"
#include "rpmb.h"

/*
 * Replay Protected Memory Block (RPMB) partitions are defined in JEDEC Standard
 * JESD84-A441 and have a signed protocol requiring knowledge of a secure key
 * before they can be used.
 *
 * Detect if we have found an RPMB partition and, if we have, return 1 so that
 * no further attempt is made to use it at this time as it cannot have a
 * partition table.
 */

int rpmb_partition(struct parsed_partitions *state)
{
	char blkdev_name[BDEVNAME_SIZE];

	/* Get the name of the blockdevice we are operating upon */
	if (bdevname(state->bdev, blkdev_name) == NULL) {
		printk(KERN_WARNING
			"rpmb_partition: Could not get a blkdev name\n");
		return 0;
	}

	if (strstr(blkdev_name, "rpmb") != NULL) {
		/*
		 * RPMB partition found. There is no partition table.
		 * There can be only 1 RPMB partition on an MMC device.
		 */
		printk(KERN_DEBUG
			"rpmb_partition: partition table %s not matching\n",
				blkdev_name);
		return 1;
	}

	return 0;
}

