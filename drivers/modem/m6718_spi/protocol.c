/*
 * Copyright (C) ST-Ericsson SA 2010,2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * U9500 <-> M6718 IPC protocol implementation using SPI.
 */
#include <linux/modem/m6718_spi/modem_driver.h>
#include "modem_protocol.h"
#include "modem_private.h"
#include "modem_util.h"
#include "modem_queue.h"
#include "modem_debug.h"
#include "modem_netlink.h"

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
#include <linux/workqueue.h>
#include "modem_state.h"

#define MODEM_STATE_REGISTER_TMO_MS  (500)
#endif

#ifdef WORKAROUND_DUPLICATED_IRQ
#include <linux/amba/pl022.h>
#endif

struct l2mux_channel {
	u8 open:1;
	u8 link:7;
};

/* valid open L2 mux channels */
static const struct l2mux_channel channels[255] = {
	[MODEM_M6718_SPI_CHN_ISI] = {
		.open = true,
		.link = IPC_LINK_COMMON
	},
	[MODEM_M6718_SPI_CHN_AUDIO] = {
		.open = true,
		.link = IPC_LINK_AUDIO
	},
	[MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0] = {
		.open = true,
		.link = IPC_LINK_COMMON
	},
	[MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK0] = {
		.open = true,
		.link = IPC_LINK_COMMON
	},
	[MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1] = {
		.open = true,
		.link = IPC_LINK_AUDIO
	},
	[MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK1] = {
		.open = true,
		.link = IPC_LINK_AUDIO
	}
};

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
static void modem_state_reg_wq(struct work_struct *work);
static DECLARE_DELAYED_WORK(modem_state_reg_work, modem_state_reg_wq);
#endif

/* the spi driver context */
struct ipc_l1_context l1_context = {
#ifdef CONFIG_DEBUG_FS
	.msr_disable = false,
#endif
	.init_done = false
};

bool modem_protocol_channel_is_open(u8 channel)
{
	return channels[channel].open;
}

void modem_comms_timeout(unsigned long data)
{
	ipc_sm_kick(IPC_SM_RUN_COMMS_TMO, (struct ipc_link_context *)data);
}

void slave_stable_timeout(unsigned long data)
{
	ipc_sm_kick(IPC_SM_RUN_STABLE_TMO, (struct ipc_link_context *)data);
}

/**
 * modem_protocol_init() - initialise the IPC protocol
 *
 * Initialises the IPC protocol in preparation for use. After this is called
 * the protocol is ready to be probed for each link to be supported.
 */
void modem_protocol_init(void)
{
	pr_info("M6718 IPC protocol initialising version %02x\n",
		IPC_DRIVER_VERSION);

	atomic_set(&l1_context.boot_sync_done, 0);
	ipc_dbg_debugfs_init();
	ipc_dbg_throughput_init();
	l1_context.init_done = true;
	ipc_dbg_measure_throughput(0);
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
	schedule_delayed_work(&modem_state_reg_work, 0);
#endif
}

/**
 * modem_m6718_spi_send() - send a frame using the IPC protocol
 * @modem_spi_dev: pointer to modem driver information structure
 * @channel:       L2 channel to send on
 * @len:           length of data to send
 * @data:          pointer to buffer containing data
 *
 * Check that the requested channel is supported and open, queue a frame
 * containing the data on the appropriate link and ensure the state machine
 * is running to start the transfer.
 */
