/*
 * O(1) TX queue with built-in allocator for ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "net/mac80211.h"
#include "queue.h"
#include "wsm.h"
#include "txrx.h"
#include "cw1200.h"

/* private */ struct cw1200_queue_item
{
	struct list_head	head;
	struct sk_buff		*skb;
	u32			packetID;
	/* For safety purposes only. I do not trust device too much.
	 * It was observed (last time quite long time ago) that it
	 * indicates TX for a packet several times, so it was not enough
	 * to use offset or address as an uniquie ID in a
	 * queue.
	 */
	u8			generation;
	u8			link_id;
	u8			reserved[2];
};

static inline void __cw1200_queue_lock(struct cw1200_queue *queue,
						struct cw1200_common *cw1200)
{
	if (queue->tx_locked_cnt++ == 0) {
		txrx_printk(KERN_DEBUG "[TX] Queue %d is locked.\n",
				queue->queue_id);
		ieee80211_stop_queue(cw1200->hw, queue->queue_id);
	}
}

static inline void __cw1200_queue_unlock(struct cw1200_queue *queue,
						struct cw1200_common *cw1200)
{
	BUG_ON(!queue->tx_locked_cnt);
	if (--queue->tx_locked_cnt == 0) {
		txrx_printk(KERN_DEBUG "[TX] Queue %d is unlocked.\n",
				queue->queue_id);
		ieee80211_wake_queue(cw1200->hw, queue->queue_id);
	}
}

static inline void cw1200_queue_parse_id(u32 packetID, u8 *queue_generation,
						u8 *queue_id,
						u8 *item_generation,
						u8 *item_id)
{
	*item_id		= (packetID >>  0) & 0xFF;
	*item_generation	= (packetID >>  8) & 0xFF;
	*queue_id		= (packetID >> 16) & 0xFF;
	*queue_generation	= (packetID >> 24) & 0xFF;
}

static inline u32 cw1200_queue_make_packet_id(u8 queue_generation, u8 queue_id,
						u8 item_generation, u8 item_id)
{
	return ((u32)item_id << 0) |
		((u32)item_generation << 8) |
		((u32)queue_id << 16) |
		((u32)queue_generation << 24);
}

int cw1200_queue_stats_init(struct cw1200_queue_stats *stats,
			    size_t map_capacity)
{
	memset(stats, 0, sizeof(*stats));
	stats->map_capacity = map_capacity;
	spin_lock_init(&stats->lock);
	init_waitqueue_head(&stats->wait_link_id_empty);

	stats->link_map_cache = kzalloc(sizeof(int[map_capacity]),
			GFP_KERNEL);
	if (!stats->link_map_cache)
		return -ENOMEM;

	return 0;
}

int cw1200_queue_init(struct cw1200_queue *queue,
		      struct cw1200_queue_stats *stats,
		      u8 queue_id,
		      size_t capacity)
{
	size_t i;

	memset(queue, 0, sizeof(*queue));
	queue->stats = stats;
	queue->capacity = capacity;
	queue->queue_id = queue_id;
	INIT_LIST_HEAD(&queue->queue);
	INIT_LIST_HEAD(&queue->pending);
	INIT_LIST_HEAD(&queue->free_pool);
	spin_lock_init(&queue->lock);

	queue->pool = kzalloc(sizeof(struct cw1200_queue_item) * capacity,
			GFP_KERNEL);
	if (!queue->pool)
		return -ENOMEM;

	queue->link_map_cache = kzalloc(sizeof(int[stats->map_capacity]),
			GFP_KERNEL);
	if (!queue->link_map_cache) {
		kfree(queue->pool);
		queue->pool = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < capacity; ++i)
		list_add_tail(&queue->pool[i].head, &queue->free_pool);

	return 0;
}

int cw1200_queue_clear(struct cw1200_queue *queue)
{
	int i;
	struct cw1200_queue_stats *stats = queue->stats;

	spin_lock_bh(&queue->lock);
	queue->generation++;
	list_splice_tail_init(&queue->queue, &queue->pending);
	while (!list_empty(&queue->pending)) {
		struct cw1200_queue_item *item = list_first_entry(
			&queue->pending, struct cw1200_queue_item, head);
		WARN_ON(!item->skb);
		if (likely(item->skb)) {
			dev_kfree_skb_any(item->skb);
			item->skb = NULL;
		}
		list_move_tail(&item->head, &queue->free_pool);
	}
	queue->num_queued = 0;
	queue->num_pending = 0;
	queue->num_sent = 0;

	spin_lock_bh(&stats->lock);
	for (i = 0; i < stats->map_capacity; ++i) {
		stats->num_queued -= queue->link_map_cache[i];
		stats->link_map_cache[i] -= queue->link_map_cache[i];
		queue->link_map_cache[i] = 0;
	}
	spin_unlock_bh(&stats->lock);
	spin_unlock_bh(&queue->lock);
	wake_up_interruptible(&stats->wait_link_id_empty);
	return 0;
}

void cw1200_queue_stats_deinit(struct cw1200_queue_stats *stats)
{
	kfree(stats->link_map_cache);
	stats->link_map_cache = NULL;
}

