/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef OSAL_KERNEL_H
#define OSAL_KERNEL_H

#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/hwmem.h>
#include <linux/regulator/consumer.h>
#include <linux/plist.h>
#include <linux/version.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#include <linux/mfd/dbx500-prcmu.h>
#include <cm/engine/api/channel_engine.h>
#include <cm/engine/api/control/configuration_engine.h>
#include <cm/engine/api/perfmeter_engine.h>
/*
 * Do not include ELF definition from cm/engine/elf/inc/elfapi.h file
 * because it conflicts with definition from linux/elf.h file
 */
#define _CM_ELF_H
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

#include "configuration.h"

/*
 * Per-MPC configuration structure
 * Use struct debugfs_blob_wrapper to store pointer and size of section
 * to allow easy re-use of data through debugfs
 */
struct mpcConfig {
	const t_nmf_core_id coreId;      /**< MPC coreId */
	const char *name;                /**< MPC name */
        t_uint8    nbYramBanks;          /**< number of TCM ram banks to reserve for y memory */
	t_nmf_executive_engine_id eeId;  /**< Type of Executive Engine */
	const void *base_phys;           /**< Physical base address of the MPC */
	struct debugfs_blob_wrapper base;/**< Remapped base address of the MPC and size of TCM24 */
	struct hwmem_alloc *hwmem_code;  /**< hwmem code segment */
	u32        sdram_code_phys;      /**< Physical base address for MPC SDRAM Code region */
	struct debugfs_blob_wrapper sdram_code; /**< Remapped base address and size for MPC SDRAM Code */
	struct hwmem_alloc *hwmem_data;  /**< hwmem data segment */
	u32        sdram_data_phys;      /**< Physical base address for MPC SDRAM Data region */
	struct debugfs_blob_wrapper sdram_data; /**< Remapped base address and size for MPC SDRAM Data */
	const unsigned int interrupt0;   /**< interrupt line triggered by the MPC, for MPC events (if HSEM not used) */
	const unsigned int interrupt1;   /**< interrupt line triggered by the MPC, for PANIC events */
	struct tasklet_struct tasklet;   /**< taskket used to process MPC events */
	struct regulator *mmdsp_regulator; /**< mmdsp regulator linked to this MPC */
	struct regulator *pipe_regulator;  /**< hardware pipe linked to this MPC */
#ifdef CONFIG_HAS_WAKELOCK
	struct wake_lock wakelock;       /**< wakelock for this MPC to prevent ARM to go in APSLEEP state */
#endif
	struct task_struct *monitor_tsk; /**< task to monitor the dsp load; */
	t_cm_mpc_load_counter oldLoadCounter; /**< previous load counter of the DSP */
	wait_queue_head_t trace_waitq;   /**< wait queue used to block trace_read() call */
#ifdef CONFIG_DEBUG_FS
	struct dentry *dir;              /**< debugfs dir entry */
	struct dentry *comp_dir;         /**< debugfs component dir entry */
	struct dentry *domain_dir;       /**< debugfs domain dir entry */
	struct dentry *snapshot_dir;     /**< debugfs snapshot dir entry */
	struct dentry *mem_file;         /**< debugfs meminfo file entry */
	struct dentry *tcm_file;         /**< debugfs meminfo file entry */
	struct dentry *esram_file;       /**< debugfs meminfo file entry */
	s8         load;                 /**< current load of the DSP */
	s8         opp_request;          /**< current requested opp of the DSP */
#endif
};

/** Describes current Kernel OSAL environment
 *
 * Note about mpc.tasklet : we declare one tasklet per MPC but their usage depends
 * on cfgSemaphoreTypeHSEM.
 *
 * This tasklet is scheduled by the interrupt handler to process MPC Events.
 * - If we use Hardware Semaphore, there is only one interrupt handler used
 * and thus only one tasklet, tasklet of MPC 0 (ie osalEnv.mpc[0].tasklet)
 * - If we use local semaphore, there is one interrupt handler and tasklet per mpc
 */
struct OsalEnvironment
{
	struct mpcConfig mpc[NB_MPC];
	void* hwsem_base;  /** < Remapped base address of the hardware semaphores */
	unsigned long esram_base_phys; /**< Physical base address of embedded RAM used within the CM */
	void* esram_base;  /**< Remapped base address embedded RAM used within the CM */
	struct regulator *esram_regulator[NB_ESRAM]; /**< regulator for ESRAM bank 1+2 and 3+4 */
	struct prcmu_auto_pm_config dsp_sleep;
	struct prcmu_auto_pm_config dsp_idle;
};


/** Structure used to store the skeleton related data.
 *  It is used for communicattion from a MPC to a user process (=host)
 */
typedef struct {
	struct list_head entry; /**< Doubly linked list descriptor */
	t_cm_bf_mpc2host_handle mpc2hostId;  /**< mpc2host ID */
	t_nmf_mpc2host_handle upperLayerThis;/**< upper-layer handle */
	struct cm_channel_priv* channelPriv; /**< Per-channel private data. The actual message queue is hold here */
} t_skelwrapper;

/** Message description for MPC to HOST communication
 */
struct osal_msg {
	struct {
		struct plist_node entry; /**< Doubly linked list descriptor */
		t_message_type type;     /**< Type of message (callback, service or interrupt for now) */
	} hdr; /**< Header of the message */
#define msg_entry hdr.entry
#define msg_type hdr.type
	union {
		struct {
			t_skelwrapper *skelwrap; /**< Link to the skelwrapper, to retrieve the channel on which this message has to be forwarded */
			t_uint32 methodIdx; /**< callback data: method index*/
			t_event_params_handle anyPtr;  /**< callback data: method parameters */
			t_uint32 ptrSize;  /**< size of the parameters */
		} itf; /**< structure holding callback data */
		struct {
			t_nmf_service_type srvType; /**< Type of the service */
			t_nmf_service_data srvData; /**< Data of the service */
		} srv; /**< structure holding service data */
	} d; /**< data */
};

extern struct OsalEnvironment osalEnv;

/** Environment initialization/deinitialization */
int remapRegions(void);
void unmapRegions(void);

/** Component manager configuration getters for CM_ENGINE_Init() */
int getNmfHwMappingDesc(t_nmf_hw_mapping_desc* nmfHwMappingDesc);

/** Component manager configuration getters for CM_ConfigureMediaProcessorCore (SVA and SIA) */
void getMpcSystemAddress(unsigned i, t_cm_system_address* mpcSystemAddress);
void getMpcSdramSegments(unsigned i, t_nmf_memory_segment* codeSegment, t_nmf_memory_segment* dataSegment);

#ifdef CM_DEBUG_ALLOC
struct cm_alloc {
	spinlock_t        lock;
	struct list_head  chain;
};

struct cm_alloc_elem {
	struct list_head elem;
	void *caller;
	size_t size;
	char addr[0];
};

void init_debug_alloc(void);
void cleanup_debug_alloc(void);
#endif /* CM_DEBUG_ALLOC */

/* TODO: To remove later */
extern __iomem void *prcmu_base;
extern __iomem void *prcmu_tcdm_base;
extern const char *cmld_devname[];

#define PRCM_SVAMMDSPCLK_MGT                (prcmu_base + 0x008)
#define PRCM_SIAMMDSPCLK_MGT                (prcmu_base + 0x00c)

#endif /* OSAL_KERNEL_H */
