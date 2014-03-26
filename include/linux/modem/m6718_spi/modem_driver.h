/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_driver.h
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver interface header.
 */
#ifndef _MODEM_DRIVER_H_
#define _MODEM_DRIVER_H_

#include <linux/device.h>
#include <linux/modem/modem.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>


/* driver L2 mux channels */
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_LOOPBACK
#define MODEM_M6718_SPI_MAX_CHANNELS		(9)
#else
#define MODEM_M6718_SPI_MAX_CHANNELS		(3)
#endif

#define MODEM_M6718_SPI_CHN_ISI			(0)
/*#define MODEM_M6718_SPI_CHN_RPC		(1) not supported */
#define MODEM_M6718_SPI_CHN_AUDIO		(2)
/*#define MODEM_M6718_SPI_CHN_SECURITY		(3) not supported */
/*						(4) not supported */
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_LOOPBACK
#define MODEM_M6718_SPI_CHN_MASTER_LOOPBACK0	(5)
#define MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK0	(6)
#define MODEM_M6718_SPI_CHN_MASTER_LOOPBACK1	(7)
#define MODEM_M6718_SPI_CHN_SLAVE_LOOPBACK1	(8)
#endif

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
 * @channel:		L2 mux channel served by this queue
 * @fifo_base:		pointer to the respective fifo base
 * @size:		size of the data to be read
 * @free:		free space in the queue
 * @readptr:		fifo read pointer
 * @writeptr:		fifo write pointer
 * @no:			total number of messages
 * @update_lock:	spinlock for protecting the queue read operation
 * @q_rp:		queue read pointer is valid
 * @wq_readable:	wait queue head
 * @msg_list:		message list
 * @modem_spi_dev:	pointer to modem device information structure
 */
struct message_queue {
      u8 channel;
      u8 *fifo_base;
      u32 size;
      u32 free;
      u32 readptr;
      u32 writeptr;
      u32 no;
      spinlock_t update_lock;
      atomic_t q_rp;
      wait_queue_head_t wq_readable;
      struct list_head msg_list;
      struct modem_spi_dev *modem_spi_dev;
};

/**
 * struct isa_device_context - modem char interface device information
 * @dl_queue:	structre to store the queue related info
 * @device_id:	channel id (ISI, AUDIO, RPC, ...)
 * @addr:	device address
 */
struct isa_device_context {
	struct message_queue dl_queue;
	u8 device_id;
	void *addr;
};

/**
 * struct isa_driver_context - modem char interface driver information
 * @is_open:		flag to check the usage of queue
 * @isadev:		pointer to struct t_isadev_context
 * @common_tx_lock:	spinlock for protecting common channel
 * @audio_tx_mutex:	mutex for protecting audio channel
 * @cdev:		character device structre
 * @modem_class:	pointer to the class structure
 */
struct isa_driver_context {
	atomic_t is_open[MODEM_M6718_SPI_MAX_CHANNELS];
	struct isa_device_context *isadev;
	spinlock_t common_tx_lock;
	struct mutex audio_tx_mutex;
	struct cdev cdev;
	struct class *modem_class;
};

/**
 * struct modem_spi_dev - modem device information
 * @dev			pointer to device
 * @ndev		pointer to net_device interface
 * @modem		pointer to registered modem structure
 * @isa_context		pointer to char device interface
 * @netdev_flag_up:	flag to indicate up/down of network device
 * @msr_flag:		flag to indicate modem-silent-reset is in progress
 */
struct modem_spi_dev {
	struct device *dev;
	struct net_device *ndev;
	struct modem *modem;
	struct isa_driver_context *isa_context;
	int netdev_flag_up;
	bool msr_flag;
};

/**
 * struct modem_m6718_spi_link_gpio - gpio configuration for an IPC link
 * @ss_pin:	pins to use for slave-select
 * @ss_active:	active level for slave-select pin
 * @int_pin:	pin to use for slave-int (ready)
 * @int_active:	active level for slave-int
 */
struct modem_m6718_spi_link_gpio {
	int ss_pin;
	int ss_active;
	int int_pin;
	int int_active;
};

/**
 * struct modem_m6718_spi_link_platform_data - IPC link data
 * @id:		link id
 * @gpio:	link gpio configuration
 * @name:	link name (to appear in debugfs)
 */
struct modem_m6718_spi_link_platform_data {
	int id;
	struct modem_m6718_spi_link_gpio gpio;
#ifdef CONFIG_DEBUG_FS
	const char *name;
#endif
};

int modem_m6718_spi_receive(struct spi_device *sdev, u8 channel,
	u32 len, void *data);
int modem_m6718_spi_send(struct modem_spi_dev *modem_spi_dev, u8 channel,
	u32 len, void *data);
bool modem_m6718_spi_is_boot_done(void);

#endif /* _MODEM_DRIVER_H_ */
