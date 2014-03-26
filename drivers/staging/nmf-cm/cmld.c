/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

/** \file cmld.c
 *
 * Nomadik Multiprocessing Framework Linux Driver
 *
 */

#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <cm/inc/cm_def.h>
#include <cm/engine/api/cm_engine.h>
#include <cm/engine/api/control/irq_engine.h>

#include "osal-kernel.h"
#include "cmld.h"
#include "cmioctl.h"
#include "cm_debug.h"
#include "cm_service.h"
#include "cm_dma.h"

#define CMDRIVER_PATCH_VERSION 122
#define O_FLUSH 0x1000000

static int cmld_major;
static struct cdev cmld_cdev;
static struct class cmld_class = {
	.name = "cm",
	.owner = THIS_MODULE,
};
const char *cmld_devname[] = CMLD_DEV_NAME;
static struct device *cmld_dev[ARRAY_SIZE(cmld_devname)];

/* List of per process structure (struct cm_process_priv list) */
LIST_HEAD(process_list);
static DEFINE_MUTEX(process_lock); /* lock used to protect previous list */
/* List of per channel structure (struct cm_channel_priv list).
   A channel == One file descriptor */
LIST_HEAD(channel_list);
static DEFINE_MUTEX(channel_lock); /* lock used to protect previous list */

#ifdef CONFIG_DEBUG_FS
/* Debugfs support */
bool cmld_user_has_debugfs = false;
pid_t cmld_dump_ongoing = 0;
module_param(cmld_dump_ongoing, uint, S_IRUGO);
static DECLARE_WAIT_QUEUE_HEAD(dump_waitq);
#endif

static inline struct cm_process_priv *getProcessPriv(void)
{
	struct list_head* head;
	struct cm_process_priv *entry;

	mutex_lock(&process_lock);

	/* Look for an entry for the calling process */
	list_for_each(head, &process_list) {
		entry = list_entry(head, struct cm_process_priv, entry);
		if (entry->pid == current->tgid) {
			kref_get(&entry->ref);
			goto out;
		}
	}
	mutex_unlock(&process_lock);

	/* Allocate, init and register a new one otherwise */
	entry = OSAL_Alloc(sizeof(*entry));
	if (entry == NULL)
		return ERR_PTR(-ENOMEM);

	/* init host2mpcLock */
	mutex_init(&entry->host2mpcLock);

	INIT_LIST_HEAD(&entry->memAreaDescList);
	kref_init(&entry->ref);
	mutex_init(&entry->mutex);

	entry->pid = current->tgid;
	mutex_lock(&process_lock);
	list_add(&entry->entry, &process_list);
	cm_debug_proc_init(entry);
out:
	mutex_unlock(&process_lock);
	return entry;
}

/* Free all messages */
static inline void freeMessages(struct cm_channel_priv* channelPriv)
{
	struct osal_msg *this, *next;
	int warn = 0;

	spin_lock_bh(&channelPriv->bh_lock);
	plist_for_each_entry_safe(this, next, &channelPriv->messageQueue, msg_entry) {
		plist_del(&this->msg_entry, &channelPriv->messageQueue);
		kfree(this);
		warn = 1;
	}
	spin_unlock_bh(&channelPriv->bh_lock);
	if (warn)
		pr_err("[CM - PID=%d]: Some remaining"
		       " message(s) freed\n", current->tgid);
}

/* Free all pending memory areas and relative descriptors  */
static inline void freeMemHandles(struct cm_process_priv* processPriv)
{
	struct list_head* head, *next;
	int warn = 0;
			
	list_for_each_safe(head, next, &processPriv->memAreaDescList) {
		struct memAreaDesc_t* curr;
		int err;
		curr = list_entry(head, struct memAreaDesc_t, list);
		err=CM_ENGINE_FreeMpcMemory(curr->handle);
		if (err)
			pr_err("[CM - PID=%d]: Error (%d) freeing remaining memory area "
			       "handle\n", current->tgid, err);
		list_del(head);
		OSAL_Free(curr);
		warn = 1;
	}
	if (warn) {
		pr_err("[CM - PID=%d]: Some remaining memory area "
		       "handle(s) freed\n", current->tgid);
		warn = 0;
	}
}

/* Free any skeleton, called when freeing the process entry */
static inline void freeSkelList(struct list_head* skelList)
{
	struct list_head* head, *next;
	int warn = 0;

	/* No lock held, we know that we are the only and the last user
	   of the list */
	list_for_each_safe(head, next, skelList) {
		t_skelwrapper* curr;
		curr = list_entry(head, t_skelwrapper, entry);
		list_del(head);
		OSAL_Free(curr);
		warn = 1;
	}
	if (warn)
		pr_err("[CM - PID=%d]: Some remaining skeleton "
		       "wrapper(s) freed\n", current->tgid);
}

/* Free any remaining channels belonging to this process */
/* Called _only_ when freeing the process entry, once the network constructed by
   this process has been destroyed.
   See cmld_release() to see why there can be some remaining non-freed channels */
static inline void freeChannels(struct cm_process_priv* processPriv)
{
	struct list_head* head, *next;
	int warn = 0;

	mutex_lock(&channel_lock);
	list_for_each_safe(head, next, &channel_list) {
		struct cm_channel_priv *channelPriv;
		channelPriv = list_entry(head, struct cm_channel_priv, entry);
		/* Only channels belonging to this process are concerned */
		if (channelPriv->proc == processPriv) {
			tasklet_disable(&cmld_service_tasklet);
			list_del(&channelPriv->entry);
			tasklet_enable(&cmld_service_tasklet);

			/* Free all remaining messages if any
			   (normally none, but double check) */
			freeMessages(channelPriv);

			/* Free any pending skeleton wrapper */
			/* Here it's safe, we know that all bindings have been undone */
			freeSkelList(&channelPriv->skelList);

			/* Free the per-channel descriptor */
			OSAL_Free(channelPriv);
			warn = 1;
		}
	}
	mutex_unlock(&channel_lock);

	if (warn)
		pr_err("[CM - PID=%d]: Some remaining channel entries "
		       "freed\n", current->tgid);
}

/* Free the process priv structure and all related stuff */
/* Called only when the last ref to this structure is released */
static void freeProcessPriv(struct kref *ref)
{
	struct cm_process_priv *entry = container_of(ref, struct cm_process_priv, ref);
	t_nmf_error err;

	mutex_lock(&process_lock);
	list_del(&entry->entry);
	mutex_unlock(&process_lock);

	/* Destroy all remaining components */
	err=CM_ENGINE_FlushComponents(entry->pid);
	if (err != NMF_OK)
		pr_err("[CM - PID=%d]: Error while flushing some remaining"
		       " components: error=%d\n", current->tgid, err);

	freeChannels(entry);

	/* Free any pending memory areas and relative descriptors */
	freeMemHandles(entry);

	/* Destroy all remaining domains */
	err=CM_ENGINE_FlushMemoryDomains(entry->pid);
	if (err != NMF_OK)
		pr_err("[CM - PID=%d]: Error while flushing some remaining"
		       " domains: error=%d\n", current->tgid, err);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(entry->dir);
	if (cmld_dump_ongoing == entry->pid) {
		cmld_dump_ongoing = 0;
		wake_up(&dump_waitq);
	}
#endif

	/* Free the per-process descriptor */
	OSAL_Free(entry);
}

