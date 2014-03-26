/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghavi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __SHRM_DRIVER_H__
#define __SHRM_DRIVER_H__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/modem/modem_client.h>
#include <linux/modem/shrm/shrm.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/wakelock.h>

#define ISA_DEVICES 10

#define BOOT_INIT  (0)
#define BOOT_INFO_SYNC  (1)
#define BOOT_DONE  (2)
#define BOOT_UNKNOWN (3)

/**
 * struct shrm_dev - shrm device information
 * @ca_wake_irq:		CMT wake interrupt number
 * @ac_read_notif_0_irq:	ape-cmt common channel read notify interrupt
 * @ac_read_notif_1_irq:	ape-cmt audio channel read notify interrupt
 * @ca_msg_pending_notif_0_irq:	cmt-ape common channel msg pending interrupt
 * @ca_msg_pending_notif_1_irq:	cmt-ape audio channel msg pending interrupt
 * @intr_base:			interrupt base register address
 * @ape_common_fifo_base:	ape side common channel fifo base addr
 * @ape_audio_fifo_base:	ape side audio channel fifo base addr
 * @cmt_common_fifo_base:	cmt side common channel fifo base addr
 * @cmt_audio_fifo_base:	cmt side audio channel fifo base addr
 * @ape_common_fifo_base_phy:	physical addr of ape common fifo
 * @ape_audio_fifo_base_phy:	physical addr of ape audio fifo
 * @cmt_common_fifo_base_phy:	physical addr of cmt common fifo
 * @cmt_audio_fifo_base_phy:	physical addr of cmt audio fifo
 * @ape_common_fifo_size:	ape side common channel fifo size
 * @ape_audio_fifo_size:	ape side audio channel fifo size
 * @cmt_common_fifo_size:	cmt side common channel fifo size
 * @cmt_audio_fifo_size:	cmt side audio channel fifo size
 * @netdev_flag_up:		flag to indicate up/down of netwok device
 * @msr_flag:			flag to check on-going MSR sequence
 * @ac_common_shared_wptr:	ape-cmt common channel write pointer
 * @ac_common_shared_rptr:	ape-cmt common channel read pointer
 * @ca_common_shared_wptr:	cmt-ape common channel write pointer
 * @ca_common_shared_rptr:	cmt-ape common channel read pointer
 * @ac_audio_shared_wptr:	ape-cmt audio channel write pointer
 * @ac_audio_shared_rptr:	ape-cmt audio channel read pointer
 * @ca_audio_shared_wptr:	cmt-ape audio channel write pointer
 * @ca_audio_shared_rptr:	cmt-ape audio channel read pointer
 * @ca_reset_status_rptr:	cmt-ape modem reset status pointer
 * @dev:			pointer to the driver device
 * @ndev:			pointer to the network device structure
 * @modem:			poiner to struct modem
 * @isa_context:		pointer to t_isa_driver_sontext dtructure
 * @shm_common_ch_wr_kw:	kthread worker for writing to common channel
 * @shm_common_ch_wr_kw_task:	task for writing to common channel
 * @shm_audio_ch_wr_kw:		kthread worker for writing to audio channel
 * @shm_audio_ch_wr_kw_task:	task for writing to audio channel
 * @shm_ac_wake_kw:		kthread worker for receiving ape-cmt wake requests
 * @shm_ac_wake_kw_task:	task for receiving ape-cmt wake requests
 * @shm_ca_wake_kw:		kthread worker for receiving cmt-ape wake requests
 * @shm_ca_wake_kw_task:	task for receiving cmt-ape wake requests
 * @shm_ac_sleep_kw:		kthread worker for recieving ape-cmt sleep requests
 * @shm_ac_sleep_kw_task:	task for recieving ape-cmt sleep requests
 * @shm_mod_stuck_kw:		kthread worker to reset the modem
 * @shm_mod_stuck_kw_task:	task for sending modem reset request
 * @send_ac_msg_pend_notify_0:	work for handling pending message on common
 * channel
 * @send_ac_msg_pend_notify_1:	work for handling pending message on audio
 * channel
 * @shm_ac_wake_req:		work to send ape-cmt wake request
 * @shm_ca_wake_req:		work to send cmt-ape wake request
 * @shm_ca_sleep_req:		work to send cmt-ape sleep request
 * @shm_ac_sleep_req:		work to send ape-cmt sleep request
 * @shm_mod_reset_process:	work to process a modem reset request
 * @shm_mod_reset_req:		work to send a reset request to modem
 * @shm_print_dbg_info:		work function to print all prcmu/abb registers
 */
