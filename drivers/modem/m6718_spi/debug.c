/*
 * Copyright (C) ST-Ericsson SA 2010,2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * U9500 <-> M6718 IPC protocol implementation using SPI:
 *   debug functionality.
 */
#include <linux/gpio.h>
#include <linux/modem/m6718_spi/modem_driver.h>
#include "modem_debug.h"
#include "modem_private.h"
#include "modem_util.h"
#include "modem_queue.h"

/* name of each state - must match enum ipc_sm_state_id */
static const char * const sm_state_id_str[] = {
	"IPC_INIT",
	"IPC_HALT",
	"IPC_RESET",
	"IPC_WAIT_SLAVE_STABLE",
	"IPC_WAIT_HANDSHAKE_INACTIVE",
	"IPC_SLW_TX_BOOTREQ",
	"IPC_ACT_TX_BOOTREQ",
	"IPC_SLW_RX_BOOTRESP",
	"IPC_ACT_RX_BOOTRESP",
	"IPC_IDL",
	"IPC_SLW_TX_WR_CMD",
	"IPC_ACT_TX_WR_CMD",
	"IPC_SLW_TX_WR_DAT",
	"IPC_ACT_TX_WR_DAT",
	"IPC_SLW_TX_RD_CMD",
	"IPC_ACT_TX_RD_CMD",
	"IPC_SLW_RX_WR_CMD",
	"IPC_ACT_RX_WR_CMD",
	"IPC_ACT_RX_WR_DAT",
};

/* name of each state machine run cause */
static const char * const sm_run_cause_str[] = {
	[IPC_SM_RUN_NONE]         = "IPC_SM_RUN_NONE",
	[IPC_SM_RUN_SLAVE_IRQ]    = "IPC_SM_RUN_SLAVE_IRQ",
	[IPC_SM_RUN_TFR_COMPLETE] = "IPC_SM_RUN_TFR_COMPLETE",
	[IPC_SM_RUN_TX_REQ]       = "IPC_SM_RUN_TX_REQ",
	[IPC_SM_RUN_INIT]         = "IPC_SM_RUN_INIT",
	[IPC_SM_RUN_ABORT]        = "IPC_SM_RUN_ABORT",
	[IPC_SM_RUN_COMMS_TMO]    = "IPC_SM_RUN_COMMS_TMO",
	[IPC_SM_RUN_STABLE_TMO]   = "IPC_SM_RUN_STABLE_TMO",
	[IPC_SM_RUN_RESET]        = "IPC_SM_RUN_RESET"
};


#if defined DUMP_SPI_TFRS || \
	defined CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_FRAME_DUMP
static const char *format_buf(const void *buffer, int len)
{
	static char dumpbuf[6000];
	char *wr = dumpbuf;
	const char *rd = buffer;
	int maxlen = min(len, (int)(sizeof(dumpbuf) / 3));
	int i;

	for (i = 0 ; i < maxlen ; i++) {
		sprintf(wr, "%02x ", rd[i]);
		wr += 3;
	}
	return dumpbuf;
}
#endif

void ipc_dbg_dump_frame(struct device *dev, int linkid,
	struct ipc_tx_queue *frame, bool tx)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_FRAME_DUMP
	if (frame->actual_len == 0)
		return;

	/*
	 * Use printk(KERN_DEBUG... directly to ensure these are printed even
	 * when DEBUG is not defined for this device - we want to be able to
	 * dump the frames independently from the debug logging.
	 */
	printk(KERN_DEBUG "IPC link%d %s %3d %4d bytes:%s\n",
		linkid, (tx ? "TX" : "RX"), frame->counter, frame->len,
		format_buf(frame->data, frame->len));
#endif
}