/** Reads Component Manager messages destinated to this process.
 * The message is composed by three fields:
 * 1) mpc2host handle (distinguishes interfaces)
 * 2) methodIndex (distinguishes interface's methods)
 * 3) Variable length parameters (method's parameters values)
 *
 * \note cfr GetEvent()
 * \return POSIX error code
 */
static ssize_t cmld_channel_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int err = 0;
	struct cm_channel_priv* channelPriv = file->private_data;
	int msgSize = 0;
	struct plist_head* messageQueue;
	struct osal_msg* msg;
	t_os_message *os_msg = (t_os_message *)buf;
	int block = !(file->f_flags & O_NONBLOCK);

	messageQueue = &channelPriv->messageQueue;
 
	if (mutex_lock_killable(&channelPriv->msgQueueLock))
		return -ERESTARTSYS;

wait:
	while (plist_head_empty(messageQueue)) {
		mutex_unlock(&channelPriv->msgQueueLock);
		if (block == 0)
			return -EAGAIN;
		/* Wait until there is a message to ferry up */
		if (wait_event_interruptible(channelPriv->waitq, ((!plist_head_empty(messageQueue)) || (file->f_flags & O_FLUSH))))
			return -ERESTARTSYS;
		if (file->f_flags & O_FLUSH) {
			file->f_flags &= ~O_FLUSH;
			return 0;
		}
		if (mutex_lock_killable(&channelPriv->msgQueueLock))
			return -ERESTARTSYS;
	}

	/* Pick up the first message from the queue, making sure that the
	 * hwsem tasklet does not wreak havoc the queue in the meantime
	 */
	spin_lock_bh(&channelPriv->bh_lock);
	msg = plist_first_entry(messageQueue, struct osal_msg, msg_entry);
	plist_del(&msg->msg_entry, messageQueue);
	spin_unlock_bh(&channelPriv->bh_lock);

	switch (msg->msg_type) {
	case MSG_INTERFACE: {

		/* Check if enough space is available */
		msgSize = sizeof(msg->msg_type) + msg->d.itf.ptrSize + sizeof(os_msg->data.itf) - sizeof(os_msg->data.itf.params) ;
		if (msgSize > count) {
			mutex_unlock(&channelPriv->msgQueueLock);
			pr_err("CM: message size bigger than buffer size silently ignored!\n");
			err = -EMSGSIZE;
			goto out;
		}

		/* Copy to user message type */
		err = put_user(msg->msg_type, &os_msg->type);
		if (err) goto ack_evt;
	
		/* Copy to user the t_nmf_mpc2host_handle */
		err = put_user(msg->d.itf.skelwrap->upperLayerThis, &os_msg->data.itf.THIS);
		if (err) goto ack_evt;

		/* The methodIndex  */
		err = put_user(msg->d.itf.methodIdx, &os_msg->data.itf.methodIndex);
		if (err) goto ack_evt;

		/* And the parameters */
		err = copy_to_user(os_msg->data.itf.params, msg->d.itf.anyPtr, msg->d.itf.ptrSize);
	
		ack_evt:
		/* This call is void */
		/* Note: that we cannot release the lock before having called this function
		   as acknowledgements MUST be executed in the same order as their
		   respective messages have arrived! */
		CM_ENGINE_AcknowledgeEvent(msg->d.itf.skelwrap->mpc2hostId);

		mutex_unlock(&channelPriv->msgQueueLock);
		break;
	}
	case MSG_SERVICE: {
		mutex_unlock(&channelPriv->msgQueueLock);
		msgSize = sizeof(msg->msg_type) + sizeof(msg->d.srv.srvType)
			+ sizeof(msg->d.srv.srvData);
		if (count < msgSize) {
			pr_err("CM: service message size bigger than buffer size - silently ignored!\n");
			err = -EMSGSIZE;
		}

		/* Copy to user message type */
		err = put_user(msg->msg_type, &os_msg->type);
		if (err) goto out;
		err = copy_to_user(&os_msg->data.srv, &msg->d.srv,
				   sizeof(msg->d.srv.srvType) + sizeof(msg->d.srv.srvData));
		break;
	}
	default:
		mutex_unlock(&channelPriv->msgQueueLock);
		pr_err("CM: invalid message type %d discarded\n", msg->msg_type);
		goto wait;
	}
out:
	/* Destroy the message */
	kfree(msg);

	return err ? err : msgSize;
}

/** Part of driver's release method. (ie userspace close())
 * It wakes up all waiter.
 *
 * \return POSIX error code
 */
static int cmld_channel_flush(struct file *file, fl_owner_t id)
{
	struct cm_channel_priv* channelPriv = file->private_data;
	/*
	 * Protect against flush called at fork(): we don't want to generate
	 * flush messages when called from child processes
	 */
	if (current->tgid == channelPriv->proc->pid) {
		file->f_flags |= O_FLUSH;
		wake_up_all(&channelPriv->waitq);
	}
	return 0;
}

static long cmld_channel_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cm_channel_priv *channelPriv = file->private_data;
#ifdef CONFIG_DEBUG_FS
	if (wait_event_interruptible(dump_waitq, (!cmld_dump_ongoing)))
		return -ERESTARTSYS;
#endif

	switch(cmd) {
		/*
		 * All channel CM SYSCALL
		 */
	case CM_BINDCOMPONENTTOCMCORE:
		return cmld_BindComponentToCMCore(channelPriv, (CM_BindComponentToCMCore_t *)arg);
	case CM_FLUSHCHANNEL:
		return cmld_channel_flush(file, 0);
	default:
		pr_err("CM(%s): unsupported command %i\n", __func__, cmd);
		return -EINVAL;
	}
	return 0;
}

static long cmld_control_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cm_process_priv* procPriv = file->private_data;
#ifdef CONFIG_DEBUG_FS
	if (cmd == CM_PRIV_DEBUGFS_DUMP_DONE) {
		cmld_dump_ongoing = 0;
		wake_up(&dump_waitq);
		return 0;
	} else if (wait_event_interruptible(dump_waitq, (!cmld_dump_ongoing)))
		return -ERESTARTSYS;
