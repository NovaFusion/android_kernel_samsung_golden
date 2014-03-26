/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#ifndef CM_MIGRATION_ENGINE_H
#define CM_MIGRATION_ENGINE_H

#include <cm/inc/cm_type.h>
#include <cm/engine/memory/inc/domain_type.h>

PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_Migrate(const t_cm_domain_id srcShared, const t_cm_domain_id src, const t_cm_domain_id dst);

PUBLIC IMPORT_SHARED t_cm_error CM_ENGINE_Unmigrate(void);

#endif /* CM_MIGRATION_ENGINE_H */
