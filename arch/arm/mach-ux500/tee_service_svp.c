/*
 * TEE service to handle the calls to trusted applications in SVP.
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/tee.h>
#include <linux/err.h>
#include "mach/tee_ta_start_modem.h"

static int cmp_uuid_start_modem(struct tee_uuid *uuid)
{
	int ret = -EINVAL;

	if (uuid == NULL)
		return -EINVAL;

	/* This handles the calls to TA for start the modem */
	if ((uuid->timeLow == UUID_TEE_TA_START_MODEM_LOW) &&
	    (uuid->timeMid == UUID_TEE_TA_START_MODEM_MID) &&
	    (uuid->timeHiAndVersion == UUID_TEE_TA_START_MODEM_HIGH)) {

		u8 clockSeqAndNode[TEE_UUID_CLOCK_SIZE] =
			UUID_TEE_TA_START_MODEM_CLOCKSEQ;

		ret = memcmp(uuid->clockSeqAndNode, clockSeqAndNode,
			     TEE_UUID_CLOCK_SIZE);
	}

	return ret;
}

int call_sec_world(struct tee_session *ts, int sec_cmd)
{
	int ret = 0;

	if (ts == NULL)
		return -EINVAL;

	if (cmp_uuid_start_modem(ts->uuid))
		return -EINVAL;

	switch (ts->cmd) {
	case COMMAND_ID_START_MODEM:
		ret = tee_ta_start_modem((struct tee_ta_start_modem *)
					 ts->op);
		if (ret) {
			ts->err = TEED_ERROR_GENERIC;
			ts->origin = TEED_ORIGIN_TEE_APPLICATION;
			pr_err("tee_ta_start_modem() failed!\n");
			return ret;
		}
		break;

	default:
		break;
	}

	/* TODO: to handle more trusted applications. */

	return ret;
}
