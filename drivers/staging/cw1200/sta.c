/*
 * Mac80211 STA API for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>

#include "cw1200.h"
#include "sta.h"
#include "fwio.h"
#include "bh.h"
#include "debug.h"

#if defined(CONFIG_CW1200_STA_DEBUG)
#define sta_printk(...) printk(__VA_ARGS__)
#else
#define sta_printk(...)
#endif


static int cw1200_cancel_scan(struct cw1200_common *priv);
static int __cw1200_flush(struct cw1200_common *priv, bool drop);

static inline void __cw1200_free_event_queue(struct list_head *list)
{
	while (!list_empty(list)) {
		struct cw1200_wsm_event *event =
			list_first_entry(list, struct cw1200_wsm_event,
			link);
		list_del(&event->link);
		kfree(event);
	}
}

/* ******************************************************************** */
/* STA API								*/

int cw1200_start(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	int ret = 0;

	mutex_lock(&priv->conf_mutex);

	/* default ECDA */
	WSM_EDCA_SET(&priv->edca, 0, 0x0002, 0x0003, 0x0007, 47);
	WSM_EDCA_SET(&priv->edca, 1, 0x0002, 0x0007, 0x000f, 94);
	WSM_EDCA_SET(&priv->edca, 2, 0x0003, 0x000f, 0x03ff, 0);
	WSM_EDCA_SET(&priv->edca, 3, 0x0007, 0x000f, 0x03ff, 0);
	ret = wsm_set_edca_params(priv, &priv->edca);
	if (WARN_ON(ret))
		goto out;

	memset(priv->bssid, ~0, ETH_ALEN);
	memcpy(priv->mac_addr, dev->wiphy->perm_addr, ETH_ALEN);
	priv->mode = NL80211_IFTYPE_MONITOR;
	priv->softled_state = 0;
	priv->wep_default_key_id = -1;

	priv->cqm_link_loss_count = 60;
	priv->cqm_beacon_loss_count = 20;

	ret = cw1200_setup_mac(priv);
	if (WARN_ON(ret))
		goto out;

	/* err = cw1200_set_leds(priv); */

out:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_stop(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	LIST_HEAD(list);
	int i;

	struct wsm_reset reset = {
		.reset_statistics = true,
	};

	wsm_lock_tx(priv);

	while (down_trylock(&priv->scan.lock)) {
		/* Scan is in progress. Force it to stop. */
		priv->scan.req = NULL;
		schedule();
	}
	up(&priv->scan.lock);

	mutex_lock(&priv->conf_mutex);
	cw1200_free_keys(priv);
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->listening = false;
	mutex_unlock(&priv->conf_mutex);

	cancel_delayed_work_sync(&priv->scan.probe_work);
	cancel_delayed_work_sync(&priv->scan.timeout);
	cancel_delayed_work_sync(&priv->join_timeout);
	cancel_delayed_work_sync(&priv->bss_loss_work);
	cancel_delayed_work_sync(&priv->connection_loss_work);
#if defined(CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
	cancel_delayed_work_sync(&priv->keep_alive_work);
#endif /* CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */

	mutex_lock(&priv->conf_mutex);
	switch (priv->join_status) {
	case CW1200_JOIN_STATUS_STA:
		wsm_lock_tx(priv);
		if (queue_work(priv->workqueue, &priv->unjoin_work) <= 0)
			wsm_unlock_tx(priv);
		break;
	case CW1200_JOIN_STATUS_AP:
		/* If you see this warning please change the code to iterate
		 * through the map and reset each link separately. */
		WARN_ON(priv->link_id_map);
		priv->sta_asleep_mask = 0;
		priv->suspend_multicast = false;
		wsm_reset(priv, &reset);
		break;
	case CW1200_JOIN_STATUS_MONITOR:
		cw1200_update_listening(priv, false);
		break;
	default:
		break;
	}
	mutex_unlock(&priv->conf_mutex);

	flush_workqueue(priv->workqueue);
	mutex_lock(&priv->conf_mutex);

	priv->softled_state = 0;
	/* cw1200_set_leds(priv); */

	spin_lock(&priv->event_queue_lock);
	list_splice_init(&priv->event_queue, &list);
	spin_unlock(&priv->event_queue_lock);
	__cw1200_free_event_queue(&list);

	priv->delayed_link_loss = 0;

	priv->link_id_map = 0;
	priv->join_status = CW1200_JOIN_STATUS_PASSIVE;

	for (i = 0; i < 4; i++)
		cw1200_queue_clear(&priv->tx_queue[i]);

	/* HACK! */
	if (atomic_xchg(&priv->tx_lock, 1) != 1)
		sta_printk(KERN_DEBUG "[STA] TX is force-unlocked due to stop " \
			"request.\n");

	wsm_unlock_tx(priv);

	mutex_unlock(&priv->conf_mutex);
}

int cw1200_add_interface(struct ieee80211_hw *dev,
			 struct ieee80211_vif *vif)
{
	int ret;
	struct cw1200_common *priv = dev->priv;
	/* __le32 auto_calibration_mode = __cpu_to_le32(1); */

	mutex_lock(&priv->conf_mutex);

	if (priv->mode != NL80211_IFTYPE_MONITOR) {
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
		priv->mode = vif->type;
		break;
	default:
		mutex_unlock(&priv->conf_mutex);
		return -EOPNOTSUPP;
	}

	priv->vif = vif;
	memcpy(priv->mac_addr, vif->addr, ETH_ALEN);

	ret = WARN_ON(cw1200_setup_mac(priv));
	/* Enable auto-calibration */
	/* Exception in subsequent channel switch; disabled.
	WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_SET_AUTO_CALIBRATION_MODE,
		&auto_calibration_mode, sizeof(auto_calibration_mode)));
	*/

	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_remove_interface(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif)
{
	struct cw1200_common *priv = dev->priv;

	struct wsm_reset reset = {
		.reset_statistics = true,
	};

	mutex_lock(&priv->conf_mutex);

	priv->vif = NULL;
	priv->mode = NL80211_IFTYPE_MONITOR;
	memset(priv->mac_addr, 0, ETH_ALEN);
	memset(priv->bssid, 0, ETH_ALEN);
	WARN_ON(wsm_reset(priv, &reset));
	cw1200_free_keys(priv);
	cw1200_setup_mac(priv);
	priv->listening = false;
	priv->join_status = CW1200_JOIN_STATUS_PASSIVE;

	mutex_unlock(&priv->conf_mutex);
}

int cw1200_config(struct ieee80211_hw *dev, u32 changed)
{
	int ret = 0;
	struct cw1200_common *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	mutex_lock(&priv->conf_mutex);
	/* TODO: IEEE80211_CONF_CHANGE_QOS */
	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		priv->output_power = conf->power_level;
		sta_printk(KERN_DEBUG "[STA] TX power: %d\n", priv->output_power);
		WARN_ON(wsm_set_output_power(priv, priv->output_power * 10));
	}

	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL) {
		/* TODO: Not sure. Needs to be verified. */
		/* TODO: DTIM skipping */
		int dtim_interval = conf->ps_dtim_period;
		int listen_interval = conf->listen_interval;
		if (dtim_interval < 1)
			dtim_interval = 1;
		if (listen_interval < dtim_interval)
			listen_interval = 0;
		/* TODO: max_sleep_period is not supported
		 * and silently skipped. */
		sta_printk(KERN_DEBUG "[STA] DTIM %d, listen %d\n",
			dtim_interval, listen_interval);
		WARN_ON(wsm_set_beacon_wakeup_period(priv,
			dtim_interval, listen_interval));
	}


	if ((changed & IEEE80211_CONF_CHANGE_CHANNEL) &&
			(priv->channel != conf->channel)) {
		struct ieee80211_channel *ch = conf->channel;
		struct wsm_switch_channel channel = {
			.newChannelNumber = ch->hw_value,
		};
		cw1200_cancel_scan(priv);
		sta_printk(KERN_DEBUG "[STA] Freq %d (wsm ch: %d).\n",
			ch->center_freq, ch->hw_value);
		WARN_ON(wait_event_interruptible_timeout(
			priv->channel_switch_done,
			!priv->channel_switch_in_progress, 3 * HZ) <= 0);

		ret = WARN_ON(__cw1200_flush(priv, false));
		if (!ret) {
			ret = WARN_ON(wsm_switch_channel(priv, &channel));
			if (!ret)
				priv->channel = ch;
			else
				wsm_unlock_tx(priv);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		priv->powersave_mode.pmMode =
				(conf->flags & IEEE80211_CONF_PS) ?
				WSM_PSM_PS : WSM_PSM_ACTIVE;
		if (priv->join_status == CW1200_JOIN_STATUS_STA)
			WARN_ON(wsm_set_pm(priv, &priv->powersave_mode));
	}

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		/* TBD: It looks like it's transparent
		 * there's a monitor interface present -- use this
		 * to determine for example whether to calculate
		 * timestamps for packets or not, do not use instead
		 * of filter flags! */
	}

	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		struct wsm_operational_mode mode = {
			.power_mode = (conf->flags & IEEE80211_CONF_IDLE) ?
				wsm_power_mode_quiescent :
				wsm_power_mode_doze,
			.disableMoreFlagUsage = true,
		};
		wsm_lock_tx(priv);
		WARN_ON(wsm_set_operational_mode(priv, &mode));
		wsm_unlock_tx(priv);
	}

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		sta_printk(KERN_DEBUG "[STA] Retry limits: %d (long), " \
			"%d (short).\n",
			conf->long_frame_max_tx_count,
			conf->short_frame_max_tx_count);
		spin_lock_bh(&priv->tx_policy_cache.lock);
		priv->long_frame_max_tx_count = conf->long_frame_max_tx_count;
		priv->short_frame_max_tx_count =
			(conf->short_frame_max_tx_count < 0x0F) ?
			conf->short_frame_max_tx_count : 0x0F;
		priv->hw->max_rate_tries = priv->short_frame_max_tx_count;
		spin_unlock_bh(&priv->tx_policy_cache.lock);
		/* TBD: I think we don't need tx_policy_force_upload().
		 * Outdated policies will leave cache in a normal way. */
		/* WARN_ON(tx_policy_force_upload(priv)); */
	}
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_update_filtering(struct cw1200_common *priv)
{
	int ret;

	if (priv->join_status == CW1200_JOIN_STATUS_PASSIVE)
		return;

	ret = wsm_set_rx_filter(priv, &priv->rx_filter);
	if (!ret)
		ret = wsm_beacon_filter_control(priv, &priv->bf_control);
	if (!ret)
		ret = wsm_set_bssid_filtering(priv, !priv->rx_filter.bssid);
	if (ret)
		wiphy_err(priv->hw->wiphy,
				"%s: Update filtering failed: %d.\n",
				__func__, ret);
	return;
}

