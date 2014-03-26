/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Stefan Nilsson <stefan.xk.nilsson@stericsson.com> for ST-Ericsson.
 * Author: Martin Persson <martin.persson@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

/*
 * Mailbox nomenclature:
 *
 *       APE           MODEM
 *           mbox pairX
 *   ..........................
 *   .                       .
 *   .           peer        .
 *   .     send  ----        .
 *   .      -->  |  |        .
 *   .           |  |        .
 *   .           ----        .
 *   .                       .
 *   .           local       .
 *   .     rec   ----        .
 *   .           |  | <--    .
 *   .           |  |        .
 *   .           ----        .
 *   .........................
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mfd/db5500-prcmu.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/mbox-db5500.h>
#include <mach/reboot_reasons.h>

#define MBOX_NAME "mbox"

#define MBOX_FIFO_DATA        0x000
#define MBOX_FIFO_ADD         0x004
#define MBOX_FIFO_REMOVE      0x008
#define MBOX_FIFO_THRES_FREE  0x00C
#define MBOX_FIFO_THRES_OCCUP 0x010
#define MBOX_FIFO_STATUS      0x014

#define MBOX_DISABLE_IRQ 0x4
#define MBOX_ENABLE_IRQ  0x0
#define MBOX_LATCH 1

struct mbox_device_info {
	struct mbox *mbox;
	struct workqueue_struct *mbox_modem_rel_wq;
	struct work_struct mbox_modem_rel;
	struct completion mod_req_ack_work;
	atomic_t ape_state;
	atomic_t mod_req;
	atomic_t mod_reset;
};

/* Global list of all mailboxes */
struct hrtimer ape_timer;
struct hrtimer modem_timer;
static DEFINE_MUTEX(modem_state_mutex);
static struct list_head mboxs = LIST_HEAD_INIT(mboxs);
static struct mbox_device_info *mb;

static enum hrtimer_restart mbox_ape_callback(struct hrtimer *hrtimer)
{
	queue_work(mb->mbox_modem_rel_wq, &mb->mbox_modem_rel);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart mbox_mod_callback(struct hrtimer *hrtimer)
{
	atomic_set(&mb->ape_state, 0);
	return HRTIMER_NORESTART;
}

static void mbox_modem_rel_work(struct work_struct *work)
{
	mutex_lock(&modem_state_mutex);
	prcmu_modem_rel();
	atomic_set(&mb->mod_req, 0);
	mutex_unlock(&modem_state_mutex);
}

static void mbox_modem_req(void)
{
	mutex_lock(&modem_state_mutex);
	if (!db5500_prcmu_is_modem_requested()) {
		prcmu_modem_req();
		/* TODO: optimize this timeout */
		if (!wait_for_completion_timeout(&mb->mod_req_ack_work,
					msecs_to_jiffies(2000)))
			printk(KERN_ERR "mbox:modem_req_ack timedout(2sec)\n");
	}
	atomic_set(&mb->mod_req, 1);
	mutex_unlock(&modem_state_mutex);
}

static struct mbox *get_mbox_with_id(u8 id)
{
	u8 i;
	struct list_head *pos = &mboxs;
	for (i = 0; i <= id; i++)
		pos = pos->next;

	return (struct mbox *) list_entry(pos, struct mbox, list);
}

int mbox_send(struct mbox *mbox, u32 mbox_msg, bool block)
{
	int res = 0;
	unsigned long flag;

	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev,
				"mbox_send called after modem reset\n");
		return -EINVAL;
	}
	dev_dbg(&(mbox->pdev->dev),
		"About to buffer 0x%X to mailbox 0x%X."
		" ri = %d, wi = %d\n",
		mbox_msg, (u32)mbox, mbox->read_index,
		mbox->write_index);

	/* Request for modem */
	if (!db5500_prcmu_is_modem_requested())
		mbox_modem_req();

	spin_lock_irqsave(&mbox->lock, flag);
	/* Check if write buffer is full */
	while (((mbox->write_index + 1) % MBOX_BUF_SIZE) == mbox->read_index) {
		if (!block) {
			dev_dbg(&(mbox->pdev->dev),
			"Buffer full in non-blocking call! "
			"Returning -ENOMEM!\n");
			res = -ENOMEM;
			goto exit;
		}
		spin_unlock_irqrestore(&mbox->lock, flag);
		dev_dbg(&(mbox->pdev->dev),
			"Buffer full in blocking call! Sleeping...\n");
		mbox->client_blocked = 1;
		wait_for_completion(&mbox->buffer_available);
		dev_dbg(&(mbox->pdev->dev),
			"Blocking send was woken up! Trying again...\n");
		spin_lock_irqsave(&mbox->lock, flag);
	}

	mbox->buffer[mbox->write_index] = mbox_msg;
	mbox->write_index = (mbox->write_index + 1) % MBOX_BUF_SIZE;

	/*
	 * Indicate that we want an IRQ as soon as there is a slot
	 * in the FIFO
	 */
	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev,
				"modem is in reset state, cannot proceed\n");
		res  = -EINVAL;
		goto exit;
	}
	writel(MBOX_ENABLE_IRQ, mbox->virtbase_peer + MBOX_FIFO_THRES_FREE);

