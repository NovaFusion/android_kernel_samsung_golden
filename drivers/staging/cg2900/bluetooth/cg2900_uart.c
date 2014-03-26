/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * Lukasz Rymanowski (lukasz.rymanowski@tieto.com) for ST-Ericsson.
 * Hemant Gupta (hemant.gupta@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth UART Driver for ST-Ericsson CG2900 connectivity controller.
 */
#define NAME			"cg2900_uart"
#define pr_fmt(fmt)		NAME ": " fmt "\n"

#include <asm/byteorder.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_qos_params.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/regulator/consumer.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/types.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

#include "cg2900.h"

#include "hci_uart.h"

#define MAIN_DEV		(uart_info->dev)

/* Workqueues' names */
#define UART_WQ_NAME		"cg2900_uart_wq"
#define UART_NAME		"cg2900_uart"

/*
 * A BT command complete event without any parameters is the defined size plus
 * 1 byte extra for the status field which is always present in a
 * command complete event.
 */
#define HCI_BT_CMD_COMPLETE_LEN	(sizeof(struct hci_ev_cmd_complete) + 1)

/* Timers used in milliseconds */
#define UART_TX_TIMEOUT		100
#define UART_RX_TIMEOUT		20
#define UART_RESP_TIMEOUT	1000
#define UART_RESUME_TIMEOUT	20
/* Minimum time host should maintain the break */
#define UART_MIN_BREAK_ON_TIME 5

/* Timers used in microseconds */
/* Minimum time required after sending the last data to apply break */
#define UART_MIN_TIME_BEFORE_APPLYING_BREAK 900
/* Minimum time required after exiting break condition */
#define UART_MIN_BREAK_OFF_TIME 200

/* Max latency in microseconds for PM QoS to achieve max throughput */
#define CG2900_PM_QOS_LATENCY	30

/* Number of bytes to reserve at start of sk_buffer when receiving packet */
#define RX_SKB_RESERVE		8
/* Max size of received packet (not including reserved bytes) */
#define RX_SKB_MAX_SIZE		1024

/* Size of the header in the different packets */
#define HCI_BT_EVT_HDR_SIZE	2
#define HCI_BT_ACL_HDR_SIZE	4
#define HCI_NFC_HDR_SIZE	3
#define HCI_ANT_CMD_HDR_SIZE	1
#define HCI_ANT_DAT_HDR_SIZE	1
#define HCI_FM_RADIO_HDR_SIZE	1
#define HCI_GNSS_HDR_SIZE	3
#define HCI_DEV_MGMT_HDR_SIZE	2

/* Position of length field in the different packets */
#define HCI_EVT_LEN_POS		2
#define HCI_ACL_LEN_POS		3
#define FM_RADIO_LEN_POS	1
#define GNSS_LEN_POS		2

/* Baud rate defines */
#define ZERO_BAUD_RATE		0
#define DEFAULT_BAUD_RATE	115200
#define HIGH_BAUD_RATE		3250000

#define BT_SIZE_OF_HDR				(sizeof(__le16) + sizeof(__u8))
#define BT_PARAM_LEN(__pkt_len)			(__pkt_len - BT_SIZE_OF_HDR)

/* Standardized Bluetooth H:4 channels */
#define HCI_BT_CMD_H4_CHANNEL			0x01
#define HCI_BT_ACL_H4_CHANNEL			0x02
#define HCI_BT_SCO_H4_CHANNEL			0x03
#define HCI_BT_EVT_H4_CHANNEL			0x04

#define HCI_DEV_MGMT_H4_CHANNEL			0x80

#define BT_BDADDR_SIZE				6

/* Reserve 1 byte for the HCI H:4 header */
#define HCI_H4_SIZE				1
#define CG2900_SKB_RESERVE			HCI_H4_SIZE

/* Default H4 channels which may change depending on connected controller */
#define HCI_NFC_H4_CHANNEL			0x05
#define HCI_ANT_CMD_H4_CHANNEL			0x0C
#define HCI_ANT_DAT_H4_CHANNEL			0x0E
#define HCI_FM_RADIO_H4_CHANNEL			0x08
#define HCI_GNSS_H4_CHANNEL			0x09
#define HCI_DEBUG_H4_CHANNEL			0x0B
#define HCI_DEV_MGMT_CHANNEL_BIT	0x80

/* Bluetooth error codes */
#define HCI_BT_ERROR_NO_ERROR			0x00

/* Bytes in the command Hci_Cmd_ST_Set_Uart_Baud_Rate */
#define CG2900_BAUD_RATE_57600				0x03
#define CG2900_BAUD_RATE_115200				0x02
#define CG2900_BAUD_RATE_230400				0x01
#define CG2900_BAUD_RATE_460800				0x00
#define CG2900_BAUD_RATE_921600				0x20
#define CG2900_BAUD_RATE_2000000			0x25
#define CG2900_BAUD_RATE_3000000			0x27
#define CG2900_BAUD_RATE_3250000			0x28
#define CG2900_BAUD_RATE_4000000			0x2B
#define CG2900_BAUD_RATE_4050000			0x59
#define CG2900_BAUD_RATE_4800000			0x5A

/* NFC: 1byte extra is recieved for checksum */
#define NFC_CHECKSUM_DATA_LEN			0x01

/* NFC */
struct nfc_hci_hdr {
	__u8	op_code;
	__le16	plen;
} __packed;

/* ANT COMMAND HEADER */
struct ant_cmd_hci_hdr {
	__u8	plen;
} __packed;

/* ANT DATA HEADER */
struct ant_dat_hci_hdr {
	__u8	plen;
} __packed;

/**
 * enum sleep_allowed_bits - bits indicating if sleep is allowed.
 * @SLEEP_TTY_READY:	Bit for checking if break can be applied using tty.
 * @SLEEP_CORE_READY:	Bit for checking if core is ready for sleep or not.
 */
enum sleep_allowed_bits {
	SLEEP_TTY_READY,
	SLEEP_CORE_READY
};

/* GNSS */
struct gnss_hci_hdr {
	__u8	op_code;
	__le16	plen;
} __packed;

/* FM legacy command packet */
struct fm_leg_cmd {
	__u8	length;
	__u8	opcode;
	__u8	read_write;
	__u8	fm_function;
	union { /* Payload varies with function */
		__le16	irqmask;
		struct fm_leg_fm_cmd {
			__le16	head;
			__le16	data[];
		} fm_cmd;
	};
} __packed;

/* FM legacy command complete packet */
struct fm_leg_cmd_cmpl {
	__u8	param_length;
	__u8	status;
	__u8	opcode;
	__u8	read_write;
	__u8	cmd_status;
	__u8	fm_function;
	__le16	response_head;
	__le16	data[];
} __packed;

/* FM legacy interrupt packet, PG2 style */
struct fm_leg_irq_v2 {
	__u8	param_length;
	__u8	status;
	__u8	opcode;
	__u8	event_type;
	__u8	event_id;
	__le16	irq;
} __packed;

/* FM legacy interrupt packet, PG1 style */
struct fm_leg_irq_v1 {
	__u8	param_length;
	__u8	opcode;
	__u8	event_id;
	__le16	irq;
} __packed;

union fm_leg_evt_or_irq {
	__u8			param_length;
	struct fm_leg_cmd_cmpl	evt;
	struct fm_leg_irq_v2	irq_v2;
	struct fm_leg_irq_v1	irq_v1;
} __packed;

/* BT VS SetBaudRate command */
#define CG2900_BT_OP_VS_SET_BAUD_RATE			0xFC09
struct bt_vs_set_baud_rate_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	baud_rate;
} __packed;

/**
 * enum uart_rx_state - UART RX-state for UART.
 * @W4_PACKET_TYPE:	Waiting for packet type.
 * @W4_EVENT_HDR:	Waiting for BT event header.
 * @W4_ACL_HDR:		Waiting for BT ACL header.
 * @W4_NFC_HDR:		Waiting for NFC header.
 * @W4_ANT_CMD_HDR:	Waiting for ANT command header.
 * @W4_ANT_DAT_HDR:	Waiting for ANT data header.
 * @W4_FM_RADIO_HDR:	Waiting for FM header.
 * @W4_DBG_HDR:		Waiting for Debug header.
 * @W4_GNSS_HDR:	Waiting for GNSS header.
 * @W4_DATA:		Waiting for data in rest of the packet (after header).
 */
enum uart_rx_state {
	W4_PACKET_TYPE,
	W4_EVENT_HDR,
	W4_ACL_HDR,
	W4_NFC_HDR,
	W4_ANT_CMD_HDR,
	W4_ANT_DAT_HDR,
	W4_FM_RADIO_HDR,
	W4_GNSS_HDR,
	W4_DBG_HDR,
	W4_DEV_MGMT_HDR,
	W4_DATA
};

/**
  * enum sleep_state - Sleep-state for UART.
  * @CHIP_AWAKE:  Chip is awake.
  * @CHIP_FALLING_ASLEEP:  Chip is falling asleep.
  * @CHIP_ASLEEP: Chip is asleep.
  * @CHIP_SUSPENDED: Chip in suspend state.
  * @CHIP_RESUMING: Chip is going back from suspend state.
  * @CHIP_POWERED_DOWN: Chip is off.
  */
enum sleep_state {
	CHIP_AWAKE,
	CHIP_FALLING_ASLEEP,
	CHIP_ASLEEP,
	CHIP_SUSPENDED,
	CHIP_RESUMING,
	CHIP_POWERED_DOWN
};

/**
 * enum baud_rate_change_state - Baud rate-state for UART.
 * @BAUD_IDLE:		No baud rate change is ongoing.
 * @BAUD_SENDING_RESET:	HCI reset has been sent. Waiting for command complete
 *			event.
 * @BAUD_START:		Set baud rate cmd scheduled for sending.
 * @BAUD_SENDING:	Set baud rate cmd sending in progress.
 * @BAUD_WAITING:	Set baud rate cmd sent, waiting for command complete
 *			event.
 * @BAUD_SUCCESS:	Baud rate change has succeeded.
 * @BAUD_FAIL:		Baud rate change has failed.
 */
enum baud_rate_change_state {
	BAUD_IDLE,
	BAUD_SENDING_RESET,
	BAUD_START,
	BAUD_SENDING,
	BAUD_WAITING,
	BAUD_SUCCESS,
	BAUD_FAIL
};

