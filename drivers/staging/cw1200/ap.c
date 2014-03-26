/*
 * mac80211 STA and AP API for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "cw1200.h"
#include "sta.h"
#include "ap.h"
#include "bh.h"

#if defined(CONFIG_CW1200_STA_DEBUG)
#define ap_printk(...) printk(__VA_ARGS__)
#else
#define ap_printk(...)
#endif

static int cw1200_upload_beacon(struct cw1200_common *priv);
static int cw1200_start_ap(struct cw1200_common *priv);
static int cw1200_update_beaconing(struct cw1200_common *priv);


/* ******************************************************************** */
/* AP API								*/

int cw1200_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                   struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = hw->priv;
	struct cw1200_sta_priv *sta_priv =
			(struct cw1200_sta_priv *)&sta->drv_priv;
	struct wsm_map_link map_link = {
		.link_id = 0,
	};


	/* Link ID mapping works fine in STA mode as well.
	 * It's better to keep same handling for both STA ans AP modes */
#if 0
	if (priv->mode != NL80211_IFTYPE_AP)
		return 0;
#endif

	map_link.link_id = ffs(~(priv->link_id_map | 1)) - 1;
	if (map_link.link_id > CW1200_MAX_STA_IN_AP_MODE) {
		sta_priv->link_id = 0;
		printk(KERN_INFO "[AP] No more link ID available.\n");
		return -ENOENT;
	}

	memcpy(map_link.mac_addr, sta->addr, ETH_ALEN);
	if (!WARN_ON(wsm_map_link(priv, &map_link))) {
		sta_priv->link_id = map_link.link_id;
		priv->link_id_map |= 1 << map_link.link_id;
		ap_printk(KERN_DEBUG "[AP] STA added, link_id: %d\n",
			map_link.link_id);
	}
	return 0;
}

int cw1200_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                      struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = hw->priv;
	struct cw1200_sta_priv *sta_priv =
			(struct cw1200_sta_priv *)&sta->drv_priv;
	struct wsm_reset reset = {
		.link_id = 0,
		.reset_statistics = false,
	};

	if (sta_priv->link_id) {
		ap_printk(KERN_DEBUG "[AP] STA removed, link_id: %d\n",
			sta_priv->link_id);
		reset.link_id = sta_priv->link_id;
		priv->link_id_map &= ~(1 << sta_priv->link_id);
		sta_priv->link_id = 0;
		WARN_ON(wsm_reset(priv, &reset));
	}
	return 0;
}

void cw1200_sta_notify(struct ieee80211_hw *dev, struct ieee80211_vif *vif,
		       enum sta_notify_cmd notify_cmd,
		       struct ieee80211_sta *sta)
{
	struct cw1200_common *priv = dev->priv;
	struct cw1200_sta_priv *sta_priv =
		(struct cw1200_sta_priv *)&sta->drv_priv;
	u32 bit = 1 << sta_priv->link_id;

	switch (notify_cmd) {
		case STA_NOTIFY_SLEEP:
			priv->sta_asleep_mask |= bit;
			break;
		case STA_NOTIFY_AWAKE:
			priv->sta_asleep_mask &= ~bit;
			cw1200_bh_wakeup(priv);
			break;
	}
}

static int cw1200_set_tim_impl(struct cw1200_common *priv, bool multicast)
{
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_BEACON,
	};
	u16 tim_offset, tim_length;

	ap_printk(KERN_DEBUG "[AP] %s.\n", __func__);

	frame.skb = ieee80211_beacon_get_tim(priv->hw, priv->vif,
			&tim_offset, &tim_length);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally. */
		frame.skb->data[tim_offset + 2] = 0;

		/* Set/reset aid0 bit */
		if (multicast)
			frame.skb->data[tim_offset + 4] |= 1;
		else
			frame.skb->data[tim_offset + 4] &= ~1;
	}

	WARN_ON(wsm_set_template_frame(priv, &frame));

	dev_kfree_skb(frame.skb);

	return 0;
}

void cw1200_set_tim_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, set_tim_work);
	(void)cw1200_set_tim_impl(priv, !priv->suspend_multicast);
}