exit:
	spin_unlock_irqrestore(&mbox->lock, flag);
	return res;
}
EXPORT_SYMBOL(mbox_send);

#if defined(CONFIG_DEBUG_FS)
/*
 * Expected input: <value> <nbr sends>
 * Example: "echo 0xdeadbeef 4 > mbox-node" sends 0xdeadbeef 4 times
 */
static ssize_t mbox_write_fifo(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	unsigned long mbox_mess;
	unsigned long nbr_sends;
	unsigned long i;
	char int_buf[16];
	char *token;
	char *val;

	struct platform_device *pdev = to_platform_device(dev);
	struct mbox *mbox = platform_get_drvdata(pdev);

	strncpy((char *) &int_buf, buf, sizeof(int_buf));
	token = (char *) &int_buf;

	/* Parse message */
	val = strsep(&token, " ");
	if ((val == NULL) || (strict_strtoul(val, 16, &mbox_mess) != 0))
		mbox_mess = 0xDEADBEEF;

	val = strsep(&token, " ");
	if ((val == NULL) || (strict_strtoul(val, 10, &nbr_sends) != 0))
		nbr_sends = 1;

	dev_dbg(dev, "Will write 0x%lX %ld times using data struct at 0x%X\n",
		mbox_mess, nbr_sends, (u32) mbox);

	for (i = 0; i < nbr_sends; i++)
		mbox_send(mbox, mbox_mess, true);

	return count;
}

static ssize_t mbox_read_fifo(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	int mbox_value;
	struct platform_device *pdev = to_platform_device(dev);
	struct mbox *mbox = platform_get_drvdata(pdev);

	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev, "modem crashed, returning\n");
		return 0;
	}
	if ((readl(mbox->virtbase_local + MBOX_FIFO_STATUS) & 0x7) <= 0)
		return sprintf(buf, "Mailbox is empty\n");

	mbox_value = readl(mbox->virtbase_local + MBOX_FIFO_DATA);
	writel(MBOX_LATCH, (mbox->virtbase_local + MBOX_FIFO_REMOVE));

	return sprintf(buf, "0x%X\n", mbox_value);
}

static DEVICE_ATTR(fifo, S_IWUGO | S_IRUGO, mbox_read_fifo, mbox_write_fifo);