void cw1200_configure_filter(struct ieee80211_hw *dev,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast)
{
	struct cw1200_common *priv = dev->priv;
	bool listening = !!(*total_flags &
			(FIF_PROMISC_IN_BSS |
			 FIF_OTHER_BSS |
			 FIF_BCN_PRBRESP_PROMISC |
			 FIF_PROBE_REQ));

	*total_flags &= FIF_PROMISC_IN_BSS |
			FIF_OTHER_BSS |
			FIF_FCSFAIL |
			FIF_BCN_PRBRESP_PROMISC |
			FIF_PROBE_REQ;

	mutex_lock(&priv->conf_mutex);

	priv->rx_filter.promiscuous = (*total_flags & FIF_PROMISC_IN_BSS)
			? 1 : 0;
	priv->rx_filter.bssid = (*total_flags & (FIF_OTHER_BSS |
			FIF_PROBE_REQ)) ? 1 : 0;
	priv->rx_filter.fcs = (*total_flags & FIF_FCSFAIL) ? 1 : 0;
	priv->bf_control.bcn_count = (*total_flags &
			(FIF_BCN_PRBRESP_PROMISC |
			 FIF_PROMISC_IN_BSS |
			 FIF_PROBE_REQ)) ? 1 : 0;
	if (priv->listening ^ listening) {
		priv->listening = listening;
		cw1200_update_listening(priv, listening);
	}
	cw1200_update_filtering(priv);
	mutex_unlock(&priv->conf_mutex);
}