/**
 * struct uart_work_struct - Work structure for UART module.
 * @work:	Work structure.
 * @data:	Pointer to private data.
 *
 * This structure is used to pack work for work queue.
 */
struct uart_work_struct {
	struct work_struct	work;
	void			*data;
};

/**
 * struct uart_delayed_work_struct - Work structure for UART module.
 * @delayed_work:	Work structure.
 * @data:	Pointer to private data.
 *
 * This structure is used to pack work for work queue.
 */
struct uart_delayed_work_struct {
	struct delayed_work	work;
	void			*data;
};

/**
 * struct uart_info - Main UART info structure.
 * @rx_state:		Current RX state.
 * @rx_count:		Number of bytes left to receive.
 * @rx_skb:		SK_buffer to store the received data into.
 * @tx_queue:		TX queue for sending data to chip.
 * @rx_skb_lock	Spin lock to protect rx_skb.
 * @hu:			Hci uart structure.
 * @wq:			UART work queue.
 * @baud_rate_state:	UART baud rate change state.
 * @baud_rate:		Current baud rate setting.
 * @sleep_state:	UART sleep state.
 * @sleep_work:	Delayed sleep work struct.
 * @wakeup_work:	Wake-up work struct.
 * @restart_sleep_work:	Reschedule sleep_work and wake-up work struct.
 * @sleep_state_lock:	Used to protect chip state.
 * @sleep_flags:	Indicates whether sleep mode is allowed.
 * @tx_in_progress:	Indicates data sending in progress.
 * @rx_in_progress:	Indicates data receiving in progress.
 * @transmission_lock:	Spin_lock to protect tx/rx_in_progress.
 * @regulator:		Regulator.
 * @regulator_enabled:	True if regulator is enabled.
 * @dev:		Pointer to CG2900 uart device.
 * @chip_dev:		Chip device for current UART transport.
 * @cts_irq:		CTS interrupt for this UART.
 * @cts_gpio:		CTS GPIO for this UART.
 * @wake_lock:		Wake lock for keeping user space awake (for Android).
 * @suspend_blocked:	True if suspend operation is blocked in the framework.
 * @pm_qos_latency:	PM QoS structure.
 */
struct uart_info {
	enum uart_rx_state		rx_state;
	unsigned long			rx_count;
	struct sk_buff			*rx_skb;
	struct sk_buff_head		tx_queue;
	spinlock_t			rx_skb_lock;

	struct hci_uart			*hu;

	struct workqueue_struct		*wq;
	enum baud_rate_change_state	baud_rate_state;
	int				baud_rate;
	enum sleep_state		sleep_state;
	struct uart_delayed_work_struct	sleep_work;
	struct uart_work_struct		wakeup_work;
	struct uart_work_struct		restart_sleep_work;
	struct mutex			sleep_state_lock;
	unsigned long			sleep_flags;
	bool				tx_in_progress;
	bool				rx_in_progress;
	spinlock_t			transmission_lock;
	struct	regulator		*regulator;
	bool				regulator_enabled;
	struct device			*dev;
	struct cg2900_chip_dev		chip_dev;
	int				cts_irq;
	int				cts_gpio;
	struct wake_lock		wake_lock;
	bool				suspend_blocked;
	struct pm_qos_request_list	pm_qos_latency;
};

/* Module parameters */
static int uart_default_baud = DEFAULT_BAUD_RATE;
static int uart_high_baud = HIGH_BAUD_RATE;
static int uart_debug;

static DECLARE_WAIT_QUEUE_HEAD(uart_wait_queue);

static void wake_up_chip(struct uart_info *uart_info);

/**
 * is_chip_flow_off() - Check if chip has set flow off.
 * @tty:	Pointer to tty.
 *
 * Returns:
 *   true - chip flows off.
 *   false - chip flows on.
 */
static bool is_chip_flow_off(struct uart_info *uart_info)
{
	int lines = 0;

	if (uart_info->hu)
		lines = hci_uart_tiocmget(uart_info->hu);

	if (lines & TIOCM_CTS)
		return false;
	else
		return true;
}

/**
 * create_work_item() - Create work item and add it to the work queue.
 * @uart_info:	Main Uart structure.
 * @work_func:	Work function.
 *
 * Returns:
 *   0 if there is no error.
 *   -EBUSY if not possible to queue work.
 *   -ENOMEM if allocation fails.
 */
static int create_work_item(struct uart_info *uart_info,
					work_func_t work_func)
{
	struct uart_work_struct *new_work;
	int res;

	new_work = kmalloc(sizeof(*new_work), GFP_ATOMIC);
	if (!new_work) {
		dev_err(MAIN_DEV,
			"Failed to alloc memory for uart_work_struct\n");
		return -ENOMEM;
	}

	new_work->data = uart_info;
	INIT_WORK(&new_work->work, work_func);

	res = queue_work(uart_info->wq, &new_work->work);
	if (!res) {
		dev_err(MAIN_DEV,
			"Failed to queue work_struct because it's already "
			"in the queue\n");
		kfree(new_work);
		return -EBUSY;
	}

	return 0;
}

/**
 * handle_cts_irq() - Called to handle CTS interrupt in work context.
 * @work:	work which needs to be done.
 *
 * The handle_cts_irq() function is a work handler called if interrupt on CTS
 * occurred. It wakes up the transport.
 */
static void handle_cts_irq(struct work_struct *work)
{
	struct uart_work_struct *current_work =
		container_of(work, struct uart_work_struct, work);
	struct uart_info *uart_info = (struct uart_info *)current_work->data;

	pm_qos_update_request(&uart_info->pm_qos_latency,
			      CG2900_PM_QOS_LATENCY);

	spin_lock_bh(&(uart_info->transmission_lock));
	/* Mark that there is an ongoing transfer. */
	uart_info->rx_in_progress = true;
	spin_unlock_bh(&(uart_info->transmission_lock));

	/* Cancel pending sleep work if there is any. */
	cancel_delayed_work_sync(&uart_info->sleep_work.work);

	mutex_lock(&(uart_info->sleep_state_lock));

	if (uart_info->sleep_state == CHIP_SUSPENDED) {
		dev_dbg(MAIN_DEV, "New sleep_state: CHIP_RESUMING\n");
		uart_info->sleep_state = CHIP_RESUMING;
		mutex_unlock(&(uart_info->sleep_state_lock));
	} else {
		mutex_unlock(&(uart_info->sleep_state_lock));
		wake_up_chip(uart_info);
	}

	kfree(current_work);
}

/**
 * cts_interrupt() - Called to handle CTS interrupt.
 * @irq:	Interrupt that occurred.
 * @dev_id:	Device ID where interrupt occurred.
 *
 * The cts_interrupt() function is called if interrupt on CTS occurred.
 * It disables the interrupt and starts a new work thread to handle
 * the interrupt.
 */
static irqreturn_t cts_interrupt(int irq, void *dev_id)
{
	struct uart_info *uart_info = dev_get_drvdata(dev_id);
#ifdef CONFIG_PM
	disable_irq_wake(irq);
#endif
	disable_irq_nosync(irq);
	if (!uart_info->suspend_blocked) {
		wake_lock(&uart_info->wake_lock);
		uart_info->suspend_blocked = true;
	}

	/* Create work and leave IRQ context. */
	(void)create_work_item(uart_info, handle_cts_irq);

	return IRQ_HANDLED;
}

/**
 * set_cts_irq() - Enable interrupt on CTS.
 * @uart_info: Main Uart structure.
 *
 * Returns:
 *   0 if there is no error.
 *   Error codes from request_irq and disable_uart.
 */
static int set_cts_irq(struct uart_info *uart_info)
{
	int err;
	int cts_val = 0;

	/* Set IRQ on CTS. */
	err = request_irq(uart_info->cts_irq,
			  cts_interrupt,
			  IRQF_TRIGGER_FALLING,
			  UART_NAME,
			  uart_info->dev);
	if (err) {
		dev_err(MAIN_DEV, "Could not request CTS IRQ (%d)\n", err);
		return err;
	}

	/*
	 * It may happen that there was already an interrupt on CTS just before
	 * the enable_irq() call above. If the CTS line is low now it means that
	 * it's happened, so disable the CTS interrupt and return -ECANCELED.
	 */
	cts_val = gpio_get_value(uart_info->cts_gpio);
	if (!cts_val) {
		dev_dbg(MAIN_DEV, "Missed interrupt, going back to "
			"awake state\n");
		free_irq(uart_info->cts_irq, uart_info->dev);
		return -ECANCELED;
	}

#ifdef CONFIG_PM
	enable_irq_wake(uart_info->cts_irq);
#endif
	return 0;
}

/**
 * disable_uart_pins() - Disable the UART pins.
 * @uart_info: Main Uart structure.
 */
static void disable_uart_pins(struct uart_info *uart_info)
{
	struct cg2900_platform_data *pf_data;

	pf_data = dev_get_platdata(uart_info->dev);

	if (pf_data->uart.disable_uart) {
		int err = pf_data->uart.disable_uart(&uart_info->chip_dev);
		if (err)
			dev_err(MAIN_DEV,
				"Unable to disable UART Hardware (%d)\n", err);
	}
}

/**
 * enable_uart_pins() - Enable the UART pins.
 * @uart_info: Main Uart structure.
 */
static void enable_uart_pins(struct uart_info *uart_info)
{
	struct cg2900_platform_data *pf_data;

	pf_data = dev_get_platdata(uart_info->dev);

	if (pf_data->uart.enable_uart) {
		int err = pf_data->uart.enable_uart(&uart_info->chip_dev);
		if (err)
			dev_err(MAIN_DEV,
				"Unable to enable UART Hardware (%d)\n", err);
	}
}

/**
 * unset_cts_irq() - Disable interrupt on CTS.
 * @uart_info: Main Uart structure.
 */
static void unset_cts_irq(struct uart_info *uart_info)
{
	/* Free CTS interrupt */
	free_irq(uart_info->cts_irq, uart_info->dev);
}

/**
 * get_sleep_timeout() - Get sleep timeout.
 * @uart_info: Main Uart structure.
 *
 * Check all conditions for sleep and return sleep timeout.
 * Return:
 *	0: sleep not allowed.
 *	other: Timeout value in jiffies.
 */