static int mbox_show(struct seq_file *s, void *data)
{
	struct list_head *pos;
	u8 mbox_index = 0;

	list_for_each(pos, &mboxs) {
		struct mbox *m =
			(struct mbox *) list_entry(pos, struct mbox, list);
		if (m == NULL) {
			seq_printf(s,
				   "Unable to retrieve mailbox %d\n",
				   mbox_index);
			continue;
		}

		spin_lock(&m->lock);
		if ((m->virtbase_peer == NULL) || (m->virtbase_local == NULL)) {
			seq_printf(s, "MAILBOX %d not setup or corrupt\n",
				   mbox_index);
			spin_unlock(&m->lock);
			continue;
		}

		if (atomic_read(&mb->mod_reset)) {
			dev_err(&m->pdev->dev, "modem crashed, returning\n");
			spin_unlock(&m->lock);
			return 0;
		}
		seq_printf(s,
		"===========================\n"
		" MAILBOX %d\n"
		" PEER MAILBOX DUMP\n"
		"---------------------------\n"
		"FIFO:                 0x%X (%d)\n"
		"Free     Threshold:   0x%.2X (%d)\n"
		"Occupied Threshold:   0x%.2X (%d)\n"
		"Status:               0x%.2X (%d)\n"
		"   Free spaces  (ot):    %d (%d)\n"
		"   Occup spaces (ot):    %d (%d)\n"
		"===========================\n"
		" LOCAL MAILBOX DUMP\n"
		"---------------------------\n"
		"FIFO:                 0x%.X (%d)\n"
		"Free     Threshold:   0x%.2X (%d)\n"
		"Occupied Threshold:   0x%.2X (%d)\n"
		"Status:               0x%.2X (%d)\n"
		"   Free spaces  (ot):    %d (%d)\n"
		"   Occup spaces (ot):    %d (%d)\n"
		"===========================\n"
		"write_index: %d\n"
		"read_index : %d\n"
		"===========================\n"
		"\n",
		mbox_index,
		readl(m->virtbase_peer + MBOX_FIFO_DATA),
		readl(m->virtbase_peer + MBOX_FIFO_DATA),
		readl(m->virtbase_peer + MBOX_FIFO_THRES_FREE),
		readl(m->virtbase_peer + MBOX_FIFO_THRES_FREE),
		readl(m->virtbase_peer + MBOX_FIFO_THRES_OCCUP),
		readl(m->virtbase_peer + MBOX_FIFO_THRES_OCCUP),
		readl(m->virtbase_peer + MBOX_FIFO_STATUS),
		readl(m->virtbase_peer + MBOX_FIFO_STATUS),
		(readl(m->virtbase_peer + MBOX_FIFO_STATUS) >> 4) & 0x7,
		(readl(m->virtbase_peer + MBOX_FIFO_STATUS) >> 7) & 0x1,
		(readl(m->virtbase_peer + MBOX_FIFO_STATUS) >> 0) & 0x7,
		(readl(m->virtbase_peer + MBOX_FIFO_STATUS) >> 3) & 0x1,
		readl(m->virtbase_local + MBOX_FIFO_DATA),
		readl(m->virtbase_local + MBOX_FIFO_DATA),
		readl(m->virtbase_local + MBOX_FIFO_THRES_FREE),
		readl(m->virtbase_local + MBOX_FIFO_THRES_FREE),
		readl(m->virtbase_local + MBOX_FIFO_THRES_OCCUP),
		readl(m->virtbase_local + MBOX_FIFO_THRES_OCCUP),
		readl(m->virtbase_local + MBOX_FIFO_STATUS),
		readl(m->virtbase_local + MBOX_FIFO_STATUS),
		(readl(m->virtbase_local + MBOX_FIFO_STATUS) >> 4) & 0x7,
		(readl(m->virtbase_local + MBOX_FIFO_STATUS) >> 7) & 0x1,
		(readl(m->virtbase_local + MBOX_FIFO_STATUS) >> 0) & 0x7,
		(readl(m->virtbase_local + MBOX_FIFO_STATUS) >> 3) & 0x1,
		m->write_index, m->read_index);
		mbox_index++;
		spin_unlock(&m->lock);
	}

	return 0;
}

static int mbox_open(struct inode *inode, struct file *file)
{
	return single_open(file, mbox_show, NULL);
}