int cw1200_conf_tx(struct ieee80211_hw *dev, u16 queue,
		   const struct ieee80211_tx_queue_params *params)
{
	struct cw1200_common *priv = dev->priv;
	int ret = 0;

	mutex_lock(&priv->conf_mutex);

	if (queue < dev->queues) {
		WSM_EDCA_SET(&priv->edca, queue, params->aifs,
			params->cw_min, params->cw_max, params->txop);
		ret = wsm_set_edca_params(priv, &priv->edca);
	} else
		ret = -EINVAL;

	mutex_unlock(&priv->conf_mutex);
	return ret;
}

int cw1200_get_stats(struct ieee80211_hw *dev,
		     struct ieee80211_low_level_stats *stats)
{
	struct cw1200_common *priv = dev->priv;

	memcpy(stats, &priv->stats, sizeof(*stats));
	return 0;
}

/*
int cw1200_get_tx_stats(struct ieee80211_hw *dev,
			struct ieee80211_tx_queue_stats *stats)
{
	int i;
	struct cw1200_common *priv = dev->priv;

	for (i = 0; i < dev->queues; ++i)
		cw1200_queue_get_stats(&priv->tx_queue[i], &stats[i]);

	return 0;
}
*/

int cw1200_set_key(struct ieee80211_hw *dev, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key)
{
	int ret = -EOPNOTSUPP;
	struct cw1200_common *priv = dev->priv;

	mutex_lock(&priv->conf_mutex);

	if (cmd == SET_KEY) {
		u8 *peer_addr = NULL;
		int pairwise = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
			1 : 0;
		int idx = cw1200_alloc_key(priv);
		struct wsm_add_key *wsm_key = &priv->keys[idx];

		if (idx < 0) {
			ret = -EINVAL;
			goto finally;
		}

		BUG_ON(pairwise && !sta);
		if (sta)
			peer_addr = sta->addr;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			if (key->keylen > 16) {
				cw1200_free_key(priv, idx);
				ret = -EINVAL;
				goto finally;
			}

			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_WEP_PAIRWISE;
				memcpy(wsm_key->wepPairwiseKey.peerAddress,
					 peer_addr, ETH_ALEN);
				memcpy(wsm_key->wepPairwiseKey.keyData,
					&key->key[0], key->keylen);
				wsm_key->wepPairwiseKey.keyLength = key->keylen;
			} else {
				wsm_key->type = WSM_KEY_TYPE_WEP_DEFAULT;
				memcpy(wsm_key->wepGroupKey.keyData,
					&key->key[0], key->keylen);
				wsm_key->wepGroupKey.keyLength = key->keylen;
				wsm_key->wepGroupKey.keyId = key->keyidx;
			}
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_TKIP_PAIRWISE;
				memcpy(wsm_key->tkipPairwiseKey.peerAddress,
					peer_addr, ETH_ALEN);
				memcpy(wsm_key->tkipPairwiseKey.tkipKeyData,
					&key->key[0],  16);
				memcpy(wsm_key->tkipPairwiseKey.txMicKey,
					&key->key[16],  8);
				memcpy(wsm_key->tkipPairwiseKey.rxMicKey,
					&key->key[24],  8);
			} else {
				size_t mic_offset =
					(priv->mode == NL80211_IFTYPE_AP) ?
					16 : 24;
				wsm_key->type = WSM_KEY_TYPE_TKIP_GROUP;
				memcpy(wsm_key->tkipGroupKey.tkipKeyData,
					&key->key[0],  16);
				memcpy(wsm_key->tkipGroupKey.rxMicKey,
					&key->key[mic_offset],  8);

				/* TODO: Where can I find TKIP SEQ? */
				memset(wsm_key->tkipGroupKey.rxSeqCounter,
					0,		8);
				wsm_key->tkipGroupKey.keyId = key->keyidx;

				print_hex_dump_bytes("TKIP: ", DUMP_PREFIX_NONE,
					key->key, key->keylen);
			}
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_AES_PAIRWISE;
				memcpy(wsm_key->aesPairwiseKey.peerAddress,
					peer_addr, ETH_ALEN);
				memcpy(wsm_key->aesPairwiseKey.aesKeyData,
					&key->key[0],  16);
			} else {
				wsm_key->type = WSM_KEY_TYPE_AES_GROUP;
				memcpy(wsm_key->aesGroupKey.aesKeyData,
					&key->key[0],  16);
				/* TODO: Where can I find AES SEQ? */
				memset(wsm_key->aesGroupKey.rxSeqCounter,
					0,              8);
				wsm_key->aesGroupKey.keyId = key->keyidx;
			}
			break;
