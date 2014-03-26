/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_CONFIGSTATUS_H_
#define __INC_CONFIGSTATUS_H

#include <cm/inc/cm_type.h>
#include <cm/engine/utils/inc/string.h>

/*
 * Variable to active intensive check
 *
 * \ingroup CM_CONFIGURATION_API
 */
extern t_sint32 cmIntensiveCheckState;

/*
 * Variable to active trace level
 *
 * \ingroup CM_CONFIGURATION_API
 */
extern t_sint32 cm_debug_level;

/*
 * Variable to active error break
 *
 * \ingroup CM_CONFIGURATION_API
 */
extern t_sint32 cm_error_break;

/*
 * Variable to activate Ulp
 *
 * \ingroup CM_CONFIGURATION_API
 */
extern t_bool cmUlpEnable;

extern t_dup_char anonymousDup, eventDup, skeletonDup, stubDup, traceDup;

#endif