static unsigned long get_sleep_timeout(struct uart_info *uart_info)
{
	unsigned long timeout_jiffies = 0;
	bool check_sleep = false;
	if (uart_info->sleep_state == CHIP_FALLING_ASLEEP)
		check_sleep = true;

	timeout_jiffies = cg2900_get_sleep_timeout(check_sleep);

	if (timeout_jiffies &&
		uart_info->hu &&
		uart_info->hu->fd &&
		test_bit(SLEEP_TTY_READY, &uart_info->sleep_flags) &&
		test_bit(SLEEP_CORE_READY, &uart_info->sleep_flags))
		return timeout_jiffies;

	return 0;
}

/**
 * work_wake_up_chip() - Called to wake up of the transport in work context.
 * @work:	work which needs to be done.
 */
static void work_wake_up_chip(struct work_struct *work)
{
	struct uart_work_struct *current_work =
		container_of(work, struct uart_work_struct, work);
	struct uart_info *uart_info = (struct uart_info *)current_work->data;

	wake_up_chip(uart_info);
}

/**
 * wake_up_chip() - Wakes up the chip and transport.
 * @work:	pointer to a work struct if the function was called that way.
 *
 * Depending on the current sleep state it may wake up the transport.
 */
static void wake_up_chip(struct uart_info *uart_info)
{
	unsigned long timeout_jiffies = get_sleep_timeout(uart_info);

	/* Resuming state is special. Need to get back chip to awake state. */
	if (!timeout_jiffies && uart_info->sleep_state != CHIP_RESUMING)
		return;

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "wake_up_chip: UART not open\n");
		return;
	}

	mutex_lock(&(uart_info->sleep_state_lock));

	/*
	 * If chip is powered down we cannot wake it up here. It has to be woken
	 * up through a call to uart_set_chip_power()
	 */
	if (CHIP_POWERED_DOWN == uart_info->sleep_state)
		goto finished;

	if (!uart_info->suspend_blocked) {
		wake_lock(&uart_info->wake_lock);
		uart_info->suspend_blocked = true;
		pm_qos_update_request(&uart_info->pm_qos_latency,
				      CG2900_PM_QOS_LATENCY);
	}

	/*
	 * This function indicates data is transmitted.
	 * Therefore see to that the chip is awake.
	 */
	if (CHIP_AWAKE == uart_info->sleep_state)
		goto finished;

	if (CHIP_ASLEEP == uart_info->sleep_state ||
		CHIP_RESUMING == uart_info->sleep_state) {
		/* Wait before disabling IRQ */
		schedule_timeout_killable(
				msecs_to_jiffies(UART_RESUME_TIMEOUT));

		/* Disable IRQ only when it was enabled. */
		unset_cts_irq(uart_info);
		(void)hci_uart_set_baudrate(uart_info->hu,
					    uart_info->baud_rate);

		enable_uart_pins(uart_info);

		/*
		 * Wait before flowing on. Otherwise UART might not be ready in
		 * time
		 */
		schedule_timeout_killable(
				msecs_to_jiffies(UART_RESUME_TIMEOUT));

		/* Set FLOW on. */
		hci_uart_flow_ctrl(uart_info->hu, FLOW_ON);

		/*
		 * Add work so we will go to sleep, this will ensure
		 * if some spurious CTS interrupt comes from UART
		 * on wake up line, we go back to sleep.
		 */
		if (uart_info->rx_in_progress)
			(void)queue_work(uart_info->wq,
				&uart_info->restart_sleep_work.work);
	}

	/* Unset BREAK. */
	dev_dbg(MAIN_DEV, "wake_up_chip: Clear break\n");
	hci_uart_set_break(uart_info->hu, BREAK_OFF);
	udelay(UART_MIN_BREAK_OFF_TIME);

	dev_dbg(MAIN_DEV, "New sleep_state: CHIP_AWAKE\n");
	uart_info->sleep_state = CHIP_AWAKE;

finished:
	mutex_unlock(&(uart_info->sleep_state_lock));
}

/**
 * set_chip_sleep_mode() - Put the chip and transport to sleep mode.
 * @work:	pointer to work_struct.
 *
 * The set_chip_sleep_mode() function is called if there are no ongoing data
 * transmissions. It tries to put the chip in sleep mode.
 *
 */
static void set_chip_sleep_mode(struct work_struct *work)
{
	int err = 0;
	struct delayed_work *delayed_work =
			container_of(work, struct delayed_work, work);
	struct uart_delayed_work_struct *current_work =	container_of(
			delayed_work, struct uart_delayed_work_struct, work);
	struct uart_info *uart_info = (struct uart_info *)current_work->data;
	unsigned long timeout_jiffies = get_sleep_timeout(uart_info);
	int chars_in_buffer;

	if (!timeout_jiffies)
		return;

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "set_chip_sleep_mode: UART not open\n");
		return;
	}

	if (uart_info->tx_in_progress || uart_info->rx_in_progress) {
		dev_dbg(MAIN_DEV, "Not going to sleep, TX/RX in progress\n");
		return;
	}

	mutex_lock(&(uart_info->sleep_state_lock));

	switch (uart_info->sleep_state) {
	case CHIP_FALLING_ASLEEP:
		if (!is_chip_flow_off(uart_info)) {
			dev_dbg(MAIN_DEV, "Chip flow is on, it's not ready to"
				"sleep yet\n");
			hci_uart_set_break(uart_info->hu, BREAK_OFF);
			udelay(UART_MIN_BREAK_OFF_TIME);

			dev_dbg(MAIN_DEV, "New sleep_state: CHIP_AWAKE\n");
			uart_info->sleep_state = CHIP_AWAKE;
			goto schedule_sleep_work;
		}

		/* Flow OFF. */
		hci_uart_flow_ctrl(uart_info->hu, FLOW_OFF);

		disable_uart_pins(uart_info);

		/*
		 * Set baud zero.
		 * This cause shut off UART clock as well.
		 */
		(void)hci_uart_set_baudrate(uart_info->hu, ZERO_BAUD_RATE);
		err = set_cts_irq(uart_info);
		if (err < 0) {
			enable_uart_pins(uart_info);
			(void)hci_uart_set_baudrate(uart_info->hu,
						uart_info->baud_rate);
			hci_uart_flow_ctrl(uart_info->hu, FLOW_ON);
			hci_uart_set_break(uart_info->hu, BREAK_OFF);
			udelay(UART_MIN_BREAK_OFF_TIME);

			dev_dbg(MAIN_DEV, "New sleep_state: CHIP_AWAKE\n");
			uart_info->sleep_state = CHIP_AWAKE;

			if (err == -ECANCELED)
				goto finished;
			else {
				dev_err(MAIN_DEV, "Can not set interrupt on "
						"CTS, err:%d\n", err);
				goto error;
			}
		}

		dev_dbg(MAIN_DEV, "New sleep_state: CHIP_ASLEEP\n");
		uart_info->sleep_state = CHIP_ASLEEP;
		if (uart_info->suspend_blocked) {
			wake_unlock(&uart_info->wake_lock);
			uart_info->suspend_blocked = false;
			pm_qos_update_request(&uart_info->pm_qos_latency,
					      PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE);
		}
		break;
	case CHIP_AWAKE:
		chars_in_buffer = hci_uart_chars_in_buffer(uart_info->hu);
		if (chars_in_buffer) {
			dev_dbg(MAIN_DEV, "sleep_timer_expired: "
					"tx not finished, stay awake and "
					"restart the sleep timer\n");
			goto schedule_sleep_work;
		}

		dev_dbg(MAIN_DEV, "sleep_timer_expired: Set break\n");
		udelay(UART_MIN_TIME_BEFORE_APPLYING_BREAK);
		hci_uart_set_break(uart_info->hu, BREAK_ON);
		schedule_timeout_killable(
				msecs_to_jiffies(UART_MIN_BREAK_ON_TIME));

		dev_dbg(MAIN_DEV, "New sleep_state: CHIP_FALLING_ASLEEP\n");
		uart_info->sleep_state = CHIP_FALLING_ASLEEP;
		goto schedule_sleep_work;

	case CHIP_POWERED_DOWN:
	case CHIP_SUSPENDED:
	case CHIP_ASLEEP: /* Fallthrough. */
	default:
		dev_dbg(MAIN_DEV,
			"Chip sleeps, is suspended or powered down\n");
		break;
	}

	mutex_unlock(&(uart_info->sleep_state_lock));

	return;

finished:
	mutex_unlock(&(uart_info->sleep_state_lock));
	return;
schedule_sleep_work:
	mutex_unlock(&(uart_info->sleep_state_lock));
	if (timeout_jiffies)
		queue_delayed_work(uart_info->wq, &uart_info->sleep_work.work,
				timeout_jiffies);
	return;
error:
	/* Disable sleep mode.*/
	dev_err(MAIN_DEV, "Disable sleep mode\n");
	clear_bit(SLEEP_CORE_READY, &uart_info->sleep_flags);
	mutex_unlock(&(uart_info->sleep_state_lock));
}

#ifdef CONFIG_PM
/**
 * cg2900_uart_suspend() - Called by Linux PM to put the device in a low power mode.
 * @pdev:	Pointer to platform device.
 * @state:	New state.
 *
 * In UART case, CG2900 driver does nothing on suspend.
 *
 * Returns:
 *   0 - Success.
 */
static int cg2900_uart_suspend(struct platform_device *pdev, pm_message_t state)
{
	int err = 0;
	struct uart_info *uart_info = dev_get_drvdata(&pdev->dev);

	mutex_lock(&(uart_info->sleep_state_lock));

	if (uart_info->sleep_state == CHIP_POWERED_DOWN)
		goto finished;

	if (uart_info->sleep_state != CHIP_ASLEEP) {
		err = -EBUSY;
		goto finished;
	}

	dev_dbg(MAIN_DEV, "New sleep_state: CHIP_SUSPENDED\n");
	uart_info->sleep_state = CHIP_SUSPENDED;

finished:
	mutex_unlock(&(uart_info->sleep_state_lock));
	return err;
}

/**
 * cg2900_uart_resume() - Called to bring a device back from a low power state.
 * @pdev:	Pointer to platform device.
 *
 * In UART case, CG2900 driver does nothing on resume.
 *
 * Returns:
 *   0 - Success.
 */
