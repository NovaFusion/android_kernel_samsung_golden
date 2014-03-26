/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Biju Das <biju.das@stericsson.com> for ST-Ericsson
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com> for ST-Ericsson
 * Author: Arun Murthy <arun.murthy@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/netlink.h>
#include <linux/kthread.h>
#include <linux/modem/shrm/shrm.h>
#include <linux/modem/shrm/shrm_driver.h>
#include <linux/modem/shrm/shrm_private.h>
#include <linux/modem/shrm/shrm_net.h>
#include <linux/modem/modem_client.h>
#include <linux/modem/u8500_client.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/mfd/abx500.h>
#include <mach/reboot_reasons.h>
#include <mach/suspend.h>
#include <mach/prcmu-debug.h>
#ifdef CONFIG_U8500_SHRM_ENABLE_FEATURE_SIGNATURE_VERIFICATION
#include <mach/open_modem_shared_memory.h>
#endif

#define L2_HEADER_ISI		0x0
#define L2_HEADER_RPC		0x1
#define L2_HEADER_AUDIO		0x2
#define L2_HEADER_SECURITY	0x3
#define L2_HEADER_COMMON_SIMPLE_LOOPBACK	0xC0
#define L2_HEADER_COMMON_ADVANCED_LOOPBACK	0xC1
#define L2_HEADER_AUDIO_SIMPLE_LOOPBACK		0x80
#define L2_HEADER_AUDIO_ADVANCED_LOOPBACK	0x81
#define L2_HEADER_CIQ		0xC3
#define L2_HEADER_RTC_CALIBRATION		0xC8
#define L2_HEADER_IPCCTRL 0xDC
#define L2_HEADER_IPCDATA 0xDD
#define L2_HEADER_SYSCLK3	0xE6
#define MAX_PAYLOAD 1024
#define MOD_STUCK_TIMEOUT	6
#define FIFO_FULL_TIMEOUT	1
#define PRCM_MOD_AWAKE_STATUS_PRCM_MOD_COREPD_AWAKE	BIT(0)
#define PRCM_MOD_AWAKE_STATUS_PRCM_MOD_AAPD_AWAKE	BIT(1)
#define PRCM_MOD_AWAKE_STATUS_PRCM_MOD_VMODEM_OFF_ISO	BIT(2)
#define PRCM_MOD_PURESET	BIT(0)
#define PRCM_MOD_SW_RESET	BIT(1)

#define PRCM_HOSTACCESS_REQ	0x334
#define PRCM_MOD_AWAKE_STATUS	0x4A0
#define PRCM_MOD_RESETN_VAL	0x204

extern void log_this(u8 pc, char* a, u32 extra1, char* b, u32 extra2);

static u8 boot_state = BOOT_INIT;
static u8 recieve_common_msg[8*1024];
static u8 recieve_audio_msg[8*1024];
static received_msg_handler rx_common_handler;
static received_msg_handler rx_audio_handler;
static struct hrtimer timer;
static struct hrtimer mod_stuck_timer_0;
static struct hrtimer mod_stuck_timer_1;
static struct hrtimer fifo_full_timer;
struct sock *shrm_nl_sk;

static char shrm_common_tx_state = SHRM_SLEEP_STATE;
static char shrm_common_rx_state = SHRM_SLEEP_STATE;
static char shrm_audio_tx_state = SHRM_SLEEP_STATE;
static char shrm_audio_rx_state = SHRM_SLEEP_STATE;

static atomic_t ac_sleep_disable_count = ATOMIC_INIT(0);
static atomic_t ac_msg_pend_1 = ATOMIC_INIT(0);
static atomic_t mod_stuck = ATOMIC_INIT(0);
static atomic_t fifo_full = ATOMIC_INIT(0);
static struct shrm_dev *shm_dev;

/* Spin lock and tasklet declaration */
DECLARE_TASKLET(shm_ca_0_tasklet, shm_ca_msgpending_0_tasklet, 0);
DECLARE_TASKLET(shm_ca_1_tasklet, shm_ca_msgpending_1_tasklet, 0);
DECLARE_TASKLET(shm_ac_read_0_tasklet, shm_ac_read_notif_0_tasklet, 0);
DECLARE_TASKLET(shm_ac_read_1_tasklet, shm_ac_read_notif_1_tasklet, 0);

static DEFINE_MUTEX(ac_state_mutex);

static DEFINE_SPINLOCK(ca_common_lock);
static DEFINE_SPINLOCK(ca_audio_lock);
static DEFINE_SPINLOCK(ca_wake_req_lock);
static DEFINE_SPINLOCK(boot_lock);
static DEFINE_SPINLOCK(mod_stuck_lock);
static DEFINE_SPINLOCK(start_timer_lock);

enum shrm_nl {
	SHRM_NL_MOD_RESET = 1,
	SHRM_NL_MOD_QUERY_STATE,
	SHRM_NL_USER_MOD_RESET,
	SHRM_NL_STATUS_MOD_ONLINE,
	SHRM_NL_STATUS_MOD_OFFLINE,
};

static int check_modem_in_reset(void);

void shm_print_dbg_info_work(struct kthread_work *work)
{
	abx500_dump_all_banks();
	prcmu_debug_dump_regs();
	prcmu_debug_dump_data_mem();
}

void shm_mod_reset_req_work(struct kthread_work *work)
{
	unsigned long flags;

	/* update the boot_state */
	spin_lock_irqsave(&boot_lock, flags);
	if (boot_state != BOOT_DONE) {
		dev_info(shm_dev->dev, "Modem in reset state\n");
		spin_unlock_irqrestore(&boot_lock, flags);
		atomic_set(&mod_stuck, 0);
		return;
	}
	boot_state = BOOT_UNKNOWN;
	wmb();
	spin_unlock_irqrestore(&boot_lock, flags);
	dev_err(shm_dev->dev, "APE makes modem reset\n");
	prcmu_modem_reset();
}

static void shm_ac_sleep_req_work(struct kthread_work *work)
{
	if (boot_state == BOOT_DONE) {
		mutex_lock(&ac_state_mutex);
		if (atomic_read(&ac_sleep_disable_count) == 0)
			modem_release(shm_dev->modem);
		mutex_unlock(&ac_state_mutex);
	}
}

static void shm_ac_wake_req_work(struct kthread_work *work)
{
	mutex_lock(&ac_state_mutex);
	modem_request(shm_dev->modem);
	mutex_unlock(&ac_state_mutex);
}

static u32 get_host_accessport_val(void)
{
	u32 prcm_hostaccess;
	u32 status;
	u32 reset_stats;

	status = (prcmu_read(PRCM_MOD_AWAKE_STATUS) & 0x03);
	reset_stats = (prcmu_read(PRCM_MOD_RESETN_VAL) & 0x03);
	prcm_hostaccess = prcmu_read(PRCM_HOSTACCESS_REQ);
	wmb();
	prcm_hostaccess = ((prcm_hostaccess & 0x01) &&
		(status == (PRCM_MOD_AWAKE_STATUS_PRCM_MOD_AAPD_AWAKE |
			    PRCM_MOD_AWAKE_STATUS_PRCM_MOD_COREPD_AWAKE)) &&
		(reset_stats == (PRCM_MOD_SW_RESET | PRCM_MOD_PURESET)));

	return prcm_hostaccess;
}

