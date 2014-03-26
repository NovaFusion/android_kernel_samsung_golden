/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/**
 * \internal
 */
#ifndef __INC_CM_SEMAPHORES_DSP_H
#define __INC_CM_SEMAPHORES_DSP_H

#include <share/semaphores/inc/semaphores.h>
#include <cm/engine/dsp/inc/dsp.h>

PUBLIC void cm_DSP_SEM_Take(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC void cm_DSP_SEM_Give(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC void cm_DSP_SEM_GenerateIrq(t_nmf_core_id coreId, t_semaphore_id semId);
PUBLIC void cm_DSP_AssertDspIrq(t_nmf_core_id coreId, t_host2mpc_irq_num irqNum);

PUBLIC void cm_DSP_AcknowledgeDspIrq(t_nmf_core_id coreId, t_mpc2host_irq_num irqNum);

#endif /* __INC_CM_SEMAPHORES_DSP_H */