static int cg2900_uart_resume(struct platform_device *pdev)
{
	struct uart_info *uart_info = dev_get_drvdata(&pdev->dev);

	mutex_lock(&(uart_info->sleep_state_lock));

	if (uart_info->sleep_state == CHIP_RESUMING)
		/* System resume because of trafic on UART. Lets wakeup.*/
		(void)queue_work(uart_info->wq, &uart_info->wakeup_work.work);
	else if (uart_info->sleep_state != CHIP_POWERED_DOWN) {
		/* No need to wakeup chip. Go back to Asleep state.*/
		dev_dbg(MAIN_DEV, "New sleep_state: CHIP_ASLEEP\n");
		uart_info->sleep_state = CHIP_ASLEEP;
	}

	mutex_unlock(&(uart_info->sleep_state_lock));
	return 0;
}
#endif /* CONFIG_PM */

/**
 * cg2900_enable_regulator() - Enable regulator.
 * @uart_info: Main Uart structure.
 *
 * Returns:
 *   0 - Success.
 *   Error from regulator_get, regulator_enable.
 */
static int cg2900_enable_regulator(struct uart_info *uart_info)
{
#ifdef CONFIG_REGULATOR
	int err;

	/* Get and enable regulator. */
	uart_info->regulator = regulator_get(uart_info->dev, "gbf_1v8");
	if (IS_ERR(uart_info->regulator)) {
		dev_err(MAIN_DEV, "Not able to find regulator\n");
		err = PTR_ERR(uart_info->regulator);
	} else {
		err = regulator_enable(uart_info->regulator);
		if (err)
			dev_err(MAIN_DEV, "Not able to enable regulator\n");
		else
			uart_info->regulator_enabled = true;
	}
	return err;
#else
	return 0;
#endif
}

/**
 * cg2900_disable_regulator() - Disable regulator.
 * @uart_info: Main Uart structure.
 *
 */
static void cg2900_disable_regulator(struct uart_info *uart_info)
{
#ifdef CONFIG_REGULATOR
	/* Disable and put regulator. */
	if (uart_info->regulator && uart_info->regulator_enabled) {
		regulator_disable(uart_info->regulator);
		uart_info->regulator_enabled = false;
	}
	regulator_put(uart_info->regulator);
	uart_info->regulator = NULL;
#endif
}

/**
 * is_set_baud_rate_cmd() - Checks if data contains set baud rate hci cmd.
 * @data:	Pointer to data array to check.
 *
 * Returns:
 *   true - if cmd found;
 *   false - otherwise.
 */
static bool is_set_baud_rate_cmd(const char *data)
{
	struct hci_command_hdr *cmd;

	cmd = (struct hci_command_hdr *)&data[1];
	if (le16_to_cpu(cmd->opcode) == CG2900_BT_OP_VS_SET_BAUD_RATE &&
	    cmd->plen == BT_PARAM_LEN(sizeof(struct bt_vs_set_baud_rate_cmd)))
		return true;

	return false;
}

/**
 * is_bt_cmd_complete_no_param() - Checks if data contains command complete event for a certain command.
 * @skb:	sk_buffer containing the data including H:4 header.
 * @opcode:	Command op code.
 * @status:	Command status.
 *
 * Returns:
 *   true - If this is the command complete we were looking for;
 *   false - otherwise.
 */
static bool is_bt_cmd_complete_no_param(struct sk_buff *skb, u16 opcode,
					u8 *status, u8 h4_channel)
{
	struct hci_event_hdr *event;
	struct hci_ev_cmd_complete *complete;
	u8 *data = &(skb->data[0]);

	if (h4_channel != *data)
		return false;

	data += HCI_H4_SIZE;
	event = (struct hci_event_hdr *)data;
	if (HCI_EV_CMD_COMPLETE != event->evt ||
	    HCI_BT_CMD_COMPLETE_LEN != event->plen)
		return false;

	data += sizeof(*event);
	complete = (struct hci_ev_cmd_complete *)data;
	if (opcode != le16_to_cpu(complete->opcode))
		return false;

	if (status) {
		/*
		 * All command complete have the status field at first byte of
		 * packet data.
		 */
		data += sizeof(*complete);
		*status = *data;
	}
	return true;
}

/**
 * alloc_rx_skb() - Alloc an sk_buff structure for receiving data from controller.
 * @size:	Size in number of octets.
 * @priority:	Allocation priority, e.g. GFP_KERNEL.
 *
 * Returns:
 *   Pointer to sk_buff structure.
 */
static struct sk_buff *alloc_rx_skb(unsigned int size, gfp_t priority)
{
	struct sk_buff *skb;

	/* Allocate the SKB and reserve space for the header */
	skb = alloc_skb(size + RX_SKB_RESERVE, priority);
	if (skb)
		skb_reserve(skb, RX_SKB_RESERVE);

	return skb;
}

/**
 * finish_setting_baud_rate() - Handles sending the ste baud rate hci cmd.
 * @hu:	Pointer to associated Hci uart structure.
 *
 * finish_setting_baud_rate() makes sure that the set baud rate cmd has
 * been really sent out on the wire and then switches the tty driver to new
 * baud rate.
 */
static void finish_setting_baud_rate(struct hci_uart *hu)
{
	struct uart_info *uart_info =
			(struct uart_info *)dev_get_drvdata(hu->proto->dev);
	/*
	 * Give the tty driver time to send data and proceed. If it hasn't
	 * been sent we can't do much about it anyway.
	 */
	schedule_timeout_killable(msecs_to_jiffies(UART_TX_TIMEOUT));

	/*
	 * Now set the termios struct to the new baudrate. Start by storing
	 * the old termios.
	 */
	if (hci_uart_set_baudrate(hu, uart_info->baud_rate) < 0) {
		/* Something went wrong.*/
		dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_IDLE\n");
		uart_info->baud_rate_state = BAUD_IDLE;
	} else {
		dev_dbg(MAIN_DEV, "Setting termios to new baud rate\n");
		dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_WAITING\n");
		uart_info->baud_rate_state = BAUD_WAITING;
	}

	hci_uart_flow_ctrl(hu, FLOW_ON);
}

/**
 * alloc_set_baud_rate_cmd() - Allocates new sk_buff and fills in the change baud rate hci cmd.
 * @uart_info:	Main Uart structure.
 * @baud:	(in/out) Requested new baud rate. Updated to default baud rate
 *		upon invalid value.
 *
 * Returns:
 *   Pointer to allocated sk_buff if successful;
 *   NULL otherwise.
 */
static struct sk_buff *alloc_set_baud_rate_cmd(struct uart_info *uart_info,
							int *baud)
{
	struct sk_buff *skb;
	u8 *h4;
	struct bt_vs_set_baud_rate_cmd *cmd;

	skb = alloc_skb(sizeof(*cmd) + CG2900_SKB_RESERVE, GFP_ATOMIC);
	if (!skb) {
		dev_err(MAIN_DEV,
			"alloc_set_baud_rate_cmd: Failed to alloc skb\n");
		return NULL;
	}
	skb_reserve(skb, CG2900_SKB_RESERVE);

	cmd = (struct bt_vs_set_baud_rate_cmd *)skb_put(skb, sizeof(cmd));

	/* Create the Hci_Cmd_ST_Set_Uart_Baud_Rate packet */
	cmd->opcode = cpu_to_le16(CG2900_BT_OP_VS_SET_BAUD_RATE);
	cmd->plen = BT_PARAM_LEN(sizeof(cmd));

	switch (*baud) {
	case 57600:
		cmd->baud_rate = CG2900_BAUD_RATE_57600;
		break;
	case 115200:
		cmd->baud_rate = CG2900_BAUD_RATE_115200;
		break;
	case 230400:
		cmd->baud_rate = CG2900_BAUD_RATE_230400;
		break;
	case 460800:
		cmd->baud_rate = CG2900_BAUD_RATE_460800;
		break;
	case 921600:
		cmd->baud_rate = CG2900_BAUD_RATE_921600;
		break;
	case 2000000:
		cmd->baud_rate = CG2900_BAUD_RATE_2000000;
		break;
	case 3000000:
		cmd->baud_rate = CG2900_BAUD_RATE_3000000;
		break;
	case 3250000:
		cmd->baud_rate = CG2900_BAUD_RATE_3250000;
		break;
	case 4000000:
		cmd->baud_rate = CG2900_BAUD_RATE_4000000;
		break;
	case 4050000:
		cmd->baud_rate = CG2900_BAUD_RATE_4050000;
		break;
	case 4800000:
		cmd->baud_rate = CG2900_BAUD_RATE_4800000;
		break;
	default:
		dev_err(MAIN_DEV,
			"Invalid speed requested (%d), using 115200 bps "
			"instead\n", *baud);
		cmd->baud_rate = CG2900_BAUD_RATE_115200;
		*baud = 115200;
		break;
	};

	h4 = skb_push(skb, HCI_H4_SIZE);
	if (use_device_channel_for_vs_cmd(
			uart_info->chip_dev.chip.hci_revision)) {
		*h4 = HCI_DEV_MGMT_H4_CHANNEL;
	} else {
		*h4 = HCI_BT_CMD_H4_CHANNEL;
	}

	return skb;
}

/**
 * work_do_transmit() - Transmit data packet to connectivity controller over UART.
 * @work:	Pointer to work info structure. Contains uart_info structure
 *		pointer.
 */
static void work_do_transmit(struct work_struct *work)
{
	struct uart_work_struct *current_work =
			container_of(work, struct uart_work_struct, work);
	struct uart_info *uart_info = (struct uart_info *)current_work->data;

	kfree(current_work);

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "work_do_transmit: UART not open\n");
		return;
	}

	spin_lock_bh(&(uart_info->transmission_lock));
	/* Mark that there is an ongoing transfer. */
	uart_info->tx_in_progress = true;
	spin_unlock_bh(&(uart_info->transmission_lock));

	/* Cancel pending sleep work if there is any. */
	cancel_delayed_work_sync(&uart_info->sleep_work.work);

	/* Wake up the chip and transport. */
	wake_up_chip(uart_info);

	(void)hci_uart_tx_wakeup(uart_info->hu);
}

/**
 * work_hw_deregistered() - Handle HW deregistered.
 * @work: Reference to work data.
 */