static enum hrtimer_restart shm_fifo_full_timeout(struct hrtimer *timer)
{
	queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_mod_reset_req);
	queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_print_dbg_info);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart shm_mod_stuck_timeout(struct hrtimer *timer)
{
	unsigned long flags;

	spin_lock_irqsave(&mod_stuck_lock, flags);
	/* Check MSR is already in progress */
	if (shm_dev->msr_flag || boot_state == BOOT_UNKNOWN ||
			atomic_read(&mod_stuck) || atomic_read(&fifo_full)) {
		spin_unlock_irqrestore(&mod_stuck_lock, flags);
		return HRTIMER_NORESTART;
	}
	atomic_set(&mod_stuck, 1);
	spin_unlock_irqrestore(&mod_stuck_lock, flags);
	dev_err(shm_dev->dev, "No response from modem, timeout %dsec\n",
			MOD_STUCK_TIMEOUT);
	dev_err(shm_dev->dev, "APE initiating MSR\n");
	queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_mod_reset_req);
	queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_print_dbg_info);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart callback(struct hrtimer *timer)
{
	unsigned long flags;

	spin_lock_irqsave(&ca_wake_req_lock, flags);
	if (((shrm_common_rx_state == SHRM_IDLE) ||
				(shrm_common_rx_state == SHRM_SLEEP_STATE))
			&& ((shrm_common_tx_state == SHRM_IDLE) ||
				(shrm_common_tx_state == SHRM_SLEEP_STATE))
			&& ((shrm_audio_rx_state == SHRM_IDLE)  ||
				(shrm_audio_rx_state == SHRM_SLEEP_STATE))
			&& ((shrm_audio_tx_state == SHRM_IDLE)  ||
				(shrm_audio_tx_state == SHRM_SLEEP_STATE))) {

		shrm_common_rx_state = SHRM_SLEEP_STATE;
		shrm_audio_rx_state = SHRM_SLEEP_STATE;
		shrm_common_tx_state = SHRM_SLEEP_STATE;
		shrm_audio_tx_state = SHRM_SLEEP_STATE;

		queue_kthread_work(&shm_dev->shm_ac_sleep_kw,
				&shm_dev->shm_ac_sleep_req);

	}
	spin_unlock_irqrestore(&ca_wake_req_lock, flags);

	return HRTIMER_NORESTART;
}

int nl_send_multicast_message(int msg, gfp_t gfp_mask)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	int err;

	/* prepare netlink message */
	skb = alloc_skb(NLMSG_SPACE(MAX_PAYLOAD), gfp_mask);
	if (!skb) {
		dev_err(shm_dev->dev, "%s:alloc_skb failed\n", __func__);
		err = -ENOMEM;
		goto out;
	}

	nlh = (struct nlmsghdr *)skb->data;
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	dev_dbg(shm_dev->dev, "nlh->nlmsg_len = %d\n", nlh->nlmsg_len);

	nlh->nlmsg_pid = 0;  /* from kernel */
	nlh->nlmsg_flags = 0;
	*(int *)NLMSG_DATA(nlh) = msg;
	skb_put(skb, MAX_PAYLOAD);
	/* sender is in group 1<<0 */
	NETLINK_CB(skb).pid = 0;  /* from kernel */
	/* to mcast group 1<<0 */
	NETLINK_CB(skb).dst_group = 1;

	/*multicast the message to all listening processes*/
	err = netlink_broadcast(shrm_nl_sk, skb, 0, 1, gfp_mask);
	dev_dbg(shm_dev->dev, "ret val from nl-multicast = %d\n", err);

out:
	return err;
}

static void nl_send_unicast_message(int dst_pid)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	int err;
	int bt_state;
	unsigned long flags;

	dev_dbg(shm_dev->dev, "Sending unicast message\n");

	/* prepare the NL message for unicast */
	skb = alloc_skb(NLMSG_SPACE(MAX_PAYLOAD), GFP_KERNEL);
	if (!skb) {
		dev_err(shm_dev->dev, "%s:alloc_skb failed\n", __func__);
		return;
	}

	nlh = (struct nlmsghdr *)skb->data;
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	dev_dbg(shm_dev->dev, "nlh->nlmsg_len = %d\n", nlh->nlmsg_len);

	nlh->nlmsg_pid = 0;  /* from kernel */
	nlh->nlmsg_flags = 0;

	spin_lock_irqsave(&boot_lock, flags);
	bt_state = boot_state;
	spin_unlock_irqrestore(&boot_lock, flags);

	if (bt_state == BOOT_DONE)
		*(int *)NLMSG_DATA(nlh) = SHRM_NL_STATUS_MOD_ONLINE;
	else
		*(int *)NLMSG_DATA(nlh) = SHRM_NL_STATUS_MOD_OFFLINE;

	skb_put(skb, MAX_PAYLOAD);
	/* sender is in group 1<<0 */
	NETLINK_CB(skb).pid = 0;  /* from kernel */
	NETLINK_CB(skb).dst_group = 0;

	/*unicast the message to the querying processes*/
	err = netlink_unicast(shrm_nl_sk, skb, dst_pid, MSG_DONTWAIT);
	dev_dbg(shm_dev->dev, "ret val from nl-unicast = %d\n", err);
}

bool shrm_is_modem_online(void)
{
	return boot_state == BOOT_DONE;
}

static int check_modem_in_reset(void)
{
	u8 bt_state;
	unsigned long flags;

	spin_lock_irqsave(&boot_lock, flags);
	bt_state = boot_state;
	spin_unlock_irqrestore(&boot_lock, flags);

#ifdef CONFIG_U8500_SHRM_MODEM_SILENT_RESET
	if (bt_state != BOOT_UNKNOWN &&
			(!readl(shm_dev->ca_reset_status_rptr)))
		return 0;
	else
		return -ENODEV;
#else
	/*
	 * this check won't be applicable and won't work correctly
	 * if modem-silent-feature is not enabled
	 * so, simply return 0
	 */
	return 0;
#endif
}

