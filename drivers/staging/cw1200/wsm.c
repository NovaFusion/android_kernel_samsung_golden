/*
 * WSM host interface (HI) implementation for
 * ST-Ericsson CW1200 mac80211 drivers.
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "cw1200.h"
#include "wsm.h"
#include "bh.h"
#include "debug.h"

#if defined(CONFIG_CW1200_WSM_DEBUG)
#define wsm_printk(...) printk(__VA_ARGS__)
#else
#define wsm_printk(...)
#endif

#define WSM_CMD_TIMEOUT		(1 * HZ)
#define WSM_CMD_JOIN_TIMEOUT	(7 * HZ) /* Join timeout is 5 sec. in FW */
#define WSM_CMD_START_TIMEOUT	(7 * HZ)
#define WSM_TX_TIMEOUT		(1 * HZ)
#define WSM_CMD_LAST_CHANCE_TIMEOUT (10 * HZ)

#define WSM_SKIP(buf, size)						\
	do {								\
		if (unlikely(buf->data + size > buf->end))		\
			goto underflow;					\
		buf->data += size;					\
	} while (0)

#define WSM_GET(buf, ptr, size)						\
	do {								\
		if (unlikely(buf->data + size > buf->end))		\
			goto underflow;					\
		memcpy(ptr, buf->data, size);				\
		buf->data += size;					\
	} while (0)

#define __WSM_GET(buf, type, cvt)					\
	({								\
		type val;						\
		if (unlikely(buf->data + sizeof(type) > buf->end))	\
			goto underflow;					\
		val = cvt(*(type *)buf->data);				\
		buf->data += sizeof(type);				\
		val;							\
	})

#define WSM_GET8(buf)  __WSM_GET(buf, u8, (u8))
#define WSM_GET16(buf) __WSM_GET(buf, u16, __le16_to_cpu)
#define WSM_GET32(buf) __WSM_GET(buf, u32, __le32_to_cpu)

#define WSM_PUT(buf, ptr, size)						\
	do {								\
		if (unlikely(buf->data + size > buf->end))		\
			if (unlikely(wsm_buf_reserve(buf, size)))	\
				goto nomem;				\
		memcpy(buf->data, ptr, size);				\
		buf->data += size;					\
	} while (0)

#define __WSM_PUT(buf, val, type, cvt)					  \
	do {								  \
		if (unlikely(buf->data + sizeof(type) > buf->end))	  \
			if (unlikely(wsm_buf_reserve(buf, sizeof(type)))) \
				goto nomem;				  \
		*(type *)buf->data = cvt(val);				  \
		buf->data += sizeof(type);				  \
	} while (0)

#define WSM_PUT8(buf, val)  __WSM_PUT(buf, val, u8, (u8))
#define WSM_PUT16(buf, val) __WSM_PUT(buf, val, u16, __cpu_to_le16)
#define WSM_PUT32(buf, val) __WSM_PUT(buf, val, u32, __cpu_to_le32)

static void wsm_buf_reset(struct wsm_buf *buf);
static int wsm_buf_reserve(struct wsm_buf *buf, size_t extra_size);

static int wsm_cmd_send(struct cw1200_common *priv,
			struct wsm_buf *buf,
			void *arg, u16 cmd, long tmo);

static inline void wsm_cmd_lock(struct cw1200_common *priv)
{
	mutex_lock(&priv->wsm_cmd_mux);
}

static inline void wsm_cmd_unlock(struct cw1200_common *priv)
{
	mutex_unlock(&priv->wsm_cmd_mux);
}

/* ******************************************************************** */
/* WSM API implementation						*/

static int wsm_generic_confirm(struct cw1200_common *priv,
			     void *arg,
			     struct wsm_buf *buf)
{
	u32 status = WSM_GET32(buf);
	if (status != WSM_STATUS_SUCCESS)
		return -EINVAL;
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

int wsm_configuration(struct cw1200_common *priv, struct wsm_configuration *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->dot11MaxTransmitMsduLifeTime);
	WSM_PUT32(buf, arg->dot11MaxReceiveLifeTime);
	WSM_PUT32(buf, arg->dot11RtsThreshold);

	/* DPD block. */
	WSM_PUT16(buf, arg->dpdData_size + 12);
	WSM_PUT16(buf, 1); /* DPD version */
	WSM_PUT(buf, arg->dot11StationId, ETH_ALEN);
	WSM_PUT16(buf, 5); /* DPD flags */
	WSM_PUT(buf, arg->dpdData, arg->dpdData_size);