static void work_hw_deregistered(struct work_struct *work)
{
	struct uart_work_struct *current_work;
	struct uart_info *uart_info;
	int err;
	current_work = container_of(work, struct uart_work_struct, work);
	uart_info = (struct uart_info *)current_work->data;

	err = cg2900_deregister_trans_driver(&uart_info->chip_dev);
	if (err)
		dev_err(MAIN_DEV, "Could not deregister UART from Core (%d)\n",
			err);

	kfree(current_work);
}

/**
 * set_baud_rate() - Sets new baud rate for the UART.
 * @hu:		Pointer to hci_uart structure.
 * @baud:	New baud rate.
 *
 * This function first sends the HCI command
 * Hci_Cmd_ST_Set_Uart_Baud_Rate. It then changes the baud rate in HW, and
 * finally it waits for the Command Complete event for the
 * Hci_Cmd_ST_Set_Uart_Baud_Rate command.
 *
 * Returns:
 *   0 if there is no error.
 *   -EALREADY if baud rate change is already in progress.
 *   -EFAULT if one or more of the UART related structs is not allocated.
 *   -ENOMEM if skb allocation has failed.
 *   -EPERM if setting the new baud rate has failed.
 *   Errors from create_work_item.
 */
static int set_baud_rate(struct hci_uart *hu, int baud)
{
	int err = 0;
	struct sk_buff *skb;
	int old_baud_rate;
	struct uart_info *uart_info =
			(struct uart_info *)dev_get_drvdata(hu->proto->dev);

	dev_dbg(MAIN_DEV, "set_baud_rate (%d baud)\n", baud);

	if (uart_info->baud_rate_state != BAUD_IDLE) {
		dev_err(MAIN_DEV,
			"Trying to set new baud rate before old setting "
			   "is finished\n");
		return -EALREADY;
	}

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "set_baud_rate: UART not open\n");
		return -EFAULT;
	}

	/*
	 * Wait some time to be sure that any RX process has finished (which
	 * flows on RTS in the end) before flowing off the RTS.
	 */
	schedule_timeout_killable(msecs_to_jiffies(UART_RX_TIMEOUT));
	hci_uart_flow_ctrl(uart_info->hu, FLOW_OFF);

	/*
	 * Store old baud rate so that we can restore it if something goes
	 * wrong.
	 */
	old_baud_rate = uart_info->baud_rate;

	skb = alloc_set_baud_rate_cmd(uart_info, &baud);
	if (!skb) {
		dev_err(MAIN_DEV, "alloc_set_baud_rate_cmd failed\n");
		return -ENOMEM;
	}

	dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_START\n");
	uart_info->baud_rate_state = BAUD_START;
	uart_info->baud_rate = baud;

	/* Queue the sk_buffer... */
	skb_queue_tail(&uart_info->tx_queue, skb);

	/* ... and call the common UART TX function */
	err = create_work_item(uart_info, work_do_transmit);
	if (err) {
		dev_err(MAIN_DEV,
			"Failed to send change baud rate cmd, freeing skb\n");
		skb = skb_dequeue_tail(&uart_info->tx_queue);
		dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_IDLE\n");
		uart_info->baud_rate_state = BAUD_IDLE;
		uart_info->baud_rate = old_baud_rate;
		kfree_skb(skb);
		return err;
	}

	dev_dbg(MAIN_DEV, "Set baud rate cmd scheduled for sending\n");

	/*
	 * Now wait for the command complete.
	 * It will come at the new baudrate.
	 */
	wait_event_timeout(uart_wait_queue,
				((BAUD_SUCCESS == uart_info->baud_rate_state) ||
				 (BAUD_FAIL    == uart_info->baud_rate_state)),
				 msecs_to_jiffies(UART_RESP_TIMEOUT));
	if (BAUD_SUCCESS == uart_info->baud_rate_state)
		dev_info(MAIN_DEV, "Baud rate changed to %d baud\n", baud);
	else {
		dev_err(MAIN_DEV, "Failed to set new baud rate (%d)\n",
			   uart_info->baud_rate_state);
		err = -EPERM;
	}

	/* Finally flush the TTY so we are sure that is no bad data there */
	hci_uart_flush_buffer(hu);
	dev_dbg(MAIN_DEV, "Flushing TTY after baud rate change\n");
	/* Finished. Set state to IDLE */
	dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_IDLE\n");
	uart_info->baud_rate_state = BAUD_IDLE;

	return err;
}

/**
 * uart_set_baud_rate() - External Interface for setting baud rate
 * @dev:		Transport device information.
 * @low_baud:	whether switch to low_baud or high.
 *
 * Returns:
 *   none
 */
static void uart_set_baud_rate(struct cg2900_chip_dev *dev, bool low_baud)
{
	struct uart_info *uart_info = dev_get_drvdata(dev->dev);

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "uart_set_baud_rate: UART not open\n");
		return;
	}

	if (low_baud) {
		if (uart_info->baud_rate != uart_default_baud) {
			dev_dbg(MAIN_DEV, "Changing BAUD now to : %d",
					uart_default_baud);
			set_baud_rate(uart_info->hu, uart_default_baud);
		} else {
			dev_dbg(MAIN_DEV, "BAUD is already set to :%d",
					uart_default_baud);
		}
	} else {
		if (uart_info->baud_rate != uart_high_baud) {
			dev_dbg(MAIN_DEV, "Changing BAUD now to : %d",
					uart_high_baud);
			set_baud_rate(uart_info->hu, uart_high_baud);
		} else {
			dev_dbg(MAIN_DEV, "BAUD is already set to :%d",
					uart_high_baud);
		}
	}
}

/**
 * uart_write() - Transmit data to CG2900 over UART.
 * @dev:	Transport device information.
 * @skb:	SK buffer to transmit.
 *
 * Returns:
 *   0 if there is no error.
 *   Errors from create_work_item.
 */
static int uart_write(struct cg2900_chip_dev *dev, struct sk_buff *skb)
{
	int err;
	struct uart_info *uart_info = dev_get_drvdata(dev->dev);

	if (uart_debug)
		dev_dbg(MAIN_DEV, "uart_write: data len = %d\n", skb->len);

	/* Queue the sk_buffer... */
	skb_queue_tail(&uart_info->tx_queue, skb);

	/* ...and start TX operation */

	err = create_work_item(uart_info, work_do_transmit);
	if (err)
		dev_err(MAIN_DEV,
			"Failed to create work item (%d) uart_tty_wakeup\n",
			err);

	return err;
}

/**
 * uart_open() - Open the CG2900 UART for data transfers.
 * @dev:	Transport device information.
 *
 * Returns:
 *   0 if there is no error,
 *   -EACCES if write to transport failed,
 *   -EIO if chip did not answer to commands.
 *   Errors from set_baud_rate.
 */
static int uart_open(struct cg2900_chip_dev *dev)
{
	u8 *h4;
	struct sk_buff *skb;
	struct hci_command_hdr *cmd;
	struct uart_info *uart_info = dev_get_drvdata(dev->dev);

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "uart_open: UART not open\n");
		return -EACCES;
	}

	uart_info->baud_rate = uart_default_baud;
	/*
	 * Chip has just been started up. It has a system to autodetect
	 * exact baud rate and transport to use. There are only a few commands
	 * it will recognize and HCI Reset is one of them.
	 * We therefore start with sending that before actually changing
	 * baud rate.
	 *
	 * Create the Hci_Reset packet
	 */

	skb = alloc_skb(sizeof(*cmd) + HCI_H4_SIZE, GFP_ATOMIC);
	if (!skb) {
		dev_err(MAIN_DEV, "Couldn't allocate sk_buff with length %d\n",
			     sizeof(*cmd));
		return -EACCES;
	}
	skb_reserve(skb, HCI_H4_SIZE);
	cmd = (struct hci_command_hdr *)skb_put(skb, sizeof(*cmd));
	cmd->opcode = cpu_to_le16(HCI_OP_RESET);
	cmd->plen = 0; /* No parameters for HCI reset */

	h4 = skb_push(skb, HCI_H4_SIZE);
	*h4 = HCI_BT_CMD_H4_CHANNEL;

	dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_SENDING_RESET\n");
	uart_info->baud_rate_state = BAUD_SENDING_RESET;
	dev_dbg(MAIN_DEV, "Sending HCI reset before baud rate change\n");


	/* Queue the sk_buffer... */
	skb_queue_tail(&uart_info->tx_queue, skb);

	(void)hci_uart_tx_wakeup(uart_info->hu);

	/*
	 * Wait for command complete. If error, exit without changing
	 * baud rate.
	 */
	wait_event_timeout(uart_wait_queue,
					BAUD_IDLE == uart_info->baud_rate_state,
					msecs_to_jiffies(UART_RESP_TIMEOUT));
	if (BAUD_IDLE != uart_info->baud_rate_state) {
		dev_err(MAIN_DEV, "Failed to send HCI Reset\n");
		dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_IDLE\n");
		uart_info->baud_rate_state = BAUD_IDLE;
		return -EIO;
	}

	/* Just return if there will be no change of baud rate */
	if (uart_default_baud != uart_high_baud) {

		/*
		 * Don't change to high baud yet
		 * as there is bug CG2905/10 PG1 that firmware d/l
		 * has to take place at lower baud
		 * after firmware d/l UART will be switched
		 * to higher baud.
		 */
		if (CG2905_PG1_05_REV != dev->chip.hci_revision &&
				CG2910_PG1_REV != dev->chip.hci_revision &&
				CG2910_PG1_05_REV != dev->chip.hci_revision)
			return set_baud_rate(uart_info->hu, uart_high_baud);
	}

	return 0;
}

/**
 * uart_set_chip_power() - Enable or disable the CG2900.
 * @chip_on:	true if chip shall be enabled, false otherwise.
 */
