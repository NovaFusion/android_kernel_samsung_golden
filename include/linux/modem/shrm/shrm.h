/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghavi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __SHM_DRIVER_IF_H__
#define __SHM_DRIVER_IF_H__

#include <linux/device.h>

/* forward declaration */
struct shrm_dev;

typedef void (*rx_cb)(void *data, unsigned int length);
typedef void (*received_msg_handler)(unsigned char l2_header,
			void *msg_ptr, unsigned int length,
			struct shrm_dev *shrm);
#ifdef CONFIG_U8500_SHRM
bool shrm_is_modem_online(void);
#else
static inline bool shrm_is_modem_online(void)
{
	return false;
}
#endif

#endif