void ipc_dbg_dump_spi_tfr(struct ipc_link_context *context)
{
#ifdef DUMP_SPI_TFRS
	struct spi_transfer *tfr = &context->spi_transfer;
	struct spi_message *msg = &context->spi_message;

	if (tfr->tx_buf != NULL)
		dev_info(&context->sdev->dev, "link%d TX %4d bytes:%s\n",
			context->link->id, msg->actual_length,
			format_buf(tfr->tx_buf, msg->actual_length));

	if (tfr->rx_buf != NULL)
		dev_info(&context->sdev->dev, "link%d RX %4d bytes:%s\n",
			context->link->id, msg->actual_length,
			format_buf(tfr->rx_buf, msg->actual_length));
#endif
}

const char *ipc_dbg_state_id(const struct ipc_sm_state *state)
{
	if (state == NULL)
		return "(unknown)";
	else
		return sm_state_id_str[state->id];
}

const char *ipc_dbg_event(u8 event)
{
	return sm_run_cause_str[event];
}

char *ipc_dbg_link_state_str(struct ipc_link_context *context)
{
	char *statestr;
	int ss_pin;
	int int_pin;
	int min_free_pc;

	if (context == NULL)
		return NULL;

	statestr = kmalloc(500, GFP_ATOMIC);
	if (statestr == NULL)
		return NULL;

	ss_pin = gpio_get_value(context->link->gpio.ss_pin);
	int_pin = gpio_get_value(context->link->gpio.int_pin);
	min_free_pc = context->tx_q_min > 0 ?
		(context->tx_q_min * 100) / IPC_TX_QUEUE_MAX_SIZE :
		0;

	sprintf(statestr,
		"state=%s (for %lus)\n"
		"ss=%s(%d)\n"
		"int=%s(%d)\n"
		"lastevent=%s\n"
		"lastignored=%s in %s (ignoredinthis=%d)\n"
		"tx_q_min=%d(%d%%)\n"
		"tx_q_count=%d\n"
		"lastcmd=0x%08x (type %d count %d len %d)\n",
		sm_state_id_str[context->state->id],
		(jiffies - context->statesince) / HZ,
		ss_pin == ipc_util_ss_level_active(context) ?
			"ACTIVE" : "INACTIVE",
		ss_pin,
		int_pin == ipc_util_int_level_active(context) ?
			"ACTIVE" : "INACTIVE",
		int_pin,
		sm_run_cause_str[context->lastevent],
		sm_run_cause_str[context->lastignored],
		sm_state_id_str[context->lastignored_in],
		context->lastignored_inthis,
		context->tx_q_min,
		min_free_pc,
		atomic_read(&context->tx_q_count),
		context->cmd,
		ipc_util_get_l1_cmd(context->cmd),
		ipc_util_get_l1_counter(context->cmd),
		ipc_util_get_l1_length(context->cmd));
	return statestr;
}

void ipc_dbg_verify_rx_frame(struct ipc_link_context *context)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_VERIFY_FRAMES
	int i;
	u8 *last;
	u8 *curr;
	bool good = true;

	if (context->last_frame == NULL)
		return;

	if (context->last_frame->actual_len != context->frame->actual_len) {
		dev_err(&context->sdev->dev,
			"link %d error: loopback frame length error, "
			"TX %d RX %d\n",
			context->link->id,
			context->last_frame->actual_len,
			context->frame->actual_len);
		good = false;
		goto out;
	}

	last = (u8 *)context->last_frame->data;
	curr = (u8 *)context->frame->data;

	/* skip any padding bytes */
	for (i = 0; i < context->last_frame->actual_len; i++) {
		if (last[i] != curr[i]) {
			dev_err(&context->sdev->dev,
				"link %d bad byte %05d: "
				"TX %02x RX %02x\n",
				context->link->id,
				i,
				last[i],
				curr[i]);
			good = false;
		}
	}

out:
	if (!good)
		dev_info(&context->sdev->dev,
			"link %d error: loopback frame verification failed!\n",
			context->link->id);

	ipc_queue_delete_frame(context->last_frame);
	context->last_frame = NULL;
#endif
}

#ifdef CONFIG_DEBUG_FS
static int debugfs_linkstate_open(struct inode *inode, struct file *file);
static int debugfs_linkstate_show(struct seq_file *s, void *data);