#if 0
		case WLAN_CIPHER_SUITE_WAPI:
			if (pairwise) {
				wsm_key->type = WSM_KEY_TYPE_WAPI_PAIRWISE;
				memcpy(wsm_key->wapiPairwiseKey.peerAddress,
					peer_addr, ETH_ALEN);
				memcpy(wsm_key->wapiPairwiseKey.wapiKeyData,
					&key->key[0],  16);
				memcpy(wsm_key->wapiPairwiseKey.micKeyData,
					&key->key[16], 16);
				wsm_key->wapiPairwiseKey.keyId = key->keyidx;
			} else {
				wsm_key->type = WSM_KEY_TYPE_WAPI_GROUP;
				memcpy(wsm_key->wapiGroupKey.wapiKeyData,
					&key->key[0],  16);
				memcpy(wsm_key->wapiGroupKey.micKeyData,
					&key->key[16], 16);
				wsm_key->wapiGroupKey.keyId = key->keyidx;
			}
			break;
#endif
		default:
			WARN_ON(1);
			cw1200_free_key(priv, idx);
			ret = -EOPNOTSUPP;
			goto finally;
		}
		ret = WARN_ON(wsm_add_key(priv, wsm_key));
		if (!ret)
			key->hw_key_idx = idx;
		else
			cw1200_free_key(priv, idx);
	} else if (cmd == DISABLE_KEY) {
		struct wsm_remove_key wsm_key = {
			.entryIndex = key->hw_key_idx,
		};

		if (wsm_key.entryIndex > WSM_KEY_MAX_INDEX) {
			ret = -EINVAL;
			goto finally;
		}

		cw1200_free_key(priv, wsm_key.entryIndex);
		ret = wsm_remove_key(priv, &wsm_key);
	} else {
		BUG_ON("Unsupported command");
	}

