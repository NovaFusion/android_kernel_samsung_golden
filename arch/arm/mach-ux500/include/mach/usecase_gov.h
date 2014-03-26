/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Michel JAOUEN <michel.jaouen@stericsson.con> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_UX500_USECASE_GOV
#define __MACH_UX500_USECASE_GOV

struct usecase_config {
	char *name;
	unsigned int min_arm;
	unsigned int max_arm;
	unsigned long cpuidle_multiplier;
	bool second_cpu_online;
	bool l2_prefetch_en;
	bool enable;
	unsigned int forced_state; /* Forced cpu idle state. */
	bool vc_override; /* QOS override for voice-call. */
	bool force_usecase; /* this usecase will be used no matter what */
};

enum ux500_uc {
	UX500_UC_NORMAL	=  0,
	UX500_UC_AUTO, /* Add use case below this. */
	UX500_UC_VC,
	UX500_UC_LPA,
	UX500_UC_EXT, /* External control use */
	UX500_UC_USER, /* Add use case above this. */
	UX500_UC_MAX,
};

enum usecase_req_type {
	ENABLE_MAX_LIMIT = 0,
	DISABLE_MAX_LIMIT = 1,
	ENABLE_MIN_LIMIT = 2,
	DISABLE_MIN_LIMIT = 3,
};

enum usecase_req_cmd {
	REQ_RESET_VALUE = -1,
	REQ_NO_CHANGE = 0,
};

int set_usecase_config(int enable, int max, int min);

#endif
