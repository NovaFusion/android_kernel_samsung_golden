/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Rickard Evertsson <rickard.evertsson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Use this file to customize your reboot / sw reset reasons. Add, remove or
 * modify reasons in reboot_reasons[].
 * The reboot reasons will be saved to a secure location in TCDM memory and
 * can be read at bootup by e.g. the bootloader, or at a later stage userspace
 * since the code is exposed through sysfs.
 */

#ifndef _REBOOT_REASONS_H
#define _REBOOT_REASONS_H

/*
 * These defines contains the codes that will be written down to a secure
 * location before resetting. These values are exposed through a sysfs
 * entry under /sys/socinfo, see mach-ux500/cpu-db8500.c
 */
#define SW_RESET_NO_ARGUMENT 0xBEEF
#define SW_RESET_FACTORY_RESET 0x4242
#define SW_RESET_CRASH 0xDEAD
#define SW_RESET_NORMAL 0xc001
#define SW_RESET_CHARGING 0xCAFE
#define SW_RESET_COLDSTART 0x0
#define SW_RESET_RECOVERY 0x5502
#define SW_RESET_CHGONLY_EXIT 0xCAFF

/*
 * The array reboot_reasons[] is used when you want to map a string to a reboot
 * reason code
 */
struct reboot_reason {
	const char *reason;
	u16 code;
};

extern struct reboot_reason reboot_reasons[];

extern unsigned int reboot_reasons_size;

u16 reboot_reason_code(const char *cmd);
const char *reboot_reason_string(u16 code);

#endif
