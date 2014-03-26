/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef CMLD_H
#define CMLD_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <inc/nmf-limits.h>
#include "cmioctl.h"

/** Channel state used within the per-channel private structure 'cm_channel_priv'
 */
enum channel_state {
	CHANNEL_CLOSED = 0,	/**< Channel already closed */
	CHANNEL_OPEN,		/**< Channel still open */
};

/*
 * Component Manager per-process private structure
 * It is created the first time a process opens /dev/cm_channel
 * or /dev/cm_control
 */
struct cm_process_priv
{
	struct kref ref;                  /**< ref count */
	struct list_head entry;           /**< This entry */
	pid_t pid;                        /**< pid of process owner */
	struct mutex mutex;               /**< per process mutex: protect memAreaDescList */
	struct list_head memAreaDescList; /**< memAreaDesc_t list */
	struct mutex host2mpcLock;        /**< used to synchronize each AllocEvent + PushEvent */
#ifdef CONFIG_DEBUG_FS
	struct dentry *dir;               /**< debugfs dir entry under nmf-cm/proc */
	struct dentry *comp_dir;          /**< debugfs dir entry under nmf-cm/proc/..%components */
	struct dentry *domain_dir;        /**< debugfs dir entry under nmf-cm/proc/..%domains */
#endif
};

/*
 * Component Manager per-channel private structure
 * It is created when a user opens /dev/cm_channel
 */
struct cm_channel_priv
{
	enum channel_state state;         /**< Channel state */
	struct list_head entry;           /**< This entry */
	struct cm_process_priv *proc;     /**< Pointer to the owner process structure */
	struct list_head skelList;        /**< t_skelwrapper list */
	struct mutex skelListLock;        /**< skelList mutex */
	struct plist_head messageQueue;   /**< queueelem_t list */
	struct mutex msgQueueLock;        /**< lock used to synchronize MPC to HOST bindings
					      in case of multiple read (see cmld_read comments) */
	spinlock_t bh_lock;               /**< lock used to synchronize add/removal of element in/from
					      the message queue in both user context and tasklet */
	wait_queue_head_t waitq;          /**< wait queue used to block read() call */
};

/*
 * Component Manager per trace-reader private structure
 * It is created when a user opens /dev/cm_sxa_trace
 */
struct cm_trace_priv {
	int read_idx;          /* current read index */
	int last_read_rev;     /* revision of last trace read */
	struct mpcConfig *mpc; /* pointer to mpc struct being read */
};

/** Memory area descriptor.
 */
struct memAreaDesc_t {
	struct list_head list;            /**< Doubly linked list descriptor */
	atomic_t count;                   /**< Reference counter */
	pid_t tid;                        /**< tid of the process this area is reserved for */
	t_cm_memory_handle handle;        /**< Component Manager handle */
	unsigned int size;                /**< Size */
	unsigned int physAddr;            /**< Physical address */
	unsigned int kernelLogicalAddr;   /**< Logical address as seen by kernel */
	unsigned int userLogicalAddr;     /**< Logical address as seen by user */
	unsigned int mpcPhysAddr;         /**< Physicaladdress as seen by MPC */
	struct cm_process_priv* procPriv; /**< link to per process private structure */
};

extern struct list_head channel_list; /**< List of all allocated channel structures */
extern struct list_head process_list; /**< List of all allocated process private structure */
#ifdef CONFIG_DEBUG_FS
extern bool cmld_user_has_debugfs; /**< Whether user side has proper support of debugfs to take a dump */
extern pid_t cmld_dump_ongoing; /**< If a dump is on-going, store pid of process doing the dump */
#endif

/* Structure used to embed DSP traces */
#define TB_MEDIA_FILE 0x1C
#define TB_DEV_PC 0x10
#define TB_DEV_TRACEBOX 0x4C
#define TB_TRACEBOX 0x7C
#define DEFAULT_RECEIVERR_OBJ 0x0
#define DEFAULT_SENDER_OBJ 0x0A
#define TB_TRACE_MSG 0x94
#define TB_TRACE_EXCEPTION_MSG 0x95
#define TB_EXCEPTION_LONG_OVRF_PACKET 0x07
#define OST_MASTERID 0x08
#define OST_VERSION 0x05
#define ENTITY 0xAA
#define PROTOCOL_ID 0x03
#define BTRACE_HEADER_SIZE 4

struct __attribute__ ((__packed__)) mmdsp_trace {
	u8 media;
	u8 receiver_dev;
	u8 sender_dev;
	u8 unused;
	u16 size;
	u8 receiver_obj;
	u8 sender_obj;
	u8 transaction_id;
	u8 message_id;
	u8 master_id;
	u8 channel_id;
	u64 timestamp;
	u8 ost_master_id;
	u8 ost_version;
	u8 entity;
	u8 protocol_id;
	u8 length;
	u64 timestamp2;
	u32 component_id;
	u32 trace_id;
	u8 btrace_hdr_size;
	u8 btrace_hdr_flag;
	u8 btrace_hdr_category;
	u8 btrace_hdr_subcategory;
	u32 parent_handle;
	u32 component_handle;
	u32 params[4];
};

/** Lock/unlock per process mutex
 *
 * \note Must be taken before tasklet_disable (if necessary)!
 */
