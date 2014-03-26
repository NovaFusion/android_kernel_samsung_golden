/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * Hemant Gupta (hemant.gupta@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth Audio Driver for ST-Ericsson CG2900 controller.
 */
#define NAME					"cg2900_audio"
#define pr_fmt(fmt)				NAME ": " fmt "\n"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

#include "cg2900.h"
#include "cg2900_audio.h"
#include "cg2900_chip.h"

#define MAX_NBR_OF_USERS			10
#define FIRST_USER				1

/*
 * This is a default ACL handle. It is necessary to provide to the chip, but
 * does not actually do anything.
 */
#define DEFAULT_ACL_HANDLE			0x0001

/* Use a timeout of 5 seconds when waiting for a command response */
#define RESP_TIMEOUT				5000

#define BT_DEV					(info->dev_bt)
#define FM_DEV					(info->dev_fm)

/* FM Set control conversion macros for CG2905/10 */
#define CG2910_FM_CMD_SET_CTRL_48000_HEX	0x12C0
#define CG2910_FM_CMD_SET_CTRL_44100_HEX	0x113A

/* Bluetooth error codes */
#define HCI_BT_ERROR_NO_ERROR			0x00

/* Used to select proper API, ignoring subrevisions etc */
enum chip_revision {
	CG2900_CHIP_REV_PG1,
	CG2900_CHIP_REV_PG2,
	CG2905_CHIP_REV_PG1_05,
	CG2905_CHIP_REV_PG2,
	CG2910_CHIP_REV_PG1,
	CG2910_CHIP_REV_PG1_05,
	CG2910_CHIP_REV_PG2
};

/**
 * enum chip_resp_state - State when communicating with the CG2900 controller.
 * @IDLE:		No outstanding packets to the controller.
 * @WAITING:		Packet has been sent to the controller. Waiting for
 *			response.
 * @RESP_RECEIVED:	Response from controller has been received but not yet
 *			handled.
 */
enum chip_resp_state {
	IDLE,
	WAITING,
	RESP_RECEIVED
};

/**
 * enum main_state - Main state for the CG2900 Audio driver.
 * @OPENED:	Audio driver has registered to CG2900 Core.
 * @CLOSED:	Audio driver is not registered to CG2900 Core.
 * @RESET:	A reset of CG2900 Core has occurred and no user has re-opened
 *		the audio driver.
 */
enum main_state {
	OPENED,
	CLOSED,
	RESET
};

/**
 * struct endpoint_list - List for storing endpoint configuration nodes.
 * @ep_list:		Pointer to first node in list.
 * @management_mutex:	Mutex for handling access to list.
 */
struct endpoint_list {
	struct list_head	ep_list;
	struct mutex		management_mutex;
};

/**
 * struct endpoint_config_node - Node for storing endpoint configuration.
 * @list:		list_head struct.
 * @endpoint_id:	Endpoint ID.
 * @config:		Stored configuration for this endpoint.
 */
struct endpoint_config_node {
	struct list_head			list;
	enum cg2900_audio_endpoint_id		endpoint_id;
	union cg2900_endpoint_config_union	config;
};

/**
 * struct audio_info - Main CG2900 Audio driver info structure.
 * @list:			list_head struct.
 * @state:			Current state of the CG2900 Audio driver.
 * @revision:			Chip revision, used to select API.
 * @misc_dev:			The misc device created by this driver.
 * @misc_registered:		True if misc device is registered.
 * @parent:			Parent device.
 * @dev_bt:			Device registered by this driver for the BT
 *				audio channel.
 * @dev_fm:			Device registered by this driver for the FM
 *				audio channel.
 * @filp:			Current char device file pointer.
 * @management_mutex:		Mutex for handling access to CG2900 Audio driver
 *				management.
 * @bt_mutex:			Mutex for handling access to BT audio channel.
 * @fm_mutex:			Mutex for handling access to FM audio channel.
 * @nbr_of_users_active:	Number of sessions open in the CG2900 Audio
 *				driver.
 * @i2s_config:			DAI I2S configuration.
 * @i2s_pcm_config:		DAI PCM_I2S configuration.
 * @i2s_config_known:		@true if @i2s_config has been set,
 *				@false otherwise.
 * @i2s_pcm_config_known:	@true if @i2s_pcm_config has been set,
 *				@false otherwise.
 * @endpoints:			List containing the endpoint configurations.
 * @stream_ids:			Bitmask for in-use stream ids (only used with
 *				CG2900 PG2 onwards chip API).
 */
struct audio_info {
	struct list_head		list;
	enum main_state			state;
	enum chip_revision		revision;
	struct miscdevice		misc_dev;
	bool				misc_registered;
	struct device			*parent;
	struct device			*dev_bt;
	struct device			*dev_fm;
	struct file			*filp;
	struct mutex			management_mutex;
	struct mutex			bt_mutex;
	struct mutex			fm_mutex;
	int				nbr_of_users_active;
	struct cg2900_dai_conf_i2s	i2s_config;
	struct cg2900_dai_conf_i2s_pcm	i2s_pcm_config;
	bool				i2s_config_known;
	bool				i2s_pcm_config_known;
	struct endpoint_list		endpoints;
	u8				stream_ids[16];
};

/**
 * struct audio_user - CG2900 audio user info structure.
 * @session:	Stored session for the char device.
 * @resp_state:	State for controller communications.
 * @info:	CG2900 audio info structure.
 */
struct audio_user {
	int			session;
	enum chip_resp_state	resp_state;
	struct audio_info	*info;
};

/**
 * struct audio_cb_info - Callback info structure registered in @user_data.
 * @user:	Audio user currently awaiting data on the channel.
 * @wq:		Wait queue for this channel.
 * @skb_queue:	Sk buffer queue.
 */
struct audio_cb_info {
	struct audio_user	*user;
	wait_queue_head_t	wq;
	struct sk_buff_head	skb_queue;
};

/**
 * struct char_dev_info - CG2900 character device info structure.
 * @session:		Stored session for the char device.
 * @stored_data:	Data returned when executing last command, if any.
 * @stored_data_len:	Length of @stored_data in bytes.
 * @management_mutex:	Mutex for handling access to char dev management.
 * @rw_mutex:		Mutex for handling access to char dev writes and reads.
 * @info:		CG2900 audio info struct.
 * @rx_queue:		Data queue.
 */
struct char_dev_info {
	int			session;
	u8			*stored_data;
	int			stored_data_len;
	struct mutex		management_mutex;
	struct mutex		rw_mutex;
	struct audio_info	*info;
	struct sk_buff_head	rx_queue;
};

/*
 * cg2900_audio_devices - List of active CG2900 audio devices.
 */
LIST_HEAD(cg2900_audio_devices);

/*
 * cg2900_audio_sessions - Pointers to currently opened sessions (maps
 *			   session ID to user info).
 */
static struct audio_user *cg2900_audio_sessions[MAX_NBR_OF_USERS];

/*
 *	Internal conversion functions
 *
 *	Since the CG2900 APIs uses several different ways to encode the
 *	same parameter in different cases, we have to use translator
 *	functions.
 */

/**
 * session_config_sample_rate() - Convert sample rate to format used in VS_Set_SessionConfiguration.
 * @rate: Sample rate in API encoding.
 */
static u8 session_config_sample_rate(enum cg2900_endpoint_sample_rate rate)
{
	static const u8 codes[] = {
		[ENDPOINT_SAMPLE_RATE_8_KHZ]    = CG2900_BT_SESSION_RATE_8K,
		[ENDPOINT_SAMPLE_RATE_16_KHZ]   = CG2900_BT_SESSION_RATE_16K,
		[ENDPOINT_SAMPLE_RATE_44_1_KHZ] = CG2900_BT_SESSION_RATE_44_1K,
		[ENDPOINT_SAMPLE_RATE_48_KHZ]   = CG2900_BT_SESSION_RATE_48K
	};

	return codes[rate];
}

/**
 * mc_i2s_sample_rate() - Convert sample rate to format used in VS_Port_Config for I2S.
 * @rate: Sample rate in API encoding.
 */
static u8 mc_i2s_sample_rate(enum cg2900_dai_sample_rate rate)
{
	static const u8 codes[] = {
		[SAMPLE_RATE_8]    = CG2900_MC_I2S_SAMPLE_RATE_8,
		[SAMPLE_RATE_16]   = CG2900_MC_I2S_SAMPLE_RATE_16,
		[SAMPLE_RATE_44_1] = CG2900_MC_I2S_SAMPLE_RATE_44_1,
		[SAMPLE_RATE_48]   = CG2900_MC_I2S_SAMPLE_RATE_48
	};

	return codes[rate];
}

/**
 * mc_pcm_sample_rate() - Convert sample rate to format used in VS_Port_Config for PCM/I2S.
 * @rate: Sample rate in API encoding.
 */
static u8 mc_pcm_sample_rate(enum cg2900_dai_sample_rate rate)
{
	static const u8 codes[] = {
		[SAMPLE_RATE_8]    = CG2900_MC_PCM_SAMPLE_RATE_8,
		[SAMPLE_RATE_16]   = CG2900_MC_PCM_SAMPLE_RATE_16,
		[SAMPLE_RATE_44_1] = CG2900_MC_PCM_SAMPLE_RATE_44_1,
		[SAMPLE_RATE_48]   = CG2900_MC_PCM_SAMPLE_RATE_48
	};

	return codes[rate];
}

/**
 * mc_i2s_channel_select() - Convert channel selection to format used in VS_Port_Config.
 * @sel: Channel selection in API encoding.
 */
static u8 mc_i2s_channel_select(enum cg2900_dai_channel_sel sel)
{
	static const u8 codes[] = {
		[CHANNEL_SELECTION_RIGHT] = CG2900_MC_I2S_RIGHT_CHANNEL,
		[CHANNEL_SELECTION_LEFT]  = CG2900_MC_I2S_LEFT_CHANNEL,
		[CHANNEL_SELECTION_BOTH]  = CG2900_MC_I2S_BOTH_CHANNELS
	};
	return codes[sel];
}

/**
 * get_fs_duration() - Convert framesync-enumeration to real value.
 * @duration: Framsync duration (API encoding).
 *
 * Returns:
 * Duration in bits.
 */
static u16 get_fs_duration(enum cg2900_dai_fs_duration duration)
{
	static const u16 values[] = {
		[SYNC_DURATION_8] = 8,
		[SYNC_DURATION_16] = 16,
		[SYNC_DURATION_24] = 24,
		[SYNC_DURATION_32] = 32,
		[SYNC_DURATION_48] = 48,
		[SYNC_DURATION_50] = 50,
		[SYNC_DURATION_64] = 64,
		[SYNC_DURATION_75] = 75,
		[SYNC_DURATION_96] = 96,
		[SYNC_DURATION_125] = 125,
		[SYNC_DURATION_128] = 128,
		[SYNC_DURATION_150] = 150,
		[SYNC_DURATION_192] = 192,
		[SYNC_DURATION_250] = 250,
		[SYNC_DURATION_256] = 256,
		[SYNC_DURATION_300] = 300,
		[SYNC_DURATION_384] = 384,
		[SYNC_DURATION_500] = 500,
		[SYNC_DURATION_512] = 512,
		[SYNC_DURATION_600] = 600,
		[SYNC_DURATION_768] = 768
	};
	return values[duration];
}

/**
 * mc_i2s_role() - Convert master/slave encoding to format for I2S-ports.
 * @mode: Master/slave in API encoding.
 */
static u8 mc_i2s_role(enum cg2900_dai_mode mode)
{
	if (mode == DAI_MODE_SLAVE)
		return CG2900_I2S_MODE_SLAVE;
	else
		return CG2900_I2S_MODE_MASTER;
}

/**
 * mc_pcm_role() - Convert master/slave encoding to format for PCM/I2S-port.
 * @mode: Master/slave in API encoding.
 */
static u8 mc_pcm_role(enum cg2900_dai_mode mode)
{
	if (mode == DAI_MODE_SLAVE)
		return CG2900_PCM_MODE_SLAVE;
	else
		return CG2900_PCM_MODE_MASTER;
}

/**
 * fm_get_conversion() - Convert sample rate to convert up/down used in X_Set_Control FM commands.
 * @srate: Sample rate.
 */
static u16 fm_get_conversion(struct audio_info *info,
		enum cg2900_endpoint_sample_rate srate)
{
	/*
	 * For CG2910, Set the external sample rate (host side)
	 * of the digital output in units of [10Hz]
	 */
	if (info->revision == CG2900_CHIP_REV_PG1 ||
			info->revision == CG2900_CHIP_REV_PG2) {
		if (srate >= ENDPOINT_SAMPLE_RATE_44_1_KHZ)
			return CG2900_FM_CMD_SET_CTRL_CONV_UP;
		else
			return CG2900_FM_CMD_SET_CTRL_CONV_DOWN;
	} else {
		if (srate > ENDPOINT_SAMPLE_RATE_44_1_KHZ)
			return CG2910_FM_CMD_SET_CTRL_48000_HEX;
		else
			return CG2910_FM_CMD_SET_CTRL_44100_HEX;
	}
}

/**
 * get_info() - Return info structure for this device.
 * @dev:	Current device.
 *
 * This function returns the info structure on the following basis:
 *	* If dev is NULL return first info struct found. If none is found return
 *	  NULL.
 *	* If dev is valid we will return corresponding info struct if dev is the
 *	  parent of the info struct or if dev's parent is the parent of the info
 *	  struct.
 *	* If dev is valid and no info structure is found, a new info struct is
 *	  allocated, initialized, and returned.
 *
 * Returns:
 *   Pointer to info struct if there is no error.
 *   NULL if NULL was supplied and no info structure exist.
 *   ERR_PTR(-ENOMEM) if allocation fails.
 */
static struct audio_info *get_info(struct device *dev)
{
	struct list_head *cursor;
	struct audio_info *tmp;
	struct audio_info *info = NULL;

	/*
	 * Find the info structure for dev. If NULL is supplied for dev
	 * just return first device found.
	 */
	list_for_each(cursor, &cg2900_audio_devices) {
		tmp = list_entry(cursor, struct audio_info, list);
		if (!dev || tmp->parent == dev->parent || tmp->parent == dev) {
			info = tmp;
			break;
		}
	}

	if (!dev || info)
		return info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "Could not allocate info struct\n");
		return ERR_PTR(-ENOMEM);
	}
	info->parent = dev->parent;

	/* Initiate the mutexes */
	mutex_init(&(info->management_mutex));
	mutex_init(&(info->bt_mutex));
	mutex_init(&(info->fm_mutex));
	mutex_init(&(info->endpoints.management_mutex));

	/* Initiate the endpoint list */
	INIT_LIST_HEAD(&info->endpoints.ep_list);

	list_add_tail(&info->list, &cg2900_audio_devices);

	dev_info(dev, "CG2900 device added\n");
	return info;
}

