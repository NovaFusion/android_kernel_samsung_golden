/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

/** \file osal-kernel.c
 * 
 * Implements NMF OSAL for Linux kernel-space environment
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <trace/stm.h>

#include <cm/engine/configuration/inc/configuration_status.h>

#include "cmioctl.h"
#include "osal-kernel.h"
#include "cm_service.h"
#include "cmld.h"
#include "cm_debug.h"
#include "cm_dma.h"

__iomem void *prcmu_base = NULL;
__iomem void *prcmu_tcdm_base = NULL;

/* DSP Load Monitoring */
#define FULL_OPP 100
#define HALF_OPP 50
static unsigned long running_dsp = 0;
static unsigned int dspLoadMonitorPeriod = 1000;
module_param(dspLoadMonitorPeriod, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(dspLoadMonitorPeriod, "Period of the DSP-Load monitoring in ms");
static unsigned int dspLoadHighThreshold = 85;
module_param(dspLoadHighThreshold, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(dspLoadHighThreshold, "Threshold above which 100 APE OPP is requested");
static unsigned int dspLoadLowThreshold = 38;
module_param(dspLoadLowThreshold, uint, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(dspLoadLowThreshold, "Threshold below which 100 APE OPP request is removed");
static bool cm_use_ftrace;
module_param(cm_use_ftrace, bool, S_IWUSR|S_IRUGO);
MODULE_PARM_DESC(cm_use_ftrace, "Whether all CM debug traces goes through ftrace or normal kernel output");

/** \defgroup ENVIRONMENT_INITIALIZATION Environment initialization
 * Includes functions that initialize the Linux OSAL itself plus functions that
 * are responsible to factor configuration objects needed to initialize Component Manager library
 */

/** \defgroup OSAL_IMPLEMENTATION OSAL implementation
 *	 Linux-specific implementation of the Component Manager OSAL interface.
 */


/** \ingroup ENVIRONMENT_INITIALIZATION
 * Remaps IO, SDRAM and ESRAM regions
 *
 * \osalEnvironment NMF-Osal descriptor
 * \return POSIX error code 
 */
int remapRegions(void)
{
	unsigned i;

	/* Remap DSP base areas */
	for (i=0; i<NB_MPC; i++) {
		osalEnv.mpc[i].base.data = ioremap_nocache((int)osalEnv.mpc[i].base_phys, ONE_MB);
		if(osalEnv.mpc[i].base.data == NULL){
			pr_err("%s: could not remap base address for %s\n", __func__, osalEnv.mpc[i].name);
			return -ENOMEM;
		}
	}

	/* Remap hardware semaphores */
	osalEnv.hwsem_base = ioremap_nocache(U8500_HSEM_BASE, (4*ONE_KB));
	if(osalEnv.hwsem_base == NULL){
		pr_err("%s: could not remap HWSEM Base\n", __func__);
		return -ENOMEM;
	}

	/* Remap _all_ ESRAM banks */
	osalEnv.esram_base = ioremap_nocache(osalEnv.esram_base_phys, cfgESRAMSize*ONE_KB);
	if(osalEnv.esram_base == NULL){
		pr_err("%s: could not remap ESRAM Base\n", __func__);
		return -ENOMEM;
	}

	/* Allocate code and data sections for MPC (SVA, SIA) */
        for (i=0; i<NB_MPC; i++) {
		/* Allocate MPC SDRAM code area */
		struct hwmem_mem_chunk mem_chunk;
		size_t mem_chunk_length;
		osalEnv.mpc[i].hwmem_code = hwmem_alloc(osalEnv.mpc[i].sdram_code.size,
						       //HWMEM_ALLOC_HINT_CACHE_WB,
						       HWMEM_ALLOC_HINT_WRITE_COMBINE | HWMEM_ALLOC_HINT_UNCACHED,
						       HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE,
						       HWMEM_MEM_CONTIGUOUS_SYS);
		if (IS_ERR(osalEnv.mpc[i].hwmem_code)) {
			int err = PTR_ERR(osalEnv.mpc[i].hwmem_code);
			osalEnv.mpc[i].hwmem_code = NULL;
			pr_err("%s: could not allocate SDRAM Code for %s\n",
			       __func__, osalEnv.mpc[i].name);
			return err;
		}
		osalEnv.mpc[i].sdram_code.data = hwmem_kmap(osalEnv.mpc[i].hwmem_code);
		if (IS_ERR(osalEnv.mpc[i].sdram_code.data)) {
			int err = PTR_ERR(osalEnv.mpc[i].sdram_code.data);
			osalEnv.mpc[i].sdram_code.data = NULL;
			pr_err("%s: could not map SDRAM Code for %s\n", __func__, osalEnv.mpc[i].name);
			return err;
		}
		mem_chunk_length = 1;
		(void)hwmem_pin(osalEnv.mpc[i].hwmem_code, &mem_chunk, &mem_chunk_length);
		osalEnv.mpc[i].sdram_code_phys = mem_chunk.paddr;
		/* Allocate MPC SDRAM data area by taking care wether the data are shared or not */
		if (osalEnv.mpc[i].sdram_data.size == 0) {
			/* size of 0 means shared data segment, reuse the same param as for first MPC */
			osalEnv.mpc[i].sdram_data_phys = osalEnv.mpc[0].sdram_data_phys;
			osalEnv.mpc[i].sdram_data.data = osalEnv.mpc[0].sdram_data.data;
			osalEnv.mpc[i].sdram_data.size = osalEnv.mpc[0].sdram_data.size;
		} else {
			/* If we do not share the data segment or if this is the first MPC */
			osalEnv.mpc[i].hwmem_data = hwmem_alloc(osalEnv.mpc[i].sdram_data.size,
							       HWMEM_ALLOC_HINT_WRITE_COMBINE | HWMEM_ALLOC_HINT_UNCACHED,
							       HWMEM_ACCESS_READ | HWMEM_ACCESS_WRITE,
							       HWMEM_MEM_CONTIGUOUS_SYS);
			if (IS_ERR(osalEnv.mpc[i].hwmem_data)) {
				int err = PTR_ERR(osalEnv.mpc[i].hwmem_data);
				osalEnv.mpc[i].hwmem_data = NULL;
				pr_err("%s: could not allocate SDRAM Data for %s\n",
				       __func__, osalEnv.mpc[i].name);
				return err;
			}
			mem_chunk_length = 1;
			(void)hwmem_pin(osalEnv.mpc[i].hwmem_data,
					&mem_chunk, &mem_chunk_length);
			osalEnv.mpc[i].sdram_data_phys = mem_chunk.paddr;
			osalEnv.mpc[i].sdram_data.data = hwmem_kmap(osalEnv.mpc[i].hwmem_data);
			if (IS_ERR(osalEnv.mpc[i].sdram_data.data)) {
				int err = PTR_ERR(osalEnv.mpc[i].sdram_data.data);
				osalEnv.mpc[i].sdram_data.data = NULL;
				pr_err("%s: could not map SDRAM Data for %s\n",
				       __func__, osalEnv.mpc[i].name);
				return err;
			}
		}
	}

	return 0;
}

/** \ingroup ENVIRONMENT_INITIALIZATION
 * Unmaps IO, SDRAM and ESRAM regions
 *
 * \return POSIX error code 
 */
void unmapRegions(void)
{
	unsigned i;

	/* Release SVA, SIA, Hardware sempahores and embedded SRAM mappings */
	for (i=0; i<NB_MPC; i++) {
		if(osalEnv.mpc[i].base.data != NULL)
			iounmap(osalEnv.mpc[i].base.data);
	}

	if(osalEnv.hwsem_base != NULL)
		iounmap(osalEnv.hwsem_base);
	
	if(osalEnv.esram_base != NULL)
		iounmap(osalEnv.esram_base);

	/*
	 * Free SVA and SIA code and data sections or release their mappings
	 * according on how memory allocations has been achieved
	 */
	for (i=0; i<NB_MPC; i++) {
		if (osalEnv.mpc[i].sdram_code.data != NULL) {
			hwmem_unpin(osalEnv.mpc[i].hwmem_code);
			hwmem_kunmap(osalEnv.mpc[i].hwmem_code);
			if (osalEnv.mpc[i].hwmem_code != NULL)
				hwmem_release(osalEnv.mpc[i].hwmem_code);
		}

		/* If data segment is shared, we must free only the first data segment */
		if (((i == 0) || (osalEnv.mpc[i].sdram_data.data != osalEnv.mpc[0].sdram_data.data))
		    && (osalEnv.mpc[i].sdram_data.data != NULL)) {
			hwmem_unpin(osalEnv.mpc[i].hwmem_data);
			hwmem_kunmap(osalEnv.mpc[i].hwmem_data);
			if (osalEnv.mpc[i].hwmem_data != NULL)
				hwmem_release(osalEnv.mpc[i].hwmem_data);
		}
	}
}


/** \ingroup ENVIRONMENT_INITIALIZATION
 *	Fills a t_nmf_hw_mapping_desc object
 *
 * \param nmfHwMappingDesc Pointer to a t_nmf_hw_mapping_desc object
 * \return POSIX error code
 */
int getNmfHwMappingDesc(t_nmf_hw_mapping_desc* nmfHwMappingDesc)
{

	if (nmfHwMappingDesc == NULL)
		return -ENXIO;

	nmfHwMappingDesc->esramDesc.systemAddr.physical = (t_cm_physical_address)osalEnv.esram_base_phys;
	nmfHwMappingDesc->esramDesc.systemAddr.logical = (t_cm_logical_address)osalEnv.esram_base;
	nmfHwMappingDesc->esramDesc.size = cfgESRAMSize*ONE_KB;

	nmfHwMappingDesc->hwSemaphoresMappingBaseAddr.physical = U8500_HSEM_BASE;
	nmfHwMappingDesc->hwSemaphoresMappingBaseAddr.logical = (t_cm_logical_address)osalEnv.hwsem_base;

	return 0;
}

/**  \ingroup ENVIRONMENT_INITIALIZATION
 * Fills a t_cm_system_address object
 *
 * \param mpcSystemAddress Pointer to a t_cm_system_address object
 * \return POSIX error code
 */
void getMpcSystemAddress(unsigned i, t_cm_system_address* mpcSystemAddress)
{
	mpcSystemAddress->physical = (t_cm_physical_address)osalEnv.mpc[i].base_phys;
	mpcSystemAddress->logical  = (t_cm_logical_address)osalEnv.mpc[i].base.data;
}


/** \ingroup ENVIRONMENT_INITIALIZATION
 *	Fills t_nmf_memory_segment objects for MPC code and data segments
 *
 * \param i           Index of the MPC to initialize
 * \param codeSegment Pointer to a t_nmf_memory_segment (code segment)
 * \param dataSegment Pointer to a t_nmf_memory_segment (data segment)
 * \return Always 0
 */
void getMpcSdramSegments(unsigned i, t_nmf_memory_segment* codeSegment, t_nmf_memory_segment* dataSegment)
{
	codeSegment->systemAddr.logical = (t_cm_logical_address)osalEnv.mpc[i].sdram_code.data;
	codeSegment->systemAddr.physical = osalEnv.mpc[i].sdram_code_phys;
	codeSegment->size = osalEnv.mpc[i].sdram_code.size;

	dataSegment->systemAddr.logical = (t_cm_logical_address)osalEnv.mpc[i].sdram_data.data;
	dataSegment->systemAddr.physical = osalEnv.mpc[i].sdram_data_phys;
	dataSegment->size = osalEnv.mpc[i].sdram_data.size;
}

#ifdef CM_DEBUG_ALLOC
#include <linux/kallsyms.h>
struct cm_alloc cm_alloc;

/**
 * These routines initializes the structures used to trace all alloc/free.
 * These are used in debug mode to track all memory leak about the allocations
 * done through the OSAL.
 */
void init_debug_alloc(void)
{
	INIT_LIST_HEAD(&cm_alloc.chain);
	spin_lock_init(&cm_alloc.lock);
}

void cleanup_debug_alloc(void)
{
	struct cm_alloc_elem *entry, *next;
	char buffer[128];

	list_for_each_entry_safe(entry, next, &cm_alloc.chain, elem) {
		sprint_symbol(buffer, (int)entry->caller);
		pr_err("/!\\ ALLOC(size=%d) not freed from: 0x%p (%s)\n",
		       entry->size, entry->caller, buffer);
		list_del(&entry->elem);
		if ((void*)entry >= (void*)VMALLOC_START
		    && (void*)entry < (void*)VMALLOC_END)
			vfree(entry);
		else
			kfree(entry);
	}
}

void dump_debug_alloc(void)
{
	struct cm_alloc_elem *entry, *next;
	char buffer[128];

	pr_err("Current allocated memory:\n");
	list_for_each_entry_safe(entry, next, &cm_alloc.chain, elem) {
		sprint_symbol(buffer, (int)entry->caller);
		pr_err("=> Alloc of size=%d from: 0x%p (%s)\n",
		       entry->size, entry->caller, buffer);
	}
}
#endif


/** \ingroup OSAL_IMPLEMENTATION
 * Called by CM_ProcessMpcEvent in interrupt/tasklet context. Schedules the DFC. 
 * Enqueues the new event in the process' message queue.
 *
 * \note This is _not_ called in response to internal events such as in
 * response to a CM_InstantiateComponent. It is called when user-defined
 * functions need to be called in skeletons. This behavior is different
 * from 0.8.1 version.
 */
void OSAL_PostDfc(t_nmf_mpc2host_handle upLayerTHIS, t_uint32 methodIndex, t_event_params_handle ptr, t_uint32 size)
{
	/* skelwrapper has been created in CM_SYSCALL_BindComponentToCMCore and conveys per-process private data */
	t_skelwrapper* skelwrapper = (t_skelwrapper*)upLayerTHIS;
	struct osal_msg* message;

	/* If the clannel has been closed, no more reader exists
	   => discard the message */
	if (skelwrapper->channelPriv->state == CHANNEL_CLOSED) {
		pr_warning("%s: message discarded (channel closed)\n",
			   __func__ );
		return;
	}

	/* Create a new message */
	message = kmalloc(sizeof(*message), GFP_ATOMIC);
	if (!message) {
		pr_err("%s: message discarded (alloc failed)\n", __func__ );
		return;
	}

	/* Stuff it */
	plist_node_init(&message->msg_entry, 0);
	message->msg_type = MSG_INTERFACE;
	message->d.itf.skelwrap = skelwrapper;
	message->d.itf.methodIdx = methodIndex;
	message->d.itf.anyPtr = ptr;
	message->d.itf.ptrSize = size;

	/* Enqueue it */
	/* Should be protected with the cmPriv->msgQueueLock held
	   But we know by design that we are safe here. (Alone here in
	   tasklet (soft-interrupt) context.
	   When accessed in process context, soft-irq are disable)
	*/
	spin_lock_bh(&skelwrapper->channelPriv->bh_lock);
	plist_add(&message->msg_entry, &skelwrapper->channelPriv->messageQueue);
	spin_unlock_bh(&skelwrapper->channelPriv->bh_lock);
	
	/* Wake up process' wait queue */
	wake_up(&skelwrapper->channelPriv->waitq);
}


#define MAX_LOCKS 8 // max number of locks/semaphores creatable
static unsigned long semused = 0; // bit field for used semaphores
static unsigned long lockused = 0; // bit field for used mutexes
static struct mutex cmld_locks[MAX_LOCKS];

/** \ingroup OSAL_IMPLEMENTATION
 */
t_nmf_osal_sync_handle OSAL_CreateLock(void)
{
	int i;

	for (i=0; i<MAX_LOCKS; i++)
		if (!test_and_set_bit(i, &lockused)) {
			struct mutex* mutex = &cmld_locks[i];		
			mutex_init(mutex);
			return (t_nmf_osal_sync_handle)mutex;
		}

	return (t_nmf_osal_sync_handle)NULL;
}


/** \ingroup OSAL_IMPLEMENTATION
 */
void OSAL_Lock(t_nmf_osal_sync_handle handle)
{
		// unfortunately there is no return value to this function
		// so we cannot use 'mutex_lock_killable()'
		mutex_lock((struct mutex*)handle);
}


/** \ingroup OSAL_IMPLEMENTATION
 */
void OSAL_Unlock(t_nmf_osal_sync_handle handle)
{
		mutex_unlock((struct mutex*)handle);
}


/** \ingroup OSAL_IMPLEMENTATION
 */
void OSAL_DestroyLock(t_nmf_osal_sync_handle handle)
{
	int i;

	// clear the bit in the bits field about used locks
	i = ((struct mutex*)handle - cmld_locks);

	clear_bit(i, &lockused);
}

static struct semaphore cmld_semaphores[MAX_LOCKS];
/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * Goal: Use by CM to allow to synchronize with code running on mpc side.
 *
 * \param[in] value : Initial value of semaphore.
 * 
 * \return handle of the Semaphore created 
 * 
 * Called by:
 *  - any CM API call
 *
 * \ingroup OSAL
 */ 
t_nmf_osal_sem_handle OSAL_CreateSemaphore(t_uint32 value)
{
	int i;

	for (i=0; i<MAX_LOCKS; i++)
		if (!test_and_set_bit(i, &semused)) {
			struct semaphore* sem = &cmld_semaphores[i];		
			sema_init(sem, value);
			return (t_nmf_osal_sem_handle)sem;
		}

	return (t_nmf_osal_sem_handle)NULL;
}

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * Goal: Use by CM to allow to synchronize with code running on mpc side. This function can be called under
 *       Irq context by CM.
 *
 * param[in] : handle of the Semaphore for which we increase value and so potentially wake up thread.
 * 
 * param[in] : aCtx is a hint to indicate to os that we are in a none normal context (e.g under interruption).
 *  
 * Called by:
 *  - any CM API call
 *
 * \ingroup OSAL
 */ 
void OSAL_SemaphorePost(t_nmf_osal_sem_handle handle, t_uint8 aCtx)
{
		up((struct semaphore*)handle);
}

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * Goal: Use by CM to allow to synchronize with code running on mpc side.
 *
 * param[in] : handle of the Semaphore for which we decrease value and so potentially block current thread.
 *  
 * param[in] : maximun time in ms after which the block thread is wake up. In this case function return SYNC_ERROR_TIMEOUT value.
 * 
 * \return error number: SYNC_ERROR_TIMEOUT in case semaphore is not release withing timeOutInMs.
 * 
 * Called by:
 *  - any CM API call
 *
 * \ingroup OSAL
 */ 
t_nmf_osal_sync_error OSAL_SemaphoreWaitTimed(t_nmf_osal_sem_handle handle,
					      t_uint32 timeOutInMs)
{
	if (down_timeout((struct semaphore*)handle, msecs_to_jiffies(timeOutInMs)))
		return SYNC_ERROR_TIMEOUT;
	else
		return SYNC_OK;
}

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * Goal: Use by CM to allow to synchronize with code running on mpc side.
 *
 * param[in] : handle of the Semaphore to be destroyed
 * 
 * Called by:
 *  - any CM API call
 *
 * \ingroup OSAL
 */
void OSAL_DestroySemaphore(t_nmf_osal_sem_handle handle)
{
	int i;

	// clear the bit in the bits field about used locks
	i = ((struct semaphore*)handle - cmld_semaphores);

	clear_bit(i, &semused);
}

/** \ingroup OSAL_IMPLEMENTATION
 * OSAL alloc implementation
 *
 * In both OSAL_Alloc() and OSAL_Alloc_Zero() function, the strategy is to use
 * kmalloc() as it is the more efficient and most common way to allocate memory.
 * For big allocation, kmalloc may fail because memory is very fragmented
 * (kmalloc() allocates contiguous memory). In that case, we fall to vmalloc()
 * instead.
 * In OSAL_Free(), we rely on the virtual address to know which of kfree() or
 * vfree() to use (vmalloc() use its own range of virtual addresses)
 */
void* OSAL_Alloc(t_cm_size size)
{
#ifdef CM_DEBUG_ALLOC
	struct cm_alloc_elem *entry;

	if (size == 0)
		return NULL;

	entry = kmalloc(size + sizeof(*entry), GFP_KERNEL | __GFP_NOWARN);

	if (entry == NULL) {
		entry = vmalloc(size + sizeof(*entry));

		if (entry == NULL) {
			pr_alert("%s: kmalloc(%d) and vmalloc(%d) failed\n",
				 __func__, (int)size, (int)size);
			dump_debug_alloc();
			return NULL;
		}
	}
	/* return address of the caller */
	entry->caller = __builtin_return_address(0);
	entry->size = size;

	spin_lock(&cm_alloc.lock);
	list_add_tail(&entry->elem, &cm_alloc.chain);
	spin_unlock(&cm_alloc.lock);

	return entry->addr;
#else
	void* mem;

	if (size == 0)
		return NULL;
	mem = kmalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (mem == NULL) {
		mem = vmalloc(size);
		if (mem == NULL)
			pr_alert("CM (%s): No more memory (requested "
				 "size=%d) !!!\n", __func__, (int)size);
	}
	return mem;
#endif
}


/** \ingroup OSAL_IMPLEMENTATION
 * OSAL alloc implementation
 */ 
void* OSAL_Alloc_Zero(t_cm_size size)
{
#ifdef CM_DEBUG_ALLOC
	struct cm_alloc_elem *entry;

	if (size == 0)
		return NULL;

	entry = kzalloc(size + sizeof(*entry), GFP_KERNEL | __GFP_NOWARN);
	if (entry == NULL) {
		entry = vmalloc(size + sizeof(*entry));
		if (entry == NULL) {
			pr_alert("%s: kmalloc(%d) and vmalloc(%d) failed\n",
				 __func__, (int)size, (int)size);
			dump_debug_alloc();
			return NULL;
		} else {
			memset(entry, 0, size + sizeof(*entry));
		}
	}

	/* return address of the caller */
	entry->caller = __builtin_return_address(0);
	entry->size = size;

	spin_lock(&cm_alloc.lock);
	list_add_tail(&entry->elem, &cm_alloc.chain);
	spin_unlock(&cm_alloc.lock);

	return entry->addr;
#else
	void* mem;

	if (size == 0)
		return NULL;
	mem = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (mem == NULL) {
		mem = vmalloc(size);
		if (mem == NULL)
			pr_alert("CM (%s): No more memory (requested "
				 "size=%d) !!!\n", __func__, (int)size);
		else
			memset(mem, 0, size);
	}

	return mem;
#endif
}


/** \ingroup OSAL_IMPLEMENTATION
 *	OSAL free implementation
 */
void OSAL_Free(void* mem)
{
#ifdef CM_DEBUG_ALLOC
	struct cm_alloc_elem *entry = container_of(mem, struct cm_alloc_elem, addr);
	unsigned int i;
	char pattern[4] = { 0xEF, 0xBE, 0xAD, 0xDE };

	if (mem == NULL)
		return;

	/* fill with a pattern to detect bad re-use of this area */
	for (i=0; i<entry->size; i++)
		entry->addr[i] = pattern[i%4];

	spin_lock(&cm_alloc.lock);
	list_del(&entry->elem);
	spin_unlock(&cm_alloc.lock);

	if ((void*)entry >= (void*)VMALLOC_START
	    && (void*)entry < (void*)VMALLOC_END)
		vfree(entry);
	else
		kfree(entry);
#else
	if (mem >= (void*)VMALLOC_START && mem < (void*)VMALLOC_END)
		vfree(mem);
	else
		kfree(mem);
#endif
}

/** \ingroup OSAL_IMPLEMENTATION
 *	OSAL Copy implementation
 * This copy some data from userspace (address to kernel space.
 * This implementation differs on Symbian.
 */
t_cm_error OSAL_Copy(void *dst, const void *src, t_cm_size size)
{
	if (copy_from_user(dst, src, size))
		return CM_UNKNOWN_MEMORY_HANDLE;
	return CM_OK;
}

/**  \ingroup OSAL_IMPLEMENTATION
 *	OSAL write64 function implementation
 */
void OSAL_Write64(t_nmf_trace_channel channel, t_uint8 isTimestamped, t_uint64 value)
{
#ifdef CONFIG_STM_TRACE
	if (isTimestamped)
		stm_tracet_64(channel, value);
	else
		stm_trace_64(channel, value);
#endif
}


/**  \ingroup OSAL_IMPLEMENTATION
 *	OSAL log function implementation
 */
void OSAL_Log(const char *format, int param1, int param2, int param3, int param4, int param5, int param6)
{
	if (cm_use_ftrace)
		trace_printk(format,
			     param1, param2, param3, param4, param5, param6);
	else
		printk(format, param1, param2, param3, param4, param5, param6);
}

/**
 * compute the dsp load
 * 
 * return -1 if in case of failure, a value between 0 and 100 otherwise
 */
static s8 computeDspLoad(t_cm_mpc_load_counter *oldCounter, t_cm_mpc_load_counter *counter)
{
	u32 t, l;

	if ((oldCounter->totalCounter == 0) && (oldCounter->loadCounter == 0))
		return -1; // Failure or not started ?
	if ((counter->totalCounter == 0) && (counter->loadCounter == 0))
		return -1; // Failure or already stopped ?

	if (counter->totalCounter < oldCounter->totalCounter)
		t = (u32)((((u64)-1) - oldCounter->totalCounter)
			  + counter->totalCounter + 1);
	else
		t = (u32)(counter->totalCounter - oldCounter->totalCounter);

	if (counter->loadCounter < oldCounter->loadCounter)
		l = (u32)((((u64)-1) - oldCounter->loadCounter)
			  + counter->loadCounter  + 1);
	else
		l = (u32)(counter->loadCounter - oldCounter->loadCounter);

	if (t == 0) // not significant
		return -1;

	if (l > t) // not significant
		return -1;

	return (l*100) / t;
}

static void wakeup_process(unsigned long data)
{
	wake_up_process((struct task_struct *)data);
}

/**
 * Thread function entry for monitorin the CPU load
 */
static int dspload_monitor(void *idx)
{
	int i = (int)idx;
	unsigned char current_opp_request = FULL_OPP;
	struct mpcConfig *mpc = &osalEnv.mpc[i];
	struct timer_list timer;

	timer.function = wakeup_process;
	timer.data = (unsigned long)current;
	init_timer_deferrable(&timer);

#ifdef CONFIG_DEBUG_FS
	mpc->opp_request = current_opp_request;
#endif
	if (prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
				      (char*)mpc->name,
				      current_opp_request))
		pr_err("CM Driver: Add QoS failed\n");

	/*
	 * Wait for 500ms before initializing the counter,
	 * to let the DSP boot (init of counter will failed if
	 * DSP is not booted).
	 */
	schedule_timeout_uninterruptible(msecs_to_jiffies(500));

	/* init counter */
	if (CM_GetMpcLoadCounter(mpc->coreId,
				 &mpc->oldLoadCounter) != CM_OK)
		pr_warn("CM Driver: Failed to init load counter for %s\n",
			mpc->name);

	while (!kthread_should_stop()) {
		t_cm_mpc_load_counter loadCounter;
		s8 load = -1;
		unsigned long expire;

		__set_current_state(TASK_UNINTERRUPTIBLE);

		expire = msecs_to_jiffies(dspLoadMonitorPeriod) + jiffies;

		mod_timer(&timer, expire);
		schedule();
		/* We can be woken up before the expiration of the timer
		   but we don't need to handle that case as the
		   computation of the DSP load takes that into account */

		if (!test_bit(i, &running_dsp))
			continue;

		if (CM_GetMpcLoadCounter(mpc->coreId,
					 &loadCounter) != CM_OK)
			loadCounter = mpc->oldLoadCounter;

#ifdef CONFIG_DEBUG_FS
		mpc->load =
#endif
		load = computeDspLoad(&mpc->oldLoadCounter, &loadCounter);
		mpc->oldLoadCounter = loadCounter;

		if (load == -1)
			continue;
		/* check if we must request more opp */
		if ((current_opp_request == HALF_OPP)
		    && (load > dspLoadHighThreshold)) {
#ifdef CONFIG_DEBUG_FS
			mpc->opp_request =
#endif
			current_opp_request = FULL_OPP;
			if (cm_debug_level)
				pr_info("CM Driver: Request QoS OPP %d for %s\n",
					current_opp_request, mpc->name);
			prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
						     (char*)mpc->name,
						     current_opp_request);
		}
		/* check if we can request less opp */
		else if ((current_opp_request == FULL_OPP)
			 && (load < dspLoadLowThreshold)) {
#ifdef CONFIG_DEBUG_FS
			mpc->opp_request =
#endif
			current_opp_request = HALF_OPP;
			if (cm_debug_level)
				pr_info("CM Driver: Request QoS OPP %d for %s\n",
					current_opp_request, mpc->name);
			prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
						     (char*)mpc->name,
						     current_opp_request);
		}
	}

#ifdef CONFIG_DEBUG_FS
	mpc->opp_request = mpc->load = 0;
#endif
	del_singleshot_timer_sync(&timer);
	if (cm_debug_level)
		pr_info("CM Driver: Remove QoS OPP for %s\n", mpc->name);
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
				     (char*)mpc->name);
 	return 0;
}