static const struct file_operations mbox_operations = {
	.owner = THIS_MODULE,
	.open = mbox_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static irqreturn_t mbox_irq(int irq, void *arg)
{
	u32 mbox_value;
	int nbr_occup;
	int nbr_free;
	struct mbox *mbox = (struct mbox *) arg;

	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev, "modem in reset state\n");
		return IRQ_HANDLED;
	}
	spin_lock(&mbox->lock);

	dev_dbg(&(mbox->pdev->dev),
		"mbox IRQ [%d] received. ri = %d, wi = %d\n",
		irq, mbox->read_index, mbox->write_index);

	/*
	 * Check if we have any outgoing messages, and if there is space for
	 * them in the FIFO.
	 */
	if (mbox->read_index != mbox->write_index) {
		/*
		 * Check by reading FREE for LOCAL since that indicates
		 * OCCUP for PEER
		 */
		nbr_free = (readl(mbox->virtbase_local + MBOX_FIFO_STATUS)
			    >> 4) & 0x7;
		dev_dbg(&(mbox->pdev->dev),
			"Status indicates %d empty spaces in the FIFO!\n",
			nbr_free);

		while ((nbr_free > 0) &&
		       (mbox->read_index != mbox->write_index)) {
			if (atomic_read(&mb->mod_reset)) {
				dev_err(&mbox->pdev->dev,
						"modem in reset state\n");
				goto exit;
			}
			/* Write the message and latch it into the FIFO */
			writel(mbox->buffer[mbox->read_index],
			       (mbox->virtbase_peer + MBOX_FIFO_DATA));
			writel(MBOX_LATCH,
			       (mbox->virtbase_peer + MBOX_FIFO_ADD));
			dev_dbg(&(mbox->pdev->dev),
				"Wrote message 0x%X to addr 0x%X\n",
				mbox->buffer[mbox->read_index],
				(u32) (mbox->virtbase_peer + MBOX_FIFO_DATA));

			nbr_free--;
			mbox->read_index =
				(mbox->read_index + 1) % MBOX_BUF_SIZE;
		}

		if (atomic_read(&mb->mod_reset)) {
			dev_err(&mbox->pdev->dev, "modem in reset state\n");
			goto exit;
		}
		/*
		 * Check if we still want IRQ:s when there is free
		 * space to send
		 */
		if (mbox->read_index != mbox->write_index) {
			dev_dbg(&(mbox->pdev->dev),
				"Still have messages to send, but FIFO full. "
				"Request IRQ again!\n");
			writel(MBOX_ENABLE_IRQ,
			       mbox->virtbase_peer + MBOX_FIFO_THRES_FREE);
		} else {
			dev_dbg(&(mbox->pdev->dev),
				"No more messages to send. "
				"Do not request IRQ again!\n");
			writel(MBOX_DISABLE_IRQ,
			       mbox->virtbase_peer + MBOX_FIFO_THRES_FREE);
		}

		/*
		 * Check if we can signal any blocked clients that it is OK to
		 * start buffering again
		 */
		if (mbox->client_blocked &&
		    (((mbox->write_index + 1) % MBOX_BUF_SIZE)
		     != mbox->read_index)) {
			dev_dbg(&(mbox->pdev->dev),
				"Waking up blocked client\n");
			complete(&mbox->buffer_available);
			mbox->client_blocked = 0;
		}
	}

	/* Start timer and on timer expiry call modem_rel */
	hrtimer_start(&ape_timer, ktime_set(0, 10*NSEC_PER_MSEC),
			HRTIMER_MODE_REL);

	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev, "modem in reset state\n");
		goto exit;
	}
	/* Check if we have any incoming messages */
	nbr_occup = readl(mbox->virtbase_local + MBOX_FIFO_STATUS) & 0x7;
	if (nbr_occup == 0)
		goto exit;

redo:
	if (mbox->cb == NULL) {
		dev_dbg(&(mbox->pdev->dev), "No receive callback registered, "
			"leaving %d incoming messages in fifo!\n", nbr_occup);
		goto exit;
	}
	atomic_set(&mb->ape_state, 1);

	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev, "modem in reset state\n");
		goto exit;
	}
	/* Read and acknowledge the message */
	mbox_value = readl(mbox->virtbase_local + MBOX_FIFO_DATA);
	writel(MBOX_LATCH, (mbox->virtbase_local + MBOX_FIFO_REMOVE));

	/* Notify consumer of new mailbox message */
	dev_dbg(&(mbox->pdev->dev), "Calling callback for message 0x%X!\n",
		mbox_value);
	mbox->cb(mbox_value, mbox->client_data);

	nbr_occup = readl(mbox->virtbase_local + MBOX_FIFO_STATUS) & 0x7;

	if (nbr_occup > 0)
		goto redo;

	/* Start a timer and timer expiry will be the criteria for sleep */
	hrtimer_start(&modem_timer, ktime_set(0, 100*MSEC_PER_SEC),
			HRTIMER_MODE_REL);
exit:
	dev_dbg(&(mbox->pdev->dev), "Exit mbox IRQ. ri = %d, wi = %d\n",
		mbox->read_index, mbox->write_index);
	spin_unlock(&mbox->lock);

	return IRQ_HANDLED;
}

