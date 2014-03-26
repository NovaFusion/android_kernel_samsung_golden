/*
 * Datapath implementation for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/mac80211.h>

#include "cw1200.h"
#include "wsm.h"
#include "bh.h"
#include "debug.h"

#if defined(CONFIG_CW1200_TX_POLICY_DEBUG)
#define tx_policy_printk(...) printk(__VA_ARGS__)
#else
#define tx_policy_printk(...)
#endif

static int cw1200_handle_action_rx(struct cw1200_common *priv,
				   struct sk_buff *skb);
static int cw1200_handle_action_tx(struct cw1200_common *priv,
				   struct sk_buff *skb);
static const struct ieee80211_rate *
cw1200_get_tx_rate(const struct cw1200_common *priv,
		   const struct ieee80211_tx_rate *rate);

/* ******************************************************************** */
/* TX queue lock / unlock						*/

static inline void cw1200_tx_queues_lock(struct cw1200_common *priv)
{
	int i;
	for (i = 0; i < 4; ++i)
		cw1200_queue_lock(&priv->tx_queue[i], priv);
}

static inline void cw1200_tx_queues_unlock(struct cw1200_common *priv)
{
	int i;
	for (i = 0; i < 4; ++i)
		cw1200_queue_unlock(&priv->tx_queue[i], priv);
}

/* ******************************************************************** */
/* TX policy cache implementation					*/

static void tx_policy_dump(struct tx_policy *policy)
{
	tx_policy_printk(KERN_DEBUG "[TX policy] "
		"%.1X%.1X%.1X%.1X%.1X%.1X%.1X%.1X"
		"%.1X%.1X%.1X%.1X%.1X%.1X%.1X%.1X"
		"%.1X%.1X%.1X%.1X%.1X%.1X%.1X%.1X: %d\n",
		policy->raw[0] & 0x0F,  policy->raw[0] >> 4,
		policy->raw[1] & 0x0F,  policy->raw[1] >> 4,
		policy->raw[2] & 0x0F,  policy->raw[2] >> 4,
		policy->raw[3] & 0x0F,  policy->raw[3] >> 4,
		policy->raw[4] & 0x0F,  policy->raw[4] >> 4,
		policy->raw[5] & 0x0F,  policy->raw[5] >> 4,
		policy->raw[6] & 0x0F,  policy->raw[6] >> 4,
		policy->raw[7] & 0x0F,  policy->raw[7] >> 4,
		policy->raw[8] & 0x0F,  policy->raw[8] >> 4,
		policy->raw[9] & 0x0F,  policy->raw[9] >> 4,
		policy->raw[10] & 0x0F,  policy->raw[10] >> 4,
		policy->raw[11] & 0x0F,  policy->raw[11] >> 4,
		policy->defined);
}

static void tx_policy_build(const struct cw1200_common *priv,
	/* [out] */ struct tx_policy *policy,
	struct ieee80211_tx_rate *rates, size_t count)
{
	int i, j;
	unsigned limit = priv->short_frame_max_tx_count;
	unsigned total = 0;
	BUG_ON(rates[0].idx < 0);
	memset(policy, 0, sizeof(*policy));

	/* minstrel is buggy a little bit, so distille
	 * incoming rates first. */

	/* Sort rates in descending order. */
	for (i = 1; i < count; ++i) {
		if (rates[i].idx < 0) {
			count = i;
			break;
		}
		if (rates[i].idx > rates[i - 1].idx) {
			struct ieee80211_tx_rate tmp = rates[i - 1];
			rates[i - 1] = rates[i];
			rates[i] = tmp;
		}
	}

	/* Eliminate duplicates. */
	total = rates[0].count;
	for (i = 0, j = 1; j < count; ++j) {
		if (rates[j].idx == rates[i].idx) {
			rates[i].count += rates[j].count;
		} else if (rates[j].idx > rates[i].idx) {
			break;
		} else {
			++i;
			if (i != j)
				rates[i] = rates[j];
		}
		total += rates[j].count;
	}
	count = i + 1;