finally:
	mutex_unlock(&priv->conf_mutex);
	return ret;
}

void cw1200_wep_key_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, wep_key_work);
	__le32 wep_default_key_id = __cpu_to_le32(
		priv->wep_default_key_id);

	sta_printk(KERN_DEBUG "[STA] Setting default WEP key: %d\n",
		priv->wep_default_key_id);
	wsm_flush_tx(priv);
	WARN_ON(wsm_write_mib(priv, WSM_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID,
		&wep_default_key_id, sizeof(wep_default_key_id)));
	wsm_unlock_tx(priv);
}

int cw1200_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	int ret;
	__le32 val32;

	if (value != (u32) -1)
		val32 = __cpu_to_le32(value);
	else
		val32 = 0; /* disabled */

	/* mutex_lock(&priv->conf_mutex); */
	ret = WARN_ON(wsm_write_mib(hw->priv, WSM_MIB_ID_DOT11_RTS_THRESHOLD,
		&val32, sizeof(val32)));
	/* mutex_unlock(&priv->conf_mutex); */
	return ret;
}

static int __cw1200_flush(struct cw1200_common *priv, bool drop)
{
	int i, ret;

	if (drop) {
		for (i = 0; i < 4; ++i)
			cw1200_queue_clear(&priv->tx_queue[i]);
	}

	for (;;) {
		ret = wait_event_interruptible_timeout(
				priv->tx_queue_stats.wait_link_id_empty,
				cw1200_queue_stats_is_empty(
					&priv->tx_queue_stats, -1),
				10 * HZ);

		if (unlikely(ret <= 0)) {
			if (!ret)
				ret = -ETIMEDOUT;
			break;
		} else {
			ret = 0;
		}
		ret = 0;

		wsm_lock_tx(priv);
		if (unlikely(!cw1200_queue_stats_is_empty(
				&priv->tx_queue_stats, -1))) {
			/* Highly unlekely: WSM requeued frames. */
			wsm_unlock_tx(priv);
			continue;
		}
		break;
	}
	return ret;
}

void cw1200_flush(struct ieee80211_hw *hw, bool drop)
{
	struct cw1200_common *priv = hw->priv;

	if (!WARN_ON(__cw1200_flush(priv, drop)))
		wsm_unlock_tx(priv);

	return;
}

/* ******************************************************************** */
/* WSM callbacks							*/

void cw1200_channel_switch_cb(struct cw1200_common *priv)
{
	wsm_unlock_tx(priv);
}

void cw1200_free_event_queue(struct cw1200_common *priv)
{
	LIST_HEAD(list);

	spin_lock(&priv->event_queue_lock);
	list_splice_init(&priv->event_queue, &list);
	spin_unlock(&priv->event_queue_lock);

	__cw1200_free_event_queue(&list);
}