	ret = wsm_cmd_send(priv, buf, arg, 0x0009, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

static int wsm_configuration_confirm(struct cw1200_common *priv,
				     struct wsm_configuration *arg,
				     struct wsm_buf *buf)
{
	int i;
	int status;

	status = WSM_GET32(buf);
	if (WARN_ON(status != WSM_STATUS_SUCCESS))
		return -EINVAL;

	WSM_GET(buf, arg->dot11StationId, ETH_ALEN);
	arg->dot11FrequencyBandsSupported = WSM_GET8(buf);
	WSM_SKIP(buf, 1);
	arg->supportedRateMask = WSM_GET32(buf);
	for (i = 0; i < 2; ++i) {
		arg->txPowerRange[i].min_power_level = WSM_GET32(buf);
		arg->txPowerRange[i].max_power_level = WSM_GET32(buf);
		arg->txPowerRange[i].stepping = WSM_GET32(buf);
	}
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

int wsm_reset(struct cw1200_common *priv, const struct wsm_reset *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	u16 cmd = 0x000A | ((arg->link_id & 0x0F) << 6);

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->reset_statistics ? 0 : 1);
	ret = wsm_cmd_send(priv, buf, NULL, cmd, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

struct wsm_mib {
	u16 mibId;
	void *buf;
	size_t buf_size;
};

int wsm_read_mib(struct cw1200_common *priv, u16 mibId, void *_buf,
			size_t buf_size)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	struct wsm_mib mib_buf = {
		.mibId = mibId,
		.buf = _buf,
		.buf_size = buf_size,
	};
	wsm_cmd_lock(priv);

	WSM_PUT16(buf, mibId);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, &mib_buf, 0x0005, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

static int wsm_read_mib_confirm(struct cw1200_common *priv,
				struct wsm_mib *arg,
				struct wsm_buf *buf)
{
	u16 size;
	if (WARN_ON(WSM_GET32(buf) != WSM_STATUS_SUCCESS))
		return -EINVAL;

	if (WARN_ON(WSM_GET16(buf) != arg->mibId))
		return -EINVAL;

	size = WSM_GET16(buf);
	if (size > arg->buf_size)
		size = arg->buf_size;

	WSM_GET(buf, arg->buf, size);
	arg->buf_size = size;
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

int wsm_write_mib(struct cw1200_common *priv, u16 mibId, void *_buf,
			size_t buf_size)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	struct wsm_mib mib_buf = {
		.mibId = mibId,
		.buf = _buf,
		.buf_size = buf_size,
	};

	wsm_cmd_lock(priv);

	WSM_PUT16(buf, mibId);
	WSM_PUT16(buf, buf_size);
	WSM_PUT(buf, _buf, buf_size);

	ret = wsm_cmd_send(priv, buf, &mib_buf, 0x0006, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

static int wsm_write_mib_confirm(struct cw1200_common *priv,
				struct wsm_mib *arg,
				struct wsm_buf *buf)
{
	int ret;

	ret = wsm_generic_confirm(priv, arg, buf);
	if (ret)
		return ret;

	if (arg->mibId == 0x1006) {
		/* OperationalMode: update PM status. */
		const char *p = arg->buf;
		cw1200_enable_powersave(priv,
				(p[0] & 0x0F) ? true : false);
	}
	return 0;
}

/* ******************************************************************** */

int wsm_scan(struct cw1200_common *priv, const struct wsm_scan *arg)
{
	int i;
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	if (unlikely(arg->numOfChannels > 48))
		return -EINVAL;

	if (unlikely(arg->numOfSSIDs > 2))
		return -EINVAL;

	if (unlikely(arg->band > 1))
		return -EINVAL;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->band);
	WSM_PUT8(buf, arg->scanType);
	WSM_PUT8(buf, arg->scanFlags);
	WSM_PUT8(buf, arg->maxTransmitRate);
	WSM_PUT32(buf, arg->autoScanInterval);
	WSM_PUT8(buf, arg->numOfProbeRequests);
	WSM_PUT8(buf, arg->numOfChannels);
	WSM_PUT8(buf, arg->numOfSSIDs);
	WSM_PUT8(buf, arg->probeDelay);

	for (i = 0; i < arg->numOfChannels; ++i) {
		WSM_PUT16(buf, arg->ch[i].number);
		WSM_PUT16(buf, 0);
		WSM_PUT32(buf, arg->ch[i].minChannelTime);
		WSM_PUT32(buf, arg->ch[i].maxChannelTime);
		WSM_PUT32(buf, 0);
	}

	for (i = 0; i < arg->numOfSSIDs; ++i) {
		WSM_PUT32(buf, arg->ssids[i].length);
		WSM_PUT(buf, &arg->ssids[i].ssid[0],
				sizeof(arg->ssids[i].ssid));
	}

	ret = wsm_cmd_send(priv, buf, NULL, 0x0007, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_stop_scan(struct cw1200_common *priv)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	wsm_cmd_lock(priv);
	ret = wsm_cmd_send(priv, buf, NULL, 0x0008, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;
}


static int wsm_tx_confirm(struct cw1200_common *priv, struct wsm_buf *buf)
{
	struct wsm_tx_confirm tx_confirm;

	tx_confirm.packetID = WSM_GET32(buf);
	tx_confirm.status = WSM_GET32(buf);
	tx_confirm.txedRate = WSM_GET8(buf);
	tx_confirm.ackFailures = WSM_GET8(buf);
	tx_confirm.flags = WSM_GET16(buf);
	tx_confirm.mediaDelay = WSM_GET32(buf);
	tx_confirm.txQueueDelay = WSM_GET32(buf);

	if (priv->wsm_cbc.tx_confirm)
		priv->wsm_cbc.tx_confirm(priv, &tx_confirm);
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

static int wsm_multi_tx_confirm(struct cw1200_common *priv,
				struct wsm_buf *buf)
{
	int ret;
	int count;
	int i;

	count = WSM_GET32(buf);
	if (WARN_ON(count <= 0))
		return -EINVAL;
	else if (count > 1) {
		ret = wsm_release_tx_buffer(priv, count - 1);
		if (ret < 0)
			return ret;
		else if (ret > 0)
			cw1200_bh_wakeup(priv);
	}

	cw1200_debug_txed_multi(priv, count);
	for (i = 0; i < count; ++i) {
		ret = wsm_tx_confirm(priv, buf);
		if (ret)
			return ret;
	}
	return ret;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

static int wsm_join_confirm(struct cw1200_common *priv,
			    struct wsm_join *arg,
			    struct wsm_buf *buf)
{
	if (WARN_ON(WSM_GET32(buf) != WSM_STATUS_SUCCESS)) {
		priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
		wsm_unlock_tx(priv);
		return -EINVAL;
	}

	arg->minPowerLevel = WSM_GET32(buf);
	arg->maxPowerLevel = WSM_GET32(buf);

	priv->join_status = CW1200_JOIN_STATUS_STA;
	wsm_unlock_tx(priv);
	return 0;

underflow:
	WARN_ON(1);
	priv->join_status = CW1200_JOIN_STATUS_PASSIVE;
	wsm_unlock_tx(priv);
	return -EINVAL;
}

int wsm_join(struct cw1200_common *priv, struct wsm_join *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->band);
	WSM_PUT16(buf, arg->channelNumber);
	WSM_PUT(buf, &arg->bssid[0], sizeof(arg->bssid));
	WSM_PUT16(buf, arg->atimWindow);
	WSM_PUT8(buf, arg->preambleType);
	WSM_PUT8(buf, arg->probeForJoin);
	WSM_PUT8(buf, arg->dtimPeriod);
	WSM_PUT8(buf, arg->flags);
	WSM_PUT32(buf, arg->ssidLength);
	WSM_PUT(buf, &arg->ssid[0], sizeof(arg->ssid));
	WSM_PUT32(buf, arg->beaconInterval);
	WSM_PUT32(buf, arg->basicRateSet);

	ret = wsm_cmd_send(priv, buf, arg, 0x000B, WSM_CMD_JOIN_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_bss_params(struct cw1200_common *priv,
			const struct wsm_set_bss_params *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, 0);
	WSM_PUT8(buf, arg->beaconLostCount);
	WSM_PUT16(buf, arg->aid);
	WSM_PUT32(buf, arg->operationalRateSet);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0011, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_add_key(struct cw1200_common *priv, const struct wsm_add_key *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT(buf, arg, sizeof(*arg));

	ret = wsm_cmd_send(priv, buf, NULL, 0x000C, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_remove_key(struct cw1200_common *priv, const struct wsm_remove_key *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->entryIndex);
	WSM_PUT8(buf, 0);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, NULL, 0x000D, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_tx_queue_params(struct cw1200_common *priv,
				const struct wsm_set_tx_queue_params *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	/* TODO: verify me. */
	WSM_PUT8(buf, arg->queueId);
	WSM_PUT8(buf, 0);
	WSM_PUT8(buf, arg->ackPolicy);
	WSM_PUT8(buf, 0);
	WSM_PUT32(buf, arg->maxTransmitLifetime);
	WSM_PUT16(buf, arg->allowedMediumTime);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0012, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_edca_params(struct cw1200_common *priv,
				const struct wsm_edca_params *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	/* TODO: verify me. */
	/* Implemented according to specification. Note that there is a
	 * mismatch in BK and BE mapping. */

	WSM_PUT16(buf, arg->params[1].cwMin);
	WSM_PUT16(buf, arg->params[0].cwMin);
	WSM_PUT16(buf, arg->params[2].cwMin);
	WSM_PUT16(buf, arg->params[3].cwMin);

	WSM_PUT16(buf, arg->params[1].cwMax);
	WSM_PUT16(buf, arg->params[0].cwMax);
	WSM_PUT16(buf, arg->params[2].cwMax);
	WSM_PUT16(buf, arg->params[3].cwMax);

	WSM_PUT8(buf, arg->params[1].aifns);
	WSM_PUT8(buf, arg->params[0].aifns);
	WSM_PUT8(buf, arg->params[2].aifns);
	WSM_PUT8(buf, arg->params[3].aifns);

	WSM_PUT16(buf, arg->params[1].txOpLimit);
	WSM_PUT16(buf, arg->params[0].txOpLimit);
	WSM_PUT16(buf, arg->params[2].txOpLimit);
	WSM_PUT16(buf, arg->params[3].txOpLimit);

	WSM_PUT32(buf, arg->params[1].maxReceiveLifetime);
	WSM_PUT32(buf, arg->params[0].maxReceiveLifetime);
	WSM_PUT32(buf, arg->params[2].maxReceiveLifetime);
	WSM_PUT32(buf, arg->params[3].maxReceiveLifetime);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0013, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_switch_channel(struct cw1200_common *priv,
			const struct wsm_switch_channel *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_lock_tx(priv);
	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->channelMode);
	WSM_PUT8(buf, arg->channelSwitchCount);
	WSM_PUT16(buf, arg->newChannelNumber);

	priv->channel_switch_in_progress = 1;

	ret = wsm_cmd_send(priv, buf, NULL, 0x0016, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	if (ret) {
		wsm_unlock_tx(priv);
		priv->channel_switch_in_progress = 0;
	}
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	wsm_unlock_tx(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_pm(struct cw1200_common *priv, const struct wsm_set_pm *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->pmMode);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0010, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_start(struct cw1200_common *priv, const struct wsm_start *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->band);
	WSM_PUT16(buf, arg->channelNumber);
	WSM_PUT32(buf, arg->CTWindow);
	WSM_PUT32(buf, arg->beaconInterval);
	WSM_PUT8(buf, arg->DTIMPeriod);
	WSM_PUT8(buf, arg->preambleType);
	WSM_PUT8(buf, arg->probeDelay);
	WSM_PUT8(buf, arg->ssidLength);
	WSM_PUT(buf, arg->ssid, sizeof(arg->ssid));
	WSM_PUT32(buf, arg->basicRateSet);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0017, WSM_CMD_START_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_beacon_transmit(struct cw1200_common *priv,
			const struct wsm_beacon_transmit *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);

	WSM_PUT32(buf, arg->enableBeaconing ? 1 : 0);

	ret = wsm_cmd_send(priv, buf, NULL, 0x0018, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_start_find(struct cw1200_common *priv)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);
	ret = wsm_cmd_send(priv, buf, NULL, 0x0019, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;
}

/* ******************************************************************** */

int wsm_stop_find(struct cw1200_common *priv)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;

	wsm_cmd_lock(priv);
	ret = wsm_cmd_send(priv, buf, NULL, 0x001A, WSM_CMD_TIMEOUT);
	wsm_cmd_unlock(priv);
	return ret;
}

/* ******************************************************************** */

int wsm_map_link(struct cw1200_common *priv, const struct wsm_map_link *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->wsm_cmd_buf;
	u16 cmd = 0x001C | ((arg->link_id & 0x0F) << 6);

	wsm_cmd_lock(priv);

	WSM_PUT(buf, &arg->mac_addr[0], sizeof(arg->mac_addr));
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv, buf, NULL, cmd, WSM_CMD_TIMEOUT);

	wsm_cmd_unlock(priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv);
	return -ENOMEM;
}


/* ******************************************************************** */
/* WSM indication events implementation					*/

static int wsm_startup_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	u16 status;
	char fw_label[129];
	static const char * const fw_types[] = {
		"ETF",
		"WFM",
		"WSM",
		"HI test",
		"Platform test"
	};

	priv->wsm_caps.numInpChBufs	= WSM_GET16(buf);
	priv->wsm_caps.sizeInpChBuf	= WSM_GET16(buf);
	priv->wsm_caps.hardwareId	= WSM_GET16(buf);
	priv->wsm_caps.hardwareSubId	= WSM_GET16(buf);
	status				= WSM_GET16(buf);
	priv->wsm_caps.firmwareCap	= WSM_GET16(buf);
	priv->wsm_caps.firmwareType	= WSM_GET16(buf);
	priv->wsm_caps.firmwareApiVer	= WSM_GET16(buf);
	priv->wsm_caps.firmwareBuildNumber = WSM_GET16(buf);
	priv->wsm_caps.firmwareVersion	= WSM_GET16(buf);
	WSM_GET(buf, &fw_label[0], sizeof(fw_label) - 1);
	fw_label[sizeof(fw_label) - 1] = 0; /* Do not trust FW too much. */

	if (WARN_ON(status))
		return -EINVAL;

	if (WARN_ON(priv->wsm_caps.firmwareType > 4))
		return -EINVAL;

	printk(KERN_INFO "CW1200 WSM init done.\n"
		"   Input buffers: %d x %d bytes\n"
		"   Hardware: %d.%d\n"
		"   %s firmware [%s], ver: %d, build: %d,"
		    " api: %d, cap: 0x%.4X\n",
		priv->wsm_caps.numInpChBufs, priv->wsm_caps.sizeInpChBuf,
		priv->wsm_caps.hardwareId, priv->wsm_caps.hardwareSubId,
		fw_types[priv->wsm_caps.firmwareType],
		&fw_label[0], priv->wsm_caps.firmwareVersion,
		priv->wsm_caps.firmwareBuildNumber,
		priv->wsm_caps.firmwareApiVer, priv->wsm_caps.firmwareCap);

	priv->wsm_caps.firmwareReady = 1;

	wake_up_interruptible(&priv->wsm_startup_done);
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

static int wsm_receive_indication(struct cw1200_common *priv,
					struct wsm_buf *buf,
					struct sk_buff **skb_p)
{
	priv->rx_timestamp = jiffies;
	if (priv->wsm_cbc.rx) {
		struct wsm_rx rx;
		size_t hdr_len;
		__le16 fctl;

		rx.status = WSM_GET32(buf);
		rx.channelNumber = WSM_GET16(buf);
		rx.rxedRate = WSM_GET8(buf);
		rx.rcpiRssi = WSM_GET8(buf);
		rx.flags = WSM_GET32(buf);
		fctl = *(__le16 *)buf->data;
		hdr_len = buf->data - buf->begin;
		skb_pull(*skb_p, hdr_len);
		if (!rx.status && unlikely(ieee80211_is_deauth(fctl))) {
			if (priv->join_status == CW1200_JOIN_STATUS_STA) {
				/* Shedule unjoin work */
				wsm_printk(KERN_DEBUG \
					"[WSM] Issue unjoin command (RX).\n");
				wsm_lock_tx_async(priv);
				if (queue_work(priv->workqueue,
						&priv->unjoin_work) <= 0)
					wsm_unlock_tx(priv);
			}
		}
		priv->wsm_cbc.rx(priv, &rx, skb_p);
		if (*skb_p)
			skb_push(*skb_p, hdr_len);
	}
	return 0;

underflow:
	return -EINVAL;
}

static int wsm_event_indication(struct cw1200_common *priv, struct wsm_buf *buf)
{
	int first;
	struct cw1200_wsm_event *event;

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		return 0;
	}

	event = kzalloc(sizeof(struct cw1200_wsm_event), GFP_KERNEL);

	event->evt.eventId = __le32_to_cpu(WSM_GET32(buf));
	event->evt.eventData = __le32_to_cpu(WSM_GET32(buf));

	wsm_printk(KERN_DEBUG "[WSM] Event: %d(%d)\n",
		event->evt.eventId, event->evt.eventData);

	spin_lock(&priv->event_queue_lock);
	first = list_empty(&priv->event_queue);
	list_add_tail(&event->link, &priv->event_queue);
	spin_unlock(&priv->event_queue_lock);

	if (first)
		queue_work(priv->workqueue, &priv->event_handler);

	return 0;

underflow:
	kfree(event);
	return -EINVAL;
}

static int wsm_channel_switch_indication(struct cw1200_common *priv,
						struct wsm_buf *buf)
{
	wsm_unlock_tx(priv); /* Re-enable datapath */
	WARN_ON(WSM_GET32(buf));

	priv->channel_switch_in_progress = 0;
	wake_up_interruptible(&priv->channel_switch_done);

	if (priv->wsm_cbc.channel_switch)
		priv->wsm_cbc.channel_switch(priv);
	return 0;

underflow:
	return -EINVAL;
}

static int wsm_set_pm_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	return 0;
}

static int wsm_scan_complete_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	if (priv->wsm_cbc.scan_complete) {
		struct wsm_scan_complete arg;
		arg.status = WSM_GET32(buf);
		arg.psm = WSM_GET8(buf);
		arg.numChannels = WSM_GET8(buf);
		priv->wsm_cbc.scan_complete(priv, &arg);
	}
	return 0;

underflow:
	return -EINVAL;
}

static int wsm_find_complete_indication(struct cw1200_common *priv,
					struct wsm_buf *buf)
{
	/* TODO: Implement me. */
	STUB();
	return 0;
}

static int wsm_suspend_resume_indication(struct cw1200_common *priv,
					 int link_id, struct wsm_buf *buf)
{
	if (priv->wsm_cbc.suspend_resume) {
		u32 flags;
		struct wsm_suspend_resume arg;

		flags = WSM_GET32(buf);
		arg.link_id = link_id;
		arg.stop = !(flags & 1);
		arg.multicast = !!(flags & 8);
		arg.queue = (flags >> 1) & 3;

		priv->wsm_cbc.suspend_resume(priv, &arg);
	}
	return 0;

underflow:
	return -EINVAL;
}


/* ******************************************************************** */
/* WSM TX								*/

int wsm_cmd_send(struct cw1200_common *priv,
		 struct wsm_buf *buf,
		 void *arg, u16 cmd, long tmo)
{
	size_t buf_len = buf->data - buf->begin;
	int ret;

	if (cmd == 0x0006) /* Write MIB */
		wsm_printk(KERN_DEBUG "[WSM] >>> 0x%.4X [MIB: 0x%.4X] (%d)\n",
			cmd, __le16_to_cpu(((__le16 *)buf->begin)[2]),
			buf_len);
	else
		wsm_printk(KERN_DEBUG "[WSM] >>> 0x%.4X (%d)\n", cmd, buf_len);

	/* Fill HI message header */
	/* BH will add sequence number */
	((__le16 *)buf->begin)[0] = __cpu_to_le16(buf_len);
	((__le16 *)buf->begin)[1] = __cpu_to_le16(cmd);

	spin_lock(&priv->wsm_cmd.lock);
	BUG_ON(priv->wsm_cmd.ptr);
	priv->wsm_cmd.done = 0;
	priv->wsm_cmd.ptr = buf->begin;
	priv->wsm_cmd.len = buf_len;
	priv->wsm_cmd.arg = arg;
	priv->wsm_cmd.cmd = cmd;
	spin_unlock(&priv->wsm_cmd.lock);

	cw1200_bh_wakeup(priv);

	if (unlikely(priv->bh_error)) {
		/* Do not wait for timeout if BH is dead. Exit immediately. */
		ret = 0;
	} else {
		long rx_timestamp;
		/* Firmware prioritizes data traffic over control confirm.
		 * Loop below checks if data was RXed and increases timeout
		 * accordingly. */
		do {
			/* It's safe to use unprotected access to
			 * wsm_cmd.done here */
			ret = wait_event_interruptible_timeout(
					priv->wsm_cmd_wq,
					priv->wsm_cmd.done, tmo);
			rx_timestamp = jiffies - priv->rx_timestamp;
			if (unlikely(rx_timestamp < 0))
				rx_timestamp = tmo + 1;
		} while (!ret && rx_timestamp <= tmo);
	}

	if (unlikely(ret == 0)) {
		u16 raceCheck;

		spin_lock(&priv->wsm_cmd.lock);
		raceCheck = priv->wsm_cmd.cmd;
		priv->wsm_cmd.arg = NULL;
		priv->wsm_cmd.ptr = NULL;
		spin_unlock(&priv->wsm_cmd.lock);

		/* Race condition check to make sure _confirm is not called
		 * after exit of _send */
		if (raceCheck == 0xFFFF) {
			/* If wsm_handle_rx got stuck in _confirm we will hang
			 * system there. It's better than silently currupt
			 * stack or heap, isn't it? */
			BUG_ON(wait_event_interruptible_timeout(
					priv->wsm_cmd_wq, priv->wsm_cmd.done,
					WSM_CMD_LAST_CHANCE_TIMEOUT) <= 0);
		}
		ret = -ETIMEDOUT;
	} else {
		spin_lock(&priv->wsm_cmd.lock);
		BUG_ON(!priv->wsm_cmd.done);
		ret = priv->wsm_cmd.ret;
		spin_unlock(&priv->wsm_cmd.lock);
	}
	wsm_buf_reset(buf);
	return ret;
}

/* ******************************************************************** */
/* WSM TX port control							*/

void wsm_lock_tx(struct cw1200_common *priv)
{
	wsm_cmd_lock(priv);
	if (atomic_add_return(1, &priv->tx_lock) == 1) {
		WARN_ON(wait_event_interruptible_timeout(priv->hw_bufs_used_wq,
			!priv->hw_bufs_used, WSM_CMD_LAST_CHANCE_TIMEOUT) <= 0);
		wsm_printk(KERN_DEBUG "[WSM] TX is locked.\n");
	}
	wsm_cmd_unlock(priv);
}

void wsm_lock_tx_async(struct cw1200_common *priv)
{
	if (atomic_add_return(1, &priv->tx_lock) == 1)
		wsm_printk(KERN_DEBUG "[WSM] TX is locked.\n");
}

void wsm_flush_tx(struct cw1200_common *priv)
{
	BUG_ON(!atomic_read(&priv->tx_lock));
	WARN_ON(wait_event_interruptible_timeout(priv->hw_bufs_used_wq,
		!priv->hw_bufs_used, WSM_CMD_LAST_CHANCE_TIMEOUT) <= 0);
}

void wsm_unlock_tx(struct cw1200_common *priv)
{
	int tx_lock;
	tx_lock = atomic_sub_return(1, &priv->tx_lock);
	if (tx_lock < 0) {
		BUG_ON(1);
	} else if (tx_lock == 0) {
		cw1200_bh_wakeup(priv);
		wsm_printk(KERN_DEBUG "[WSM] TX is unlocked.\n");
	}
}

/* ******************************************************************** */
/* WSM RX								*/

int wsm_handle_exception(struct cw1200_common *priv, u8 *data, size_t len)
{
	STUB();
	return 0;
}

int wsm_handle_rx(struct cw1200_common *priv, int id,
		  struct wsm_hdr *wsm, struct sk_buff **skb_p)
{
	int ret = 0;
	struct wsm_buf wsm_buf;
	int link_id = (id >> 6) & 0x0F;

	/* Strip link id. */
	id &= ~(0x0F << 6);

	wsm_buf.begin = (u8 *)&wsm[0];
	wsm_buf.data = (u8 *)&wsm[1];
	wsm_buf.end = &wsm_buf.begin[__le32_to_cpu(wsm->len)];

	wsm_printk(KERN_DEBUG "[WSM] <<< 0x%.4X (%d)\n", id,
			wsm_buf.end - wsm_buf.begin);

	if (id == 0x404) {
		ret = wsm_tx_confirm(priv, &wsm_buf);
	} else if (id == 0x41E) {
		ret = wsm_multi_tx_confirm(priv, &wsm_buf);
	} else if (id & 0x0400) {
		void *wsm_arg;
		u16 wsm_cmd;

		/* Do not trust FW too much. Protection against repeated
		 * response and race condition removal (see above). */
		spin_lock(&priv->wsm_cmd.lock);
		wsm_arg = priv->wsm_cmd.arg;
		wsm_cmd = priv->wsm_cmd.cmd & ~(0x0F << 6);
		priv->wsm_cmd.cmd = 0xFFFF;
		spin_unlock(&priv->wsm_cmd.lock);

		if (WARN_ON((id & ~0x0400) != wsm_cmd)) {
			/* Note that any non-zero is a fatal retcode. */
			ret = -EINVAL;
			goto out;
		}

		switch (id) {
		case 0x0409:
			/* Note that wsm_arg can be NULL in case of timeout in
			 * wsm_cmd_send(). */
			if (likely(wsm_arg))
				ret = wsm_configuration_confirm(priv, wsm_arg,
								&wsm_buf);
			break;
		case 0x0405:
			if (likely(wsm_arg))
				ret = wsm_read_mib_confirm(priv, wsm_arg,
								&wsm_buf);
			break;
		case 0x0406:
			if (likely(wsm_arg))
				ret = wsm_write_mib_confirm(priv, wsm_arg,
								&wsm_buf);
			break;
		case 0x040B:
			if (likely(wsm_arg))
				ret = wsm_join_confirm(priv, wsm_arg, &wsm_buf);
			break;
		case 0x0407: /* start-scan */
		case 0x0408: /* stop-scan */
		case 0x040A: /* wsm_reset */
		case 0x040C: /* add_key */
		case 0x040D: /* remove_key */
		case 0x0410: /* wsm_set_pm */
		case 0x0411: /* set_bss_params */
		case 0x0412: /* set_tx_queue_params */
		case 0x0413: /* set_edca_params */
		case 0x0416: /* switch_channel */
		case 0x0417: /* start */
		case 0x0418: /* beacon_transmit */
		case 0x0419: /* start_find */
		case 0x041A: /* stop_find */
		case 0x041C: /* map_link */
			WARN_ON(wsm_arg != NULL);
			ret = wsm_generic_confirm(priv, wsm_arg, &wsm_buf);
			if (ret)
				printk(KERN_ERR
					"[WSM] wsm_generic_confirm "
					"failed for request 0x%.4X.\n",
					id);
			break;
		default:
			BUG_ON(1);
		}

		spin_lock(&priv->wsm_cmd.lock);
		priv->wsm_cmd.ret = ret;
		priv->wsm_cmd.done = 1;
		spin_unlock(&priv->wsm_cmd.lock);
		ret = 0; /* Error response from device should ne stop BH. */

		wake_up_interruptible(&priv->wsm_cmd_wq);
	} else if (id & 0x0800) {
		switch (id) {
		case 0x0801:
			ret = wsm_startup_indication(priv, &wsm_buf);
			break;
		case 0x0804:
			ret = wsm_receive_indication(priv, &wsm_buf, skb_p);
			break;
		case 0x0805:
			ret = wsm_event_indication(priv, &wsm_buf);
			break;
		case 0x080A:
			ret = wsm_channel_switch_indication(priv, &wsm_buf);
			break;
		case 0x0809:
			ret = wsm_set_pm_indication(priv, &wsm_buf);
			break;
		case 0x0806:
			ret = wsm_scan_complete_indication(priv, &wsm_buf);
			break;
		case 0x080B:
			ret = wsm_find_complete_indication(priv, &wsm_buf);
			break;
		case 0x080C:
			ret = wsm_suspend_resume_indication(priv, link_id, &wsm_buf);
			break;
		default:
			STUB();
		}
	} else {
		WARN_ON(1);
		ret = -EINVAL;
	}
out:
	return ret;
}

static bool wsm_handle_tx_data(struct cw1200_common *priv,
			       const struct wsm_tx *wsm,
			       const struct ieee80211_tx_info *tx_info)
{
	bool handled = false;
	const struct ieee80211_hdr *frame =
		(struct ieee80211_hdr *)&wsm[1];
	__le16 fctl = frame->frame_control;
	enum {
		doProbe,
		doDrop,
		doJoin,
		doWep,
		doTx,
	} action = doTx;

	switch (priv->mode) {
	case NL80211_IFTYPE_STATION:
		if (unlikely(!priv->join_status ||
			memcmp(frame->addr1, priv->join_bssid,
				sizeof(priv->join_bssid)))) {
			if (ieee80211_is_auth(fctl))
				action = doJoin;
			else
				action = doDrop;
		}
		break;
	case NL80211_IFTYPE_AP:
		if (unlikely(!priv->join_status))
			action = doDrop;
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		STUB();
	case NL80211_IFTYPE_MONITOR:
	default:
		action = doDrop;
		break;
	}

	if (action == doTx) {
		if (unlikely(ieee80211_is_probe_req(fctl)))
			action = doProbe;
		else if ((fctl & __cpu_to_le32(IEEE80211_FCTL_PROTECTED)) &&
			tx_info->control.hw_key &&
			unlikely(tx_info->control.hw_key->keyidx !=
					priv->wep_default_key_id) &&
			(tx_info->control.hw_key->cipher ==
					WLAN_CIPHER_SUITE_WEP40 ||
			 tx_info->control.hw_key->cipher ==
					WLAN_CIPHER_SUITE_WEP104))
			action = doWep;
	}

	switch (action) {
	case doProbe:
	{
		/* An interesting FW "feature". Device filters
		 * probe responses.
		 * The easiest way to get it back is to convert
		 * probe request into WSM start_scan command. */
		int rate_id = (wsm->flags >> 4) & 0x07;
		struct cw1200_queue *queue =
			&priv->tx_queue[cw1200_queue_get_queue_id(
				wsm->packetID)];
		wsm_printk(KERN_DEBUG \
			"[WSM] Convert probe request to scan.\n");
		wsm_lock_tx_async(priv);
		BUG_ON(priv->scan.probe_skb);
		BUG_ON(cw1200_queue_get_skb(queue,
					wsm->packetID,
					&priv->scan.probe_skb));
		BUG_ON(cw1200_queue_remove(queue, priv,
						wsm->packetID));
		/* Release used TX rate policy */
		tx_policy_put(priv, rate_id);
		queue_delayed_work(priv->workqueue,
				&priv->scan.probe_work, 0);
		handled = true;
	}
	break;
	case doDrop:
	{
		/* See detailed description of "join" below.
		 * We are dropping everything except AUTH in non-joined mode. */
		struct sk_buff *skb;
		int rate_id = (wsm->flags >> 4) & 0x07;
		struct cw1200_queue *queue =
			&priv->tx_queue[cw1200_queue_get_queue_id(
				wsm->packetID)];
		wsm_printk(KERN_DEBUG "[WSM] Drop frame (0x%.4X):"
			" not joined.\n", fctl);
		BUG_ON(cw1200_queue_get_skb(queue, wsm->packetID, &skb));
		BUG_ON(cw1200_queue_remove(queue, priv, wsm->packetID));
		/* Release used TX rate policy */
		tx_policy_put(priv, rate_id);
		/* Release SKB. TODO: report TX failure. */
		dev_kfree_skb(skb);
		handled = true;
	}
	break;
	case doJoin:
	{
		/* There is one more interesting "feature"
		 * in FW: it can't do RX/TX before "join".
		 * "Join" here is not an association,
		 * but just a syncronization between AP and STA.
		 * BTW that means device can't receive frames
		 * in monitor mode.
		 * priv->join_status is used only in bh thread and does
		 * not require protection */
		wsm_printk(KERN_DEBUG "[WSM] Issue join command.\n");
		wsm_lock_tx_async(priv);
		BUG_ON(priv->join_pending_frame);
		priv->join_pending_frame = wsm;
		if (queue_work(priv->workqueue, &priv->join_work) <= 0)
			wsm_unlock_tx(priv);
		handled = true;
	}
	break;
	case doWep:
	{
		wsm_printk(KERN_DEBUG "[WSM] Issue set_default_wep_key.\n");
		wsm_lock_tx_async(priv);
		priv->wep_default_key_id = tx_info->control.hw_key->keyidx;
		if (queue_work(priv->workqueue, &priv->wep_key_work) <= 0)
			wsm_unlock_tx(priv);
		handled = true;
	}
	break;
	case doTx:
	{
#if 0
		/* Kept for history. If you want to implement wsm->more,
		 * make sure you are able to send a frame after that. */
		wsm->more = (count > 1) ? 1 : 0;
		if (wsm->more) {
			/* HACK!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			 * It's undocumented in WSM spec, but CW1200 hangs
			 * if 'more' is set and no TX is performed due to TX
			 * buffers limitation. */
			if (priv->hw_bufs_used + 1 ==
					priv->wsm_caps.numInpChBufs)
				wsm->more = 0;
		}

		/* BUG!!! FIXME: we can't use 'more' at all: we don't know
		 * future. It could be a request from upper layer with TX lock
		 * requirements (scan, for example). If "more" is set device
		 * will not send data and wsm_tx_lock() will fail...
		 * It's not obvious how to fix this deadlock. Any ideas?
		 * As a workaround more is set to 0. */
		wsm->more = 0;
#endif /* 0 */

		if (ieee80211_is_deauth(fctl) &&
				priv->mode != NL80211_IFTYPE_AP) {
			/* Shedule unjoin work */
			wsm_printk(KERN_DEBUG "[WSM] Issue unjoin command"
				" (TX).\n");
#if 0
			wsm->more = 0;
#endif /* 0 */
			wsm_lock_tx_async(priv);
			if (queue_work(priv->workqueue,
					&priv->unjoin_work) <= 0)
				wsm_unlock_tx(priv);
		}
	}
	break;
	}
	return handled;
}

static int wsm_get_tx_queue_and_mask(struct cw1200_common *priv,
				     struct cw1200_queue **queue_p,
				     u32* tx_allowed_mask_p,
				     bool *more)
{
	int i;
	struct cw1200_queue *queue = NULL;
	u32 tx_allowed_mask;
	int mcasts = 0;

	/* Search for a queue with multicast frames buffered */
	if (priv->sta_asleep_mask && !priv->suspend_multicast) {
		tx_allowed_mask = 1 << CW1200_LINK_ID_AFTER_DTIM;
		for (i = 0; i < 4; ++i) {
			mcasts += cw1200_queue_get_num_queued(
					&priv->tx_queue[i], tx_allowed_mask);
			if (!queue && mcasts)
				queue = &priv->tx_queue[i];
			if (mcasts > 1)
				break;
		}
		if (mcasts)
			goto found;
	}

	/* Search for unicast traffic */
	for (i = 0; i < 4; ++i) {
		queue = &priv->tx_queue[i];
		tx_allowed_mask = ~priv->sta_asleep_mask;
		if (priv->sta_asleep_mask) {
			tx_allowed_mask |= ~priv->tx_suspend_mask[i];
		} else {
			tx_allowed_mask |= 1 << CW1200_LINK_ID_AFTER_DTIM;
		}
		if (cw1200_queue_get_num_queued(
				queue, tx_allowed_mask))
			goto found;
	}
	return -ENOENT;

found:
	*queue_p = queue;
	*tx_allowed_mask_p = tx_allowed_mask;
	*more = mcasts > 1;
	return 0;
}

int wsm_get_tx(struct cw1200_common *priv, u8 **data,
	       size_t *tx_len)
{
	struct wsm_tx *wsm = NULL;
	struct ieee80211_tx_info *tx_info;
	struct cw1200_queue *queue;
	struct cw1200_sta_priv *sta_priv;
	u32 tx_allowed_mask = 0;
	/*
	 * Count was intended as an input for wsm->more flag.
	 * During implementation it was found that wsm->more
	 * is not usable, see details above. It is kept just
	 * in case you would like to try to implement it again.
	 */
	int count = 0;

	/* More is used only for broadcasts. */
	bool more;

	if (priv->wsm_cmd.ptr) {
		++count;
		spin_lock(&priv->wsm_cmd.lock);
		BUG_ON(!priv->wsm_cmd.ptr);
		*data = priv->wsm_cmd.ptr;
		*tx_len = priv->wsm_cmd.len;
		spin_unlock(&priv->wsm_cmd.lock);
	} else {
		for (;;) {
			if (atomic_add_return(0, &priv->tx_lock))
				break;

			if (wsm_get_tx_queue_and_mask(priv, &queue,
					&tx_allowed_mask, &more))
				break;

			if (cw1200_queue_get(queue,
					tx_allowed_mask,
					&wsm, &tx_info))
				continue;

			if (wsm_handle_tx_data(priv, wsm, tx_info))
				continue;  /* Handled by WSM */

			if (tx_info->control.sta) {
				/* Update link id */
				sta_priv = (struct cw1200_sta_priv *)
					&tx_info->control.sta->drv_priv;
				wsm->hdr.id &= __cpu_to_le16(~(0x0F << 6));
				wsm->hdr.id |=
					cpu_to_le16(sta_priv->link_id << 6);
			}

			*data = (u8 *)wsm;
			*tx_len = __le16_to_cpu(wsm->hdr.len);
			if (more) {
				struct ieee80211_hdr *hdr =
					(struct ieee80211_hdr *) &wsm[1];
				/* more buffered multicast/broadcast frames
				 *  ==> set MoreData flag in IEEE 802.11 header
				 *  to inform PS STAs */
				hdr->frame_control |=
					cpu_to_le16(IEEE80211_FCTL_MOREDATA);
			} else if (priv->mode == NL80211_IFTYPE_AP &&
					!priv->suspend_multicast) {
				priv->suspend_multicast = true;
				queue_work(priv->workqueue,
					&priv->multicast_stop_work);
			}

			wsm_printk(KERN_DEBUG "[WSM] >>> 0x%.4X (%d) %p %c\n",
				0x0004, *tx_len, *data,
				wsm->more ? 'M' : ' ');
			++count;
			break;
		}
	}

	return count;
}

void wsm_txed(struct cw1200_common *priv, u8 *data)
{
	if (data == priv->wsm_cmd.ptr) {
		spin_lock(&priv->wsm_cmd.lock);
		priv->wsm_cmd.ptr = NULL;
		spin_unlock(&priv->wsm_cmd.lock);
	}
}

/* ******************************************************************** */
/* WSM buffer								*/

void wsm_buf_init(struct wsm_buf *buf)
{
	BUG_ON(buf->begin);
	buf->begin = kmalloc(SDIO_BLOCK_SIZE, GFP_KERNEL | GFP_DMA);
	buf->end = buf->begin ? &buf->begin[SDIO_BLOCK_SIZE] : buf->begin;
	wsm_buf_reset(buf);
}

void wsm_buf_deinit(struct wsm_buf *buf)
{
	kfree(buf->begin);
	buf->begin = buf->data = buf->end = NULL;
}

static void wsm_buf_reset(struct wsm_buf *buf)
{
	if (buf->begin) {
		buf->data = &buf->begin[4];
		*(u32 *)buf->begin = 0;
	} else
		buf->data = buf->begin;
}

static int wsm_buf_reserve(struct wsm_buf *buf, size_t extra_size)
{
	size_t pos = buf->data - buf->begin;
	size_t size = pos + extra_size;


	if (size & (SDIO_BLOCK_SIZE - 1)) {
		size &= SDIO_BLOCK_SIZE;
		size += SDIO_BLOCK_SIZE;
	}

	buf->begin = krealloc(buf->begin, size, GFP_KERNEL | GFP_DMA);
	if (buf->begin) {
		buf->data = &buf->begin[pos];
		buf->end = &buf->begin[size];
		return 0;
	} else {
		buf->end = buf->data = buf->begin;
		return -ENOMEM;
	}
}