#endif

	switch(cmd) {
		/*
		 * All wrapped CM SYSCALL
		 */
	case CM_INSTANTIATECOMPONENT:
		return cmld_InstantiateComponent(procPriv,
						 (CM_InstantiateComponent_t *)arg);

	case CM_BINDCOMPONENTFROMCMCORE:
		return cmld_BindComponentFromCMCore(procPriv,
						    (CM_BindComponentFromCMCore_t *)arg);

	case CM_UNBINDCOMPONENTFROMCMCORE:
		return cmld_UnbindComponentFromCMCore((CM_UnbindComponentFromCMCore_t *)arg);

	case CM_UNBINDCOMPONENTTOCMCORE:
		return cmld_UnbindComponentToCMCore(procPriv, (CM_UnbindComponentToCMCore_t *)arg);

	case CM_BINDCOMPONENTASYNCHRONOUS:
		return cmld_BindComponentAsynchronous(procPriv, (CM_BindComponentAsynchronous_t *)arg);

	case CM_UNBINDCOMPONENTASYNCHRONOUS:
		return cmld_UnbindComponentAsynchronous(procPriv, (CM_UnbindComponentAsynchronous_t *)arg);

	case CM_BINDCOMPONENT:
		return cmld_BindComponent(procPriv, (CM_BindComponent_t *)arg);

	case CM_UNBINDCOMPONENT:
		return cmld_UnbindComponent(procPriv, (CM_UnbindComponent_t *)arg);

	case CM_BINDCOMPONENTTOVOID:
		return cmld_BindComponentToVoid(procPriv, (CM_BindComponentToVoid_t *)arg);

	case CM_DESTROYCOMPONENT:
		return cmld_DestroyComponent(procPriv, (CM_DestroyComponent_t *)arg);

	case CM_CREATEMEMORYDOMAIN:
		return cmld_CreateMemoryDomain(procPriv, (CM_CreateMemoryDomain_t *)arg);

	case CM_CREATEMEMORYDOMAINSCRATCH:
		return cmld_CreateMemoryDomainScratch(procPriv, (CM_CreateMemoryDomainScratch_t *)arg);

	case CM_DESTROYMEMORYDOMAIN:
		return cmld_DestroyMemoryDomain((CM_DestroyMemoryDomain_t *)arg);

	case CM_GETDOMAINCOREID:
		return cmld_GetDomainCoreId((CM_GetDomainCoreId_t *)arg);

	case CM_ALLOCMPCMEMORY:
		return cmld_AllocMpcMemory(procPriv, (CM_AllocMpcMemory_t *)arg);

	case CM_FREEMPCMEMORY:
		return cmld_FreeMpcMemory(procPriv, (CM_FreeMpcMemory_t *)arg);

	case CM_GETMPCMEMORYSTATUS:
		return cmld_GetMpcMemoryStatus((CM_GetMpcMemoryStatus_t *)arg);

	case CM_STARTCOMPONENT:
		return cmld_StartComponent(procPriv, (CM_StartComponent_t *)arg);

	case CM_STOPCOMPONENT:
		return cmld_StopComponent(procPriv, (CM_StopComponent_t *)arg);

	case CM_GETMPCLOADCOUNTER:
		return cmld_GetMpcLoadCounter((CM_GetMpcLoadCounter_t *)arg);

	case CM_GETCOMPONENTDESCRIPTION:
		return cmld_GetComponentDescription(procPriv, (CM_GetComponentDescription_t *)arg);

	case CM_GETCOMPONENTLISTHEADER:
		return cmld_GetComponentListHeader(procPriv, (CM_GetComponentListHeader_t *)arg);

	case CM_GETCOMPONENTLISTNEXT:
		return cmld_GetComponentListNext(procPriv, (CM_GetComponentListNext_t *)arg);

	case CM_GETCOMPONENTREQUIREDINTERFACENUMBER:
		return cmld_GetComponentRequiredInterfaceNumber(procPriv,
								(CM_GetComponentRequiredInterfaceNumber_t *)arg);

	case CM_GETCOMPONENTREQUIREDINTERFACE:
		return cmld_GetComponentRequiredInterface(procPriv,
							  (CM_GetComponentRequiredInterface_t *)arg);

	case CM_GETCOMPONENTREQUIREDINTERFACEBINDING:
		return cmld_GetComponentRequiredInterfaceBinding(procPriv,
								 (CM_GetComponentRequiredInterfaceBinding_t *)arg);

	case CM_GETCOMPONENTPROVIDEDINTERFACENUMBER:
		return cmld_GetComponentProvidedInterfaceNumber(procPriv,
								(CM_GetComponentProvidedInterfaceNumber_t *)arg);

	case CM_GETCOMPONENTPROVIDEDINTERFACE:
		return cmld_GetComponentProvidedInterface(procPriv,
							  (CM_GetComponentProvidedInterface_t *)arg);

	case CM_GETCOMPONENTPROPERTYNUMBER:
		return cmld_GetComponentPropertyNumber(procPriv,
						       (CM_GetComponentPropertyNumber_t *)arg);

	case CM_GETCOMPONENTPROPERTYNAME:
		return cmld_GetComponentPropertyName(procPriv,
						     (CM_GetComponentPropertyName_t *)arg);

	case CM_GETCOMPONENTPROPERTYVALUE:
		return cmld_GetComponentPropertyValue(procPriv,
						      (CM_GetComponentPropertyValue_t *)arg);

	case CM_READCOMPONENTATTRIBUTE:
		return cmld_ReadComponentAttribute(procPriv,
						   (CM_ReadComponentAttribute_t *)arg);

	case CM_GETEXECUTIVEENGINEHANDLE:
		return cmld_GetExecutiveEngineHandle(procPriv,
						     (CM_GetExecutiveEngineHandle_t *)arg);

	case CM_SETMODE:
		return cmld_SetMode((CM_SetMode_t *)arg);

	case CM_GETREQUIREDCOMPONENTFILES:
		return cmld_GetRequiredComponentFiles(procPriv,
						      (CM_GetRequiredComponentFiles_t *)arg);

	case CM_MIGRATE:
		return cmld_Migrate((CM_Migrate_t *)arg);

	case CM_UNMIGRATE:
		return cmld_Unmigrate((CM_Unmigrate_t *)arg);

	case CM_SETUPRELINKAREA:
		return cmld_SetupRelinkArea(procPriv,
					    (CM_SetupRelinkArea_t *)arg);

	case CM_PUSHCOMPONENT:
		return cmld_PushComponent((CM_PushComponent_t *)arg);

	case CM_RELEASECOMPONENT:
		return cmld_ReleaseComponent((CM_ReleaseComponent_t *)arg);

		/*
		 * NMF CALLS (Host->MPC bindings)
		 */
	case CM_PUSHEVENTWITHSIZE: {
		CM_PushEventWithSize_t data;
		t_event_params_handle event;

		/* coverity[tainted_data_argument : FALSE] */
		if (copy_from_user(&data.in, (CM_PushEventWithSize_t*)arg, sizeof(data.in)))
			return -EFAULT;

		/* Take the lock to synchronize CM_ENGINE_AllocEvent()
		 * and CM_ENGINE_PushEvent()
		 */
		if (mutex_lock_killable(&procPriv->host2mpcLock))
			return -ERESTARTSYS;

		event = CM_ENGINE_AllocEvent(data.in.host2mpcId);
		if (event == NULL) {
			mutex_unlock(&procPriv->host2mpcLock);
			return put_user(CM_PARAM_FIFO_OVERFLOW,
					&((CM_PushEventWithSize_t*)arg)->out.error);
		}
		if (data.in.size != 0)
			/* coverity[tainted_data : FALSE] */
			if (copy_from_user(event, data.in.h, data.in.size)) {
				mutex_unlock(&procPriv->host2mpcLock);
				return -EFAULT; // TODO: what about the already allocated and acknowledged event!?!
			}

		data.out.error = CM_ENGINE_PushEvent(data.in.host2mpcId, event, data.in.methodIndex);
		mutex_unlock(&procPriv->host2mpcLock);
 
		/* copy error value back */
		return put_user(data.out.error, &((CM_PushEventWithSize_t*)arg)->out.error);
	}
	
		/*
		 * All private (internal) commands
		 */
	case CM_PRIVGETMPCMEMORYDESC:
		return cmld_PrivGetMPCMemoryDesc(procPriv, (CM_PrivGetMPCMemoryDesc_t *)arg);

	case CM_PRIVRESERVEMEMORY:
		return cmld_PrivReserveMemory(procPriv, arg);

	case CM_GETVERSION: {
		t_uint32 nmfversion = NMF_VERSION;
		return copy_to_user((void*)arg, &nmfversion, sizeof(nmfversion));
	}
	case CM_PRIV_GETBOARDVERSION: {
		enum board_version v = U8500_V2;
		return copy_to_user((void*)arg, &v, sizeof(v));
	}
	case CM_PRIV_ISCOMPONENTCACHEEMPTY:
		if (CM_ENGINE_IsComponentCacheEmpty())
			return 0;
		else
			return -ENOENT;
	case CM_PRIV_DEBUGFS_READY:
#ifdef CONFIG_DEBUG_FS
		cmld_user_has_debugfs = true;
#endif
		return 0;
	case CM_PRIV_DEBUGFS_WAIT_DUMP:
		return 0;

	case CM_WRITECOMPONENTATTRIBUTE:
		return cmld_WriteComponentAttribute(procPriv,
            (CM_WriteComponentAttribute_t *)arg);

	default:
		pr_err("CM(%s): unsupported command %i\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

/** VMA open callback function
 */
static void cmld_vma_open(struct vm_area_struct* vma) {
	struct memAreaDesc_t* curr = (struct memAreaDesc_t*)vma->vm_private_data;

	atomic_inc(&curr->count);
}

/** VMA close callback function
 */
static void cmld_vma_close(struct vm_area_struct* vma) {
	struct memAreaDesc_t* curr = (struct memAreaDesc_t*)vma->vm_private_data;
	
	atomic_dec(&curr->count);
}

static struct vm_operations_struct cmld_remap_vm_ops = {
	.open = cmld_vma_open,
	.close = cmld_vma_close,
};

/** mmap implementation.
 * Remaps just once.
 * 
 * \return POSIX error code
 */
static int cmld_control_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct list_head* listHead;
	struct list_head* cursor;
	struct cm_process_priv* procPriv = file->private_data;
	struct memAreaDesc_t* curr = NULL;
	unsigned int vma_size = vma->vm_end-vma->vm_start;

	listHead = &procPriv->memAreaDescList;

	if (lock_process(procPriv)) return -ERESTARTSYS;
	/* Make sure the memory area has not already been remapped */
	list_for_each(cursor, listHead) {
		curr = list_entry(cursor, struct memAreaDesc_t, list);
		/* For now, the user space aligns any requested physaddr to a page-size limit
		   This is not safe and must be fixed. But this is the only way to
		   minimize the allocated TCM memory, needed because of low amount of
		   TCM memory 
		   Another way is to add some more check before doing this mmap()
		   to allow this mmap, for example.
		   NOTE: this memory must be first reserved via the CM_PRIVRESERVEMEMORY ioctl()
		*/
		if ((curr->physAddr&PAGE_MASK) == offset &&
		    curr->tid == current->pid) {
			if (curr->userLogicalAddr) {
				unlock_process(procPriv);
				return -EINVAL; // already mapped!
			}
			/* reset the thread id value, to not confuse any further mmap() */
			curr->tid = 0;
			break;
		}
	}

	if (cursor == listHead) {
		unlock_process(procPriv);
		return -EINVAL; // no matching memory area descriptor found!
	}
	
	/* Very, very important to have consistent buffer transition */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_RESERVED | VM_IO | VM_DONTEXPAND | VM_DONTCOPY;
	
	if (remap_pfn_range(vma, vma->vm_start, offset>>PAGE_SHIFT,
			    vma_size, vma->vm_page_prot)) {
		unlock_process(procPriv);
		return -EAGAIN;
	}
 
	/* Offset represents the physical address.
	 * Update the list entry filling in the logical address assigned to the user
	 */
	/*
	 * NOTE: here the useLogicalAddr is page-aligned, but not necessaly the
	 *       phycical address. We mmap() more than originaly requested by the
	 *       user, see in CM User Proxy (file cmsyscallwrapper.c)
	 */
	curr->userLogicalAddr = vma->vm_start;

	/* increment reference counter */
	atomic_inc(&curr->count);
	
	unlock_process(procPriv);

	/* set private data structure and callbacks */
	vma->vm_private_data = (void *)curr;
	vma->vm_ops = &cmld_remap_vm_ops;

	return 0;
}

/* Driver's release method for /dev/cm_channel */
static int cmld_channel_release(struct inode *inode, struct file *file)
{
	struct cm_channel_priv* channelPriv = file->private_data;
	struct cm_process_priv* procPriv = channelPriv->proc;

	/*
	 * The driver must guarantee that all related resources are released.
	 * Thus all these checks below are necessary to release all remaining
	 * resources still linked to this 'client', in case of abnormal process
	 * exit.
	 * => These are error cases !
	 * In the usual case, nothing should be done except the free of
	 * the cmPriv itself
	 */

	/* We don't need to synchronize here by using the skelListLock:
	   the list is only accessed during ioctl() and we can't be here
	   if an ioctl() is on-going */
	if (list_empty(&channelPriv->skelList)) {
		/* There is no pending MPC->HOST binding
		   => we can quietly delete the channel */
		tasklet_disable(&cmld_service_tasklet);
		mutex_lock(&channel_lock);
		list_del(&channelPriv->entry);
		mutex_unlock(&channel_lock);
		tasklet_enable(&cmld_service_tasklet);

		/* Free all remaining messages if any */
		freeMessages(channelPriv);

		/* Free the per-channel descriptor */
		OSAL_Free(channelPriv);
	} else {
		/*
		 * Uh: there are still some MPC->HOST binding but we don't have
		 * the required info to unbind them.
		 * => we must keep all skel structures because possibly used in
		 * OSAL_PostDfc (incoming callback msg). We flag the channel as
		 * closed to discard any new msg that will never be read anyway
		 */
		channelPriv->state = CHANNEL_CLOSED;

		/* Already Free all remaining messages if any,
		   they will never be read anyway */
		freeMessages(channelPriv);
	}

	kref_put(&procPriv->ref, freeProcessPriv);
	file->private_data = NULL;

	return 0;
}

/* Driver's release method for /dev/cm_control */
static int cmld_control_release(struct inode *inode, struct file *file)
{
	struct cm_process_priv* procPriv = file->private_data;

	kref_put(&procPriv->ref, freeProcessPriv);
	file->private_data = NULL;

	return 0;
}

static struct file_operations cmld_control_fops = {
	.owner		=	THIS_MODULE,
	.unlocked_ioctl	=	cmld_control_ioctl,
	.mmap		=	cmld_control_mmap,
	.release	=	cmld_control_release,
};

static int cmld_control_open(struct file *file)
{
	struct cm_process_priv *procPriv = getProcessPriv();
	if (IS_ERR(procPriv))
		return PTR_ERR(procPriv);
	file->private_data = procPriv;
	file->f_op = &cmld_control_fops;
	return 0;
}

static struct file_operations cmld_channel_fops = {
	.owner		=	THIS_MODULE,
	.read		=	cmld_channel_read,
	.unlocked_ioctl	=	cmld_channel_ioctl,
	.flush		=	cmld_channel_flush,
	.release	=	cmld_channel_release,
};

static int cmld_channel_open(struct file *file)
{
	struct cm_process_priv *procPriv = getProcessPriv();
	struct cm_channel_priv *channelPriv;

	if (IS_ERR(procPriv))
		return PTR_ERR(procPriv);

	channelPriv = (struct cm_channel_priv*)OSAL_Alloc(sizeof(*channelPriv));
	if (channelPriv == NULL) {
		kref_put(&procPriv->ref, freeProcessPriv);
		return -ENOMEM;
	}

	channelPriv->proc = procPriv;
	channelPriv->state = CHANNEL_OPEN;

	/* Initialize wait_queue, lists and mutexes */
	init_waitqueue_head(&channelPriv->waitq);
	plist_head_init(&channelPriv->messageQueue);
	INIT_LIST_HEAD(&channelPriv->skelList);
	spin_lock_init(&channelPriv->bh_lock);
	mutex_init(&channelPriv->msgQueueLock);
	mutex_init(&channelPriv->skelListLock);

	tasklet_disable(&cmld_service_tasklet);
	mutex_lock(&channel_lock);
	list_add(&channelPriv->entry, &channel_list);
	mutex_unlock(&channel_lock);
	tasklet_enable(&cmld_service_tasklet);

	file->private_data = channelPriv;
	file->f_op = &cmld_channel_fops;
	return 0;
}

static u64 computeDspTime(const s64 refTime, const s64 prcmuRefTime, const u64 prcmuDspTime)
{
	s64 dspTimeus;

	/*
	 * To convert DSP time to local ARM time, we use a common PRCMU timer
	 * on both side.
	 * PRCMU time is based on PRCMU Timer 4, clocked at 32KHz
	 */
	dspTimeus = div_s64( (((s64)prcmuDspTime - prcmuRefTime) * USEC_PER_SEC),
			     32*1024);

	return (refTime + dspTimeus);
}

static ssize_t cmld_sxa_trace_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct cm_trace_priv *tracePriv = file->private_data;
	struct mpcConfig *mpc = tracePriv->mpc;
	size_t written = 0;
	struct t_nmf_trace trace;
	t_cm_trace_type traceType;
	s64 refTime, prcmuRefTime;
	struct mmdsp_trace mmdsp_tr = {
		.media = TB_MEDIA_FILE,
		.receiver_dev = TB_DEV_PC,
		.sender_dev = TB_DEV_TRACEBOX,
		.unused = TB_TRACEBOX,
		.receiver_obj = DEFAULT_RECEIVERR_OBJ,
		.sender_obj = DEFAULT_SENDER_OBJ,
		.transaction_id = 0,
		.message_id = TB_TRACE_MSG,
		.master_id = mpc->coreId+1,
		.channel_id = 0,
		.ost_version = OST_VERSION,
		.entity = ENTITY,
		.protocol_id = PROTOCOL_ID,
		.btrace_hdr_flag = 0,
		.btrace_hdr_subcategory = 0,
	};

	/*
	 * Use CLOCK_MONOTIC clock to synchronize with user logs
	 * based on the same clock
	 */
	refTime = ktime_to_us(ktime_get());
	prcmuRefTime = OSAL_GetPrcmuTimer();

	while ((count - written) >= sizeof(mmdsp_tr)) {
		u16 param_nr, handle_valid;
		u32 to_write;
		u64 dspTime;

		if (wait_event_interruptible(mpc->trace_waitq,
					     ((traceType =
					       CM_ENGINE_GetNextTrace(mpc->coreId, &trace,
								      &tracePriv->read_idx,
								      &tracePriv->last_read_rev))
					      != CM_MPC_TRACE_NONE)
					     || (file->f_flags & O_NONBLOCK)
					     || written))
			return -ERESTARTSYS;

		if (traceType == CM_MPC_TRACE_NONE)
			return written;

		dspTime = computeDspTime(refTime, prcmuRefTime, trace.timeStamp);

		if (traceType == CM_MPC_TRACE_READ_OVERRUN) {
			/* Add an overflow trace */
			mmdsp_tr.size =
				cpu_to_be16(offsetof(struct mmdsp_trace,
						     ost_version)
					    -offsetof(struct mmdsp_trace,
						      receiver_obj));
			mmdsp_tr.message_id = TB_TRACE_EXCEPTION_MSG;
			mmdsp_tr.ost_master_id = TB_EXCEPTION_LONG_OVRF_PACKET;
			mmdsp_tr.timestamp = cpu_to_be64(dspTime);
			if (copy_to_user(&buf[written], &mmdsp_tr,
					 offsetof(struct mmdsp_trace,
						  ost_version)))
				return -EFAULT;
			written += offsetof(struct mmdsp_trace, ost_version);
		}

		/* return if not enough remaining space to put the trace */
			if ((count - written) < sizeof(mmdsp_tr))
			return written;

		param_nr = (u16)trace.paramOpt;
		handle_valid = (u16)(trace.paramOpt >> 16);
		to_write = offsetof(struct mmdsp_trace, parent_handle);

			mmdsp_tr.transaction_id = trace.revision%256;
			mmdsp_tr.message_id = TB_TRACE_MSG;
			mmdsp_tr.ost_master_id = OST_MASTERID;
		mmdsp_tr.timestamp2 = mmdsp_tr.timestamp
			= cpu_to_be64(dspTime);
			mmdsp_tr.component_id = cpu_to_be32(trace.componentId);
			mmdsp_tr.trace_id = cpu_to_be32(trace.traceId);
			mmdsp_tr.btrace_hdr_category = (trace.traceId>>16)&0xFF;
			mmdsp_tr.btrace_hdr_size = BTRACE_HEADER_SIZE
				+ sizeof(trace.params[0]) * param_nr;
			if (handle_valid) {
				mmdsp_tr.parent_handle = trace.parentHandle;
			mmdsp_tr.component_handle = trace.componentHandle;
				to_write += sizeof(trace.parentHandle)
					+ sizeof(trace.componentHandle);
				mmdsp_tr.btrace_hdr_size +=
					sizeof(trace.parentHandle)
					+ sizeof(trace.componentHandle);
			}
			mmdsp_tr.size =
				cpu_to_be16(to_write
					    + (sizeof(trace.params[0])*param_nr)
					    - offsetof(struct mmdsp_trace,
						       receiver_obj));
			mmdsp_tr.length = to_write
				+ (sizeof(trace.params[0])*param_nr)
				- offsetof(struct mmdsp_trace,
					   timestamp2);
			if (copy_to_user(&buf[written], &mmdsp_tr, to_write))
				return -EFAULT;
			written += to_write;
			/* write param */
			to_write = sizeof(trace.params[0]) * param_nr;
			if (copy_to_user(&buf[written], trace.params, to_write))
				return -EFAULT;
			written += to_write;
	}
	return written;
}