	/* Re-fill policy trying to keep every requested rate and with
	 * respect to the global max tx retransmission count. */
	if (limit < count)
		limit = count;
	if (total > limit) {
		for (i = 0; i < count; ++i) {
			int left = count - i - 1;
			if (rates[i].count > limit - left)
				rates[i].count = limit - left;
			limit -= rates[i].count;
		}
	}
	policy->defined = cw1200_get_tx_rate(priv, &rates[0])->hw_value + 1;

	for (i = 0; i < count; ++i) {
		register unsigned rateid, off, shift, retries;

		rateid = cw1200_get_tx_rate(priv, &rates[i])->hw_value;
		off = rateid >> 3;		/* eq. rateid / 8 */
		shift = (rateid & 0x07) << 2;	/* eq. (rateid % 8) * 4 */

		retries = rates[i].count;
		if (unlikely(retries > 0x0F))
			rates[i].count = retries = 0x0F;
		policy->tbl[off] |= __cpu_to_le32(retries << shift);
		policy->retry_count += retries;
	}

	tx_policy_printk(KERN_DEBUG "[TX policy] Policy (%d): " \
		"%d:%d, %d:%d, %d:%d, %d:%d, %d:%d\n",
		count,
		rates[0].idx, rates[0].count,
		rates[1].idx, rates[1].count,
		rates[2].idx, rates[2].count,
		rates[3].idx, rates[3].count,
		rates[4].idx, rates[4].count);
}

static inline bool tx_policy_is_equal(const struct tx_policy *wanted,
					const struct tx_policy *cached)
{
	size_t count = wanted->defined >> 1;
	if (wanted->defined > cached->defined)
		return false;
	if (count) {
		if (memcmp(wanted->raw, cached->raw, count))
			return false;
	}
	if (wanted->defined & 1) {
		if ((wanted->raw[count] & 0x0F) != (cached->raw[count] & 0x0F))
			return false;
	}
	return true;
}

static int tx_policy_find(struct tx_policy_cache *cache,
				const struct tx_policy *wanted)
{
	/* O(n) complexity. Not so good, but there's only 8 entries in
	 * the cache.
	 * Also lru helps to reduce search time. */
	struct tx_policy_cache_entry *it;
	/* First search for policy in "used" list */
	list_for_each_entry(it, &cache->used, link) {
		if (tx_policy_is_equal(wanted, &it->policy))
			return it - cache->cache;
	}
	/* Then - in "free list" */
	list_for_each_entry(it, &cache->free, link) {
		if (tx_policy_is_equal(wanted, &it->policy))
			return it - cache->cache;
	}
	return -1;
}

static inline void tx_policy_use(struct tx_policy_cache *cache,
				 struct tx_policy_cache_entry *entry)
{
	++entry->policy.usage_count;
	list_move(&entry->link, &cache->used);
}

static inline int tx_policy_release(struct tx_policy_cache *cache,
				    struct tx_policy_cache_entry *entry)
{
	int ret = --entry->policy.usage_count;
	if (!ret)
		list_move(&entry->link, &cache->free);
	return ret;
}

/* ******************************************************************** */
/* External TX policy cache API						*/

void tx_policy_init(struct cw1200_common *priv)
{
	struct tx_policy_cache *cache = &priv->tx_policy_cache;
	int i;

	memset(cache, 0, sizeof(*cache));

	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->used);
	INIT_LIST_HEAD(&cache->free);

	for (i = 0; i < TX_POLICY_CACHE_SIZE; ++i)
		list_add(&cache->cache[i].link, &cache->free);
}