static int debugfs_msr_open(struct inode *inode, struct file *file);
static int debugfs_msr_show(struct seq_file *s, void *data);
static ssize_t debugfs_msr_write(struct file *file,
	const char __user *user_buf,
	size_t count, loff_t *ppos);

static const struct file_operations debugfs_fops = {
	.open = debugfs_linkstate_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};

static const struct file_operations debugfs_msr_fops = {
	.open = debugfs_msr_open,
	.read = seq_read,
	.write = debugfs_msr_write,
	.llseek = seq_lseek,
	.release = single_release
};

static int debugfs_linkstate_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_linkstate_show, inode->i_private);
}

static int debugfs_linkstate_show(struct seq_file *s, void *data)
{
	struct ipc_link_context *context = s->private;
	char *statestr;

	if (context == NULL) {
		seq_printf(s, "invalid context\n");
		return 0;
	}

	statestr = ipc_dbg_link_state_str(context);
	if (statestr == NULL) {
		seq_printf(s, "unable to get link state string\n");
		return 0;
	}

	seq_printf(s, "%s:\n%s", context->link->name, statestr);
	kfree(statestr);
	return 0;
}

static int debugfs_msr_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_msr_show, inode->i_private);
}

static int debugfs_msr_show(struct seq_file *s, void *data)
{
	struct ipc_l1_context *context = s->private;

	if (context == NULL) {
		seq_printf(s, "invalid context\n");
		return 0;
	}

	seq_printf(s, "msr %s\n",
		context->msr_disable ? "disabled" : "enabled");
	return 0;
}

static ssize_t debugfs_msr_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	char buf[128];
	int buf_size;

	/* get user space string and assure termination */
	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = 0;

	if (buf[0] == '0' || buf[0] == 'd') {
		pr_info("disabling msr\n");
		l1_context.msr_disable = true;
	} else if (buf[0] == '1' || buf[0] == 'e') {
		pr_info("enabling msr\n");
		l1_context.msr_disable = false;
	} else {
		pr_info("unknown request\n");
	}

	return buf_size;
}
#endif /* CONFIG_DEBUG_FS */

void ipc_dbg_debugfs_init(void)
{
#ifdef CONFIG_DEBUG_FS
	/* create debugfs directory entry for ipc in debugfs root */
	l1_context.debugfsdir = debugfs_create_dir("modemipc", NULL);
	l1_context.debugfs_silentreset =
		debugfs_create_file("msrenable", S_IRUSR | S_IWUSR,
			l1_context.debugfsdir, &l1_context, &debugfs_msr_fops);
	if (l1_context.debugfs_silentreset == NULL)
		pr_err("failed to create debugfs MSR control file\n");
#endif
}

void ipc_dbg_debugfs_link_init(struct ipc_link_context *context)
{
#ifdef CONFIG_DEBUG_FS
	context->debugfsfile = NULL;
	context->lastevent = IPC_SM_RUN_NONE;
	context->lastignored = IPC_SM_RUN_NONE;
	context->lastignored_in = IPC_SM_IDL;
	context->lastignored_inthis = false;
	context->tx_q_min = IPC_TX_QUEUE_MAX_SIZE;
	context->statesince = 0;

	if (l1_context.debugfsdir != NULL) {
		context->debugfsfile =
			debugfs_create_file(context->link->name, S_IRUGO,
				l1_context.debugfsdir, context, &debugfs_fops);
		if (context->debugfsfile == NULL)
			dev_err(&context->sdev->dev,
				"link %d: failed to create debugfs file %s\n",
				context->link->id,
				context->link->name);
	}
#endif
}

void ipc_dbg_ignoring_event(struct ipc_link_context *context, u8 event)
{
#ifdef CONFIG_DEBUG_FS
	context->lastignored = event;
	context->lastignored_in = context->state->id;
	context->lastignored_inthis = true;
#endif
}

