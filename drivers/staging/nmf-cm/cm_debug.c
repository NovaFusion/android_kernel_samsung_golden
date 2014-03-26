/*
 * Copyright (C) ST-Ericsson SA 2011
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/proc_fs.h>

#include "osal-kernel.h"
#include "cm_debug.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/sched.h>

static struct dentry *cm_dir;        /* nmf-cm/            */
static struct dentry *proc_dir;      /* nmf-cm/proc/       */
static struct dentry *core_dir;      /* nmf-cm/dsp/        */
static struct dentry *domain_dir;    /* nmf-cm/domains/    */

/* components data managment */
struct cm_debug_component_cooky {
	struct dentry *comp_file; /* entry in nmf-cm/dsp/sxa/components/ */
	struct dentry *proc_link; /* entry in nmf-cm/proc/       */
};

static ssize_t component_read(struct file *file, char __user *userbuf,
			      size_t count, loff_t *ppos) {
	t_component_instance *component = file->f_dentry->d_inode->i_private;
	char buf[640];
	int ret=0;

	OSAL_LOCK_API();
	if ((component != NULL) && (component->dbgCooky != NULL)) {
		char nb_i[16] = "";
		int i;

		if (component->Template->classe == SINGLETON)
			snprintf(nb_i, sizeof(nb_i), " (%d)",
				 component->Template->numberOfInstance);

		ret = snprintf(buf, sizeof(buf),
			       "Name:\t\t%s <%s>\n"
			       "Class:\t\t%s%s\n"
			       "State:\t\t%s\n"
			       "Priority:\t%u\n"
			       "Domain:\t\t%u\n\n"
			       "Memory    : Physical address  Logical address"
			       "   DSP address           Size\n"
			       "---------------------------------------------"
			       "-----------------------------\n",
			       component->pathname,
			       component->Template->name,
			       component->Template->classe == COMPONENT ?
			       "Component" :
			       (component->Template->classe == SINGLETON ?
				"Singleton" :
				(component->Template->classe == FIRMWARE ?
				 "Firmware" :
				 "?")),
			       nb_i,
			       component->state == STATE_RUNNABLE ? "Runnable" :
			       (component->state == STATE_STOPPED ? "Sopped" :
				"None"),
			       (unsigned)component->priority,
			       component->domainId
			);

		for (i=0; i<NUMBER_OF_MMDSP_MEMORY && ret<sizeof(buf); i++) {
			if (component->memories[i]) {
				t_cm_system_address addr;
				t_uint32 dspAddr, dspSize;
				cm_DSP_GetHostSystemAddress(
					component->memories[i], &addr);
				cm_DSP_GetDspAddress(
					component->memories[i], &dspAddr);
				cm_DSP_GetDspMemoryHandleSize(
					component->memories[i], &dspSize);
				ret += snprintf(
					&buf[ret], sizeof(buf)-ret,
					"%-10s: %p-%p %p-%p %p-%p %8lu\n",
					MMDSP_getMappingById(i)->memoryName,
					(void *)addr.physical,
					(void *)addr.physical
					+ component->memories[i]->size-1,
					(void *)addr.logical,
					(void *)addr.logical
					+ component->memories[i]->size-1,
					(void *)dspAddr,
					(void *)dspAddr + dspSize - 1,
					component->memories[i]->size);
			}
		}
	}

	OSAL_UNLOCK_API();
	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations component_fops = {
	.read = component_read,
};

static void cm_debug_component_create(t_component_instance *component)
{
        char tmp[12+MAX_COMPONENT_NAME_LENGTH];
	struct cm_debug_component_cooky *cooky;
	struct mpcConfig *mpc;
	mpc = &osalEnv.mpc[COREIDX(component->Template->dspId)];

	cooky = OSAL_Alloc_Zero(sizeof(*cooky));
	if (cooky == NULL)
		return;

	component->dbgCooky = cooky;
	sprintf(tmp, "%s-%08x", component->pathname,
		(unsigned int)component->instance);
	cooky->comp_file = debugfs_create_file(tmp, S_IRUSR|S_IRGRP,
					       mpc->comp_dir,
					       component, &component_fops);
	if (IS_ERR(cooky->comp_file)) {
		if (PTR_ERR(cooky->comp_file) != -ENODEV)
			pr_info("CM: Can't create dsp/%s/components/%s"
				"debugfs file: %ld\n",
				mpc->name,
				tmp,
				PTR_ERR(cooky->comp_file));
		cooky->comp_file = NULL;
	} else {
		char target_lnk[40+MAX_COMPONENT_NAME_LENGTH];
		sprintf(target_lnk, "../../../dsp/%s/components/%s-%08x",
			mpc->name,
			component->pathname,
			(unsigned int)component->instance);

		/* Some firmware, like Executive Engine, do not belong
		   to any process */
		if (domainDesc[component->domainId].client == current->tgid) {
			struct list_head* head;
			struct cm_process_priv *entry = NULL;
			/* Search the entry for the calling process */
			list_for_each(head, &process_list) {
				entry = list_entry(head,
						   struct cm_process_priv,
						   entry);
				if (entry->pid == current->tgid)
					break;
			}

			if (entry) {
				cooky->proc_link = debugfs_create_symlink(
					tmp,
					entry->comp_dir,
					target_lnk);
				if (IS_ERR(cooky->proc_link)) {
					long err = PTR_ERR(cooky->proc_link);
					if (err != -ENODEV)
						pr_info("CM: Can't create "
							"proc/%d/%s "
							"debugfs link: %ld\n",
							entry->pid, tmp, err);
					cooky->proc_link = NULL;
				}
			}
		}
	}
}

static void cm_debug_component_destroy(t_component_instance *component)
{
	struct cm_debug_component_cooky *cooky = component->dbgCooky;

	if (cooky) {
		component->dbgCooky = NULL;
		debugfs_remove(cooky->proc_link);
		debugfs_remove(cooky->comp_file);
		OSAL_Free(cooky);
	}
}

/* domain data managment */
struct cm_debug_domain_cooky {
	struct dentry *domain_file; /* entry in nmf-cm/components/ */
	struct dentry *proc_link;   /* entry in nmf-cm/proc/       */
	struct dentry *dsp_link;    /* entry in nmf-cm/dsp/sxa/domains */
};

static ssize_t domain_read(struct file *file, char __user *userbuf,
			   size_t count, loff_t *ppos)
{
	t_cm_domain_id id =
		(t_cm_domain_id)(long)file->f_dentry->d_inode->i_private;
	t_cm_domain_desc *domain = &domainDesc[id];

	char buf[640];
	int ret=0;

	OSAL_LOCK_API();
	if ((domain->domain.coreId != MASK_ALL8)
	    && (domain->dbgCooky != NULL)) {
		t_cm_allocator_status status;
		t_uint32 dOffset;
		t_uint32 dSize;
		if (domain->domain.coreId != ARM_CORE_ID) {
			t_cm_domain_info info;

			cm_DM_GetDomainAbsAdresses(id, &info);
			cm_DSP_GetInternalMemoriesInfo(id, ESRAM_CODE,
						       &dOffset, &dSize);
			cm_MM_GetAllocatorStatus(
				cm_DSP_GetAllocator(domain->domain.coreId,
						    ESRAM_CODE),
				dOffset, dSize, &status);
			ret = snprintf(
				buf, sizeof(buf),
				"Core:\t%s\n\n"
				"Memory    : Physical address  Logical address"
				"     Size     Free     Used\n"
				"---------------------------------------------"
				"-----------------------------\n"
				"ESRAM Code: %08x-%08lx %08x-%08lx\t%8lu %8lu "
				"%8lu\n",
				osalEnv.mpc[COREIDX(domain->domain.coreId)].name,
				(unsigned int)info.esramCode.physical,
				domain->domain.esramCode.size ?
				info.esramCode.physical
				+ domain->domain.esramCode.size - 1 : 0,
				(unsigned int)info.esramCode.logical,
				domain->domain.esramCode.size ?
				info.esramCode.logical
				+ domain->domain.esramCode.size - 1 : 0,
				domain->domain.esramCode.size,
				status.global.accumulate_free_memory,
				status.global.accumulate_used_memory);

			cm_DSP_GetInternalMemoriesInfo(id, ESRAM_EXT24,
						       &dOffset, &dSize);
			cm_MM_GetAllocatorStatus(
				cm_DSP_GetAllocator(domain->domain.coreId,
						    ESRAM_EXT24),
				dOffset, dSize, &status);
			ret += snprintf(
				&buf[ret], sizeof(buf)-ret,
				"ESRAM Data: %08x-%08lx "
				"%08x-%08lx\t%8lu %8lu %8lu\n",
				(unsigned int)info.esramData.physical,
				domain->domain.esramData.size ?
				info.esramData.physical
				+ domain->domain.esramData.size - 1 : 0,
				(unsigned int)info.esramData.logical,
				domain->domain.esramData.size ?
				info.esramData.logical
				+ domain->domain.esramData.size - 1 : 0,
				domain->domain.esramData.size,
				status.global.accumulate_free_memory,
				status.global.accumulate_used_memory);

			cm_DSP_GetInternalMemoriesInfo(id, SDRAM_CODE,
						       &dOffset, &dSize);
			cm_MM_GetAllocatorStatus(
				cm_DSP_GetAllocator(domain->domain.coreId,
						    SDRAM_CODE),
				dOffset, dSize, &status);
			ret += snprintf(
				&buf[ret], sizeof(buf)-ret,
				"SDRAM Code: %08x-%08lx "
				"%08x-%08lx\t%8lu %8lu %8lu\n",
				(unsigned int)info.sdramCode.physical,
				domain->domain.sdramCode.size ?
				info.sdramCode.physical +
				domain->domain.sdramCode.size - 1 : 0,
				(unsigned int)info.sdramCode.logical,
				domain->domain.sdramCode.size ?
				info.sdramCode.logical +
				domain->domain.sdramCode.size - 1 : 0,
				domain->domain.sdramCode.size,
				status.global.accumulate_free_memory,
				status.global.accumulate_used_memory);

			cm_DSP_GetInternalMemoriesInfo(id, SDRAM_EXT24,
						       &dOffset, &dSize);
			cm_MM_GetAllocatorStatus(
				cm_DSP_GetAllocator(domain->domain.coreId,
						    SDRAM_EXT24),
				dOffset, dSize, &status);
			ret += snprintf(
				&buf[ret], sizeof(buf)-ret,
				"SDRAM Data: %08x-%08lx "
				"%08x-%08lx\t%8lu %8lu %8lu\n",
				(unsigned int)info.sdramData.physical,
				domain->domain.sdramData.size ?
				info.sdramData.physical +
				domain->domain.sdramData.size - 1 : 0,
				(unsigned int)info.sdramData.logical,
				domain->domain.sdramData.size ?
				info.sdramData.logical +
				domain->domain.sdramData.size - 1 : 0,
				domain->domain.sdramData.size,
				status.global.accumulate_free_memory,
				status.global.accumulate_used_memory);
		} else {
			t_cm_system_address addr;
			ret = snprintf(
				buf, sizeof(buf),
				"Core:\tarm\n\n"
				"Memory    : Physical address  Logical "
				"address     Size     Free     Used\n"
				"---------------------------------------"
				"-----------------------------------\n");
			if (domain->domain.esramCode.size &&
			    cm_DSP_GetDspBaseAddress(ARM_CORE_ID,
						     ESRAM_CODE,
						     &addr) == CM_OK) {
				cm_DSP_GetInternalMemoriesInfo(id, ESRAM_CODE,
							       &dOffset,
							       &dSize);
				cm_MM_GetAllocatorStatus(
					cm_DSP_GetAllocator(ARM_CORE_ID,
							    ESRAM_CODE),
					dOffset, dSize, &status);
				ret += snprintf(
					&buf[ret], sizeof(buf)-ret,
					"ESRAM Code: %08x-%08lx "
					"%08x-%08lx\t%8lu %8lu %8lu\n",
					(unsigned int)addr.physical,
					addr.physical +
					domain->domain.esramCode.size - 1,
					(unsigned int)addr.logical,
					addr.logical +
					domain->domain.esramCode.size - 1,
					domain->domain.esramCode.size,
					status.global.accumulate_free_memory,
					status.global.accumulate_used_memory);
			}
			if (domain->domain.esramData.size &&
			    cm_DSP_GetDspBaseAddress(ARM_CORE_ID,
						     ESRAM_EXT24,
						     &addr) == CM_OK) {
				cm_DSP_GetInternalMemoriesInfo(id, ESRAM_EXT24,
							       &dOffset,
							       &dSize);
				cm_MM_GetAllocatorStatus(
					cm_DSP_GetAllocator(ARM_CORE_ID,
							    ESRAM_EXT24),
					dOffset, dSize, &status);
				ret += snprintf(
					&buf[ret], sizeof(buf)-ret,
					"ESRAM Data: %08x-%08lx "
					"%08x-%08lx\t%8lu %8lu %8lu\n",
					(unsigned int)addr.physical,
					addr.physical +
					domain->domain.esramData.size - 1,
					(unsigned int)addr.logical,
					addr.logical +
					domain->domain.esramData.size - 1,
					domain->domain.esramData.size,
					status.global.accumulate_free_memory,
					status.global.accumulate_used_memory);
			}
			if (domain->domain.sdramCode.size &&
			    cm_DSP_GetDspBaseAddress(ARM_CORE_ID,
						     SDRAM_CODE,
						     &addr) == CM_OK) {
				cm_DSP_GetInternalMemoriesInfo(id, SDRAM_CODE,
							       &dOffset,
							       &dSize);
				cm_MM_GetAllocatorStatus(
					cm_DSP_GetAllocator(ARM_CORE_ID,
							    SDRAM_CODE),
					dOffset, dSize, &status);
				ret += snprintf(
					&buf[ret], sizeof(buf)-ret,
					"SDRAM Code: %08x-%08lx %08x-%08lx\t"
					"%8lu %8lu %8lu\n",
					(unsigned int)addr.physical,
					addr.physical +
					domain->domain.sdramCode.size - 1,
					(unsigned int)addr.logical,
					addr.logical +
					domain->domain.sdramCode.size - 1,
					domain->domain.sdramCode.size,
					status.global.accumulate_free_memory,
					status.global.accumulate_used_memory);
			}
			if (domain->domain.sdramData.size &&
			    cm_DSP_GetDspBaseAddress(ARM_CORE_ID,
						     SDRAM_EXT24,
						     &addr) == CM_OK) {
				cm_DSP_GetInternalMemoriesInfo(id, SDRAM_EXT24,
							       &dOffset,
							       &dSize);
				cm_MM_GetAllocatorStatus(
					cm_DSP_GetAllocator(ARM_CORE_ID,
							    SDRAM_EXT24),
					dOffset, dSize, &status);
				ret += snprintf(
					&buf[ret], sizeof(buf)-ret,
					"SDRAM Data: %08x-%08lx %08x-%08lx\t"
					"%8lu %8lu %8lu\n",
					(unsigned int)addr.physical,
					addr.physical +
					domain->domain.sdramData.size - 1,
					(unsigned int)addr.logical,
					addr.logical +
					domain->domain.sdramData.size - 1,
					domain->domain.sdramData.size,
					status.global.accumulate_free_memory,
					status.global.accumulate_used_memory);
			}
		}
	}
	OSAL_UNLOCK_API();
	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);;
}

