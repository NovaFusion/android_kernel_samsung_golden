/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2, with
 * user space exemption described in the top-level COPYING file in
 * the Linux kernel source tree.
 */
/*!
 * \brief Components Component Manager API type.
 *
 * \defgroup COMPONENT CM Components API
 * \ingroup CM_USER_API
 */

#ifndef REPOSITORY_TYPE_H_
#define REPOSITORY_TYPE_H_

typedef enum
{
    BIND_ASYNC,
    BIND_TRACE,
    BIND_FROMUSER,
    BIND_TOUSER
} t_action_to_do;

#endif