void ipc_dbg_handling_event(struct ipc_link_context *context, u8 event)
{
#ifdef CONFIG_DEBUG_FS
	context->lastevent = event;
	context->lastignored_inthis = false;
#endif
}

void ipc_dbg_entering_state(struct ipc_link_context *context)
{
#ifdef CONFIG_DEBUG_FS
	context->statesince = jiffies;
#endif
}

void ipc_dbg_enter_idle(struct ipc_link_context *context)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	context->idl_idle_enter = jiffies;
#endif
}

void ipc_dbg_exit_idle(struct ipc_link_context *context)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	context->idl_idle_total += jiffies - context->idl_idle_enter;
#endif
}

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
static int measure_usage(struct ipc_link_context *context)
{
	unsigned long now = jiffies;
	unsigned long idle;
	unsigned long total;

	if (ipc_util_link_is_idle(context))
		ipc_dbg_exit_idle(context);

	idle = context->idl_idle_total;
	total = now - context->idl_measured_at;

	context->idl_measured_at = now;
	context->idl_idle_total = 0;
	if (ipc_util_link_is_idle(context))
		context->idl_idle_enter = now;

	return 100 - ((idle * 100) / total);
}
#endif

void ipc_dbg_measure_throughput(unsigned long unused)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	u32 tx_bps_0, tx_bps_1;
	u32 rx_bps_0, rx_bps_1;
	int pc0, pc1;

	tx_bps_0 = tx_bps_1 = 0;
	rx_bps_0 = rx_bps_1 = 0;

	/* link0 */
	tx_bps_0 = (l1_context.device_context[0].tx_bytes * 8) /
		CONFIG_MODEM_M6718_SPI_SET_THROUGHPUT_FREQUENCY;
	rx_bps_0 = (l1_context.device_context[0].rx_bytes * 8) /
		CONFIG_MODEM_M6718_SPI_SET_THROUGHPUT_FREQUENCY;
	l1_context.device_context[0].tx_bytes = 0;
	l1_context.device_context[0].rx_bytes = 0;
	pc0 = measure_usage(&l1_context.device_context[0]);
#if IPC_NBR_SUPPORTED_SPI_LINKS > 0
	/* link1 */
	tx_bps_1 = (l1_context.device_context[1].tx_bytes * 8) /
		CONFIG_MODEM_M6718_SPI_SET_THROUGHPUT_FREQUENCY;
	rx_bps_1 = (l1_context.device_context[1].rx_bytes * 8) /
		CONFIG_MODEM_M6718_SPI_SET_THROUGHPUT_FREQUENCY;
	l1_context.device_context[1].tx_bytes = 0;
	l1_context.device_context[1].rx_bytes = 0;
	pc1 = measure_usage(&l1_context.device_context[1]);
#endif

	pr_info("IPC THROUGHPUT (bit/s): "
		"link0 TX:%8d RX:%8d %3d%% "
		"link1 TX:%8d RX:%8d %3d%%\n",
		tx_bps_0, rx_bps_0, pc0,
		tx_bps_1, rx_bps_1, pc1);

	/* restart the measurement timer */
	l1_context.tp_timer.expires = jiffies +
		(CONFIG_MODEM_M6718_SPI_SET_THROUGHPUT_FREQUENCY * HZ);
	add_timer(&l1_context.tp_timer);
#endif
}

void ipc_dbg_throughput_init(void)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	pr_info("M6718 IPC throughput measurement interval: %d\n",
		CONFIG_MODEM_M6718_SPI_SET_THROUGHPUT_FREQUENCY);
	/* init the throughput measurement timer */
	init_timer(&l1_context.tp_timer);
	l1_context.tp_timer.function = ipc_dbg_measure_throughput;
	l1_context.tp_timer.data = 0;
#endif
}

void ipc_dbg_throughput_link_init(struct ipc_link_context *context)
{
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	context->tx_bytes = 0;
	context->rx_bytes = 0;
	context->idl_measured_at = jiffies;
	context->idl_idle_enter = 0;
	context->idl_idle_total = 0;
#endif
}