void cw1200_event_handler(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, event_handler);
	struct cw1200_wsm_event *event;
	LIST_HEAD(list);

	spin_lock(&priv->event_queue_lock);
	list_splice_init(&priv->event_queue, &list);
	spin_unlock(&priv->event_queue_lock);

	list_for_each_entry(event, &list, link) {
		switch (event->evt.eventId) {
		case WSM_EVENT_ERROR:
			/* I even don't know what is it about.. */
			STUB();
			break;
		case WSM_EVENT_BSS_LOST:
		{
			sta_printk(KERN_DEBUG "[CQM] BSS lost.\n");
			cancel_delayed_work_sync(&priv->bss_loss_work);
			cancel_delayed_work_sync(&priv->connection_loss_work);
			if (!down_trylock(&priv->scan.lock)) {
				up(&priv->scan.lock);
				priv->delayed_link_loss = 0;
				queue_delayed_work(priv->workqueue,
						&priv->bss_loss_work, 0);
			} else {
				/* Scan is in progress. Delay reporting. */
				/* Scan complete will trigger bss_loss_work */
				priv->delayed_link_loss = 1;
				/* Also we're starting watchdog. */
				queue_delayed_work(priv->workqueue,
						&priv->bss_loss_work, 10 * HZ);
			}
			break;
		}
		case WSM_EVENT_BSS_REGAINED:
		{
			sta_printk(KERN_DEBUG "[CQM] BSS regained.\n");
			priv->delayed_link_loss = 0;
			cancel_delayed_work_sync(&priv->bss_loss_work);
			cancel_delayed_work_sync(&priv->connection_loss_work);
			break;
		}
		case WSM_EVENT_RADAR_DETECTED:
			STUB();
			break;
		case WSM_EVENT_RCPI_RSSI:
		{
			int rssi = (int)(s8)(event->evt.eventData & 0xFF);
			int cqm_evt = (rssi <= priv->cqm_rssi_thold) ?
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
			sta_printk(KERN_DEBUG "[CQM] RSSI event: %d", rssi);
			ieee80211_cqm_rssi_notify(priv->vif, cqm_evt,
								GFP_KERNEL);
			break;
		}
		case WSM_EVENT_BT_INACTIVE:
			STUB();
			break;
		case WSM_EVENT_BT_ACTIVE:
			STUB();
			break;
		}
	}
	__cw1200_free_event_queue(&list);
}

void cw1200_bss_loss_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, bss_loss_work.work);
	int timeout; /* in beacons */

	timeout = priv->cqm_link_loss_count -
		priv->cqm_beacon_loss_count;

	if (priv->cqm_beacon_loss_count) {
		sta_printk(KERN_DEBUG "[CQM] Beacon loss.\n");
		if (timeout <= 0)
			timeout = 0;
#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
		ieee80211_cqm_beacon_miss_notify(priv->vif, GFP_KERNEL);
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
	} else {
		timeout = 0;
	}

	cancel_delayed_work_sync(&priv->connection_loss_work);
	queue_delayed_work(priv->workqueue,
		&priv->connection_loss_work,
		timeout * HZ / 10);
}

void cw1200_connection_loss_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common,
				connection_loss_work.work);
	sta_printk(KERN_DEBUG "[CQM] Reporting connection loss.\n");
	ieee80211_connection_loss(priv->vif);
}

#if defined(CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
void cw1200_keep_alive_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, keep_alive_work.work);
	unsigned long now = jiffies;
	unsigned long delta = now - priv->last_activity_time;
	unsigned long tmo = 30 * HZ;

	if (delta >= tmo) {
		sta_printk(KERN_DEBUG "[CQM] Keep-alive ping.\n");
		STUB();
		/* TODO: Do a keep-alive ping :) */
		priv->last_activity_time = now;
	} else {
		tmo -= delta;
	}
	queue_delayed_work(priv->workqueue,
		&priv->keep_alive_work, tmo);
}
#endif /* CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */

void cw1200_tx_failure_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, tx_failure_work);
	sta_printk(KERN_DEBUG "[CQM] Reporting TX failure.\n");
#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
	ieee80211_cqm_tx_fail_notify(priv->vif, GFP_KERNEL);
#else /* CONFIG_CW1200_USE_STE_EXTENSIONS */
	(void)priv;
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
}

/* ******************************************************************** */
/* Internal API								*/