static int tx_policy_get(struct cw1200_common *priv,
		  struct ieee80211_tx_rate *rates,
		  size_t count, bool *renew)
{
	int idx;
	struct tx_policy_cache *cache = &priv->tx_policy_cache;
	struct tx_policy wanted;

	tx_policy_build(priv, &wanted, rates, count);

	spin_lock_bh(&cache->lock);
	BUG_ON(list_empty(&cache->free));
	idx = tx_policy_find(cache, &wanted);
	if (idx >= 0) {
		tx_policy_printk(KERN_DEBUG "[TX policy] Used TX policy: %d\n",
					idx);
		*renew = false;
	} else {
		struct tx_policy_cache_entry *entry;
		*renew = true;
		/* If policy is not found create a new one
		 * using the oldest entry in "free" list */
		entry = list_entry(cache->free.prev,
			struct tx_policy_cache_entry, link);
		entry->policy = wanted;
		idx = entry - cache->cache;
		tx_policy_printk(KERN_DEBUG "[TX policy] New TX policy: %d\n",
					idx);
		tx_policy_dump(&entry->policy);
	}
	tx_policy_use(cache, &cache->cache[idx]);
	if (unlikely(list_empty(&cache->free))) {
		/* Lock TX queues. */
		cw1200_tx_queues_lock(priv);
	}
	spin_unlock_bh(&cache->lock);
	return idx;
}

void tx_policy_put(struct cw1200_common *priv, int idx)
{
	int usage, locked;
	struct tx_policy_cache *cache = &priv->tx_policy_cache;

	spin_lock_bh(&cache->lock);
	locked = list_empty(&cache->free);
	usage = tx_policy_release(cache, &cache->cache[idx]);
	if (unlikely(locked) && !usage) {
		/* Unlock TX queues. */
		cw1200_tx_queues_unlock(priv);
	}
	spin_unlock_bh(&cache->lock);
}

/*
bool tx_policy_cache_full(struct cw1200_common *priv)
{
	bool ret;
	struct tx_policy_cache *cache = &priv->tx_policy_cache;
	spin_lock_bh(&cache->lock);
	ret = list_empty(&cache->free);
	spin_unlock_bh(&cache->lock);
	return ret;
}
*/

static int tx_policy_upload(struct cw1200_common *priv)
{
	struct tx_policy_cache *cache = &priv->tx_policy_cache;
	int i;
	struct wsm_set_tx_rate_retry_policy arg = {
		.hdr = {
			.numTxRatePolicies = 0,
		}
	};
	spin_lock_bh(&cache->lock);

	/* Upload only modified entries. */
	for (i = 0; i < TX_POLICY_CACHE_SIZE; ++i) {
		struct tx_policy *src = &cache->cache[i].policy;
		if (src->retry_count && !src->uploaded) {
			struct wsm_set_tx_rate_retry_policy_policy *dst =
				&arg.tbl[arg.hdr.numTxRatePolicies];
			dst->policyIndex = i;
			dst->shortRetryCount = priv->short_frame_max_tx_count;
			dst->longRetryCount = priv->long_frame_max_tx_count;

			/* BIT(2) - Terminate retries when Tx rate retry policy
			 *          finishes.
			 * BIT(3) - Count initial frame transmission as part of
			 *          rate retry counting but not as a retry
			 *          attempt */
			dst->policyFlags = BIT(2) | BIT(3);

			memcpy(dst->rateCountIndices, src->tbl,
					sizeof(dst->rateCountIndices));
			src->uploaded = 1;
			++arg.hdr.numTxRatePolicies;
		}
	}
	spin_unlock_bh(&cache->lock);
	cw1200_debug_tx_cache_miss(priv);
	tx_policy_printk(KERN_DEBUG "[TX policy] Upload %d policies\n",
				arg.hdr.numTxRatePolicies);
	return wsm_set_tx_rate_retry_policy(priv, &arg);
}

void tx_policy_upload_work(struct work_struct *work)
{
	struct cw1200_common *priv =
		container_of(work, struct cw1200_common, tx_policy_upload_work);

	tx_policy_printk(KERN_DEBUG "[TX] TX policy upload.\n");
	WARN_ON(tx_policy_upload(priv));

	wsm_unlock_tx(priv);
	cw1200_tx_queues_unlock(priv);
}

/* ******************************************************************** */
/* cw1200 TX implementation						*/