void shm_ca_msgpending_0_tasklet(unsigned long tasklet_data)
{
	struct shrm_dev *shrm = (struct shrm_dev *)tasklet_data;
	u32 reader_local_rptr;
	u32 reader_local_wptr;
	u32 shared_rptr;
	u32 config = 0, version = 0;
	unsigned long flags;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	/* Interprocess locking */
	spin_lock(&ca_common_lock);

	/* Update_reader_local_wptr with shared_wptr */
	update_ca_common_local_wptr(shrm);
	get_reader_pointers(COMMON_CHANNEL, &reader_local_rptr,
				&reader_local_wptr, &shared_rptr);

	set_ca_msg_0_read_notif_send(0);

	if (boot_state == BOOT_DONE) {
		shrm_common_rx_state = SHRM_PTR_FREE;

		if (reader_local_rptr != shared_rptr)
			ca_msg_read_notification_0(shrm);
		if (reader_local_rptr != reader_local_wptr)
			receive_messages_common(shrm);
		get_reader_pointers(COMMON_CHANNEL, &reader_local_rptr,
				&reader_local_wptr, &shared_rptr);
		if (reader_local_rptr == reader_local_wptr)
			shrm_common_rx_state = SHRM_IDLE;
	} else {
		/* BOOT phase.only a BOOT_RESP should be in FIFO */
		if (boot_state != BOOT_INFO_SYNC) {
			if (!read_boot_info_req(shrm, &config, &version)) {
				dev_err(shrm->dev,
						"Unable to read boot state\n");
				return;
			}
			/* SendReadNotification */
			ca_msg_read_notification_0(shrm);
			/*
			 * Check the version number before
			 * sending Boot info response
			 */

			/* send MsgPending notification */
			write_boot_info_resp(shrm, config, version);
			spin_lock_irqsave(&boot_lock, flags);
			boot_state = BOOT_INFO_SYNC;
			spin_unlock_irqrestore(&boot_lock, flags);
			dev_info(shrm->dev, "BOOT_INFO_SYNC\n");
			queue_kthread_work(&shrm->shm_common_ch_wr_kw,
					&shrm->send_ac_msg_pend_notify_0);
		} else {
			ca_msg_read_notification_0(shrm);
			dev_info(shrm->dev,
				"BOOT_INFO_SYNC\n");
		}
	}
	/* Interprocess locking */
	spin_unlock(&ca_common_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

void shm_ca_msgpending_1_tasklet(unsigned long tasklet_data)
{
	struct shrm_dev *shrm = (struct shrm_dev *)tasklet_data;
	u32 reader_local_rptr;
	u32 reader_local_wptr;
	u32 shared_rptr;

	/*
	 * This function is called when CaMsgPendingNotification Trigerred
	 * by CMU. It means that CMU has wrote a message into Ca Audio FIFO
	 */

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown\n",
				__func__);
		return;
	}

	/* Interprocess locking */
	spin_lock(&ca_audio_lock);

	/* Update_reader_local_wptr(with shared_wptr) */
	update_ca_audio_local_wptr(shrm);
	get_reader_pointers(AUDIO_CHANNEL, &reader_local_rptr,
					&reader_local_wptr, &shared_rptr);

	set_ca_msg_1_read_notif_send(0);

	if (boot_state != BOOT_DONE) {
		dev_err(shrm->dev, "Boot Error\n");
		return;
	}
	shrm_audio_rx_state = SHRM_PTR_FREE;
	/* Check we already read the message */
	if (reader_local_rptr != shared_rptr)
		ca_msg_read_notification_1(shrm);
	if (reader_local_rptr != reader_local_wptr)
		receive_messages_audio(shrm);

	get_reader_pointers(AUDIO_CHANNEL, &reader_local_rptr,
			&reader_local_wptr, &shared_rptr);
	if (reader_local_rptr == reader_local_wptr)
		shrm_audio_rx_state = SHRM_IDLE;

	 /* Interprocess locking */
	spin_unlock(&ca_audio_lock);
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

void shm_ac_read_notif_0_tasklet(unsigned long tasklet_data)
{
	struct shrm_dev *shrm = (struct shrm_dev *)tasklet_data;
	u32 writer_local_rptr;
	u32 writer_local_wptr;
	u32 shared_wptr;
	unsigned long flags;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	/* Update writer_local_rptrwith shared_rptr */
	update_ac_common_local_rptr(shrm);
	get_writer_pointers(COMMON_CHANNEL, &writer_local_rptr,
				&writer_local_wptr, &shared_wptr);

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown\n",
				__func__);
		return;
	}

	if (boot_state == BOOT_INFO_SYNC) {
		/* BOOT_RESP sent by APE has been received by CMT */
		spin_lock_irqsave(&boot_lock, flags);
		boot_state = BOOT_DONE;
		spin_unlock_irqrestore(&boot_lock, flags);
		dev_info(shrm->dev, "IPC_ISA BOOT_DONE\n");

		if (shrm->msr_flag) {
#ifdef CONFIG_U8500_SHRM_DEFAULT_NET
			shrm_start_netdev(shrm->ndev);
#endif
			/* To prevent kernel panic during MSR
			if (shrm->msr_reinit_cb)
				shrm->msr_reinit_cb(shrm->msr_cookie);
			*/

			shrm->msr_flag = 0;
		}
		/* multicast that modem is online */
		nl_send_multicast_message(SHRM_NL_STATUS_MOD_ONLINE,
				GFP_ATOMIC);
	} else if (boot_state == BOOT_DONE) {
		if (writer_local_rptr != writer_local_wptr) {
			shrm_common_tx_state = SHRM_PTR_FREE;
			queue_kthread_work(&shrm->shm_common_ch_wr_kw,
					&shrm->send_ac_msg_pend_notify_0);
		} else {
			shrm_common_tx_state = SHRM_IDLE;
#ifdef CONFIG_U8500_SHRM_DEFAULT_NET
			shrm_restart_netdev(shrm->ndev);
#endif
		}
	} else {
		dev_err(shrm->dev, "Invalid boot state\n");
	}
	/* start timer here */
	hrtimer_start(&timer, ktime_set(0, 25*NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	atomic_dec(&ac_sleep_disable_count);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

void shm_ac_read_notif_1_tasklet(unsigned long tasklet_data)
{
	struct shrm_dev *shrm = (struct shrm_dev *)tasklet_data;
	u32 writer_local_rptr;
	u32 writer_local_wptr;
	u32 shared_wptr;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown\n",
				__func__);
		return;
	}

	/* Update writer_local_rptr(with shared_rptr) */
	update_ac_audio_local_rptr(shrm);
	get_writer_pointers(AUDIO_CHANNEL, &writer_local_rptr,
				&writer_local_wptr, &shared_wptr);
	if (boot_state != BOOT_DONE) {
		dev_err(shrm->dev, "Error Case in boot state\n");
		return;
	}
	if (writer_local_rptr != writer_local_wptr) {
		shrm_audio_tx_state = SHRM_PTR_FREE;
		queue_kthread_work(&shrm->shm_audio_ch_wr_kw,
				&shrm->send_ac_msg_pend_notify_1);
	} else {
		shrm_audio_tx_state = SHRM_IDLE;
	}
	/* start timer here */
	hrtimer_start(&timer, ktime_set(0, 25*NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	atomic_dec(&ac_sleep_disable_count);
	atomic_dec(&ac_msg_pend_1);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

void shm_ca_sleep_req_work(struct kthread_work *work)
{
	unsigned long flags;

	dev_dbg(shm_dev->dev, "%s:IRQ_PRCMU_CA_SLEEP\n", __func__);

	local_irq_save(flags);
	preempt_disable();
	if ((boot_state != BOOT_DONE) ||
			(readl(shm_dev->ca_reset_status_rptr))) {
		dev_err(shm_dev->dev, "%s:Modem state reset or unknown\n",
				__func__);
		preempt_enable();
		local_irq_restore(flags);
		return;
	}
	shrm_common_rx_state = SHRM_IDLE;
	shrm_audio_rx_state =  SHRM_IDLE;

	if (!get_host_accessport_val()) {
		dev_err(shm_dev->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		preempt_enable();
		local_irq_restore(flags);
		return;
	}

	log_this(40, NULL, 0, NULL, 0);
	trace_printk("CA_WAKE_ACK\n");
	writel((1<<GOP_CA_WAKE_ACK_BIT),
		shm_dev->intr_base + GOP_SET_REGISTER_BASE);
	preempt_enable();
	local_irq_restore(flags);

	hrtimer_start(&timer, ktime_set(0, 10*NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	if (suspend_sleep_is_blocked()) {
		suspend_unblock_sleep();
	}
	atomic_dec(&ac_sleep_disable_count);
}

void shm_ca_wake_req_work(struct kthread_work *work)
{
	unsigned long flags;
	struct shrm_dev *shrm = container_of(work,
			struct shrm_dev, shm_ca_wake_req);

	/* initialize the FIFO Variables */
	if (boot_state == BOOT_INIT) {
#ifdef CONFIG_U8500_SHRM_ENABLE_FEATURE_SIGNATURE_VERIFICATION
		/* Unlock the shared memory before accessing it */
		if (open_modem_shared_memory()) {
			dev_err(shrm->dev,
				"Unable to unlock the shared memory\n");
			return;
		}
#endif
		shm_fifo_init(shrm);
	}
	mutex_lock(&ac_state_mutex);
	modem_request(shrm->modem);
	mutex_unlock(&ac_state_mutex);

	local_irq_save(flags);
	preempt_disable();
	/* send ca_wake_ack_interrupt to CMU */
	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown\n",
			__func__);
		preempt_enable();
		local_irq_restore(flags);
		return;
	}

	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		preempt_enable();
		local_irq_restore(flags);
	}

	log_this(40, NULL, 1, NULL, 0);
	trace_printk("CA_WAKE_ACK\n");
	/* send ca_wake_ack_interrupt to CMU */
	writel((1<<GOP_CA_WAKE_ACK_BIT),
			shm_dev->intr_base + GOP_SET_REGISTER_BASE);
	preempt_enable();
	local_irq_restore(flags);
}
#ifdef CONFIG_U8500_SHRM_MODEM_SILENT_RESET
static int shrm_modem_reset_sequence(void)
{
	hrtimer_cancel(&timer);
	hrtimer_cancel(&mod_stuck_timer_0);
	hrtimer_cancel(&mod_stuck_timer_1);
	hrtimer_cancel(&fifo_full_timer);
	atomic_set(&mod_stuck, 0);
	atomic_set(&fifo_full, 0);
	tasklet_disable_nosync(&shm_ac_read_0_tasklet);
	tasklet_disable_nosync(&shm_ac_read_1_tasklet);
	tasklet_disable_nosync(&shm_ca_0_tasklet);
	tasklet_disable_nosync(&shm_ca_1_tasklet);

	/*
	 * keep the count to 0 so that we can bring down the line
	 * for normal ac-wake and ac-sleep logic
	 */
	atomic_set(&ac_sleep_disable_count, 0);
	atomic_set(&ac_msg_pend_1, 0);

	/* Notification of the crash to SIPC layer */
	if (shm_dev->msr_crash_cb)
		shm_dev->msr_crash_cb(shm_dev->msr_cookie);

	/* reset char device queues */
	shrm_char_reset_queues(shm_dev);

	/* reset protocol states */
	shrm_common_tx_state = SHRM_SLEEP_STATE;
	shrm_common_rx_state = SHRM_SLEEP_STATE;
	shrm_audio_tx_state = SHRM_SLEEP_STATE;
	shrm_audio_rx_state = SHRM_SLEEP_STATE;

	/* set the msr flag */
	shm_dev->msr_flag = 1;

	/* The order is very important here */
	queue_kthread_work(&shm_dev->shm_ac_wake_kw, &shm_dev->shm_ac_wake_req);
	queue_kthread_work(&shm_dev->shm_ac_wake_kw, &shm_dev->shm_mod_reset_process);

	return 0;
}

static void shm_mod_reset_work(struct kthread_work *work)
{
	int err;
	unsigned long flags;

	/* multicast that modem is going to reset */
	err = nl_send_multicast_message(SHRM_NL_MOD_RESET, GFP_KERNEL);
	if (err)
		dev_err(shm_dev->dev, "unable to send netlink message %d", err);

	/* reset the boot state */
	spin_lock_irqsave(&boot_lock, flags);
	boot_state = BOOT_INIT;
	spin_unlock_irqrestore(&boot_lock, flags);

	tasklet_enable(&shm_ac_read_0_tasklet);
	tasklet_enable(&shm_ac_read_1_tasklet);
	tasklet_enable(&shm_ca_0_tasklet);
	tasklet_enable(&shm_ca_1_tasklet);
	/* re-enable irqs */
	enable_irq(shm_dev->ac_read_notif_0_irq);
	enable_irq(shm_dev->ac_read_notif_1_irq);
	enable_irq(shm_dev->ca_msg_pending_notif_0_irq);
	enable_irq(shm_dev->ca_msg_pending_notif_1_irq);
	enable_irq(IRQ_PRCMU_CA_WAKE);
	enable_irq(IRQ_PRCMU_CA_SLEEP);

	if (suspend_sleep_is_blocked()) {
		suspend_unblock_sleep();
	}
}
#endif

static void shrm_modem_reset_callback(unsigned long irq)
{
	dev_err(shm_dev->dev, "Received mod_reset_req interrupt\n");

#ifdef CONFIG_U8500_SHRM_MODEM_SILENT_RESET
	{
		int err;
		dev_info(shm_dev->dev, "Initiating Modem silent reset\n");

		err = shrm_modem_reset_sequence();
		if (err)
			dev_err(shm_dev->dev,
				"Failed multicast of modem reset\n");
	}
#else
	dev_info(shm_dev->dev, "Modem in reset loop, doing System reset\n");

	/* Call the PRCMU reset API */
	prcmu_system_reset(SW_RESET_NO_ARGUMENT);
#endif
}

DECLARE_TASKLET(shrm_sw_reset_callback, shrm_modem_reset_callback,
		IRQ_PRCMU_MODEM_SW_RESET_REQ);

static irqreturn_t shrm_prcmu_irq_handler(int irq, void *data)
{
	struct shrm_dev *shrm = data;
	unsigned long flags;

	switch (irq) {
	case IRQ_PRCMU_CA_WAKE:
		suspend_block_sleep();
		if (shrm->msr_flag)
			atomic_set(&ac_sleep_disable_count, 0);
		atomic_inc(&ac_sleep_disable_count);
		queue_kthread_work(&shrm->shm_ca_wake_kw, &shrm->shm_ca_wake_req);
		break;
	case IRQ_PRCMU_CA_SLEEP:
		queue_kthread_work(&shrm->shm_ca_wake_kw, &shrm->shm_ca_sleep_req);
		break;
	case IRQ_PRCMU_MODEM_SW_RESET_REQ:
		/* update the boot_state */
		spin_lock_irqsave(&boot_lock, flags);
		boot_state = BOOT_UNKNOWN;

		/*
		 * put a barrier over here to make sure boot_state is updated
		 * else, it is seen that some of already executing modem
		 * irqs or tasklets fail the protocol checks and will ultimately
		 * try to acces the modem causing system to hang.
		 * This is particularly seen with user-space initiated modem reset
		 */
		wmb();
		spin_unlock_irqrestore(&boot_lock, flags);

		disable_irq_nosync(shrm->ac_read_notif_0_irq);
		disable_irq_nosync(shrm->ac_read_notif_1_irq);
		disable_irq_nosync(shrm->ca_msg_pending_notif_0_irq);
		disable_irq_nosync(shrm->ca_msg_pending_notif_1_irq);
		disable_irq_nosync(IRQ_PRCMU_CA_WAKE);
		disable_irq_nosync(IRQ_PRCMU_CA_SLEEP);

#ifdef CONFIG_U8500_SHRM_DEFAULT_NET
		/* stop network queue */
		shrm_stop_netdev(shm_dev->ndev);
#endif
		tasklet_schedule(&shrm_sw_reset_callback);
		break;
	default:
		dev_err(shrm->dev, "%s: => IRQ %d\n", __func__, irq);
		return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

static void send_ac_msg_pend_notify_0_work(struct kthread_work *work)
{
	unsigned long flags;
	struct shrm_dev *shrm = container_of(work, struct shrm_dev,
			send_ac_msg_pend_notify_0);

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	update_ac_common_shared_wptr(shrm);

	mutex_lock(&ac_state_mutex);
	atomic_inc(&ac_sleep_disable_count);
	modem_request(shrm->modem);
	mutex_unlock(&ac_state_mutex);

	spin_lock_irqsave(&start_timer_lock, flags);
	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		spin_unlock_irqrestore(&start_timer_lock, flags);
		return;
	}

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		spin_unlock_irqrestore(&start_timer_lock, flags);
		return;
	}

	log_this(251, NULL, 0, NULL, 0);
	trace_printk("AC COM PEND NOTIF\n");
	/* Trigger AcMsgPendingNotification to CMU */
	writel((1<<GOP_COMMON_AC_MSG_PENDING_NOTIFICATION_BIT),
			shrm->intr_base + GOP_SET_REGISTER_BASE);

	/* timer to detect modem stuck or hang */
	hrtimer_start(&mod_stuck_timer_0, ktime_set(MOD_STUCK_TIMEOUT, 0),
			HRTIMER_MODE_REL);
	spin_unlock_irqrestore(&start_timer_lock, flags);
	if (shrm_common_tx_state == SHRM_PTR_FREE)
		shrm_common_tx_state = SHRM_PTR_BUSY;

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

static void send_ac_msg_pend_notify_1_work(struct kthread_work *work)
{
	unsigned long flags;
	struct shrm_dev *shrm = container_of(work, struct shrm_dev,
			send_ac_msg_pend_notify_1);

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	/* Update shared_wptr with writer_local_wptr) */
	update_ac_audio_shared_wptr(shrm);

	mutex_lock(&ac_state_mutex);
	if (!atomic_read(&ac_msg_pend_1)) {
		atomic_inc(&ac_sleep_disable_count);
		atomic_inc(&ac_msg_pend_1);
	}
	modem_request(shrm->modem);
	mutex_unlock(&ac_state_mutex);

	spin_lock_irqsave(&start_timer_lock, flags);
	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		spin_unlock_irqrestore(&start_timer_lock, flags);
		return;
	}

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		spin_unlock_irqrestore(&start_timer_lock, flags);
		return;
	}

	log_this(252, NULL, 0, NULL, 0);
	trace_printk("AC AUD PEND NOTIF\n");
	/* Trigger AcMsgPendingNotification to CMU */
	writel((1<<GOP_AUDIO_AC_MSG_PENDING_NOTIFICATION_BIT),
			shrm->intr_base + GOP_SET_REGISTER_BASE);

	/* timer to detect modem stuck or hang */
	hrtimer_start(&mod_stuck_timer_1, ktime_set(MOD_STUCK_TIMEOUT, 0),
			HRTIMER_MODE_REL);
	spin_unlock_irqrestore(&start_timer_lock, flags);
	if (shrm_audio_tx_state == SHRM_PTR_FREE)
		shrm_audio_tx_state = SHRM_PTR_BUSY;

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

void shm_nl_receive(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	int msg;

	dev_dbg(shm_dev->dev, "Received NL msg from user-space\n");

	nlh = (struct nlmsghdr *)skb->data;
	msg = *((int *)(NLMSG_DATA(nlh)));
	switch (msg) {
	case SHRM_NL_MOD_QUERY_STATE:
		dev_dbg(shm_dev->dev, "mod-query-state from user-space\n");
		nl_send_unicast_message(nlh->nlmsg_pid);
		break;

	case SHRM_NL_USER_MOD_RESET:
		dev_info(shm_dev->dev, "user-space inited mod-reset-req\n");
		dev_info(shm_dev->dev, "PCRMU resets modem\n");
		if (atomic_read(&mod_stuck) || atomic_read(&fifo_full)) {
			dev_info(shm_dev->dev,
					"Modem reset already in progress\n");
			break;
		}
		atomic_set(&mod_stuck, 1);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_mod_reset_req);
		break;

	default:
		dev_err(shm_dev->dev, "Invalid NL msg from user-space\n");
		break;
	};
}

int shrm_protocol_init(struct shrm_dev *shrm,
			received_msg_handler common_rx_handler,
			received_msg_handler audio_rx_handler)
{
	int err = 0;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	shm_dev = shrm;
	boot_state = BOOT_INIT;
	dev_info(shrm->dev, "IPC_ISA BOOT_INIT\n");
	rx_common_handler = common_rx_handler;
	rx_audio_handler = audio_rx_handler;
	atomic_set(&ac_sleep_disable_count, 0);

	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.function = callback;
	hrtimer_init(&mod_stuck_timer_0, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mod_stuck_timer_0.function = shm_mod_stuck_timeout;
	hrtimer_init(&mod_stuck_timer_1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mod_stuck_timer_1.function = shm_mod_stuck_timeout;
	hrtimer_init(&fifo_full_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	fifo_full_timer.function = shm_fifo_full_timeout;

	init_kthread_worker(&shrm->shm_common_ch_wr_kw);
	shrm->shm_common_ch_wr_kw_task = kthread_run(kthread_worker_fn,
						     &shrm->shm_common_ch_wr_kw,
						     "shm_common_channel_irq");
	if (IS_ERR(shrm->shm_common_ch_wr_kw_task)) {
		dev_err(shrm->dev, "failed to create work task\n");
		return -ENOMEM;
	}

	init_kthread_worker(&shrm->shm_audio_ch_wr_kw);
	shrm->shm_audio_ch_wr_kw_task = kthread_run(kthread_worker_fn,
						    &shrm->shm_audio_ch_wr_kw,
						    "shm_audio_channel_irq");
	if (IS_ERR(shrm->shm_audio_ch_wr_kw_task)) {
		dev_err(shrm->dev, "failed to create work task\n");
		err = -ENOMEM;
		goto free_kw1;
	}
	/* must use the FIFO scheduler as it is realtime sensitive */
	sched_setscheduler(shrm->shm_audio_ch_wr_kw_task, SCHED_FIFO, &param);

	init_kthread_worker(&shrm->shm_ac_wake_kw);
	shrm->shm_ac_wake_kw_task = kthread_run(kthread_worker_fn,
						&shrm->shm_ac_wake_kw,
						"shm_ac_wake_req");
	if (IS_ERR(shrm->shm_ac_wake_kw_task)) {
		dev_err(shrm->dev, "failed to create work task\n");
		err = -ENOMEM;
		goto free_kw2;
	}
	/* must use the FIFO scheduler as it is realtime sensitive */
	sched_setscheduler(shrm->shm_ac_wake_kw_task, SCHED_FIFO, &param);

	init_kthread_worker(&shrm->shm_ca_wake_kw);
	shrm->shm_ca_wake_kw_task = kthread_run(kthread_worker_fn,
						&shrm->shm_ca_wake_kw,
						"shm_ca_wake_req");
	if (IS_ERR(shrm->shm_ca_wake_kw_task)) {
		dev_err(shrm->dev, "failed to create work task\n");
		err = -ENOMEM;
		goto free_kw3;
	}
	/* must use the FIFO scheduler as it is realtime sensitive */
	sched_setscheduler(shrm->shm_ca_wake_kw_task, SCHED_FIFO, &param);

	init_kthread_worker(&shrm->shm_ac_sleep_kw);
	shrm->shm_ac_sleep_kw_task = kthread_run(kthread_worker_fn,
						 &shrm->shm_ac_sleep_kw,
						 "shm_ac_sleep_req");
	if (IS_ERR(shrm->shm_ac_sleep_kw_task)) {
		dev_err(shrm->dev, "failed to create work task\n");
		err = -ENOMEM;
		goto free_kw4;
	}
	init_kthread_worker(&shrm->shm_mod_stuck_kw);
	shrm->shm_mod_stuck_kw_task = kthread_run(kthread_worker_fn,
						     &shrm->shm_mod_stuck_kw,
						     "shm_mod_reset_req");
	if (IS_ERR(shrm->shm_mod_stuck_kw_task)) {
		dev_err(shrm->dev, "failed to create work task\n");
		err = -ENOMEM;
		goto free_kw5;
	}

	init_kthread_work(&shrm->send_ac_msg_pend_notify_0,
			  send_ac_msg_pend_notify_0_work);
	init_kthread_work(&shrm->send_ac_msg_pend_notify_1,
			  send_ac_msg_pend_notify_1_work);
	init_kthread_work(&shrm->shm_ca_wake_req, shm_ca_wake_req_work);
	init_kthread_work(&shrm->shm_ca_sleep_req, shm_ca_sleep_req_work);
	init_kthread_work(&shrm->shm_ac_sleep_req, shm_ac_sleep_req_work);
	init_kthread_work(&shrm->shm_ac_wake_req, shm_ac_wake_req_work);
	init_kthread_work(&shrm->shm_mod_reset_process, shm_mod_reset_work);
	init_kthread_work(&shrm->shm_mod_reset_req, shm_mod_reset_req_work);
	init_kthread_work(&shrm->shm_print_dbg_info, shm_print_dbg_info_work);

	/* set tasklet data */
	shm_ca_0_tasklet.data = (unsigned long)shrm;
	shm_ca_1_tasklet.data = (unsigned long)shrm;

	err = request_irq(IRQ_PRCMU_CA_SLEEP, shrm_prcmu_irq_handler,
			IRQF_NO_SUSPEND, "ca-sleep", shrm);
	if (err < 0) {
		dev_err(shm_dev->dev, "Failed alloc IRQ_PRCMU_CA_SLEEP.\n");
		goto free_kw6;
	}

	err = request_irq(IRQ_PRCMU_CA_WAKE, shrm_prcmu_irq_handler,
		IRQF_NO_SUSPEND, "ca-wake", shrm);
	if (err < 0) {
		dev_err(shm_dev->dev, "Failed alloc IRQ_PRCMU_CA_WAKE.\n");
		goto drop2;
	}

	err = request_irq(IRQ_PRCMU_MODEM_SW_RESET_REQ, shrm_prcmu_irq_handler,
			IRQF_NO_SUSPEND, "modem-sw-reset-req", shrm);
	if (err < 0) {
		dev_err(shm_dev->dev,
				"Failed alloc IRQ_PRCMU_MODEM_SW_RESET_REQ.\n");
		goto drop1;
	}

#ifdef CONFIG_U8500_SHRM_MODEM_SILENT_RESET
	/* init netlink socket for user-space communication */
	shrm_nl_sk = netlink_kernel_create(NULL, NETLINK_SHRM, 1,
			shm_nl_receive, NULL, THIS_MODULE);

	if (!shrm_nl_sk) {
		dev_err(shm_dev->dev, "netlink socket creation failed\n");
		goto drop;
	}

	netlink_set_nonroot(NETLINK_SHRM, NL_NONROOT_RECV);
#endif
#ifdef CONFIG_U8500_KERNEL_CLIENT
	err = u8500_kernel_client_init(shrm);
#endif
	/*
	 * Since modem is loaded via kernel, kernel should make sure to set
	 * host access port, to enable modem.
	 */
	modem_request(shrm->modem);
	return err;

#ifdef CONFIG_U8500_SHRM_MODEM_SILENT_RESET
drop:
	free_irq(IRQ_PRCMU_MODEM_SW_RESET_REQ, NULL);
#endif
drop1:
	free_irq(IRQ_PRCMU_CA_WAKE, NULL);
drop2:
	free_irq(IRQ_PRCMU_CA_SLEEP, NULL);
free_kw6:
	kthread_stop(shrm->shm_mod_stuck_kw_task);
free_kw5:
	kthread_stop(shrm->shm_ac_sleep_kw_task);
free_kw4:
	kthread_stop(shrm->shm_ca_wake_kw_task);
free_kw3:
	kthread_stop(shrm->shm_ac_wake_kw_task);
free_kw2:
	kthread_stop(shrm->shm_audio_ch_wr_kw_task);
free_kw1:
	kthread_stop(shrm->shm_common_ch_wr_kw_task);
	return err;
}

void shrm_protocol_deinit(struct shrm_dev *shrm)
{
#ifdef CONFIG_U8500_KERNEL_CLIENT
	u8500_kernel_client_exit();
#endif
	free_irq(IRQ_PRCMU_CA_SLEEP, NULL);
	free_irq(IRQ_PRCMU_CA_WAKE, NULL);
	free_irq(IRQ_PRCMU_MODEM_SW_RESET_REQ, NULL);
	flush_kthread_worker(&shrm->shm_common_ch_wr_kw);
	flush_kthread_worker(&shrm->shm_audio_ch_wr_kw);
	flush_kthread_worker(&shrm->shm_ac_wake_kw);
	flush_kthread_worker(&shrm->shm_ca_wake_kw);
	flush_kthread_worker(&shrm->shm_ac_sleep_kw);
	flush_kthread_worker(&shrm->shm_mod_stuck_kw);
	kthread_stop(shrm->shm_common_ch_wr_kw_task);
	kthread_stop(shrm->shm_audio_ch_wr_kw_task);
	kthread_stop(shrm->shm_ac_wake_kw_task);
	kthread_stop(shrm->shm_ca_wake_kw_task);
	kthread_stop(shrm->shm_ac_sleep_kw_task);
	kthread_stop(shrm->shm_mod_stuck_kw_task);
	modem_put(shrm->modem);
}

int get_ca_wake_req_state(void)
{
	return ((atomic_read(&ac_sleep_disable_count) > 0) ||
			modem_get_usage(shm_dev->modem));
}

irqreturn_t ca_wake_irq_handler(int irq, void *ctrlr)
{
	struct shrm_dev *shrm = ctrlr;

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	/* initialize the FIFO Variables */
	if (boot_state == BOOT_INIT)
		shm_fifo_init(shrm);

	dev_dbg(shrm->dev, "Inside ca_wake_irq_handler\n");

	/* Clear the interrupt */
	writel((1 << GOP_CA_WAKE_REQ_BIT),
				shrm->intr_base + GOP_CLEAR_REGISTER_BASE);

	/* send ca_wake_ack_interrupt to CMU */
	log_this(40, NULL, 2, NULL, 0);
	trace_printk("CA_WAKE_ACK\n");
	writel((1 << GOP_CA_WAKE_ACK_BIT),
		shrm->intr_base + GOP_SET_REGISTER_BASE);


	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return IRQ_HANDLED;
}


irqreturn_t ac_read_notif_0_irq_handler(int irq, void *ctrlr)
{
	unsigned long flags;
	struct shrm_dev *shrm = ctrlr;

	dev_dbg(shrm->dev, "%s IN\n", __func__);
	/* Cancel the modem stuck timer */
	spin_lock_irqsave(&start_timer_lock, flags);
	hrtimer_cancel(&mod_stuck_timer_0);
	spin_unlock_irqrestore(&start_timer_lock, flags);
	if (atomic_read(&fifo_full)) {
		atomic_set(&fifo_full, 0);
		hrtimer_cancel(&fifo_full_timer);
	}

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		return IRQ_NONE;
	}

	shm_ac_read_0_tasklet.data = (unsigned long)shrm;
	tasklet_schedule(&shm_ac_read_0_tasklet);

	local_irq_save(flags);
	preempt_disable();
	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}

	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}
	/* Clear the interrupt */
	writel((1 << GOP_COMMON_AC_READ_NOTIFICATION_BIT),
			shrm->intr_base + GOP_CLEAR_REGISTER_BASE);
	preempt_enable();
	local_irq_restore(flags);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return IRQ_HANDLED;
}

irqreturn_t ac_read_notif_1_irq_handler(int irq, void *ctrlr)
{
	unsigned long flags;
	struct shrm_dev *shrm = ctrlr;

	dev_dbg(shrm->dev, "%s IN+\n", __func__);
	/* Cancel the modem stuck timer */
	spin_lock_irqsave(&start_timer_lock, flags);
	hrtimer_cancel(&mod_stuck_timer_1);
	spin_unlock_irqrestore(&start_timer_lock, flags);
	if (atomic_read(&fifo_full)) {
		atomic_set(&fifo_full, 0);
		hrtimer_cancel(&fifo_full_timer);
	}

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		return IRQ_NONE;
	}

	shm_ac_read_1_tasklet.data = (unsigned long)shrm;
	tasklet_schedule(&shm_ac_read_1_tasklet);

	local_irq_save(flags);
	preempt_disable();
	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}

	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}
	/* Clear the interrupt */
	writel((1 << GOP_AUDIO_AC_READ_NOTIFICATION_BIT),
			shrm->intr_base + GOP_CLEAR_REGISTER_BASE);
	preempt_enable();
	local_irq_restore(flags);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return IRQ_HANDLED;
}

