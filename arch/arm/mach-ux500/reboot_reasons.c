/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Rickard Evertsson <rickard.evertsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Use this file to customize your reboot / sw reset reasons. Add, remove or
 * modify reasons in reboot_reasons[].
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <mach/reboot_reasons.h>

struct reboot_reason reboot_reasons[] = {
	{"crash", SW_RESET_CRASH},
	{"factory-reset", SW_RESET_FACTORY_RESET},
	{"recovery", SW_RESET_RECOVERY},
	{"charging", SW_RESET_CHARGING},
	{"coldstart", SW_RESET_COLDSTART},
	{"none", SW_RESET_NO_ARGUMENT}, /* Normal Boot */
	{"chgonly-exit", SW_RESET_CHGONLY_EXIT}, /* Exit Charge Only Mode */
};

unsigned int reboot_reasons_size = ARRAY_SIZE(reboot_reasons);

/*
 * The reboot reason string can be 255 characters long and the memory
 * in which we save the sw reset reason is 2 bytes. Therefore we need to
 * convert the string into a 16 bit pattern.
 *
 * See file reboot_reasons.h for conversion.
 */
u16 reboot_reason_code(const char *cmd)
{
	int i;

	if (cmd == NULL) {
		if (oops_in_progress) {
			/* if we're in an oops assume it's a crash */
			return SW_RESET_CRASH;
		} else {
			/* normal reboot w/o argument */
			return SW_RESET_NO_ARGUMENT;
		}
	}

	/* Search through reboot reason list */
	for (i = 0; i < reboot_reasons_size; i++) {
		if (!strcmp(reboot_reasons[i].reason, cmd))
			return reboot_reasons[i].code;
	}

	/* No valid reboot reason found */
	return SW_RESET_NO_ARGUMENT;
}

/*
 * The saved sw reset reason is a 2 byte code that is translated into
 * a reboot reason string which is up to 255 characters long by this
 * function.
 *
 * See file reboot_reasons.h for conversion.
 */
const char *reboot_reason_string(u16 code)
{
	int i;

	/* Search through reboot reason list */
	for (i = 0; i < reboot_reasons_size; i++) {
		if (reboot_reasons[i].code == code)
			return reboot_reasons[i].reason;
	}

	/* No valid reboot reason code found */
	return "unknown";
}