/* Driver's release method for /dev/cm_sxa_trace */
static int cmld_sxa_trace_release(struct inode *inode, struct file *file)
{
	OSAL_Free(file->private_data);
	return 0;
}

static struct file_operations cmld_sxa_trace_fops = {
	.owner		=	THIS_MODULE,
	.read		=	cmld_sxa_trace_read,
	.release	=	cmld_sxa_trace_release,
};

static int cmld_sxa_trace_open(struct file *file, struct mpcConfig *mpc)
{
	struct cm_trace_priv *tracePriv;

	tracePriv = (struct cm_trace_priv*)OSAL_Alloc(sizeof(*tracePriv));
	if (tracePriv == NULL)
		return -ENOMEM;

	tracePriv->read_idx = 0;
	tracePriv->last_read_rev = 0;
	tracePriv->mpc = mpc;

	file->private_data = tracePriv;
	file->f_op = &cmld_sxa_trace_fops;

	return 0;
}

/* driver open() call: specific */
static int cmld_open(struct inode *inode, struct file *file)
{
	switch (iminor(inode)) {
	case 0:
		return cmld_control_open(file);
	case 1:
		return cmld_channel_open(file);
	case 2:
		return cmld_sxa_trace_open(file, &osalEnv.mpc[SIA]);
	case 3:
		return cmld_sxa_trace_open(file, &osalEnv.mpc[SVA]);
	default:
		return -ENOSYS;
	}
}