irqreturn_t ca_msg_pending_notif_0_irq_handler(int irq, void *ctrlr)
{
	unsigned long flags;
	struct shrm_dev *shrm = ctrlr;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		return IRQ_NONE;
	}

	tasklet_schedule(&shm_ca_0_tasklet);

	local_irq_save(flags);
	preempt_disable();
	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}

	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}
	/* Clear the interrupt */
	log_this(248, NULL, 0, NULL, 0);
	trace_printk("CA MSG PEND\n");
	writel((1 << GOP_COMMON_CA_MSG_PENDING_NOTIFICATION_BIT),
			shrm->intr_base + GOP_CLEAR_REGISTER_BASE);
	preempt_enable();
	local_irq_restore(flags);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return IRQ_HANDLED;
}

irqreturn_t ca_msg_pending_notif_1_irq_handler(int irq, void *ctrlr)
{
	unsigned long flags;
	struct shrm_dev *shrm = ctrlr;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		return IRQ_NONE;
	}

	tasklet_schedule(&shm_ca_1_tasklet);

	local_irq_save(flags);
	preempt_disable();
	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}

	if (!get_host_accessport_val()) {
		dev_err(shrm->dev, "%s: host_accessport is low\n", __func__);
		queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
				&shm_dev->shm_mod_reset_req);
		preempt_enable();
		local_irq_restore(flags);
		return IRQ_NONE;
	}
	/* Clear the interrupt */
	log_this(249, NULL, 0, NULL, 0);
	trace_printk("CA MSG PEND\n");
	writel((1<<GOP_AUDIO_CA_MSG_PENDING_NOTIFICATION_BIT),
			shrm->intr_base+GOP_CLEAR_REGISTER_BASE);
	preempt_enable();
	local_irq_restore(flags);

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return IRQ_HANDLED;

}