int cw1200_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,
		   bool set)
{
	struct cw1200_common *priv = dev->priv;
	queue_work(priv->workqueue, &priv->set_tim_work);
	return 0;
}

void cw1200_bss_info_changed(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u32 changed)
{
	struct cw1200_common *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	mutex_lock(&priv->conf_mutex);
	if (changed & BSS_CHANGED_BSSID) {
		memcpy(priv->bssid, info->bssid, ETH_ALEN);
		cw1200_setup_mac(priv);
	}

	/* TODO: BSS_CHANGED_IBSS */
	/* TODO: BSS_CHANGED_ARP_FILTER */

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		priv->enable_beacon = info->enable_beacon;

	if (changed & BSS_CHANGED_BEACON)
		WARN_ON(cw1200_upload_beacon(priv));

	if (changed & (BSS_CHANGED_BEACON_ENABLED | BSS_CHANGED_BEACON |
			BSS_CHANGED_BEACON_INT))
		WARN_ON(cw1200_update_beaconing(priv));

	if (changed & BSS_CHANGED_ASSOC) {
		wsm_lock_tx(priv);
		priv->wep_default_key_id = -1;
		wsm_unlock_tx(priv);

		if (!info->assoc /* && !info->ibss_joined */) {
			priv->cqm_link_loss_count = 60;
			priv->cqm_beacon_loss_count = 20;
			priv->cqm_tx_failure_thold = 0;
		}
		priv->cqm_tx_failure_count = 0;
	}

	if (changed &
	    (BSS_CHANGED_ASSOC |
	     BSS_CHANGED_BASIC_RATES |
	     BSS_CHANGED_ERP_PREAMBLE |
	     BSS_CHANGED_HT |
	     BSS_CHANGED_ERP_SLOT)) {
		ap_printk(KERN_DEBUG "BSS_CHANGED_ASSOC.\n");
		if (info->assoc) { /* TODO: ibss_joined */
			int dtim_interval = conf->ps_dtim_period;
			int listen_interval = conf->listen_interval;
			struct ieee80211_sta *sta = NULL;

			/* Associated: kill join timeout */
			cancel_delayed_work_sync(&priv->join_timeout);

			/* TODO: This code is not verified {{{ */
			rcu_read_lock();
			if (info->bssid)
				sta = ieee80211_find_sta(vif, info->bssid);
			if (sta) {
				BUG_ON(!priv->channel);
				priv->ht_info.ht_cap = sta->ht_cap;
				priv->bss_params.operationalRateSet =
					__cpu_to_le32(
					cw1200_rate_mask_to_wsm(priv,
					sta->supp_rates[priv->channel->band]));
				priv->ht_info.channel_type =
						info->channel_type;
				priv->ht_info.operation_mode =
						info->ht_operation_mode;
			} else {
				memset(&priv->ht_info, 0,
						sizeof(priv->ht_info));
				priv->bss_params.operationalRateSet = -1;
			}
			rcu_read_unlock();
			/* }}} */

			priv->association_mode.greenfieldMode =
				cw1200_ht_greenfield(&priv->ht_info);
			priv->association_mode.flags =
				WSM_ASSOCIATION_MODE_SNOOP_ASSOC_FRAMES |
				WSM_ASSOCIATION_MODE_USE_PREAMBLE_TYPE |
				WSM_ASSOCIATION_MODE_USE_HT_MODE |
				WSM_ASSOCIATION_MODE_USE_BASIC_RATE_SET |
				WSM_ASSOCIATION_MODE_USE_MPDU_START_SPACING;
			priv->association_mode.preambleType =
				info->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG;
			priv->association_mode.basicRateSet = __cpu_to_le32(
				cw1200_rate_mask_to_wsm(priv,
				info->basic_rates));
			priv->association_mode.mpduStartSpacing =
				cw1200_ht_ampdu_density(&priv->ht_info);

#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
			priv->cqm_beacon_loss_count =
					info->cqm_beacon_miss_thold;
			priv->cqm_tx_failure_thold =
					info->cqm_tx_fail_thold;
			priv->cqm_tx_failure_count = 0;
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */

			priv->bss_params.beaconLostCount =
					priv->cqm_beacon_loss_count ?
					priv->cqm_beacon_loss_count :
					priv->cqm_link_loss_count;

			priv->bss_params.aid = info->aid;

			if (dtim_interval < 1)
				dtim_interval = 1;
			if (dtim_interval < priv->join_dtim_period)
				dtim_interval = priv->join_dtim_period;
			if (listen_interval < dtim_interval)
				listen_interval = 0;

			ap_printk(KERN_DEBUG "[STA] DTIM %d, listen %d\n",
				dtim_interval, listen_interval);
			ap_printk(KERN_DEBUG "[STA] Preamble: %d, " \
				"Greenfield: %d, Aid: %d, " \
				"Rates: 0x%.8X, Basic: 0x%.8X\n",
				priv->association_mode.preambleType,
				priv->association_mode.greenfieldMode,
				priv->bss_params.aid,
				priv->bss_params.operationalRateSet,
				priv->association_mode.basicRateSet);
			WARN_ON(wsm_set_association_mode(priv,
				&priv->association_mode));
			WARN_ON(wsm_set_bss_params(priv, &priv->bss_params));
			WARN_ON(wsm_set_beacon_wakeup_period(priv,
				dtim_interval, listen_interval));
#if 0
			/* It's better to override internal TX rete; otherwise
			 * device sends RTS at too high rate. However device
			 * can't receive CTS at 1 and 2 Mbps. Well, 5.5 is a
			 * good choice for RTS/CTS, but that means PS poll
			 * will be sent at the same rate - impact on link
			 * budget. Not sure what is better.. */

			/* Update: internal rate selection algorythm is not
			 * bad: if device is not receiving CTS at high rate,
			 * it drops RTS rate.
			 * So, conclusion: if-0 the code. Keep code just for
			 * information:
			 * Do not touch WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE! */

			/* ~3 is a bug in device: RTS/CTS is not working at
			 * low rates */

			__le32 internal_tx_rate = __cpu_to_le32(__ffs(
				priv->association_mode.basicRateSet & ~3));
			WARN_ON(wsm_write_mib(priv,
				WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE,
				&internal_tx_rate,
				sizeof(internal_tx_rate)));
#endif
		} else {
			memset(&priv->association_mode, 0,
				sizeof(priv->association_mode));
			memset(&priv->bss_params, 0, sizeof(priv->bss_params));
		}
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_CTS_PROT)) {
		__le32 use_cts_prot = info->use_cts_prot ?
			__cpu_to_le32(1) : 0;

		ap_printk(KERN_DEBUG "[STA] CTS protection %d\n",
			info->use_cts_prot);
		WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_NON_ERP_PROTECTION,
			&use_cts_prot, sizeof(use_cts_prot)));
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_SLOT)) {
		__le32 slot_time = info->use_short_slot ?
			__cpu_to_le32(9) : __cpu_to_le32(20);
		ap_printk(KERN_DEBUG "[STA] Slot time :%d us.\n",
			__le32_to_cpu(slot_time));
		WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_DOT11_SLOT_TIME,
			&slot_time, sizeof(slot_time)));
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_CQM)) {
		struct wsm_rcpi_rssi_threshold threshold = {
			.rssiRcpiMode = WSM_RCPI_RSSI_USE_RSSI,
			.rollingAverageCount = 1,
		};

#if 0
		/* For verification purposes */
		info->cqm_rssi_thold = -50;
		info->cqm_rssi_hyst = 4;
#endif /* 0 */

		ap_printk(KERN_DEBUG "[CQM] RSSI threshold subscribe: %d +- %d\n",
			info->cqm_rssi_thold, info->cqm_rssi_hyst);
#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
		ap_printk(KERN_DEBUG "[CQM] Beacon loss subscribe: %d\n",
			info->cqm_beacon_miss_thold);
		ap_printk(KERN_DEBUG "[CQM] TX failure subscribe: %d\n",
			info->cqm_tx_fail_thold);
		priv->cqm_rssi_thold = info->cqm_rssi_thold;
		priv->cqm_rssi_hyst = info->cqm_rssi_hyst;
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
		if (info->cqm_rssi_thold || info->cqm_rssi_hyst) {
			/* RSSI subscription enabled */
			/* TODO: It's not a correct way of setting threshold.
			 * Upper and lower must be set equal here and adjusted
			 * in callback. However current implementation is much
			 * more relaible and stable. */
			threshold.upperThreshold =
				info->cqm_rssi_thold + info->cqm_rssi_hyst;
			threshold.lowerThreshold =
				info->cqm_rssi_thold;
			threshold.rssiRcpiMode |=
				WSM_RCPI_RSSI_THRESHOLD_ENABLE;
		} else {
			/* There is a bug in FW, see sta.c. We have to enable
			 * dummy subscription to get correct RSSI values. */
			threshold.rssiRcpiMode |=
				WSM_RCPI_RSSI_THRESHOLD_ENABLE |
				WSM_RCPI_RSSI_DONT_USE_UPPER |
				WSM_RCPI_RSSI_DONT_USE_LOWER;
		}
		WARN_ON(wsm_set_rcpi_rssi_threshold(priv, &threshold));

#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
		priv->cqm_tx_failure_thold = info->cqm_tx_fail_thold;
		priv->cqm_tx_failure_count = 0;

		if (priv->cqm_beacon_loss_count !=
				info->cqm_beacon_miss_thold) {
			priv->cqm_beacon_loss_count =
				info->cqm_beacon_miss_thold;
			priv->bss_params.beaconLostCount =
				priv->cqm_beacon_loss_count ?
				priv->cqm_beacon_loss_count :
				priv->cqm_link_loss_count;
			WARN_ON(wsm_set_bss_params(priv, &priv->bss_params));
		}
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
	}
	mutex_unlock(&priv->conf_mutex);
}