struct shrm_dev {
	u8 ca_wake_irq;
	u8 ac_read_notif_0_irq;
	u8 ac_read_notif_1_irq;
	u8 ca_msg_pending_notif_0_irq;
	u8 ca_msg_pending_notif_1_irq;
	void __iomem *intr_base;
	void __iomem *ape_common_fifo_base;
	void __iomem *ape_audio_fifo_base;
	void __iomem *cmt_common_fifo_base;
	void __iomem *cmt_audio_fifo_base;

	u32 *ape_common_fifo_base_phy;
	u32 *ape_audio_fifo_base_phy;
	u32 *cmt_common_fifo_base_phy;
	u32 *cmt_audio_fifo_base_phy;

	int ape_common_fifo_size;
	int ape_audio_fifo_size;
	int cmt_common_fifo_size;
	int cmt_audio_fifo_size;
	int netdev_flag_up;
	int msr_flag;
	void (*msr_crash_cb)(void *);
	void (*msr_reinit_cb)(void *);
	void *msr_cookie;

	void __iomem *ac_common_shared_wptr;
	void __iomem *ac_common_shared_rptr;
	void __iomem *ca_common_shared_wptr;
	void __iomem *ca_common_shared_rptr;

	void __iomem *ac_audio_shared_wptr;
	void __iomem *ac_audio_shared_rptr;
	void __iomem *ca_audio_shared_wptr;
	void __iomem *ca_audio_shared_rptr;

	void __iomem *ca_reset_status_rptr;

	struct device *dev;
	struct net_device *ndev;
	struct modem *modem;
	struct isa_driver_context *isa_context;
	struct kthread_worker shm_common_ch_wr_kw;
	struct task_struct *shm_common_ch_wr_kw_task;
	struct kthread_worker shm_audio_ch_wr_kw;
	struct task_struct *shm_audio_ch_wr_kw_task;
	struct kthread_worker shm_ac_wake_kw;
	struct task_struct *shm_ac_wake_kw_task;
	struct kthread_worker shm_ca_wake_kw;
	struct task_struct *shm_ca_wake_kw_task;
	struct kthread_worker shm_ac_sleep_kw;
	struct task_struct *shm_ac_sleep_kw_task;
	struct kthread_worker shm_mod_stuck_kw;
	struct task_struct *shm_mod_stuck_kw_task;
	struct kthread_work send_ac_msg_pend_notify_0;
	struct kthread_work send_ac_msg_pend_notify_1;
	struct kthread_work shm_ac_wake_req;
	struct kthread_work shm_ca_wake_req;
	struct kthread_work shm_ca_sleep_req;
	struct kthread_work shm_ac_sleep_req;
	struct kthread_work shm_mod_reset_process;
	struct kthread_work shm_mod_reset_req;
	struct kthread_work shm_print_dbg_info;
	struct wake_lock rpc_wake_lock;
	struct wake_lock sec_wake_lock;
};

/**
 * struct queue_element - information to add an element to queue
 * @entry:	list entry
 * @offset:	message offset
 * @size:	message size
 * @no:		total number of messages
 */
struct queue_element {
	struct list_head entry;
	u32 offset;
	u32 size;
	u32 no;
};

/**
 * struct message_queue - ISI, RPC, AUDIO, SECURITY message queue information
 * @fifo_base:		pointer to the respective fifo base
 * @size:		size of the data to be read
 * @readptr:		fifo read pointer
 * @writeptr:		fifo write pointer
 * @no:			total number of messages
 * @update_lock:	spinlock for protecting the queue read operation
 * @q_rp:		queue write pointer
 * @wq_readable:	wait queue head
 * @msg_list:		message list
 * @shrm:		pointer to shrm device information structure
 */
struct message_queue {
      u8 *fifo_base;
      u32 size;
      u32 readptr;
      u32 writeptr;
      u32 no;
      spinlock_t update_lock;
      atomic_t q_rp;
      wait_queue_head_t wq_readable;
      struct list_head msg_list;
      struct shrm_dev *shrm;
};

/**
 * struct isadev_context - shrm char interface context
 * @dl_queue:	structre to store the queue related info
 * @device_id:	message id(ISI, RPC, AUDIO, SECURITY)
 * @addr:	device addresses.
 */
struct isadev_context {
	struct message_queue dl_queue;
	u8 device_id;
	void *addr;
};

/**
 * struct isa_driver_context - shrm char interface device information
 * @is_open:		flag to check the usage of queue
 * @isadev:		pointer to struct t_isadev_context
 * @common_tx:		spinlock for protecting common channel
 * @tx_audio_mutex:	mutex for protecting audio channel
 * @cdev:		character device structre
 * @shm_class:		pointer to the class structure
 */
struct isa_driver_context {
	atomic_t is_open[ISA_DEVICES];
	struct isadev_context *isadev;
	spinlock_t common_tx;
	struct mutex tx_audio_mutex;
	struct cdev cdev;
	struct class *shm_class;
};

#endif
