/*
 * mac80211 glue code for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on:
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 * - stlc45xx driver
 *   Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/sched.h>

#include <net/mac80211.h>

#include "cw1200.h"
#include "txrx.h"
#include "sbus.h"
#include "fwio.h"
#include "hwio.h"
#include "bh.h"
#include "sta.h"
#include "ap.h"
#include "scan.h"
#include "debug.h"

MODULE_AUTHOR("Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>");
MODULE_DESCRIPTION("Softmac ST-Ericsson CW1200 common code");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cw1200_core");

/* Accept MAC address of the form macaddr=0x00,0x80,0xE1,0x30,0x40,0x50 */
static u8 cw1200_mac_template[ETH_ALEN] = {0x00, 0x80, 0xe1, 0x00, 0x00, 0x00};
module_param_array_named(macaddr, cw1200_mac_template, byte, NULL, S_IRUGO);
MODULE_PARM_DESC(macaddr, "MAC address");

/* TODO: use rates and channels from the device */
#define RATETAB_ENT(_rate, _rateid, _flags)		\
	{						\
		.bitrate	= (_rate),		\
		.hw_value	= (_rateid),		\
		.flags		= (_flags),		\
	}

static struct ieee80211_rate cw1200_rates[] = {
	RATETAB_ENT(10,  0,   0),
	RATETAB_ENT(20,  1,   0),
	RATETAB_ENT(55,  2,   0),
	RATETAB_ENT(110, 3,   0),
	RATETAB_ENT(60,  6,  0),
	RATETAB_ENT(90,  7,  0),
	RATETAB_ENT(120, 8,  0),
	RATETAB_ENT(180, 9,  0),
	RATETAB_ENT(240, 10, 0),
	RATETAB_ENT(360, 11, 0),
	RATETAB_ENT(480, 12, 0),
	RATETAB_ENT(540, 13, 0),
	RATETAB_ENT(65,  14, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(130, 15, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(195, 16, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(260, 17, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(390, 18, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(520, 19, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(585, 20, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(650, 21, IEEE80211_TX_RC_MCS),
};

#define cw1200_a_rates		(cw1200_rates + 4)
#define cw1200_a_rates_size	(ARRAY_SIZE(cw1200_rates) - 4)
#define cw1200_g_rates		(cw1200_rates + 0)
#define cw1200_g_rates_size	(ARRAY_SIZE(cw1200_rates))
#define cw1200_n_rates		(cw1200_rates + 12)
#define cw1200_n_rates_size	(ARRAY_SIZE(cw1200_rates) - 12)


#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq	= 5000 + (5 * (_channel)),		\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

static struct ieee80211_channel cw1200_2ghz_chantable[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

#ifdef CONFIG_CW1200_5GHZ_SUPPORT
static struct ieee80211_channel cw1200_5ghz_chantable[] = {
	CHAN5G(34, 0),		CHAN5G(36, 0),
	CHAN5G(38, 0),		CHAN5G(40, 0),
	CHAN5G(42, 0),		CHAN5G(44, 0),
	CHAN5G(46, 0),		CHAN5G(48, 0),
	CHAN5G(52, 0),		CHAN5G(56, 0),
	CHAN5G(60, 0),		CHAN5G(64, 0),
	CHAN5G(100, 0),		CHAN5G(104, 0),
	CHAN5G(108, 0),		CHAN5G(112, 0),
	CHAN5G(116, 0),		CHAN5G(120, 0),
	CHAN5G(124, 0),		CHAN5G(128, 0),
	CHAN5G(132, 0),		CHAN5G(136, 0),
	CHAN5G(140, 0),		CHAN5G(149, 0),
	CHAN5G(153, 0),		CHAN5G(157, 0),
	CHAN5G(161, 0),		CHAN5G(165, 0),
	CHAN5G(184, 0),		CHAN5G(188, 0),
	CHAN5G(192, 0),		CHAN5G(196, 0),
	CHAN5G(200, 0),		CHAN5G(204, 0),
	CHAN5G(208, 0),		CHAN5G(212, 0),
	CHAN5G(216, 0),
};
#endif /* CONFIG_CW1200_5GHZ_SUPPORT */

static struct ieee80211_supported_band cw1200_band_2ghz = {
	.channels = cw1200_2ghz_chantable,
	.n_channels = ARRAY_SIZE(cw1200_2ghz_chantable),
	.bitrates = cw1200_g_rates,
	.n_bitrates = cw1200_g_rates_size,
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_SM_PS |
			IEEE80211_HT_CAP_GRN_FLD |
			/* HT Rx STBC: Rx support of one spatial stream */
			0x0100 |
			IEEE80211_HT_CAP_DELAY_BA |
			IEEE80211_HT_CAP_MAX_AMSDU,
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = 0xFF,
			.rx_highest = __cpu_to_le16(0x41),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

#ifdef CONFIG_CW1200_5GHZ_SUPPORT
static struct ieee80211_supported_band cw1200_band_5ghz = {
	.channels = cw1200_5ghz_chantable,
	.n_channels = ARRAY_SIZE(cw1200_5ghz_chantable),
	.bitrates = cw1200_a_rates,
	.n_bitrates = cw1200_a_rates_size,
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_SM_PS |
			IEEE80211_HT_CAP_GRN_FLD |
			/* HT Rx STBC: Rx support of one spatial stream */
			0x0100 |
			IEEE80211_HT_CAP_DELAY_BA |
			IEEE80211_HT_CAP_MAX_AMSDU,
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = 0xFF,
			.rx_highest = __cpu_to_le16(0x41),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};
#endif /* CONFIG_CW1200_5GHZ_SUPPORT */

static const struct ieee80211_ops cw1200_ops = {
	.start			= cw1200_start,
	.stop			= cw1200_stop,
	.add_interface		= cw1200_add_interface,
	.remove_interface	= cw1200_remove_interface,
	.tx			= cw1200_tx,
	.hw_scan		= cw1200_hw_scan,
	.set_tim		= cw1200_set_tim,
	.sta_notify		= cw1200_sta_notify,
	.sta_add		= cw1200_sta_add,
	.sta_remove		= cw1200_sta_remove,
	.set_key		= cw1200_set_key,
	.set_rts_threshold	= cw1200_set_rts_threshold,
	.config			= cw1200_config,
	.bss_info_changed	= cw1200_bss_info_changed,
	.configure_filter	= cw1200_configure_filter,
	.conf_tx		= cw1200_conf_tx,
	.get_stats		= cw1200_get_stats,
	.ampdu_action		= cw1200_ampdu_action,
	.flush			= cw1200_flush,
	/* Intentionally not offloaded:					*/
	/*.channel_switch	= cw1200_channel_switch,		*/
	/*.remain_on_channel	= cw1200_remain_on_channel,		*/
	/*.cancel_remain_on_channel = cw1200_cancel_remain_on_channel,	*/
};

struct ieee80211_hw *cw1200_init_common(size_t priv_data_len)
{
	int i;
	struct ieee80211_hw *hw;
	struct cw1200_common *priv;

	hw = ieee80211_alloc_hw(priv_data_len, &cw1200_ops);
	if (!hw)
		return NULL;

	priv = hw->priv;
	priv->hw = hw;
	priv->mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->rates = cw1200_rates; /* TODO: fetch from FW */
	priv->mcs_rates = cw1200_n_rates;
	/* Enable block ACK for every TID but voice. */
	priv->ba_tid_mask = 0x3F;

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_SUPPORTS_PS |
		    /* IEEE80211_HW_SUPPORTS_UAPSD | */
		    IEEE80211_HW_CONNECTION_MONITOR |
		    IEEE80211_HW_SUPPORTS_CQM_RSSI |
		    /* Aggregation is fully controlled by firmware.
		     * Do not need any support from the mac80211 stack */
		    /* IEEE80211_HW_AMPDU_AGGREGATION | */
#if defined(CONFIG_CW1200_USE_STE_EXTENSIONS)
		    IEEE80211_HW_SUPPORTS_CQM_BEACON_MISS |
		    IEEE80211_HW_SUPPORTS_CQM_TX_FAIL |
#endif /* CONFIG_CW1200_USE_STE_EXTENSIONS */
		    IEEE80211_HW_BEACON_FILTER;

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					  BIT(NL80211_IFTYPE_ADHOC) |
					  BIT(NL80211_IFTYPE_AP) |
					  BIT(NL80211_IFTYPE_MESH_POINT);

	hw->channel_change_time = 1000;	/* TODO: find actual value */
	/* priv->beacon_req_id = cpu_to_le32(0); */
	hw->queues = 4;
	priv->noise = -94;

	hw->max_rates = 8;
	hw->max_rate_tries = 15;
	hw->extra_tx_headroom = WSM_TX_EXTRA_HEADROOM;

	hw->sta_data_size = sizeof(struct cw1200_sta_priv);

	/*
	 * For now, disable PS by default because it affects
	 * link stability significantly.
	 */
	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &cw1200_band_2ghz;
#ifdef CONFIG_CW1200_5GHZ_SUPPORT
	hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &cw1200_band_5ghz;
#endif /* CONFIG_CW1200_5GHZ_SUPPORT */

	hw->wiphy->max_scan_ssids = 2;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;

	SET_IEEE80211_PERM_ADDR(hw, cw1200_mac_template);

	if (hw->wiphy->perm_addr[3] == 0 &&
	    hw->wiphy->perm_addr[4] == 0 &&
	    hw->wiphy->perm_addr[5] == 0) {
		get_random_bytes(&hw->wiphy->perm_addr[3], 3);
	}

	mutex_init(&priv->wsm_cmd_mux);
	mutex_init(&priv->conf_mutex);
	priv->workqueue = create_singlethread_workqueue("cw1200_wq");
	sema_init(&priv->scan.lock, 1);
	INIT_WORK(&priv->scan.work, cw1200_scan_work);
	INIT_DELAYED_WORK(&priv->scan.probe_work, cw1200_probe_work);
	INIT_DELAYED_WORK(&priv->scan.timeout, cw1200_scan_timeout);
	INIT_WORK(&priv->join_work, cw1200_join_work);
	INIT_DELAYED_WORK(&priv->join_timeout, cw1200_join_timeout);
	INIT_WORK(&priv->unjoin_work, cw1200_unjoin_work);
	INIT_WORK(&priv->wep_key_work, cw1200_wep_key_work);
	INIT_WORK(&priv->tx_policy_upload_work, tx_policy_upload_work);
	INIT_LIST_HEAD(&priv->event_queue);
	INIT_WORK(&priv->event_handler, cw1200_event_handler);
	INIT_DELAYED_WORK(&priv->bss_loss_work, cw1200_bss_loss_work);
	INIT_DELAYED_WORK(&priv->connection_loss_work,
			  cw1200_connection_loss_work);
#if defined(CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE)
	INIT_DELAYED_WORK(&priv->keep_alive_work, cw1200_keep_alive_work);
#endif /* CONFIG_CW1200_FIRMWARE_DOES_NOT_SUPPORT_KEEPALIVE */
	INIT_WORK(&priv->tx_failure_work, cw1200_tx_failure_work);
	INIT_WORK(&priv->set_tim_work, cw1200_set_tim_work);
	INIT_WORK(&priv->multicast_start_work, cw1200_multicast_start_work);
	INIT_WORK(&priv->multicast_stop_work, cw1200_multicast_stop_work);

	if (unlikely(cw1200_queue_stats_init(&priv->tx_queue_stats,
			CW1200_LINK_ID_AFTER_DTIM + 1))) {
		ieee80211_free_hw(hw);
		return NULL;
	}

	for (i = 0; i < 4; ++i) {
		if (unlikely(cw1200_queue_init(&priv->tx_queue[i],
				&priv->tx_queue_stats, i, 16))) {
			for (; i > 0; i--)
				cw1200_queue_deinit(&priv->tx_queue[i - 1]);
			cw1200_queue_stats_deinit(&priv->tx_queue_stats);
			ieee80211_free_hw(hw);
			return NULL;
		}
	}

	init_waitqueue_head(&priv->channel_switch_done);
	init_waitqueue_head(&priv->wsm_cmd_wq);
	init_waitqueue_head(&priv->wsm_startup_done);
	wsm_buf_init(&priv->wsm_cmd_buf);
	tx_policy_init(priv);

	return hw;
}
EXPORT_SYMBOL_GPL(cw1200_init_common);

int cw1200_register_common(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	int err;

	err = ieee80211_register_hw(dev);
	if (err) {
		cw1200_dbg(CW1200_DBG_ERROR, "Cannot register device (%d).\n",
				err);
		return err;
	}

#ifdef CONFIG_CW1200_LEDS
	err = cw1200_init_leds(priv);
	if (err)
		return err;
#endif /* CONFIG_CW1200_LEDS */

	cw1200_debug_init(priv);

	cw1200_dbg(CW1200_DBG_MSG, "is registered as '%s'\n",
			wiphy_name(dev->wiphy));
	return 0;
}
EXPORT_SYMBOL_GPL(cw1200_register_common);

void cw1200_free_common(struct ieee80211_hw *dev)
{
	/* struct cw1200_common *priv = dev->priv; */
	/* unsigned int i; */

	ieee80211_free_hw(dev);
}
EXPORT_SYMBOL_GPL(cw1200_free_common);

void cw1200_unregister_common(struct ieee80211_hw *dev)
{
	struct cw1200_common *priv = dev->priv;
	int i;

	cw1200_debug_release(priv);

	priv->sbus_ops->irq_unsubscribe(priv->sbus_priv);
	cw1200_unregister_bh(priv);

#ifdef CONFIG_CW1200_LEDS
	cw1200_unregister_leds(priv);
#endif /* CONFIG_CW1200_LEDS */

	ieee80211_unregister_hw(dev);
	mutex_destroy(&priv->conf_mutex);
	mutex_destroy(&priv->eeprom_mutex);

	wsm_buf_deinit(&priv->wsm_cmd_buf);

	kfree(priv->scan.ie);
	priv->scan.ie = NULL;
	priv->scan.ie_len = 0;
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;

	if (priv->skb_cache) {
		dev_kfree_skb(priv->skb_cache);
		priv->skb_cache = NULL;
	}

	for (i = 0; i < 4; ++i)
		cw1200_queue_deinit(&priv->tx_queue[i]);
	cw1200_queue_stats_deinit(&priv->tx_queue_stats);
}
EXPORT_SYMBOL_GPL(cw1200_unregister_common);

int cw1200_probe(const struct sbus_ops *sbus_ops,
		 struct sbus_priv *sbus,
		 struct device *pdev,
		 struct cw1200_common **pself)
{
	int err = -ENOMEM;
	struct ieee80211_hw *dev;
	struct cw1200_common *priv;
	struct wsm_operational_mode mode = {
		.power_mode = wsm_power_mode_quiescent,
		.disableMoreFlagUsage = true,
	};

	dev = cw1200_init_common(sizeof(struct cw1200_common));
	if (!dev)
		goto err;

	priv = dev->priv;

	priv->sbus_ops = sbus_ops;
	priv->sbus_priv = sbus;
	priv->pdev = pdev;
	SET_IEEE80211_DEV(priv->hw, pdev);

	/* WSM callbacks. */
	priv->wsm_cbc.scan_complete = cw1200_scan_complete_cb;
	priv->wsm_cbc.tx_confirm = cw1200_tx_confirm_cb;
	priv->wsm_cbc.rx = cw1200_rx_cb;
	priv->wsm_cbc.suspend_resume = cw1200_suspend_resume;
	/* priv->wsm_cbc.set_pm_complete = cw1200_set_pm_complete_cb; */
	priv->wsm_cbc.channel_switch = cw1200_channel_switch_cb;

	err = cw1200_register_bh(priv);
	if (err)
		goto err1;

	err = cw1200_load_firmware(priv);
	if (err)
		goto err2;

	if (wait_event_interruptible_timeout(priv->wsm_startup_done,
				priv->wsm_caps.firmwareReady, 3*HZ) <= 0) {
		/* TODO: Needs to find how to reset device */
		/*       in QUEUE mode properly.           */
		goto err3;
	}

	/* Set low-power mode. */
	WARN_ON(wsm_set_operational_mode(priv, &mode));

	/* Enable multi-TX confirmation */
	WARN_ON(wsm_use_multi_tx_conf(priv, true));

	err = cw1200_register_common(dev);
	if (err) {
		priv->sbus_ops->irq_unsubscribe(priv->sbus_priv);
		goto err3;
	}

	*pself = dev->priv;
	return err;

err3:
	sbus_ops->reset(sbus);
err2:
	cw1200_unregister_bh(priv);
err1:
	cw1200_free_common(dev);
err:
	return err;
}
EXPORT_SYMBOL_GPL(cw1200_probe);

void cw1200_release(struct cw1200_common *self)
{
	cw1200_unregister_common(self->hw);
	cw1200_free_common(self->hw);
	return;
}
EXPORT_SYMBOL_GPL(cw1200_release);