void cw1200_multicast_start_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, multicast_start_work);

        (void)cw1200_set_tim_impl(priv, true);
	wsm_lock_tx(priv);
	priv->suspend_multicast = false;
	wsm_unlock_tx(priv);
	cw1200_bh_wakeup(priv);
}

void cw1200_multicast_stop_work(struct work_struct *work)
{
        struct cw1200_common *priv =
                container_of(work, struct cw1200_common, multicast_stop_work);

	/* Lock flushes send queue in device. Just to make sure DTIM beacom
	 * and frames are sent. */
	wsm_lock_tx(priv);
	priv->suspend_multicast = true;
        (void)cw1200_set_tim_impl(priv, false);
	wsm_unlock_tx(priv);
}

int cw1200_ampdu_action(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			enum ieee80211_ampdu_mlme_action action,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	/* Aggregation is implemented fully in firmware,
	 * including block ack negotiation. Do not allow
	 * mac80211 stack to do anything: it interferes with
	 * the firmware. */
	return -ENOTSUPP;
}

/* ******************************************************************** */
/* WSM callback								*/
void cw1200_suspend_resume(struct cw1200_common *priv,
			  struct wsm_suspend_resume *arg)
{
	int queue = 1 << wsm_queue_id_to_linux(arg->queue);
	u32 unicast = 1 << arg->link_id;
	u32 after_dtim = 1 << CW1200_LINK_ID_AFTER_DTIM;
	u32 wakeup_required = 0;
	u32 set = 0;
	u32 clear;
	u32 tx_suspend_mask;
	int i;