#define lock_process_uninterruptible(proc) (mutex_lock(&proc->mutex))
#define lock_process(proc) (mutex_lock_killable(&proc->mutex))
#define unlock_process(proc) (mutex_unlock(&proc->mutex))



int cmld_InstantiateComponent(struct cm_process_priv *, CM_InstantiateComponent_t __user *);
int cmld_BindComponentFromCMCore(struct cm_process_priv *,
				 CM_BindComponentFromCMCore_t __user *);
int cmld_UnbindComponentFromCMCore(CM_UnbindComponentFromCMCore_t __user *);
int cmld_BindComponentToCMCore(struct cm_channel_priv *, CM_BindComponentToCMCore_t __user *);
int cmld_UnbindComponentToCMCore(struct cm_process_priv*, CM_UnbindComponentToCMCore_t __user *);
int cmld_BindComponentAsynchronous(struct cm_process_priv*, CM_BindComponentAsynchronous_t __user *);
int cmld_UnbindComponentAsynchronous(struct cm_process_priv*, CM_UnbindComponentAsynchronous_t __user *);
int cmld_BindComponent(struct cm_process_priv*, CM_BindComponent_t __user *);
int cmld_UnbindComponent(struct cm_process_priv*, CM_UnbindComponent_t __user *);
int cmld_BindComponentToVoid(struct cm_process_priv*, CM_BindComponentToVoid_t  __user *);
int cmld_DestroyComponent(struct cm_process_priv*, CM_DestroyComponent_t __user *);
int cmld_CreateMemoryDomain(struct cm_process_priv*, CM_CreateMemoryDomain_t __user *);
int cmld_CreateMemoryDomainScratch(struct cm_process_priv*, CM_CreateMemoryDomainScratch_t __user *);
int cmld_DestroyMemoryDomain(CM_DestroyMemoryDomain_t __user *);
int cmld_GetDomainCoreId(CM_GetDomainCoreId_t __user *);
int cmld_AllocMpcMemory(struct cm_process_priv *, CM_AllocMpcMemory_t __user *);
int cmld_FreeMpcMemory(struct cm_process_priv *, CM_FreeMpcMemory_t __user *);
int cmld_GetMpcMemoryStatus(CM_GetMpcMemoryStatus_t __user *);
int cmld_StartComponent(struct cm_process_priv *, CM_StartComponent_t __user *);
int cmld_StopComponent(struct cm_process_priv *, CM_StopComponent_t __user *);
int cmld_GetMpcLoadCounter(CM_GetMpcLoadCounter_t __user *);
int cmld_GetComponentDescription(struct cm_process_priv *, CM_GetComponentDescription_t __user *);
int cmld_GetComponentListHeader(struct cm_process_priv *, CM_GetComponentListHeader_t __user *);
int cmld_GetComponentListNext(struct cm_process_priv *, CM_GetComponentListNext_t __user *);
int cmld_GetComponentRequiredInterfaceNumber(struct cm_process_priv *,
					     CM_GetComponentRequiredInterfaceNumber_t __user *);
int cmld_GetComponentRequiredInterface(struct cm_process_priv *,
				       CM_GetComponentRequiredInterface_t __user *);
int cmld_GetComponentRequiredInterfaceBinding(struct cm_process_priv *,
					      CM_GetComponentRequiredInterfaceBinding_t __user *);
int cmld_GetComponentProvidedInterfaceNumber(struct cm_process_priv *,
					     CM_GetComponentProvidedInterfaceNumber_t __user *);
int cmld_GetComponentProvidedInterface(struct cm_process_priv *,
				       CM_GetComponentProvidedInterface_t __user *);
int cmld_GetComponentPropertyNumber(struct cm_process_priv *,
				    CM_GetComponentPropertyNumber_t __user *);
int cmld_GetComponentPropertyName(struct cm_process_priv *, CM_GetComponentPropertyName_t __user *);
int cmld_GetComponentPropertyValue(struct cm_process_priv *, CM_GetComponentPropertyValue_t __user *);
int cmld_ReadComponentAttribute(struct cm_process_priv *, CM_ReadComponentAttribute_t __user *);
int cmld_WriteComponentAttribute(struct cm_process_priv *, CM_WriteComponentAttribute_t __user *);
int cmld_GetExecutiveEngineHandle(struct cm_process_priv *, CM_GetExecutiveEngineHandle_t __user *);
int cmld_SetMode(CM_SetMode_t __user *);
int cmld_GetRequiredComponentFiles(struct cm_process_priv *cmPriv,
				   CM_GetRequiredComponentFiles_t __user *);
int cmld_Migrate(CM_Migrate_t __user *);
int cmld_Unmigrate(CM_Unmigrate_t __user *);
int cmld_SetupRelinkArea(struct cm_process_priv *, CM_SetupRelinkArea_t __user *);
int cmld_PushComponent(CM_PushComponent_t __user *);
int cmld_ReleaseComponent(CM_ReleaseComponent_t __user *);
int cmld_PrivGetMPCMemoryDesc(struct cm_process_priv *, CM_PrivGetMPCMemoryDesc_t __user *);
int cmld_PrivReserveMemory(struct cm_process_priv *, unsigned int);
#endif
