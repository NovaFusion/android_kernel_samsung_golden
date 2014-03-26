/*
 * Copyright (C) ST-Ericsson SA 2010,2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * U9500 <-> M6718 IPC protocol implementation using SPI:
 *   utility functions.
 */
#include <linux/gpio.h>
#include <linux/modem/m6718_spi/modem_driver.h>
#include "modem_util.h"

#define MODEM_COMMS_TMO_MS  (5000) /* 0 == no timeout */
#define SLAVE_STABLE_TMO_MS (1000)

#define DRIVER_NAME "ipcspi" /* name used when reserving gpio pins */


bool ipc_util_channel_is_loopback(u8 channel)
{
	return channel == MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0 ||
		channel == MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1;
}

u32 ipc_util_make_l2_header(u8 channel, u32 len)
{
	return ((channel & 0xf) << 28) | (len & 0x000fffff);
}

u8 ipc_util_get_l2_channel(u32 hdr)
{
	return hdr >> 28;
}

u32 ipc_util_get_l2_length(u32 hdr)
{
	return hdr & 0x000fffff;
}

u32 ipc_util_make_l1_header(u8 cmd, u8 counter, u32 len)
{
	return (cmd << 28) |
		((counter & 0x000000ff) << 20) |
		(len & 0x000fffff);
}

u8 ipc_util_get_l1_cmd(u32 hdr)
{
	return hdr >> 28;
}

u8 ipc_util_get_l1_counter(u32 hdr)
{
	return (hdr >> 20) & 0x000000ff;
}

u32 ipc_util_get_l1_length(u32 hdr)
{
	return hdr & 0x000fffff;
}

u8 ipc_util_get_l1_bootresp_ver(u32 bootresp)
{
	return bootresp & 0x000000ff;
}

int ipc_util_ss_level_active(struct ipc_link_context *context)
{
	return context->link->gpio.ss_active == 0 ? 0 : 1;
}

int ipc_util_ss_level_inactive(struct ipc_link_context *context)
{
	return !ipc_util_ss_level_active(context);
}

int ipc_util_int_level_active(struct ipc_link_context *context)
{
	return context->link->gpio.int_active == 0 ? 0 : 1;
}

int ipc_util_int_level_inactive(struct ipc_link_context *context)
{
	return !ipc_util_int_level_active(context);
}

void ipc_util_deactivate_ss(struct ipc_link_context *context)
{
	gpio_set_value(context->link->gpio.ss_pin,
		ipc_util_ss_level_inactive(context));

	dev_dbg(&context->sdev->dev,
		"link %d: deactivated SS\n", context->link->id);
}

void ipc_util_activate_ss(struct ipc_link_context *context)
{
	gpio_set_value(context->link->gpio.ss_pin,
		ipc_util_ss_level_active(context));

	dev_dbg(&context->sdev->dev,
		"link %d: activated SS\n", context->link->id);
}

void ipc_util_activate_ss_with_tmo(struct ipc_link_context *context)
{
	gpio_set_value(context->link->gpio.ss_pin,
		ipc_util_ss_level_active(context));

#if MODEM_COMMS_TMO_MS == 0
	dev_dbg(&context->sdev->dev,
		 "link %d: activated SS (timeout is disabled)\n",
		 context->link->id);
#else
	context->comms_timer.expires = jiffies +
		((MODEM_COMMS_TMO_MS * HZ) / 1000);
	add_timer(&context->comms_timer);

	dev_dbg(&context->sdev->dev,
		"link %d: activated SS with timeout\n", context->link->id);
#endif
}

bool ipc_util_int_is_active(struct ipc_link_context *context)
{
	return gpio_get_value(context->link->gpio.int_pin) ==
		ipc_util_int_level_active(context);
}

bool ipc_util_link_is_idle(struct ipc_link_context *context)
{
	if (context->state == NULL)
		return false;

	switch (context->state->id) {
	case IPC_SM_IDL:
		return true;
	default:
		return false;
	}
}

void ipc_util_start_slave_stable_timer(struct ipc_link_context *context)
{
	context->slave_stable_timer.expires =
		jiffies + ((SLAVE_STABLE_TMO_MS * HZ) / 1000);
	add_timer(&context->slave_stable_timer);
}