	if (!arg->link_id) /* For all links */
		unicast = (1 << (CW1200_MAX_STA_IN_AP_MODE + 1)) - 2;

	ap_printk(KERN_DEBUG "[AP] %s: %s\n",
		arg->stop ? "stop" : "start",
		arg->multicast ? "broadcast" : "unicast");

	if (arg->multicast) {
		if (arg->stop)
			queue_work(priv->workqueue,
				&priv->multicast_stop_work);
		else {
			/* Handle only if there is data to be sent */
			for (i = 0; i < 4; ++i) {
				if (cw1200_queue_get_num_queued(
						&priv->tx_queue[i],
						after_dtim)) {
					queue_work(priv->workqueue,
						&priv->multicast_start_work);
					break;
				}
			}
		}
	} else {
		if (arg->stop)
			set = unicast;
		else
			set = 0;

		clear = set ^ unicast;

		/* TODO: if (!priv->uapsd) */
		queue = 0x0F;

		for (i = 0; i < 4; ++i) {
			if (!(queue & (1 << i)))
				continue;

			tx_suspend_mask = priv->tx_suspend_mask[i];
			priv->tx_suspend_mask[i] =
				(tx_suspend_mask & ~clear) | set;

			wakeup_required = wakeup_required ||
				cw1200_queue_get_num_queued(
					&priv->tx_queue[i],
					tx_suspend_mask & clear);
		}
	}
	if (wakeup_required)
		cw1200_bh_wakeup(priv);
	return;
}