static void mbox_shutdown(struct mbox *mbox)
{
	if (!mbox->allocated)
		return;
#if defined(CONFIG_DEBUG_FS)
	debugfs_remove(mbox->dentry);
	device_remove_file(&mbox->pdev->dev, &dev_attr_fifo);
#endif
	/* TODO: Need to check if we can write after modem reset */
	if (!atomic_read(&mb->mod_reset)) {
		writel(MBOX_DISABLE_IRQ, mbox->virtbase_local +
				MBOX_FIFO_THRES_OCCUP);
		writel(MBOX_DISABLE_IRQ, mbox->virtbase_peer +
				MBOX_FIFO_THRES_FREE);
	}
	free_irq(mbox->irq, (void *)mbox);
	mbox->client_blocked = 0;
	iounmap(mbox->virtbase_local);
	iounmap(mbox->virtbase_peer);
	mbox->cb = NULL;
	mbox->client_data = NULL;
	mbox->allocated = false;
}

/** mbox_state_reset - Reset the mailbox state machine
 *
 * This function is called on receiving modem reset interrupt. Reset all
 * the mailbox state machine, disable irq, cancel timers, shutdown the
 * mailboxs and re-enable irq's.
 */
void mbox_state_reset(void)
{
	struct mbox *mbox = mb->mbox;

	/* Common for all mailbox */
	atomic_set(&mb->mod_reset, 1);

	/* Disable IRQ */
	disable_irq_nosync(IRQ_DB5500_PRCMU_AC_WAKE_ACK);

	/* Cancel sleep_req timers */
	hrtimer_cancel(&modem_timer);
	hrtimer_cancel(&ape_timer);

	/* specific to each mailbox */
	list_for_each_entry(mbox, &mboxs, list) {
		mbox_shutdown(mbox);
	}

	/* Reset mailbox state machine */
	atomic_set(&mb->mod_req, 0);
	atomic_set(&mb->ape_state, 0);

	/* Enable irq */
	enable_irq(IRQ_DB5500_PRCMU_AC_WAKE_ACK);
}


/* Setup is executed once for each mbox pair */
struct mbox *mbox_setup(u8 mbox_id, mbox_recv_cb_t *mbox_cb, void *priv)
{
	struct resource *resource;
	int res;
	struct mbox *mbox;

	/*
	 * set mod_reset flag to '0', clients calling this APE should make sure
	 * that modem is rebooted after MSR. Mailbox doesnt have any means of
	 * knowing the boot status of modem.
	 */
	atomic_set(&mb->mod_reset, 0);

	mbox = get_mbox_with_id(mbox_id);
	if (mbox == NULL) {
		dev_err(&(mbox->pdev->dev), "Incorrect mailbox id: %d!\n",
			mbox_id);
		goto exit;
	}

	/*
	 * Check if mailbox has been allocated to someone else,
	 * otherwise allocate it
	 */
	if (mbox->allocated) {
		dev_err(&(mbox->pdev->dev), "Mailbox number %d is busy!\n",
			mbox_id);
		mbox = NULL;
		goto exit;
	}
	mbox->allocated = true;

	dev_dbg(&(mbox->pdev->dev), "Initiating mailbox number %d: 0x%X...\n",
		mbox_id, (u32)mbox);

	mbox->client_data = priv;
	mbox->cb = mbox_cb;

	/* Get addr for peer mailbox and ioremap it */
	resource = platform_get_resource_byname(mbox->pdev,
						IORESOURCE_MEM,
						"mbox_peer");
	if (resource == NULL) {
		dev_err(&(mbox->pdev->dev),
			"Unable to retrieve mbox peer resource\n");
		mbox = NULL;
		goto free_mbox;
	}
	dev_dbg(&(mbox->pdev->dev),
		"Resource name: %s start: 0x%X, end: 0x%X\n",
		resource->name, resource->start, resource->end);
	mbox->virtbase_peer = ioremap(resource->start, resource_size(resource));
	if (!mbox->virtbase_peer) {
		dev_err(&(mbox->pdev->dev), "Unable to ioremap peer mbox\n");
		mbox = NULL;
		goto free_mbox;
	}
	dev_dbg(&(mbox->pdev->dev),
		"ioremapped peer physical: (0x%X-0x%X) to virtual: 0x%X\n",
		resource->start, resource->end, (u32) mbox->virtbase_peer);