u32 cw1200_rate_mask_to_wsm(struct cw1200_common *priv, u32 rates)
{
	u32 ret = 0;
	int i;
	for (i = 0; i < 32; ++i) {
		if (rates & (1 << i))
			ret |= 1 << priv->rates[i].hw_value;
	}
	return ret;
}

static const struct ieee80211_rate *
cw1200_get_tx_rate(const struct cw1200_common *priv,
		   const struct ieee80211_tx_rate *rate)
{
	if (rate->idx < 0)
		return NULL;
	if (rate->flags & IEEE80211_TX_RC_MCS)
		return &priv->mcs_rates[rate->idx];
	return &priv->hw->wiphy->bands[priv->channel->band]->
		bitrates[rate->idx];
}

/* NOTE: cw1200_skb_to_wsm executes in atomic context. */
int cw1200_skb_to_wsm(struct cw1200_common *priv, struct sk_buff *skb,
			struct wsm_tx *wsm)
{
	bool tx_policy_renew = false;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	const struct ieee80211_rate *rate = cw1200_get_tx_rate(priv,
		&tx_info->control.rates[0]);

	memset(wsm, 0, sizeof(*wsm));
	wsm->hdr.len = __cpu_to_le16(skb->len);
	wsm->hdr.id = __cpu_to_le16(0x0004);
	if (rate) {
		wsm->maxTxRate = rate->hw_value;
		if (rate->flags & IEEE80211_TX_RC_MCS) {
			if (cw1200_ht_greenfield(&priv->ht_info))
				wsm->htTxParameters |=
					__cpu_to_le32(WSM_HT_TX_GREENFIELD);
			else
				wsm->htTxParameters |=
					__cpu_to_le32(WSM_HT_TX_MIXED);
		}
	}
	wsm->flags = tx_policy_get(priv,
		tx_info->control.rates, IEEE80211_TX_MAX_RATES,
		&tx_policy_renew) << 4;

	if (tx_policy_renew) {
		tx_policy_printk(KERN_DEBUG "[TX] TX policy renew.\n");
		/* It's not so optimal to stop TX queues every now and then.
		 * Maybe it's better to reimplement task scheduling with
		 * a counter. */
		/* cw1200_tx_queues_lock(priv); */
		/* Definetly better. TODO. */
		wsm_lock_tx_async(priv);
		cw1200_tx_queues_lock(priv);
		queue_work(priv->workqueue, &priv->tx_policy_upload_work);
	}

	wsm->queueId = wsm_queue_id_to_wsm(skb_get_queue_mapping(skb));
	return 0;
}

/* ******************************************************************** */

