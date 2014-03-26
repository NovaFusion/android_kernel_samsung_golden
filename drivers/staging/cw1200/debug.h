#ifndef CW1200_DEBUG_H_INCLUDED
#define CW1200_DEBUG_H_INCLUDED

struct cw200_common;

#ifdef CONFIG_CW1200_DEBUGFS

struct cw1200_debug_priv {
	struct dentry *debugfs_phy;
	int tx;
	int tx_agg;
	int rx;
	int rx_agg;
	int tx_multi;
	int tx_multi_frames;
	int tx_cache_miss;
	int tx_copy;
};

int cw1200_debug_init(struct cw1200_common *priv);
void cw1200_debug_release(struct cw1200_common *priv);

static inline void cw1200_debug_txed(struct cw1200_common *priv)
{
	++priv->debug->tx;
}

static inline void cw1200_debug_txed_agg(struct cw1200_common *priv)
{
	++priv->debug->tx_agg;
}

static inline void cw1200_debug_txed_multi(struct cw1200_common *priv,
					   int count)
{
	++priv->debug->tx_multi;
	priv->debug->tx_multi_frames += count;
}

static inline void cw1200_debug_rxed(struct cw1200_common *priv)
{
	++priv->debug->rx;
}

static inline void cw1200_debug_rxed_agg(struct cw1200_common *priv)
{
	++priv->debug->rx_agg;
}

static inline void cw1200_debug_tx_cache_miss(struct cw1200_common *priv)
{
	++priv->debug->tx_cache_miss;
}

static inline void cw1200_debug_tx_copy(struct cw1200_common *priv)
{
	++priv->debug->tx_copy;
}

#else /* CONFIG_CW1200_DEBUGFS */

static inline int cw1200_debug_init(struct cw1200_common *priv)
{
	return 0;
}

static inline void cw1200_debug_release(struct cw1200_common *priv)
{
}

static inline void cw1200_debug_txed(struct cw1200_common *priv)
{
}

static inline void cw1200_debug_txed_agg(struct cw1200_common *priv)
{
}

static inline void cw1200_debug_txed_multi(struct cw1200_common *priv,
					   int count)
{
}

static inline void cw1200_debug_rxed(struct cw1200_common *priv)
{
}

static inline void cw1200_debug_rxed_agg(struct cw1200_common *priv)
{
}

static inline void cw1200_debug_tx_cache_miss(struct cw1200_common *priv)
{
}

static inline void cw1200_debug_tx_copy(struct cw1200_common *priv)
{
}

#endif /* CONFIG_CW1200_DEBUGFS */

#endif /* CW1200_DEBUG_H_INCLUDED */