int modem_m6718_spi_send(struct modem_spi_dev *modem_spi_dev, u8 channel,
	u32 len, void *data)
{
	int err;
	struct ipc_link_context *context;

	if (!channels[channel].open) {
		dev_err(modem_spi_dev->dev,
			"error: invalid channel (%d), discarding frame\n",
			channel);
		return -EINVAL;
	}

	context = &l1_context.device_context[channels[channel].link];
	if (context->state == NULL || context->state->id == IPC_SM_HALT) {
		static unsigned long linkfail_warn_time;
		if (printk_timed_ratelimit(&linkfail_warn_time, 60 * 1000))
			dev_err(modem_spi_dev->dev,
				"error: link %d for ch %d is not available, "
				"discarding frames\n",
				channels[channel].link, channel);
		return -ENODEV;
	}

	err = ipc_queue_push_frame(context, channel, len, data);
	if (err < 0)
		return err;

	if (ipc_util_link_is_idle(context)) {
		dev_dbg(modem_spi_dev->dev,
			"link %d is idle, kicking\n", channels[channel].link);
		ipc_sm_kick(IPC_SM_RUN_TX_REQ, context);
	} else {
		dev_dbg(modem_spi_dev->dev,
			"link %d is already running\n", channels[channel].link);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(modem_m6718_spi_send);

/**
 * modem_m6718_spi_is_boot_done() - check if boot handshake with modem is done
 */
bool modem_m6718_spi_is_boot_done(void)
{
	return atomic_read(&l1_context.boot_sync_done);
}
EXPORT_SYMBOL_GPL(modem_m6718_spi_is_boot_done);

/**
 * modem_protocol_is_busy() - check if the protocol is currently active
 * @sdev: pointer to spi_device for link to check
 *
 * Checks each of the IPC links to see if they are inactive: this means they
 * can be in either IDLE or INIT states. If any of the links are not idle then
 * true is returned to indicate that the protocol is busy.
 */
bool modem_protocol_is_busy(struct spi_device *sdev)
{
	int i;

	for (i = 0; i < IPC_NBR_SUPPORTED_SPI_LINKS; i++)
		switch (l1_context.device_context[i].state->id) {
		case IPC_SM_IDL:
		case IPC_SM_INIT:
		case IPC_SM_WAIT_SLAVE_STABLE:
			/* not busy; continue checking */
			break;
		default:
			dev_info(&sdev->dev, "link %d is busy\n", i);
			return true;
		}
	return false;
}

int modem_protocol_suspend(struct spi_device *sdev)
{
	struct modem_m6718_spi_link_platform_data *link =
		sdev->dev.platform_data;
	struct ipc_link_context *context;
	int link_id;

	if (link == NULL) {
		/* platform data missing in board config? */
		dev_err(&sdev->dev, "error: no platform data for link!\n");
		return -ENODEV;
	}

	link_id = link->id;
	context = &l1_context.device_context[link_id];

	if (link_id >= IPC_NBR_SUPPORTED_SPI_LINKS) {
		dev_err(&sdev->dev,
			"link %d error: too many links! (max %d)\n",
			link->id, IPC_NBR_SUPPORTED_SPI_LINKS);
		return -ENODEV;
	}

	ipc_util_suspend_link(context);
	return 0;
}

int modem_protocol_resume(struct spi_device *sdev)
{
	struct modem_m6718_spi_link_platform_data *link =
		sdev->dev.platform_data;
	struct ipc_link_context *context;
	int link_id;

	if (link == NULL) {
		/* platform data missing in board config? */
		dev_err(&sdev->dev, "error: no platform data for link!\n");
		return -ENODEV;
	}

	link_id = link->id;
	context = &l1_context.device_context[link_id];

	if (link_id >= IPC_NBR_SUPPORTED_SPI_LINKS) {
		dev_err(&sdev->dev,
			"link %d error: too many links! (max %d)\n",
			link->id, IPC_NBR_SUPPORTED_SPI_LINKS);
		return -ENODEV;
	}

	ipc_util_resume_link(context);

	/*
	 * If the resume event was an interrupt from the slave then the event
	 * is pending and we need to service it now.
	 */
	if (ipc_util_int_is_active(context)) {
		dev_dbg(&sdev->dev,
			"link %d: slave-ready is pending after resume\n",
			link_id);
		ipc_sm_kick(IPC_SM_RUN_SLAVE_IRQ, context);
	}
	return 0;
}

static void spi_tfr_complete(void *context)
{
	ipc_sm_kick(IPC_SM_RUN_TFR_COMPLETE,
		(struct ipc_link_context *)context);
}

static irqreturn_t slave_ready_irq(int irq, void *dev)
{
	struct ipc_link_context *context = (struct ipc_link_context *)dev;
	struct modem_m6718_spi_link_platform_data *link = context->link;
	struct spi_device *sdev = context->sdev;

	if (irq != GPIO_TO_IRQ(link->gpio.int_pin)) {
		dev_err(&sdev->dev,
			"link %d error: spurious slave irq!", link->id);
		return IRQ_NONE;
	}

#ifdef WORKAROUND_DUPLICATED_IRQ
	if (pl022_tfr_in_progress(sdev)) {
		dev_warn(&sdev->dev,
			"link %d warning: slave irq while transfer "
			"is active! discarding event\n", link->id);
		return IRQ_HANDLED;
	}
#endif
	ipc_sm_kick(IPC_SM_RUN_SLAVE_IRQ, context);
	return IRQ_HANDLED;
}

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_MODEM_STATE
static int modem_state_callback(unsigned long unused)
{
	int modem_state = modem_state_get_state();
	struct ipc_link_context *contexts = l1_context.device_context;
	u8 i;

	pr_info("M6718 IPC protocol modemstate reports modem is %s\n",
		modem_state_to_str(modem_state));

	switch (modem_state) {
	case MODEM_STATE_ON:
		/*
		 * Modem is on, ensure each link is configured and trigger
		 * a state change on link0 to begin handshake.
		 */
		for (i = 0; i < IPC_NBR_SUPPORTED_SPI_LINKS; i++)
			ipc_util_link_gpio_config(&contexts[i]);
		ipc_sm_kick(IPC_SM_RUN_INIT, &contexts[0]);
		break;
	case MODEM_STATE_OFF:
	case MODEM_STATE_RESET:
	case MODEM_STATE_CRASH:
		/* force all links to reset */
		for (i = 0; i < IPC_NBR_SUPPORTED_SPI_LINKS; i++)
			ipc_sm_kick(IPC_SM_RUN_RESET, &contexts[i]);
		break;
	default:
		break;
	}
	return 0;
}

static void modem_state_reg_wq(struct work_struct *work)
{
	if (modem_state_register_callback(modem_state_callback, 0) == -EAGAIN) {
		pr_info("M6718 IPC protocol failed to register with "
			"modemstate, will retry\n");
		schedule_delayed_work(&modem_state_reg_work,
			(MODEM_STATE_REGISTER_TMO_MS * HZ) / 1000);
	} else {
		pr_info("M6718 IPC protocol registered with modemstate\n");
	}
}
#endif

int modem_protocol_probe(struct spi_device *sdev)
{
	struct modem_m6718_spi_link_platform_data *link =
		sdev->dev.platform_data;
	struct ipc_link_context *context;
	int link_id;

	if (link == NULL) {
		/* platform data missing in board config? */
		dev_err(&sdev->dev, "error: no platform data for link!\n");
		return -ENODEV;
	}

	link_id = link->id;
	context = &l1_context.device_context[link_id];

	if (link_id >= IPC_NBR_SUPPORTED_SPI_LINKS) {
		dev_err(&sdev->dev,
			"link %d error: too many links! (max %d)\n",
			link->id, IPC_NBR_SUPPORTED_SPI_LINKS);
		return -ENODEV;
	}

	dev_info(&sdev->dev,
		"link %d: registering SPI link bus:%d cs:%d\n",
		link->id, sdev->master->bus_num, sdev->chip_select);

	/* update spi device with correct word size for our device */
	sdev->bits_per_word = 16;
	spi_setup(sdev);

	/* init link context */
	context->link = link;
	context->sdev = sdev;
	ipc_util_resume_link(context);
	atomic_set(&context->gpio_configured, 0);
	atomic_set(&context->state_int,
		ipc_util_int_level_inactive(context));
	spin_lock_init(&context->sm_lock);
	context->state = ipc_sm_init_state(context);
	ipc_util_spi_message_init(context, spi_tfr_complete);
	init_timer(&context->comms_timer);
	context->comms_timer.function = modem_comms_timeout;
	context->comms_timer.data = (unsigned long)context;
	init_timer(&context->slave_stable_timer);
	context->slave_stable_timer.function = slave_stable_timeout;
	context->slave_stable_timer.data = (unsigned long)context;

	if (!ipc_util_link_gpio_request(context, slave_ready_irq))
		return -ENODEV;
	if (!ipc_util_link_gpio_config(context))
		return -ENODEV;

#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_VERIFY_FRAMES
	context->last_frame = NULL;
#endif

	ipc_queue_init(context);
	ipc_dbg_debugfs_link_init(context);
	ipc_dbg_throughput_link_init(context);
	ipc_create_netlink_socket(context);

	/*
	 * For link0 (the handshake link) we force a state transition now so
	 * that it prepares for boot sync.
	 */
	if (link->id == 0)
		ipc_sm_kick(IPC_SM_RUN_INIT, context);

	/*
	 * unlikely but possible: for links other than 0, check if handshake is
	 * already complete by the time this link is probed - if so we force a
	 * state transition since the one issued by the handshake exit actions
	 * will have been ignored.
	 */
	if (link->id > 0 && atomic_read(&l1_context.boot_sync_done)) {
		dev_dbg(&sdev->dev,
			"link %d: boot sync is done, kicking state machine\n",
			link->id);
		ipc_sm_kick(IPC_SM_RUN_INIT, context);
	}
	return 0;
}

void modem_protocol_exit(void)
{
	int i;

	pr_info("M6718 IPC protocol exit\n");
	for (i = 0; i < IPC_NBR_SUPPORTED_SPI_LINKS; i++)
		ipc_util_link_gpio_unconfig(&l1_context.device_context[i]);
}
