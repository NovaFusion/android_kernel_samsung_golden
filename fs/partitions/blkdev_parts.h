/*
 *
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Ulf Hansson <ulf.hansson@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 *
 * Create partitions for block devices by reading from the kernel
 * command line (kernel boot arguments).
 *
 */

int blkdev_partition(struct parsed_partitions *state);