/**
 * shm_write_msg() - write message to shared memory
 * @shrm:	pointer to the shrm device information structure
 * @l2_header:	L2 header
 * @addr:	pointer to the message
 * @length:	length of the message to be written
 *
 * This function is called from net or char interface driver write operation.
 * Prior to calling this function the message is copied from the user space
 * buffer to the kernel buffer. This function based on the l2 header routes
 * the message to the respective channel and FIFO. Then makes a call to the
 * fifo write function where the message is written to the physical device.
 */
int shm_write_msg(struct shrm_dev *shrm, u8 l2_header,
					void *addr, u32 length)
{
	u8 channel = 0;
	int ret, i;

	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (boot_state != BOOT_DONE) {
		dev_err(shrm->dev,
			"error:after boot done  call this fn, L2Header = %d\n",
			l2_header);
		dev_err(shrm->dev, "packet not sent, modem in reset");
		for (i = 0; i < length; i++)
			dev_dbg(shrm->dev, "data[%d]=%02x\n", i, *(u8 *)addr);
		/*
		 * If error is returned then phonet tends to resend the msg
		 * this will lead to the msg bouncing to and fro between
		 * phonet and shrm, hence dont return error.
		*/
		ret = 0;
		goto out;
	}

	if ((l2_header == L2_HEADER_ISI) ||
			(l2_header == L2_HEADER_RPC) ||
			(l2_header == L2_HEADER_SECURITY) ||
			(l2_header == L2_HEADER_COMMON_SIMPLE_LOOPBACK) ||
			(l2_header == L2_HEADER_COMMON_ADVANCED_LOOPBACK) ||
			(l2_header == L2_HEADER_CIQ) ||
			(l2_header == L2_HEADER_RTC_CALIBRATION) ||
			(l2_header == L2_HEADER_IPCCTRL) ||
			(l2_header == L2_HEADER_IPCDATA) ||
			(l2_header == L2_HEADER_SYSCLK3)) {
		channel = 0;
		if (shrm_common_tx_state == SHRM_SLEEP_STATE)
			shrm_common_tx_state = SHRM_PTR_FREE;
		else if (shrm_common_tx_state == SHRM_IDLE)
			shrm_common_tx_state = SHRM_PTR_FREE;

	} else if ((l2_header == L2_HEADER_AUDIO) ||
			(l2_header == L2_HEADER_AUDIO_SIMPLE_LOOPBACK) ||
			(l2_header == L2_HEADER_AUDIO_ADVANCED_LOOPBACK)) {
		if (shrm_audio_tx_state == SHRM_SLEEP_STATE)
			shrm_audio_tx_state = SHRM_PTR_FREE;
		else if (shrm_audio_tx_state == SHRM_IDLE)
			shrm_audio_tx_state = SHRM_PTR_FREE;

		channel = 1;
	} else {
		ret = -ENODEV;
		goto out;
	}
	ret = shm_write_msg_to_fifo(shrm, channel, l2_header, addr, length);
	if (ret < 0) {
		dev_err(shrm->dev, "write message to fifo failed\n");
		if (ret == -EAGAIN) {
			if (!atomic_read(&fifo_full)) {
				/* Start a timer so as to handle this gently */
				atomic_set(&fifo_full, 1);
				hrtimer_start(&fifo_full_timer, ktime_set(
						FIFO_FULL_TIMEOUT, 0),
						HRTIMER_MODE_REL);
			}
		}
		return ret;
	}
	/*
	 * notify only if new msg copied is the only unread one
	 * otherwise it means that reading process is ongoing
	 */
	if (is_the_only_one_unread_message(shrm, channel, length)) {

		/* Send Message Pending Noitication to CMT */
		if (channel == 0)
			queue_kthread_work(&shrm->shm_common_ch_wr_kw,
					&shrm->send_ac_msg_pend_notify_0);
		else
			queue_kthread_work(&shrm->shm_audio_ch_wr_kw,
					&shrm->send_ac_msg_pend_notify_1);

	}

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
	return 0;

out:
	return ret;
}

