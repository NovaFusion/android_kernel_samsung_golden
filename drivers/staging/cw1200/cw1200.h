/*
 * Common private data for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on the mac80211 Prism54 code, which is
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_H
#define CW1200_H

#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <net/mac80211.h>

#include "queue.h"
#include "wsm.h"
#include "scan.h"
#include "txrx.h"
#include "ht.h"

/* extern */ struct sbus_ops;
/* extern */ struct task_struct;
/* extern */ struct cw1200_debug_priv;

#if defined(CONFIG_CW1200_TXRX_DEBUG)
#define txrx_printk(...) printk(__VA_ARGS__)
#else
#define txrx_printk(...)
#endif

#define CW1200_MAX_CTRL_FRAME_LEN	(0x1000)

#define CW1200_MAX_STA_IN_AP_MODE	(5)
#define CW1200_LINK_ID_AFTER_DTIM	(CW1200_MAX_STA_IN_AP_MODE + 1)

enum cw1200_join_status {
	CW1200_JOIN_STATUS_PASSIVE = 0,
	CW1200_JOIN_STATUS_MONITOR,
	CW1200_JOIN_STATUS_STA,
	CW1200_JOIN_STATUS_AP,
};

struct cw1200_common {
	struct cw1200_queue		tx_queue[4];
	struct cw1200_queue_stats	tx_queue_stats;
	struct cw1200_debug_priv	*debug;

	struct ieee80211_hw		*hw;
	struct ieee80211_vif		*vif;
	struct device			*pdev;
	struct workqueue_struct		*workqueue;

	struct mutex			conf_mutex;

	const struct sbus_ops		*sbus_ops;
	struct sbus_priv		*sbus_priv;

	/* HW type (HIF_...) */
	int				hw_type;
	int				hw_revision;

	/* firmware/hardware info */
	unsigned int tx_hdr_len;

	/* Radio data */
	int output_power;
	int noise;

	/* calibration, output power limit and rssi<->dBm conversation data */

	/* BBP/MAC state */
	struct ieee80211_rate		*rates;
	struct ieee80211_rate		*mcs_rates;
	u8 mac_addr[ETH_ALEN];
	struct ieee80211_channel	*channel;
	u8 bssid[ETH_ALEN];
	struct wsm_edca_params		edca;
	struct wsm_association_mode	association_mode;
	struct wsm_set_bss_params	bss_params;
	struct cw1200_ht_info		ht_info;
	struct wsm_set_pm		powersave_mode;
	int				cqm_rssi_thold;
	unsigned			cqm_rssi_hyst;
	unsigned			cqm_tx_failure_thold;
	unsigned			cqm_tx_failure_count;
	int				cqm_link_loss_count;
	int				cqm_beacon_loss_count;
	int				channel_switch_in_progress;
	wait_queue_head_t		channel_switch_done;
	u8				long_frame_max_tx_count;
	u8				short_frame_max_tx_count;
	int				mode;
	bool				enable_beacon;
	size_t				ssid_length;
	u8				ssid[IEEE80211_MAX_SSID_LEN];
	bool				listening;
	struct wsm_rx_filter		rx_filter;
	struct wsm_beacon_filter_control bf_control;
	u8				ba_tid_mask;

	/* BH */
	atomic_t			bh_rx;
	atomic_t			bh_tx;
	atomic_t			bh_term;
	struct task_struct		*bh_thread;
	int				bh_error;
	wait_queue_head_t		bh_wq;
	int				buf_id_tx;	/* byte */
	int				buf_id_rx;	/* byte */
	int				wsm_rx_seq;	/* byte */
	int				wsm_tx_seq;	/* byte */
	int				hw_bufs_used;
	wait_queue_head_t		hw_bufs_used_wq;
	struct sk_buff			*skb_cache;
	bool				powersave_enabled;
	bool				device_can_sleep;

	/* WSM */
	struct wsm_caps			wsm_caps;
	struct mutex			wsm_cmd_mux;
	struct wsm_buf			wsm_cmd_buf;
	struct wsm_cmd			wsm_cmd;
	wait_queue_head_t		wsm_cmd_wq;
	wait_queue_head_t		wsm_startup_done;
	struct wsm_cbc			wsm_cbc;
	atomic_t			tx_lock;

	/* Scan status */
	struct cw1200_scan scan;

	/* WSM Join */
	enum cw1200_join_status	join_status;
	u8			join_bssid[ETH_ALEN];
	const struct wsm_tx	*join_pending_frame;
	struct work_struct	join_work;
	struct delayed_work	join_timeout;
	struct work_struct	unjoin_work;
	int			join_dtim_period;

	/* TX/RX and security */
	s8			wep_default_key_id;
	struct work_struct	wep_key_work;
	u32			key_map;
	struct wsm_add_key	keys[WSM_KEY_MAX_INDEX + 1];
	unsigned long		rx_timestamp;

	/* AP powersave */
	u32			link_id_map;
	u32			tx_suspend_mask[4];
	u32			sta_asleep_mask;
	bool			suspend_multicast;
	struct work_struct	set_tim_work;
	struct work_struct	multicast_start_work;
	struct work_struct	multicast_stop_work;


	/* WSM events and CQM implementation */
	spinlock_t		event_queue_lock;
	struct list_head	event_queue;
	struct work_struct	event_handler;
	struct delayed_work	bss_loss_work;
	struct delayed_work	connection_loss_work;
#if defined(CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
	struct delayed_work	keep_alive_work;
	unsigned long		last_activity_time;
#endif /* CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
	struct work_struct	tx_failure_work;
	int			delayed_link_loss;

	/* TX rate policy cache */
	struct tx_policy_cache tx_policy_cache;
	struct work_struct tx_policy_upload_work;

	/* cryptographic engine information */

	/* bit field of glowing LEDs */
	u16 softled_state;

	/* statistics */
	struct ieee80211_low_level_stats stats;
};

struct cw1200_sta_priv {
        int link_id;
};

/* interfaces for the drivers */
int cw1200_probe(const struct sbus_ops *sbus_ops,
		 struct sbus_priv *sbus,
		 struct device *pdev,
		 struct cw1200_common **pself);
void cw1200_release(struct cw1200_common *self);

#define CW1200_DBG_MSG		0x00000001
#define CW1200_DBG_NIY		0x00000002
#define CW1200_DBG_SBUS		0x00000004
#define CW1200_DBG_INIT		0x00000008
#define CW1200_DBG_ERROR	0x00000010
#define CW1200_DBG_LEVEL	0xFFFFFFFF

#define cw1200_dbg(level, ...)				\
	do {						\
		if ((level) & CW1200_DBG_LEVEL)		\
			printk(KERN_DEBUG __VA_ARGS__);	\
	} while (0)

#define STUB()						\
	do {						\
		cw1200_dbg(CW1200_DBG_NIY, "%s: STUB at line %d.\n", \
		__func__, __LINE__);			\
	} while (0)

#endif /* CW1200_H */