static int enable_auto_pm = 1;
module_param(enable_auto_pm, bool, S_IWUSR|S_IRUGO);

/** \ingroup OSAL_IMPLEMENTATION
 *     Used by CM to disable a power resource
 */
void OSAL_DisablePwrRessource(t_nmf_power_resource resource, t_uint32 firstParam, t_uint32 secondParam)
{
	switch (resource) {
	case CM_OSAL_POWER_SxA_CLOCK: {
		unsigned idx = COREIDX(firstParam);
		struct osal_msg msg;

		if (idx >= NB_MPC) {
			pr_err("CM Driver(%s(res=%d)): core %u unknown\n",
			       __func__, (int)resource, (unsigned)firstParam);
			return;
		}

		cm_debug_destroy_tcm_file(idx);

		/* Stop the DSP load monitoring */
		clear_bit(idx, &running_dsp);
		if (osalEnv.mpc[idx].monitor_tsk) {
			kthread_stop(osalEnv.mpc[idx].monitor_tsk);
			osalEnv.mpc[idx].monitor_tsk = NULL;
		}

		/* Stop the DMA (normally done on DSP side, but be safe) */
		if (firstParam == SIA_CORE_ID)
			cmdma_stop_dma();

		/* Stop the DSP */
		if (regulator_disable(osalEnv.mpc[idx].mmdsp_regulator) < 0)
			pr_err("CM Driver(%s): can't disable regulator %s-mmsdp\n",
			       __func__, osalEnv.mpc[idx].name);
#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&osalEnv.mpc[idx].wakelock);
#endif

		/* Create and dispatch a shutdown service message */
		msg.msg_type = MSG_SERVICE;
		msg.d.srv.srvType = NMF_SERVICE_SHUTDOWN;
		msg.d.srv.srvData.shutdown.coreid = firstParam;
		dispatch_service_msg(&msg);

		/* wake up all trace readers to let them retrieve last traces */
		wake_up_all(&osalEnv.mpc[idx].trace_waitq);
		break;
	}
	case CM_OSAL_POWER_SxA_AUTOIDLE:
		switch (firstParam) {
		case SVA_CORE_ID:
			osalEnv.dsp_sleep.sva_auto_pm_enable = PRCMU_AUTO_PM_OFF;
			osalEnv.dsp_sleep.sva_power_on       = 0;
			osalEnv.dsp_sleep.sva_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF;
			break;
		case SIA_CORE_ID:
			osalEnv.dsp_sleep.sia_auto_pm_enable = PRCMU_AUTO_PM_OFF;
			osalEnv.dsp_sleep.sia_power_on       = 0;
			osalEnv.dsp_sleep.sia_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF;
			break;
		default:
			pr_err("CM Driver(%s(res=%d)): core %u unknown\n", __func__, (int)resource, (unsigned)firstParam);
			return;
		}
		if (enable_auto_pm)
			prcmu_configure_auto_pm(&osalEnv.dsp_sleep, &osalEnv.dsp_idle);
		break;
	case CM_OSAL_POWER_SxA_HARDWARE: {
		unsigned idx = COREIDX(firstParam);
		if (idx >= NB_MPC) {
			pr_err("CM Driver(%s(res=%d)): core %u unknown\n",
			       __func__, (int)resource, (unsigned)firstParam);
			return;
		}
		if (regulator_disable(osalEnv.mpc[idx].pipe_regulator) < 0)
			pr_err("CM Driver(%s): can't disable regulator %s-pipe\n",
			       __func__, osalEnv.mpc[idx].name);
		break;
	}
	case CM_OSAL_POWER_HSEM:
		break;
	case CM_OSAL_POWER_SDRAM:
		break;
	case CM_OSAL_POWER_ESRAM: {
		int i;
		/* firstParam: base address; secondParam: size
		   U8500_ESRAM_BASE is the start address of BANK 0,
		   BANK size=0x20000 */

		/* Compute the relative end address of the range,
		   relative to base address of BANK1 */
		secondParam = (firstParam+secondParam-U8500_ESRAM_BANK1-1);

		/* if end is below base address of BANK1, it means that full
		   range of addresses is on Bank0 */
		if (((int)secondParam) < 0)
			break;
		/* Compute the index of the last bank accessed among
		   esram 1+2 and esram 3+4 banks */
		secondParam /= (2*U8500_ESRAM_BANK_SIZE);
		WARN_ON(secondParam > 1);

		/* Compute the index of the first bank accessed among esram 1+2
		   and esram 3+4 banks
		   Do not manage Bank 0 (secured, must be always ON) */
		if (firstParam < U8500_ESRAM_BANK1)
			firstParam  = 0;
		else
			firstParam  = (firstParam-U8500_ESRAM_BANK1)/(2*U8500_ESRAM_BANK_SIZE);

		/* power off the banks 1+2 and 3+4 if accessed. */
		for (i=firstParam; i<=secondParam; i++) {
			if (regulator_disable(osalEnv.esram_regulator[i]) < 0)
				pr_err("CM Driver(%s): can't disable regulator"
				       "for esram bank %s\n", __func__,
				       i ? "34" : "12");
		}
		break;
	}
	default:
		pr_err("CM Driver(%s): resource %d unknown/not supported\n",
		       __func__, (int)resource);
	}
}