/**
 * flush_endpoint_list() - Deletes all stored endpoints in @list.
 * @list:	List of endpoints.
 */
static void flush_endpoint_list(struct endpoint_list *list)
{
	struct list_head *cursor, *next;
	struct endpoint_config_node *tmp;

	mutex_lock(&list->management_mutex);
	list_for_each_safe(cursor, next, &(list->ep_list)) {
		tmp = list_entry(cursor, struct endpoint_config_node, list);
		list_del(cursor);
		kfree(tmp);
	}
	mutex_unlock(&list->management_mutex);
}

/**
 * device_removed() - Remove device from list if there are no channels left.
 * @info:	CG2900 audio info structure.
 */
static void device_removed(struct audio_info *info)
{
	struct list_head *cursor;
	struct audio_info *tmp;

	if (info->dev_bt || info->dev_fm)
		/* There are still devices active */
		return;

	/* Find the stored info structure */
	list_for_each(cursor, &cg2900_audio_devices) {
		tmp = list_entry(cursor, struct audio_info, list);
		if (tmp == info) {
			list_del(cursor);
			break;
		}
	}

	flush_endpoint_list(&info->endpoints);

	mutex_destroy(&info->management_mutex);
	mutex_destroy(&info->bt_mutex);
	mutex_destroy(&info->fm_mutex);
	mutex_destroy(&info->endpoints.management_mutex);

	kfree(info);
	pr_info("CG2900 Audio device removed");
}

/**
 * read_cb() - Handle data received from STE connectivity driver.
 * @dev:	Device receiving data.
 * @skb:	Buffer with data coming form device.
 */
static void read_cb(struct cg2900_user_data *dev, struct sk_buff *skb)
{
	struct audio_cb_info *cb_info;

	cb_info = cg2900_get_usr(dev);

	if (!(cb_info->user)) {
		dev_err(dev->dev, "NULL supplied as cb_info->user\n");
		return;
	}

	/* Mark that packet has been received */
	dev_dbg(dev->dev, "New resp_state: RESP_RECEIVED");
	cb_info->user->resp_state = RESP_RECEIVED;
	skb_queue_tail(&cb_info->skb_queue, skb);
	wake_up_all(&cb_info->wq);
}

/**
 * reset_cb() - Reset callback function.
 * @dev:	CG2900_Core device resetting.
 */
static void reset_cb(struct cg2900_user_data *dev)
{
	struct audio_info *info;

	dev_dbg(dev->dev, "reset_cb\n");

	info = dev_get_drvdata(dev->dev);
	mutex_lock(&info->management_mutex);
	info->nbr_of_users_active = 0;
	info->state = RESET;
	mutex_unlock(&info->management_mutex);
}

/**
 * get_session_user() - Check that supplied session is within valid range.
 * @session:	Session ID.
 *
 * Returns:
 *   Audio_user if there is no error.
 *   NULL for bad session ID.
 */
static struct audio_user *get_session_user(int session)
{
	struct audio_user *audio_user;

	if (session < FIRST_USER || session >= MAX_NBR_OF_USERS) {
		pr_err("Calling with invalid session %d", session);
		return NULL;
	}

	audio_user = cg2900_audio_sessions[session];
	if (!audio_user)
		pr_err("Calling with non-opened session %d", session);
	return audio_user;
}

/**
 * del_endpoint_private() - Deletes an endpoint from @list.
 * @endpoint_id:	Endpoint ID.
 * @list:		List of endpoints.
 *
 * Deletes an endpoint from the supplied endpoint list.
 * This function is not protected by any semaphore.
 */
static void del_endpoint_private(enum cg2900_audio_endpoint_id endpoint_id,
				 struct endpoint_list *list)
{
	struct list_head *cursor, *next;
	struct endpoint_config_node *tmp;

	list_for_each_safe(cursor, next, &(list->ep_list)) {
		tmp = list_entry(cursor, struct endpoint_config_node, list);
		if (tmp->endpoint_id == endpoint_id) {
			list_del(cursor);
			kfree(tmp);
		}
	}
}

/**
 * add_endpoint() - Add endpoint node to @list.
 * @ep_config:	Endpoint configuration.
 * @list:	List of endpoints.
 *
 * Add endpoint node to the supplied list and copies supplied config to node.
 * If a node already exists for the supplied endpoint, the old node is removed
 * and replaced by the new node.
 */
static void add_endpoint(struct cg2900_endpoint_config *ep_config,
			 struct endpoint_list *list)
{
	struct endpoint_config_node *item;

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		pr_err("add_endpoint: Failed to alloc memory");
		return;
	}

	/* Store values */
	item->endpoint_id = ep_config->endpoint_id;
	memcpy(&(item->config), &(ep_config->config), sizeof(item->config));

	mutex_lock(&(list->management_mutex));

	/*
	 * Check if endpoint ID already exist in list.
	 * If that is the case, remove it.
	 */
	if (!list_empty(&(list->ep_list)))
		del_endpoint_private(ep_config->endpoint_id, list);

	list_add_tail(&(item->list), &(list->ep_list));

	mutex_unlock(&(list->management_mutex));
}

/**
 * find_endpoint() - Finds endpoint identified by @endpoint_id in @list.
 * @endpoint_id:	Endpoint ID.
 * @list:		List of endpoints.
 *
 * Returns:
 *   Endpoint configuration if there is no error.
 *   NULL if no configuration can be found for @endpoint_id.
 */
static union cg2900_endpoint_config_union *
find_endpoint(enum cg2900_audio_endpoint_id endpoint_id,
	      struct endpoint_list *list)
{
	struct list_head *cursor, *next;
	struct endpoint_config_node *tmp;
	struct endpoint_config_node *ret_ep = NULL;

	mutex_lock(&list->management_mutex);
	list_for_each_safe(cursor, next, &(list->ep_list)) {
		tmp = list_entry(cursor, struct endpoint_config_node, list);
		if (tmp->endpoint_id == endpoint_id) {
			ret_ep = tmp;
			break;
		}
	}
	mutex_unlock(&list->management_mutex);

	if (ret_ep)
		return &(ret_ep->config);
	else
		return NULL;
}

/**
 * new_stream_id() - Allocate a new stream id.
 * @info:	Current audio info struct.
 *
 * Returns:
 *  0-127 new valid id.
 *  -ENOMEM if no id is available.
 */
static s8 new_stream_id(struct audio_info *info)
{
	int r;

	mutex_lock(&info->management_mutex);

	r = find_first_zero_bit(info->stream_ids,
				8 * sizeof(info->stream_ids));

	if (r >= 8 * sizeof(info->stream_ids)) {
		r = -ENOMEM;
		goto out;
	}

	set_bit(r, (unsigned long int *)info->stream_ids);

out:
	mutex_unlock(&info->management_mutex);
	return r;
}

/**
 * release_stream_id() - Release a stream id.
 * @info:	Current audio info struct.
 * @id:		Stream to release.
 */
static void release_stream_id(struct audio_info *info, u8 id)
{
	if (id >= 8 * sizeof(info->stream_ids))
		return;

	mutex_lock(&info->management_mutex);
	clear_bit(id, (unsigned long int *)info->stream_ids);
	mutex_unlock(&info->management_mutex);
}

/**
 * receive_fm_write_response() - Wait for and handle the response to an FM Legacy WriteCommand request.
 * @audio_user:	Audio user to check for.
 * @command:	FM command to wait for.
 *
 * This function first waits (up to 5 seconds) for a response to an FM
 * write command and when one arrives, it checks that it is the one we
 * are waiting for and also that no error has occurred.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -EIO for other errors.
 */
static int receive_fm_write_response(struct audio_user *audio_user,
				     u16 command)
{
	int err = 0;
	int res;
	struct sk_buff *skb;
	struct fm_leg_cmd_cmpl *pkt;
	u16 rsp_cmd;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = audio_user->info;
	pf_data = dev_get_platdata(info->dev_fm);
	cb_info = cg2900_get_usr(pf_data);

	/*
	 * Wait for callback to receive command complete and then wake us up
	 * again.
	 */
	res = wait_event_timeout(cb_info->wq,
				 audio_user->resp_state == RESP_RECEIVED,
				 msecs_to_jiffies(RESP_TIMEOUT));
	if (!res) {
		dev_err(FM_DEV, "Timeout while waiting for return packet\n");
		return -ECOMM;
	} else if (res < 0) {
		dev_err(FM_DEV,
			"Error %d occurred while waiting for return packet\n",
			res);
		return -ECOMM;
	}

	/* OK, now we should have received answer. Let's check it. */
	skb = skb_dequeue_tail(&cb_info->skb_queue);
	if (!skb) {
		dev_err(FM_DEV, "No skb in queue when it should be there\n");
		return -EIO;
	}

	pkt = (struct fm_leg_cmd_cmpl *)skb->data;

	/* Check if we received the correct event */
	if (pkt->opcode != CG2900_FM_GEN_ID_LEGACY) {
		dev_err(FM_DEV,
			"Received unknown FM packet. 0x%X %X %X %X %X\n",
			skb->data[0], skb->data[1], skb->data[2],
			skb->data[3], skb->data[4]);
		err = -EIO;
		goto error_handling_free_skb;
	}

	/* FM Legacy Command complete event */
	rsp_cmd = cg2900_get_fm_cmd_id(le16_to_cpu(pkt->response_head));

	if (pkt->fm_function != CG2900_FM_CMD_PARAM_WRITECOMMAND ||
	    rsp_cmd != command) {
		dev_err(FM_DEV,
			"Received unexpected packet func 0x%X cmd 0x%04X\n",
			pkt->fm_function, rsp_cmd);
		err = -EIO;
		goto error_handling_free_skb;
	}

	if (pkt->cmd_status != CG2900_FM_CMD_STATUS_COMMAND_SUCCEEDED) {
		dev_err(FM_DEV, "FM Command failed (%d)\n", pkt->cmd_status);
		err = -EIO;
		goto error_handling_free_skb;
	}
	/* Operation succeeded. We are now done */

error_handling_free_skb:
	kfree_skb(skb);
	return err;
}

/**
 * receive_bt_cmd_complete() - Wait for and handle an BT Command Complete event.
 * @audio_user:	Audio user to check for.
 * @rsp:	Opcode of BT command to wait for.
 * @data:	Pointer to buffer if any received data should be stored (except
 *		status).
 * @data_len:	Length of @data in bytes.
 *
 * This function first waits for BT Command Complete event (up to 5 seconds)
 * and when one arrives, it checks that it is the one we are waiting for and
 * also that no error has occurred.
 * If @data is supplied it also copies received data into @data.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -EIO for other errors.
 */
static int receive_bt_cmd_complete(struct audio_user *audio_user, u16 rsp,
				   void *data, int data_len)
{
	int err = 0;
	int res;
	struct sk_buff *skb;
	struct bt_cmd_cmpl_event *evt;
	u16 opcode;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = audio_user->info;
	pf_data = dev_get_platdata(info->dev_bt);
	cb_info = cg2900_get_usr(pf_data);

	/*
	 * Wait for callback to receive command complete and then wake us up
	 * again.
	 */
	res = wait_event_timeout(cb_info->wq,
				 audio_user->resp_state == RESP_RECEIVED,
				 msecs_to_jiffies(RESP_TIMEOUT));
	if (!res) {
		dev_err(BT_DEV, "Timeout while waiting for return packet\n");
		return -ECOMM;
	} else if (res < 0) {
		/* We timed out or an error occurred */
		dev_err(BT_DEV,
			"Error %d occurred while waiting for return packet\n",
			res);
		return -ECOMM;
	}

	/* OK, now we should have received answer. Let's check it. */
	skb = skb_dequeue_tail(&cb_info->skb_queue);
	if (!skb) {
		dev_err(BT_DEV, "No skb in queue when it should be there\n");
		return -EIO;
	}

	evt = (struct bt_cmd_cmpl_event *)skb->data;
	if (evt->eventcode != HCI_EV_CMD_COMPLETE) {
		dev_err(BT_DEV,
			"We did not receive the event we expected (0x%X)\n",
			evt->eventcode);
		err = -EIO;
		goto error_handling_free_skb;
	}

	opcode = le16_to_cpu(evt->opcode);
	if (opcode != rsp) {
		dev_err(BT_DEV,
			"Received cmd complete for unexpected command: "
			"0x%04X\n", opcode);
		err = -EIO;
		goto error_handling_free_skb;
	}

	if (evt->status != HCI_BT_ERROR_NO_ERROR) {
		dev_err(BT_DEV, "Received command complete with err %d\n",
			evt->status);
		err = -EIO;
		/*
		* In data there might be more detailed error code.
		* Let's copy it.
		*/
	}

	/*
	 * Copy the rest of the parameters if a buffer has been supplied.
	 * The caller must have set the length correctly.
	 */
	if (data)
		memcpy(data, evt->data, data_len);

	/* Operation succeeded. We are now done */

error_handling_free_skb:
	kfree_skb(skb);
	return err;
}

/**
 * send_vs_delete_stream() - Delete an audio stream defined by @stream_handle.
 * @audio_user:		Audio user to check for.
 * @stream_handle:	Handle of the audio stream.
 *
 * This function is used to delete an audio stream defined by a stream
 * handle.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -ENOMEM upon allocation errors.
 *   Errors from @cg2900_write.
 *   -EIO for other errors.
 */