static const struct file_operations domain_fops = {
	.read = domain_read,
};

static void cm_debug_domain_create(t_cm_domain_id id)
{
        char tmp[12];
	struct cm_debug_domain_cooky *cooky;

	cooky = OSAL_Alloc_Zero(sizeof(*cooky));
	if (cooky == NULL)
		return;

	domainDesc[id].dbgCooky = cooky;
	sprintf(tmp, "%u", id);
	cooky->domain_file = debugfs_create_file(tmp, S_IRUSR|S_IRGRP,
						 domain_dir,
						 (void *)(long)id,
						 &domain_fops);
	if (IS_ERR(cooky->domain_file)) {
		if (PTR_ERR(cooky->domain_file) != -ENODEV)
			pr_err("CM: Can't create domains/%s debugfs "
			       "file: %ld\n", tmp,
			       PTR_ERR(cooky->domain_file));
		cooky->domain_file = NULL;
	} else {
		char target_lnk[40];
		sprintf(target_lnk, "../../../domains/%u", id);

		if (domainDesc[id].client != NMF_CORE_CLIENT) {
			struct list_head* head;
			struct cm_process_priv *entry = NULL;

			/* Search the entry for the target process */
			list_for_each(head, &process_list) {
				entry = list_entry(head,
						   struct cm_process_priv,
						   entry);
				if (entry->pid == domainDesc[id].client)
					break;
			}

			if (entry) {
				cooky->proc_link = debugfs_create_symlink(
					tmp,
					entry->domain_dir,
					target_lnk);
				if (IS_ERR(cooky->proc_link)) {
					long err = PTR_ERR(cooky->proc_link);
					if (err != -ENODEV)
						pr_err("CM: Can't create "
						       "proc/%d/domains/%s "
						       "debugfs link: %ld\n",
						       entry->pid, tmp, err);
					cooky->proc_link = NULL;
				}
			}
		}
		if (domainDesc[id].domain.coreId != ARM_CORE_ID) {
			cooky->dsp_link =
				debugfs_create_symlink(
					tmp,
					osalEnv.mpc[COREIDX(domainDesc[id].domain.coreId)].domain_dir,
					target_lnk);
			if (IS_ERR(cooky->dsp_link)) {
				if (PTR_ERR(cooky->dsp_link) != -ENODEV)
					pr_err("CM: Can't create dsp/%s/domains/%s "
					       "debugfs link: %ld\n",
					       osalEnv.mpc[COREIDX(domainDesc[id].domain.coreId)].name,
					       tmp,
					       PTR_ERR(cooky->dsp_link));
				cooky->dsp_link = NULL;
			}
		}
	}
}