void ipc_util_spi_message_prepare(struct ipc_link_context *link_context,
	void *tx_buf, void *rx_buf, int len)
{
	struct spi_transfer *tfr = &link_context->spi_transfer;
	struct spi_message *msg = &link_context->spi_message;

	tfr->tx_buf = tx_buf;
	tfr->rx_buf = rx_buf;
	tfr->len    = len;
	msg->context = link_context;
}

void ipc_util_spi_message_init(struct ipc_link_context *link_context,
	void (*complete)(void *))
{
	struct spi_message *msg = &link_context->spi_message;
	struct spi_transfer *tfr = &link_context->spi_transfer;

	tfr->bits_per_word = 16;

	/* common init of transfer - use default from board device */
	tfr->cs_change = 0;
	tfr->speed_hz = 0;
	tfr->delay_usecs = 0;

	/* common init of message */
	spi_message_init(msg);
	msg->spi = link_context->sdev;
	msg->complete = complete;
	spi_message_add_tail(tfr, msg);
}

bool ipc_util_link_gpio_request(struct ipc_link_context *context,
	irqreturn_t (*irqhnd)(int, void*))
{
	struct spi_device *sdev = context->sdev;
	struct modem_m6718_spi_link_platform_data *link = context->link;
	unsigned long irqflags;

	if (gpio_request(link->gpio.ss_pin, DRIVER_NAME) < 0) {
		dev_err(&sdev->dev,
			"link %d error: failed to get gpio %d for SS pin\n",
			link->id,
			link->gpio.ss_pin);
		return false;
	}
	if (gpio_request(link->gpio.int_pin, DRIVER_NAME) < 0) {
		dev_err(&sdev->dev,
			"link %d error: failed to get gpio %d for INT pin\n",
			link->id,
			link->gpio.int_pin);
		return false;
	}

	if (ipc_util_int_level_active(context) == 1)
		irqflags = IRQF_TRIGGER_RISING;
	else
		irqflags = IRQF_TRIGGER_FALLING;

	if (request_irq(GPIO_TO_IRQ(link->gpio.int_pin),
			irqhnd,
			irqflags,
			DRIVER_NAME,
			context) < 0) {
		dev_err(&sdev->dev,
			"link %d error: could not get irq %d\n",
			link->id, GPIO_TO_IRQ(link->gpio.int_pin));
		return false;
	}
	return true;
}

bool ipc_util_link_gpio_config(struct ipc_link_context *context)
{
	struct spi_device *sdev = context->sdev;
	struct modem_m6718_spi_link_platform_data *link = context->link;

	if (atomic_read(&context->gpio_configured) == 1)
		return true;

	dev_dbg(&sdev->dev, "link %d: configuring GPIO\n", link->id);

	ipc_util_deactivate_ss(context);
	gpio_direction_input(link->gpio.int_pin);
	if (enable_irq_wake(GPIO_TO_IRQ(link->gpio.int_pin)) < 0) {
		dev_err(&sdev->dev,
			"link %d error: failed to enable wake on INT\n",
			link->id);
		return false;
	}

	atomic_set(&context->state_int, gpio_get_value(link->gpio.int_pin));
	atomic_set(&context->gpio_configured, 1);
	return true;
}

bool ipc_util_link_gpio_unconfig(struct ipc_link_context *context)
{
	struct spi_device *sdev = context->sdev;
	struct modem_m6718_spi_link_platform_data *link = context->link;

	if (atomic_read(&context->gpio_configured) == 0)
		return true;

	dev_dbg(&sdev->dev, "link %d: un-configuring GPIO\n", link->id);

	/* SS: output anyway, just make sure it is low */
	gpio_set_value(link->gpio.ss_pin, 0);

	/* INT: disable system-wake, reconfigure as output-low */
	disable_irq_wake(GPIO_TO_IRQ(link->gpio.int_pin));
	gpio_direction_output(link->gpio.int_pin, 0);
	atomic_set(&context->gpio_configured, 0);
	return true;
}

bool ipc_util_link_is_suspended(struct ipc_link_context *context)
{
	return atomic_read(&context->suspended) == 1;
}

void ipc_util_suspend_link(struct ipc_link_context *context)
{
	atomic_set(&context->suspended, 1);
}

void ipc_util_resume_link(struct ipc_link_context *context)
{
	atomic_set(&context->suspended, 0);
}