int cw1200_tx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct cw1200_common *priv = dev->priv;
	unsigned queue = skb_get_queue_mapping(skb);
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr =
		(struct ieee80211_hdr *)skb->data;
	struct cw1200_sta_priv *sta_priv =
		(struct cw1200_sta_priv *)&tx_info->control.sta->drv_priv;
	int link_id = 0;
	int ret;

	if (tx_info->flags | IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		link_id = CW1200_LINK_ID_AFTER_DTIM;
	else if (tx_info->control.sta)
		link_id = sta_priv->link_id;

	txrx_printk(KERN_DEBUG "[TX] TX %d bytes (queue: %d, link_id: %d).\n",
			skb->len, queue, link_id);

	if (WARN_ON(queue >= 4))
		goto err;

#if 0
	{
		/* HACK!!!
		* Workarounnd against a bug in WSM_A21.05.0288 firmware.
		* In AP mode FW calculates FCS incorrectly when DA
		* is FF:FF:FF:FF:FF:FF. Just for verification,
		* do not enable this code in the real live. */
		static const u8 mac_ff[] =
			{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		static const u8 mac_mc[] =
			{0x01, 0x00, 0x5e, 0x00, 0x00, 0x16};
		if (!memcmp(&skb->data[4], mac_ff, sizeof(mac_ff)))
			memcpy(&skb->data[4], mac_mc, sizeof(mac_mc));
	}
#endif


	/* IV/ICV injection. */
	/* TODO: Quite unoptimal. It's better co modify mac80211
	 * to reserve space for IV */
	if (tx_info->control.hw_key &&
	    (hdr->frame_control &
	     __cpu_to_le32(IEEE80211_FCTL_PROTECTED))) {
		size_t hdrlen = ieee80211_hdrlen(hdr->frame_control);
		size_t iv_len = tx_info->control.hw_key->iv_len;
		size_t icv_len = tx_info->control.hw_key->icv_len;
		u8 *icv;
		u8 *newhdr;

		if (tx_info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
			icv_len += 8; /* MIC */
		}

		if (skb_headroom(skb) < iv_len + WSM_TX_EXTRA_HEADROOM
				|| skb_tailroom(skb) < icv_len) {
			wiphy_err(priv->hw->wiphy,
				"Bug: no space allocated "
				"for crypto headers.\n"
				"headroom: %d, tailroom: %d, "
				"req_headroom: %d, req_tailroom: %d\n"
				"Please fix it in cw1200_get_skb().\n",
				skb_headroom(skb), skb_tailroom(skb),
				iv_len + WSM_TX_EXTRA_HEADROOM, icv_len);
			goto err;
		}

		newhdr = skb_push(skb, iv_len);
		memmove(newhdr, newhdr + iv_len, hdrlen);
		memset(&newhdr[hdrlen], 0, iv_len);
		icv = skb_put(skb, icv_len);
		memset(icv, 0, icv_len);
	}

	if ((size_t)skb->data & 3) {
		size_t offset = (size_t)skb->data & 3;
		u8 *p;
		if (skb_headroom(skb) < 4) {
			wiphy_err(priv->hw->wiphy,
					"Bug: no space allocated "
					"for DMA alignment.\n"
					"headroom: %d\n",
					skb_headroom(skb));
			goto err;
		}
		p = skb_push(skb, offset);
		memmove(p, &p[offset], skb->len - offset);
		skb_trim(skb, skb->len - offset);
		cw1200_debug_tx_copy(priv);
	}

	if (ieee80211_is_action(hdr->frame_control))
		if (cw1200_handle_action_tx(priv, skb))
			goto drop;

	ret = cw1200_queue_put(&priv->tx_queue[queue], priv, skb,
			link_id);
	if (!WARN_ON(ret))
		cw1200_bh_wakeup(priv);
	else
		goto err;

	return NETDEV_TX_OK;

err:
	/* TODO: Update TX failure counters */
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/* ******************************************************************** */

static int cw1200_handle_action_rx(struct cw1200_common *priv,
				   struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	/* Filter block ACK negotiation: fully controlled by firmware */
	if (mgmt->u.action.category == WLAN_CATEGORY_BACK)
		return 1;

	return 0;
}

static int cw1200_handle_action_tx(struct cw1200_common *priv,
				   struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;

	/* Filter block ACK negotiation: fully controlled by firmware */
	if (mgmt->u.action.category == WLAN_CATEGORY_BACK)
		return 1;

	return 0;
}

/* ******************************************************************** */

void cw1200_tx_confirm_cb(struct cw1200_common *priv,
			  struct wsm_tx_confirm *arg)
{
	u8 queue_id = cw1200_queue_get_queue_id(arg->packetID);
	struct cw1200_queue *queue = &priv->tx_queue[queue_id];
	struct sk_buff *skb;

	txrx_printk(KERN_DEBUG "[TX] TX confirm.\n");

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		return;
	}

	if (WARN_ON(queue_id >= 4))
		return;

	if ((arg->status == WSM_REQUEUE) &&
	    (arg->flags & WSM_TX_STATUS_REQUEUE)) {
		WARN_ON(cw1200_queue_requeue(queue, arg->packetID));
	} else if (!WARN_ON(cw1200_queue_get_skb(queue, arg->packetID, &skb))) {
		struct ieee80211_tx_info *tx = IEEE80211_SKB_CB(skb);
		struct wsm_tx *wsm_tx = (struct wsm_tx *)skb->data;
		int rate_id = (wsm_tx->flags >> 4) & 0x07;
		int tx_count = arg->ackFailures;
		u8 ht_flags = 0;
		int i;

		if (cw1200_ht_greenfield(&priv->ht_info))
			ht_flags |= IEEE80211_TX_RC_GREEN_FIELD;

		/* Release used TX rate policy */
		tx_policy_put(priv, rate_id);

		if (likely(!arg->status)) {
			tx->flags |= IEEE80211_TX_STAT_ACK;
#if defined(CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
			priv->last_activity_time = jiffies;
#endif /* CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
			priv->cqm_tx_failure_count = 0;
			++tx_count;
			cw1200_debug_txed(priv);
			if (arg->flags & WSM_TX_STATUS_AGGREGATION) {
				/* Do not report aggregation to mac80211:
				 * it confuses minstrel a lot. */
				/* tx->flags |= IEEE80211_TX_STAT_AMPDU; */
				cw1200_debug_txed_agg(priv);
			}
		} else {
			/* TODO: Update TX failure counters */
			if (unlikely(priv->cqm_tx_failure_thold &&
			     (++priv->cqm_tx_failure_count >
			      priv->cqm_tx_failure_thold))) {
				priv->cqm_tx_failure_thold = 0;
				queue_work(priv->workqueue,
						&priv->tx_failure_work);
			}
		}

		for (i = 0; i < IEEE80211_TX_MAX_RATES; ++i) {
			if (tx->status.rates[i].count >= tx_count) {
				tx->status.rates[i].count = tx_count;
				break;
			}
			tx_count -= tx->status.rates[i].count;
			if (tx->status.rates[i].flags & IEEE80211_TX_RC_MCS)
				tx->status.rates[i].flags |= ht_flags;
		}

		for (++i; i < IEEE80211_TX_MAX_RATES; ++i) {
			tx->status.rates[i].count = 0;
			tx->status.rates[i].idx = -1;
		}

		skb_pull(skb, sizeof(struct wsm_tx));
		ieee80211_tx_status(priv->hw, skb);

		WARN_ON(cw1200_queue_remove(queue, priv, arg->packetID));
	}
}

void cw1200_rx_cb(struct cw1200_common *priv,
		  struct wsm_rx *arg,
		  struct sk_buff **skb_p)
{
	struct sk_buff *skb = *skb_p;
	struct ieee80211_rx_status *hdr = IEEE80211_SKB_RXCB(skb);
	const struct ieee80211_rate *rate;
	__le16 frame_control;
	hdr->flag = 0;

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		goto drop;
	}

	if (unlikely(arg->status)) {
		if (arg->status == WSM_STATUS_MICFAILURE) {
			txrx_printk(KERN_DEBUG "[RX] MIC failure.\n");
			hdr->flag |= RX_FLAG_MMIC_ERROR;
		} else if (arg->status == WSM_STATUS_NO_KEY_FOUND) {
			txrx_printk(KERN_DEBUG "[RX] No key found.\n");
			goto drop;
		} else {
			txrx_printk(KERN_DEBUG "[RX] Receive failure: %d.\n",
				arg->status);
			goto drop;
		}
	}

	if (skb->len < sizeof(struct ieee80211_hdr_3addr)) {
		wiphy_warn(priv->hw->wiphy, "Mailformed SDU rx'ed. "
				"Size is lesser than IEEE header.\n");
		goto drop;
	}

	frame_control = *(__le16*)skb->data;
	hdr->mactime = 0; /* Not supported by WSM */
	hdr->freq = ieee80211_channel_to_frequency(arg->channelNumber);
	hdr->band = (hdr->freq >= 5000) ?
		IEEE80211_BAND_5GHZ : IEEE80211_BAND_2GHZ;

	if (arg->rxedRate >= 4)
		rate = &priv->rates[arg->rxedRate - 2];
	else
		rate = &priv->rates[arg->rxedRate];

	if (rate >= priv->mcs_rates) {
		hdr->rate_idx = rate - priv->mcs_rates;
		hdr->flag |= RX_FLAG_HT;
	} else {
		hdr->rate_idx = rate - priv->rates;
	}

	hdr->signal = (s8)arg->rcpiRssi;
	hdr->antenna = 0;

	if (WSM_RX_STATUS_ENCRYPTION(arg->flags)) {
		size_t iv_len = 0, icv_len = 0;
		size_t hdrlen = ieee80211_hdrlen(frame_control);

		hdr->flag |= RX_FLAG_DECRYPTED | RX_FLAG_IV_STRIPPED;

		/* Oops... There is no fast way to ask mac80211 about
		 * IV/ICV lengths. Even defineas are not exposed.*/
		switch (WSM_RX_STATUS_ENCRYPTION(arg->flags)) {
		case WSM_RX_STATUS_WEP:
			iv_len = 4 /* WEP_IV_LEN */;
			icv_len = 4 /* WEP_ICV_LEN */;
			break;
		case WSM_RX_STATUS_TKIP:
			iv_len = 8 /* TKIP_IV_LEN */;
			icv_len = 4 /* TKIP_ICV_LEN */
				+ 8 /*MICHAEL_MIC_LEN*/;
			hdr->flag |= RX_FLAG_MMIC_STRIPPED;
			break;
		case WSM_RX_STATUS_AES:
			iv_len = 8 /* CCMP_HDR_LEN */;
			icv_len = 8 /* CCMP_MIC_LEN */;
			break;
		case WSM_RX_STATUS_WAPI:
			iv_len = 18 /* WAPI_HDR_LEN */;
			icv_len = 16 /* WAPI_MIC_LEN */;
			break;
		default:
			WARN_ON("Unknown encryption type");
			goto drop;
		}

		if (skb->len < hdrlen + iv_len + icv_len) {
			wiphy_warn(priv->hw->wiphy, "Mailformed SDU rx'ed. "
				"Size is lesser than crypto headers.\n");
			goto drop;
		}

		/* Remove IV, ICV and MIC */
		skb_trim(skb, skb->len - icv_len);
		memmove(skb->data + iv_len, skb->data, hdrlen);
		skb_pull(skb, iv_len);
	}

	cw1200_debug_rxed(priv);
	if (arg->flags & WSM_RX_STATUS_AGGREGATE)
		cw1200_debug_rxed_agg(priv);

	if (ieee80211_is_action(frame_control) &&
			(arg->flags & WSM_RX_STATUS_ADDRESS1))
		if (cw1200_handle_action_rx(priv, skb))
			return;

	/* Not that we really need _irqsafe variant here,
	 * but it offloads realtime bh thread and improve
	 * system performance. */
	ieee80211_rx_irqsafe(priv->hw, skb);
	*skb_p = NULL;
	return;

drop:
	/* TODO: update failure counters */
	return;
}

/* ******************************************************************** */
/* Security								*/

int cw1200_alloc_key(struct cw1200_common *priv)
{
	int idx;

	idx = ffs(~priv->key_map) - 1;
	if (idx < 0 || idx > WSM_KEY_MAX_INDEX)
		return -1;

	priv->key_map |= 1 << idx;
	priv->keys[idx].entryIndex = idx;
	return idx;
}

void cw1200_free_key(struct cw1200_common *priv, int idx)
{
	BUG_ON(!(priv->key_map & (1 << idx)));
	memset(&priv->keys[idx], 0, sizeof(priv->keys[idx]));
	priv->key_map &= ~(1 << idx);
}

void cw1200_free_keys(struct cw1200_common *priv)
{
	memset(&priv->keys, 0, sizeof(priv->keys));
	priv->key_map = 0;
}

int cw1200_upload_keys(struct cw1200_common *priv)
{
	int idx, ret = 0;
	for (idx = 0; idx <= WSM_KEY_MAX_INDEX; ++idx)
		if (priv->key_map & (1 << idx)) {
			ret = wsm_add_key(priv, &priv->keys[idx]);
			if (ret < 0)
				break;
		}
	return ret;
}