/** \ingroup OSAL_IMPLEMENTATION
 *     Used by CM to enable a power resource 
 */
t_cm_error OSAL_EnablePwrRessource(t_nmf_power_resource resource, t_uint32 firstParam, t_uint32 secondParam)
{
	switch (resource) {
	case CM_OSAL_POWER_SxA_CLOCK: {
		unsigned idx = COREIDX(firstParam);

		if (idx > NB_MPC) {
			pr_err("CM Driver(%s(res=%d)): core %u unknown\n", __func__, (int)resource, (unsigned)firstParam);
			return CM_INVALID_PARAMETER;
		}

		/* Start the DSP */
#ifdef CONFIG_HAS_WAKELOCK
		wake_lock(&osalEnv.mpc[idx].wakelock);
#endif
		if (regulator_enable(osalEnv.mpc[idx].mmdsp_regulator) < 0)
			pr_err("CM Driver(%s): can't enable regulator %s-mmsdp\n", __func__, osalEnv.mpc[idx].name);

		/* Start the DSP load monitoring for this dsp */
		set_bit(idx, &running_dsp);
		osalEnv.mpc[idx].monitor_tsk = kthread_run(&dspload_monitor,
							   (void*)idx,
							   "%s-loadd",
							   osalEnv.mpc[idx].name);
		if (IS_ERR(osalEnv.mpc[idx].monitor_tsk)) {
			pr_err("CM Driver: failed to start dspmonitord "
			       "thread: %ld\n", PTR_ERR(osalEnv.mpc[idx].monitor_tsk));
			osalEnv.mpc[idx].monitor_tsk = NULL;
		}

		cm_debug_create_tcm_file(idx);
		break;
	}
	case CM_OSAL_POWER_SxA_AUTOIDLE:
		switch (firstParam) {
		case SVA_CORE_ID:
			osalEnv.dsp_sleep.sva_auto_pm_enable = PRCMU_AUTO_PM_ON;
			osalEnv.dsp_sleep.sva_power_on       = PRCMU_AUTO_PM_POWER_ON_HSEM | PRCMU_AUTO_PM_POWER_ON_ABB_FIFO_IT;
			osalEnv.dsp_sleep.sva_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_RAMRET_HWP_OFF;
			break;
		case SIA_CORE_ID:
			osalEnv.dsp_sleep.sia_auto_pm_enable = PRCMU_AUTO_PM_ON;
			osalEnv.dsp_sleep.sia_power_on       = PRCMU_AUTO_PM_POWER_ON_HSEM | PRCMU_AUTO_PM_POWER_ON_ABB_FIFO_IT;
			osalEnv.dsp_sleep.sia_policy         = PRCMU_AUTO_PM_POLICY_DSP_OFF_RAMRET_HWP_OFF;
			break;
		default:
			pr_err("CM Driver(%s(res=%d)): core %u unknown\n", __func__, (int)resource, (unsigned)firstParam);
			return CM_INVALID_PARAMETER;
		}
		if (enable_auto_pm)
			prcmu_configure_auto_pm(&osalEnv.dsp_sleep, &osalEnv.dsp_idle);
		break;
	case CM_OSAL_POWER_SxA_HARDWARE: {
		unsigned idx = COREIDX(firstParam);

		if (idx > NB_MPC) {
			pr_err("CM Driver(%s(res=%d)): core %u unknown\n", __func__, (int)resource, (unsigned)firstParam);
			return CM_INVALID_PARAMETER;
		}
		if (regulator_enable(osalEnv.mpc[idx].pipe_regulator) < 0)
			pr_err("CM Driver(%s): can't enable regulator %s-pipe\n", __func__, osalEnv.mpc[idx].name);
		break;
	}
	case CM_OSAL_POWER_HSEM:
		return CM_OK;
	case CM_OSAL_POWER_SDRAM:
		break;
	case CM_OSAL_POWER_ESRAM:
	{
		int i;
		/* firstParam: base address; secondParam: size
		   U8500_ESRAM_BASE is the start address of BANK 0,
		   BANK size=0x20000 */

		/* Compute the relative end address of the range, relative
		   to base address of BANK1 */
		secondParam = (firstParam+secondParam-U8500_ESRAM_BANK1-1);

		/* if end is below base address of BANK1, it means that full
		   range of addresses is on Bank0 */
		if (((int)secondParam) < 0)
			break;
		/* Compute the index of the last bank accessed among esram 1+2
		   and esram 3+4 banks */
		secondParam /= (2*U8500_ESRAM_BANK_SIZE);
		WARN_ON(secondParam > 1);

		/* Compute the index of the first bank accessed among esram 1+2
		   and esram 3+4 banks
		   Do not manage Bank 0 (secured, must be always ON) */
		if (firstParam < U8500_ESRAM_BANK1)
			firstParam  = 0;
		else
			firstParam  = (firstParam-U8500_ESRAM_BANK1)/(2*U8500_ESRAM_BANK_SIZE);

		/* power on the banks 1+2 and 3+4 if accessed. */
		for (i=firstParam; i<=secondParam; i++) {
			if (regulator_enable(osalEnv.esram_regulator[i]) < 0)
				pr_err("CM Driver(%s): can't enable regulator "
				       "for esram bank %s\n", __func__,
				       i ? "34" : "12");
		}
		break;
	}
	default:
		pr_err("CM Driver(%s): resource %x unknown/not supported\n",
		       __func__, (int)resource);
		return CM_INVALID_PARAMETER;
	}

	return CM_OK;
}