static void cm_debug_domain_destroy(t_cm_domain_id id)
{
	struct cm_debug_domain_cooky *cooky = domainDesc[id].dbgCooky;
	if (cooky) {
		domainDesc[id].dbgCooky = NULL;
		debugfs_remove(cooky->proc_link);
		debugfs_remove(cooky->dsp_link);
		debugfs_remove(cooky->domain_file);
		OSAL_Free(cooky);
	}
}

/* proc directory */
void cm_debug_proc_init(struct cm_process_priv *entry)
{
        char tmp[PROC_NUMBUF];
        sprintf(tmp, "%d", entry->pid);
	entry->dir = debugfs_create_dir(tmp, proc_dir);
	if (IS_ERR(entry->dir)) {
		if (PTR_ERR(entry->dir) != -ENODEV)
			pr_info("CM: Can't create proc/%d debugfs directory: "
				"%ld\n", entry->pid, PTR_ERR(entry->dir));
		entry->dir = NULL;
		return;
	}
	entry->comp_dir = debugfs_create_dir("components", entry->dir);
	if (IS_ERR(entry->comp_dir)) {
		if (PTR_ERR(entry->comp_dir) != -ENODEV)
			pr_info("CM: Can't create proc/%d/components debugfs "
				"directory: %ld\n", entry->pid,
				PTR_ERR(entry->comp_dir));
		entry->comp_dir = NULL;
	}
	entry->domain_dir = debugfs_create_dir("domains", entry->dir);
	if (IS_ERR(entry->domain_dir)) {
		if (PTR_ERR(entry->domain_dir) != -ENODEV)
			pr_info("CM: Can't create proc/%d/domains debugfs "
				"directory: %ld\n", entry->pid,
				PTR_ERR(entry->domain_dir));
		entry->domain_dir = NULL;
	}
}

