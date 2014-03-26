/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 node splitter
 *
 * Author: Fredrik Allansson <fredrik.allansson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __B2R2_NODE_SPLIT_H_
#define __B2R2_NODE_SPLIT_H_

#include "b2r2_internal.h"
#include "b2r2_hw.h"

/**
 * b2r2_node_split_analyze() - Analyzes a B2R2 request
 *
 * @req          - The request to analyze
 * @max_buf_size - The largest size allowed for intermediate buffers
 * @node_count   - Number of nodes required for the job
 * @buf_count    - Number of intermediate buffers required for the job
 * @bufs         - An array of buffers needed for intermediate buffers
 *
 * Analyzes the request and determines how many nodes and intermediate buffers
 * are required.
 *
 * It is the responsibility of the caller to allocate memory and assign the
 * physical addresses. After that b2r2_node_split_assign_buffers should be
 * called to assign the buffers to the right nodes.
 *
 * Returns:
 *   A handle identifing the analyzed request if successful, a negative
 *   value otherwise.
 */
int b2r2_node_split_analyze(const struct b2r2_blt_request *req, u32 max_buf_size,
		u32 *node_count, struct b2r2_work_buf **bufs, u32* buf_count,
		struct b2r2_node_split_job *job);

/**
 * b2r2_node_split_configure() - Performs a node split
 *
 * @handle - A handle for the analyzed request
 * @first  - The first node in the list of nodes to use
 *
 * Fills the supplied list of nodes with the parameters acquired by analyzing
 * the request.
 *
 * All pointers to intermediate buffers are represented by integers to be used
 * in the array returned by b2r2_node_split_analyze.
 *
 * Returns:
 *   A negative value if an error occurred, 0 otherwise.
 */
int b2r2_node_split_configure(struct b2r2_control *cont,
		struct b2r2_node_split_job *job, struct b2r2_node *first);

/**
 * b2r2_node_split_assign_buffers() - Assignes physical addresses
 *
 * @handle    - The handle for the job
 * @first     - The first node in the node list
 * @bufs      - Buffers with assigned physical addresses
 * @buf_count - Number of physical addresses
 *
 * Assigns the physical addresses where intermediate buffers are required in
 * the node list.
 *
 * The order of the elements of 'bufs' must be maintained from the call to
 * b2r2_node_split_analyze.
 *
 * Returns:
 *   A negative value if an error occurred, 0 otherwise.
 */
int b2r2_node_split_assign_buffers(struct b2r2_control *cont,
		struct b2r2_node_split_job *job,
		struct b2r2_node *first, struct b2r2_work_buf *bufs,
		u32 buf_count);

/**
 * b2r2_node_split_unassign_buffers() - Removes all physical addresses
 *
 * @handle - The handle associated with the job
 * @first  - The first node in the node list
 *
 * Removes all references to intermediate buffers from the node list.
 *
 * This makes it possible to reuse the node list with new buffers by calling
 * b2r2_node_split_assign_buffers again. Useful for caching node lists.
 */
void b2r2_node_split_unassign_buffers(struct b2r2_control *cont,
		struct b2r2_node_split_job *job,
		struct b2r2_node *first);

/**
 * b2r2_node_split_release() - Releases all resources for a job
 *
 * @handle - The handle identifying the job. This will be set to 0.
 *
 * Releases all resources associated with a job.
 *
 * This should always be called once b2r2_node_split_analyze has been called
 * in order to release any resources allocated while analyzing.
 */
void b2r2_node_split_cancel(struct b2r2_control *cont,
		struct b2r2_node_split_job *job);

/**
 * b2r2_node_split_init() - Initializes the node split module
 *
 * Initializes the node split module and creates debugfs files.
 */
int b2r2_node_split_init(struct b2r2_control *cont);

/**
 * b2r2_node_split_exit() - Deinitializes the node split module
 *
 * Releases all resources for the node split module.
 */
void b2r2_node_split_exit(struct b2r2_control *cont);

#endif
