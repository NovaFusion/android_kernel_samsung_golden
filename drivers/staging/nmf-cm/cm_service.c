/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

/** \file cm_service.c
 *
 * Nomadik Multiprocessing Framework Linux Driver
 *
 */

#include <linux/module.h>
#include <linux/plist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>

#include <cm/engine/api/control/irq_engine.h>

#include "osal-kernel.h"
#include "cmld.h"
#include "cm_service.h"
#include "cm_dma.h"

/* Panic managment */
static void service_tasklet_func(unsigned long);
unsigned long service_tasklet_data = 0;
DECLARE_TASKLET(cmld_service_tasklet, service_tasklet_func, 0);

void dispatch_service_msg(struct osal_msg *msg)
{
	struct list_head *head, *next;
#ifdef CONFIG_DEBUG_FS
	bool dump_flag_to_set = true;
#endif
	/*
	 * Note: no lock needed to protect the channel_list against list
	 * changes, as the current tasklet is disabled each time we modify
	 * the list
	 */
	list_for_each_safe(head, next, &channel_list) {
		struct cm_channel_priv *channelPriv = list_entry(head, struct cm_channel_priv, entry);
		struct osal_msg *new_msg;
		size_t msg_size;

		if (channelPriv->state == CHANNEL_CLOSED)
			continue;
		msg_size = sizeof(new_msg->hdr) + sizeof(new_msg->d.srv);
		new_msg = kmalloc(msg_size, GFP_ATOMIC);
		if (new_msg == NULL) {
			pr_err("[CM] %s: can't allocate memory, service"
			       " message not dispatched !!\n", __func__);
			continue;
		}
		memcpy(new_msg, msg, msg_size);
		plist_node_init(&new_msg->msg_entry, 0);
#ifdef CONFIG_DEBUG_FS
		if (cmld_user_has_debugfs && dump_flag_to_set
		    && (new_msg->d.srv.srvType == NMF_SERVICE_PANIC)) {
			/*
			 * The reciever of this message will do the DSP
			 * memory dump
			 */
			new_msg->d.srv.srvData.panic.panicSource
				|= DEBUGFS_DUMP_FLAG;
			dump_flag_to_set = false;
			cmld_dump_ongoing = channelPriv->proc->pid;
		}
#endif
		spin_lock_bh(&channelPriv->bh_lock);
		plist_add(&new_msg->msg_entry, &channelPriv->messageQueue);
		spin_unlock_bh(&channelPriv->bh_lock);
		wake_up(&channelPriv->waitq);
	}
}

static void service_tasklet_func(unsigned long unused)
{
	t_cm_service_type type;
	t_cm_service_description desc;
	int i=0;

	do {
		if (test_and_clear_bit(i, &service_tasklet_data)) { 
			CM_getServiceDescription(osalEnv.mpc[i].coreId, &type, &desc);

			switch (type) {
			case CM_MPC_SERVICE_PANIC: {
				struct osal_msg msg;

				msg.msg_type = MSG_SERVICE;
				msg.d.srv.srvType = NMF_SERVICE_PANIC;
				msg.d.srv.srvData.panic = desc.u.panic;

				dispatch_service_msg(&msg);
				/*
				 * Stop DMA directly before shutdown, to avoid
				 * bad sound. Should be called after DSP has
				 * stopped executing, to avoid the DSP
				 * re-starting DMA
				 */
				if (osalEnv.mpc[i].coreId == SIA_CORE_ID)
					cmdma_stop_dma();

				/*
				 * wake up all trace readers to let them
				 * retrieve last traces
				 */
				wake_up_all(&osalEnv.mpc[i].trace_waitq);
				break;
			}
			case CM_MPC_SERVICE_PRINT: {
				char msg[256];
				if (CM_ReadMPCString(osalEnv.mpc[i].coreId,
						     desc.u.print.dspAddress, msg,
						     sizeof(msg)) == CM_OK)
					printk(msg, desc.u.print.value1,
					       desc.u.print.value2);
				break;
			}
			case CM_MPC_SERVICE_TRACE:
				wake_up_all(&osalEnv.mpc[i].trace_waitq);
				break;
			default:
				pr_err("[CM] %s: MPC Service Type %d not supported\n", __func__, type);
			}
			enable_irq(osalEnv.mpc[i].interrupt1);
		}
		i = (i+1) % NB_MPC;
	} while (service_tasklet_data != 0);
}