void ca_msg_read_notification_0(struct shrm_dev *shrm)
{
	unsigned long flags;
	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (get_ca_msg_0_read_notif_send() == 0) {
		update_ca_common_shared_rptr(shrm);

		local_irq_save(flags);
		preempt_disable();
		if (check_modem_in_reset()) {
			dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
					__func__);
			preempt_enable();
			local_irq_restore(flags);
			return;
		}

		if (!get_host_accessport_val()) {
			dev_err(shrm->dev, "%s: host_accessport is low\n",
					__func__);
			queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_mod_reset_req);
			preempt_enable();
			local_irq_restore(flags);
			return;
		}

		log_this(253, NULL, 0, NULL, 0);
		trace_printk("CA COM READ NOTIF\n");
		/* Trigger CaMsgReadNotification to CMU */
		writel((1 << GOP_COMMON_CA_READ_NOTIFICATION_BIT),
			shrm->intr_base + GOP_SET_REGISTER_BASE);
		preempt_enable();
		local_irq_restore(flags);
		set_ca_msg_0_read_notif_send(1);
		shrm_common_rx_state = SHRM_PTR_BUSY;
	}

	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

void ca_msg_read_notification_1(struct shrm_dev *shrm)
{
	unsigned long flags;
	dev_dbg(shrm->dev, "%s IN\n", __func__);

	if (get_ca_msg_1_read_notif_send() == 0) {
		update_ca_audio_shared_rptr(shrm);

		local_irq_save(flags);
		preempt_disable();
		if (check_modem_in_reset()) {
			dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
					__func__);
			preempt_enable();
			local_irq_restore(flags);
			return;
		}

		if (!get_host_accessport_val()) {
			dev_err(shrm->dev, "%s: host_accessport is low\n",
					__func__);
			queue_kthread_work(&shm_dev->shm_mod_stuck_kw,
					&shm_dev->shm_mod_reset_req);
			preempt_enable();
			local_irq_restore(flags);
			return;
		}

		log_this(254, NULL, 0, NULL, 0);
		trace_printk("CA AUD READ NOTIF\n");
		/* Trigger CaMsgReadNotification to CMU */
		writel((1<<GOP_AUDIO_CA_READ_NOTIFICATION_BIT),
			shrm->intr_base+GOP_SET_REGISTER_BASE);
		preempt_enable();
		local_irq_restore(flags);
		set_ca_msg_1_read_notif_send(1);
		shrm_audio_rx_state = SHRM_PTR_BUSY;
	}
	dev_dbg(shrm->dev, "%s OUT\n", __func__);
}

