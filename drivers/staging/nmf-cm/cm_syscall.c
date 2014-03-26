/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <cm/engine/api/cm_engine.h>
#include "cmioctl.h"
#include "osal-kernel.h"
#include "cmld.h"
#include "cm_dma.h"

/** Dequeue and free per-process messages for specific binding
 *
 * \note
 * This is only safe if the per process mutex is held when called.
 */
static inline void freeMessages(struct cm_channel_priv* cPriv, t_skelwrapper* binding)
{
	struct osal_msg *this, *next;

	spin_lock_bh(&cPriv->bh_lock);

	/* free any pending messages */
         plist_for_each_entry_safe(this, next, &cPriv->messageQueue, msg_entry) {
		if (this->msg_type == MSG_INTERFACE
		    && this->d.itf.skelwrap == binding) {
			plist_del(&this->msg_entry, &cPriv->messageQueue);
			kfree(this);
		}
	}
	spin_unlock_bh(&cPriv->bh_lock);
}

static t_cm_error copy_string_from_user(char *dst, const char __user *src, int len)
{
	int ret;

	ret = strncpy_from_user(dst, src, len);
	if (ret < 0) /* -EFAULT */
		return CM_INVALID_PARAMETER;

	if (ret >= len)
		return CM_OUT_OF_LIMITS;

	return 0;
}

inline int cmld_InstantiateComponent(struct cm_process_priv* procPriv,
				     CM_InstantiateComponent_t __user *param)
{
	CM_InstantiateComponent_t data;
	char templateName[MAX_TEMPLATE_NAME_LENGTH];
	char localName[MAX_COMPONENT_NAME_LENGTH];
	char *dataFile = NULL;

	/* Copy all user data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if (data.in.dataFile != NULL) {
		dataFile = vmalloc(data.in.dataFileSize);
		if (dataFile == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFile, data.in.dataFile, data.in.dataFileSize)) {
			data.out.error = CM_INVALID_PARAMETER;
			goto out;
		}
	}

	if ((data.out.error = copy_string_from_user(templateName,
						    data.in.templateName,
						    sizeof(templateName))))
		goto out;

	if ((data.in.localName != NULL) &&
	    (data.out.error = copy_string_from_user(localName,
						    data.in.localName,
						    sizeof(localName))))
		goto out;

	/* Do appropriate CM Engine call */
	data.out.error = CM_ENGINE_InstantiateComponent(templateName,
							data.in.domainId,
							procPriv->pid,
							data.in.priority,
							data.in.localName ? localName : NULL,
							dataFile,
							&data.out.component);

out:
	if (dataFile)
		vfree(dataFile);
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_BindComponentFromCMCore(struct cm_process_priv* procPriv,
					CM_BindComponentFromCMCore_t __user *param)
{
	CM_BindComponentFromCMCore_t data;
	char providedItfServerName[MAX_INTERFACE_NAME_LENGTH];
	char *dataFileSkeleton = NULL;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(providedItfServerName,
						    data.in.providedItfServerName,
						    sizeof(providedItfServerName))))
		goto out;

	if (data.in.dataFileSkeleton != NULL) {
		dataFileSkeleton = OSAL_Alloc(data.in.dataFileSkeletonSize);
		if (dataFileSkeleton == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFileSkeleton, data.in.dataFileSkeleton,
				   data.in.dataFileSkeletonSize)) {
			data.out.error = CM_INVALID_PARAMETER;
			goto out;
		}
	}

	data.out.error = CM_ENGINE_BindComponentFromCMCore(data.in.server,
							   providedItfServerName,
							   data.in.fifosize,
							   data.in.eventMemType,
							   &data.out.host2mpcId,
							   procPriv->pid,
							   dataFileSkeleton);
out:
	if (dataFileSkeleton)
		OSAL_Free(dataFileSkeleton);
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;

	return 0;
}

