/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 */
#ifndef MPCLOAD_H_
#define MPCLOAD_H_

#include <cm/engine/component/inc/instance.h>

/******************************************************************************/
/************************ FUNCTIONS PROTOTYPES ********************************/
/******************************************************************************/

PUBLIC t_cm_error cm_PFM_allocatePerfmeterDataMemory(t_nmf_core_id coreId, t_cm_domain_id domainId);
PUBLIC void cm_PFM_deallocatePerfmeterDataMemory(t_nmf_core_id coreId);

#endif /* MPCLOAD_H_ */