/*!
  * \brief Generate 'software' panic to notify cm users
  *  that a problem occurs but no dsp panic has been sent yet
  * (for example a dsp crash)
  * \ingroup CM_ENGINE_OSAL_API
  */
void OSAL_GeneratePanic(t_nmf_core_id coreId, t_uint32 reason)
{
	struct osal_msg msg;

	/* Create and dispatch a panic service message */
	msg.msg_type = MSG_SERVICE;
	msg.d.srv.srvType = NMF_SERVICE_PANIC;
	msg.d.srv.srvData.panic.panicReason = MPC_NOT_RESPONDING_PANIC;
	msg.d.srv.srvData.panic.panicSource = MPC_EE;
	msg.d.srv.srvData.panic.info.mpc.coreid = coreId;
	msg.d.srv.srvData.panic.info.mpc.faultingComponent = 0;
	msg.d.srv.srvData.panic.info.mpc.panicInfo1 = reason;
	msg.d.srv.srvData.panic.info.mpc.panicInfo2 = 0;
	dispatch_service_msg(&msg);

	/* wake up all trace readers to let them retrieve last traces */
	wake_up_all(&osalEnv.mpc[COREIDX(coreId)].trace_waitq);
}

/*!
 * \brief Generate an OS-Panic. Called in from CM_ASSERT().
 * \ingroup CM_ENGINE_OSAL_API
 */