inline int cmld_UnbindComponentFromCMCore(CM_UnbindComponentFromCMCore_t __user *param)
{
	CM_UnbindComponentFromCMCore_t data;

	/* Copy all user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_UnbindComponentFromCMCore(data.in.host2mpcId);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_BindComponentToCMCore(struct cm_channel_priv* channelPriv,
				      CM_BindComponentToCMCore_t __user *param)
{
	CM_BindComponentToCMCore_t data;
	t_skelwrapper *skelwrapper;
	struct cm_process_priv *procPriv = channelPriv->proc;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];
	char *dataFileStub = NULL;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	/* Do appropriate CM Engine call */
	skelwrapper = (t_skelwrapper *)OSAL_Alloc(sizeof(*skelwrapper));
	if (skelwrapper == NULL) {
		data.out.error = CM_NO_MORE_MEMORY;
		goto out;
	}

	if (data.in.dataFileStub != NULL) {
		dataFileStub = OSAL_Alloc(data.in.dataFileStubSize);
		if (dataFileStub == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFileStub, data.in.dataFileStub, data.in.dataFileStubSize)) {
			data.out.error = CM_INVALID_PARAMETER;
			goto out;
		}
	}

	if ((data.out.error = CM_ENGINE_BindComponentToCMCore(
		     data.in.client,
		     requiredItfClientName,
		     data.in.fifosize,
		     (t_nmf_mpc2host_handle)skelwrapper,
		     dataFileStub,
		     &data.out.mpc2hostId,
		     procPriv->pid)) != CM_OK) {
		OSAL_Free(skelwrapper);
		goto out;
	}

	skelwrapper->upperLayerThis = data.in.upLayerThis;
	skelwrapper->mpc2hostId = data.out.mpc2hostId;
	skelwrapper->channelPriv = channelPriv;
	mutex_lock(&channelPriv->skelListLock);
	list_add(&skelwrapper->entry, &channelPriv->skelList);
	mutex_unlock(&channelPriv->skelListLock);
out:
	if (dataFileStub != NULL)
		OSAL_Free(dataFileStub);
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_UnbindComponentToCMCore(struct cm_process_priv* procPriv,
					CM_UnbindComponentToCMCore_t __user *param)
{
	CM_UnbindComponentToCMCore_t data;
	t_skelwrapper *skelwrapper;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	data.out.error = CM_ENGINE_UnbindComponentToCMCore(
			data.in.client, requiredItfClientName,
			(t_nmf_mpc2host_handle*)&skelwrapper,
			procPriv->pid);

	if (data.out.error != CM_OK && data.out.error != CM_MPC_NOT_RESPONDING)
		goto out;

	data.out.upLayerThis = skelwrapper->upperLayerThis;

	mutex_lock(&skelwrapper->channelPriv->msgQueueLock);
	freeMessages(skelwrapper->channelPriv, skelwrapper);
	mutex_lock(&skelwrapper->channelPriv->skelListLock);
	list_del(&skelwrapper->entry);
	mutex_unlock(&skelwrapper->channelPriv->skelListLock);
	mutex_unlock(&skelwrapper->channelPriv->msgQueueLock);
	OSAL_Free(skelwrapper);
out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_BindComponentAsynchronous(struct cm_process_priv* procPriv,
					  CM_BindComponentAsynchronous_t __user *param)
{
	CM_BindComponentAsynchronous_t data;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];
	char providedItfServerName[MAX_INTERFACE_NAME_LENGTH];
	char *dataFileSkeletonOrEvent = NULL;
	char *dataFileStub = NULL;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	if ((data.out.error = copy_string_from_user(providedItfServerName,
						    data.in.providedItfServerName,
						    sizeof(providedItfServerName))))
		goto out;

	if (data.in.dataFileSkeletonOrEvent != NULL) {
		dataFileSkeletonOrEvent =
			OSAL_Alloc(data.in.dataFileSkeletonOrEventSize);
		if (dataFileSkeletonOrEvent == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFileSkeletonOrEvent, data.in.dataFileSkeletonOrEvent, data.in.dataFileSkeletonOrEventSize)) {
			data.out.error = CM_INVALID_PARAMETER;
			goto out;
		}
	}

	if (data.in.dataFileStub != NULL) {
		dataFileStub = OSAL_Alloc(data.in.dataFileStubSize);
		if (dataFileStub == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFileStub, data.in.dataFileStub, data.in.dataFileStubSize)) {
			data.out.error = CM_INVALID_PARAMETER;
			goto out;
		}
	}

	/* Do appropriate CM Engine call */
	data.out.error = CM_ENGINE_BindComponentAsynchronous(data.in.client,
							     requiredItfClientName,
							     data.in.server,
							     providedItfServerName,
							     data.in.fifosize,
							     data.in.eventMemType,
							     procPriv->pid,
							     dataFileSkeletonOrEvent,
							     dataFileStub);