/**
 * receive_messages_common - receive common channnel msg from
 * CMT(Cellular Mobile Terminal)
 * @shrm:	pointer to shrm device information structure
 *
 * The messages sent from CMT to APE are written to the respective FIFO
 * and an interrupt is triggered by the CMT. This ca message pending
 * interrupt calls this function. This function sends a read notification
 * acknowledgement to the CMT and calls the common channel receive handler
 * where the messsage is copied to the respective(ISI, RPC, SECURIT) queue
 * based on the message l2 header.
 */
void receive_messages_common(struct shrm_dev *shrm)
{
	u8 l2_header;
	u32 len;

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		return;
	}

	l2_header = read_one_l2msg_common(shrm, recieve_common_msg, &len);
	/* Send Recieve_Call_back to Upper Layer */
	if (!rx_common_handler) {
		dev_err(shrm->dev, "common_rx_handler is Null\n");
		BUG();
	}
	(*rx_common_handler)(l2_header, &recieve_common_msg, len,
					shrm);
	/* SendReadNotification */
	ca_msg_read_notification_0(shrm);

	while (read_remaining_messages_common()) {
		if (check_modem_in_reset()) {
			dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
					__func__);
			return;
		}

		l2_header = read_one_l2msg_common(shrm, recieve_common_msg,
								&len);
		/* Send Recieve_Call_back to Upper Layer */
		(*rx_common_handler)(l2_header,
					&recieve_common_msg, len,
					shrm);
	}
}

