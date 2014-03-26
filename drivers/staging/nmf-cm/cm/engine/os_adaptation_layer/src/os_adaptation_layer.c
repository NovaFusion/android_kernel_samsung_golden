/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>
#include <cm/engine/utils/inc/mem.h>

t_nmf_osal_sync_handle lockHandleApi;
t_nmf_osal_sync_handle lockHandleCom;
t_nmf_osal_sem_handle semHandle;
struct osal_debug_operations osal_debug_ops;

/****************/
/* Generic part */
/****************/
PUBLIC t_cm_error cm_OSAL_Init(void)
{

	/* create locks */
	lockHandleApi = OSAL_CreateLock();
	if (lockHandleApi == 0) {return CM_INVALID_PARAMETER;}
	lockHandleCom = OSAL_CreateLock();
	if (lockHandleCom == 0) {return CM_INVALID_PARAMETER;}

	/* create semaphore */
	semHandle = OSAL_CreateSemaphore(0);
	if (semHandle == 0) {return CM_INVALID_PARAMETER;}

	/* init to zero */
	cm_MemSet(&osal_debug_ops, 0, sizeof(osal_debug_ops));

	return CM_OK;
}

PUBLIC void cm_OSAL_Destroy(void)
{
	/* destroy locks */
	OSAL_DestroyLock(lockHandleApi);
	OSAL_DestroyLock(lockHandleCom);

	/* destroy semaphore */
	OSAL_DestroySemaphore(semHandle);
}