/** MPC Events tasklet
 *  The parameter is used to know from which interrupts we're comming
 *  and which core to pass to CM_ProcessMpcEvent():
 *  0 means HSEM => ARM_CORE_ID
 *  otherwise, it gives the index+1 of MPC within osalEnv.mpc table
 */
static void mpc_events_tasklet_handler(unsigned long core)
{
	/* This serves internal events directly. No propagation to user space.
	 * Calls OSAL_PostDfc implementation for user interface events */
	if (core == 0) {
		CM_ProcessMpcEvent(ARM_CORE_ID);
		enable_irq(IRQ_DB8500_HSEM);
	} else {
		--core;
		CM_ProcessMpcEvent(osalEnv.mpc[core].coreId);
		enable_irq(osalEnv.mpc[core].interrupt0);
	}
}

/** Hardware semaphore and MPC interrupt handler
 * 'data' param is the one given when registering the IRQ hanlder,
 * contains the source core (ARM or MPC), and follows the same logic
 * as for mpc_events_tasklet_handler()
 * This handler is used for all IRQ handling some com (ie HSEM or
 * all MPC IRQ line0)
 */
static irqreturn_t mpc_events_irq_handler(int irq, void *data)
{
	unsigned core = (unsigned)data;

	if (core != 0)
		--core;
	disable_irq_nosync(irq);
	tasklet_schedule(&osalEnv.mpc[core].tasklet);

	return IRQ_HANDLED;
}