	/* Get addr for local mailbox and ioremap it */
	resource = platform_get_resource_byname(mbox->pdev,
						IORESOURCE_MEM,
						"mbox_local");
	if (resource == NULL) {
		dev_err(&(mbox->pdev->dev),
			"Unable to retrieve mbox local resource\n");
		mbox = NULL;
		goto free_map;
	}
	dev_dbg(&(mbox->pdev->dev),
		"Resource name: %s start: 0x%X, end: 0x%X\n",
		resource->name, resource->start, resource->end);
	mbox->virtbase_local = ioremap(resource->start, resource_size(resource));
	if (!mbox->virtbase_local) {
		dev_err(&(mbox->pdev->dev), "Unable to ioremap local mbox\n");
		mbox = NULL;
		goto free_map;
	}
	dev_dbg(&(mbox->pdev->dev),
		"ioremapped local physical: (0x%X-0x%X) to virtual: 0x%X\n",
		resource->start, resource->end, (u32) mbox->virtbase_peer);

	init_completion(&mbox->buffer_available);
	mbox->client_blocked = 0;

	/* Get IRQ for mailbox and allocate it */
	mbox->irq = platform_get_irq_byname(mbox->pdev, "mbox_irq");
	if (mbox->irq < 0) {
		dev_err(&(mbox->pdev->dev),
			"Unable to retrieve mbox irq resource\n");
		mbox = NULL;
		goto free_map1;
	}

	dev_dbg(&(mbox->pdev->dev), "Allocating irq %d...\n", mbox->irq);
	res = request_threaded_irq(mbox->irq, NULL, mbox_irq,
			IRQF_NO_SUSPEND | IRQF_ONESHOT,
			mbox->name, (void *) mbox);
	if (res < 0) {
		dev_err(&(mbox->pdev->dev),
			"Unable to allocate mbox irq %d\n", mbox->irq);
		mbox = NULL;
		goto exit;
	}

	/* check if modem has reset */
	if (atomic_read(&mb->mod_reset)) {
		dev_err(&mbox->pdev->dev,
				"modem is in reset state, cannot proceed\n");
		mbox = NULL;
		goto free_irq;
	}
	/* Set up mailbox to not launch IRQ on free space in mailbox */
	writel(MBOX_DISABLE_IRQ, mbox->virtbase_peer + MBOX_FIFO_THRES_FREE);

	/*
	 * Set up mailbox to launch IRQ on new message if we have
	 * a callback set. If not, do not raise IRQ, but keep message
	 * in FIFO for manual retrieval
	 */
	if (mbox_cb != NULL)
		writel(MBOX_ENABLE_IRQ,
		       mbox->virtbase_local + MBOX_FIFO_THRES_OCCUP);
	else
		writel(MBOX_DISABLE_IRQ,
		       mbox->virtbase_local + MBOX_FIFO_THRES_OCCUP);

#if defined(CONFIG_DEBUG_FS)
	res = device_create_file(&(mbox->pdev->dev), &dev_attr_fifo);
	if (res != 0)
		dev_warn(&(mbox->pdev->dev),
			 "Unable to create mbox sysfs entry");

	mbox->dentry = debugfs_create_file("mbox", S_IFREG | S_IRUGO, NULL,
				   NULL, &mbox_operations);
#endif
	dev_info(&(mbox->pdev->dev),
		 "Mailbox driver with index %d initiated!\n", mbox_id);

	return mbox;
free_irq:
	free_irq(mbox->irq, (void *)mbox);
free_map1:
	iounmap(mbox->virtbase_local);
free_map:
	iounmap(mbox->virtbase_peer);
free_mbox:
	mbox->client_data = NULL;
	mbox->cb = NULL;
exit:
	return mbox;
}
EXPORT_SYMBOL(mbox_setup);

static irqreturn_t mbox_prcmu_mod_req_ack_handler(int irq, void *data)
{
	complete(&mb->mod_req_ack_work);
	return IRQ_HANDLED;
}

int __init mbox_probe(struct platform_device *pdev)
{
	struct mbox *mbox;
	int res = 0;
	dev_dbg(&(pdev->dev), "Probing mailbox (pdev = 0x%X)...\n", (u32) pdev);

	mbox = kzalloc(sizeof(struct mbox), GFP_KERNEL);
	if (mbox == NULL) {
		dev_err(&pdev->dev,
				"Could not allocate memory for struct mbox\n");
		return -ENOMEM;
	}

	mbox->pdev = pdev;
	mbox->write_index = 0;
	mbox->read_index = 0;

	INIT_LIST_HEAD(&(mbox->list));
	list_add_tail(&(mbox->list), &mboxs);

	sprintf(mbox->name, "%s", MBOX_NAME);
	spin_lock_init(&mbox->lock);

	platform_set_drvdata(pdev, mbox);
	mb->mbox = mbox;
	dev_info(&(pdev->dev), "Mailbox driver loaded\n");

	return res;
}