void cw1200_queue_deinit(struct cw1200_queue *queue)
{
	cw1200_queue_clear(queue);
	INIT_LIST_HEAD(&queue->free_pool);
	kfree(queue->pool);
	kfree(queue->link_map_cache);
	queue->pool = NULL;
	queue->link_map_cache = NULL;
	queue->capacity = 0;
}

size_t cw1200_queue_get_num_queued(struct cw1200_queue *queue,
				   u32 link_id_map)
{
	size_t ret;
	int i, bit;
	size_t map_capacity = queue->stats->map_capacity;

	if (!link_id_map)
		return 0;

	spin_lock_bh(&queue->lock);
	if (likely(link_id_map == (u32) -1))
		ret = queue->num_queued - queue->num_pending;
	else {
		ret = 0;
		for (i = 0, bit = 1; i < map_capacity; ++i, bit <<= 1) {
			if (link_id_map & bit)
				ret += queue->link_map_cache[i];
		}
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

int cw1200_queue_put(struct cw1200_queue *queue, struct cw1200_common *priv,
			struct sk_buff *skb, u8 link_id)
{
	int ret;
	struct wsm_tx *wsm;
	struct cw1200_queue_stats *stats = queue->stats;

	wsm = (struct wsm_tx *)skb_push(skb, sizeof(struct wsm_tx));
	ret = cw1200_skb_to_wsm(priv, skb, wsm);
	if (ret)
		return ret;

	if (link_id >= queue->stats->map_capacity)
		return -EINVAL;

	spin_lock_bh(&queue->lock);
	if (!WARN_ON(list_empty(&queue->free_pool))) {
		struct cw1200_queue_item *item = list_first_entry(
			&queue->free_pool, struct cw1200_queue_item, head);
		BUG_ON(item->skb);

		list_move_tail(&item->head, &queue->queue);
		item->skb = skb;
		item->packetID = cw1200_queue_make_packet_id(
			queue->generation, queue->queue_id,
			item->generation, item - queue->pool);
		wsm->packetID = __cpu_to_le32(item->packetID);
		item->link_id = link_id;

		++queue->num_queued;
		++queue->link_map_cache[link_id];

		spin_lock_bh(&stats->lock);
		++stats->num_queued;
		++stats->link_map_cache[link_id];
		spin_unlock_bh(&stats->lock);

		if (queue->num_queued >= queue->capacity) {
			queue->overfull = true;
			__cw1200_queue_lock(queue, priv);
		}
	} else {
		ret = -ENOENT;
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

int cw1200_queue_get(struct cw1200_queue *queue,
		     u32 link_id_map,
		     struct wsm_tx **tx,
		     struct ieee80211_tx_info **tx_info)
{
	int ret = -ENOENT;
	struct cw1200_queue_item *item;
	struct cw1200_queue_stats *stats = queue->stats;
	bool wakeup_stats = false;

	spin_lock_bh(&queue->lock);
	list_for_each_entry(item, &queue->queue, head) {
		if (link_id_map & BIT(item->link_id)) {
			ret = 0;
			break;
		}
	}

	if (!WARN_ON(ret)) {
		*tx = (struct wsm_tx *)item->skb->data;
		*tx_info = IEEE80211_SKB_CB(item->skb);
		list_move_tail(&item->head, &queue->pending);
		++queue->num_pending;
		--queue->link_map_cache[item->link_id];

		spin_lock_bh(&stats->lock);
		--stats->num_queued;
		if (!--stats->link_map_cache[item->link_id])
			wakeup_stats = true;
		spin_unlock_bh(&stats->lock);
	}
	spin_unlock_bh(&queue->lock);
	if (wakeup_stats)
		wake_up_interruptible(&stats->wait_link_id_empty);
	return ret;
}

int cw1200_queue_requeue(struct cw1200_queue *queue, u32 packetID)
{
	int ret = 0;
	u8 queue_generation, queue_id, item_generation, item_id;
	struct cw1200_queue_item *item;
	struct cw1200_queue_stats *stats = queue->stats;

	cw1200_queue_parse_id(packetID, &queue_generation, &queue_id,
				&item_generation, &item_id);

	item = &queue->pool[item_id];

	spin_lock_bh(&queue->lock);
	BUG_ON(queue_id != queue->queue_id);
	if (unlikely(queue_generation != queue->generation)) {
		ret = -ENOENT;
	} else if (unlikely(item_id >= (unsigned) queue->capacity)) {
		WARN_ON(1);
		ret = -EINVAL;
	} else if (unlikely(item->generation != item_generation)) {
		WARN_ON(1);
		ret = -ENOENT;
	} else {
		struct wsm_tx *wsm = (struct wsm_tx *)item->skb->data;
		--queue->num_pending;
		++queue->link_map_cache[item->link_id];

		spin_lock_bh(&stats->lock);
		++stats->num_queued;
		++stats->link_map_cache[item->link_id];
		spin_unlock_bh(&stats->lock);

		item->generation = ++item_generation;
		item->packetID = cw1200_queue_make_packet_id(
			queue_generation, queue_id, item_generation, item_id);
		wsm->packetID = __cpu_to_le32(item->packetID);
		list_move_tail(&item->head, &queue->queue);
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

int cw1200_queue_requeue_all(struct cw1200_queue *queue)
{
	struct cw1200_queue_stats *stats = queue->stats;
	spin_lock_bh(&queue->lock);
	while (!list_empty(&queue->pending)) {
		struct cw1200_queue_item *item = list_first_entry(
			&queue->pending, struct cw1200_queue_item, head);
		struct wsm_tx *wsm = (struct wsm_tx *)item->skb->data;

		--queue->num_pending;
		++queue->link_map_cache[item->link_id];

		spin_lock_bh(&stats->lock);
		++stats->num_queued;
		++stats->link_map_cache[item->link_id];
		spin_unlock_bh(&stats->lock);

		++item->generation;
		item->packetID = cw1200_queue_make_packet_id(
			queue->generation, queue->queue_id,
			item->generation, item - queue->pool);
		wsm->packetID = __cpu_to_le32(item->packetID);
		list_move_tail(&item->head, &queue->queue);
	}
	spin_unlock_bh(&queue->lock);

	return 0;
}

int cw1200_queue_remove(struct cw1200_queue *queue, struct cw1200_common *priv,
				u32 packetID)
{
	int ret = 0;
	u8 queue_generation, queue_id, item_generation, item_id;
	struct cw1200_queue_item *item;
	struct sk_buff *skb_to_free = NULL;
	cw1200_queue_parse_id(packetID, &queue_generation, &queue_id,
				&item_generation, &item_id);

	item = &queue->pool[item_id];

	spin_lock_bh(&queue->lock);
	BUG_ON(queue_id != queue->queue_id);
	if (unlikely(queue_generation != queue->generation)) {
		ret = -ENOENT;
	} else if (unlikely(item_id >= (unsigned) queue->capacity)) {
		WARN_ON(1);
		ret = -EINVAL;
	} else if (unlikely(item->generation != item_generation)) {
		WARN_ON(1);
		ret = -ENOENT;
	} else {
		--queue->num_pending;
		--queue->num_queued;
		++queue->num_sent;
		++item->generation;
		skb_to_free = item->skb;
		item->skb = NULL;
		/* Do not use list_move_tail here, but list_move:
		 * try to utilize cache row.
		 */
		list_move(&item->head, &queue->free_pool);

		if (unlikely(queue->overfull) &&
		    (queue->num_queued <= (queue->capacity >> 1))) {
			queue->overfull = false;
			__cw1200_queue_unlock(queue, priv);
		}
	}
	spin_unlock_bh(&queue->lock);

	if (skb_to_free)
		dev_kfree_skb_any(item->skb);

	return ret;
}

int cw1200_queue_get_skb(struct cw1200_queue *queue, u32 packetID,
				struct sk_buff **skb)
{
	int ret = 0;
	u8 queue_generation, queue_id, item_generation, item_id;
	struct cw1200_queue_item *item;
	cw1200_queue_parse_id(packetID, &queue_generation, &queue_id,
				&item_generation, &item_id);

	item = &queue->pool[item_id];

	spin_lock_bh(&queue->lock);
	BUG_ON(queue_id != queue->queue_id);
	if (unlikely(queue_generation != queue->generation)) {
		ret = -ENOENT;
	} else if (unlikely(item_id >= (unsigned) queue->capacity)) {
		WARN_ON(1);
		ret = -EINVAL;
	} else if (unlikely(item->generation != item_generation)) {
		WARN_ON(1);
		ret = -ENOENT;
	} else {
		*skb = item->skb;
		item->skb = NULL;
	}
	spin_unlock_bh(&queue->lock);
	return ret;
}

void cw1200_queue_lock(struct cw1200_queue *queue, struct cw1200_common *cw1200)
{
	spin_lock_bh(&queue->lock);
	__cw1200_queue_lock(queue, cw1200);
	spin_unlock_bh(&queue->lock);
}

void cw1200_queue_unlock(struct cw1200_queue *queue,
				struct cw1200_common *cw1200)
{
	spin_lock_bh(&queue->lock);
	__cw1200_queue_unlock(queue, cw1200);
	spin_unlock_bh(&queue->lock);
}

/*
int cw1200_queue_get_stats(struct cw1200_queue *queue,
				struct ieee80211_tx_queue_stats *stats)
{
	spin_lock_bh(&queue->lock);
	stats->len = queue->num_queued;
	stats->limit = queue->capacity;
	stats->count = queue->num_sent;
	spin_unlock_bh(&queue->lock);

	return 0;
}
*/

bool cw1200_queue_stats_is_empty(struct cw1200_queue_stats *stats,
				 u32 link_id_map)
{
	bool empty = true;

	spin_lock_bh(&stats->lock);
	if (link_id_map == (u32)-1)
		empty = stats->num_queued == 0;
	else {
		int i;
		for (i = 0; i < stats->map_capacity; ++i) {
			if (link_id_map & BIT(i)) {
				if (stats->link_map_cache[i]) {
					empty = false;
					break;
				}
			}
		}
	}
	spin_unlock_bh(&stats->lock);

	return empty;
}