void OSAL_Panic(void)
{
	panic("FATAL ISSUE IN THE CM DRIVER !!");
}
#include <mach/dcache.h>
/*!
 * \brief Clean data cache in DDR in order to be accessible from peripheral.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
void OSAL_CleanDCache(t_uint32 startAddr, t_uint32 size)
{
#if 0
	/*
	 * Currently, the code sections are non-cached/buffered,
	 * which normally doesn't required the maintenance done below.
	 * As the cost is low (doesn't do much thing), I keep it in case
	 * of the memory settings are changed later.
	 */

	struct hwmem_region region;
	struct mpcConfig *mpc;
	t_uint32 endAddr = startAddr + size;

	if (startAddr >= (u32)osalEnv.mpc[0].sdram_code.data
	    && endAddr <= (u32)(osalEnv.mpc[0].sdram_code.data
				+ osalEnv.mpc[0].sdram_code.size)) {
		mpc = &osalEnv.mpc[0];
	} else if (startAddr >= (u32)osalEnv.mpc[1].sdram_code.data
		   && endAddr <= (u32)(osalEnv.mpc[1].sdram_code.data
				       + osalEnv.mpc[1].sdram_code.size)) {
		mpc = &osalEnv.mpc[1];
	} else {
		/* The code may be in esram, in that case, nothing to do */
		return;
	}

	region.offset = startAddr - (u32)mpc->sdram_code.data;
	region.count  = 1;
	region.start  = 0;
	region.end    = size;
	region.size   = size;
	hwmem_set_domain(mpc->hwmem_code, HWMEM_ACCESS_READ,
			 HWMEM_DOMAIN_SYNC, &region);
	/*
	 * The hwmem keep track of region being sync or not.
	 * Mark the region as being write-accessed here right now
	 * to let following clean being done as expected. Today,
	 * there is no other place to do that in CM Core right now
	 */
	hwmem_set_domain(mpc->hwmem_code, HWMEM_ACCESS_WRITE,
			 HWMEM_DOMAIN_CPU, &region);
