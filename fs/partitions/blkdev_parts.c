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

#include "check.h"
#include "blkdev_parts.h"

static char *cmdline;

/*
 * This is the handler for our kernel commandline parameter,
 * called from main.c::checksetup().
 * Note that we can not yet kmalloc() anything, so we only save
 * the commandline for later processing.
 */
static int cmdline_setup(char *s)
{
	cmdline = s;
	return 1;
}
__setup("blkdevparts=", cmdline_setup);

/* Parse for a matching blkdev-id and return pointer to partdef */
static char *parse_blkdev_id(char *blkdev_name)
{
	int blkdev_id_len;
	char *p, *blkdev_id;

	/* Start parsing for a matching blkdev-id */
	p = blkdev_id = cmdline;
	while (blkdev_id != NULL) {

		/* Find the end of the blkdev-id string */
		p = strchr(blkdev_id, ':');
		if (p == NULL)
			return NULL;

		/* Check if we found a matching blkdev-id */
		blkdev_id_len = p - blkdev_id;
		if (strlen(blkdev_name)	== blkdev_id_len) {
			if (strncmp(blkdev_name, blkdev_id, blkdev_id_len) == 0)
				return p;
		}

		/* Move to next blkdev-id string if there is one */
		blkdev_id = strchr(p, ';');
		if (blkdev_id != NULL)
			blkdev_id++;
	}
	return NULL;
}

static int parse_partdef(char **part, struct parsed_partitions *state, int part_nbr)
{
	sector_t size, offset;
	char *p = *part;

	/* Skip the beginning "," or ":" */
	p++;

	/* Fetch and verify size from partdef */
	size = simple_strtoull(p, &p, 10);
	if ((size == 0) || (*p != '@'))
		return 0;

	/* Skip the "@" */
	p++;

	/* Fetch offset from partdef and check if there are more parts */
	offset = simple_strtoull(p, &p, 10);
	if (*p == ',')
		*part = p;
	else
		*part = NULL;

	/* Add partition to state */
	put_partition(state, part_nbr, offset, size);
	printk(KERN_INFO "\nPartition: size=%llu, offset=%llu\n",
		(unsigned long long) size,
		(unsigned long long) offset);
	return 1;
}

static int parse_blkdev_parts(char *blkdev_name, struct parsed_partitions *state)
{
	char *partdef;
	int part_nbr = 0;

	/* Find partdef */
	partdef = parse_blkdev_id(blkdev_name);

	/* Add parts */
	while (partdef != NULL) {
		/* Find next part and add it to state */
		part_nbr++;
		if (!parse_partdef(&partdef, state, part_nbr))
			return 0;
	}
	return part_nbr;
}

int blkdev_partition(struct parsed_partitions *state)
{
	char blkdev_name[BDEVNAME_SIZE];

	/* Check if there are any partitions to handle */
	if (cmdline == NULL)
		return 0;

	/* Get the name of the blockdevice we are operating upon */
	if (bdevname(state->bdev, blkdev_name) == NULL) {
		printk(KERN_WARNING "Could not get a blkdev name\n");
		return 0;
	}

	/* Parse for partitions and add them to the state */
	return parse_blkdev_parts(blkdev_name, state);
}