/**
 * receive_messages_audio() - receive audio message from CMT
 * @shrm:	pointer to shrm device information structure
 *
 * The messages sent from CMT to APE are written to the respective FIFO
 * and an interrupt is triggered by the CMT. This ca message pending
 * interrupt calls this function. This function sends a read notification
 * acknowledgement to the CMT and calls the common channel receive handler
 * where the messsage is copied to the audio queue.
 */
void receive_messages_audio(struct shrm_dev *shrm)
{
	u8 l2_header;
	u32 len;

	if (check_modem_in_reset()) {
		dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
				__func__);
		return;
	}

	l2_header = read_one_l2msg_audio(shrm, recieve_audio_msg, &len);
	/* Send Recieve_Call_back to Upper Layer */

	if (!rx_audio_handler) {
		dev_crit(shrm->dev, "audio_rx_handler is Null\n");
		BUG();
	}
	(*rx_audio_handler)(l2_header, &recieve_audio_msg,
					len, shrm);

	/* SendReadNotification */
	ca_msg_read_notification_1(shrm);
	while (read_remaining_messages_audio())	{
		if (check_modem_in_reset()) {
			dev_err(shrm->dev, "%s:Modem state reset or unknown.\n",
					__func__);
			return;
		}

		l2_header = read_one_l2msg_audio(shrm,
						recieve_audio_msg, &len);
		/* Send Recieve_Call_back to Upper Layer */
		(*rx_audio_handler)(l2_header,
					&recieve_audio_msg, len,
					shrm);
	}
}

u8 get_boot_state()
{
	return boot_state;
}