/* DSP meminfo */
static ssize_t meminfo_read(struct file *file, char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	const t_nmf_core_id id =
		*(t_nmf_core_id *)file->f_dentry->d_inode->i_private;
	char buf[640];
	int ret=0;
	t_cm_allocator_status status;
	t_cm_system_address addr;

	OSAL_LOCK_API();
	cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(id, ESRAM_CODE),
				 0, 0, &status);
	cm_DSP_GetDspBaseAddress(id, ESRAM_CODE,  &addr);
	ret = snprintf(buf, sizeof(buf),
		       "Memory    : Physical address  Logical address     Size "
		       "    Free     Used\n"
		       "-------------------------------------------------------"
		       "-------------------\n"
		       "ESRAM Code: %08x-%08lx %08x-%08lx\t%8lu %8lu %8lu\n",
		       (unsigned int)addr.physical,
		       addr.physical + status.global.size - 1,
		       (unsigned int)addr.logical,
		       addr.logical + status.global.size - 1,
		       status.global.size,
		       status.global.accumulate_free_memory,
		       status.global.accumulate_used_memory);

	cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(id, ESRAM_EXT24),
				 0, 0, &status);
	cm_DSP_GetDspBaseAddress(id, ESRAM_EXT24, &addr);
	ret += snprintf(&buf[ret], sizeof(buf)-ret,
			"ESRAM Data: %08x-%08lx %08x-%08lx\t%8lu %8lu %8lu\n",
			(unsigned int)addr.physical,
			addr.physical + status.global.size - 1,
			(unsigned int)addr.logical,
			addr.logical + status.global.size - 1,
			status.global.size,
			status.global.accumulate_free_memory,
			status.global.accumulate_used_memory);

	cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(id, SDRAM_CODE),
				 0, 0, &status);
	cm_DSP_GetDspBaseAddress(id, SDRAM_CODE,  &addr);
	ret += snprintf(&buf[ret], sizeof(buf)-ret,
			"SDRAM Code: %08x-%08lx %08x-%08lx\t%8lu %8lu %8lu\n",
			(unsigned int)addr.physical,
			addr.physical + status.global.size - 1,
			(unsigned int)addr.logical,
			addr.logical + status.global.size - 1,
			status.global.size,
			status.global.accumulate_free_memory,
			status.global.accumulate_used_memory);

	cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(id, SDRAM_EXT24),
				 0, 0, &status);
	cm_DSP_GetDspBaseAddress(id, SDRAM_EXT24, &addr);
	ret += snprintf(&buf[ret], sizeof(buf)-ret,
			"SDRAM Data: %08x-%08lx %08x-%08lx\t%8lu %8lu %8lu\n",
			(unsigned int)addr.physical,
			addr.physical + status.global.size - 1,
			(unsigned int)addr.logical,
			addr.logical + status.global.size - 1,
			status.global.size,
			status.global.accumulate_free_memory,
			status.global.accumulate_used_memory);

	cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(id, INTERNAL_XRAM24),
				 0, 0, &status);
	cm_DSP_GetDspBaseAddress(id, INTERNAL_XRAM24, &addr);
	ret += snprintf(&buf[ret], sizeof(buf)-ret,
			"TCM XRAM  : %08x-%08lx %08x-%08lx\t%8lu %8lu %8lu\n",
			(unsigned int)addr.physical,
			addr.physical + status.global.size - 1,
			(unsigned int)addr.logical,
			addr.logical + status.global.size - 1,
			status.global.size,
			status.global.accumulate_free_memory,
			status.global.accumulate_used_memory);

	cm_MM_GetAllocatorStatus(cm_DSP_GetAllocator(id, INTERNAL_YRAM24),
				 0, 0, &status);
	cm_DSP_GetDspBaseAddress(id, INTERNAL_YRAM24, &addr);
	ret += snprintf(&buf[ret], sizeof(buf)-ret,
			"TCM YRAM  : %08x-%08lx %08x-%08lx\t%8lu %8lu %8lu\n",
			(unsigned int)addr.physical,
			addr.physical + status.global.size - 1,
			(unsigned int)addr.logical,
			addr.logical + status.global.size - 1,
			status.global.size,
			status.global.accumulate_free_memory,
			status.global.accumulate_used_memory);

	OSAL_UNLOCK_API();
	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);;
}