/* ******************************************************************** */
/* AP privates								*/

static int cw1200_upload_beacon(struct cw1200_common *priv)
{
	int ret = 0;
	const u8 *ssidie;
	const struct ieee80211_mgmt *mgmt;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_BEACON,
	};

	ap_printk(KERN_DEBUG "[AP] %s.\n", __func__);

	frame.skb = ieee80211_beacon_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	mgmt = (struct ieee80211_mgmt *)frame.skb->data;
	ssidie = cfg80211_find_ie(WLAN_EID_SSID,
		mgmt->u.beacon.variable,
		frame.skb->len - (mgmt->u.beacon.variable - frame.skb->data));
	memset(priv->ssid, 0, sizeof(priv->ssid));
	if (ssidie) {
		priv->ssid_length = ssidie[1];
		if (WARN_ON(priv->ssid_length > sizeof(priv->ssid)))
			priv->ssid_length = sizeof(priv->ssid);
		memcpy(priv->ssid, &ssidie[2], priv->ssid_length);
	} else {
		priv->ssid_length = 0;
	}

	ret = wsm_set_template_frame(priv, &frame);
	if (!ret) {
		/* TODO: Distille probe resp; remove TIM
		 * and other beacon-specific IEs */
		*(__le16 *)frame.skb->data =
			__cpu_to_le16(IEEE80211_FTYPE_MGMT |
				      IEEE80211_STYPE_PROBE_RESP);
		frame.frame_type = WSM_FRAME_TYPE_PROBE_RESPONSE;
		ret = wsm_set_template_frame(priv, &frame);
	}
	dev_kfree_skb(frame.skb);

	return ret;
}

static int cw1200_start_ap(struct cw1200_common *priv)
{
	int ret;
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct wsm_start start = {
		.mode = WSM_START_MODE_AP,
		.band = (priv->channel->band == IEEE80211_BAND_5GHZ) ?
				WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G,
		.channelNumber = priv->channel->hw_value,
		.beaconInterval = conf->beacon_int,
		.DTIMPeriod = conf->dtim_period,
		.preambleType = conf->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG,
		.probeDelay = 100,
		.basicRateSet = cw1200_rate_mask_to_wsm(priv,
				conf->basic_rates),
		.ssidLength = priv->ssid_length,
	};
	struct wsm_beacon_transmit transmit = {
		.enableBeaconing = priv->enable_beacon,
	};

	memcpy(&start.ssid[0], priv->ssid, start.ssidLength);

	ap_printk(KERN_DEBUG "[AP] ch: %d(%d), bcn: %d(%d), brt: 0x%.8X, ssid: %.*s %s.\n",
		start.channelNumber, start.band,
		start.beaconInterval, start.DTIMPeriod,
		start.basicRateSet,
		start.ssidLength, start.ssid,
		transmit.enableBeaconing ? "ena" : "dis");
	ret = WARN_ON(wsm_start(priv, &start));
	if (!ret)
		ret = WARN_ON(cw1200_upload_keys(priv));
	if (!ret)
		ret = WARN_ON(wsm_beacon_transmit(priv, &transmit));
	if (!ret) {
		WARN_ON(wsm_set_block_ack_policy(priv,
			priv->ba_tid_mask, priv->ba_tid_mask));
		priv->join_status = CW1200_JOIN_STATUS_AP;
		cw1200_update_filtering(priv);
	}
	return ret;
}

static int cw1200_update_beaconing(struct cw1200_common *priv)
{
	struct wsm_reset reset = {
		.link_id = 0,
		.reset_statistics = true,
	};

	if (priv->mode == NL80211_IFTYPE_AP) {
		ap_printk(KERN_DEBUG "[AP] %s.\n", __func__);
		WARN_ON(wsm_reset(priv, &reset));
		priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
		WARN_ON(cw1200_start_ap(priv));
	}
	return 0;
}