static int send_vs_delete_stream(struct audio_user *audio_user,
			    unsigned int stream_handle)
{
	int err = 0;
	struct sk_buff *skb;
	u16 opcode;
	struct audio_info *info = audio_user->info;
	struct cg2900_user_data *pf_data = dev_get_platdata(info->dev_bt);
	struct audio_cb_info *cb_info = cg2900_get_usr(pf_data);

	/* Now delete the stream - format command... */
	if (info->revision == CG2900_CHIP_REV_PG1) {
		struct bt_vs_reset_session_cfg_cmd *cmd;

		dev_dbg(BT_DEV, "BT: HCI_VS_Reset_Session_Configuration\n");

		skb = pf_data->alloc_skb(sizeof(*cmd), GFP_KERNEL);
		if (!skb) {
			dev_err(BT_DEV, "Could not allocate skb\n");
			err = -ENOMEM;
			return err;
		}

		cmd = (struct bt_vs_reset_session_cfg_cmd *)
			skb_put(skb, sizeof(*cmd));

		opcode = CG2900_BT_VS_RESET_SESSION_CONFIG;
		cmd->opcode = cpu_to_le16(opcode);
		cmd->plen   = BT_PARAM_LEN(sizeof(*cmd));
		cmd->id     = (u8)stream_handle;
	} else {
		struct mc_vs_delete_stream_cmd *cmd;

		dev_dbg(BT_DEV, "BT: HCI_VS_Delete_Stream\n");

		skb = pf_data->alloc_skb(sizeof(*cmd), GFP_KERNEL);
		if (!skb) {
			dev_err(BT_DEV, "Could not allocate skb\n");
			err = -ENOMEM;
			return err;
		}

		cmd = (struct mc_vs_delete_stream_cmd *)
			skb_put(skb, sizeof(*cmd));

		opcode = CG2900_MC_VS_DELETE_STREAM;
		cmd->opcode = cpu_to_le16(opcode);
		cmd->plen   = BT_PARAM_LEN(sizeof(*cmd));
		cmd->stream = (u8)stream_handle;
	}

	/* ...and send it */
	cb_info->user = audio_user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	audio_user->resp_state = WAITING;

	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		goto error_handling_free_skb;
	}

	/* wait for response */
	if (info->revision == CG2900_CHIP_REV_PG1) {
		err = receive_bt_cmd_complete(audio_user, opcode, NULL, 0);
	} else {
		u8 vs_err;

		/* All commands on CG2900 PG2 onwards
		 * API returns one byte extra status */
		err = receive_bt_cmd_complete(audio_user, opcode,
					      &vs_err, sizeof(vs_err));

	if (err)
		dev_err(BT_DEV,
			"VS_DELETE_STREAM - failed with error 0x%02X\n",
			vs_err);
		else
			release_stream_id(info, stream_handle);
	}

	return err;

error_handling_free_skb:
	kfree_skb(skb);
	return err;
}

/**
 * send_vs_session_ctrl() - Formats an sends a CG2900_BT_VS_SESSION_CTRL command.
 * @user:		Audio user this command belongs to.
 * @stream_handle:	Handle to stream.
 * @command:		Command to execute on stream, should be one of
 *			CG2900_BT_SESSION_START, CG2900_BT_SESSION_STOP,
 *			CG2900_BT_SESSION_PAUSE, CG2900_BT_SESSION_RESUME.
 *
 * Packs and sends a command packet and waits for the response. Must
 * be called with the bt_mutex held.
 *
 * Returns:
 *  0 if there is no error.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int send_vs_session_ctrl(struct audio_user *user,
				u8 stream_handle, u8 command)
{
	int err = 0;
	struct bt_vs_session_ctrl_cmd *pkt;
	struct sk_buff *skb;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = user->info;
	pf_data = dev_get_platdata(info->dev_bt);
	cb_info = cg2900_get_usr(pf_data);

	dev_dbg(BT_DEV, "BT: HCI_VS_Session_Control handle: %d cmd: %d\n",
		stream_handle, command);

	skb = pf_data->alloc_skb(sizeof(*pkt), GFP_KERNEL);
	if (!skb) {
		dev_err(BT_DEV,
			"send_vs_session_ctrl: Could not allocate skb\n");
		return -ENOMEM;
	}

	/* Enter data into the skb */
	pkt = (struct bt_vs_session_ctrl_cmd *) skb_put(skb, sizeof(*pkt));

	pkt->opcode  = cpu_to_le16(CG2900_BT_VS_SESSION_CTRL);
	pkt->plen    = BT_PARAM_LEN(sizeof(*pkt));
	pkt->id      = stream_handle;
	pkt->control = command; /* Start/stop etc */

	cb_info->user = user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	user->resp_state = WAITING;

	/* Send packet to controller */
	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		kfree_skb(skb);
		goto finished;
	}

	err = receive_bt_cmd_complete(user, CG2900_BT_VS_SESSION_CTRL,
				      NULL, 0);
finished:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	user->resp_state = IDLE;
	return err;
}

/**
 * send_vs_session_config() - Formats an sends a CG2900_BT_VS_SESSION_CONFIG command.
 * @user:		Audio user this command belongs to.
 * @config_stream:	Custom function for configuring the stream.
 * @priv_data:		Private data passed to @config_stream untouched.
 *
 * Packs and sends a command packet and waits for the response. Must
 * be called with the bt_mutex held.
 *
 * Space is allocated for one stream and a custom function is used to
 * fill in the stream configuration.
 *
 * Returns:
 *  0-255 stream handle if no error.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int send_vs_session_config(struct audio_user *user,
	void(*config_stream)(struct audio_info *, void *,
			     struct session_config_stream *),
	void *priv_data)
{
	int err = 0;
	struct sk_buff *skb;
	struct bt_vs_session_config_cmd *pkt;
	u8 session_id;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = user->info;
	pf_data = dev_get_platdata(info->dev_bt);
	cb_info = cg2900_get_usr(pf_data);

	dev_dbg(BT_DEV, "BT: HCI_VS_Set_Session_Configuration\n");

	skb = pf_data->alloc_skb(sizeof(*pkt), GFP_KERNEL);
	if (!skb) {
		dev_err(BT_DEV,
			"send_vs_session_config: Could not allocate skb\n");
		return -ENOMEM;
	}

	pkt = (struct bt_vs_session_config_cmd *)skb_put(skb, sizeof(*pkt));
	/* zero the packet so we don't have to set all reserved fields */
	memset(pkt, 0, sizeof(*pkt));

	/* Common parameters */
	pkt->opcode    = cpu_to_le16(CG2900_BT_VS_SET_SESSION_CONFIG);
	pkt->plen      = BT_PARAM_LEN(sizeof(*pkt));
	pkt->n_streams = 1; /* 1 stream configuration supplied */

	/* Let the custom-function fill in the rest */
	config_stream(info, priv_data, &pkt->stream);

	cb_info->user = user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	user->resp_state = WAITING;

	/* Send packet to controller */
	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		kfree_skb(skb);
		goto finished;
	}

	err = receive_bt_cmd_complete(user,
				      CG2900_BT_VS_SET_SESSION_CONFIG,
				      &session_id, sizeof(session_id));
	/* Return session id/stream handle if success */
	if (!err)
		err = session_id;

finished:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	user->resp_state = IDLE;
	return err;
}

/**
 * send_fm_write_1_param() - Formats and sends an FM legacy write command with one parameter.
 * @user:	Audio user this command belongs to.
 * @command:	Command.
 * @param:	Parameter for command.
 *
 * Packs and sends a command packet and waits for the response. Must
 * be called with the fm_mutex held.
 *
 * Returns:
 *  0 if there is no error.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int send_fm_write_1_param(struct audio_user *user,
				 u16 command, u16 param)
{
	int err = 0;
	struct sk_buff *skb;
	struct fm_leg_cmd *cmd;
	size_t len;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = user->info;
	pf_data = dev_get_platdata(info->dev_fm);
	cb_info = cg2900_get_usr(pf_data);

	dev_dbg(FM_DEV, "send_fm_write_1_param cmd 0x%X param 0x%X\n",
		command, param);

	/* base package + one parameter */
	len = sizeof(*cmd) + sizeof(cmd->fm_cmd.data[0]);

	skb = pf_data->alloc_skb(len, GFP_KERNEL);
	if (!skb) {
		dev_err(FM_DEV,
			"send_fm_write_1_param: Could not allocate skb\n");
		return -ENOMEM;
	}

	cmd = (struct fm_leg_cmd *)skb_put(skb, len);

	cmd->length      = CG2900_FM_CMD_PARAM_LEN(len);
	cmd->opcode      = CG2900_FM_GEN_ID_LEGACY;
	cmd->read_write  = CG2900_FM_CMD_LEG_PARAM_WRITE;
	cmd->fm_function = CG2900_FM_CMD_PARAM_WRITECOMMAND;
	/* one parameter - builtin assumption for this function */
	cmd->fm_cmd.head    = cpu_to_le16(cg2900_make_fm_cmd_id(command, 1));
	cmd->fm_cmd.data[0] = cpu_to_le16(param);

	cb_info->user = user;
	dev_dbg(FM_DEV, "New resp_state: WAITING\n");
	user->resp_state = WAITING;

	/* Send packet to controller */
	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(FM_DEV, "Error %d occurred while transmitting skb\n",
			err);
		kfree_skb(skb);
		goto finished;
	}

	err = receive_fm_write_response(user, command);
finished:
	dev_dbg(FM_DEV, "New resp_state: IDLE\n");
	user->resp_state = IDLE;
	return err;
}

/**
 * send_vs_stream_ctrl() - Formats an sends a CG2900_MC_VS_STREAM_CONTROL command.
 * @user:	Audio user this command belongs to.
 * @stream:	Stream id.
 * @command:	Start/stop etc.
 *
 * Packs and sends a command packet and waits for the response. Must
 * be called with the bt_mutex held.
 *
 * While the HCI command allows for multiple streams in one command,
 * this function only handles one.
 *
 * Returns:
 *  0 if there is no error.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int send_vs_stream_ctrl(struct audio_user *user, u8 stream, u8 command)
{
	int err = 0;
	struct sk_buff *skb;
	struct mc_vs_stream_ctrl_cmd *cmd;
	size_t len;
	u8 vs_err;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = user->info;
	pf_data = dev_get_platdata(info->dev_bt);
	cb_info = cg2900_get_usr(pf_data);

	dev_dbg(BT_DEV, "send_vs_stream_ctrl stream %d command %d\n", stream,
		command);

	/* basic length + one stream */
	len = sizeof(*cmd) + sizeof(cmd->stream[0]);

	skb = pf_data->alloc_skb(len, GFP_KERNEL);
	if (!skb) {
		dev_err(BT_DEV, "send_vs_stream_ctrl:Could not allocate skb\n");
		return -ENOMEM;
	}

	cmd = (struct mc_vs_stream_ctrl_cmd *)skb_put(skb, len);

	cmd->opcode  = cpu_to_le16(CG2900_MC_VS_STREAM_CONTROL);
	cmd->plen    = BT_PARAM_LEN(len);
	cmd->command = command;

	/* one stream */
	cmd->n_streams  = 1;
	cmd->stream[0] = stream;

	cb_info->user = user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	user->resp_state = WAITING;

	/* Send packet to controller */
	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		kfree_skb(skb);
		goto finished;
	}

	/* All commands on CG2900 PG2 onwards
	 * API returns one byte extra status */
	err = receive_bt_cmd_complete(user,
				      CG2900_MC_VS_STREAM_CONTROL,
				      &vs_err, sizeof(vs_err));
	if (err)
		dev_err(BT_DEV,
			"VS_STREAM_CONTROL - failed with error 0x%02x\n",
			vs_err);

finished:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	user->resp_state = IDLE;
	return err;
}

/**
 * send_vs_create_stream() - Formats an sends a CG2900_MC_VS_CREATE_STREAM command.
 * @user:	Audio user this command belongs to.
 * @inport:	Stream id.
 * @outport:	Start/stop etc.
 * @order:	Activation order.
 *
 * Packs and sends a command packet and waits for the response. Must
 * be called with the bt_mutex held.
 *
 * Returns:
 *  0 if there is no error.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int send_vs_create_stream(struct audio_user *user, u8 inport,
				 u8 outport, u8 order)
{
	int err = 0;
	struct sk_buff *skb;
	struct mc_vs_create_stream_cmd *cmd;
	s8 id;
	u8 vs_err;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = user->info;
	pf_data = dev_get_platdata(info->dev_bt);
	cb_info = cg2900_get_usr(pf_data);

	dev_dbg(BT_DEV,
		"send_vs_create_stream inport %d outport %d order %d\n",
		inport, outport, order);

	id = new_stream_id(info);
	if (id < 0) {
		dev_err(BT_DEV, "No free stream id\n");
		err = -EIO;
		goto finished;
	}

	skb = pf_data->alloc_skb(sizeof(*cmd), GFP_KERNEL);
	if (!skb) {
		dev_err(BT_DEV,
			"send_vs_create_stream: Could not allocate skb\n");
		err = -ENOMEM;
		goto finished_release_id;
	}

	cmd = (struct mc_vs_create_stream_cmd *)skb_put(skb, sizeof(*cmd));

	cmd->opcode  = cpu_to_le16(CG2900_MC_VS_CREATE_STREAM);
	cmd->plen    = BT_PARAM_LEN(sizeof(*cmd));
	cmd->id      = (u8)id;
	cmd->inport  = inport;
	cmd->outport = outport;
	cmd->order   = order;

	cb_info->user = user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	user->resp_state = WAITING;

	/* Send packet to controller */
	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		kfree_skb(skb);
		goto finished_release_id;
	}

	/* All commands on CG2900 PG2 onwards
	 * API returns one byte extra status */
	err = receive_bt_cmd_complete(user,
				      CG2900_MC_VS_CREATE_STREAM,
				      &vs_err, sizeof(vs_err));
	if (err) {
		dev_err(BT_DEV,
			"VS_CREATE_STREAM - failed with error 0x%02x\n",
			vs_err);
		goto finished_release_id;
	}

	err = id;
	goto finished;

finished_release_id:
	release_stream_id(info, id);
finished:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	user->resp_state = IDLE;
	return err;
}