static int __exit mbox_remove(struct platform_device *pdev)
{
	struct mbox *mbox = platform_get_drvdata(pdev);

	hrtimer_cancel(&ape_timer);
	hrtimer_cancel(&modem_timer);
	mbox_shutdown(mbox);
	list_del(&mbox->list);
	kfree(mbox);
	return 0;
}

#ifdef CONFIG_PM
int mbox_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mbox *mbox = platform_get_drvdata(pdev);

	/*
	 * Nothing to be done for now, once APE-Modem power management is
	 * in place communication will have to be stopped.
	 */

	list_for_each_entry(mbox, &mboxs, list) {
		if (mbox->client_blocked)
			return -EBUSY;
	}
	dev_dbg(dev, "APE_STATE = %d\n", atomic_read(&mb->ape_state));
	dev_dbg(dev, "MODEM_STATE = %d\n", db5500_prcmu_is_modem_requested());
	if (atomic_read(&mb->ape_state) || db5500_prcmu_is_modem_requested() ||
			atomic_read(&mb->mod_req))
		return -EBUSY;
	return 0;
}

int mbox_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mbox *mbox = platform_get_drvdata(pdev);

	/*
	 * Nothing to be done for now, once APE-Modem power management is
	 * in place communication will have to be resumed.
	 */

	return 0;
}

static const struct dev_pm_ops mbox_dev_pm_ops = {
	.suspend_noirq = mbox_suspend,
	.resume_noirq = mbox_resume,
};
#endif

static struct platform_driver mbox_driver = {
	.remove = __exit_p(mbox_remove),
	.driver = {
		.name = MBOX_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &mbox_dev_pm_ops,
#endif
	},
};

static int __init mbox_init(void)
{
	struct mbox_device_info *mb_di;
	int err;

	mb_di = kzalloc(sizeof(struct mbox_device_info), GFP_KERNEL);
	if (mb_di == NULL) {
		printk(KERN_ERR
			"mbox:Could not allocate memory for struct mbox_device_info\n");
		return -ENOMEM;
	}

	mb_di->mbox_modem_rel_wq = create_singlethread_workqueue(
			"mbox_modem_rel");
	if (!mb_di->mbox_modem_rel_wq) {
		printk(KERN_ERR "mbox:failed to create work queue\n");
		err = -ENOMEM;
		goto free_mem;
	}

	INIT_WORK(&mb_di->mbox_modem_rel, mbox_modem_rel_work);

	hrtimer_init(&ape_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ape_timer.function = mbox_ape_callback;
	hrtimer_init(&modem_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	modem_timer.function = mbox_mod_callback;

	atomic_set(&mb_di->ape_state, 0);
	atomic_set(&mb_di->mod_req, 0);
	atomic_set(&mb_di->mod_reset, 0);

	err = request_irq(IRQ_DB5500_PRCMU_AC_WAKE_ACK,
			mbox_prcmu_mod_req_ack_handler,
			IRQF_NO_SUSPEND, "mod_req_ack", NULL);
	if (err < 0) {
		printk(KERN_ERR "mbox:Failed alloc IRQ_PRCMU_CA_SLEEP.\n");
		goto free_irq;
	}

	init_completion(&mb_di->mod_req_ack_work);
	mb = mb_di;
	return platform_driver_probe(&mbox_driver, mbox_probe);
free_irq:
	destroy_workqueue(mb_di->mbox_modem_rel_wq);
free_mem:
	kfree(mb_di);
	return err;
}

module_init(mbox_init);

void __exit mbox_exit(void)
{
	free_irq(IRQ_DB5500_PRCMU_AC_WAKE_ACK, NULL);
	destroy_workqueue(mb->mbox_modem_rel_wq);
	platform_driver_unregister(&mbox_driver);
	kfree(mb);
}

module_exit(mbox_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MBOX driver");