static void uart_set_chip_power(struct cg2900_chip_dev *dev, bool chip_on)
{
	int uart_baudrate = uart_default_baud;
	struct cg2900_platform_data *pf_data;
	struct uart_info *uart_info;

	pf_data = dev_get_platdata(dev->dev);
	uart_info = dev_get_drvdata(dev->dev);

	dev_info(MAIN_DEV, "Set chip power: %s\n",
		    (chip_on ? "ENABLE" : "DISABLE"));

	/* Cancel any ongoing works.*/
	cancel_work_sync(&uart_info->wakeup_work.work);
	cancel_delayed_work_sync(&uart_info->sleep_work.work);

	mutex_lock(&uart_info->sleep_state_lock);

	if (!uart_info->hu) {
		dev_err(MAIN_DEV, "Hci uart struct is not allocated\n");
		goto unlock;
	}

	if (chip_on) {
		if (!uart_info->suspend_blocked) {
			wake_lock(&uart_info->wake_lock);
			uart_info->suspend_blocked = true;
			pm_qos_update_request(&uart_info->pm_qos_latency,
					      CG2900_PM_QOS_LATENCY);
		}
		if (uart_info->sleep_state != CHIP_POWERED_DOWN) {
			dev_err(MAIN_DEV, "Chip is already powered up (%d)\n",
				uart_info->sleep_state);
			goto unlock;
		}

		if (cg2900_enable_regulator(uart_info))
			goto unlock;

		if (pf_data->enable_chip) {
			pf_data->enable_chip(dev);
			dev_dbg(MAIN_DEV, "New sleep_state: CHIP_AWAKE\n");
			uart_info->sleep_state = CHIP_AWAKE;
		}

		(void)hci_uart_set_baudrate(uart_info->hu, uart_baudrate);

		hci_uart_flow_ctrl(uart_info->hu, FLOW_ON);
		hci_uart_set_break(uart_info->hu, BREAK_OFF);
		udelay(UART_MIN_BREAK_OFF_TIME);
	} else {
		/* Turn off the chip.*/
		switch (uart_info->sleep_state) {
		case CHIP_AWAKE:
			break;
		case CHIP_FALLING_ASLEEP:
			hci_uart_set_break(uart_info->hu, BREAK_OFF);
			udelay(UART_MIN_BREAK_OFF_TIME);
			break;
		case CHIP_SUSPENDED:
		case CHIP_ASLEEP:
			unset_cts_irq(uart_info);
			enable_uart_pins(uart_info);
			break;
		case CHIP_POWERED_DOWN:
			dev_err(MAIN_DEV, "Chip is already powered down (%d)\n",
					uart_info->sleep_state);
			goto unlock;
		default:
			break;
		}

		if (uart_info->suspend_blocked) {
			wake_unlock(&uart_info->wake_lock);
			uart_info->suspend_blocked = false;
			pm_qos_update_request(&uart_info->pm_qos_latency,
					      PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE);
		}

		if (pf_data->disable_chip) {
			pf_data->disable_chip(dev);
			dev_dbg(MAIN_DEV,
				"New sleep_state: CHIP_POWERED_DOWN\n");
			uart_info->sleep_state = CHIP_POWERED_DOWN;
			/*
			 * This is to ensure when chip is switched
			 * on next time sleep_flags is again set with
			 * SLEEP_CORE_READY when startup is done properly
			 */
			clear_bit(SLEEP_CORE_READY, &uart_info->sleep_flags);
		}

		cg2900_disable_regulator(uart_info);
		/*
		 * Setting baud rate to 0 will tell UART driver to shut off its
		 * clocks.
		 */
		(void)hci_uart_set_baudrate(uart_info->hu, ZERO_BAUD_RATE);

		spin_lock_bh(&uart_info->rx_skb_lock);
		if (uart_info->rx_skb) {
			/*
			 * Reset the uart_info state so that
			 * next packet can be handled
			 * correctly by driver.
			 */
			dev_dbg(MAIN_DEV, "Power off in the middle of data receiving?"
							"Reseting state machine.\n");
			kfree_skb(uart_info->rx_skb);
			uart_info->rx_skb = NULL;
			uart_info->rx_state = W4_PACKET_TYPE;
			uart_info->rx_count = 0;
		}
		spin_unlock_bh(&uart_info->rx_skb_lock);
	}

unlock:
	mutex_unlock(&(uart_info->sleep_state_lock));
}

/**
 * uart_chip_startup_finished() - CG2900 startup finished.
 * @dev:	Transport device information.
 */
static void uart_chip_startup_finished(struct cg2900_chip_dev *dev)
{
	struct uart_info *uart_info = dev_get_drvdata(dev->dev);
	unsigned long timeout_jiffies = get_sleep_timeout(uart_info);

	/*
	 * Chip startup is done, now chip is allowed
	 * to go to sleep
	 */
	set_bit(SLEEP_CORE_READY, &uart_info->sleep_flags);
	/* Schedule work to put the chip and transport to sleep. */
	if (timeout_jiffies)
		queue_delayed_work(uart_info->wq, &uart_info->sleep_work.work,
				timeout_jiffies);
}
/**
 * uart_close() - Close the CG2900 UART for data transfers.
 * @dev:	Transport device information.
 *
 * Returns:
 *   0 if there is no error.
 */
static int uart_close(struct cg2900_chip_dev *dev)
{
	/* The chip is already shut down. Power off the chip. */
	uart_set_chip_power(dev, false);
	return 0;
}

/**
 * send_skb_to_core() - Sends packet received from UART to CG2900 Core.
 * @skb:	Received data packet.
 *
 * This function checks if UART is waiting for Command complete event,
 * see set_baud_rate.
 * If it is waiting it checks if it is the expected packet and the status.
 * If not is passes the packet to CG2900 Core.
 */
static void send_skb_to_core(struct uart_info *uart_info, struct sk_buff *skb)
{
	u8 status;

	if (!skb) {
		dev_err(MAIN_DEV, "send_skb_to_core: Received NULL as skb\n");
		return;
	}

	if (BAUD_WAITING == uart_info->baud_rate_state) {
		/*
		 * Should only really be one packet received now:
		 * the CmdComplete for the SetBaudrate command
		 * Let's see if this is the packet we are waiting for.
		 */
		u8 h4_channel = HCI_BT_EVT_H4_CHANNEL;
		if (use_device_channel_for_vs_cmd(
				uart_info->chip_dev.chip.hci_revision)) {
			h4_channel = HCI_DEV_MGMT_H4_CHANNEL;
		}
		if (!is_bt_cmd_complete_no_param(skb,
				CG2900_BT_OP_VS_SET_BAUD_RATE, &status,
				h4_channel)) {
			/*
			 * Received other event. Should not really happen,
			 * but pass the data to CG2900 Core anyway.
			 */
			dev_dbg(MAIN_DEV, "Sending packet to CG2900 Core while "
				"waiting for BaudRate CmdComplete\n");
			uart_info->chip_dev.c_cb.data_from_chip
				(&uart_info->chip_dev, skb);
			return;
		}

		/*
		 * We have received complete event for our baud rate
		 * change command
		 */
		if (HCI_BT_ERROR_NO_ERROR == status) {
			dev_dbg(MAIN_DEV, "Received baud rate change complete "
				"event OK\n");
			dev_dbg(MAIN_DEV,
				"New baud_rate_state: BAUD_SUCCESS\n");
			uart_info->baud_rate_state = BAUD_SUCCESS;
		} else {
			dev_err(MAIN_DEV,
				"Received baud rate change complete event "
				"with status 0x%X\n", status);
			dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_FAIL\n");
			uart_info->baud_rate_state = BAUD_FAIL;
		}
		wake_up_all(&uart_wait_queue);
		kfree_skb(skb);
	} else if (BAUD_SENDING_RESET == uart_info->baud_rate_state) {
		/*
		 * Should only really be one packet received now:
		 * the CmdComplete for the Reset command
		 * Let's see if this is the packet we are waiting for.
		 */
		if (!is_bt_cmd_complete_no_param(skb, HCI_OP_RESET, &status,
				HCI_BT_EVT_H4_CHANNEL)) {
			/*
			 * Received other event. Should not really happen,
			 * but pass the data to CG2900 Core anyway.
			 */
			dev_dbg(MAIN_DEV, "Sending packet to CG2900 Core while "
				"waiting for Reset CmdComplete\n");
			uart_info->chip_dev.c_cb.data_from_chip
					(&uart_info->chip_dev, skb);
			return;
		}

		/*
		 * We have received complete event for our baud rate
		 * change command
		 */
		if (HCI_BT_ERROR_NO_ERROR == status) {
			dev_dbg(MAIN_DEV,
				"Received HCI reset complete event OK\n");
			/*
			 * Go back to BAUD_IDLE since this was not really
			 * baud rate change but just a preparation of the chip
			 * to be ready to receive commands.
			 */
			dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_IDLE\n");
			uart_info->baud_rate_state = BAUD_IDLE;
		} else {
			dev_err(MAIN_DEV,
				"Received HCI reset complete event with "
				"status 0x%X", status);
			dev_dbg(MAIN_DEV, "New baud_rate_state: BAUD_FAIL\n");
			uart_info->baud_rate_state = BAUD_FAIL;
		}
		wake_up_all(&uart_wait_queue);
		kfree_skb(skb);
	} else {
		/* Just pass data to CG2900 Core */
		uart_info->chip_dev.c_cb.data_from_chip
						(&uart_info->chip_dev, skb);
	}
}

/**
 * check_data_len() - Check number of bytes to receive.
 * @len:	Number of bytes left to receive.
 */
static void check_data_len(struct uart_info *uart_info, int len)
{
	/* First get number of bytes left in the sk_buffer */
	register int room = skb_tailroom(uart_info->rx_skb);

	if (!len) {
		/* No data left to receive. Transmit to CG2900 Core */
		send_skb_to_core(uart_info, uart_info->rx_skb);
	} else if (len > room) {
		dev_err(MAIN_DEV, "Data length is too large (%d > %d)\n",
			len, room);
		kfree_skb(uart_info->rx_skb);
	} else {
		/*
		 * "Normal" case. Switch to data receiving state and store
		 * data length.
		 */
		uart_info->rx_state = W4_DATA;
		uart_info->rx_count = len;
		return;
	}

	uart_info->rx_state = W4_PACKET_TYPE;
	uart_info->rx_skb   = NULL;
	uart_info->rx_count = 0;
}

/**
 * work_restart_sleep() - Cancel pending sleep_work, wake-up driver and
 * schedule new sleep_work in a work context.
 * @work:	work which needs to be done.
 */
