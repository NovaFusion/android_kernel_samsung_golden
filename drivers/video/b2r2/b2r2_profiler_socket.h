/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 profiler socket communication
 *
 * Author: Johan Mossberg <johan.xx.mossberg@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef _LINUX_VIDEO_B2R2_PROFILER_SOCKET_H
#define _LINUX_VIDEO_B2R2_PROFILER_SOCKET_H

#include "b2r2_internal.h"

/* Will give a correct result most of the time but can be wrong */
bool is_profiler_registered_approx(void);

void b2r2_call_profiler_blt_done(const struct b2r2_blt_request * const request);

#endif /* _LINUX_VIDEO_B2R2_PROFILER_SOCKET_H */
