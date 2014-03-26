/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson CG2900 connectivity
 * controller.
 */

#ifndef _CG2900_H_
#define _CG2900_H_

#include <linux/types.h>

/* Perform reset. No parameters used */
#define CG2900_CHAR_DEV_IOCTL_RESET		_IOW('U', 210, int)
/* Check for reset */
#define CG2900_CHAR_DEV_IOCTL_CHECK4RESET	_IOR('U', 212, int)
/* Retrieve revision info */
#define CG2900_CHAR_DEV_IOCTL_GET_REVISION	_IOR('U', 213, \
						     struct cg2900_rev_data)
/* Sysclk3 - Clock Enable used for GPS */
#define CG2900_CHAR_DEV_IOCTL_EXT_CLK_ENABLE    _IOR('U', 214, int)
/* Sysclk3 - Clock Disable used for GPS */
#define CG2900_CHAR_DEV_IOCTL_EXT_CLK_DISABLE   _IOR('U', 215, int)

#define CG2900_CHAR_DEV_IOCTL_EVENT_IDLE	0
#define CG2900_CHAR_DEV_IOCTL_EVENT_RESET	1

/* Specific chip version data */
#define STLC2690_REV			0x0600
#define CG2900_PG1_REV			0x0101
#define CG2900_PG2_REV			0x0200
#define CG2900_PG1_SPECIAL_REV	0x0700
#define CG2905_PG1_05_REV		0x1805
/*
 * There is an issue in OTP setting of a single bit for distinction
 * between CG2905 and CG2910. So Recommendation from the CG2900 Chip
 * Architects is that CG2910 PG1_05 HCI version has to be
 * considered as CG2905 PG1_05.
 */
#define CG2910_PG1_05_REV		0x1005
#define CG2905_PG2_REV			0x1806
#define CG2905_PG2_REV_OTP_NOT_SET	0x1006
#define CG2910_PG1_REV			0x1004
#define CG2910_PG2_REV			0x1008

/**
 * struct cg2900_rev_data - Contains revision data for the local controller.
 * @revision:		Revision of the controller, e.g. to indicate that it is
 *			a CG2900 controller.
 * @sub_version:	Subversion of the controller, e.g. to indicate a certain
 *			tape-out of the controller.
 *
 * The values to match retrieved values to each controller may be retrieved from
 * the manufacturer.
 */
struct cg2900_rev_data {
	u16 revision;
	u16 sub_version;
};

#ifdef __KERNEL__
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>

/* Temporary solution while in staging directory */
#include "cg2900_hci.h"

/**
 * struct cg2900_chip_rev_info - Chip info structure.
 * @manufacturer:	Chip manufacturer.
 * @hci_version:	Bluetooth version supported over HCI.
 * @hci_revision:	Chip revision, i.e. which chip is this.
 * @lmp_pal_version:	Bluetooth version supported over air.
 * @hci_sub_version:	Chip sub-version, i.e. which tape-out is this.
 *
 * Note that these values match the Bluetooth Assigned Numbers,
 * see http://www.bluetooth.org/
 */
struct cg2900_chip_rev_info {
	u16	manufacturer;
	u8	hci_version;
	u16	hci_revision;
	u8	lmp_pal_version;
	u16	hci_sub_version;
};

struct cg2900_chip_dev;

/**
 * struct cg2900_id_callbacks - Chip handler identification callbacks.
 * @check_chip_support:	Called when chip is connected. If chip is supported by
 *			driver, return true and fill in @callbacks in @dev.
 *
 * Note that the callback may be NULL. It must always be NULL checked before
 * calling.
 */
struct cg2900_id_callbacks {
	bool (*check_chip_support)(struct cg2900_chip_dev *dev);
};

/**
 * struct cg2900_chip_callbacks - Callback functions registered by chip handler.
 * @data_from_chip:	Called when data shall be transmitted to user.
 * @chip_removed:	Called when chip is removed.
 *
 * Note that some callbacks may be NULL. They must always be NULL checked before
 * calling.
 */
struct cg2900_chip_callbacks {
	void (*data_from_chip)(struct cg2900_chip_dev *dev,
			       struct sk_buff *skb);
	void (*chip_removed)(struct cg2900_chip_dev *dev);
};

