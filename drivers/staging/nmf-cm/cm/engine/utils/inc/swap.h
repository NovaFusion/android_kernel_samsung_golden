/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Swap integer manipulation.
 */
#ifndef H_CM_UTILS_SWAP
#define H_CM_UTILS_SWAP

#include <cm/inc/cm_type.h>

/*
 * Swap methods
 */
t_uint16 swap16(t_uint16 x);
t_uint32 swap32(t_uint32 x);
t_uint64 swap64(t_uint64 x);
t_uint32 noswap32(t_uint32 x);

#endif