/**
 * send_vs_port_cfg() - Formats an sends a CG2900_MC_VS_PORT_CONFIG command.
 * @user:	Audio user this command belongs to.
 * @port:	Port id to configure.
 * @cfg:	Pointer to specific configuration.
 * @cfglen:	Length of configuration.
 *
 * Packs and sends a command packet and waits for the response. Must
 * be called with the bt_mutex held.
 *
 * Returns:
 *  0 if there is no error.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int send_vs_port_cfg(struct audio_user *user, u8 port,
			    const void *cfg, size_t cfglen)
{
	int err = 0;
	struct sk_buff *skb;
	struct mc_vs_port_cfg_cmd *cmd;
	void *ptr;
	u8 vs_err;
	struct audio_cb_info *cb_info;
	struct audio_info *info;
	struct cg2900_user_data *pf_data;

	info = user->info;
	pf_data = dev_get_platdata(info->dev_bt);
	cb_info = cg2900_get_usr(pf_data);

	dev_dbg(BT_DEV, "send_vs_port_cfg len %d\n", cfglen);

	skb = pf_data->alloc_skb(sizeof(*cmd) + cfglen, GFP_KERNEL);
	if (!skb) {
		dev_err(BT_DEV, "send_vs_port_cfg: Could not allocate skb\n");
		return -ENOMEM;
	}

	/* Fill in common part */
	cmd = (struct mc_vs_port_cfg_cmd *) skb_put(skb, sizeof(*cmd));
	cmd->opcode = cpu_to_le16(CG2900_MC_VS_PORT_CONFIG);
	cmd->plen = BT_PARAM_LEN(sizeof(*cmd) + cfglen);
	cmd->type = port;

	/* Copy specific configuration */
	ptr = skb_put(skb, cfglen);
	memcpy(ptr, cfg, cfglen);

	/* Send */
	cb_info->user = user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	user->resp_state = WAITING;

	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		kfree_skb(skb);
		goto finished;
	}

	/* All commands on CG2900 PG2 onwards
	 * API returns one byte extra status */
	err = receive_bt_cmd_complete(user, CG2900_MC_VS_PORT_CONFIG,
				      &vs_err, sizeof(vs_err));
	if (err)
		dev_err(BT_DEV, "VS_PORT_CONFIG - failed with error 0x%02x\n",
			vs_err);

finished:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	user->resp_state = IDLE;
	return err;
}

/**
 * set_dai_config_pg1() - Internal implementation of @cg2900_audio_set_dai_config for PG1 hardware.
 * @audio_user:	Pointer to audio user struct.
 * @config:	Pointer to the configuration to set.
 *
 * Sets the Digital Audio Interface (DAI) configuration for PG1
 * hardware. This is and internal function and basic
 * argument-verification should have been done by the caller.
 *
 * Returns:
 *  0 if there is no error.
 *  -EACCESS if port is not supported.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int set_dai_config_pg1(struct audio_user *audio_user,
			      struct cg2900_dai_config *config)
{
	int err = 0;
	struct cg2900_dai_conf_i2s_pcm *i2s_pcm;
	struct sk_buff *skb = NULL;
	struct bt_vs_set_hw_cfg_cmd_i2s *i2s_cmd;
	struct bt_vs_set_hw_cfg_cmd_pcm *pcm_cmd;
	struct audio_info *info = audio_user->info;
	struct cg2900_user_data *pf_data = dev_get_platdata(info->dev_bt);
	struct audio_cb_info *cb_info = cg2900_get_usr(pf_data);

	dev_dbg(BT_DEV, "set_dai_config_pg1 port %d\n", config->port);

	/*
	 * Use mutex to assure that only ONE command is sent at any time on
	 * each channel.
	 */
	mutex_lock(&info->bt_mutex);

	/* Allocate the sk_buffer. The length is actually a max length since
	 * length varies depending on logical transport.
	 */
	skb = pf_data->alloc_skb(CG2900_BT_LEN_VS_SET_HARDWARE_CONFIG,
				 GFP_KERNEL);
	if (!skb) {
		dev_err(BT_DEV, "set_dai_config_pg1: Could not allocate skb\n");
		err = -ENOMEM;
		goto finished_unlock_mutex;
	}

	/* Fill in hci-command according to received configuration */
	switch (config->port) {
	case PORT_0_I2S:
		i2s_cmd = (struct bt_vs_set_hw_cfg_cmd_i2s *)
			skb_put(skb, sizeof(*i2s_cmd));

		i2s_cmd->opcode = cpu_to_le16(CG2900_BT_VS_SET_HARDWARE_CONFIG);
		i2s_cmd->plen   = BT_PARAM_LEN(sizeof(*i2s_cmd));

		i2s_cmd->vp_type = PORT_PROTOCOL_I2S;
		i2s_cmd->port_id = 0x00; /* First/only I2S port */
		i2s_cmd->half_period = config->conf.i2s.half_period;

		i2s_cmd->master_slave = mc_i2s_role(config->conf.i2s.mode);

		/* Store the new configuration */
		mutex_lock(&info->management_mutex);
		memcpy(&info->i2s_config, &config->conf.i2s,
		       sizeof(config->conf.i2s));
		info->i2s_config_known = true;
		mutex_unlock(&info->management_mutex);
		break;

	case PORT_1_I2S_PCM:
		pcm_cmd = (struct bt_vs_set_hw_cfg_cmd_pcm *)
			skb_put(skb, sizeof(*pcm_cmd));

		pcm_cmd->opcode = cpu_to_le16(CG2900_BT_VS_SET_HARDWARE_CONFIG);
		pcm_cmd->plen   = BT_PARAM_LEN(sizeof(*pcm_cmd));

		i2s_pcm = &config->conf.i2s_pcm;

		/*
		 * CG2900 PG1 chips don't support I2S over the PCM/I2S bus,
		 * and CG2900 PG2 onwards chips don't use this command
		 */
		if (i2s_pcm->protocol != PORT_PROTOCOL_PCM) {
			dev_err(BT_DEV,
				"I2S not supported over the PCM/I2S bus\n");
			err = -EACCES;
			goto error_handling_free_skb;
		}

		pcm_cmd->vp_type = PORT_PROTOCOL_PCM;
		pcm_cmd->port_id = 0x00; /* First/only PCM port */

		HWCONFIG_PCM_SET_MODE(pcm_cmd, mc_pcm_role(i2s_pcm->mode));

		HWCONFIG_PCM_SET_DIR(pcm_cmd, 0, i2s_pcm->slot_0_dir);
		HWCONFIG_PCM_SET_DIR(pcm_cmd, 1, i2s_pcm->slot_1_dir);
		HWCONFIG_PCM_SET_DIR(pcm_cmd, 2, i2s_pcm->slot_2_dir);
		HWCONFIG_PCM_SET_DIR(pcm_cmd, 3, i2s_pcm->slot_3_dir);

		pcm_cmd->bit_clock = i2s_pcm->clk;
		pcm_cmd->frame_len =
			cpu_to_le16(get_fs_duration(i2s_pcm->duration));

		/* Store the new configuration */
		mutex_lock(&info->management_mutex);
		memcpy(&info->i2s_pcm_config, &config->conf.i2s_pcm,
		       sizeof(config->conf.i2s_pcm));
		info->i2s_pcm_config_known = true;
		mutex_unlock(&info->management_mutex);
		break;

	default:
		dev_err(BT_DEV, "Unknown port configuration %d\n",
			config->port);
		err = -EACCES;
		goto error_handling_free_skb;
	};

	cb_info->user = audio_user;
	dev_dbg(BT_DEV, "New resp_state: WAITING\n");
	audio_user->resp_state = WAITING;

	/* Send packet to controller */
	err = pf_data->write(pf_data, skb);
	if (err) {
		dev_err(BT_DEV, "Error %d occurred while transmitting skb\n",
			err);
		goto error_handling_free_skb;
	}

	err = receive_bt_cmd_complete(audio_user,
				      CG2900_BT_VS_SET_HARDWARE_CONFIG,
				      NULL, 0);

	goto finished_unlock_mutex;

error_handling_free_skb:
	kfree_skb(skb);
finished_unlock_mutex:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	audio_user->resp_state = IDLE;
	mutex_unlock(&info->bt_mutex);
	return err;
}

/**
 * set_dai_config_pg2() - Internal implementation of
 * cg2900_audio_set_dai_config for CG2900 PG2 onwards hardware.
 * @audio_user:	Pointer to audio user struct.
 * @config:	Pointer to the configuration to set.
 *
 * Sets the Digital Audio Interface (DAI) configuration for
 * CG2900 PG2 onwards hardware. This is an internal function
 * and basic argument-verification should have been
 * done by the caller.
 *
 * Returns:
 *  0 if there is no error.
 *  -EACCESS if port is not supported.
 *  -ENOMEM if not possible to allocate packet.
 *  -ECOMM if no response was received.
 *  -EIO for other errors.
 */
static int set_dai_config_pg2(struct audio_user *audio_user,
			      struct cg2900_dai_config *config)
{
	int err = 0;
	struct cg2900_dai_conf_i2s *i2s;
	struct cg2900_dai_conf_i2s_pcm *i2s_pcm;

	struct mc_vs_port_cfg_i2s i2s_cfg;
	struct mc_vs_port_cfg_pcm_i2s pcm_cfg;
	struct audio_info *info = audio_user->info;

	dev_dbg(BT_DEV, "set_dai_config_pg2 port %d\n", config->port);

	/*
	 * Use mutex to assure that only ONE command is sent at any time on
	 * each channel.
	 */
	mutex_lock(&info->bt_mutex);

	switch (config->port) {
	case PORT_0_I2S:
		i2s = &config->conf.i2s;

		memset(&i2s_cfg, 0, sizeof(i2s_cfg)); /* just to be safe  */

		/* master/slave */
		PORTCFG_I2S_SET_ROLE(i2s_cfg, mc_i2s_role(i2s->mode));

		PORTCFG_I2S_SET_HALFPERIOD(i2s_cfg, i2s->half_period);
		PORTCFG_I2S_SET_CHANNELS(i2s_cfg,
			mc_i2s_channel_select(i2s->channel_sel));
		PORTCFG_I2S_SET_SRATE(i2s_cfg,
			mc_i2s_sample_rate(i2s->sample_rate));
		switch (i2s->word_width) {
		case WORD_WIDTH_16:
			PORTCFG_I2S_SET_WORDLEN(i2s_cfg, CG2900_MC_I2S_WORD_16);
			break;
		case WORD_WIDTH_32:
			PORTCFG_I2S_SET_WORDLEN(i2s_cfg, CG2900_MC_I2S_WORD_32);
			break;
		}

		/* Store the new configuration */
		mutex_lock(&info->management_mutex);
		memcpy(&(info->i2s_config), &(config->conf.i2s),
		       sizeof(config->conf.i2s));
		info->i2s_config_known = true;
		mutex_unlock(&info->management_mutex);

		/* Send */
		err = send_vs_port_cfg(audio_user, CG2900_MC_PORT_I2S,
				       &i2s_cfg, sizeof(i2s_cfg));
		break;

	case PORT_1_I2S_PCM:
		i2s_pcm = &config->conf.i2s_pcm;

		memset(&pcm_cfg, 0, sizeof(pcm_cfg)); /* just to be safe  */

		/* master/slave */
		PORTCFG_PCM_SET_ROLE(pcm_cfg, mc_pcm_role(i2s_pcm->mode));

		/* set direction for all 4 slots */
		PORTCFG_PCM_SET_DIR(pcm_cfg, 0, i2s_pcm->slot_0_dir);
		PORTCFG_PCM_SET_DIR(pcm_cfg, 1, i2s_pcm->slot_1_dir);
		PORTCFG_PCM_SET_DIR(pcm_cfg, 2, i2s_pcm->slot_2_dir);
		PORTCFG_PCM_SET_DIR(pcm_cfg, 3, i2s_pcm->slot_3_dir);

		/* set used SCO slots, other use cases not supported atm */
		PORTCFG_PCM_SET_SCO_USED(pcm_cfg, 0, i2s_pcm->slot_0_used);
		PORTCFG_PCM_SET_SCO_USED(pcm_cfg, 1, i2s_pcm->slot_1_used);
		PORTCFG_PCM_SET_SCO_USED(pcm_cfg, 2, i2s_pcm->slot_2_used);
		PORTCFG_PCM_SET_SCO_USED(pcm_cfg, 3, i2s_pcm->slot_3_used);

		/* slot starts */
		pcm_cfg.slot_start[0] = i2s_pcm->slot_0_start;
		pcm_cfg.slot_start[1] = i2s_pcm->slot_1_start;
		pcm_cfg.slot_start[2] = i2s_pcm->slot_2_start;
		pcm_cfg.slot_start[3] = i2s_pcm->slot_3_start;

		/* audio/voice sample-rate ratio */
		PORTCFG_PCM_SET_RATIO(pcm_cfg, i2s_pcm->ratio);

		/* PCM or I2S mode */
		PORTCFG_PCM_SET_MODE(pcm_cfg, i2s_pcm->protocol);

		pcm_cfg.frame_len = i2s_pcm->duration;

		PORTCFG_PCM_SET_BITCLK(pcm_cfg, i2s_pcm->clk);
		PORTCFG_PCM_SET_SRATE(pcm_cfg,
			mc_pcm_sample_rate(i2s_pcm->sample_rate));

		/* Store the new configuration */
		mutex_lock(&info->management_mutex);
		memcpy(&(info->i2s_pcm_config), &(config->conf.i2s_pcm),
		       sizeof(config->conf.i2s_pcm));
		info->i2s_pcm_config_known = true;
		mutex_unlock(&info->management_mutex);

		/* Send */
		err = send_vs_port_cfg(audio_user, CG2900_MC_PORT_PCM_I2S,
				       &pcm_cfg, sizeof(pcm_cfg));
		break;

	default:
		dev_err(BT_DEV, "Unknown port configuration %d\n",
			config->port);
		err = -EACCES;
	};

	mutex_unlock(&info->bt_mutex);
	return err;
}

/**
 * struct i2s_fm_stream_config_priv - Helper struct for stream i2s-fm streams.
 * @fm_config:	FM endpoint configuration.
 * @rx:		true for FM-RX, false for FM-TX.
 */
struct i2s_fm_stream_config_priv {
	struct cg2900_endpoint_config_fm	*fm_config;
	bool					rx;

};

/**
 * config_i2s_fm_stream() - Callback for @send_vs_session_config.
 * @info:	Audio info structure.
 * @_priv:	Pointer to a @i2s_fm_stream_config_priv struct.
 * @cfg:	Pointer to stream config block in command packet.
 *
 * Fills in stream configuration for I2S-FM RX/TX.
 */