int cw1200_setup_mac(struct cw1200_common *priv)
{
	/* TBD: Do you know how to assing MAC address without
	 * annoying uploading RX data? */
	u8 prev_mac[ETH_ALEN];

	/* NOTE: There is a bug in FW: it reports signal
	* as RSSI if RSSI subscription is enabled.
	* It's not enough to set WSM_RCPI_RSSI_USE_RSSI. */
	struct wsm_rcpi_rssi_threshold threshold = {
		.rssiRcpiMode = WSM_RCPI_RSSI_USE_RSSI |
		WSM_RCPI_RSSI_THRESHOLD_ENABLE |
		WSM_RCPI_RSSI_DONT_USE_UPPER |
		WSM_RCPI_RSSI_DONT_USE_LOWER,
		.rollingAverageCount = 16,
	};
	int ret = 0;

	if (wsm_get_station_id(priv, &prev_mac[0])
	    || memcmp(prev_mac, priv->mac_addr, ETH_ALEN)) {
		const char *sdd_path = NULL;
		const struct firmware *firmware = NULL;
		struct wsm_configuration cfg = {
			.dot11StationId = &priv->mac_addr[0],
		};

		switch (priv->hw_revision) {
		case CW1200_HW_REV_CUT10:
			sdd_path = SDD_FILE_10;
			break;
		case CW1200_HW_REV_CUT11:
			sdd_path = SDD_FILE_11;
			break;
		case CW1200_HW_REV_CUT20:
			sdd_path = SDD_FILE_20;
			break;
		case CW1200_HW_REV_CUT22:
			sdd_path = SDD_FILE_22;
			break;
		default:
			BUG_ON(1);
		}

		ret = request_firmware(&firmware,
			sdd_path, priv->pdev);

		if (unlikely(ret)) {
			cw1200_dbg(CW1200_DBG_ERROR,
				"%s: can't load sdd file %s.\n",
				__func__, sdd_path);
			return ret;
		}

		cfg.dpdData = firmware->data;
		cfg.dpdData_size = firmware->size;
		ret = WARN_ON(wsm_configuration(priv, &cfg));

		release_firmware(firmware);
	}
	if (ret)
		return ret;

	/* Configure RSSI/SCPI reporting as RSSI. */
	WARN_ON(wsm_set_rcpi_rssi_threshold(priv, &threshold));

	/* TODO: */
	switch (priv->mode) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		/* TODO: Not verified yet. */
		STUB();
		break;
	}

	return 0;
}

void cw1200_join_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, join_work);
	const struct wsm_tx *wsm = priv->join_pending_frame;
	const u8 *frame = (u8 *)&wsm[1];
	const u8 *bssid = &frame[4]; /* AP SSID in a 802.11 frame */
	struct cfg80211_bss *bss;
	const u8 *ssidie;
	const u8 *dtimie;
	const struct ieee80211_tim_ie *tim = NULL;
	u8 queueId = wsm_queue_id_to_linux(wsm->queueId);

	cancel_delayed_work_sync(&priv->join_timeout);

	bss = cfg80211_get_bss(priv->hw->wiphy, NULL, bssid, NULL, 0, 0, 0);
	if (!bss) {
		priv->join_pending_frame = NULL;
		cw1200_queue_remove(&priv->tx_queue[queueId],
			priv, __le32_to_cpu(wsm->packetID));
		return;
	}
	ssidie = cfg80211_find_ie(WLAN_EID_SSID,
		bss->information_elements,
		bss->len_information_elements);
	dtimie = cfg80211_find_ie(WLAN_EID_TIM,
		bss->information_elements,
		bss->len_information_elements);
	if (dtimie)
		tim = (struct ieee80211_tim_ie *)&dtimie[2];

	mutex_lock(&priv->conf_mutex);
	{
		struct wsm_join join = {
			.mode = (bss->capability & WLAN_CAPABILITY_IBSS) ?
				WSM_JOIN_MODE_IBSS : WSM_JOIN_MODE_BSS,
			.preambleType = WSM_JOIN_PREAMBLE_SHORT,
			.probeForJoin = 1,
			/* dtimPeriod will be updated after association */
			.dtimPeriod = 1,
			.beaconInterval = bss->beacon_interval,
			/* basicRateSet will be updated after association */
			.basicRateSet = 7,
		};

		if (tim && tim->dtim_period > 1) {
			join.dtimPeriod = tim->dtim_period;
			priv->join_dtim_period = tim->dtim_period;
			sta_printk(KERN_DEBUG "[STA] Join DTIM: %d\n",
				join.dtimPeriod);
		}

		priv->join_pending_frame = NULL;
		BUG_ON(!wsm);
		BUG_ON(!priv->channel);

		join.channelNumber = priv->channel->hw_value;
		join.band = (priv->channel->band == IEEE80211_BAND_5GHZ) ?
			WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G;

		memcpy(&join.bssid[0], bssid, sizeof(join.bssid));
		memcpy(&priv->join_bssid[0], bssid, sizeof(priv->join_bssid));

		if (ssidie) {
			join.ssidLength = ssidie[1];
			if (WARN_ON(join.ssidLength > sizeof(join.ssid)))
				join.ssidLength = sizeof(join.ssid);
			memcpy(&join.ssid[0], &ssidie[2], join.ssidLength);
		}

		wsm_flush_tx(priv);

		WARN_ON(wsm_set_block_ack_policy(priv,
				priv->ba_tid_mask, priv->ba_tid_mask));

#if defined(CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
		priv->last_activity_time = jiffies;
		/* Queue keep-alive ping avery 30 sec. */
		queue_delayed_work(priv->workqueue,
			&priv->keep_alive_work, 30 * HZ);
#endif /* CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
		/* Queue unjoin if not associated in 3 sec. */
		queue_delayed_work(priv->workqueue,
			&priv->join_timeout, 3 * HZ);

		cw1200_update_listening(priv, false);
		if (wsm_join(priv, &join)) {
			memset(&priv->join_bssid[0],
				0, sizeof(priv->join_bssid));
			cw1200_queue_remove(&priv->tx_queue[queueId],
				priv, __le32_to_cpu(wsm->packetID));
			cancel_delayed_work_sync(&priv->join_timeout);
#if defined(CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
			cancel_delayed_work_sync(&priv->keep_alive_work);
#endif /* CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
			cw1200_update_listening(priv, priv->listening);
			WARN_ON(wsm_set_pm(priv, &priv->powersave_mode));
		} else {
			/* Upload keys */
			WARN_ON(cw1200_upload_keys(priv));
#if !defined(CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
			WARN_ON(wsm_keep_alive_period(priv, 30 /* sec */));
#endif /* CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
			cw1200_queue_requeue(&priv->tx_queue[queueId],
				__le32_to_cpu(wsm->packetID));
		}
		cw1200_update_filtering(priv);
	}
	mutex_unlock(&priv->conf_mutex);
	cfg80211_put_bss(bss);
}