/** MPC panic handler
 * 'idx' contains the index of the core within the osalEnv.mpc table.
 * This handler is used for all MPC IRQ line1
 */
static irqreturn_t panic_handler(int irq, void *idx)
{
	set_bit((int)idx, &service_tasklet_data);
	disable_irq_nosync(irq);
	tasklet_schedule(&cmld_service_tasklet);
	return IRQ_HANDLED;
}

/** Driver's operations
 */
static struct file_operations cmld_fops = {
	.owner		=	THIS_MODULE,
	.open		=	cmld_open,
};

/**
 * Configure a MPC, called for each MPC to configure
 *
 * \param i            index of the MPC to configure (refer to the index
 *                     of the MPC within the osalEnvironment.mpc table)
 * \param dataAllocId  allocId of the data segment, passed through each call of
 *                     this function, and initialized at the first call in case
 *                     shared data segment
 */
static int configureMpc(unsigned i, t_cfg_allocator_id *dataAllocId)
{
	int err;
	t_cm_system_address mpcSystemAddress;
	t_nmf_memory_segment codeSegment, dataSegment;
	t_cfg_allocator_id codeAllocId;
	t_cm_domain_id eeDomainId;
	t_cm_domain_memory eeDomain = INIT_DOMAIN;
	char regulator_name[14];

	getMpcSystemAddress(i, &mpcSystemAddress);
	getMpcSdramSegments(i, &codeSegment, &dataSegment);

	/* Create code segment */
	err = CM_ENGINE_AddMpcSdramSegment(&codeSegment, &codeAllocId, "Code");
	if (err != CM_OK) {
		pr_err("CM_ENGINE_AddMpcSdramSegment() error code: %d\n", err);
		return -EAGAIN;
	}

	/* Create data segment
	 * NOTE: in case of shared data segment, all MPC point to the same data segment
	 * (see in remapRegions()) and we need to create the segment only at first call.
	 * => we reuse the same allocId for the following MPCs
	 */
	if ((osalEnv.mpc[i].sdram_data.data != osalEnv.mpc[0].sdram_data.data)
	    || *dataAllocId == -1) {
		err = CM_ENGINE_AddMpcSdramSegment(&dataSegment, dataAllocId, "Data");
		if (err != CM_OK) {
			pr_err("CM_ENGINE_AddMpcSdramSegment() error code: %d\n", err);
			return -EAGAIN;
		}
	}

	/* create default domain for the given coreId
	 * this serves for instanciating EE and the LoadMap, only sdram segment is present
	 * this domain will probably overlap with other user domains
	 */
	eeDomain.coreId            = osalEnv.mpc[i].coreId;
	eeDomain.sdramCode.offset  = 0x0;
	eeDomain.sdramData.offset  = 0x0;
	eeDomain.sdramCode.size    = 0x8000;
	eeDomain.sdramData.size    = 0x40000;
	eeDomain.esramCode.size    = 0x4000;
	eeDomain.esramData.size    = 0x40000;
	err = CM_ENGINE_CreateMemoryDomain(NMF_CORE_CLIENT, &eeDomain, &eeDomainId);
	if (err != CM_OK) {
		pr_err("Create EE domain on %s failed with error code: %d\n", osalEnv.mpc[i].name, err);
		return -EAGAIN;
	}

	err = CM_ENGINE_ConfigureMediaProcessorCore(
		osalEnv.mpc[i].coreId,
		osalEnv.mpc[i].eeId,
		(cfgSemaphoreTypeHSEM ? SYSTEM_SEMAPHORES : LOCAL_SEMAPHORES),
		osalEnv.mpc[i].nbYramBanks,
		&mpcSystemAddress,
		eeDomainId,
		codeAllocId,
		*dataAllocId);

	if (err != CM_OK) {
		pr_err("CM_ConfigureMediaProcessorCore failed with error code: %d\n", err);	
		return -EAGAIN;
	}

        // Communication channel
        if (! cfgSemaphoreTypeHSEM) {
		tasklet_init(&osalEnv.mpc[i].tasklet, mpc_events_tasklet_handler, i+1);
		err = request_irq(osalEnv.mpc[i].interrupt0, mpc_events_irq_handler, IRQF_DISABLED, osalEnv.mpc[i].name, (void*)(i+1));
		if (err != 0) {
			pr_err("CM: request_irq failed to register irq0 %i for %s (%i)\n", osalEnv.mpc[i].interrupt0, osalEnv.mpc[i].name, err);
			return err;
		}
        }

        // Panic channel
	err = request_irq(osalEnv.mpc[i].interrupt1, panic_handler, IRQF_DISABLED, osalEnv.mpc[i].name, (void*)i);
	if (err != 0) {
		pr_err("CM: request_irq failed to register irq1 %i for %s (%i)\n", osalEnv.mpc[i].interrupt1, osalEnv.mpc[i].name, err);
		free_irq(osalEnv.mpc[i].interrupt0, (void*)(i+1));
		return err;
	}

	// Retrieve the regulators used for this MPCs
	sprintf(regulator_name, "%s-mmdsp", osalEnv.mpc[i].name);
	osalEnv.mpc[i].mmdsp_regulator = regulator_get(cmld_dev[0], regulator_name);
	if (IS_ERR(osalEnv.mpc[i].mmdsp_regulator)) {
		long err = PTR_ERR(osalEnv.mpc[i].mmdsp_regulator);
		pr_err("CM: Error while retrieving the regulator %s: %ld\n", regulator_name, err);
		osalEnv.mpc[i].mmdsp_regulator = NULL;
		return err;
	}
	sprintf(regulator_name, "%s-pipe", osalEnv.mpc[i].name);
	osalEnv.mpc[i].pipe_regulator = regulator_get(cmld_dev[0], regulator_name);
	if (IS_ERR(osalEnv.mpc[i].pipe_regulator)) {
		long err = PTR_ERR(osalEnv.mpc[i].pipe_regulator);
		pr_err("CM: Error while retrieving the regulator %s: %ld\n", regulator_name, err);
		osalEnv.mpc[i].pipe_regulator = NULL;
		return err;
	}
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&osalEnv.mpc[i].wakelock, WAKE_LOCK_SUSPEND, osalEnv.mpc[i].name);
#endif
	return 0;
}

