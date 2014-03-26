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

#ifndef CW1200_QUEUE_H_INCLUDED
#define CW1200_QUEUE_H_INCLUDED

/* private */ struct cw1200_queue_item;

/* extern */ struct sk_buff;
/* extern */ struct wsm_tx;
/* extern */ struct cw1200_common;
/* extern */ struct ieee80211_tx_queue_stats;

/* forward */ struct cw1200_queue_stats;

struct cw1200_queue {
	struct cw1200_queue_stats *stats;
	size_t			capacity;
	size_t			num_queued;
	size_t			num_pending;
	size_t			num_sent;
	struct cw1200_queue_item *pool;
	struct list_head	queue;
	struct list_head	free_pool;
	struct list_head	pending;
	int			tx_locked_cnt;
	int			*link_map_cache;
	bool			overfull;
	spinlock_t		lock;
	u8			queue_id;
	u8			generation;
};

struct cw1200_queue_stats {
	spinlock_t		lock;
	int			*link_map_cache;
	int			num_queued;
	size_t			map_capacity;
	wait_queue_head_t	wait_link_id_empty;
};

int cw1200_queue_stats_init(struct cw1200_queue_stats *stats,
			    size_t map_capacity);
int cw1200_queue_init(struct cw1200_queue *queue,
		      struct cw1200_queue_stats *stats,
		      u8 queue_id,
		      size_t capacity);
int cw1200_queue_clear(struct cw1200_queue *queue);
void cw1200_queue_stats_deinit(struct cw1200_queue_stats *stats);
void cw1200_queue_deinit(struct cw1200_queue *queue);

size_t cw1200_queue_get_num_queued(struct cw1200_queue *queue,
				   u32 link_id_map);
int cw1200_queue_put(struct cw1200_queue *queue, struct cw1200_common *cw1200,
			struct sk_buff *skb, u8 link_id);
int cw1200_queue_get(struct cw1200_queue *queue,
		     u32 link_id_map,
		     struct wsm_tx **tx,
		     struct ieee80211_tx_info **tx_info);
int cw1200_queue_requeue(struct cw1200_queue *queue, u32 packetID);
int cw1200_queue_requeue_all(struct cw1200_queue *queue);
int cw1200_queue_remove(struct cw1200_queue *queue, struct cw1200_common *priv,
				u32 packetID);
int cw1200_queue_get_skb(struct cw1200_queue *queue, u32 packetID,
				struct sk_buff **skb);
void cw1200_queue_lock(struct cw1200_queue *queue,
			struct cw1200_common *cw1200);
void cw1200_queue_unlock(struct cw1200_queue *queue,
				struct cw1200_common *cw1200);

bool cw1200_queue_stats_is_empty(struct cw1200_queue_stats *stats,
				 u32 link_id_map);

/* int cw1200_queue_get_stats(struct cw1200_queue *queue,
struct ieee80211_tx_queue_stats *stats); */

static inline u8 cw1200_queue_get_queue_id(u32 packetID)
{
	return (packetID >> 16) & 0xFF;
}

#endif /* CW1200_QUEUE_H_INCLUDED */