static void config_i2s_fm_stream(struct audio_info *info, void *_priv,
				 struct session_config_stream *cfg)
{
	struct i2s_fm_stream_config_priv *priv = _priv;
	struct session_config_vport *fm;
	struct session_config_vport *i2s;

	cfg->media_type = CG2900_BT_SESSION_MEDIA_TYPE_AUDIO;

	if (info->i2s_config.channel_sel == CHANNEL_SELECTION_BOTH)
		SESSIONCFG_SET_CHANNELS(cfg, CG2900_BT_MEDIA_CONFIG_STEREO);
	else
		SESSIONCFG_SET_CHANNELS(cfg, CG2900_BT_MEDIA_CONFIG_MONO);

	SESSIONCFG_I2S_SET_SRATE(cfg,
		session_config_sample_rate(priv->fm_config->sample_rate));

	cfg->codec_type = CG2900_CODEC_TYPE_NONE;
	/* codec mode and parameters not used  */

	if (priv->rx) {
		fm  = &cfg->inport;  /* FM is input */
		i2s = &cfg->outport; /* I2S is output */
	} else {
		i2s = &cfg->inport;  /* I2S is input */
		fm  = &cfg->outport; /* FM is output */
	}

	fm->type = CG2900_BT_VP_TYPE_FM;

	i2s->type = CG2900_BT_VP_TYPE_I2S;
	i2s->i2s.index   = CG2900_BT_SESSION_I2S_INDEX_I2S;
	i2s->i2s.channel = info->i2s_config.channel_sel;
}

/**
 * conn_start_i2s_to_fm_rx() - Start an audio stream connecting FM RX to I2S.
 * @audio_user:		Audio user to check for.
 * @stream_handle:	[out] Pointer where to store the stream handle.
 *
 * This function sets up an FM RX to I2S stream.
 * It does this by first setting the output mode and then the configuration of
 * the External Sample Rate Converter.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -ENOMEM upon allocation errors.
 *   -EIO for other errors.
 */
static int conn_start_i2s_to_fm_rx(struct audio_user *audio_user,
				   unsigned int *stream_handle)
{
	int err = 0;
	union cg2900_endpoint_config_union *fm_config;
	struct audio_info *info = audio_user->info;

	dev_dbg(FM_DEV, "conn_start_i2s_to_fm_rx\n");

	fm_config = find_endpoint(ENDPOINT_FM_RX, &info->endpoints);
	if (!fm_config) {
		dev_err(FM_DEV, "FM RX not configured before stream start\n");
		return -EIO;
	}

	if (!(info->i2s_config_known)) {
		dev_err(FM_DEV,
			"I2S DAI not configured before stream start\n");
		return -EIO;
	}

	/*
	 * Use mutex to assure that only ONE command is sent at any
	 * time on each channel.
	 */
	mutex_lock(&info->fm_mutex);
	mutex_lock(&info->bt_mutex);

	/*
	 * Now set the output mode of the BT Sample Rate Converter by
	 * sending HCI_Write command with AUP_BT_SetMode.
	 */
	err = send_fm_write_1_param(audio_user,
				    CG2900_FM_CMD_ID_AUP_BT_SET_MODE,
				    CG2900_FM_CMD_AUP_BT_SET_MODE_PARALLEL);
	if (err)
		goto finished_unlock_mutex;

	/*
	 * Now configure the BT Sample Rate Converter by sending
	 * HCI_Write command with AUP_BT_SetControl.
	 */
	err = send_fm_write_1_param(
		audio_user, CG2900_FM_CMD_ID_AUP_BT_SET_CTRL,
		fm_get_conversion(info, fm_config->fm.sample_rate));
	if (err)
		goto finished_unlock_mutex;

	/* Set up the stream */
	if (info->revision == CG2900_CHIP_REV_PG1) {
		struct i2s_fm_stream_config_priv stream_priv;

		/* Now send HCI_VS_Set_Session_Configuration command */
		stream_priv.fm_config = &fm_config->fm;
		stream_priv.rx = true;
		err = send_vs_session_config(audio_user, config_i2s_fm_stream,
					     &stream_priv);
	} else {
		struct mc_vs_port_cfg_fm fm_cfg;

		memset(&fm_cfg, 0, sizeof(fm_cfg));

		/* Configure port FM RX - PORT 0 is used for BT Sample Rate Converter digital Output*/
		/* Expects 0-3 - same as user API - so no conversion needed */
		PORTCFG_FM_SET_SRATE(fm_cfg, (u8)fm_config->fm.sample_rate);

		err = send_vs_port_cfg(audio_user, CG2900_MC_PORT_FM_RX_0,
				       &fm_cfg, sizeof(fm_cfg));
		if (err)
			goto finished_unlock_mutex;

		/* CreateStream */
		err = send_vs_create_stream(audio_user,
					    CG2900_MC_PORT_FM_RX_0,
					    CG2900_MC_PORT_I2S,
					    0); /* chip doesn't care */
	}

	if (err < 0)
		goto finished_unlock_mutex;

	/* Store the stream handle (used for start and stop stream) */
	*stream_handle = (u8)err;
	dev_dbg(FM_DEV, "stream_handle set to %d\n", *stream_handle);

	/* Now start the stream */
	if (info->revision == CG2900_CHIP_REV_PG1)
		err = send_vs_session_ctrl(audio_user, *stream_handle,
					   CG2900_BT_SESSION_START);
	else
		err = send_vs_stream_ctrl(audio_user, *stream_handle,
					  CG2900_MC_STREAM_START);
	/*Let's delete a stream.*/
	if (err < 0) {
		dev_dbg(BT_DEV, "Could not start a stream.");
		(void)send_vs_delete_stream(audio_user, *stream_handle);
	}

finished_unlock_mutex:
	dev_dbg(FM_DEV, "New resp_state: IDLE\n");
	audio_user->resp_state = IDLE;
	mutex_unlock(&info->bt_mutex);
	mutex_unlock(&info->fm_mutex);
	return err;
}

/**
 * conn_start_i2s_to_fm_tx() - Start an audio stream connecting FM TX to I2S.
 * @audio_user:		Audio user to check for.
 * @stream_handle:	[out] Pointer where to store the stream handle.
 *
 * This function sets up an I2S to FM TX stream.
 * It does this by first setting the Audio Input source and then setting the
 * configuration and input source of BT sample rate converter.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -ENOMEM upon allocation errors.
 *   -EIO for other errors.
 */
static int conn_start_i2s_to_fm_tx(struct audio_user *audio_user,
				   unsigned int *stream_handle)
{
	int err = 0;
	union cg2900_endpoint_config_union *fm_config;
	struct audio_info *info = audio_user->info;

	dev_dbg(FM_DEV, "conn_start_i2s_to_fm_tx\n");

	fm_config = find_endpoint(ENDPOINT_FM_TX, &info->endpoints);
	if (!fm_config) {
		dev_err(FM_DEV, "FM TX not configured before stream start\n");
		return -EIO;
	}

	if (!(info->i2s_config_known)) {
		dev_err(FM_DEV,
			"I2S DAI not configured before stream start\n");
		return -EIO;
	}

	/*
	 * Use mutex to assure that only ONE command is sent at any time
	 * on each channel.
	 */
	mutex_lock(&info->fm_mutex);
	mutex_lock(&info->bt_mutex);

	/*
	 * Select Audio Input Source by sending HCI_Write command with
	 * AIP_SetMode.
	 */
	if (info->revision == CG2900_CHIP_REV_PG1 ||
			info->revision == CG2900_CHIP_REV_PG2) {
		dev_dbg(FM_DEV, "FM: AIP_SetMode\n");
		err = send_fm_write_1_param(audio_user,
				CG2900_FM_CMD_ID_AIP_SET_MODE,
				CG2900_FM_CMD_AIP_SET_MODE_INPUT_DIG);
		if (err)
			goto finished_unlock_mutex;
	}
	/*
	 * Now configure the BT sample rate converter by sending HCI_Write
	 * command with AIP_BT_SetControl.
	 */
	dev_dbg(FM_DEV, "FM: AIP_BT_SetControl\n");
	err = send_fm_write_1_param(
		audio_user, CG2900_FM_CMD_ID_AIP_BT_SET_CTRL,
		fm_get_conversion(info, fm_config->fm.sample_rate));
	if (err)
		goto finished_unlock_mutex;

	/*
	 * Now set input of the BT sample rate converter by sending HCI_Write
	 * command with AIP_BT_SetMode.
	 */
	dev_dbg(FM_DEV, "FM: AIP_BT_SetMode\n");
	err = send_fm_write_1_param(audio_user,
				    CG2900_FM_CMD_ID_AIP_BT_SET_MODE,
				    CG2900_FM_CMD_AIP_BT_SET_MODE_INPUT_PAR);
	if (err)
		goto finished_unlock_mutex;

	/* Set up the stream */
	if (info->revision == CG2900_CHIP_REV_PG1) {
		struct i2s_fm_stream_config_priv stream_priv;

		/* Now send HCI_VS_Set_Session_Configuration command */
		stream_priv.fm_config = &fm_config->fm;
		stream_priv.rx = false;
		err = send_vs_session_config(audio_user, config_i2s_fm_stream,
					     &stream_priv);
	} else {
		struct mc_vs_port_cfg_fm fm_cfg;

		memset(&fm_cfg, 0, sizeof(fm_cfg));

		/* Configure port FM TX */
		/* Expects 0-3 - same as user API - so no conversion needed */
		PORTCFG_FM_SET_SRATE(fm_cfg, (u8)fm_config->fm.sample_rate);

		err = send_vs_port_cfg(audio_user, CG2900_MC_PORT_FM_TX,
				       &fm_cfg, sizeof(fm_cfg));
		if (err)
			goto finished_unlock_mutex;

		/* CreateStream */
		err = send_vs_create_stream(audio_user,
					    CG2900_MC_PORT_I2S,
					    CG2900_MC_PORT_FM_TX,
					    0); /* chip doesn't care */
	}

	if (err < 0)
		goto finished_unlock_mutex;

	/* Store the stream handle (used for start and stop stream) */
	*stream_handle = (u8)err;
	dev_dbg(FM_DEV, "stream_handle set to %d\n", *stream_handle);

	/* Now start the stream */
	if (info->revision == CG2900_CHIP_REV_PG1)
		err = send_vs_session_ctrl(audio_user, *stream_handle,
					   CG2900_BT_SESSION_START);
	else
		err = send_vs_stream_ctrl(audio_user, *stream_handle,
					  CG2900_MC_STREAM_START);
	/* Let's delete and release stream.*/
	if (err < 0) {
		dev_dbg(BT_DEV, "Could not start a stream.");
		(void)send_vs_delete_stream(audio_user, *stream_handle);
	}

finished_unlock_mutex:
	dev_dbg(FM_DEV, "New resp_state: IDLE\n");
	audio_user->resp_state = IDLE;
	mutex_unlock(&info->bt_mutex);
	mutex_unlock(&info->fm_mutex);
	return err;
}

/**
 * config_pcm_sco_stream() - Callback for @send_vs_session_config.
 * @info:	Audio info structure.
 * @_priv:	Pointer to a @cg2900_endpoint_config_sco_in_out struct.
 * @cfg:	Pointer to stream config block in command packet.
 *
 * Fills in stream configuration for PCM-SCO.
 */
static void config_pcm_sco_stream(struct audio_info *info, void *_priv,
				  struct session_config_stream *cfg)
{
	struct cg2900_endpoint_config_sco_in_out *sco_ep = _priv;

	cfg->media_type = CG2900_BT_SESSION_MEDIA_TYPE_AUDIO;

	SESSIONCFG_SET_CHANNELS(cfg, CG2900_BT_MEDIA_CONFIG_MONO);
	SESSIONCFG_I2S_SET_SRATE(cfg,
		session_config_sample_rate(sco_ep->sample_rate));

	cfg->codec_type = CG2900_CODEC_TYPE_NONE;
	/* codec mode and parameters not used  */

	cfg->inport.type = CG2900_BT_VP_TYPE_BT_SCO;
	cfg->inport.sco.acl_handle = cpu_to_le16(DEFAULT_ACL_HANDLE);

	cfg->outport.type = CG2900_BT_VP_TYPE_PCM;
	cfg->outport.pcm.index = CG2900_BT_SESSION_PCM_INDEX_PCM_I2S;

	SESSIONCFG_PCM_SET_USED(cfg->outport, 0,
				info->i2s_pcm_config.slot_0_used);
	SESSIONCFG_PCM_SET_USED(cfg->outport, 1,
				info->i2s_pcm_config.slot_1_used);
	SESSIONCFG_PCM_SET_USED(cfg->outport, 2,
				info->i2s_pcm_config.slot_2_used);
	SESSIONCFG_PCM_SET_USED(cfg->outport, 3,
				info->i2s_pcm_config.slot_3_used);

	cfg->outport.pcm.slot_start[0] =
		info->i2s_pcm_config.slot_0_start;
	cfg->outport.pcm.slot_start[1] =
		info->i2s_pcm_config.slot_1_start;
	cfg->outport.pcm.slot_start[2] =
		info->i2s_pcm_config.slot_2_start;
	cfg->outport.pcm.slot_start[3] =
		info->i2s_pcm_config.slot_3_start;
}

/**
 * conn_start_pcm_to_sco() - Start an audio stream connecting Bluetooth (e)SCO to PCM_I2S.
 * @audio_user:		Audio user to check for.
 * @stream_handle:	[out] Pointer where to store the stream handle.
 *
 * This function sets up a BT to_from PCM_I2S stream. It does this by
 * first setting the Session configuration and then starting the Audio
 * Stream.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -ENOMEM upon allocation errors.
 *   Errors from @cg2900_write
 *   -EIO for other errors.
 */