static void work_restart_sleep(struct work_struct *work)
{
	struct uart_work_struct *current_work =
		container_of(work, struct uart_work_struct, work);
	struct uart_info *uart_info = (struct uart_info *)current_work->data;
	unsigned long timeout_jiffies = get_sleep_timeout(uart_info);

	spin_lock_bh(&(uart_info->transmission_lock));
	uart_info->rx_in_progress = false;
	spin_unlock_bh(&(uart_info->transmission_lock));

	/* Cancel pending sleep work if there is any. */
	cancel_delayed_work_sync(&uart_info->sleep_work.work);

	wake_up_chip(uart_info);

	spin_lock_bh(&(uart_info->transmission_lock));
	/*
	 * If there are no ongoing transfers schedule the sleep work.
	 */
	if (!(uart_info->tx_in_progress) && timeout_jiffies)
		queue_delayed_work(uart_info->wq,
				&uart_info->sleep_work.work,
				timeout_jiffies);
	spin_unlock_bh(&(uart_info->transmission_lock));
}

/**
 * cg2900_hu_receive() - Handles received UART data.
 * @data:	Data received
 * @count:	Number of bytes received
 *
 * The cg2900_hu_receive() function handles received UART data and puts it
 * together to one complete packet.
 *
 * Returns:
 *   Number of bytes not handled, i.e. 0 = no error.
 */
static int cg2900_hu_receive(struct hci_uart *hu,
					void *data, int count)
{
	const u8 *r_ptr;
	u8 *w_ptr;
	int len;
	struct hci_event_hdr	*evt;
	struct hci_acl_hdr	*acl;
	struct nfc_hci_hdr	*nfc;
	struct ant_cmd_hci_hdr	*ant_cmd;
	struct ant_dat_hci_hdr	*ant_dat;
	union fm_leg_evt_or_irq	*fm;
	struct gnss_hci_hdr	*gnss;
	struct uart_info *uart_info = dev_get_drvdata(hu->proto->dev);
	u8 *tmp;

	r_ptr = (const u8 *)data;

	spin_lock_bh(&(uart_info->transmission_lock));
	/* Mark that there is an ongoing transfer. */
	uart_info->rx_in_progress = true;
	spin_unlock_bh(&(uart_info->transmission_lock));

	/* Cancel pending sleep work if there is any. */
	cancel_delayed_work(&uart_info->sleep_work.work);

	if (uart_debug)
		print_hex_dump_bytes(NAME " RX:\t", DUMP_PREFIX_NONE,
				     data, count);

	spin_lock_bh(&uart_info->rx_skb_lock);

	/* Continue while there is data left to handle */
	while (count) {
		/*
		 * If we have already received a packet we know how many bytes
		 * there are left.
		 */
		if (!uart_info->rx_count)
			goto check_h4_header;

		/* First copy received data into the skb_rx */
		len = min_t(unsigned int, uart_info->rx_count, count);
		memcpy(skb_put(uart_info->rx_skb, len), r_ptr, len);
		/* Update counters from the length and step the data pointer */
		uart_info->rx_count -= len;
		count -= len;
		r_ptr += len;

		if (uart_info->rx_count)
			/*
			 * More data to receive to current packet. Break and
			 * wait for next data on the UART.
			 */
			break;

		/* Handle the different states */
		tmp = uart_info->rx_skb->data + CG2900_SKB_RESERVE;
		switch (uart_info->rx_state) {
		case W4_DATA:
			/*
			 * Whole data packet has been received.
			 * Transmit it to CG2900 Core.
			 */
			send_skb_to_core(uart_info, uart_info->rx_skb);

			uart_info->rx_state = W4_PACKET_TYPE;
			uart_info->rx_skb = NULL;
			continue;

		case W4_DBG_HDR:
		case W4_EVENT_HDR:
			evt = (struct hci_event_hdr *)tmp;
			check_data_len(uart_info, evt->plen);
			/* Header read. Continue with next bytes */
			continue;

		case W4_ACL_HDR:
			acl = (struct hci_acl_hdr *)tmp;
			check_data_len(uart_info, le16_to_cpu(acl->dlen));
			/* Header read. Continue with next bytes */
			continue;

		case W4_NFC_HDR:
			nfc = (struct nfc_hci_hdr *)tmp;
			/*
			 * NFC Packet(s) have 1 extra byte for checksum.
			 * This is not indicated by the byte length determined
			 * from the received NFC Packet over HCI. So
			 * length of received NFC packet should be updated
			 * accordingly.
			 */
			check_data_len(uart_info, le16_to_cpu(nfc->plen) +
					NFC_CHECKSUM_DATA_LEN);
			/* Header read. Continue with next bytes */
			continue;

		case W4_ANT_CMD_HDR:
			ant_cmd = (struct ant_cmd_hci_hdr *)tmp;
			check_data_len(uart_info, ant_cmd->plen);
			/* Header read. Continue with next bytes */
			continue;

		case W4_ANT_DAT_HDR:
			ant_dat = (struct ant_dat_hci_hdr *)tmp;
			check_data_len(uart_info, ant_dat->plen);
			/* Header read. Continue with next bytes */
			continue;

		case W4_FM_RADIO_HDR:
			fm = (union fm_leg_evt_or_irq *)tmp;
			check_data_len(uart_info, fm->param_length);
			/* Header read. Continue with next bytes */
			continue;

		case W4_GNSS_HDR:
			gnss = (struct gnss_hci_hdr *)tmp;
			check_data_len(uart_info, le16_to_cpu(gnss->plen));
			/* Header read. Continue with next bytes */
			continue;

		case W4_DEV_MGMT_HDR:
			/*
			 * Device management events are similar to
			 * BT HCI Header
			 */
			evt = (struct hci_event_hdr *)tmp;
			check_data_len(uart_info, evt->plen);
			/* Header read. Continue with next bytes */
			continue;

		default:
			dev_err(MAIN_DEV,
				"Bad state indicating memory overwrite "
				"(0x%X)\n", (u8)(uart_info->rx_state));
			break;
		}

check_h4_header:
		/* Check which H:4 packet this is and update RX states */
		if (*r_ptr == HCI_BT_EVT_H4_CHANNEL) {
			uart_info->rx_state = W4_EVENT_HDR;
			uart_info->rx_count = HCI_BT_EVT_HDR_SIZE;
		} else if (*r_ptr == HCI_BT_ACL_H4_CHANNEL) {
			uart_info->rx_state = W4_ACL_HDR;
			uart_info->rx_count = HCI_BT_ACL_HDR_SIZE;
		} else if (*r_ptr == HCI_NFC_H4_CHANNEL) {
			uart_info->rx_state = W4_NFC_HDR;
			uart_info->rx_count = HCI_NFC_HDR_SIZE;
		} else if (*r_ptr == HCI_ANT_CMD_H4_CHANNEL) {
			uart_info->rx_state = W4_ANT_CMD_HDR;
			uart_info->rx_count = HCI_ANT_CMD_HDR_SIZE;
		} else if (*r_ptr == HCI_ANT_DAT_H4_CHANNEL) {
			uart_info->rx_state = W4_ANT_DAT_HDR;
			uart_info->rx_count = HCI_ANT_DAT_HDR_SIZE;
		} else if (*r_ptr == HCI_FM_RADIO_H4_CHANNEL) {
			uart_info->rx_state = W4_FM_RADIO_HDR;
			uart_info->rx_count = HCI_FM_RADIO_HDR_SIZE;
		} else if (*r_ptr == HCI_GNSS_H4_CHANNEL) {
			uart_info->rx_state = W4_GNSS_HDR;
			uart_info->rx_count = HCI_GNSS_HDR_SIZE;
		} else if (*r_ptr == HCI_DEBUG_H4_CHANNEL) {
			uart_info->rx_state = W4_DBG_HDR;
			uart_info->rx_count = HCI_BT_EVT_HDR_SIZE;
		} else if (*r_ptr & HCI_DEV_MGMT_CHANNEL_BIT) {
			uart_info->rx_state = W4_DEV_MGMT_HDR;
			uart_info->rx_count = HCI_DEV_MGMT_HDR_SIZE;
		} else {
			dev_err(MAIN_DEV, "Unknown HCI packet type 0x%X\n",
				(u8)*r_ptr);
			r_ptr++;
			count--;
			continue;
		}

		/*
		 * Allocate packet. We do not yet know the size and therefore
		 * allocate max size.
		 */
		uart_info->rx_skb = alloc_rx_skb(RX_SKB_MAX_SIZE, GFP_ATOMIC);
		if (!uart_info->rx_skb) {
			dev_err(MAIN_DEV,
				"Can't allocate memory for new packet\n");
			uart_info->rx_state = W4_PACKET_TYPE;
			uart_info->rx_count = 0;

			spin_lock_bh(&(uart_info->transmission_lock));
			uart_info->rx_in_progress = false;
			spin_unlock_bh(&(uart_info->transmission_lock));

			spin_unlock_bh(&uart_info->rx_skb_lock);
			return 0;
		}

		/* Write the H:4 header first in the sk_buffer */
		w_ptr = skb_put(uart_info->rx_skb, 1);
		*w_ptr = *r_ptr;

		/* First byte (H4 header) read. Goto next byte */
		r_ptr++;
		count--;
	}

	(void)queue_work(uart_info->wq, &uart_info->restart_sleep_work.work);

	spin_unlock_bh(&uart_info->rx_skb_lock);
	return count;
}

/**
 * cg2900_hu_open() - Called when UART line discipline changed to N_HCI.
 * @hu:	Pointer to associated Hci uart structure.
 *
 * Returns:
 *   0 if there is no error.
 *   Errors from cg2900_register_trans_driver.
 */
static int cg2900_hu_open(struct hci_uart *hu)
{
	int err;
	struct uart_info *uart_info = dev_get_drvdata(hu->proto->dev);

	if (!uart_info)
		return -EACCES;

	dev_info(MAIN_DEV, "UART opened\n");

	skb_queue_head_init(&uart_info->tx_queue);

	uart_info->hu = hu;

	/* Tell CG2900 Core that UART is connected */
	err = cg2900_register_trans_driver(&uart_info->chip_dev);
	if (err)
		dev_err(MAIN_DEV, "Could not register transport driver (%d)\n",
			err);

	if (hu->tty->ops->tiocmget && hu->tty->ops->break_ctl)
		set_bit(SLEEP_TTY_READY, &uart_info->sleep_flags);
	else {
		dev_err(MAIN_DEV, "Sleep mode not available\n");
		clear_bit(SLEEP_TTY_READY, &uart_info->sleep_flags);
	}

	return err;

}