/**
 * struct cg2900_trans_callbacks - Callback functions registered by transport.
 * @open:		CG2900 Core needs a transport.
 * @close:		CG2900 Core does not need a transport.
 * @write:		CG2900 Core transmits to the chip.
 * @set_chip_power:	CG2900 Core enables or disables the chip.
 * @chip_startup_finished:	CG2900 Chip startup finished notification.
 *
 * Note that some callbacks may be NULL. They must always be NULL checked before
 * calling.
 */
struct cg2900_trans_callbacks {
	int (*open)(struct cg2900_chip_dev *dev);
	int (*close)(struct cg2900_chip_dev *dev);
	int (*write)(struct cg2900_chip_dev *dev, struct sk_buff *skb);
	void (*set_chip_power)(struct cg2900_chip_dev *dev, bool chip_on);
	void (*chip_startup_finished)(struct cg2900_chip_dev *dev);
	void (*set_baud_rate)(struct cg2900_chip_dev *dev, bool low_baud);
};

/**
 * struct cg2900_chip_dev - Chip handler info structure.
 * @dev:	Device associated with this chip.
 * @pdev:	Platform device associated with this chip.
 * @chip:	Chip info such as manufacturer.
 * @c_cb:	Callback structure for the chip handler.
 * @t_cb:	Callback structure for the transport.
 * @c_data:	Arbitrary data set by chip handler.
 * @t_data:	Arbitrary data set by transport.
 * @b_data:	Arbitrary data set by board handler.
 * @prv_data:	Arbitrary data set by CG2900 Core.
 */
struct cg2900_chip_dev {
	struct device			*dev;
	struct platform_device		*pdev;
	struct cg2900_chip_rev_info	chip;
	struct cg2900_chip_callbacks	c_cb;
	struct cg2900_trans_callbacks	t_cb;
	void				*c_data;
	void				*t_data;
	void				*b_data;
	void				*prv_data;
};

/**
 * struct cg2900_platform_data - Contains platform data for CG2900.
 * @init:		Callback called upon system start.
 * @exit:		Callback called upon system shutdown.
 * @enable_chip:	Callback called for enabling CG2900 chip.
 * @disable_chip:	Callback called for disabling CG2900 chip.
 * @get_power_switch_off_cmd:	Callback called to retrieve
 *				HCI VS_Power_Switch_Off command (command
 *				HCI requires platform specific GPIO data).
 * @regulator_id:	Id of the regulator that powers on the chip
 * @bus:		Transport used, see @include/net/bluetooth/hci.h.
 * @gpio_sleep:		Array of GPIO sleep settings.
 * @enable_uart:	Callback called when switching from UART GPIO to
 *			UART HW.
 * @disable_uart:	Callback called when switching from UART HW to
 *			UART GPIO.
 * @n_uart_gpios:	Number of UART GPIOs.
 * @uart_enabled:	Array of size @n_uart_gpios with GPIO setting for
 *			enabling UART HW (switching from GPIO mode).
 * @uart_disabled:	Array of size @n_uart_gpios with GPIO setting for
 *			disabling UART HW (switching to GPIO mode).
 * @uart:		Platform data structure for UART transport.
 *
 * Any callback may be NULL if not needed.
 */
struct cg2900_platform_data {
	int (*init)(struct cg2900_chip_dev *dev);
	void (*exit)(struct cg2900_chip_dev *dev);
	void (*enable_chip)(struct cg2900_chip_dev *dev);
	void (*disable_chip)(struct cg2900_chip_dev *dev);
	struct sk_buff* (*get_power_switch_off_cmd)(struct cg2900_chip_dev *dev,
						    u16 *op_code);

	char *regulator_id;
	__u8 bus;
	enum cg2900_gpio_pull_sleep *gpio_sleep;

	struct {
		int (*enable_uart)(struct cg2900_chip_dev *dev);
		int (*disable_uart)(struct cg2900_chip_dev *dev);
		int n_uart_gpios;
		unsigned long *uart_enabled;
		unsigned long *uart_disabled;
	} uart;
};

