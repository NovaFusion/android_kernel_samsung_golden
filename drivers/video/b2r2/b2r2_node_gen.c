/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 node generator
 *
 * Author: Robert Fekete <robert.fekete@stericsson.com>
 * Author: Paul Wannback
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <asm/dma-mapping.h>
#include "b2r2_internal.h"

static void free_nodes(struct b2r2_control *cont,
		struct b2r2_node *first_node)
{
	struct b2r2_node *node = first_node;
	int no_of_nodes = 0;

	while (node) {
		no_of_nodes++;
		node = node->next;
	}

	dma_free_coherent(cont->dev,
			no_of_nodes * sizeof(struct b2r2_node),
			first_node,
			first_node->physical_address -
			offsetof(struct b2r2_node, node));
}

struct b2r2_node *b2r2_blt_alloc_nodes(struct b2r2_control *cont,
		int no_of_nodes)
{
	u32 physical_address;
	struct b2r2_node *nodes;
	struct b2r2_node *tmpnode;

	if (no_of_nodes <= 0) {
		dev_err(cont->dev, "%s: Wrong number of nodes (%d)",
				__func__, no_of_nodes);
		return NULL;
	}

	/* Allocate the memory */
	nodes = (struct b2r2_node *) dma_alloc_coherent(cont->dev,
			no_of_nodes * sizeof(struct b2r2_node),
			&physical_address, GFP_DMA | GFP_KERNEL);

	if (nodes == NULL) {
		dev_err(cont->dev,
				"%s: Failed to alloc memory for nodes",
				__func__);
		return NULL;
	}

	/* Build the linked list */
	tmpnode = nodes;
	physical_address += offsetof(struct b2r2_node, node);
	while (no_of_nodes--) {
		tmpnode->physical_address = physical_address;
		if (no_of_nodes)
			tmpnode->next = tmpnode + 1;
		else
			tmpnode->next = NULL;

		tmpnode++;
		physical_address += sizeof(struct b2r2_node);
	}

	return nodes;
}

void b2r2_blt_free_nodes(struct b2r2_control *cont,
		struct b2r2_node *first_node)
{
	free_nodes(cont, first_node);
}