/* Free all used MPC irqs and clocks.
 * max_mpc allows it to be called from init_module and free
 * only the already configured irqs.
 */
static void free_mpc_irqs(int max_mpc)
{
	int i;
	for (i=0; i<max_mpc; i++) {
		if (! cfgSemaphoreTypeHSEM)
			free_irq(osalEnv.mpc[i].interrupt0, (void*)(i+1));
		free_irq(osalEnv.mpc[i].interrupt1, (void*)i);
		if (osalEnv.mpc[i].mmdsp_regulator)
			regulator_put(osalEnv.mpc[i].mmdsp_regulator);
		if (osalEnv.mpc[i].pipe_regulator)
			regulator_put(osalEnv.mpc[i].pipe_regulator);
#ifdef CONFIG_HAS_WAKELOCK
		wake_lock_destroy(&osalEnv.mpc[i].wakelock);
#endif
	}
}

/** Module entry point
 * Allocate memory chunks. Register hardware semaphore, SIA and SVA interrupts.
 * Initialize Component Manager. Register hotplug for components download.
 *
 * \return POSIX error code
 */
static int __init cmld_init_module(void)
{
	int err;
	unsigned i=0;
	dev_t dev;
	t_cfg_allocator_id dataAllocId = -1;
	void *htim_base=NULL;

	/* Component manager initialization descriptors */
	t_nmf_hw_mapping_desc nmfHwMappingDesc;
	t_nmf_config_desc nmfConfigDesc = { cfgCommunicationLocationInSDRAM ? COMS_IN_SDRAM : COMS_IN_ESRAM };

	/* OSAL_*Resources() assumes the following, so check that it is correct */
	if (SVA != COREIDX((int)SVA_CORE_ID)) {
		pr_err("SVA and (SVA_CORE_ID-1) differs : code must be fixed !\n");
		return -EIO;
	}
	if (SIA != COREIDX((int)SIA_CORE_ID)) {
		pr_err("SIA and (SIA_CORE_ID-1) differs : code must be fixed !\n");
		return -EIO;
	}

#ifdef CM_DEBUG_ALLOC
	init_debug_alloc();
#endif

	err = -EIO;
	prcmu_base = __io_address(U8500_PRCMU_BASE);

	/* power on a clock/timer 90KHz used on SVA */
	htim_base = ioremap_nocache(U8500_CR_BASE /*0xA03C8000*/, SZ_4K);
	prcmu_tcdm_base = __io_address(U8500_PRCMU_TCDM_BASE);

	/* Activate SVA 90 KHz timer */
	if (htim_base == NULL)
		goto out;
	iowrite32((1<<26) | ioread32(htim_base), htim_base);
	iounmap(htim_base);

	err = init_config();
	if (err)
		goto out;

	/* Remap all needed regions and store in osalEnv base addresses */
	err = remapRegions();
	if (err != 0)
		goto out;
	
	/* Initialize linux devices */
	err = class_register(&cmld_class);
	if (err) {
		pr_err("CM: class_register failed (%d)\n", err);
                goto out;
	}

	/* Register char device */
	err = alloc_chrdev_region(&dev, 0, ARRAY_SIZE(cmld_devname), "cm");
	if (err) {
		pr_err("CM: alloc_chrdev_region failed (%d)\n", err);
		goto out_destroy_class;
	}
	cmld_major =  MAJOR(dev);

	cdev_init(&cmld_cdev, &cmld_fops);
	cmld_cdev.owner = THIS_MODULE;
	err = cdev_add (&cmld_cdev, dev, ARRAY_SIZE(cmld_devname));
	if (err) {
		pr_err("CM: cdev_add failed (%d)\n", err);
		goto out_destroy_chrdev;
	}

	for (i=0; i<ARRAY_SIZE(cmld_devname); i++) {
		cmld_dev[i] = device_create(&cmld_class, NULL, MKDEV(cmld_major, i), NULL,
					    "%s", cmld_devname[i]);
		if (IS_ERR(cmld_dev[i])) {
			err = PTR_ERR(cmld_dev[i]);
			pr_err("CM: device_create failed (%d)\n", err);
			goto out_destroy_device;
		}
	}

	osalEnv.esram_regulator[ESRAM_12] = regulator_get(cmld_dev[0], "esram12");
	if (IS_ERR(osalEnv.esram_regulator[ESRAM_12])) {
		err = PTR_ERR(osalEnv.esram_regulator[ESRAM_12]);
		pr_err("CM: Error while retrieving the regulator for esram12: %d\n", err);
		osalEnv.esram_regulator[ESRAM_12] = NULL;
		goto out_destroy_device;
	}
	osalEnv.esram_regulator[ESRAM_34] = regulator_get(cmld_dev[0], "esram34");
	if (IS_ERR(osalEnv.esram_regulator[ESRAM_34])) {
		err = PTR_ERR(osalEnv.esram_regulator[ESRAM_34]);
		pr_err("CM: Error while retrieving the regulator for esram34: %d\n", err);
		osalEnv.esram_regulator[ESRAM_34] = NULL;
		goto out_destroy_device;
	}

	/* Fill in the descriptors needed by CM_ENGINE_Init() */
	getNmfHwMappingDesc(&nmfHwMappingDesc);

	/* Initialize Component Manager */
	err = CM_ENGINE_Init(&nmfHwMappingDesc, &nmfConfigDesc);
	if (err != CM_OK) {
		pr_err("CM: CM_Init failed with error code: %d\n", err);
		err = -EAGAIN;
		goto out_destroy_device;
	} else {
		pr_info("Initialize NMF %d.%d.%d Component Manager......\n",
			VERSION_MAJOR(NMF_VERSION),
			VERSION_MINOR(NMF_VERSION),
			VERSION_PATCH(NMF_VERSION));
		pr_info("[ CM Linux Driver %d.%d.%d ]\n",
			VERSION_MAJOR(NMF_VERSION),
			VERSION_MINOR(NMF_VERSION),
			CMDRIVER_PATCH_VERSION);
	}

	cm_debug_init();
	if (osal_debug_ops.domain_create) {
		osal_debug_ops.domain_create(DEFAULT_SVA_DOMAIN);
		osal_debug_ops.domain_create(DEFAULT_SIA_DOMAIN);
	}

	/* Configure MPC Cores */
	for (i=0; i<NB_MPC; i++) {
		err = configureMpc(i, &dataAllocId);
		if (err)
			goto out_all;
	}
	/* End of Component Manager initialization phase */


        if (cfgSemaphoreTypeHSEM) {
		/* We use tasklet of mpc[0]. See comments above osalEnvironnent struct */
		tasklet_init(&osalEnv.mpc[0].tasklet, mpc_events_tasklet_handler, 0);
		err = request_irq(IRQ_DB8500_HSEM, mpc_events_irq_handler, IRQF_DISABLED,
				  "hwsem", 0);
		if (err) {
			pr_err("CM: request_irq failed to register hwsem irq %i (%i)\n", 
			       IRQ_DB8500_HSEM, err);
			goto out_all;
		}
	}

	err = cmdma_init();
	if (err == 0)
		return 0;

out_all:
	cm_debug_exit();
	free_mpc_irqs(i);
	CM_ENGINE_Destroy();
	i=ARRAY_SIZE(cmld_devname);
out_destroy_device:
	if (osalEnv.esram_regulator[ESRAM_12])
		regulator_put(osalEnv.esram_regulator[ESRAM_12]);
	if (osalEnv.esram_regulator[ESRAM_34])
		regulator_put(osalEnv.esram_regulator[ESRAM_34]);
	while (i--)
		device_destroy(&cmld_class, MKDEV(cmld_major, i));
	cdev_del(&cmld_cdev);
out_destroy_chrdev:
	unregister_chrdev_region(dev, ARRAY_SIZE(cmld_devname));
out_destroy_class:
	class_unregister(&cmld_class);
out:
	unmapRegions();
#ifdef 	CM_DEBUG_ALLOC
	cleanup_debug_alloc();
#endif
	return err;
}