static const struct file_operations mem_fops = {
	.read = meminfo_read,
};

/* ESRAM file operations */
static int esram_open(struct inode *inode, struct file *file)
{
	int i, err=0;
	for (i=0; i<NB_ESRAM; i++) {
		if (regulator_enable(osalEnv.esram_regulator[i]) < 0) {
			pr_err("CM (%s): can't enable regulator"
			       "for esram bank %s\n", __func__,
			       i ? "34" : "12");
			err = -EIO;
			break;
		}
	}

	if (err) {
		for (i--; i>=0; i--)
			regulator_disable(osalEnv.esram_regulator[i]);
	}

	return err;
}

static int esram_release(struct inode *inode, struct file *file)
{
	int i;
	for (i=0; i<NB_ESRAM; i++)
		regulator_disable(osalEnv.esram_regulator[i]);
	return 0;
}

static ssize_t esram_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(user_buf, count, ppos,
				       osalEnv.esram_base,
				       cfgESRAMSize*ONE_KB);
}

static const struct file_operations esram_fops = {
	.read    =	esram_read,
	.open    =	esram_open,
	.release =	esram_release,
};


/* TCM file */
void cm_debug_create_tcm_file(unsigned mpc_index)
{
	osalEnv.mpc[mpc_index].tcm_file = debugfs_create_blob(
		"tcm24", S_IRUSR|S_IRGRP|S_IROTH,
		osalEnv.mpc[mpc_index].snapshot_dir,
		&osalEnv.mpc[mpc_index].base);
	if (IS_ERR(osalEnv.mpc[mpc_index].tcm_file)) {
		if (PTR_ERR(osalEnv.mpc[mpc_index].tcm_file) != -ENODEV)
			pr_info("CM: Can't create dsp/%s/tcm24 debugfs "
				"directory: %ld\n", osalEnv.mpc[mpc_index].name,
				PTR_ERR(osalEnv.mpc[mpc_index].tcm_file));
		osalEnv.mpc[mpc_index].tcm_file = NULL;
	}
}

