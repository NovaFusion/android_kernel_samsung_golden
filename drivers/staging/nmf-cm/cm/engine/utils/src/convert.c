/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/utils/inc/convert.h>

const char* dspNames[NB_CORE_IDS] = {
    "ARM",
    "SVA",
    "SIA"
};


const char* cm_getDspName(t_nmf_core_id dsp) {
	return dspNames[dsp];
}
