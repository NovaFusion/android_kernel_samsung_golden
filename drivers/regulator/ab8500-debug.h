/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson.
 *
 * License Terms: GNU General Public License v2
 */

#ifndef __AB8500_DEBUG_H__
#define __AB8500_DEBUG_H__

/*
 * regulator status print
 */
enum ab8500_regulator_id {
	AB8500_VARM,
	AB8500_VBBP,
	AB8500_VBBN,
	AB8500_VAPE,
	AB8500_VSMPS1,
	AB8500_VSMPS2,
	AB8500_VSMPS3,
	AB8500_VPLL,
	AB8500_VREFDDR,
	AB8500_VMOD,
	AB8500_VEXTSUPPLY1,
	AB8500_VEXTSUPPLY2,
	AB8500_VEXTSUPPLY3,
	AB8500_VRF1,
	AB8500_VANA,
	AB8500_VAUX1,
	AB8500_VAUX2,
	AB8500_VAUX3,
	AB8500_VAUX4,		/* Note: ABx540 / AB8505 only */
	AB8500_VAUX5,		/* Note: AB8505 only */
	AB8500_VAUX6,		/* Note: AB8505 only */
	AB8500_VAUX8,		/* Note: AB8505 only */
	AB8500_VINTCORE,
	AB8500_VTVOUT,
	AB8500_VAUDIO,
	AB8500_VANAMIC1,
	AB8500_VANAMIC2,
	AB8500_VDMIC,		/* Note: ABx540 / AB8500 only */
	AB8500_VUSB,
	AB8500_VOTG,
	AB8500_VBUSBIS,
	AB8500_NUM_REGULATORS,
};

enum ab8500_regulator_mode {
	AB8500_MODE_OFF = 0,
	AB8500_MODE_ON,
	AB8500_MODE_HW,
	AB8500_MODE_LP
};

enum ab8500_regulator_hwmode {
	AB8500_HWMODE_NONE = 0,
	AB8500_HWMODE_HPLP,
	AB8500_HWMODE_HPOFF,
	AB8500_HWMODE_HP,
	AB8500_HWMODE_HP2,
};

enum hwmode_auto {
	HWM_OFF = 0,
	HWM_ON = 1,
	HWM_INVAL = 2,
};

struct ab8500_debug_regulator_status {
	char *name;
	enum ab8500_regulator_mode mode;
	enum ab8500_regulator_hwmode hwmode;
	enum hwmode_auto hwmode_auto[4];
	int volt_selected;
	int volt_len;
	int volt[4];
};

int ab8500_regulator_debug_read(enum ab8500_regulator_id id,
				struct ab8500_debug_regulator_status *s);
#endif /* __AB8500_DEBUG_H__ */