static int conn_start_pcm_to_sco(struct audio_user *audio_user,
				 unsigned int *stream_handle)
{
	int err = 0;
	union cg2900_endpoint_config_union *bt_config;
	struct audio_info *info = audio_user->info;

	dev_dbg(BT_DEV, "conn_start_pcm_to_sco\n");

	bt_config = find_endpoint(ENDPOINT_BT_SCO_INOUT, &info->endpoints);
	if (!bt_config) {
		dev_err(BT_DEV, "BT not configured before stream start\n");
		return -EIO;
	}

	if (!(info->i2s_pcm_config_known)) {
		dev_err(BT_DEV,
			"I2S_PCM DAI not configured before stream start\n");
		return -EIO;
	}

	/*
	 * Use mutex to assure that only ONE command is sent at any time on each
	 * channel.
	 */
	mutex_lock(&info->bt_mutex);

	/* Set up the stream */
	if (info->revision == CG2900_CHIP_REV_PG1) {
		err = send_vs_session_config(audio_user, config_pcm_sco_stream,
					     &bt_config->sco);
	} else {
		struct mc_vs_port_cfg_sco sco_cfg;

		/* zero codec params etc */
		memset(&sco_cfg, 0, sizeof(sco_cfg));
		sco_cfg.acl_id = DEFAULT_ACL_HANDLE;
		PORTCFG_SCO_SET_CODEC(sco_cfg, CG2900_CODEC_TYPE_NONE);

		err = send_vs_port_cfg(audio_user, CG2900_MC_PORT_BT_SCO,
				       &sco_cfg, sizeof(sco_cfg));
		if (err)
			goto finished_unlock_mutex;

		/* CreateStream */
		err = send_vs_create_stream(audio_user,
					    CG2900_MC_PORT_PCM_I2S,
					    CG2900_MC_PORT_BT_SCO,
					    0); /* chip doesn't care */
	}

	if (err < 0)
		goto finished_unlock_mutex;

	/* Store the stream handle (used for start and stop stream) */
	*stream_handle = (u8)err;
	dev_dbg(BT_DEV, "stream_handle set to %d\n", *stream_handle);

	/* Now start the stream */
	if (info->revision == CG2900_CHIP_REV_PG1)
		err = send_vs_session_ctrl(audio_user, *stream_handle,
					   CG2900_BT_SESSION_START);
	else
		err = send_vs_stream_ctrl(audio_user, *stream_handle,
					  CG2900_MC_STREAM_START);
	/* Let's delete and release stream.*/
	if (err < 0) {
		dev_dbg(BT_DEV, "Could not start a stream.");
		(void)send_vs_delete_stream(audio_user, *stream_handle);
	}

finished_unlock_mutex:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	audio_user->resp_state = IDLE;
	mutex_unlock(&info->bt_mutex);
	return err;
}

/**
 * conn_stop_stream() - Stops an audio stream defined by @stream_handle.
 * @audio_user:		Audio user to check for.
 * @stream_handle:	Handle of the audio stream.
 *
 * This function is used to stop an audio stream defined by a stream
 * handle. It does this by first stopping the stream and then
 * resetting the session/stream.
 *
 * Returns:
 *   0 if there is no error.
 *   -ECOMM if no response was received.
 *   -ENOMEM upon allocation errors.
 *   Errors from @cg2900_write.
 *   -EIO for other errors.
 */
static int conn_stop_stream(struct audio_user *audio_user,
			    unsigned int stream_handle)
{
	int err = 0;
	struct audio_info *info = audio_user->info;

	dev_dbg(BT_DEV, "conn_stop_stream handle %d\n", stream_handle);

	/*
	 * Use mutex to assure that only ONE command is sent at any
	 * time on each channel.
	 */
	mutex_lock(&info->bt_mutex);

	/* Now stop the stream */
	if (info->revision == CG2900_CHIP_REV_PG1)
		err = send_vs_session_ctrl(audio_user, stream_handle,
					   CG2900_BT_SESSION_STOP);
	else
		err = send_vs_stream_ctrl(audio_user, stream_handle,
					  CG2900_MC_STREAM_STOP);
	if (err)
		goto finished_unlock_mutex;

	err = send_vs_delete_stream(audio_user, stream_handle);

finished_unlock_mutex:
	dev_dbg(BT_DEV, "New resp_state: IDLE\n");
	audio_user->resp_state = IDLE;
	mutex_unlock(&info->bt_mutex);
	return err;
}

/**
 * cg2900_audio_get_devices() - Returns connected CG2900 Audio devices.
 * @devices:	Array of CG2900 Audio devices.
 * @size:	Max number of devices in array.
 *
 * Returns:
 *   0 if no devices exist.
 *   > 0 is the number of devices inserted in the list.
 *   -EINVAL upon bad input parameter.
 */
int cg2900_audio_get_devices(struct device *devices[], __u8 size)
{
	struct list_head *cursor;
	struct audio_info *tmp;
	int i = 0;

	if (!size) {
		pr_err("No space to insert devices into list\n");
		return 0;
	}

	if (!devices) {
		pr_err("NULL submitted as devices array\n");
		return -EINVAL;
	}

	/*
	 * Go through and store the devices. If NULL is supplied for dev
	 * just return first device found.
	 */
	list_for_each(cursor, &cg2900_audio_devices) {
		tmp = list_entry(cursor, struct audio_info, list);
		devices[i] = tmp->parent;
		i++;
		if (i == size)
			break;
	}
	return i;
}
EXPORT_SYMBOL_GPL(cg2900_audio_get_devices);

/**
 * cg2900_audio_open() - Opens a session to the ST-Ericsson CG2900 Audio control interface.
 * @session:	[out] Address where to store the session identifier.
 *		Allocated by caller, must not be NULL.
 * @parent:	Parent device representing the CG2900 controller connected.
 *		If NULL is supplied the first available device is used.
 *
 * Returns:
 *   0 if there is no error.
 *   -EACCES if no info structure can be found.
 *   -EINVAL upon bad input parameter.
 *   -ENOMEM upon allocation failure.
 *   -EMFILE if no more user session could be opened.
 *   -EIO upon failure to register to CG2900.
 *   Error codes from get_info.
 */