/** Module exit point
 * Unregister the driver. This will lead to a 'remove' call.
 */
static void __exit cmld_cleanup_module(void)
{
	unsigned i;

	if (!list_empty(&channel_list))
		pr_err("CM Driver ending with non empty channel list\n");
	if (!list_empty(&process_list))
		pr_err("CM Driver ending with non empty process list\n");
	
        if (cfgSemaphoreTypeHSEM)
		free_irq(IRQ_DB8500_HSEM, NULL);
	free_mpc_irqs(NB_MPC);
	tasklet_kill(&cmld_service_tasklet);

	if (osalEnv.esram_regulator[ESRAM_12])
		regulator_put(osalEnv.esram_regulator[ESRAM_12]);
	if (osalEnv.esram_regulator[ESRAM_34])
		regulator_put(osalEnv.esram_regulator[ESRAM_34]);
	for (i=0; i<ARRAY_SIZE(cmld_devname); i++)
		device_destroy(&cmld_class, MKDEV(cmld_major, i));
	cdev_del(&cmld_cdev);
	unregister_chrdev_region(MKDEV(cmld_major, 0), ARRAY_SIZE(cmld_devname));
	class_unregister(&cmld_class);

	CM_ENGINE_Destroy();

	cmdma_destroy();
	unmapRegions();
#ifdef 	CM_DEBUG_ALLOC
	cleanup_debug_alloc();
#endif
	cm_debug_exit();
}
module_init(cmld_init_module);
module_exit(cmld_cleanup_module);

MODULE_AUTHOR("David Siorpaes");
MODULE_AUTHOR("Wolfgang Betz");
MODULE_AUTHOR("Pierre Peiffer");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Nomadik Multiprocessing Framework Component Manager Linux driver");
