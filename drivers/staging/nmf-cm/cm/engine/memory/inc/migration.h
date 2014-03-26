/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Migration API.
 *
 * \defgroup
 *
 */
#ifndef __INC_MIGRATION_H
#define __INC_MIGRATION_H

#include <cm/engine/memory/inc/domain_type.h>
#include <cm/engine/dsp/inc/dsp.h>

typedef enum {
    STATE_MIGRATED = 1,
    STATE_NORMAL   = 0,
} t_cm_migration_state;

PUBLIC t_cm_error cm_migrate(const t_cm_domain_id srcShared, const t_cm_domain_id src, const t_cm_domain_id dst);

PUBLIC t_cm_error cm_unmigrate(void);

PUBLIC t_uint32 cm_migration_translate(t_dsp_segment_type segmentType, t_uint32 addr);

PUBLIC void cm_migration_check_state(t_nmf_core_id coreId, t_cm_migration_state expected);

#endif /* __INC_MIGRATION_H */