/**
 * cg2900_hu_close() - Close UART tty.
 * @hu:	Pointer to associated hci_uart structure.
 *
 * The uart_tty_close() function is called when the line discipline is changed
 * to something else, the TTY is closed, or the TTY detects a hangup.
 */
static int cg2900_hu_close(struct hci_uart *hu)
{
	int err;
	struct uart_info *uart_info = dev_get_drvdata(hu->proto->dev);


	BUG_ON(!uart_info);
	BUG_ON(!uart_info->wq);

	clear_bit(SLEEP_TTY_READY, &uart_info->sleep_flags);

	/* Purge any stored sk_buffers */
	skb_queue_purge(&uart_info->tx_queue);

	spin_lock_bh(&uart_info->rx_skb_lock);
	if (uart_info->rx_skb) {
		kfree_skb(uart_info->rx_skb);
		uart_info->rx_skb = NULL;
	}
	spin_unlock_bh(&uart_info->rx_skb_lock);

	dev_info(MAIN_DEV, "UART closed\n");
	err = create_work_item(uart_info, work_hw_deregistered);
	if (err)
		dev_err(MAIN_DEV, "Failed to create work item (%d) "
			"work_hw_deregistered\n", err);

	uart_info->hu = NULL;

	return 0;
}

/**
 * cg2900_hu_dequeue() - Get new skbuff.
 * @hu: Pointer to associated hci_uart structure.
 *
 * The uart_tty_close() function is called when the line discipline is changed
 * to something else, the TTY is closed, or the TTY detects a hangup.
 */
static struct sk_buff *cg2900_hu_dequeue(struct hci_uart *hu)
{
	struct sk_buff *skb;
	struct uart_info *uart_info = dev_get_drvdata(hu->proto->dev);
	unsigned long timeout_jiffies = get_sleep_timeout(uart_info);

	spin_lock_bh(&(uart_info->transmission_lock));

	skb = skb_dequeue(&uart_info->tx_queue);

	if (!skb)
		uart_info->tx_in_progress = false;

	/*
	 * If there are no ongoing transfers schedule the sleep work.
	 */
	if (!(uart_info->rx_in_progress) && timeout_jiffies && !skb)
		queue_delayed_work(uart_info->wq,
				&uart_info->sleep_work.work,
				timeout_jiffies);

	spin_unlock_bh(&(uart_info->transmission_lock));

	if (BAUD_SENDING == uart_info->baud_rate_state && !skb)
		finish_setting_baud_rate(hu);
	/*
	 * If it's set baud rate cmd set correct baud state and after
	 * sending is finished inform the tty driver about the new
	 * baud rate.
	 */
	if ((BAUD_START == uart_info->baud_rate_state) &&
		skb && (is_set_baud_rate_cmd(skb->data))) {
		dev_dbg(MAIN_DEV, "UART set baud rate cmd found\n");
		uart_info->baud_rate_state = BAUD_SENDING;
	}

	if (uart_debug && skb)
		print_hex_dump_bytes(NAME " TX:\t", DUMP_PREFIX_NONE,
				     skb->data, skb->len);

	return skb;
}

/**
 * cg2900_hu_flush() - Flush buffers.
 * @hu: Pointer to associated hci_uart structure.
 *
 */
static int cg2900_hu_flush(struct hci_uart *hu)
{
	struct uart_info *uart_info = dev_get_drvdata(hu->proto->dev);

	dev_dbg(MAIN_DEV, "ui %p", uart_info);
	skb_queue_purge(&uart_info->tx_queue);
	return 0;
}

/**
 * cg2900_uart_probe() - Initialize CG2900 UART resources.
 * @pdev:	Platform device.
 *
 * This function initializes the module and registers to the UART framework.
 *
 * Returns:
 *   0 if success.
 *   -ENOMEM for failed alloc or structure creation.
 *   -ECHILD for failed work queue creation.
 *   Error codes generated by tty_register_ldisc.
 */
static int __devinit cg2900_uart_probe(struct platform_device *pdev)
{
	int err = 0;
	struct uart_info *uart_info;
	struct hci_uart_proto *p;
	struct resource *resource;

	pr_debug("cg2900_uart_probe");

	uart_info = kzalloc(sizeof(*uart_info), GFP_KERNEL);
	if (!uart_info) {
		pr_err("Couldn't allocate uart_info");
		return -ENOMEM;
	}

	uart_info->sleep_state = CHIP_POWERED_DOWN;
	mutex_init(&(uart_info->sleep_state_lock));

	spin_lock_init(&(uart_info->transmission_lock));
	spin_lock_init(&(uart_info->rx_skb_lock));

	uart_info->chip_dev.t_cb.open = uart_open;
	uart_info->chip_dev.t_cb.close = uart_close;
	uart_info->chip_dev.t_cb.write = uart_write;
	uart_info->chip_dev.t_cb.set_chip_power = uart_set_chip_power;
	uart_info->chip_dev.t_cb.chip_startup_finished =
			uart_chip_startup_finished;
	uart_info->chip_dev.t_cb.set_baud_rate =
			uart_set_baud_rate;
	uart_info->chip_dev.pdev = pdev;
	uart_info->chip_dev.dev = &pdev->dev;
	uart_info->chip_dev.t_data = uart_info;
	wake_lock_init(&uart_info->wake_lock, WAKE_LOCK_SUSPEND, NAME);
	uart_info->suspend_blocked = false;

	pm_qos_add_request(&uart_info->pm_qos_latency, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE);

	resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
						"cts_irq");
	if (!resource) {
		dev_err(&pdev->dev, "CTS IRQ does not exist\n");
		err = -EINVAL;
		goto error_handling_free;
	}
	uart_info->cts_irq = resource->start;

	resource = platform_get_resource_byname(pdev, IORESOURCE_IO,
						"cts_gpio");
	if (!resource) {
		dev_err(&pdev->dev, "CTS GPIO does not exist\n");
		err = -EINVAL;
		goto error_handling_free;
	}
	uart_info->cts_gpio = resource->start;

	/* Init UART TX work queue */
	uart_info->wq = create_singlethread_workqueue(UART_WQ_NAME);
	if (!uart_info->wq) {
		dev_err(MAIN_DEV, "Could not create workqueue\n");
		err = -ECHILD; /* No child processes */
		goto error_handling_free;
	}

	/* Initialize sleep work data */
	uart_info->sleep_work.data = uart_info;
	INIT_DELAYED_WORK(&uart_info->sleep_work.work, set_chip_sleep_mode);

	/* Initialize wake-up work data */
	uart_info->wakeup_work.data = uart_info;
	INIT_WORK(&uart_info->wakeup_work.work, work_wake_up_chip);

	/* Initialize after_receive work data */
	uart_info->restart_sleep_work.data = uart_info;
	INIT_WORK(&uart_info->restart_sleep_work.work, work_restart_sleep);

	uart_info->dev = &pdev->dev;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		dev_err(MAIN_DEV, "cg2900_uart_probe: Could not allocate p\n");
		goto error_handling_wq;
	}

	p->dev		= uart_info->dev;
	p->id		= HCI_UART_STE;
	p->open		= &cg2900_hu_open;
	p->close	= &cg2900_hu_close;
	p->recv		= &cg2900_hu_receive;
	p->dequeue	= &cg2900_hu_dequeue;
	p->flush	= &cg2900_hu_flush;

	dev_set_drvdata(uart_info->dev, (void *)uart_info);

	err = hci_uart_register_proto(p);
	if (err) {
		dev_err(MAIN_DEV, "cg2900_uart_probe: Can not register "
			"protocol\n");
		kfree(p);
		goto error_handling_wq;
	}

	goto finished;

error_handling_wq:
	destroy_workqueue(uart_info->wq);
error_handling_free:
	wake_lock_destroy(&uart_info->wake_lock);
	kfree(uart_info);
	uart_info = NULL;
finished:
	return err;
}

/**
 * cg2900_uart_remove() - Release CG2900 UART resources.
 * @pdev:	Platform device.
 *
 * Returns:
 *   0 if success.
 *   Error codes generated by tty_unregister_ldisc.
 */
static int __devexit cg2900_uart_remove(struct platform_device *pdev)
{
	struct uart_info *uart_info = dev_get_drvdata(&pdev->dev);

	pr_debug("cg2900_uart_remove");

	if (!uart_info)
		return -ECHILD;

	if (uart_info->hu)
		hci_uart_unregister_proto(uart_info->hu->proto);

	pm_qos_remove_request(&uart_info->pm_qos_latency);
	wake_lock_destroy(&uart_info->wake_lock);
	destroy_workqueue(uart_info->wq);

	dev_info(MAIN_DEV, "CG2900 UART removed\n");
	kfree(uart_info);
	uart_info = NULL;
	return 0;
}

static struct platform_driver cg2900_uart_driver = {
	.driver = {
		.name	= "cg2900-uart",
		.owner	= THIS_MODULE,
	},
	.probe	= cg2900_uart_probe,
	.remove	= __devexit_p(cg2900_uart_remove),
#ifdef CONFIG_PM
	.suspend = cg2900_uart_suspend,
	.resume = cg2900_uart_resume
#endif
};


/**
 * cg2900_uart_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init cg2900_uart_init(void)
{
	pr_debug("cg2900_uart_init");
	return platform_driver_register(&cg2900_uart_driver);
}

/**
 * cg2900_uart_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit cg2900_uart_exit(void)
{
	pr_debug("cg2900_uart_exit");
	platform_driver_unregister(&cg2900_uart_driver);
}

module_init(cg2900_uart_init);
module_exit(cg2900_uart_exit);

module_param(uart_default_baud, int, S_IRUGO);
MODULE_PARM_DESC(uart_default_baud,
		 "Default UART baud rate, e.g. 115200. If not set 115200 will "
		 "be used.");

module_param(uart_high_baud, int, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(uart_high_baud,
		 "High speed UART baud rate, e.g. 4000000.  If not set 3000000 "
		 "will be used.");

module_param(uart_debug, int, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(uart_debug, "Enable/Disable debug. 0 means Debug disabled.");
MODULE_AUTHOR("Par-Gunnar Hjalmdahl ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ST-Ericsson CG2900 UART Driver");