#else
	dsb();
	outer_cache.sync();
#endif
}

/*!
 * \brief Flush write-buffer of L2 cache
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
void OSAL_mb(void)
{
	mb();
}

/*!
 * \brief return prcmu timer value.
 *
 * This is need for perfmeter api  (see \ref t_nmf_power_resource)
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
t_uint64 OSAL_GetPrcmuTimer()
{
	t_uint64 msbBefore;
	t_uint32 lsb;
	t_uint64 msbAfter;

	/* read prcmu timers */
	msbBefore = ~ioread32(prcmu_tcdm_base+0xDE4);
	lsb = ~ioread32(prcmu_base+0x454);
	msbAfter = ~ioread32(prcmu_tcdm_base+0xDE4);

	/* handle rollover test case */
	// NOTE : there is still a window in prcmu side between counter rollover
	// and prcmu interrupt handling
	// to update msb register => this can lead to erroneous value return here
	if (msbBefore == msbAfter || lsb >= 0x80000000UL)
		return (((msbBefore & 0xffffffUL) << 32) + lsb);
	else
		return (((msbAfter & 0xffffffUL) << 32) + lsb);
}

/*!
 * \brief Disable the service message handling (panic, etc)
 *
 * It must disable the handling of all service messages
 * If a service message is currently handled, it must wait till the end
 * of its managment before returning.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_DisableServiceMessages(void) {
	tasklet_disable(&cmld_service_tasklet);
}

/*!
 * \brief Enable the service message handling (panic, etc)
 *
 * It enables the handling of all service messages
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_EnableServiceMessages(void) {
	tasklet_enable(&cmld_service_tasklet);
}