void cm_debug_destroy_tcm_file(unsigned mpc_index)
{
	debugfs_remove(osalEnv.mpc[mpc_index].tcm_file);
}

/* Global init */
void cm_debug_init(void)
{
	int i;

	cm_dir = debugfs_create_dir(DEBUGFS_ROOT, NULL);
	if (IS_ERR(cm_dir)) {
		if (PTR_ERR(cm_dir) != -ENODEV)
			pr_info("CM: Can't create root debugfs directory: "
				"%ld\n", PTR_ERR(cm_dir));
		cm_dir = NULL;
		return;
	}

	proc_dir = debugfs_create_dir("proc", cm_dir);
	if (IS_ERR(proc_dir)) {
		if (PTR_ERR(proc_dir) != -ENODEV)
			pr_info("CM: Can't create 'proc' debugfs directory: "
				"%ld\n", PTR_ERR(proc_dir));
		proc_dir = NULL;
	}

	core_dir = debugfs_create_dir("dsp", cm_dir);
	if (IS_ERR(core_dir)) {
		if (PTR_ERR(core_dir) != -ENODEV)
			pr_info("CM: Can't create 'dsp' debugfs directory: %ld\n",
				PTR_ERR(core_dir));
		core_dir = NULL;
	}

	domain_dir = debugfs_create_dir("domains", cm_dir);
	if (IS_ERR(domain_dir)) {
		if (PTR_ERR(domain_dir) != -ENODEV)
			pr_info("CM: Can't create 'domains' debugfs directory: "
				"%ld\n",
				PTR_ERR(domain_dir));
		domain_dir = NULL;
	} else {
		osal_debug_ops.domain_create  = cm_debug_domain_create;
		osal_debug_ops.domain_destroy = cm_debug_domain_destroy;
	}

	for (i=0; i<NB_MPC; i++) {
		osalEnv.mpc[i].dir = debugfs_create_dir(osalEnv.mpc[i].name,
							core_dir);
		if (IS_ERR(osalEnv.mpc[i].dir)) {
			if (PTR_ERR(osalEnv.mpc[i].dir) != -ENODEV)
				pr_info("CM: Can't create %s debugfs directory: "
					"%ld\n",
					osalEnv.mpc[i].name,
					PTR_ERR(osalEnv.mpc[i].dir));
			osalEnv.mpc[i].dir = NULL;
		} else {
			osalEnv.mpc[i].mem_file =
				debugfs_create_file("meminfo", S_IRUSR|S_IRGRP,
						    osalEnv.mpc[i].dir,
						    (void*)&osalEnv.mpc[i].coreId,
						    &mem_fops);
			if (IS_ERR(osalEnv.mpc[i].mem_file)) {
				if (PTR_ERR(osalEnv.mpc[i].mem_file) != -ENODEV)
					pr_err("CM: Can't create dsp/%s/meminfo "
					       "debugfs file: %ld\n",
					       osalEnv.mpc[i].name,
					       PTR_ERR(osalEnv.mpc[i].mem_file));
				osalEnv.mpc[i].mem_file = NULL;
			}

			osalEnv.mpc[i].comp_dir = debugfs_create_dir(
				"components",
				osalEnv.mpc[i].dir);
			if (IS_ERR(osalEnv.mpc[i].comp_dir)) {
				if (PTR_ERR(osalEnv.mpc[i].comp_dir) != -ENODEV)
					pr_info("CM: Can't create "
						"'dsp/%s/components' debugfs "
						"directory: %ld\n",
						osalEnv.mpc[i].name,
						PTR_ERR(osalEnv.mpc[i].comp_dir));
				osalEnv.mpc[i].comp_dir = NULL;
			}

			osalEnv.mpc[i].domain_dir =
				debugfs_create_dir("domains",
						   osalEnv.mpc[i].dir);
			if (IS_ERR(osalEnv.mpc[i].domain_dir)) {
				if (PTR_ERR(osalEnv.mpc[i].domain_dir) != -ENODEV)
					pr_info("CM: Can't create "
						"'dsp/%s/domains' "
						"debugfs directory: %ld\n",
						osalEnv.mpc[i].name,
						PTR_ERR(osalEnv.mpc[i].domain_dir));
				osalEnv.mpc[i].domain_dir = NULL;
			}

			osalEnv.mpc[i].snapshot_dir = debugfs_create_dir(
				"snapshot",
				osalEnv.mpc[i].dir);
			if (IS_ERR(osalEnv.mpc[i].snapshot_dir)) {
				if (PTR_ERR(osalEnv.mpc[i].snapshot_dir) != -ENODEV)
					pr_info("CM: Can't create "
						"'dsp/%s/snapshot' debugfs "
						"directory: %ld\n",
						osalEnv.mpc[i].name,
						PTR_ERR(osalEnv.mpc[i].snapshot_dir));
				osalEnv.mpc[i].snapshot_dir = NULL;
			} else {
				debugfs_create_file("esram", S_IRUSR|S_IRGRP|S_IROTH,
						    osalEnv.mpc[i].snapshot_dir,
						    &osalEnv.esram_base,
						    &esram_fops);
				debugfs_create_blob("sdram_data", S_IRUSR|S_IRGRP|S_IROTH,
						    osalEnv.mpc[i].snapshot_dir,
						    &osalEnv.mpc[i].sdram_data);
				debugfs_create_blob("sdram_code", S_IRUSR|S_IRGRP|S_IROTH,
						    osalEnv.mpc[i].snapshot_dir,
						    &osalEnv.mpc[i].sdram_code);
			}

			debugfs_create_bool("running", S_IRUSR|S_IRGRP,
					    osalEnv.mpc[i].dir,
					    (u32 *)&osalEnv.mpc[i].monitor_tsk);
			debugfs_create_u8("load", S_IRUSR|S_IRGRP,
					  osalEnv.mpc[i].dir,
					  &osalEnv.mpc[i].load);
			debugfs_create_u8("requested_opp", S_IRUSR|S_IRGRP,
					  osalEnv.mpc[i].dir,
					  &osalEnv.mpc[i].opp_request);
		}
	}
	osal_debug_ops.component_create  = cm_debug_component_create;
	osal_debug_ops.component_destroy = cm_debug_component_destroy;
}

void cm_debug_exit(void)
{
	debugfs_remove_recursive(cm_dir);
}

#endif /* CONFIG_DEBUG_FS */
