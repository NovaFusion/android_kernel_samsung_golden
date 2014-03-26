/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Chris Blair <chris.blair@stericsson.com> for ST-Ericsson
 *   based on shrm_driver.h
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Modem IPC driver protocol interface header:
 *   private data
 */
#ifndef _MODEM_PRIVATE_H_
#define _MODEM_PRIVATE_H_

#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/atomic.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "modem_protocol.h"
#include "modem_statemachine.h"

#define IPC_DRIVER_VERSION       (0x02) /* APE protocol version */
#define IPC_DRIVER_MODEM_MIN_VER (0x02) /* version required from modem */

#define IPC_NBR_SUPPORTED_SPI_LINKS (2)
#define IPC_LINK_COMMON (0)
#define IPC_LINK_AUDIO  (1)

#define IPC_TX_QUEUE_MAX_SIZE (1024*1024)

#define IPC_L1_HDR_SIZE (4)
#define IPC_L2_HDR_SIZE (4)

/* tx queue item (frame) */
struct ipc_tx_queue {
	struct list_head node;
	int actual_len;
	int len;
	void *data;
	int counter;
};

/* context structure for an spi link */
struct ipc_link_context {
	struct modem_m6718_spi_link_platform_data *link;
	struct spi_device *sdev;
	atomic_t suspended;
	atomic_t gpio_configured;
	atomic_t state_int;
	spinlock_t sm_lock;
	spinlock_t tx_q_update_lock;
	atomic_t tx_q_count;
	int tx_q_free;
	struct list_head tx_q;
	int tx_frame_counter;
	const struct ipc_sm_state *state;
	u32 cmd;
	struct ipc_tx_queue *frame;
	struct spi_message spi_message;
	struct spi_transfer spi_transfer;
	struct timer_list comms_timer;
	struct timer_list slave_stable_timer;
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_VERIFY_FRAMES
	struct ipc_tx_queue *last_frame;
#endif
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	u32 tx_bytes;
	u32 rx_bytes;
	unsigned long idl_measured_at;
	unsigned long idl_idle_enter;
	unsigned long idl_idle_total;
#endif
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfsfile;
	u8 lastevent;
	u8 lastignored;
	enum ipc_sm_state_id lastignored_in;
	bool lastignored_inthis;
	int tx_q_min;
	unsigned long statesince;
#endif
};

/* context structure for the spi driver */
struct ipc_l1_context {
	bool init_done;
	atomic_t boot_sync_done;
	struct ipc_link_context device_context[IPC_NBR_SUPPORTED_SPI_LINKS];
#ifdef CONFIG_MODEM_M6718_SPI_ENABLE_FEATURE_THROUGHPUT_MEASUREMENT
	struct timer_list tp_timer;
#endif
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfsdir;
	struct dentry *debugfs_silentreset;
	bool msr_disable;
#endif
};

extern struct ipc_l1_context l1_context;

#endif /* _MODEM_PRIVATE_H_ */
