/*
 *  HT-related code for ST-Ericsson CW1200 driver
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_HT_H_INCLUDED
#define CW1200_HT_H_INCLUDED

#include <net/mac80211.h>

struct cw1200_ht_info {
	struct ieee80211_sta_ht_cap	ht_cap;
	enum nl80211_channel_type	channel_type;
	u16				operation_mode;
};

static inline int cw1200_is_ht(const struct cw1200_ht_info *ht_info)
{
	return ht_info->channel_type != NL80211_CHAN_NO_HT;
}

static inline int cw1200_ht_greenfield(const struct cw1200_ht_info *ht_info)
{
	return cw1200_is_ht(ht_info) &&
		(ht_info->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD) &&
		!(ht_info->operation_mode &
			IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
}

static inline int cw1200_ht_ampdu_density(const struct cw1200_ht_info *ht_info)
{
	if (!cw1200_is_ht(ht_info))
		return 0;
	return ht_info->ht_cap.ampdu_density;
}

#endif /* CW1200_HT_H_INCLUDED */