out:
	if (dataFileSkeletonOrEvent != NULL)
		OSAL_Free(dataFileSkeletonOrEvent);
	if (dataFileStub != NULL)
		OSAL_Free(dataFileStub);
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_UnbindComponentAsynchronous(struct cm_process_priv* procPriv,
					    CM_UnbindComponentAsynchronous_t __user *param)
{
	CM_UnbindComponentAsynchronous_t data;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];

	/* Copy all user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	/* Do appropriate CM Engine call */
	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_UnbindComponentAsynchronous(data.in.client,
								requiredItfClientName,
								procPriv->pid);
out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_BindComponent(struct cm_process_priv* procPriv,
			      CM_BindComponent_t __user *param)
{
	CM_BindComponent_t data;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];
	char providedItfServerName[MAX_INTERFACE_NAME_LENGTH];
	char *dataFileTrace = NULL;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	if ((data.out.error = copy_string_from_user(providedItfServerName,
						    data.in.providedItfServerName,
						    sizeof(providedItfServerName))))
		goto out;

	if (data.in.dataFileTrace != NULL) {
		dataFileTrace = OSAL_Alloc(data.in.dataFileTraceSize);
		if (dataFileTrace == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFileTrace, data.in.dataFileTrace,
				   data.in.dataFileTraceSize)) {
			data.out.error = CM_INVALID_PARAMETER;
			goto out;
		}
	}

	/* Do appropriate CM Engine call */
	data.out.error = CM_ENGINE_BindComponent(data.in.client,
						 requiredItfClientName,
						 data.in.server,
						 providedItfServerName,
						 data.in.traced,
						 procPriv->pid,
						 dataFileTrace);