/**
 * struct cg2900_user_data - Contains platform data for CG2900 user.
 * @dev:		Current device. Set by CG2900 user upon probe.
 * @opened:		True if channel is opened.
 * @user_data:		Data set and used by CG2900 user.
 * @private_data:	Data set and used by CG2900 driver.
 * @h4_channel:		H4 channel. Set by CG2900 driver.
 * @is_audio:		True if this channel is an audio channel. Set by CG2900
 *			driver.
 * @is_clk_user:	whether enabling CG29XX was started external entity
 *			for eg. WLAN.
 * @chip_independent:	True if this channel does not require chip to be
 *			powered. Set by CG2900 driver.
 * @bt_bus:		Transport used, see @include/net/bluetooth/hci.h.
 * @char_dev_name:	Name to be used for character device.
 * @channel_data:	Input data specific to current device.
 * @open:		Open device channel. Set by CG2900 driver.
 * @close:		Close device channel. Set by CG2900 driver.
 * @reset:		Reset connectivity controller. Set by CG2900 driver.
 * @alloc_skb:		Alloc sk_buffer. Set by CG2900 driver.
 * @write:		Write to device channel. Set by CG2900 driver.
 * @get_local_revision:	Get revision data of conncected chip. Set by CG2900
 *			driver.
 * @read_cb:		Callback function called when data is received on the
 *			device channel. Set by CG2900 user. Mandatory.
 * @reset_cb:		Callback function called when the connectivity
 *			controller has been reset. Set by CG2900 user.
 *
 * Any callback may be NULL if not needed.
 */
struct cg2900_user_data {
	struct device *dev;
	bool opened;

	void *user_data;
	void *private_data;

	int	h4_channel;
	bool	is_audio;
	bool	is_clk_user;
	bool	chip_independent;

	union {
		__u8 bt_bus;
		char *char_dev_name;
	} channel_data;

	int (*open)(struct cg2900_user_data *user_data);
	void (*close)(struct cg2900_user_data *user_data);
	int (*reset)(struct cg2900_user_data *user_data);
	struct sk_buff * (*alloc_skb)(unsigned int size, gfp_t priority);
	int (*write)(struct cg2900_user_data *user_data, struct sk_buff *skb);
	bool (*get_local_revision)(struct cg2900_user_data *user_data,
				   struct cg2900_rev_data *rev_data);

	void (*read_cb)(struct cg2900_user_data *user_data,
			struct sk_buff *skb);
	void (*reset_cb)(struct cg2900_user_data *user_data);
};

static inline void *cg2900_get_usr(struct cg2900_user_data *dev)
{
	if (dev)
		return dev->user_data;
	return NULL;
}

static inline void cg2900_set_usr(struct cg2900_user_data *dev, void *data)
{
	if (dev)
		dev->user_data = data;
}

static inline void *cg2900_get_prv(struct cg2900_user_data *dev)
{
	if (dev)
		return dev->private_data;
	return NULL;
}

static inline void cg2900_set_prv(struct cg2900_user_data *dev, void *data)
{
	if (dev)
		dev->private_data = data;
}

static inline bool check_chip_revision_support(u16 hci_revision)
{
	if (hci_revision != CG2900_PG1_SPECIAL_REV &&
			hci_revision != CG2900_PG1_REV &&
			hci_revision != CG2900_PG2_REV &&
			hci_revision != CG2905_PG1_05_REV &&
			hci_revision != CG2905_PG2_REV &&
			hci_revision != CG2910_PG1_REV &&
			hci_revision != CG2910_PG1_05_REV &&
			hci_revision != CG2910_PG2_REV)
		return false;

	return true;
}

static inline bool use_device_channel_for_vs_cmd(u16 hci_revision)
{
	if (hci_revision == CG2905_PG1_05_REV ||
			hci_revision == CG2905_PG2_REV ||
			hci_revision == CG2910_PG1_REV ||
			hci_revision == CG2910_PG1_05_REV ||
			hci_revision == CG2910_PG2_REV)
		return true;

	return false;
}

extern int cg2900_register_chip_driver(struct cg2900_id_callbacks *cb);
extern void cg2900_deregister_chip_driver(struct cg2900_id_callbacks *cb);
extern int cg2900_register_trans_driver(struct cg2900_chip_dev *dev);
extern int cg2900_deregister_trans_driver(struct cg2900_chip_dev *dev);
extern unsigned long cg2900_get_sleep_timeout(bool check_sleep);

#endif /* __KERNEL__ */
#endif /* _CG2900_H_ */
