/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson B2R2 internal definitions
 *
 * Author: Jorgen Nilsson <jorgen.nilsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _LINUX_DRIVERS_VIDEO_B2R2_CONTROL_H_
#define _LINUX_DRIVERS_VIDEO_B2R2_CONTROL_H_

#include "b2r2_internal.h"

int b2r2_control_init(struct b2r2_control *cont);
void b2r2_control_exit(struct b2r2_control *cont);
int b2r2_control_open(struct b2r2_control_instance *instance);
int b2r2_control_release(struct b2r2_control_instance *instance);

int b2r2_control_blt(struct b2r2_blt_request *request);
int b2r2_generic_blt(struct b2r2_blt_request *request);
int b2r2_control_waitjob(struct b2r2_blt_request *request);
int b2r2_control_synch(struct b2r2_control_instance *instance,
			int request_id);
size_t b2r2_control_read(struct b2r2_control_instance *instance,
		struct b2r2_blt_request **request_out, bool block);
size_t b2r2_control_read_id(struct b2r2_control_instance *instance,
		struct b2r2_blt_request **request_out, bool block,
		int request_id);

#endif