out:
	if (dataFileTrace != NULL)
		OSAL_Free(dataFileTrace);
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_UnbindComponent(struct cm_process_priv* procPriv,
				CM_UnbindComponent_t __user *param)
{
	CM_UnbindComponent_t data;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	/* Do appropriate CM Engine call */
	data.out.error = CM_ENGINE_UnbindComponent(data.in.client,
						   requiredItfClientName,
						   procPriv->pid);

out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_BindComponentToVoid(struct cm_process_priv* procPriv,
				    CM_BindComponentToVoid_t __user *param)
{
	CM_BindComponentToVoid_t data;
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	data.out.error = CM_ENGINE_BindComponentToVoid(data.in.client,
							requiredItfClientName,
								procPriv->pid);

out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_DestroyComponent(struct cm_process_priv* procPriv,
				 CM_DestroyComponent_t __user *param)
{
	CM_DestroyComponent_t data;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_DestroyComponent(data.in.component,
						    procPriv->pid);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_CreateMemoryDomain(struct cm_process_priv *procPriv,
				   CM_CreateMemoryDomain_t __user *param)
{
	CM_CreateMemoryDomain_t data;
	t_cm_domain_memory domain;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if (copy_from_user(&domain, data.in.domain, sizeof(domain)))
		return -EFAULT;

	if (data.in.client == NMF_CURRENT_CLIENT)
		data.out.error = CM_ENGINE_CreateMemoryDomain(procPriv->pid,
							      &domain,
							      &data.out.handle);
	else {
		/* Check if client is valid (ie already registered) */
		struct list_head* head;
		struct cm_process_priv *entry;

		list_for_each(head, &process_list) {
			entry = list_entry(head, struct cm_process_priv,
					   entry);
			if (entry->pid == data.in.client)
				break;
		}
		if (head == &process_list)
			data.out.error = CM_INVALID_PARAMETER;
		else
			data.out.error =
				CM_ENGINE_CreateMemoryDomain(data.in.client,
							     &domain,
							     &data.out.handle);
	}

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_CreateMemoryDomainScratch(struct cm_process_priv *procPriv,
					  CM_CreateMemoryDomainScratch_t __user *param)
{
	CM_CreateMemoryDomainScratch_t data;
	t_cm_domain_memory domain;

	/* Copy all user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if (copy_from_user(&domain, data.in.domain, sizeof(domain)))
		return -EFAULT;

	data.out.error = CM_ENGINE_CreateMemoryDomainScratch(procPriv->pid,
							     data.in.parentId,
							     &domain,
							     &data.out.handle);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_DestroyMemoryDomain(CM_DestroyMemoryDomain_t __user *param)
{
	CM_DestroyMemoryDomain_t data;

	/* Copy all user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_DestroyMemoryDomain(data.in.domainId);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetDomainCoreId(CM_GetDomainCoreId_t __user *param)
{
	CM_GetDomainCoreId_t data;

	/* Copy all user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_GetDomainCoreId(data.in.domainId,
						   &data.out.coreId);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_AllocMpcMemory(struct cm_process_priv *procPriv,
			       CM_AllocMpcMemory_t __user *param)
{
	t_cm_error err;
	CM_AllocMpcMemory_t data;
	t_cm_memory_handle handle = 0;
	struct memAreaDesc_t* memAreaDesc;
	t_cm_system_address systemAddress;
	t_uint32 mpcAddress;

	/* Copy all user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* Disregard alignment information and force 4kB memory alignment,
	   in any case (see devnotes.txt) */
	/* PP: Disable this 'force' for now, because of the low amount of
	   available MPC Memory */
	//data.in.memAlignment = CM_MM_MPC_ALIGN_1024WORDS;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_AllocMpcMemory(data.in.domainId,
						  procPriv->pid,
						  data.in.memType,
						  data.in.size,
						  data.in.memAlignment,
						  &handle);

	data.out.pHandle = handle;

	if (data.out.error != CM_OK)
		goto out;

	/* Get memory area decriptors in advance
	   so to fill in list elements right now */
	err = CM_ENGINE_GetMpcMemorySystemAddress(handle, &systemAddress);
	if (err != CM_OK) {
		pr_err("%s: failed CM_ENGINE_GetMpcMemorySystemAddress (%i)\n", __func__, err);
		/* If we can't manage internally this allocated memory latter, it's
		   better to report the error now.
		   Free the handle to not let the driver in an inconsistent state */
		CM_ENGINE_FreeMpcMemory(handle);
		return -EFAULT;
	}

	/* Get MPC address in advance so to fill in list elements right now */
	err = CM_ENGINE_GetMpcMemoryMpcAddress(handle, &mpcAddress);
	if (err != CM_OK) {
		pr_err("%s: failed CM_ENGINE_GetMpcMemoryMpcAddress (%i)\n", __func__, err);
		/* see comments above */
		CM_ENGINE_FreeMpcMemory(handle);
		return -EFAULT;
	}

	/* Allocate and fill a new memory area descriptor. Add it to the list */
	memAreaDesc = OSAL_Alloc(sizeof(struct memAreaDesc_t));
	if (memAreaDesc == NULL) {
		pr_err("%s: failed allocating memAreaDesc\n", __func__);
		/* see comments above */
		CM_ENGINE_FreeMpcMemory(handle);
		return -ENOMEM;
	}

	memAreaDesc->procPriv = procPriv;
	memAreaDesc->handle = handle;
	memAreaDesc->tid = 0;
	memAreaDesc->physAddr = systemAddress.physical;
	memAreaDesc->kernelLogicalAddr = systemAddress.logical;
	memAreaDesc->userLogicalAddr = 0;
	memAreaDesc->mpcPhysAddr = mpcAddress;
	memAreaDesc->size = data.in.size * ((data.in.memType % 2) ? 4 : 2); // betzw: set size in bytes for host (ugly version)
	atomic_set(&memAreaDesc->count, 0);

	if (lock_process(procPriv)) {
		/* may be rather call lock_process_uninterruptible() */
		CM_ENGINE_FreeMpcMemory(handle);
		OSAL_Free(memAreaDesc);
		return -ERESTARTSYS;
	}
	list_add(&memAreaDesc->list, &procPriv->memAreaDescList);
	unlock_process(procPriv);
out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_FreeMpcMemory(struct cm_process_priv *procPriv,
			      CM_FreeMpcMemory_t __user *param)
{
	CM_FreeMpcMemory_t data;
	struct list_head *cursor, *next;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* check that it is actually owned by the process */
	data.out.error = CM_UNKNOWN_MEMORY_HANDLE;

	if (lock_process(procPriv))
		return -ERESTARTSYS;
	list_for_each_safe(cursor, next, &procPriv->memAreaDescList){
		struct memAreaDesc_t* curr;
		curr = list_entry(cursor, struct memAreaDesc_t, list);
		if (curr->handle == data.in.handle){
			if (atomic_read(&curr->count) != 0) {
				pr_err("%s: Memory area (phyAddr: %x, size: %d) "
				       "still in use (count=%d)!\n", __func__,
				       curr->physAddr, curr->size,
				       atomic_read(&curr->count));
				data.out.error = CM_INVALID_PARAMETER;
			} else {
				data.out.error =
					CM_ENGINE_FreeMpcMemory(data.in.handle);
				if (data.out.error == CM_OK) {
					list_del(cursor);
					OSAL_Free(curr);
				}
			}
			break;
		}
	}
	unlock_process(procPriv);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetMpcMemoryStatus(CM_GetMpcMemoryStatus_t __user *param)
{
	CM_GetMpcMemoryStatus_t data;

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_GetMpcMemoryStatus(data.in.coreId,
						      data.in.memType,
						      &data.out.pStatus);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_StartComponent(struct cm_process_priv *procPriv,
			       CM_StartComponent_t __user *param)
{
	CM_StartComponent_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_StartComponent(data.in.client,
						  procPriv->pid);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_StopComponent(struct cm_process_priv *procPriv,
			      CM_StopComponent_t __user *param)
{
	CM_StopComponent_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_StopComponent(data.in.client,
						 procPriv->pid);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetMpcLoadCounter(CM_GetMpcLoadCounter_t __user *param)
{
	CM_GetMpcLoadCounter_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_getMpcLoadCounter(data.in.coreId,
						      &data.out.pMpcLoadCounter);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentDescription(struct cm_process_priv *procPriv,
					CM_GetComponentDescription_t __user *param)
{
	CM_GetComponentDescription_t data;
	char templateName[MAX_TEMPLATE_NAME_LENGTH];
	char localName[MAX_COMPONENT_NAME_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentDescription(data.in.component,
							   templateName,
							   data.in.templateNameLength,
							   &data.out.coreId,
							   localName,
							   data.in.localNameLength,
							   &data.out.priority);

	/* Copy results back to userspace */
	if (data.out.error == CM_OK) {
		/* coverity[tainted_data : FALSE] */
		if (copy_to_user(data.in.templateName, templateName, data.in.templateNameLength))
			return -EFAULT;
		/* coverity[tainted_data : FALSE] */
		if (copy_to_user(data.in.localName, localName, data.in.localNameLength))
			return -EFAULT;
	}
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentListHeader(struct cm_process_priv *procPriv,
				       CM_GetComponentListHeader_t __user *param)
{
	CM_GetComponentListHeader_t data;

	data.out.error = CM_ENGINE_GetComponentListHeader(procPriv->pid,
							  &data.out.headerComponent);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentListNext(struct cm_process_priv *procPriv,
				     CM_GetComponentListNext_t __user *param)
{
	CM_GetComponentListNext_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentListNext(procPriv->pid,
							data.in.prevComponent,
							&data.out.nextComponent);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentRequiredInterfaceNumber(struct cm_process_priv *procPriv,
						    CM_GetComponentRequiredInterfaceNumber_t __user *param)
{
	CM_GetComponentRequiredInterfaceNumber_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentRequiredInterfaceNumber(data.in.component,
								       &data.out.numberRequiredInterfaces);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentRequiredInterface(struct cm_process_priv *procPriv,
					      CM_GetComponentRequiredInterface_t __user *param)
{
	CM_GetComponentRequiredInterface_t data;
	char itfName[MAX_INTERFACE_NAME_LENGTH];
	char itfType[MAX_INTERFACE_TYPE_NAME_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentRequiredInterface(data.in.component,
								 data.in.index,
								 itfName,
								 data.in.itfNameLength,
								 itfType,
								 data.in.itfTypeLength,
								 &data.out.requireState,
								 &data.out.collectionSize);

	/* Copy results back to userspace */
	if (data.out.error == CM_OK) {
		/* coverity[tainted_data : FALSE] */
		if (copy_to_user(data.in.itfName, itfName, data.in.itfNameLength))
			return -EFAULT;
		/* coverity[tainted_data : FALSE] */
		if (copy_to_user(data.in.itfType, itfType, data.in.itfTypeLength))
			return -EFAULT;
	}
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentRequiredInterfaceBinding(struct cm_process_priv *procPriv,
						     CM_GetComponentRequiredInterfaceBinding_t __user *param)
{
	CM_GetComponentRequiredInterfaceBinding_t data;
	char itfName[MAX_INTERFACE_NAME_LENGTH];
	char serverItfName[MAX_INTERFACE_NAME_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;
	if ((data.out.error = copy_string_from_user(itfName,
						    data.in.itfName,
						    sizeof(itfName))))
		goto out;

	data.out.error = CM_ENGINE_GetComponentRequiredInterfaceBinding(data.in.component,
									itfName,
									&data.out.server,
									serverItfName,
									data.in.serverItfNameLength);

	/* Copy results back to userspace */
	if (data.out.error != CM_OK)
		goto out;

	/* coverity[tainted_data : FALSE] */
	if (copy_to_user(data.in.serverItfName, serverItfName, data.in.serverItfNameLength))
		return -EFAULT;
out:
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentProvidedInterfaceNumber(struct cm_process_priv *procPriv,
						    CM_GetComponentProvidedInterfaceNumber_t __user *param)
{
	CM_GetComponentProvidedInterfaceNumber_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentProvidedInterfaceNumber(data.in.component,
								       &data.out.numberProvidedInterfaces);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentProvidedInterface(struct cm_process_priv *procPriv,
					      CM_GetComponentProvidedInterface_t __user *param)
{
	CM_GetComponentProvidedInterface_t data;
	char itfName[MAX_INTERFACE_NAME_LENGTH];
	char itfType[MAX_INTERFACE_TYPE_NAME_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentProvidedInterface(data.in.component,
								 data.in.index,
								 itfName,
								 data.in.itfNameLength,
								 itfType,
								 data.in.itfTypeLength,
								 &data.out.collectionSize);

	/* Copy results back to userspace */
	if (data.out.error == CM_OK) {
		/* coverity[tainted_data : FALSE] */
		if (copy_to_user(data.in.itfName, itfName, data.in.itfNameLength))
			return -EFAULT;
		/* coverity[tainted_data : FALSE] */
		if (copy_to_user(data.in.itfType, itfType, data.in.itfTypeLength))
			return -EFAULT;
	}
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentPropertyNumber(struct cm_process_priv *procPriv,
					   CM_GetComponentPropertyNumber_t __user *param)
{
	CM_GetComponentPropertyNumber_t data;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentPropertyNumber(data.in.component,
							      &data.out.numberProperties);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentPropertyName(struct cm_process_priv *procPriv,
					 CM_GetComponentPropertyName_t __user *param)
{
	CM_GetComponentPropertyName_t data;
	char propertyName[MAX_PROPERTY_NAME_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_GetComponentPropertyName(data.in.component,
							    data.in.index,
							    propertyName,
							    data.in.propertyNameLength);

	/* Copy results back to userspace */
	/* coverity[tainted_data : FALSE] */
	if ((data.out.error == CM_OK) &&
	    copy_to_user(data.in.propertyName, propertyName, data.in.propertyNameLength))
		return -EFAULT;
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetComponentPropertyValue(struct cm_process_priv *procPriv,
					  CM_GetComponentPropertyValue_t __user *param)
{
	CM_GetComponentPropertyValue_t data;
	char propertyName[MAX_PROPERTY_NAME_LENGTH];
	char propertyValue[MAX_PROPERTY_VALUE_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(propertyName,
						    data.in.propertyName,
						    sizeof(propertyName))))
		goto out;

	data.out.error = CM_ENGINE_GetComponentPropertyValue(data.in.component,
							     propertyName,
							     propertyValue,
							     data.in.propertyValueLength);
	/* Copy results back to userspace */
	/* coverity[tainted_data : FALSE] */
	if ((data.out.error == CM_OK) &&
	    copy_to_user(data.in.propertyValue, propertyValue, data.in.propertyValueLength))
		return -EFAULT;
out:
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_ReadComponentAttribute(struct cm_process_priv *procPriv,
				       CM_ReadComponentAttribute_t __user *param)
{
	CM_ReadComponentAttribute_t data;
	char attrName[MAX_ATTRIBUTE_NAME_LENGTH];

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(attrName,
						    data.in.attrName,
						    sizeof(attrName))))
		goto out;

	data.out.error = CM_ENGINE_ReadComponentAttribute(data.in.component,
							  attrName,
							  &data.out.value);
out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}


inline int cmld_WriteComponentAttribute(struct cm_process_priv *procPriv,
                               CM_WriteComponentAttribute_t __user *param)
{
    CM_WriteComponentAttribute_t data;
    char attrName[MAX_ATTRIBUTE_NAME_LENGTH];

    /* Copy user input data in kernel space */
    if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
        return -EFAULT;

    if ((data.out.error = copy_string_from_user(attrName,
                            data.in.attrName,
                            sizeof(attrName))))
        goto out;

    data.out.error = CM_ENGINE_WriteComponentAttribute(data.in.component,
                            attrName,
                            data.in.value);
out:
    /* Copy results back to userspace */
    if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
        return -EFAULT;
    return 0;
}


inline int cmld_GetExecutiveEngineHandle(struct cm_process_priv *procPriv,
					 CM_GetExecutiveEngineHandle_t __user *param)
{
	CM_GetExecutiveEngineHandle_t data;

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_GetExecutiveEngineHandle(data.in.domainId,
							     &data.out.executiveEngineHandle);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_SetMode(CM_SetMode_t __user *param)
{
	CM_SetMode_t data;

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_SetMode(data.in.aCmdID, data.in.aParam);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_GetRequiredComponentFiles(struct cm_process_priv *procPriv,
					  CM_GetRequiredComponentFiles_t __user *param)
{
	CM_GetRequiredComponentFiles_t data;
	char components[4][MAX_INTERFACE_TYPE_NAME_LENGTH];
	char requiredItfClientName[MAX_INTERFACE_NAME_LENGTH];
	char providedItfServerName[MAX_INTERFACE_NAME_LENGTH];
	char type[MAX_INTERFACE_TYPE_NAME_LENGTH];
	unsigned int i;
	int err;

	/* Copy user input data in kernel space */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if (data.in.requiredItfClientName &&
	    (data.out.error = copy_string_from_user(requiredItfClientName,
						    data.in.requiredItfClientName,
						    sizeof(requiredItfClientName))))
		goto out;

	if (data.in.providedItfServerName &&
	    (data.out.error = copy_string_from_user(providedItfServerName,
						    data.in.providedItfServerName,
						    sizeof(providedItfServerName))))
		goto out;

	data.out.error = CM_ENGINE_GetRequiredComponentFiles(data.in.action,
							     data.in.client,
							     requiredItfClientName,
							     data.in.server,
							     providedItfServerName,
							     components,
							     data.in.listSize,
							     data.in.type ? type : NULL,
							     &data.out.methodNumber);

	if (data.out.error)
		goto out;

	if (data.in.fileList) {
		/* Copy results back to userspace */
		for (i=0; i<data.in.listSize; i++) {
			err = copy_to_user(&((char*)data.in.fileList)[i*MAX_INTERFACE_TYPE_NAME_LENGTH], components[i], MAX_INTERFACE_TYPE_NAME_LENGTH);
			if (err)
				return -EFAULT;
		}
	}
	if (data.in.type
	    && copy_to_user(data.in.type, type, MAX_INTERFACE_TYPE_NAME_LENGTH))
			return -EFAULT;
out:
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_Migrate(CM_Migrate_t __user *param)
{
	CM_Migrate_t data;

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	data.out.error = CM_ENGINE_Migrate(data.in.srcShared, data.in.src, data.in.dst);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_Unmigrate(CM_Unmigrate_t __user *param)
{
	CM_Unmigrate_t data;

	data.out.error = CM_ENGINE_Unmigrate();

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

int cmld_SetupRelinkArea(struct cm_process_priv *procPriv,
			 CM_SetupRelinkArea_t __user *param)
{
	CM_SetupRelinkArea_t data;
	struct list_head *cursor, *next;
	struct memAreaDesc_t *entry = NULL;

	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;


	/* check that it is actually owned by the process */
	data.out.error = CM_UNKNOWN_MEMORY_HANDLE;

	if (lock_process(procPriv))
		return -ERESTARTSYS;
	list_for_each_safe(cursor, next, &procPriv->memAreaDescList){
		entry = list_entry(cursor, struct memAreaDesc_t, list);
		if (entry->handle == data.in.mem_handle)
			break;
	}
	unlock_process(procPriv);

	if ((entry == NULL) || (entry->handle != data.in.mem_handle))
		goto out;

	if (entry->size < data.in.segments * data.in.segmentsize)
	{
		data.out.error = CM_INVALID_PARAMETER;
		goto out;
	}

	data.out.error = cmdma_setup_relink_area(
		entry->physAddr,
		data.in.peripheral_addr,
		data.in.segments,
		data.in.segmentsize,
		data.in.LOS,
		data.in.type);
out:
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;

	return 0;
}


inline int cmld_PushComponent(CM_PushComponent_t __user *param)
{
	CM_PushComponent_t data;
	char name[MAX_INTERFACE_TYPE_NAME_LENGTH];
	void *dataFile = NULL;

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(name,
						    data.in.name,
						    sizeof(name))))
		goto out;

	if (data.in.data != NULL) {
		dataFile = OSAL_Alloc(data.in.size);
		if (dataFile == NULL) {
			data.out.error = CM_NO_MORE_MEMORY;
			goto out;
		}
		/* coverity[tainted_data : FALSE] */
		if (copy_from_user(dataFile, data.in.data, data.in.size))
			data.out.error = CM_INVALID_PARAMETER;
		else
			data.out.error = CM_ENGINE_PushComponent(name, dataFile,
								 data.in.size);
		OSAL_Free(dataFile);
	}

out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_ReleaseComponent(CM_ReleaseComponent_t __user *param)
{
	CM_ReleaseComponent_t data;
	char name[MAX_INTERFACE_TYPE_NAME_LENGTH];

	/* Copy user input data in kernel space */
	/* coverity[tainted_data_argument : FALSE] */
	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if ((data.out.error = copy_string_from_user(name,
						    data.in.name,
						    sizeof(name))))
		goto out;

	/* coverity[tainted_data : FALSE] */
	data.out.error = CM_ENGINE_ReleaseComponent(name);

out:
	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

inline int cmld_PrivGetMPCMemoryDesc(struct cm_process_priv *procPriv, CM_PrivGetMPCMemoryDesc_t __user *param)
{
	CM_PrivGetMPCMemoryDesc_t data;
	struct list_head* cursor;

	if (copy_from_user(&data.in, &param->in, sizeof(data.in)))
		return -EFAULT;

	if (lock_process(procPriv))
		return -ERESTARTSYS;
	/* Scan the memory descriptors list looking for the requested handle */
	data.out.error = CM_UNKNOWN_MEMORY_HANDLE;
	list_for_each(cursor, &procPriv->memAreaDescList) {
		struct memAreaDesc_t* curr;
		curr = list_entry(cursor, struct memAreaDesc_t, list);
		if (curr->handle == data.in.handle) {
			data.out.size = curr->size;
			data.out.physAddr = curr->physAddr;
			data.out.kernelLogicalAddr = curr->kernelLogicalAddr;
			data.out.userLogicalAddr = curr->userLogicalAddr;
			data.out.mpcPhysAddr = curr->mpcPhysAddr;
			data.out.error = CM_OK;
			break;
		}
	}
	unlock_process(procPriv);

	/* Copy results back to userspace */
	if (copy_to_user(&param->out, &data.out, sizeof(data.out)))
		return -EFAULT;
	return 0;
}

int cmld_PrivReserveMemory(struct cm_process_priv *procPriv, unsigned int physAddr)
{
	struct list_head* cursor;
	struct memAreaDesc_t* curr;
	int err = -ENXIO;

	if (lock_process(procPriv))
		return -ERESTARTSYS;
	list_for_each(cursor, &procPriv->memAreaDescList) {
		curr = list_entry(cursor, struct memAreaDesc_t, list);
		if (curr->physAddr == physAddr) {
			/* Mark this memory area reserved for a mapping for this thread ID */
			/* It must not be already reserved but this should not happen */
			if (curr->tid) {
				pr_err("%s: thread %d can't reseveved memory %x already "
				       "reserved for %d\n",
				       __func__, current->pid, physAddr, curr->tid);
				err = -EBUSY;
			} else {
				curr->tid = current->pid;
				err = 0;
			}
			break;
		}
	}
	unlock_process(procPriv);
	return err;
}