int cg2900_audio_open(unsigned int *session, struct device *parent)
{
	int err = 0;
	int i;
	struct audio_info *info;
	struct cg2900_user_data *pf_data_bt;
	struct cg2900_user_data *pf_data_fm;

	pr_debug("cg2900_audio_open");

	info = get_info(parent);
	if (!info) {
		pr_err("No audio info exist");
		return -EACCES;
	} else if (IS_ERR(info))
		return PTR_ERR(info);

	if (!session) {
		pr_err("NULL supplied as session");
		return -EINVAL;
	}

	mutex_lock(&info->management_mutex);

	*session = 0;

	/*
	 * First find a free session to use and allocate the session structure.
	 */
	for (i = FIRST_USER;
	     i < MAX_NBR_OF_USERS && cg2900_audio_sessions[i];
	     i++)
		; /* Just loop until found or end reached */

	if (i >= MAX_NBR_OF_USERS) {
		pr_err("Couldn't find free user");
		err = -EMFILE;
		goto finished;
	}

	cg2900_audio_sessions[i] =
		kzalloc(sizeof(*(cg2900_audio_sessions[0])), GFP_KERNEL);
	if (!cg2900_audio_sessions[i]) {
		pr_err("Could not allocate user");
		err = -ENOMEM;
		goto finished;
	}
	pr_debug("Found free session %d", i);
	*session = i;
	info->nbr_of_users_active++;

	cg2900_audio_sessions[*session]->resp_state = IDLE;
	cg2900_audio_sessions[*session]->session = *session;
	cg2900_audio_sessions[*session]->info = info;

	pf_data_bt = dev_get_platdata(info->dev_bt);
	pf_data_fm = dev_get_platdata(info->dev_fm);

	if (info->nbr_of_users_active == 1) {
		struct cg2900_rev_data rev_data;

		/*
		 * First user so register to CG2900 Core.
		 * First the BT audio device.
		 */
		err = pf_data_bt->open(pf_data_bt);
		if (err) {
			dev_err(BT_DEV, "Failed to open BT audio channel\n");
			goto error_handling;
		}

		/* Then the FM audio device */
		err = pf_data_fm->open(pf_data_fm);
		if (err) {
			dev_err(FM_DEV, "Failed to open FM audio channel\n");
			goto error_handling;
		}

		/* Read chip revision data */
		if (!pf_data_bt->get_local_revision(pf_data_bt, &rev_data)) {
			pr_err("Couldn't retrieve revision data");
			err = -EIO;
			goto error_handling;
		}

		/* Decode revision data */
		switch (rev_data.revision) {
		case CG2900_PG1_REV:
		case CG2900_PG1_SPECIAL_REV:
			info->revision = CG2900_CHIP_REV_PG1;
			break;

		case CG2900_PG2_REV:
			info->revision = CG2900_CHIP_REV_PG2;
			break;

		case CG2905_PG1_05_REV:
			info->revision = CG2905_CHIP_REV_PG1_05;
			break;

		case CG2905_PG2_REV:
			info->revision = CG2905_CHIP_REV_PG2;
			break;

		case CG2910_PG1_REV:
			info->revision = CG2910_CHIP_REV_PG1;
			break;

		case CG2910_PG1_05_REV:
			info->revision = CG2910_CHIP_REV_PG1_05;
			break;

		case CG2910_PG2_REV:
			info->revision = CG2910_CHIP_REV_PG2;
			break;

		default:
			pr_err("Chip rev 0x%04X sub 0x%04X not supported",
			       rev_data.revision, rev_data.sub_version);
			err = -EIO;
			goto error_handling;
		}

		info->state = OPENED;
	}

	pr_info("Session %d opened", *session);

	goto finished;

error_handling:
	if (pf_data_fm->opened)
		pf_data_fm->close(pf_data_fm);
	if (pf_data_bt->opened)
		pf_data_bt->close(pf_data_bt);
	info->nbr_of_users_active--;
	kfree(cg2900_audio_sessions[*session]);
	cg2900_audio_sessions[*session] = NULL;
finished:
	mutex_unlock(&info->management_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cg2900_audio_open);

/**
 * cg2900_audio_close() - Closes an opened session to the ST-Ericsson CG2900 audio control interface.
 * @session:	[in_out] Pointer to session identifier to close.
 *		Will be 0 after this call.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL upon bad input parameter.
 *   -EIO if driver has not been opened.
 *   -EACCES if session has not opened.
 */
int cg2900_audio_close(unsigned int *session)
{
	int err = 0;
	struct audio_user *audio_user;
	struct audio_info *info;
	struct cg2900_user_data *pf_data_bt;
	struct cg2900_user_data *pf_data_fm;

	pr_debug("cg2900_audio_close");

	if (!session) {
		pr_err("NULL pointer supplied");
		return -EINVAL;
	}

	audio_user = get_session_user(*session);
	if (!audio_user) {
		pr_err("Invalid session ID");
		return -EINVAL;
	}

	info = audio_user->info;

	if (info->state != OPENED) {
		dev_err(BT_DEV, "Audio driver not open\n");
		return -EIO;
	}

	mutex_lock(&info->management_mutex);

	pf_data_bt = dev_get_platdata(info->dev_bt);
	pf_data_fm = dev_get_platdata(info->dev_fm);

	if (!cg2900_audio_sessions[*session]) {
		dev_err(BT_DEV, "Session %d not opened\n", *session);
		err = -EACCES;
		goto err_unlock_mutex;
	}

	kfree(cg2900_audio_sessions[*session]);
	cg2900_audio_sessions[*session] = NULL;

	info->nbr_of_users_active--;
	if (info->nbr_of_users_active == 0) {
		/* No more sessions open. Close channels */
		pf_data_fm->close(pf_data_fm);
		pf_data_bt->close(pf_data_bt);
		info->state = CLOSED;
	}

	dev_info(BT_DEV, "Session %d closed\n", *session);

	*session = 0;

err_unlock_mutex:
	mutex_unlock(&info->management_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cg2900_audio_close);

/**
 * cg2900_audio_set_dai_config() -  Sets the Digital Audio Interface configuration.
 * @session:	Session identifier this call is related to.
 * @config:	Pointer to the configuration to set.
 *		Allocated by caller, must not be NULL.
 *
 * Sets the Digital Audio Interface (DAI) configuration. The DAI is the external
 * interface between the combo chip and the platform.
 * For example the PCM or I2S interface.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL upon bad input parameter.
 *   -EIO if driver has not been opened.
 *   -ENOMEM upon allocation failure.
 *   -EACCES if trying to set unsupported configuration.
 *   Errors from @receive_bt_cmd_complete.
 */
int cg2900_audio_set_dai_config(unsigned int session,
				struct cg2900_dai_config *config)
{
	int err = 0;
	struct audio_user *audio_user;
	struct audio_info *info;

	pr_debug("cg2900_audio_set_dai_config session %d", session);

	audio_user = get_session_user(session);
	if (!audio_user)
		return -EINVAL;

	info = audio_user->info;

	if (info->state != OPENED) {
		dev_err(BT_DEV, "Audio driver not open\n");
		return -EIO;
	}

	/* Different command is used for CG2900 PG1 */
	if (info->revision == CG2900_CHIP_REV_PG1)
		err = set_dai_config_pg1(audio_user, config);
	else
		err = set_dai_config_pg2(audio_user, config);

	return err;
}
EXPORT_SYMBOL_GPL(cg2900_audio_set_dai_config);

/**
 * cg2900_audio_get_dai_config() - Gets the current Digital Audio Interface configuration.
 * @session:	Session identifier this call is related to.
 * @config:	[out] Pointer to the configuration to get.
 *		Allocated by caller, must not be NULL.
 *
 * Gets the current Digital Audio Interface configuration. Currently this method
 * can only be called after some one has called
 * cg2900_audio_set_dai_config(), there is today no way of getting
 * the static settings file parameters from this method.
 * Note that the @port parameter within @config must be set when calling this
 * function so that the ST-Ericsson CG2900 Audio driver will know which
 * configuration to return.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL upon bad input parameter.
 *   -EIO if driver has not been opened or configuration has not been set.
 */
int cg2900_audio_get_dai_config(unsigned int session,
				struct cg2900_dai_config *config)
{
	int err = 0;
	struct audio_user *audio_user;
	struct audio_info *info;

	pr_debug("cg2900_audio_get_dai_config session %d", session);

	if (!config) {
		pr_err("NULL supplied as config structure");
		return -EINVAL;
	}

	audio_user = get_session_user(session);
	if (!audio_user)
		return -EINVAL;

	info = audio_user->info;

	if (info->state != OPENED) {
		dev_err(BT_DEV, "Audio driver not open\n");
		return -EIO;
	}

	/*
	 * Return DAI configuration based on the received port.
	 * If port has not been configured return error.
	 */
	switch (config->port) {
	case PORT_0_I2S:
		mutex_lock(&info->management_mutex);
		if (info->i2s_config_known)
			memcpy(&config->conf.i2s,
			       &info->i2s_config,
			       sizeof(config->conf.i2s));
		else
			err = -EIO;
		mutex_unlock(&info->management_mutex);
		break;

	case PORT_1_I2S_PCM:
		mutex_lock(&info->management_mutex);
		if (info->i2s_pcm_config_known)
			memcpy(&config->conf.i2s_pcm,
			       &info->i2s_pcm_config,
			       sizeof(config->conf.i2s_pcm));
		else
			err = -EIO;
		mutex_unlock(&info->management_mutex);
		break;

	default:
		dev_err(BT_DEV, "Unknown port configuration %d\n",
			config->port);
		err = -EIO;
		break;
	};

	return err;
}
EXPORT_SYMBOL_GPL(cg2900_audio_get_dai_config);

/**
 * cg2900_audio_config_endpoint() - Configures one endpoint in the combo chip's audio system.
 * @session:	Session identifier this call is related to.
 * @config:	Pointer to the endpoint's configuration structure.
 *
 * Configures one endpoint in the combo chip's audio system.
 * Supported @endpoint_id values are:
 *  * ENDPOINT_BT_SCO_INOUT
 *  * ENDPOINT_BT_A2DP_SRC
 *  * ENDPOINT_FM_RX
 *  * ENDPOINT_FM_TX
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL upon bad input parameter.
 *   -EIO if driver has not been opened.
 *   -EACCES if supplied cg2900_dai_config struct contains not supported
 *   endpoint_id.
 */
int cg2900_audio_config_endpoint(unsigned int session,
				 struct cg2900_endpoint_config *config)
{
	struct audio_user *audio_user;
	struct audio_info *info;

	pr_debug("cg2900_audio_config_endpoint\n");

	if (!config) {
		pr_err("NULL supplied as configuration structure");
		return -EINVAL;
	}

	audio_user = get_session_user(session);
	if (!audio_user)
		return -EINVAL;

	info = audio_user->info;

	if (info->state != OPENED) {
		dev_err(BT_DEV, "Audio driver not open\n");
		return -EIO;
	}

	switch (config->endpoint_id) {
	case ENDPOINT_BT_SCO_INOUT:
	case ENDPOINT_BT_A2DP_SRC:
	case ENDPOINT_FM_RX:
	case ENDPOINT_FM_TX:
		add_endpoint(config, &info->endpoints);
		break;

	case ENDPOINT_PORT_0_I2S:
	case ENDPOINT_PORT_1_I2S_PCM:
	case ENDPOINT_SLIMBUS_VOICE:
	case ENDPOINT_SLIMBUS_AUDIO:
	case ENDPOINT_BT_A2DP_SNK:
	case ENDPOINT_ANALOG_OUT:
	case ENDPOINT_DSP_AUDIO_IN:
	case ENDPOINT_DSP_AUDIO_OUT:
	case ENDPOINT_DSP_VOICE_IN:
	case ENDPOINT_DSP_VOICE_OUT:
	case ENDPOINT_DSP_TONE_IN:
	case ENDPOINT_BURST_BUFFER_IN:
	case ENDPOINT_BURST_BUFFER_OUT:
	case ENDPOINT_MUSIC_DECODER:
	case ENDPOINT_HCI_AUDIO_IN:
	default:
		dev_err(BT_DEV, "Unsupported endpoint_id %d\n",
			config->endpoint_id);
		return -EACCES;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cg2900_audio_config_endpoint);

static bool is_dai_port(enum cg2900_audio_endpoint_id ep)
{
	/* These are the only supported ones */
	return (ep == ENDPOINT_PORT_0_I2S) || (ep == ENDPOINT_PORT_1_I2S_PCM);
}

/**
 * cg2900_audio_start_stream() - Connects two endpoints and starts the audio stream.
 * @session:		Session identifier this call is related to.
 * @ep_1:		One of the endpoints, no relation to direction or role.
 * @ep_2:		The other endpoint, no relation to direction or role.
 * @stream_handle:	Pointer where to store the stream handle.
 *			Allocated by caller, must not be NULL.
 *
 * Connects two endpoints and starts the audio stream.
 * Note that the endpoints need to be configured before the stream is started;
 * DAI endpoints, such as ENDPOINT_PORT_0_I2S, are
 * configured through @cg2900_audio_set_dai_config() while other
 * endpoints are configured through @cg2900_audio_config_endpoint().
 *
 * Supported @endpoint_id values are:
 *  * ENDPOINT_PORT_0_I2S
 *  * ENDPOINT_PORT_1_I2S_PCM
 *  * ENDPOINT_BT_SCO_INOUT
 *  * ENDPOINT_FM_RX
 *  * ENDPOINT_FM_TX
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL upon bad input parameter or unsupported configuration.
 *   -EIO if driver has not been opened.
 *   Errors from @conn_start_i2s_to_fm_rx, @conn_start_i2s_to_fm_tx, and
 *   @conn_start_pcm_to_sco.
 */
int cg2900_audio_start_stream(unsigned int session,
			      enum cg2900_audio_endpoint_id ep_1,
			      enum cg2900_audio_endpoint_id ep_2,
			      unsigned int *stream_handle)
{
	int err;
	struct audio_user *audio_user;
	struct audio_info *info;

	pr_debug("cg2900_audio_start_stream session %d ep_1 %d ep_2 %d",
		 session, ep_1, ep_2);

	audio_user = get_session_user(session);
	if (!audio_user)
		return -EINVAL;

	info = audio_user->info;

	if (info->state != OPENED) {
		dev_err(BT_DEV, "Audio driver not open\n");
		return -EIO;
	}

	/* Put digital interface in ep_1 to simplify comparison below */
	if (!is_dai_port(ep_1)) {
		/* Swap endpoints */
		enum cg2900_audio_endpoint_id t = ep_1;
		ep_1 = ep_2;
		ep_2 = t;
	}

	if (ep_1 == ENDPOINT_PORT_1_I2S_PCM && ep_2 == ENDPOINT_BT_SCO_INOUT) {
		err = conn_start_pcm_to_sco(audio_user, stream_handle);
	} else if (ep_1 == ENDPOINT_PORT_0_I2S && ep_2 == ENDPOINT_FM_RX) {
		err = conn_start_i2s_to_fm_rx(audio_user, stream_handle);
	} else if (ep_1 == ENDPOINT_PORT_0_I2S && ep_2 == ENDPOINT_FM_TX) {
		err = conn_start_i2s_to_fm_tx(audio_user, stream_handle);
	} else {
		dev_err(BT_DEV, "Endpoint config not handled: ep1: %d, "
			"ep2: %d\n", ep_1, ep_2);
		err = -EINVAL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(cg2900_audio_start_stream);

/**
 * cg2900_audio_stop_stream() - Stops a stream and disconnects the endpoints.
 * @session:		Session identifier this call is related to.
 * @stream_handle:	Handle to the stream to stop.
 *
 * Returns:
 *   0 if there is no error.
 *   -EINVAL upon bad input parameter.
 *   -EIO if driver has not been opened.
 */
int cg2900_audio_stop_stream(unsigned int session, unsigned int stream_handle)
{
	struct audio_user *audio_user;
	struct audio_info *info;

	pr_debug("cg2900_audio_stop_stream handle %d", stream_handle);

	audio_user = get_session_user(session);
	if (!audio_user)
		return -EINVAL;

	info = audio_user->info;

	if (info->state != OPENED) {
		dev_err(BT_DEV, "Audio driver not open\n");
		return -EIO;
	}

	return conn_stop_stream(audio_user, stream_handle);
}
EXPORT_SYMBOL_GPL(cg2900_audio_stop_stream);

/**
 * audio_dev_open() - Open char device.
 * @inode:	Device driver information.
 * @filp:	Pointer to the file struct.
 *
 * Returns:
 *   0 if there is no error.
 *   -ENOMEM if allocation failed.
 *   Errors from @cg2900_audio_open.
 */
static int audio_dev_open(struct inode *inode, struct file *filp)
{
	int err;
	struct char_dev_info *char_dev_info;
	int minor;
	struct audio_info *info = NULL;
	struct audio_info *tmp;
	struct list_head *cursor;

	pr_debug("audio_dev_open");

	minor = iminor(inode);

	/* Find the info struct for this file */
	list_for_each(cursor, &cg2900_audio_devices) {
		tmp = list_entry(cursor, struct audio_info, list);
		if (tmp->misc_dev.minor == minor) {
			info = tmp;
			break;
		}
	}
	if (!info) {
		pr_err("Could not identify device in inode");
		return -EINVAL;
	}

	/*
	 * Allocate the char dev info structure. It will be stored inside
	 * the file pointer and supplied when file_ops are called.
	 * It's free'd in audio_dev_release.
	 */
	char_dev_info = kzalloc(sizeof(*char_dev_info), GFP_KERNEL);
	if (!char_dev_info) {
		dev_err(BT_DEV, "Couldn't allocate char_dev_info\n");
		return -ENOMEM;
	}
	filp->private_data = char_dev_info;
	char_dev_info->info = info;
	info->filp = filp;

	mutex_init(&char_dev_info->management_mutex);
	mutex_init(&char_dev_info->rw_mutex);
	skb_queue_head_init(&char_dev_info->rx_queue);

	mutex_lock(&char_dev_info->management_mutex);
	err = cg2900_audio_open(&char_dev_info->session, info->dev_bt->parent);
	mutex_unlock(&char_dev_info->management_mutex);
	if (err) {
		dev_err(BT_DEV, "Failed to open CG2900 Audio driver (%d)\n",
			err);
		goto error_handling_free_mem;
	}

	return 0;

error_handling_free_mem:
	kfree(char_dev_info);
	filp->private_data = NULL;
	return err;
}

/**
 * audio_dev_release() - Release char device.
 * @inode:	Device driver information.
 * @filp:	Pointer to the file struct.
 *
 * Returns:
 *   0 if there is no error.
 *   -EBADF if NULL pointer was supplied in private data.
 *   Errors from @cg2900_audio_close.
 */
static int audio_dev_release(struct inode *inode, struct file *filp)
{
	int err = 0;
	struct char_dev_info *dev = filp->private_data;
	struct audio_info *info;

	if (!dev) {
		pr_err("audio_dev_release: Transport closed");
		return -EBADF;
	}

	info = dev->info;

	dev_dbg(BT_DEV, "audio_dev_release\n");

	mutex_lock(&dev->management_mutex);
	err = cg2900_audio_close(&dev->session);
	if (err)
		/*
		 * Just print the error. Still free the char_dev_info since we
		 * don't know the filp structure is valid after this call
		 */
		dev_err(BT_DEV, "Error %d when closing CG2900 audio driver\n",
			err);

	mutex_unlock(&dev->management_mutex);

	kfree(dev);
	filp->private_data = NULL;
	info->filp = NULL;

	return err;
}

/**
 * audio_dev_read() - Return information to the user from last @write call.
 * @filp:	Pointer to the file struct.
 * @buf:	Received buffer.
 * @count:	Size of buffer.
 * @f_pos:	Position in buffer.
 *
 * The audio_dev_read() function returns information from
 * the last @write call to same char device.
 * The data is in the following format:
 *   * OpCode of command for this data
 *   * Data content (Length of data is determined by the command OpCode, i.e.
 *     fixed for each command)
 *
 * Returns:
 *   Bytes successfully read (could be 0).
 *   -EBADF if NULL pointer was supplied in private data.
 *   -EFAULT if copy_to_user fails.
 *   -ENOMEM upon allocation failure.
 */
static ssize_t audio_dev_read(struct file *filp, char __user *buf, size_t count,
			      loff_t *f_pos)
{
	struct char_dev_info *dev = filp->private_data;
	struct audio_info *info;
	unsigned int bytes_to_copy;
	int err = 0;
	struct sk_buff *skb;

	if (!dev) {
		pr_err("audio_dev_read: Transport closed");
		return -EBADF;
	}

	info = dev->info;

	dev_dbg(BT_DEV, "audio_dev_read count %d\n", count);

	mutex_lock(&dev->rw_mutex);

	skb = skb_dequeue(&dev->rx_queue);
	if (!skb) {
		/* No data to read */
		bytes_to_copy = 0;
		goto finished;
	}

	bytes_to_copy = min(count, (unsigned int)(skb->len));

	err = copy_to_user(buf, skb->data, bytes_to_copy);
	if (err) {
		dev_err(BT_DEV, "copy_to_user error %d\n", err);
		skb_queue_head(&dev->rx_queue, skb);
		err = -EFAULT;
		goto error_handling;
	}

	skb_pull(skb, bytes_to_copy);

	if (skb->len > 0)
		skb_queue_head(&dev->rx_queue, skb);
	else
		kfree_skb(skb);

	goto finished;

error_handling:
	mutex_unlock(&dev->rw_mutex);
	return (ssize_t)err;
finished:
	mutex_unlock(&dev->rw_mutex);
	return bytes_to_copy;
}

/**
 * audio_dev_write() - Call CG2900 Audio API function.
 * @filp:	Pointer to the file struct.
 * @buf:	Write buffer.
 * @count:	Size of the buffer write.
 * @f_pos:	Position of buffer.
 *
 * audio_dev_write() function executes supplied data and
 * interprets it as if it was a function call to the CG2900 Audio API.
 * The data is according to:
 *   * OpCode (4 bytes, see API).
 *   * Data according to OpCode (see API). No padding between parameters.
 *
 * Returns:
 *   Bytes successfully written (could be 0). Equals input @count if successful.
 *   -EBADF if NULL pointer was supplied in private data.
 *   -EFAULT if copy_from_user fails.
 *   Error codes from all CG2900 Audio API functions.
 */
static ssize_t audio_dev_write(struct file *filp, const char __user *buf,
			       size_t count, loff_t *f_pos)
{
	u8 *rec_data;
	struct char_dev_info *dev = filp->private_data;
	struct audio_info *info;
	int err = 0;
	int op_code = 0;
	u8 *curr_data;
	unsigned int stream_handle;
	struct cg2900_dai_config dai_config;
	struct cg2900_endpoint_config ep_config;
	enum cg2900_audio_endpoint_id ep_1;
	enum cg2900_audio_endpoint_id ep_2;
	int bytes_left = count;

	pr_debug("audio_dev_write count %d", count);

	if (!dev) {
		pr_err("audio_dev_write: Transport closed");
		return -EBADF;
	}
	info = dev->info;

	rec_data = kmalloc(count, GFP_KERNEL);
	if (!rec_data) {
		dev_err(BT_DEV, "kmalloc failed (%d bytes)\n", count);
		return -ENOMEM;
	}

	mutex_lock(&dev->rw_mutex);

	err = copy_from_user(rec_data, buf, count);
	if (err) {
		dev_err(BT_DEV, "copy_from_user failed (%d)\n", err);
		err = -EFAULT;
		goto finished_mutex_unlock;
	}

	/* Initialize temporary data pointer used to traverse the packet */
	curr_data = rec_data;

	op_code = curr_data[0];
	/* OpCode is int size to keep data int aligned */
	curr_data += sizeof(unsigned int);
	bytes_left -= sizeof(unsigned int);

	switch (op_code) {
	case CG2900_OPCODE_SET_DAI_CONF:
		if (bytes_left < sizeof(dai_config)) {
			dev_err(BT_DEV, "Not enough data supplied for "
				"CG2900_OPCODE_SET_DAI_CONF\n");
			err = -EINVAL;
			goto finished_mutex_unlock;
		}
		memcpy(&dai_config, curr_data, sizeof(dai_config));
		dev_dbg(BT_DEV, "CG2900_OPCODE_SET_DAI_CONF port %d\n",
			dai_config.port);
		err = cg2900_audio_set_dai_config(dev->session, &dai_config);
		break;

	case CG2900_OPCODE_GET_DAI_CONF:
		if (bytes_left < sizeof(dai_config)) {
			dev_err(BT_DEV, "Not enough data supplied for "
				"CG2900_OPCODE_GET_DAI_CONF\n");
			err = -EINVAL;
			goto finished_mutex_unlock;
		}
		/*
		 * Only need to copy the port really, but let's copy
		 * like this for simplicity. It's only test functionality
		 * after all.
		 */
		memcpy(&dai_config, curr_data, sizeof(dai_config));
		dev_dbg(BT_DEV, "CG2900_OPCODE_GET_DAI_CONF port %d\n",
			dai_config.port);
		err = cg2900_audio_get_dai_config(dev->session, &dai_config);
		if (!err) {
			int len;
			struct sk_buff *skb;

			/*
			 * Command succeeded. Store data so it can be returned
			 * when calling read.
			 */
			len = sizeof(op_code) + sizeof(dai_config);
			skb = alloc_skb(len, GFP_KERNEL);
			if (!skb) {
				dev_err(BT_DEV, "CG2900_OPCODE_GET_DAI_CONF: "
						"Could not allocate skb\n");
				err = -ENOMEM;
				goto finished_mutex_unlock;
			}
			memcpy(skb_put(skb, sizeof(op_code)), &op_code,
			       sizeof(op_code));
			memcpy(skb_put(skb, sizeof(dai_config)),
			       &dai_config, sizeof(dai_config));
			skb_queue_tail(&dev->rx_queue, skb);
		}
		break;

	case CG2900_OPCODE_CONFIGURE_ENDPOINT:
		if (bytes_left < sizeof(ep_config)) {
			dev_err(BT_DEV, "Not enough data supplied for "
				"CG2900_OPCODE_CONFIGURE_ENDPOINT\n");
			err = -EINVAL;
			goto finished_mutex_unlock;
		}
		memcpy(&ep_config, curr_data, sizeof(ep_config));
		dev_dbg(BT_DEV, "CG2900_OPCODE_CONFIGURE_ENDPOINT ep_id %d\n",
			ep_config.endpoint_id);
		err = cg2900_audio_config_endpoint(dev->session, &ep_config);
		break;

	case CG2900_OPCODE_START_STREAM:
		if (bytes_left < (sizeof(ep_1) + sizeof(ep_2))) {
			dev_err(BT_DEV, "Not enough data supplied for "
				"CG2900_OPCODE_START_STREAM\n");
			err = -EINVAL;
			goto finished_mutex_unlock;
		}
		memcpy(&ep_1, curr_data, sizeof(ep_1));
		curr_data += sizeof(ep_1);
		memcpy(&ep_2, curr_data, sizeof(ep_2));
		dev_dbg(BT_DEV, "CG2900_OPCODE_START_STREAM ep_1 %d ep_2 %d\n",
			ep_1, ep_2);

		err = cg2900_audio_start_stream(dev->session,
			ep_1, ep_2, &stream_handle);
		if (!err) {
			int len;
			struct sk_buff *skb;

			/*
			 * Command succeeded. Store data so it can be returned
			 * when calling read.
			 */
			len = sizeof(op_code) + sizeof(stream_handle);
			skb = alloc_skb(len, GFP_KERNEL);
			if (!skb) {
				dev_err(BT_DEV, "CG2900_OPCODE_START_STREAM: "
						"Could not allocate skb\n");
				err = -ENOMEM;
				goto finished_mutex_unlock;
			}
			memcpy(skb_put(skb, sizeof(op_code)), &op_code,
			       sizeof(op_code));
			memcpy(skb_put(skb, sizeof(stream_handle)),
			       &stream_handle, sizeof(stream_handle));
			skb_queue_tail(&dev->rx_queue, skb);

			dev_dbg(BT_DEV, "stream_handle %d\n", stream_handle);
		}
		break;

	case CG2900_OPCODE_STOP_STREAM:
		if (bytes_left < sizeof(stream_handle)) {
			dev_err(BT_DEV, "Not enough data supplied for "
				"CG2900_OPCODE_STOP_STREAM\n");
			err = -EINVAL;
			goto finished_mutex_unlock;
		}
		memcpy(&stream_handle, curr_data, sizeof(stream_handle));
		dev_dbg(BT_DEV, "CG2900_OPCODE_STOP_STREAM stream_handle %d\n",
			stream_handle);
		err = cg2900_audio_stop_stream(dev->session, stream_handle);
		break;

	default:
		dev_err(BT_DEV, "Received bad op_code %d\n", op_code);
		break;
	};

finished_mutex_unlock:
	kfree(rec_data);
	mutex_unlock(&dev->rw_mutex);

	if (err)
		return err;
	else
		return count;
}

/**
 * audio_dev_poll() - Handle POLL call to the interface.
 * @filp:	Pointer to the file struct.
 * @wait:	Poll table supplied to caller.
 *
 * This function is used by the User Space application to see if the device is
 * still open and if there is any data available for reading.
 *
 * Returns:
 *   Mask of current set POLL values.
 */
static unsigned int audio_dev_poll(struct file *filp, poll_table *wait)
{
	struct char_dev_info *dev = filp->private_data;
	struct audio_info *info;
	unsigned int mask = 0;

	if (!dev) {
		pr_err("audio_dev_poll: Transport closed");
		return POLLERR | POLLRDHUP;
	}
	info = dev->info;

	if (RESET == info->state)
		mask |= POLLERR | POLLRDHUP | POLLPRI;
	else
		/* Unless RESET we can transmit */
		mask |= POLLOUT;

	if (!skb_queue_empty(&dev->rx_queue))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct file_operations char_dev_fops = {
	.open = audio_dev_open,
	.release = audio_dev_release,
	.read = audio_dev_read,
	.write = audio_dev_write,
	.poll = audio_dev_poll
};

/**
 * probe_common() - Register misc device.
 * @info:	Audio info structure.
 * @dev:	Current device.
 *
 * Returns:
 *   0 if there is no error.
 *   -ENOMEM if allocation fails.
 *   Error codes from misc_register.
 */
static int probe_common(struct audio_info *info, struct device *dev)
{
	struct audio_cb_info *cb_info;
	struct cg2900_user_data *pf_data;
	int err;

	cb_info = kzalloc(sizeof(*cb_info), GFP_KERNEL);
	if (!cb_info) {
		dev_err(dev, "Failed to allocate cb_info\n");
		return -ENOMEM;
	}
	init_waitqueue_head(&cb_info->wq);
	skb_queue_head_init(&cb_info->skb_queue);

	pf_data = dev_get_platdata(dev);
	cg2900_set_usr(pf_data, cb_info);
	pf_data->dev = dev;
	pf_data->read_cb = read_cb;
	pf_data->reset_cb = reset_cb;

	/* Only register misc device when both devices (BT and FM) are probed */
	if (!info->dev_bt || !info->dev_fm)
		return 0;

	/* Prepare and register MISC device */
	info->misc_dev.minor = MISC_DYNAMIC_MINOR;
	info->misc_dev.name = NAME;
	info->misc_dev.fops = &char_dev_fops;
	info->misc_dev.parent = dev;
	info->misc_dev.mode = S_IRUGO | S_IWUGO;

	err = misc_register(&info->misc_dev);
	if (err) {
		dev_err(dev, "Error %d registering misc dev\n", err);
		return err;
	}
	info->misc_registered = true;

	dev_info(dev, "CG2900 Audio driver started\n");
	return 0;
}

/**
 * cg2900_audio_bt_probe() - Initialize CG2900 BT audio resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if there is no error.
 *   -ENOMEM if allocation fails.
 *   -EEXIST if device has already been started.
 *   Error codes from probe_common.
 */
static int __devinit cg2900_audio_bt_probe(struct platform_device *pdev)
{
	int err;
	struct audio_info *info;

	dev_dbg(&pdev->dev, "cg2900_audio_bt_probe\n");

	info = get_info(&pdev->dev);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->dev_bt = &pdev->dev;
	dev_set_drvdata(&pdev->dev, info);

	err = probe_common(info, &pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Could not probe audio BT (%d)\n", err);
		dev_set_drvdata(&pdev->dev, NULL);
		device_removed(info);
	}

	return err;
}

/**
 * cg2900_audio_bt_probe() - Initialize CG2900 FM audio resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if there is no error.
 *   -ENOMEM if allocation fails.
 *   -EEXIST if device has already been started.
 *   Error codes from probe_common.
 */
static int __devinit cg2900_audio_fm_probe(struct platform_device *pdev)
{
	int err;
	struct audio_info *info;

	dev_dbg(&pdev->dev, "cg2900_audio_fm_probe\n");

	info = get_info(&pdev->dev);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->dev_fm = &pdev->dev;
	dev_set_drvdata(&pdev->dev, info);

	err = probe_common(info, &pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Could not probe audio FM (%d)\n", err);
		dev_set_drvdata(&pdev->dev, NULL);
		device_removed(info);
	}

	return err;
}

/**
 * common_remove() - Dergister misc device.
 * @info:	Audio info structure.
 * @dev:	Current device.
 *
 * Returns:
 *   0 if success.
 *   Error codes from misc_deregister.
 */
static int common_remove(struct audio_info *info, struct device *dev)
{
	int err;
	struct audio_cb_info *cb_info;
	struct cg2900_user_data *pf_data;

	pf_data = dev_get_platdata(dev);
	cb_info = cg2900_get_usr(pf_data);
	skb_queue_purge(&cb_info->skb_queue);
	wake_up_all(&cb_info->wq);
	kfree(cb_info);

	if (!info->misc_registered)
		return 0;

	err = misc_deregister(&info->misc_dev);
	if (err)
		dev_err(dev, "Error %d deregistering misc dev\n", err);
	info->misc_registered = false;

	if (info->filp)
		info->filp->private_data = NULL;

	dev_info(dev, "CG2900 Audio driver removed\n");
	return err;
}

/**
 * cg2900_audio_bt_remove() - Release CG2900 audio resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success.
 *   Error codes from common_remove.
 */
static int __devexit cg2900_audio_bt_remove(struct platform_device *pdev)
{
	int err;
	struct audio_info *info;

	dev_dbg(&pdev->dev, "cg2900_audio_bt_remove\n");

	info = dev_get_drvdata(&pdev->dev);

	info->dev_bt = NULL;

	err = common_remove(info, &pdev->dev);
	if (err)
		dev_err(&pdev->dev,
			"cg2900_audio_bt_remove:common_remove failed\n");

	device_removed(info);

	return 0;
}

/**
 * cg2900_audio_fm_remove() - Release CG2900 audio resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success.
 *   Error codes from common_remove.
 */
static int __devexit cg2900_audio_fm_remove(struct platform_device *pdev)
{
	int err;
	struct audio_info *info;

	dev_dbg(&pdev->dev, "cg2900_audio_fm_remove\n");

	info = dev_get_drvdata(&pdev->dev);

	info->dev_fm = NULL;

	err = common_remove(info, &pdev->dev);
	if (err)
		dev_err(&pdev->dev,
			"cg2900_audio_fm_remove:common_remove failed\n");

	device_removed(info);

	return 0;
}

static struct platform_driver cg2900_audio_bt_driver = {
	.driver = {
		.name	= "cg2900-audiovs",
		.owner	= THIS_MODULE,
	},
	.probe	= cg2900_audio_bt_probe,
	.remove	= __devexit_p(cg2900_audio_bt_remove),
};

static struct platform_driver cg2900_audio_fm_driver = {
	.driver = {
		.name	= "cg2900-audiofm",
		.owner	= THIS_MODULE,
	},
	.probe	= cg2900_audio_fm_probe,
	.remove	= __devexit_p(cg2900_audio_fm_remove),
};

/**
 * cg2900_audio_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init cg2900_audio_init(void)
{
	int err;

	pr_debug("cg2900_audio_init");

	err = platform_driver_register(&cg2900_audio_bt_driver);
	if (err)
		return err;
	return platform_driver_register(&cg2900_audio_fm_driver);
}

/**
 * cg2900_audio_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit cg2900_audio_exit(void)
{
	pr_debug("cg2900_audio_exit");
	platform_driver_unregister(&cg2900_audio_fm_driver);
	platform_driver_unregister(&cg2900_audio_bt_driver);
}

module_init(cg2900_audio_init);
module_exit(cg2900_audio_exit);

MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_AUTHOR("Kjell Andersson ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Linux Bluetooth Audio ST-Ericsson controller");