void cw1200_join_timeout(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, join_timeout.work);
	sta_printk(KERN_DEBUG "[WSM] Issue unjoin command (TMO).\n");
	wsm_lock_tx(priv);
	cw1200_unjoin_work(&priv->unjoin_work);
}

void cw1200_unjoin_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, unjoin_work);

	struct wsm_reset reset = {
		.reset_statistics = true,
	};

	mutex_lock(&priv->conf_mutex);
	BUG_ON(priv->join_status &&
			priv->join_status != CW1200_JOIN_STATUS_STA);
	if (priv->join_status == CW1200_JOIN_STATUS_STA) {
		memset(&priv->join_bssid[0], 0, sizeof(priv->join_bssid));
		priv->join_status = CW1200_JOIN_STATUS_PASSIVE;

		/* Unjoin is a reset. */
		wsm_flush_tx(priv);
		WARN_ON(wsm_reset(priv, &reset));
		priv->join_dtim_period = 0;
		WARN_ON(cw1200_setup_mac(priv));
		cw1200_free_event_queue(priv);
		cancel_work_sync(&priv->event_handler);
		cancel_delayed_work_sync(&priv->connection_loss_work);
#if defined(CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
		cancel_delayed_work_sync(&priv->keep_alive_work);
#endif /* CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
		cw1200_update_listening(priv, priv->listening);
		cw1200_update_filtering(priv);
		sta_printk(KERN_DEBUG "[STA] Unjoin.\n");
	}
	mutex_unlock(&priv->conf_mutex);
	wsm_unlock_tx(priv);
}

static inline int cw1200_enable_listening(struct cw1200_common *priv)
{
	struct wsm_start start = {
		.mode = WSM_START_MODE_P2P_DEV,
		.band = (priv->channel->band == IEEE80211_BAND_5GHZ) ?
				WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G,
		.channelNumber = priv->channel->hw_value,
		.beaconInterval = 100,
		.DTIMPeriod = 1,
		.probeDelay = 0,
		.basicRateSet = 0x0F,
	};
	return wsm_start(priv, &start);
}

static inline int cw1200_disable_listening(struct cw1200_common *priv)
{
	struct wsm_reset reset = {
		.reset_statistics = true,
	};
	return wsm_reset(priv, &reset);
}

void cw1200_update_listening(struct cw1200_common *priv, bool enabled)
{
	if (enabled) {
		switch (priv->join_status) {
		case CW1200_JOIN_STATUS_PASSIVE:
			if (!WARN_ON(cw1200_enable_listening(priv)))
				priv->join_status = CW1200_JOIN_STATUS_MONITOR;
			break;
		default:
			break;
		}
	} else {
		switch (priv->join_status) {
		case CW1200_JOIN_STATUS_MONITOR:
			if (!WARN_ON(cw1200_disable_listening(priv)))
				priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
		default:
			break;
		}
	}
}


/* ******************************************************************** */
/* STA privates								*/

static int cw1200_cancel_scan(struct cw1200_common *priv)
{
	/* STUB(); */
	return 0;
}
