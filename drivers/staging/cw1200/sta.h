/*
 * Mac80211 STA interface for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef STA_H_INCLUDED
#define STA_H_INCLUDED

/* ******************************************************************** */
/* mac80211 API								*/

int cw1200_start(struct ieee80211_hw *dev);
void cw1200_stop(struct ieee80211_hw *dev);
int cw1200_add_interface(struct ieee80211_hw *dev,
			 struct ieee80211_vif *vif);
void cw1200_remove_interface(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif);
int cw1200_config(struct ieee80211_hw *dev, u32 changed);
void cw1200_configure_filter(struct ieee80211_hw *dev,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 multicast);
int cw1200_conf_tx(struct ieee80211_hw *dev, u16 queue,
		   const struct ieee80211_tx_queue_params *params);
int cw1200_get_stats(struct ieee80211_hw *dev,
		     struct ieee80211_low_level_stats *stats);
/* Not more a part of interface?
int cw1200_get_tx_stats(struct ieee80211_hw *dev,
			struct ieee80211_tx_queue_stats *stats);
*/
int cw1200_set_key(struct ieee80211_hw *dev, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key);

int cw1200_set_rts_threshold(struct ieee80211_hw *hw, u32 value);

void cw1200_flush(struct ieee80211_hw *hw, bool drop);

/* ******************************************************************** */
/* WSM callbacks							*/

/* void cw1200_set_pm_complete_cb(struct cw1200_common *priv,
	struct wsm_set_pm_complete *arg); */
void cw1200_channel_switch_cb(struct cw1200_common *priv);

/* ******************************************************************** */
/* WSM events								*/

void cw1200_free_event_queue(struct cw1200_common *priv);
void cw1200_event_handler(struct work_struct *work);
void cw1200_bss_loss_work(struct work_struct *work);
void cw1200_connection_loss_work(struct work_struct *work);
void cw1200_keep_alive_work(struct work_struct *work);
void cw1200_tx_failure_work(struct work_struct *work);

/* ******************************************************************** */
/* Internal API								*/

int cw1200_setup_mac(struct cw1200_common *priv);
void cw1200_join_work(struct work_struct *work);
void cw1200_join_timeout(struct work_struct *work);
void cw1200_unjoin_work(struct work_struct *work);
void cw1200_wep_key_work(struct work_struct *work);
void cw1200_update_listening(struct cw1200_common *priv, bool enabled);
void cw1200_update_filtering(struct cw1200_common *priv);

#endif
