/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Linux FM Driver for CG2900 FM Chip
 *
 * Author: Hemant Gupta <hemant.gupta@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/device.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <asm-generic/errno-base.h>
#include "cg2900.h"
#include "cg2900_fm_driver.h"

/*
 * Macro for printing the HCI Packet received from Protocol Driver
 * to FM Driver.
 */
#define CG2900_HEX_READ_PACKET_DUMP \
	if (cg2900_fm_debug_level == FM_HCI_PACKET_LOGS) \
		fmd_hexdump('<', skb->data, skb->len);

/* Macro for printing the HCI Packet sent to Protocol Driver from FM Driver */
#define CG2900_HEX_WRITE_PACKET_DUMP \
	if (cg2900_fm_debug_level == FM_HCI_PACKET_LOGS) \
		fmd_hexdump('>', send_buffer, num_bytes);

/* Converts the given value to ASCII format*/
#define ASCVAL(x)(((x) <= 9) ? (x) + '0' : (x) - 10 + 'a')

/* The receive Packet's 1st byte indicates the packet length */
#define FM_GET_PKT_LEN(__data) __data[0]

/* The receive Packet's following bytes are the actual data. */
#define FM_GET_RSP_PKT_ADDR(__data) (&__data[1])

/*
 * LSB consists of shifting the command Id by 3 bits
 * to left and ORING with the number of parameters of the
 * command.
 */
#define FM_CMD_GET_LSB(__cmd_id, __num_param) \
		((u8)(((__cmd_id << 3) & 0x00FF) | __num_param))

/* MSB consists of shifting the command Id by 5 bits to right. */
#define FM_CMD_GET_MSB(__cmd_id) \
		((u8)(__cmd_id >> 5))

/*
 * Command id is mapped as shifting the MSB 5 bits to left and
 * ORING with LSB shifted 3 bits to right.
 */
#define FM_GET_CMD_ID(__data) \
		((u16)((__data[2] << 5) | __data[1] >> 3))

/*
 * Number of parameters in the response packet are the last 3 bits
 * of the 1st byte of the received packet.
 */
#define FM_GET_NUM_PARAMS(__data) \
		((u16)((__data[1] & 0x07)))

/* Function Id is mapped to the 1st byte of the received packet */
#define FM_GET_FUNCTION_ID(__data) __data[0]

/*
 * Block Id of the FM Firmware downloaded is mapped to the
 *  2nd byte of the received packet.
 */
#define FM_GET_BLOCK_ID(__data) __data[1]

/* Status of the received packet is mapped to the 4th byte. */
#define FM_GET_STATUS(__data) __data[3]

/*
 * For PG1 of CG2900, the FM Interrupt is mapped
 * to the 3rd and 4th byte of the received packet.
 */
#define FM_GET_PGI_INTERRUPT(__data) \
		((u16)(__data[3] << 8 | __data[2]))

/*
 * For PG2 of CG2900, the FM Interrupt is mapped
 * to the 5th and 6th byte of the received packet.
 */
#define FM_GET_PG2_INTERRUPT(__data) \
		((u16)(__data[5] << 8 | __data[4]))

#define FM_GET_NUM_RDS_GRPS(__data) __data[0]

/* Response buffer starts from the 4th byte if the response buffer */
#define FM_GET_RSP_BUFFER_ADDR(__data) (&__data[3])

/* FM Function buffer starts from the 5th byte if the response buffer */
#define FM_GET_FUNCTION_ADDR(__data) (&__data[4])

/*
 * Maximum time for chip to respond including the Command
 * Competion interrupts for some commands. This time has been
 * adjusted to cater to increased communication time with chip
 * when debug level is set to 4.
 */
#define MAX_RESPONSE_TIME_IN_MS	5000

/* Byte per word */
#define WORD_LENGTH				2

/* Byte Offset Counter */
#define COUNTER					1

/* Binary shift offset for one byte */
#define SHIFT_OFFSET			8

/*
 * enum fmd_gocmd_t - FM Driver Command state.
 *
 * @FMD_STATE_NONE: FM Driver in Idle state
 * @FMD_STATE_MODE: FM Driver in setmode state
 * @FMD_STATE_FREQUENCY: FM Driver in Set frequency state.
 * @FMD_STATE_PA: FM Driver in SetPA state.
 * @FMD_STATE_PA_LEVEL: FM Driver in Setpalevl state.
 * @FMD_STATE_ANTENNA: FM Driver in Setantenna state
 * @FMD_STATE_MUTE: FM Driver in Setmute state
 * @FMD_STATE_SEEK: FM Driver in seek mode
 * @FMD_STATE_SEEK_STOP: FM Driver in seek stop level state.
 * @FMD_STATE_SCAN_BAND: FM Driver in Scanband mode
 * @FMD_STATE_TX_SET_CTRL: FM Driver in RDS control state
 * @FMD_STATE_TX_SET_THRSHLD: FM Driver in RDS threshld state
 * @FMD_STATE_GEN_POWERUP: FM Driver in Power UP state.
 * @FMD_STATE_SELECT_REF_CLK: FM Driver in Select Reference clock state.
 * @FMD_STATE_SET_REF_CLK_PLL: FM Driver in Set Reference Freq state.
 * @FMD_STATE_BLOCK_SCAN: FM Driver in Block Scan state.
 * @FMD_STATE_AF_UPDATE: FM Driver in AF Update State.
 * @FMD_STATE_AF_SWITCH: FM Driver in AF Switch State.
 * @FMD_STATE_LAST: Last State of FM Driver
 *
 * Various states of the FM driver.
 */
enum fmd_gocmd {
	FMD_STATE_NONE,
	FMD_STATE_MODE,
	FMD_STATE_FREQUENCY,
	FMD_STATE_PA,
	FMD_STATE_PA_LEVEL,
	FMD_STATE_ANTENNA,
	FMD_STATE_MUTE,
	FMD_STATE_SEEK,
	FMD_STATE_SEEK_STOP,
	FMD_STATE_SCAN_BAND,
	FMD_STATE_TX_SET_CTRL,
	FMD_STATE_TX_SET_THRSHLD,
	FMD_STATE_GEN_POWERUP,
	FMD_STATE_SELECT_REF_CLK,
	FMD_STATE_SET_REF_CLK_PLL,
	FMD_STATE_BLOCK_SCAN,
	FMD_STATE_AF_UPDATE,
	FMD_STATE_AF_SWITCH,
	FMD_STATE_LAST
};

/**
 * struct fmd_rdsgroup_t - Rds group structure.
 *
 * @block: Array for RDS Block(s) received.
 * @status: Array of Status of corresponding RDS block(s).
 *
 * It stores the value and status of a particular RDS group
 * received.
 */
struct fmd_rds_group {
	u16 block[NUM_OF_RDS_BLOCKS];
	u8  status[NUM_OF_RDS_BLOCKS];
};

/**
 * struct fmd_states_info - Main FM state info structure.
 *
 * @fmd_initialized: Flag indicating FM Driver is initialized or not
 * @rx_freq_range: Receiver freq range
 * @rx_volume:  Receiver volume level
 * @rx_antenna: Receiver Antenna
 * @rx_seek_stop_level:  RDS seek stop Level
 * @rx_rds_on:  Receiver RDS ON
 * @rx_stereo_mode: Receiver Stereo mode
 * @max_channels_to_scan: Maximum Number of channels to Scan.
 * @tx_freq_range: Transmitter freq Range
 * @tx_preemphasis: Transmitter Pre emphiasis level
 * @tx_stereo_mode: Transmitter stero mode
 * @tx_rds_on:  Enable RDS
 * @tx_pilot_dev: PIlot freq deviation
 * @tx_rds_dev:  RDS deviation
 * @tx_strength:  TX Signal Stregnth
 * @irq_index:  Index where last interrupt is added to Interrupt queue
 * @interrupt_available_for_processing:  Flag indicating if interrupt is
 * available for processing or not.
 * @interrupt_queue: Circular Queue to store the received interrupt from chip.
 * @gocmd:  Command which is in progress.
 * @mode:  Current Mode of FM Radio.
 * @rds_group:  Array of RDS group Buffer
 * @callback: Callback registered by upper layers.
 */
struct fmd_states_info {
	bool		fmd_initialized;
	u8			rx_freq_range;
	u8			rx_volume;
	u8			rx_antenna;
	u16			rx_seek_stop_level;
	bool		rx_rds_on;
	u8			rx_stereo_mode;
	u8			tx_freq_range;
	u8			tx_preemphasis;
	bool		tx_stereo_mode;
	u8			max_channels_to_scan;
	bool		tx_rds_on;
	u16			tx_pilot_dev;
	u16			tx_rds_dev;
	u16			tx_strength;
	u8			irq_index;
	bool		interrupt_available_for_processing;
	u16			interrupt_queue[MAX_COUNT_OF_IRQS];
	enum fmd_gocmd gocmd;
	enum fmd_mode mode;
	struct fmd_rds_group	rds_group[MAX_RDS_GROUPS];
	fmd_radio_cb callback;
};

/**
 * struct fmd_data - Main structure for FM data exchange.
 *
 * @cmd_id: Command Id of the command being exchanged.
 * @num_parameters: Number of parameters
 * @parameters: FM data parameters.
 */
struct fmd_data {
	u32 cmd_id;
	u16 num_parameters;
	u8 parameters[MAX_RESP_SIZE];
};

static struct fmd_states_info fmd_state_info;
static struct fmd_data fmd_data;
static struct semaphore cmd_sem;
static struct semaphore rds_sem;
static struct semaphore interrupt_sem;
static struct task_struct *rds_thread_task;
static struct task_struct *irq_thread_task;
static struct device *cg2900_fm_dev;
static struct mutex write_mutex;
static struct mutex send_cmd_mutex;
static spinlock_t fmd_spinlock;
static spinlock_t fmd_spinlock_read;

/* Debug Level
 * 1: Only Error Logs
 * 2: Info Logs
 * 3: Debug Logs
 * 4: HCI Logs
 */
unsigned short cg2900_fm_debug_level = FM_ERROR_LOGS;
EXPORT_SYMBOL(cg2900_fm_debug_level);

static cg2900_fm_rds_cb cb_rds_func;
static bool rds_thread_required;
static bool irq_thread_required;

static char event_name[FMD_EVENT_LAST_ELEMENT][MAX_NAME_SIZE] = {
	"FMD_EVENT_OPERATION_COMPLETED",
	"FMD_EVENT_ANTENNA_STATUS_CHANGED",
	"FMD_EVENT_FREQUENCY_CHANGED",
	"FMD_EVENT_SEEK_COMPLETED",
	"FMD_EVENT_SCAN_BAND_COMPLETED",
	"FMD_EVENT_BLOCK_SCAN_COMPLETED",
	"FMD_EVENT_AF_UPDATE_SWITCH_COMPLETE",
	"FMD_EVENT_MONO_STEREO_TRANSITION_COMPLETE",
	"FMD_EVENT_SEEK_STOPPED",
	"FMD_EVENT_GEN_POWERUP",
	"FMD_EVENT_RDSGROUP_RCVD",
};

static char interrupt_name[MAX_COUNT_OF_IRQS][MAX_NAME_SIZE] = {
	"IRPT_OPERATION_SUCCEEDED",
	"IRPT_OPERATION_FAILED",
	"IRPT_NOT_DEFINED",
	"IRPT_RX_BUFFER_FULL_OR_TX_BUFFER_EMPTY",
	"IRPT_RX_SIGNAL_QUALITY_LOW_OR_TX_MUTE_STATUS_CHANGED",
	"IRPT_MONO_STEREO_TRANSITION",
	"IRPT_RX_RDS_SYNC_FOUND_OR_TX_INPUT_OVERDRIVE",
	"IRPT_RDS_SYNC_LOST",
	"IRPT_PI_CODE_CHANGED",
	"IRPT_REQUESTED_BLOCK_AVAILABLE",
	"IRPT_NOT_DEFINED",
	"IRPT_NOT_DEFINED",
	"IRPT_NOT_DEFINED",
	"IRPT_NOT_DEFINED",
	"IRPT_WARM_BOOT_READY",
	"IRPT_COLD_BOOT_READY",
};


static void fmd_hexdump(
			char prompt,
			u8 *buffer,
			int num_bytes
			);
static u8 fmd_get_event(
			enum fmd_gocmd	gocmd
			);
static void fmd_event_name(
			u8 event,
			char *event_name
			);
static char *fmd_get_fm_function_name(
			u8 fm_function
			);
static void fmd_interrupt_name(
			u16 interrupt,
			char *interrupt_name
			);
static void fmd_add_interrupt_to_queue(
			u16 interrupt
			);
static void fmd_process_interrupt(
			u16 interrupt
			);
static void fmd_callback(
			u8 event,
			bool event_successful
			);
static int fmd_rx_frequency_to_channel(
			u32 freq,
			u16 *channel
			);
static int fmd_rx_channel_to_frequency(
			u16 channel_number,
			u32 *frequency
			);
static int fmd_tx_frequency_to_channel(
			u32 freq,
			u16 *channel
			);
static int fmd_tx_channel_to_frequency(
			u16 channel_number,
			u32 *frequency
			);
static bool fmd_go_cmd_busy(void);
static int fmd_send_cmd_and_read_resp(
			const u16 cmd_id,
			const u16 num_parameters,
			const u16 *parameters,
			u16 *resp_num_parameters,
			u16 *resp_parameters
			);
static int fmd_send_cmd(
			const u16 cmd_id,
			const u16 num_parameters,
			const u16 *parameters
			);
static int fmd_read_resp(
			u16 *cmd_id,
			u16 *num_parameters,
			u16 *parameters
			);
static void fmd_process_fm_function(
			u8 *packet_buffer
			);
static int fmd_write_file_block(
			u32 file_block_id,
			u8 *file_block,
			u16 file_block_length
			);
static void fmd_receive_data(
			u16 packet_length,
			u8 *packet_buffer
			);
static int fmd_rds_thread(
			void *data
			);
static void fmd_start_irq_thread(void);
static void fmd_stop_irq_thread(void);
static int fmd_irq_thread(
			void *data
			);
static int fmd_send_packet(
			u16 num_bytes,
			u8 *send_buffer
			);
static int fmd_get_cmd_sem(void);
static void fmd_set_cmd_sem(void);
static void fmd_get_interrupt_sem(void);
static void fmd_set_interrupt_sem(void);
static bool fmd_driver_init(void);
static void fmd_driver_exit(void);

/* structure declared in time.h */
struct timespec time_spec;


/**
 * fmd_hexdump() - Displays the HCI Data Bytes exchanged with FM Chip.
 *
 * @prompt: Prompt signifying the direction '<' for Rx '>' for Tx
 * @buffer: Buffer to be displayed.
 * @num_bytes: Number of bytes of the buffer.
 */
 static void fmd_hexdump(
			char prompt,
			u8 *buffer,
			int num_bytes
			)
{
	int i;
	u8 tmp_val;
	struct timespec time;
	static u8 pkt_write[MAX_BUFFER_SIZE], *pkt_ptr;

	getnstimeofday(&time);
	sprintf(pkt_write, "\n[%08x:%08x] [%04x] %c",
		(unsigned int)time.tv_sec,
		(unsigned int)time.tv_nsec,
		num_bytes, prompt);

	pkt_ptr = pkt_write + strlen(pkt_write);
	if (buffer == NULL)
		return;

	/* Copy the buffer only if the input buffer is not NULL */
	for (i = 0; i < num_bytes; i++) {
		*pkt_ptr++ = ' ';
		tmp_val = buffer[i] >> 4;
		*pkt_ptr++ = ASCVAL(tmp_val);
		tmp_val = buffer[i] & 0x0F;
		*pkt_ptr++ = ASCVAL(tmp_val);
		if (i > 20) {
			/* Print only 20 bytes at max */
			break;
		}
	}
	*pkt_ptr++ = '\0';
	FM_HEX_REPORT("%s", pkt_write);
}

/**
 * fmd_get_event() - Returns the Event based on FM Driver State.
 *
 * @gocmd: Pending FM Command
 *
 * Returns: Corresponding Event
 */
static u8 fmd_get_event(
			enum fmd_gocmd gocmd
			)
{
	u8 event = FMD_EVENT_OPERATION_COMPLETED;
	switch (gocmd) {
	case FMD_STATE_ANTENNA:
		event = FMD_EVENT_ANTENNA_STATUS_CHANGED;
		break;
	case FMD_STATE_FREQUENCY:
		event = FMD_EVENT_FREQUENCY_CHANGED;
		break;
	case FMD_STATE_SEEK:
		event = FMD_EVENT_SEEK_COMPLETED;
		break;
	case FMD_STATE_SCAN_BAND:
		event = FMD_EVENT_SCAN_BAND_COMPLETED;
		break;
	case FMD_STATE_BLOCK_SCAN:
		event = FMD_EVENT_BLOCK_SCAN_COMPLETED;
		break;
	case FMD_STATE_AF_UPDATE:
	/* Drop Down */
	case FMD_STATE_AF_SWITCH:
		event = FMD_EVENT_AF_UPDATE_SWITCH_COMPLETE;
		break;
	case FMD_STATE_SEEK_STOP:
		event = FMD_EVENT_SEEK_STOPPED;
		break;
	default:
		event = FMD_EVENT_OPERATION_COMPLETED;
		break;
	}
	return event;
}

/**
 * fmd_event_name() - Converts the event to a displayable string.
 *
 * @event: Event that has occurred.
 * @eventname: (out) Buffer to store event name.
 */
static void fmd_event_name(
			u8 event,
			char *eventname
			)
{
	if (eventname == NULL) {
		FM_ERR_REPORT("fmd_event_name: Output Buffer is NULL");
		return;
	}
	if (event < FMD_EVENT_LAST_ELEMENT)
		strcpy(eventname, event_name[event]);
	else
		strcpy(eventname, "FMD_EVENT_UNKNOWN");
}

/**
 * fmd_get_fm_function_name() - Returns the FM Fucntion name.
 *
 * @fm_function: Function whose name is to be retrieved.
 *
 * Returns FM Function Name.
 */
static char *fmd_get_fm_function_name(
			u8 fm_function
			)
{
	switch (fm_function) {
	case FM_FUNCTION_ENABLE:
		return "FM_FUNCTION_ENABLE";
		break;
	case FM_FUNCTION_DISABLE:
		return "FM_FUNCTION_DISABLE";
		break;
	case FM_FUNCTION_RESET:
		return "FM_FUNCTION_RESET";
		break;
	case FM_FUNCTION_WRITE_COMMAND:
		return "FM_FUNCTION_WRITE_COMMAND";
		break;
	case FM_FUNCTION_SET_INT_MASK_ALL:
		return "FM_FUNCTION_SET_INT_MASK_ALL";
		break;
	case FM_FUNCTION_GET_INT_MASK_ALL:
		return "FM_FUNCTION_GET_INT_MASK_ALL";
		break;
	case FM_FUNCTION_SET_INT_MASK:
		return "FM_FUNCTION_SET_INT_MASK";
		break;
	case FM_FUNCTION_GET_INT_MASK:
		return "FM_FUNCTION_GET_INT_MASK";
		break;
	case FM_FUNCTION_FIRMWARE_DOWNLOAD:
		return "FM_FUNCTION_FIRMWARE_DOWNLOAD";
		break;
	default:
		return "FM_FUNCTION_UNKNOWN";
		break;
	}
}

/**
 * fmd_interrupt_name() - Converts the interrupt to a displayable string.
 *
 * @interrupt: interrupt received from FM Chip
 * @interruptname: (out) Buffer to store interrupt name.
 */
static void fmd_interrupt_name(
			u16 interrupt,
			char *interruptname
			)
{
	int index;

	if (interruptname == NULL) {
		FM_ERR_REPORT("fmd_interrupt_name: Output Buffer is NULL!!!");
		return;
	}
	/* Convert Interrupt to Bit */
	for (index = 0; index < MAX_COUNT_OF_IRQS; index++) {
		if (interrupt & (1 << index)) {
			/* Match found, break the loop */
			break;
		}
	}
	if (index < MAX_COUNT_OF_IRQS)
		strcpy(interruptname, interrupt_name[index]);
	else
		strcpy(interruptname, "IRPT_UNKNOWN");
}

/**
 * fmd_add_interrupt_to_queue() - Add interrupt to IRQ Queue.
 *
 * @interrupt: interrupt received from FM Chip
 */
static void fmd_add_interrupt_to_queue(
			u16 interrupt
			)
{
	FM_DEBUG_REPORT("fmd_add_interrupt_to_queue : "
			"Interrupt Received = %04x", (u16) interrupt);

	/* Reset the index if it reaches the array limit */
	if (fmd_state_info.irq_index > MAX_COUNT_OF_IRQS - 1) {
		spin_lock(&fmd_spinlock);
		fmd_state_info.irq_index = 0;
		spin_unlock(&fmd_spinlock);
	}

	spin_lock(&fmd_spinlock);
	fmd_state_info.interrupt_queue[fmd_state_info.irq_index] = interrupt;
	fmd_state_info.irq_index++;
	spin_unlock(&fmd_spinlock);
	if (!fmd_state_info.interrupt_available_for_processing) {
		spin_lock(&fmd_spinlock);
		fmd_state_info.interrupt_available_for_processing = true;
		spin_unlock(&fmd_spinlock);
		fmd_set_interrupt_sem();
	}
}

/**
 * fmd_process_interrupt() - Processes the Interrupt.
 *
 * This function processes the interrupt received from FM Chip
 * and calls the corresponding callback registered by upper layers with
 * proper parameters.
 * @interrupt: interrupt received from FM Chip
 */
static void fmd_process_interrupt(
			u16 interrupt
			)
{
	char irpt_name[MAX_NAME_SIZE];

	fmd_interrupt_name(interrupt, irpt_name);
	FM_DEBUG_REPORT("%s", irpt_name);
	if ((interrupt & IRPT_OPERATION_SUCCEEDED) |
		(interrupt & IRPT_OPERATION_FAILED)) {
		bool event_status = (interrupt & IRPT_OPERATION_SUCCEEDED);
		u8 event = fmd_get_event(fmd_state_info.gocmd);

		switch (fmd_state_info.gocmd) {
		case FMD_STATE_MODE:
			/* Mode has been changed. */
		case FMD_STATE_MUTE:
			/* FM radio is Muter or Unmuted */
		case FMD_STATE_PA:
			/* Power Amplifier has been enabled/disabled */
		case FMD_STATE_PA_LEVEL:
			/* Power Amplifier Level has been changed. */
		case FMD_STATE_SELECT_REF_CLK:
			/* Reference Clock has been selected. */
		case FMD_STATE_SET_REF_CLK_PLL:
			/* Reference Clock frequency has been changed. */
		case FMD_STATE_TX_SET_CTRL:
			/* Tx Control has been set. */
		case FMD_STATE_TX_SET_THRSHLD:
			/* Tx Threashold has been set. */
			/* Set State to None and set the waiting semaphore. */
			fmd_state_info.gocmd = FMD_STATE_NONE;
			fmd_set_cmd_sem();
			break;
		case FMD_STATE_ANTENNA:
			/* Antenna status has been changed. */
		case FMD_STATE_SEEK_STOP:
			/* Band scan, seek or block scan has completed. */
		case FMD_STATE_AF_UPDATE:
			/* AF Update has completed. */
		case FMD_STATE_AF_SWITCH:
			/* AF Switch has completed. */
		case FMD_STATE_FREQUENCY:
			/* Frequency has been changed. */
			/*
			 * Set State to None, set the waiting semaphore,
			 * and inform upper layer.
			 */
			fmd_state_info.gocmd = FMD_STATE_NONE;
			fmd_set_cmd_sem();
			fmd_callback(
				event,
				event_status);
			break;
		case FMD_STATE_SEEK:
			/* Seek has completed. */
		case FMD_STATE_SCAN_BAND:
			/* Band scan has completed. */
		case FMD_STATE_BLOCK_SCAN:
			/* Block scan has completed. */
			/*
			 * Set State to None. No need to set the
			 * semaphore since this is an asyncronous event.
			 */
			fmd_state_info.gocmd = FMD_STATE_NONE;
			/* Inform Upper layer. */
			fmd_callback(event,	event_status);
			break;
		default:
			/* Do Nothing */
			FM_ERR_REPORT("Default %s case of "\
				"interrupt processing", event_status ? \
				"Success" : "Failed");
			break;
		}
	}

	if (interrupt & IRPT_RX_BUFFERFULL_TX_BUFFEREMPTY) {
		/*
		 * RDS Buffer Full or RDS Buffer Empty
		 * interrupt received from chip, indicating
		 * that RDS data is available if chip
		 * is in Rx mode or RDS data can be send
		 * to chip in case of Tx mode. Inform the
		 * upper layers about this interrupt.
		 */
		fmd_callback(
		     FMD_EVENT_RDSGROUP_RCVD,
		     true);
	}

	if (interrupt & IRPT_RX_MONO_STEREO_TRANSITION) {
		/*
		 * Mono Stereo Transition interrupt
		 * received from chip, inform the
		 * upper layers about it.
		 */
		fmd_callback(
		     FMD_EVENT_MONO_STEREO_TRANSITION_COMPLETE,
		     true);
	}

	if ((interrupt & IRPT_COLD_BOOT_READY) |
		(interrupt &  IRPT_WARM_BOOT_READY)) {
		switch (fmd_state_info.gocmd) {
		case FMD_STATE_GEN_POWERUP:
			/*
			 * Cold Boot/ Warm Boot Interrupt received from
			 * chip, indicating transition from
			 * power off/standby state to active state.
			 * Inform the upper layers about it.
			 */
			fmd_callback(
				FMD_EVENT_GEN_POWERUP,
				true);
			/* Set State to None and set the waiting semaphore. */
			fmd_state_info.gocmd = FMD_STATE_NONE;
			fmd_set_cmd_sem();
			break;
		default:
			/* Do Nothing */
			break;
		}
	}
}

/**
 * fmd_callback() - Callback function for upper layers.
 *
 * Callback function that calls the registered callback of upper
 * layers with proper parameters.
 * @event: event for which the callback function was called
 * from FM Driver.
 * @event_successful: Signifying whether the event is called from FM
 * Driver on receiving irpt_Operation_Succeeded or irpt_Operation_Failed.
 */
static void fmd_callback(
			u8 event,
			bool event_successful
			)
{
	char event_name_string[MAX_NAME_SIZE];

	fmd_event_name(event, event_name_string);

	FM_DEBUG_REPORT("%s %x, %d", event_name_string,
		(unsigned int)event , (unsigned int)event_successful);

	if (fmd_state_info.callback)
		fmd_state_info.callback(
				event,
				event_successful);
}

/**
 * fmd_rx_frequency_to_channel() - Converts Rx frequency to channel number.
 *
 * Converts the Frequency in kHz to corresponding Channel number.
 * This is used for FM Rx.
 * @freq: Frequency in kHz.
 * @channel: Channel Number corresponding to the given Frequency.
 *
 * Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if parameters are not valid.
 *
 */
static int fmd_rx_frequency_to_channel(
			u32 freq,
			u16 *channel
			)
{
	u8	range;
	int	result;
	u32	min_freq;
	u32	max_freq;

	if (channel == NULL) {
		result = -EINVAL;
		goto error;
	}

	result = fmd_get_freq_range(
			&range);

	if (result != 0)
		goto error;

	result = fmd_get_freq_range_properties(
			range,
			&min_freq,
			&max_freq);

	if (result != 0)
		goto error;

	if (freq > max_freq)
		freq = max_freq;
	else if (freq < min_freq)
		freq = min_freq;

	/*
	 * Frequency in kHz needs to be divided with 50 kHz to get
	 * channel number for all FM Bands
	 */
	*channel = (u16)((freq - min_freq) / CHANNEL_FREQ_CONVERTER_MHZ);
	result = 0;
error:
	return result;
}

/**
 * fmd_rx_channel_to_frequency() - Converts Rx Channel number to frequency.
 *
 * Converts the Channel Number to corresponding Frequency in kHz.
 * This is used for FM Rx.
 * @channel_number: Channel Number to be converted.
 * @frequency: Frequency corresponding to the corresponding channel in kHz.
 *
 * Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if parameters are not valid.
 *
 */
static int fmd_rx_channel_to_frequency(
			u16 channel_number,
			u32 *frequency
			)
{
	u8	range;
	int	result;
	u32	min_freq;
	u32	max_freq;

	if (frequency == NULL) {
		result = -EINVAL;
		goto error;
	}

	result = fmd_get_freq_range(
			&range);

	if (result != 0)
		goto error;

	result = fmd_get_freq_range_properties(
			range,
			&min_freq,
			&max_freq);

	if (result != 0)
		goto error;

	/*
	 * Channel Number needs to be multiplied with 50 kHz to get
	 * frequency in kHz for all FM Bands
	 */
	*frequency = min_freq + (channel_number * CHANNEL_FREQ_CONVERTER_MHZ);

error:
	return result;
}

/**
 * fmd_tx_frequency_to_channel() - Converts Tx frequency to channel number.
 *
 * Converts the Frequency in kHz to corresponding Channel number.
 * This is used for FM Tx.
 * @freq: Frequency in kHz.
 * @channel: (out)Channel Number corresponding to the given Frequency.
 *
 * Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if parameters are not valid.
 */
static int fmd_tx_frequency_to_channel(
			u32 freq,
			u16 *channel
			)
{
	u8	range;
	int	result;
	u32	min_freq;
	u32	max_freq;

	if (channel == NULL) {
		result = -EINVAL;
		goto error;
	}

	result = fmd_tx_get_freq_range(
			&range);

	if (result != 0)
		goto error;

	result = fmd_get_freq_range_properties(
			range,
			&min_freq,
			&max_freq);

	if (result != 0)
		goto error;

	if (freq > max_freq)
		freq = max_freq;
	else if (freq < min_freq)
		freq = min_freq;

	/*
	 * Frequency in kHz needs to be divided with 50 kHz to get
	 * channel number for all FM Bands
	 */
	*channel = (u16)((freq - min_freq) / CHANNEL_FREQ_CONVERTER_MHZ);
	result = 0;
error:
	return result;
}

/**
 * fmd_tx_channel_to_frequency() - Converts Tx Channel number to frequency.
 *
 * Converts the Channel Number to corresponding Frequency in kHz.
 * This is used for FM Tx.
 * @channel_number: Channel Number to be converted.
 * @frequency: Frequency corresponding to the corresponding channel
 * in kHz.
 *
 * Returns:
 *	 0,  if no error.
 *	 -ENOEXEC, if preconditions are violated.
 *	 -EINVAL, if parameters are not valid.
 */
static int fmd_tx_channel_to_frequency(
			u16 channel_number,
			u32 *frequency
			)
{
	u8	range;
	int	result;
	u32	min_freq;
	u32	max_freq;

	if (frequency == NULL) {
		result = -EINVAL;
		goto error;
	}

	result = fmd_tx_get_freq_range(
			&range);

	if (result != 0)
		goto error;

	result = fmd_get_freq_range_properties(
			range,
			&min_freq,
			&max_freq);

	if (result != 0)
		goto error;

	/*
	 * Channel Number needs to be multiplied with 50 kHz to get
	 * frequency in kHz for all FM Bands
	 */
	*frequency = min_freq + (channel_number * CHANNEL_FREQ_CONVERTER_MHZ);

	if (*frequency > max_freq)
		*frequency = max_freq;
	else if (*frequency < min_freq)
		*frequency = min_freq;

	result = 0;
error:
	return result;
}

/**
 * fmd_go_cmd_busy() - Function to check if FM Driver is busy or idle
 *
 * Returns:
 *   false if FM Driver is Idle
 *   true otherwise
 */
static bool fmd_go_cmd_busy(void)
{
	return (fmd_state_info.gocmd != FMD_STATE_NONE);
}

/**
 * fmd_read_cb() - Handle Received Data
 *
 * This function handles data received from connectivity protocol driver.
 * @dev: Device receiving data.
 * @skb: Buffer with data coming form device.
 */
static void fmd_read_cb(
			struct cg2900_user_data *dev,
			struct sk_buff *skb
			)
{
	FM_INFO_REPORT("fmd_read_cb");

	if (skb->data == NULL || skb->len == 0)
		return;

	spin_lock(&fmd_spinlock_read);
	CG2900_HEX_READ_PACKET_DUMP;
	/*
	 * The first byte is length of bytes following bytes
	 * Rest of the bytes are the actual data
	 */
	fmd_receive_data(
					FM_GET_PKT_LEN(skb->data),
					FM_GET_RSP_PKT_ADDR(skb->data));

	kfree_skb(skb);
	spin_unlock(&fmd_spinlock_read);
}

/**
 * fmd_receive_data() - Processes the FM data received from device.
 *
 * @packet_length: Length of received Data Packet
 * @packet_buffer: Received Data buffer.
 */
static void fmd_receive_data(
			u16 packet_length,
			u8 *packet_buffer
			)
{
	if (packet_buffer == NULL) {
		FM_ERR_REPORT("fmd_receive_data: Buffer = NULL");
		return;
	}

	if (packet_length == FM_PG1_INTERRUPT_EVENT_LEN &&
		packet_buffer[0] == FM_CATENA_OPCODE &&
		packet_buffer[1] == FM_EVENT_ID) {
		/* PG 1.0 interrupt Handling */
		u16 interrupt = FM_GET_PGI_INTERRUPT(packet_buffer);
		FM_DEBUG_REPORT("interrupt = %04x",
			(unsigned int)interrupt);
		fmd_add_interrupt_to_queue(interrupt);
	} else if (packet_length == FM_PG2_INTERRUPT_EVENT_LEN &&
		packet_buffer[0] == FM_SUCCESS_STATUS &&
		packet_buffer[1] == FM_CATENA_OPCODE &&
		packet_buffer[2] == FM_EVENT &&
		packet_buffer[3] == FM_EVENT_ID) {
		/* PG 2.0 interrupt Handling */
		u16 interrupt = FM_GET_PG2_INTERRUPT(packet_buffer);
		FM_DEBUG_REPORT("interrupt = %04x",
			(unsigned int)interrupt);
		fmd_add_interrupt_to_queue(interrupt);
	} else if (packet_buffer[0] == FM_SUCCESS_STATUS &&
		packet_buffer[1] == FM_CATENA_OPCODE &&
		packet_buffer[2] == FM_WRITE) {
		/* Command Complete or RDS Data Handling */
		u8 fm_status = FM_GET_STATUS(packet_buffer);;
		switch (fm_status) {
		case FM_CMD_STATUS_CMD_SUCCESS:
			fmd_process_fm_function(
				FM_GET_FUNCTION_ADDR(packet_buffer));
			break;
		case FM_CMD_STATUS_HCI_ERR_HW_FAILURE:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_HCI_ERR_HW_FAILURE");
			break;
		case FM_CMD_STATUS_HCI_ERR_INVALID_PARAMETERS:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_HCI_ERR_INVALID_PARAMETERS");
			break;
		case FM_CMD_STATUS_IP_UNINIT:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_IP_UNINIT");
			break;
		case FM_CMD_STATUS_HCI_ERR_UNSPECIFIED_ERROR:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_HCI_ERR_UNSPECIFIED_ERROR");
			break;
		case FM_CMD_STATUS_HCI_ERR_CMD_DISALLOWED:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_HCI_ERR_CMD_DISALLOWED");
			break;
		case FM_CMD_STATUS_WRONG_SEQ_NUM:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_WRONG_SEQ_NUM");
			break;
		case FM_CMD_STATUS_UNKNOWN_FILE_TYPE:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_UNKNOWN_FILE_TYPE");
			break;
		case FM_CMD_STATUS_FILE_VERSION_MISMATCH:
			FM_DEBUG_REPORT(
				"FM_CMD_STATUS_FILE_VERSION_MISMATCH");
			break;
		default:
			FM_DEBUG_REPORT(
				"Unknown Status = %02x", fm_status);
			break;
		}
	}
}

/**
 * fmd_reset_cb() - Reset callback fuction.
 *
 * @dev: CPD device reseting.
 */
static void fmd_reset_cb(struct cg2900_user_data *dev)
{
	FM_INFO_REPORT("fmd_reset_cb: Device Reset");
	spin_lock(&fmd_spinlock_read);
	cg2900_handle_device_reset();
	spin_unlock(&fmd_spinlock_read);
}

/**
 * fmd_rds_thread() - Thread for receiving RDS data from Chip.
 *
 * @data: Data beng passed as parameter on starting the thread.
 */
static int fmd_rds_thread(
			void *data
			)
{
	FM_INFO_REPORT("fmd_rds_thread Created Successfully");
	while (rds_thread_required) {
		if (cb_rds_func)
			cb_rds_func();
		/* Give 100 ms for context switching */
		schedule_timeout_interruptible(msecs_to_jiffies(100));
	}
	/* Always signal the rds_sem semaphore before exiting */
	fmd_set_rds_sem();
	FM_DEBUG_REPORT("fmd_rds_thread Exiting!!!");
	return 0;
}

/**
 * fmd_start_irq_thread() - Function for starting Interrupt Thread.
 */
static void fmd_start_irq_thread(void)
{
	FM_INFO_REPORT("fmd_start_irq_thread");
	irq_thread_task = kthread_create(fmd_irq_thread, NULL, "irq_thread");
	if (IS_ERR(irq_thread_task)) {
		FM_ERR_REPORT("fmd_start_irq_thread: "
			"Unable to Create irq_thread");
		irq_thread_task = NULL;
		return;
	}
	wake_up_process(irq_thread_task);
}

/**
 * fmd_stop_irq_thread() - Function for stopping Interrupt Thread.
 */
static void fmd_stop_irq_thread(void)
{
	FM_INFO_REPORT("fmd_stop_irq_thread");
	kthread_stop(irq_thread_task);
	irq_thread_task = NULL;
	FM_DEBUG_REPORT("-fmd_stop_irq_thread");
}

/**
 * fmd_irq_thread() - Thread for processing Interrupts received from Chip.
 *
 * @data: Data being passed as parameter on starting the thread.
 */

static int fmd_irq_thread(
			void *data
			)
{
	int index;

	FM_INFO_REPORT("fmd_irq_thread Created Successfully");

	while (irq_thread_required) {
		if (!fmd_state_info.interrupt_available_for_processing) {
			FM_DEBUG_REPORT("fmd_irq_thread: Waiting on irq sem "
				"interrupt_available_for_processing = %d "
				"fmd_state_info.fmd_initialized = %d",
			fmd_state_info.interrupt_available_for_processing,
			fmd_state_info.fmd_initialized);
			fmd_get_interrupt_sem();
			FM_DEBUG_REPORT("fmd_irq_thread: Waiting on irq sem "
				"interrupt_available_for_processing = %d "
				"fmd_state_info.fmd_initialized = %d",
			fmd_state_info.interrupt_available_for_processing,
			fmd_state_info.fmd_initialized);
		}
		index = 0;

		if (fmd_state_info.interrupt_available_for_processing) {
			while (index < MAX_COUNT_OF_IRQS) {
				if (fmd_state_info.interrupt_queue[index]
				    != IRPT_INVALID) {
					FM_DEBUG_REPORT("fmd_process_interrupt "
						"Interrupt = %04x",
						fmd_state_info.
						interrupt_queue[index]);
					fmd_process_interrupt(
					fmd_state_info.interrupt_queue[index]);
					fmd_state_info.interrupt_queue[index]
					    = IRPT_INVALID;
				}
				index++;
			}
		}
		fmd_state_info.interrupt_available_for_processing = false;
		schedule_timeout_interruptible(msecs_to_jiffies(100));
	}
	FM_DEBUG_REPORT("fmd_irq_thread Exiting!!!");
	return 0;
}

/**
 * fmd_send_packet() - Sends the FM HCI Packet to the CG2900 Protocol Driver.
 *
 * @num_bytes: Number of bytes of Data to be sent including
 * Channel Identifier (08)
 * @send_buffer: Buffer containing the Data to be sent to Chip.
 *
 * Returns:
 * 0, If packet was sent successfully to
 * CG2900 Protocol Driver, otherwise the corresponding error.
 * -EINVAL If parameters are not valid.
 * -EIO If there is an Input/Output Error.
 */
static int fmd_send_packet(
			u16 num_bytes,
			u8 *send_buffer
			)
{
	int err;
	struct sk_buff *skb;
	struct cg2900_user_data *pf_data;

	FM_INFO_REPORT("fmd_send_packet");

	if (send_buffer == NULL) {
		err = -EINVAL;
		goto error;
	}

	if (!cg2900_fm_dev) {
		FM_ERR_REPORT("fmd_send_packet: No FM device registered");
		err = -EIO;
		goto error;
	}

	pf_data = dev_get_platdata(cg2900_fm_dev);
	if (!pf_data->opened) {
		FM_ERR_REPORT("fmd_send_packet: FM channel is not opened");
		err = -EIO;
		goto error;
	}

	mutex_lock(&write_mutex);
	CG2900_HEX_WRITE_PACKET_DUMP;

	skb = pf_data->alloc_skb(num_bytes, GFP_KERNEL);
	if (!skb) {
		FM_ERR_REPORT("fmd_send_packet:Couldn't " \
			"allocate sk_buff with length %d", num_bytes);
		err = -EIO;
		goto error;
	}

	/*
	 * Copy the buffer removing the FM Header as this
	 * would be done by Protocol Driver
	 */
	memcpy(skb_put(skb, num_bytes), send_buffer, num_bytes);

	err = pf_data->write(pf_data, skb);
	if (err) {
		FM_ERR_REPORT("fmd_send_packet: "
		"Failed to send(%d) bytes using "
		"cg2900_write, err = %d",
		num_bytes, err);
		kfree(skb);
		err = -EIO;
		goto error;
	}

	err = 0;

error:
	mutex_unlock(&write_mutex);
	FM_DEBUG_REPORT("fmd_send_packet returning %d", err);
	return err;
}

/**
 * fmd_get_cmd_sem() - Block on Command Semaphore.
 *
 * This is required to ensure Flow Control in FM Driver.
 *
 * Returns:
 *	 0,  if no error.
 *	 -ETIME if timeout occurs.
 */
static int fmd_get_cmd_sem(void)
{
	int ret_val;

	FM_INFO_REPORT("fmd_get_cmd_sem");

	ret_val = down_timeout(&cmd_sem,
		msecs_to_jiffies(MAX_RESPONSE_TIME_IN_MS));

	if (ret_val)
		FM_ERR_REPORT("fmd_get_cmd_sem: down_timeout "
			      "returned error = %d", ret_val);

	return ret_val;
}

/**
 * fmd_set_cmd_sem() - Unblock on Command Semaphore.
 *
 * This is required to ensure Flow Control in FM Driver.
 */
static void fmd_set_cmd_sem(void)
{
	FM_DEBUG_REPORT("fmd_set_cmd_sem");

	up(&cmd_sem);
}

/**
 * fmd_get_interrupt_sem() - Block on Interrupt Semaphore.
 *
 * Till Interrupt is received, Interrupt Task is blocked.
 */
static void fmd_get_interrupt_sem(void)
{
	int ret_val;

	FM_DEBUG_REPORT("fmd_get_interrupt_sem");

	ret_val = down_killable(&interrupt_sem);

	if (ret_val)
		FM_ERR_REPORT("fmd_get_interrupt_sem: down_killable "
			"returned error = %d", ret_val);
}

/**
 * fmd_set_interrupt_sem() - Unblock on Interrupt Semaphore.
 *
 * on receiving  Interrupt, Interrupt Task is un-blocked.
 */
static void fmd_set_interrupt_sem(void)
{
	FM_DEBUG_REPORT("fmd_set_interrupt_sem");
	up(&interrupt_sem);
}

/**
 * fmd_driver_init()- Initializes the Mutex, Semaphore, etc for FM Driver.
 *
 * It also registers FM Driver with the Protocol Driver.
 *
 * Returns:
 *   true if initialization is successful
 *   false if initiialization fails.
 */
static bool fmd_driver_init(void)
{
	bool ret_val;
	struct cg2900_rev_data rev_data;
	struct cg2900_user_data *pf_data;
	int err;

	FM_INFO_REPORT("fmd_driver_init");

	if (!cg2900_fm_dev) {
		FM_ERR_REPORT("No device registered");
		ret_val = false;
		goto error;
	}

	/* Initialize the semaphores */
	sema_init(&cmd_sem, 0);
	sema_init(&rds_sem, 0);
	sema_init(&interrupt_sem, 0);
	cb_rds_func = NULL;
	rds_thread_required = false;
	irq_thread_required = true;

	pf_data = dev_get_platdata(cg2900_fm_dev);

	/* Create Mutex For Reading and Writing */
	spin_lock_init(&fmd_spinlock_read);
	mutex_init(&write_mutex);
	mutex_init(&send_cmd_mutex);
	spin_lock_init(&fmd_spinlock);
	fmd_start_irq_thread();

	/* Open the FM channel */
	err = pf_data->open(pf_data);
	if (err) {
		FM_ERR_REPORT("fmd_driver_init: "
			"Couldn't open FM channel. Either chip is not connected"
			" or Protocol Driver is not initialized");
		ret_val = false;
		goto error;
	}

	if (!pf_data->get_local_revision(pf_data, &rev_data)) {
		FM_DEBUG_REPORT("No revision data available");
		ret_val = false;
		goto error;
	}

	FM_DEBUG_REPORT("Read revision data revision %04x "
		   "sub_version %04x",
		   rev_data.revision, rev_data.sub_version);
	cg2900_fm_set_chip_version(rev_data.revision, rev_data.sub_version);
	ret_val = true;

error:
	FM_DEBUG_REPORT("fmd_driver_init: Returning %d", ret_val);
	return ret_val;
}

/**
 * fmd_driver_exit() - Deinitializes the mutex, semaphores, etc.
 *
 * It also deregisters FM Driver with the Protocol Driver.
 *
 */
static void fmd_driver_exit(void)
{
	struct cg2900_user_data *pf_data;

	FM_INFO_REPORT("fmd_driver_exit");
	irq_thread_required = false;
	mutex_destroy(&write_mutex);
	mutex_destroy(&send_cmd_mutex);
	fmd_stop_irq_thread();
	/* Close the FM channel */
	pf_data = dev_get_platdata(cg2900_fm_dev);
	if (pf_data->opened)
		pf_data->close(pf_data);
}

/**
 * fmd_send_cmd_and_read_resp() - Send command and read response.
 *
 * This function sends the HCI Command to Protocol Driver and
 * Reads back the Response Packet.
 * @cmd_id: Command Id to be sent to FM Chip.
 * @num_parameters: Number of parameters of the command sent.
 * @parameters: Buffer containing the Buffer to be sent.
 * @resp_num_parameters: (out) Number of paramters of the response packet.
 * @resp_parameters: (out) Buffer of the response packet.
 *
 * Returns:
 *   0: If the command is sent successfully and the
 *   response received is also correct.
 *   -EINVAL: If the received response is not correct.
 *   -EIO: If there is an input/output error.
 *   -EINVAL: If parameters are not valid.
 */
static int fmd_send_cmd_and_read_resp(
			const u16 cmd_id,
			const u16 num_parameters,
			const u16 *parameters,
			u16 *resp_num_parameters,
			u16 *resp_parameters
			)
{
	int result;
	u16 read_cmd_id = CMD_ID_NONE;

	FM_INFO_REPORT("fmd_send_cmd_and_read_resp");

	mutex_lock(&send_cmd_mutex);
	result = fmd_send_cmd(
			cmd_id,
			num_parameters,
			parameters);

	if (result != 0)
		goto error;

	result = fmd_read_resp(
			&read_cmd_id,
			resp_num_parameters,
			resp_parameters);

	if (result != 0)
		goto error;

	/*
	 * Check that the response belongs to the sent command
	 */
	if (read_cmd_id != cmd_id)
		result = -EINVAL;

error:
	mutex_unlock(&send_cmd_mutex);
	FM_DEBUG_REPORT("fmd_send_cmd_and_read_resp: "
		"returning %d", result);
	return result;
}

/**
 * fmd_send_cmd() - This function sends the HCI Command
 * to Protocol Driver.
 *
 * @cmd_id: Command Id to be sent to FM Chip.
 * @num_parameters: Number of parameters of the command sent.
 * @parameters: Buffer containing the Buffer to be sent.
 *
 * Returns:
 *   0: If the command is sent successfully to Lower Layers.
 *   -EIO: If there is an input/output error.
 *   -EINVAL: If parameters are not valid.
 */
static int fmd_send_cmd(
			const u16 cmd_id ,
			const u16 num_parameters,
			const u16 *parameters
			)
{
	/*
	 * Total Length includes 6 bytes HCI Header
	 * and remaining bytes depending on number of paramters.
	 */
	u16 total_length = num_parameters * sizeof(u16) + FM_HCI_CMD_HEADER_LEN;
	/*
	 * Parameter Length includes 5 bytes HCI Header
	 * and remaining bytes depending on number of paramters.
	 */
	u16 param_length = num_parameters * sizeof(u16) + FM_HCI_CMD_PARAM_LEN;
	u8 *fm_data = kmalloc(total_length, GFP_KERNEL);
	int err = -EINVAL;

	FM_INFO_REPORT("fmd_send_cmd");

	if (fm_data == NULL) {
		err = -EIO;
		goto error;
	}

	if (num_parameters && parameters == NULL) {
		err = -EINVAL;
		goto error;
	}

	/* HCI encapsulation */
	fm_data[0] = param_length;
	fm_data[1] = FM_CATENA_OPCODE;
	fm_data[2] = FM_WRITE;
	fm_data[3] = FM_FUNCTION_WRITE_COMMAND;
	fm_data[4] = FM_CMD_GET_LSB(cmd_id, num_parameters);
	fm_data[5] = FM_CMD_GET_MSB(cmd_id);

	memcpy(
			(fm_data + FM_HCI_CMD_HEADER_LEN),
			(void *)parameters,
			num_parameters * sizeof(u16));

	/* Send the Packet */
	err = fmd_send_packet(total_length , fm_data);

error:
	kfree(fm_data);
	FM_DEBUG_REPORT("fmd_send_cmd: "
		"returning %d", err);
	return err;
}

/**
 * fmd_read_resp() - This function reads the response packet of the previous
 * command sent to FM Chip and copies it to the buffer provided as parameter.
 *
 * @cmd_id: (out) Command Id received from FM Chip.
 * @num_parameters: (out) Number of paramters of the response packet.
 * @parameters: (out) Buffer of the response packet.
 *
 * Returns:
 *   0: If the response buffer is copied successfully.
 *   -EINVAL: If parameters are not valid.
 *   -ETIME: Otherwise
 */
static int fmd_read_resp(
			u16 *cmd_id,
			u16 *num_parameters,
			u16 *parameters
			)
{
	int err;
	int param_offset = 0;
	int byte_offset = 0;
	FM_INFO_REPORT("fmd_read_resp");

	/* Wait till response of the command is received */
	if (fmd_get_cmd_sem()) {
		err = -ETIME;
		goto error;
	}

	/* Check if the parameters are valid */
	if (cmd_id == NULL || (fmd_data.num_parameters &&
		(num_parameters == NULL || parameters == NULL))) {
		err =  -EINVAL;
		goto error;
	}

	/* Fill the arguments */
	*cmd_id = fmd_data.cmd_id;
	if (fmd_data.num_parameters) {
		*num_parameters = fmd_data.num_parameters;
		while (param_offset <
				(*num_parameters * sizeof(u16)) / WORD_LENGTH) {
			parameters[param_offset] =
				(u16)(fmd_data.parameters[byte_offset])
						& 0x00ff;
			parameters[param_offset] |=
				((u16)(fmd_data.parameters[byte_offset + COUNTER])
						& 0x00ff) << SHIFT_OFFSET;
			byte_offset = byte_offset + WORD_LENGTH;
			param_offset++;
		}
	}

	err = 0;

error:
	FM_DEBUG_REPORT("fmd_read_resp: "
		"returning %d", err);
	return err;
}

/**
 * fmd_process_fm_function() - Process FM Function.
 *
 * This function processes the Response buffer received
 * from lower layers for the FM function and performs the necessary action to
 * parse the same.
 * @packet_buffer: Received Buffer.
 */
static void fmd_process_fm_function(
			u8 *packet_buffer
			)
{
	u8 fm_function_id;
	u8 block_id;
	int count = 0;

	if (packet_buffer == NULL)
		return;

	fm_function_id = FM_GET_FUNCTION_ID(packet_buffer);
	switch (fm_function_id) {
	case FM_FUNCTION_ENABLE:
	case FM_FUNCTION_DISABLE:
	case FM_FUNCTION_RESET:
		FM_DEBUG_REPORT(
			"fmd_process_fm_function: "
			"command success received for %s",
			fmd_get_fm_function_name(fm_function_id));
		/* Release the semaphore since response is received */
		fmd_set_cmd_sem();
		break;
	case FM_FUNCTION_WRITE_COMMAND:
		FM_DEBUG_REPORT(
			"fmd_process_fm_function: "
			"command success received for %s",
			fmd_get_fm_function_name(fm_function_id));

		fmd_data.cmd_id = FM_GET_CMD_ID(packet_buffer);
		fmd_data.num_parameters =
			FM_GET_NUM_PARAMS(packet_buffer);

		FM_DEBUG_REPORT(
			"fmd_process_fm_function: "
			"Cmd Id = 0x%04x, Num Of Parms = %02x",
			fmd_data.cmd_id, fmd_data.num_parameters);

		if (fmd_data.num_parameters) {
			while (count <
				(fmd_data.num_parameters * sizeof(u16))) {
				fmd_data.parameters[count] =
				*(FM_GET_RSP_BUFFER_ADDR(packet_buffer) + count);
				count++;
			}
		}

		/* Release the semaphore since response is received */
		fmd_set_cmd_sem();
		break;
	case FM_FUNCTION_FIRMWARE_DOWNLOAD:
		block_id = FM_GET_BLOCK_ID(packet_buffer);
		FM_DEBUG_REPORT(
			"fmd_process_fm_function: "
			"command success received for %s"
			"block id = %02x",
			fmd_get_fm_function_name(fm_function_id),
			block_id);
		/* Release the semaphore since response is received */
		fmd_set_cmd_sem();
		break;
	default:
		FM_ERR_REPORT(
			"fmd_process_fm_function: "
			"default case: command success received for %s",
			fmd_get_fm_function_name(fm_function_id));
		break;
	}
}

/**
 * fmd_write_file_block() - download firmware.
 *
 * This Function adds the header for downloading
 * the firmware and coeffecient files and sends it to Protocol Driver.
 * @file_block_id: Block ID of the F/W to be transmitted to FM Chip
 * @file_block: Buffer containing the bytes to be sent.
 * @file_block_length: Size of the Firmware buffer.
 *
 * Returns:
 *   0: If there is no error.
 *   -EINVAL: If parameters are not valid.
 *   -ETIME: Otherwise
 */
static int fmd_write_file_block(
			u32 file_block_id,
			u8 *file_block,
			u16 file_block_length
			)
{
	int err;

	FM_INFO_REPORT("fmd_write_file_block");
	if (file_block == NULL) {
		err = -EINVAL;
		goto error;
	}

	mutex_lock(&send_cmd_mutex);
	file_block[0] = file_block_length + FM_HCI_WRITE_FILE_BLK_PARAM_LEN;
	file_block[1] = FM_CATENA_OPCODE;
	file_block[2] = FM_WRITE;
	file_block[3] = FM_FUNCTION_FIRMWARE_DOWNLOAD;
	file_block[4] = file_block_id;
	/* Send the Packet */
	err = fmd_send_packet(
				file_block_length +
				FM_HCI_WRITE_FILE_BLK_HEADER_LEN,
				file_block);

	/* wait till response comes */
	if (fmd_get_cmd_sem())
		err = -ETIME;

error:
	mutex_unlock(&send_cmd_mutex);
	FM_DEBUG_REPORT("fmd_write_file_block: "
		"returning %d", err);
	return err;
}

int fmd_init(void)
{
	int err;

	if (!fmd_driver_init()) {
		err = -EIO;
		goto error;
	}

	memset(&fmd_state_info, 0, sizeof(fmd_state_info));
	fmd_state_info.fmd_initialized = true;
	fmd_state_info.gocmd = FMD_STATE_NONE;
	fmd_state_info.mode = FMD_MODE_IDLE;
	fmd_state_info.callback = NULL;
	fmd_state_info.rx_freq_range = FMD_FREQRANGE_EUROAMERICA;
	fmd_state_info.rx_stereo_mode = FMD_STEREOMODE_BLENDING;
	fmd_state_info.rx_volume = MAX_ANALOG_VOLUME;
	fmd_state_info.rx_antenna = FMD_ANTENNA_EMBEDDED;
	fmd_state_info.rx_rds_on = false;
	fmd_state_info.rx_seek_stop_level = DEFAULT_RSSI_THRESHOLD;
	fmd_state_info.tx_freq_range = FMD_FREQRANGE_EUROAMERICA;
	fmd_state_info.tx_preemphasis = FMD_EMPHASIS_75US;
	fmd_state_info.tx_pilot_dev = DEFAULT_PILOT_DEVIATION;
	fmd_state_info.tx_rds_dev = DEFAULT_RDS_DEVIATION;
	fmd_state_info.tx_strength = MAX_POWER_LEVEL;
	fmd_state_info.max_channels_to_scan = DEFAULT_CHANNELS_TO_SCAN;
	fmd_state_info.tx_stereo_mode = true;
	fmd_state_info.irq_index = 0;
	spin_lock_init(&fmd_spinlock);
	err = 0;

error:
	FM_DEBUG_REPORT("fmd_init returning = %d", err);
	return err;
}

void fmd_exit(void)
{
	fmd_set_interrupt_sem();
	fmd_driver_exit();
	memset(&fmd_state_info, 0, sizeof(fmd_state_info));
}

int fmd_register_callback(
			fmd_radio_cb callback
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	fmd_state_info.callback = callback;
	err = 0;

error:
	return err;
}

int fmd_get_version(
			u16 *version
			)
{
	int err;
	int io_result;
	u16  response_count;
	u16  response_data[CMD_GET_VERSION_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (version == NULL) {
		err = -EINVAL;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_GET_VERSION,
			CMD_GET_VERSION_PARAM_LEN,
			NULL,
			&response_count,
			response_data);
	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	memcpy(version,
		response_data,
		sizeof(u16) * CMD_GET_VERSION_RSP_PARAM_LEN);
	err = 0;

error:
	return err;
}

int fmd_set_mode(
			u8 mode
			)
{
	int err;
	u16 parameters[CMD_GOTO_MODE_PARAM_LEN];
    int io_result;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (mode > FMD_MODE_TX) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = mode;

	fmd_state_info.gocmd = FMD_STATE_MODE;
	FM_ERR_REPORT("Sending Set Mode");

	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_GOTO_MODE,
			CMD_GOTO_MODE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem()) {
		err = -ETIME;
		goto error;
	}
	fmd_state_info.mode = mode;
	err = 0;

error:
	return err;
}

int fmd_get_freq_range_properties(
			u8 range,
			u32 *min_freq,
			u32 *max_freq
			)
{
	int err;

	if (min_freq == NULL || max_freq == NULL) {
		err = -EINVAL;
		goto error;
	}

	switch (range) {
	case FMD_FREQRANGE_EUROAMERICA:
		*min_freq =  FMD_EU_US_MIN_FREQ_IN_KHZ;
		*max_freq = FMD_EU_US_MAX_FREQ_IN_KHZ;
		break;
	case FMD_FREQRANGE_JAPAN:
		*min_freq = FMD_JAPAN_MIN_FREQ_IN_KHZ;
		*max_freq = FMD_JAPAN_MAX_FREQ_IN_KHZ;
		break;
	case FMD_FREQRANGE_CHINA:
		*min_freq =  FMD_CHINA_MIN_FREQ_IN_KHZ;
		*max_freq = FMD_CHINA_MAX_FREQ_IN_KHZ;
		break;
	default:
		*min_freq =  FMD_EU_US_MIN_FREQ_IN_KHZ;
		*max_freq = FMD_EU_US_MAX_FREQ_IN_KHZ;
		break;
	}

	err = 0;

error:
	return err;
}

int fmd_set_antenna(
			u8 antenna
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SET_ANTENNA_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (antenna > FMD_ANTENNA_WIRED) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = antenna;

	fmd_state_info.gocmd = FMD_STATE_ANTENNA;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SET_ANTENNA,
			CMD_SET_ANTENNA_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	fmd_state_info.rx_antenna = antenna;
	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_get_antenna(
			u8 *antenna
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	*antenna = fmd_state_info.rx_antenna;
	err = 0;

error:
	return err;
}

int fmd_set_freq_range(
			u8 range
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TN_SET_BAND_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = range;
	parameters[1] = FMD_MIN_CHANNEL_NUMBER;
	parameters[2] = FMD_MAX_CHANNEL_NUMBER;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_TN_SET_BAND,
			CMD_TN_SET_BAND_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}
	fmd_state_info.rx_freq_range = range;
	err = 0;

error:
	return err;
}

int fmd_get_freq_range(
			u8 *range
			)
{
	int err;

	if (range == NULL) {
		err = -EINVAL;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	*range = fmd_state_info.rx_freq_range;
	err = 0;

error:
	return err;
}

int fmd_rx_set_grid(
			u8 grid
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TN_SET_GRID_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (grid > FMD_GRID_200KHZ) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = grid;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_TN_SET_GRID,
			CMD_TN_SET_GRID_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_set_frequency(
			u32 freq
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SP_TUNE_SET_CHANNEL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (freq > FMD_EU_US_MAX_FREQ_IN_KHZ ||
		freq < FMD_CHINA_MIN_FREQ_IN_KHZ) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_rx_frequency_to_channel(
			freq,
			&parameters[0]);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.gocmd = FMD_STATE_FREQUENCY;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_TUNE_SET_CHANNEL,
			CMD_SP_TUNE_SET_CHANNEL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_rx_get_frequency(
			u32 *freq
			)
{
	int err;
	int io_result;
	u16 response_count;
	u16 response_data[CMD_SP_TUNE_GET_CHANNEL_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (freq == NULL) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
				CMD_FMR_SP_TUNE_GET_CHANNEL,
				CMD_SP_TUNE_GET_CHANNEL_PARAM_LEN,
				NULL,
				&response_count,
				response_data);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	io_result = fmd_rx_channel_to_frequency(
			response_data[0], /* 1st byte is the Frequency */
			freq);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_set_stereo_mode(
			u8 mode
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_RP_STEREO_SET_MODE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (mode > FMD_STEREOMODE_BLENDING) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = mode;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_RP_STEREO_SET_MODE,
			CMD_RP_STEREO_SET_MODE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.rx_stereo_mode = mode;
	err = 0;

error:
	return err;
}

int fmd_rx_set_stereo_ctrl_blending_rssi(
			u16 min_rssi,
			u16 max_rssi)
{
	int err;
	int io_result;
	u16 parameters[CMD_RP_STEREO_SET_CONTROL_BLENDING_RSSI_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = min_rssi;
	parameters[1] = max_rssi;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_RP_STEREO_SET_CONTROL_BLENDING_RSSI,
			CMD_RP_STEREO_SET_CONTROL_BLENDING_RSSI_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}
	err = 0;

error:
	return err;
}

int fmd_rx_get_stereo_mode(
			u8 *mode
			)
{
	int err;
	int io_result;
	u16 response_count;
	u16 response_data[CMD_RP_GET_STATE_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (mode == NULL) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
				CMD_FMR_RP_GET_STATE,
				CMD_RP_GET_STATE_PARAM_LEN,
				NULL,
				&response_count,
				response_data);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	/* 2nd element of response is stereo signal */
	*mode = response_data[1];
	err = 0;

error:
	return err;
}

int fmd_rx_get_signal_strength(
			u16 *strength
			)
{
	int err;
	int io_result;
	u16 response_count;
	u16 response_data[CMD_RP_GET_RSSI_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (strength == NULL) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_RP_GET_RSSI,
			CMD_RP_GET_RSSI_PARAM_LEN,
			NULL,
			&response_count,
			response_data);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	*strength = response_data[0]; /* 1st byte is the signal strength */
	err = 0;

error:
	return err;
}

int fmd_rx_set_stop_level(
			u16 stoplevel
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	fmd_state_info.rx_seek_stop_level = stoplevel;
	err = 0;

error:
	return err;
}

int fmd_rx_get_stop_level(
			u16 *stop_level
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (stop_level == NULL) {
		err = -EINVAL;
		goto error;
	}

	*stop_level = fmd_state_info.rx_seek_stop_level;
	err = 0;

error:
	return err;
}

int fmd_rx_seek(
			bool upwards
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SP_SEARCH_START_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (upwards)
		parameters[0] = 0x0000;
	else
		parameters[0] = 0x0001;
	parameters[1] = fmd_state_info.rx_seek_stop_level;
	parameters[2] = DEFAULT_PEAK_NOISE_VALUE;
	parameters[3] = DEFAULT_AVERAGE_NOISE_MAX_VALUE;
	fmd_state_info.gocmd = FMD_STATE_SEEK;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_SEARCH_START,
			CMD_SP_SEARCH_START_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_scan_band(
			u8 max_channels_to_scan
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SP_SCAN_START_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (max_channels_to_scan > MAX_CHANNELS_TO_SCAN) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = max_channels_to_scan;
	parameters[1] = fmd_state_info.rx_seek_stop_level;
	parameters[2] = DEFAULT_PEAK_NOISE_VALUE;
	parameters[3] = DEFAULT_AVERAGE_NOISE_MAX_VALUE;

	fmd_state_info.gocmd = FMD_STATE_SCAN_BAND;
	fmd_state_info.max_channels_to_scan = max_channels_to_scan;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_SCAN_START,
			CMD_SP_SCAN_START_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_get_max_channels_to_scan(
			u8 *max_channels_to_scan
			)
{
	int err;

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (max_channels_to_scan == NULL) {
		err = -EINVAL;
		goto error;
	}

	*max_channels_to_scan = fmd_state_info.max_channels_to_scan;
	err = 0;

error:
	return err;
}

int fmd_rx_get_scan_band_info(
			u32 index,
			u16 *num_channels,
			u16 *channels,
			u16 *rssi
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SP_SCAN_GET_RESULT_PARAM_LEN];
	u16 response_count;
	u16 response_data[CMD_SP_SCAN_GET_RESULT_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (num_channels == NULL || rssi == NULL || channels == NULL) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = index;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_SCAN_GET_RESULT,
			CMD_SP_SCAN_GET_RESULT_PARAM_LEN,
			parameters,
			&response_count,
			response_data);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	/* 1st byte indicates number of channels found */
	*num_channels = response_data[0];
	/* 2nd byte indicates 1st channel number */
	channels[0] = response_data[1];
	/* 3rd byte indicates RSSI of corresponding channel */
	rssi[0] = response_data[2];
	/* 4th byte indicates 2nd channel number */
	channels[1] = response_data[3];
	/* 5th byte indicates RSSI of corresponding channel */
	rssi[1] = response_data[4];
	/* 6th byte indicates 3rd channel number */
	channels[2] = response_data[5];
	/* 7th byte indicates RSSI of corresponding channel */
	rssi[2] = response_data[6];
	err = 0;

error:
	return err;
}

int fmd_block_scan(
			u32 start_freq,
			u32 stop_freq,
			u8 antenna
			)
{
	u16 start_channel;
	u16 stop_channel;
	int err;
	int io_result;
	u16 parameters[CMD_SP_BLOCK_SCAN_START_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (antenna > FMD_ANTENNA_WIRED) {
		err = -EINVAL;
		goto error;
	}

	if (start_freq > FMD_EU_US_MAX_FREQ_IN_KHZ ||
		start_freq < FMD_CHINA_MIN_FREQ_IN_KHZ) {
		err = -EINVAL;
		goto error;
	}

	if (stop_freq > FMD_EU_US_MAX_FREQ_IN_KHZ ||
		stop_freq < FMD_CHINA_MIN_FREQ_IN_KHZ) {
		err = -EINVAL;
		goto error;
	}

	/* Convert the start frequency to corresponsing channel */
	switch (fmd_state_info.mode) {
	case FMD_MODE_RX:
		io_result = fmd_rx_frequency_to_channel(
					start_freq,
					&start_channel);
		break;
	case FMD_MODE_TX:
		io_result = fmd_tx_frequency_to_channel(
					start_freq,
					&start_channel);
		break;
	default:
		err = -EINVAL;
		goto error;
	}

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	/* Convert the end frequency to corresponsing channel */
	switch (fmd_state_info.mode) {
	case FMD_MODE_RX:
		io_result = fmd_rx_frequency_to_channel(
					stop_freq,
					&stop_channel);
		break;
	case FMD_MODE_TX:
		io_result = fmd_tx_frequency_to_channel(
					stop_freq,
					&stop_channel);
		break;
	default:
		err = -EINVAL;
		goto error;
	}

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	parameters[0] = start_channel;
	parameters[1] = stop_channel;
	parameters[2] = antenna;

	fmd_state_info.gocmd = FMD_STATE_BLOCK_SCAN;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_BLOCK_SCAN_START,
			CMD_SP_BLOCK_SCAN_START_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_get_block_scan_result(
			u32 index,
			u16 *num_channels,
			u16 *rssi
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SP_BLOCK_SCAN_GET_RESULT_PARAM_LEN];
	u16 response_count;
	u16 response_data[CMD_SP_BLOCK_SCAN_GET_RESULT_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (num_channels == NULL || rssi == NULL) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = index;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_BLOCK_SCAN_GET_RESULT,
			CMD_SP_BLOCK_SCAN_GET_RESULT_PARAM_LEN,
			parameters,
			&response_count,
			response_data);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	/*
	 * Response packet has 1st byte as the number
	 * of channels, and the remaining 6 bytes as
	 * rssi values of the channels.
	 */
	*num_channels = response_data[0];
	rssi[0] = response_data[1];
	rssi[1] = response_data[2];
	rssi[2] = response_data[3];
	rssi[3] = response_data[4];
	rssi[4] = response_data[5];
	rssi[5] = response_data[6];
	err = 0;

error:
	return err;
}

int fmd_rx_stop_seeking(void)
{
	int err;
	int io_result;

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (!(fmd_state_info.gocmd == FMD_STATE_SEEK ||
		fmd_state_info.gocmd == FMD_STATE_SCAN_BAND ||
		fmd_state_info.gocmd == FMD_STATE_BLOCK_SCAN)) {
		err = -ENOEXEC;
		goto error;
	}

	fmd_state_info.gocmd = FMD_STATE_SEEK_STOP;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_STOP,
			CMD_SP_STOP_PARAM_LEN,
			NULL,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_rx_af_update_start(
			u32 freq
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SP_AF_UPDATE_START_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	io_result = fmd_rx_frequency_to_channel(
			freq,
			&parameters[0]);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.gocmd = FMD_STATE_AF_UPDATE;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_AF_UPDATE_START,
			CMD_SP_AF_UPDATE_START_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		fmd_state_info.gocmd = FMD_STATE_NONE;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_rx_get_af_update_result(
			u16 *af_level
			)
{
	int err;
	int io_result;
	u16 response_count;
	u16 response_data[CMD_SP_AF_UPDATE_GET_RESULT_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (af_level == NULL) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_AF_UPDATE_GET_RESULT,
			CMD_SP_AF_UPDATE_GET_RESULT_PARAM_LEN,
			NULL,
			&response_count,
			response_data);

	if (io_result != 0)	{
		err = io_result;
		goto error;
	}

	/*
	 * 1st byte of response packet is the
	 * RSSI of the AF Frequency.
	 */
	*af_level = response_data[0];
	err = 0;

error:
	return err;
}

int fmd_rx_af_switch_start(
			u32 freq,
			u16 picode
			)
{

	int err;
	int io_result;
	u16  parameters[CMD_SP_AF_SWITCH_START_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	io_result = fmd_rx_frequency_to_channel(
			freq,
			&parameters[0]);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	parameters[1] = picode;
	parameters[2] = 0xFFFF; /* PI Mask */
	parameters[3] = fmd_state_info.rx_seek_stop_level;
	parameters[4] = 0x0000; /* Unmute when AF's PI matches expected PI */

	fmd_state_info.gocmd = FMD_STATE_AF_SWITCH;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_AF_SWITCH_START,
			CMD_SP_AF_SWITCH_START_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_rx_get_af_switch_results(
			u16 *afs_conclusion,
			u16 *afs_level,
			u16 *afs_pi
			)
{
	int err;
	int io_result;
	u16 response_count;
	u16 response_data[CMD_SP_AF_SWITCH_GET_RESULT_RWSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (afs_conclusion == NULL ||
		afs_level == NULL ||
		afs_pi == NULL) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_SP_AF_SWITCH_GET_RESULT,
			CMD_SP_AF_SWITCH_GET_RESULT_PARAM_LEN,
			NULL,
			&response_count,
			response_data);

	if (io_result != 0)	{
		err = io_result;
		goto error;
	}

	*afs_conclusion = response_data[0];
	*afs_level = response_data[1];
	*afs_pi = response_data[2];
	err = 0;

error:
	return err;
}

int fmd_rx_get_rds(
			bool *on
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (on == NULL) {
		err = -EINVAL;
		goto error;
	}

	*on = fmd_state_info.rx_rds_on;
	err = 0;

error:
	return err;
}

int fmd_rx_buffer_set_size(
			u8 size
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_DP_BUFFER_SET_SIZE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (size > MAX_RDS_GROUPS) {
		err = -EIO;
		goto error;
	}

	parameters[0] = size;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_DP_BUFFER_SET_SIZE,
			CMD_DP_BUFFER_SET_SIZE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_buffer_set_threshold(
			u8 threshold
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_DP_BUFFER_SET_THRESHOLD_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (threshold > MAX_RDS_GROUPS) {
		err = -EIO;
		goto error;
	}

	parameters[0] = threshold;

	io_result = fmd_send_cmd_and_read_resp(
		CMD_FMR_DP_BUFFER_SET_THRESHOLD,
		CMD_DP_BUFFER_SET_THRESHOLD_PARAM_LEN,
		parameters,
		NULL,
		NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_set_rds(
			u8 on_off_state
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_DP_SET_CONTROL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	switch (on_off_state) {
	case FMD_SWITCH_ON_RDS_SIMULATOR:
		parameters[0] = 0xFFFF;
		break;
	case FMD_SWITCH_OFF_RDS:
	default:
		parameters[0] = 0x0000;
		fmd_state_info.rx_rds_on = false;
		break;
	case FMD_SWITCH_ON_RDS:
		parameters[0] = 0x0001;
		fmd_state_info.rx_rds_on = true;
		break;
	case FMD_SWITCH_ON_RDS_ENHANCED_MODE:
		parameters[0] = 0x0002;
		fmd_state_info.rx_rds_on = true;
		break;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_DP_SET_CONTROL,
			CMD_DP_SET_CONTROL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_set_rds_group_rejection(
			u8 on_off_state
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_DP_SET_GROUP_REJECTION_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (on_off_state == FMD_RDS_GROUP_REJECTION_ON)
		parameters[0] = 0x0001;
	else if (on_off_state == FMD_RDS_GROUP_REJECTION_OFF)
		parameters[0] = 0x0000;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_DP_SET_GROUP_REJECTION,
			CMD_DP_SET_GROUP_REJECTION_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_rx_get_low_level_rds_groups(
			u8 index,
			u16 *block1,
			u16 *block2,
			u16 *block3,
			u16 *block4,
			u8 *status1,
			u8 *status2,
			u8 *status3,
			u8 *status4
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (block1 == NULL ||
		block2 == NULL ||
		block3 == NULL ||
		block4 == NULL ||
		status1 == NULL ||
		status2 == NULL ||
		status3 == NULL ||
		status4 == NULL) {
		err = -EINVAL;
		goto error;
	}

	*block1  = fmd_state_info.rds_group[index].block[0];
	*block2  = fmd_state_info.rds_group[index].block[1];
	*block3  = fmd_state_info.rds_group[index].block[2];
	*block4  = fmd_state_info.rds_group[index].block[3];
	*status1 = fmd_state_info.rds_group[index].status[0];
	*status2 = fmd_state_info.rds_group[index].status[1];
	*status3 = fmd_state_info.rds_group[index].status[2];
	*status4 = fmd_state_info.rds_group[index].status[3];
	err = 0;

error:
	return err;
}

int fmd_rx_set_deemphasis(
			u8 deemphasis
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_RP_SET_DEEMPHASIS_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	switch (deemphasis)	{
	case FMD_EMPHASIS_50US:
		parameters[0] = FMD_EMPHASIS_50US;
		break;

	case FMD_EMPHASIS_75US:
		parameters[0] = FMD_EMPHASIS_75US;
		break;

	case FMD_EMPHASIS_NONE:
	default:
		parameters[0] = FMD_EMPHASIS_NONE;
		break;

	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMR_RP_SET_DEEMPHASIS,
			CMD_RP_SET_DEEMPHASIS_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}
	err = 0;

error:
	return err;
}

int fmd_tx_set_pa(
			bool on
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_PA_SET_MODE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (on)
		parameters[0] = 0x0001;
	else
		parameters[0] = 0x0000;

	fmd_state_info.gocmd = FMD_STATE_PA;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_PA_SET_MODE,
			CMD_PA_SET_MODE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_tx_set_signal_strength(
			u16 strength
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_PA_SET_CONTROL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if ((strength > MAX_POWER_LEVEL)
		|| (strength < MIN_POWER_LEVEL)) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = strength;

	fmd_state_info.gocmd = FMD_STATE_PA_LEVEL;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_PA_SET_CONTROL,
			CMD_PA_SET_CONTROL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_strength = strength;
	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_tx_get_signal_strength(
			u16 *strength
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (strength == NULL) {
		err = -EINVAL;
		goto error;
	}

	*strength = fmd_state_info.tx_strength;
	err = 0;

error:
	return err;
}

int fmd_tx_set_freq_range(
			u8 range
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TN_SET_BAND_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (range > FMD_FREQRANGE_CHINA) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = range;
	parameters[1] = FMD_MIN_CHANNEL_NUMBER;
	parameters[2] = FMD_MAX_CHANNEL_NUMBER;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_TN_SET_BAND,
			CMD_TN_SET_BAND_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_freq_range = range;
	err = 0;

error:
	return err;
}

int fmd_tx_get_freq_range(
			u8 *range
			)
{
	int err;

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (range == NULL) {
		err = -EINVAL;
		goto error;
	}

	*range = fmd_state_info.tx_freq_range;
	err = 0;

error:
	return err;
}

int fmd_tx_set_grid(
			u8 grid
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TN_SET_GRID_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (grid > FMD_GRID_200KHZ) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = grid;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_TN_SET_GRID,
			CMD_TN_SET_GRID_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}
	err = 0;

error:
	return err;
}

int fmd_tx_set_preemphasis(
			u8 preemphasis
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_RP_SET_PREEMPHASIS_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	switch (preemphasis)	{
	case FMD_EMPHASIS_50US:
		parameters[0] = FMD_EMPHASIS_50US;
		break;
	case FMD_EMPHASIS_75US:
	default:
		parameters[0] = FMD_EMPHASIS_75US;
		break;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_RP_SET_PREEMPHASIS,
			CMD_RP_SET_PREEMPHASIS_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_preemphasis = preemphasis;
	err = 0;

error:
	return err;
}

int fmd_tx_get_preemphasis(
			u8 *preemphasis
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (preemphasis == NULL) {
		err = -EINVAL;
		goto error;
	}

	*preemphasis = fmd_state_info.tx_preemphasis;
	err = 0;

error:
	return err;
}

int fmd_tx_set_frequency(
			u32 freq
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_SP_TUNE_SET_CHANNEL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (freq > FMD_EU_US_MAX_FREQ_IN_KHZ ||
		freq < FMD_CHINA_MIN_FREQ_IN_KHZ) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_tx_frequency_to_channel(
			freq,
			&parameters[0]);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.gocmd = FMD_STATE_FREQUENCY;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_SP_TUNE_SET_CHANNEL,
			CMD_SP_TUNE_SET_CHANNEL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_tx_get_frequency(
			u32 *freq
			)
{
	int err;
	int io_result;
	u16 response_count;
	u16  response_data[CMD_SP_TUNE_GET_CHANNEL_RSP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (freq == NULL) {
		err = -EINVAL;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_SP_TUNE_GET_CHANNEL,
			CMD_SP_TUNE_GET_CHANNEL_PARAM_LEN,
			NULL,
			&response_count,
			response_data);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	io_result = fmd_tx_channel_to_frequency(
			response_data[0], /* 1st byte is the Frequency */
			freq);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}
	err = 0;

error:
	return err;
}

int fmd_tx_enable_stereo_mode(
			bool enable_stereo_mode
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_RP_STEREO_SET_MODE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = enable_stereo_mode;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_RP_STEREO_SET_MODE,
			CMD_RP_STEREO_SET_MODE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_stereo_mode = enable_stereo_mode;
	err = 0;

error:
	return err;
}

int fmd_tx_get_stereo_mode(
			bool *stereo_mode
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (stereo_mode == NULL) {
		err = -EINVAL;
		goto error;
	}

	*stereo_mode = fmd_state_info.tx_stereo_mode;
	err = 0;

error:
	return err;
}

int fmd_tx_set_pilot_deviation(
			u16 deviation
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_RP_SET_PILOT_DEVIATION_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (deviation > MAX_PILOT_DEVIATION) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = deviation;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_RP_SET_PILOT_DEVIATION,
			CMD_RP_SET_PILOT_DEVIATION_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_pilot_dev = deviation;
	err = 0;

error:
	return err;
}

int fmd_tx_get_pilot_deviation(
			u16 *deviation
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (deviation == NULL) {
		err = -EINVAL;
		goto error;
	}

	*deviation = fmd_state_info.tx_pilot_dev;
	err = 0;

error:
	return err;
}

int fmd_tx_set_rds_deviation(
			u16 deviation
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_RP_SET_RDS_DEVIATION_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (deviation > MAX_RDS_DEVIATION) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = deviation;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_RP_SET_RDS_DEVIATION,
			CMD_RP_SET_RDS_DEVIATION_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_rds_dev = deviation;
	err = 0;

error:
	return err;
}

int fmd_tx_get_rds_deviation(
			u16 *deviation
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (deviation == NULL) {
		err = -EINVAL;
		goto error;
	}

	*deviation = fmd_state_info.tx_rds_dev;
	err = 0;

error:
	return err;
}

int fmd_tx_set_rds(
			bool on
			)
{
	int err;
	int io_result;
	u16  parameters[CMD_DP_SET_CONTROL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (on)
		parameters[0] = 0x0001;
	else
		parameters[0] = 0x0000;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_DP_SET_CONTROL,
			CMD_DP_SET_CONTROL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.tx_rds_on = on;
	err = 0;

error:
	return err;
}

int fmd_tx_set_group(
			u16 position,
			u8  *block1,
			u8  *block2,
			u8  *block3,
			u8  *block4
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_DP_BUFFER_SET_GROUP_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (block1 == NULL ||
		block2 == NULL ||
		block3 == NULL ||
		block4 == NULL) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = position;
	memcpy(&parameters[1], block1, sizeof(u16));
	memcpy(&parameters[2], block2, sizeof(u16));
	memcpy(&parameters[3], block3, sizeof(u16));
	memcpy(&parameters[4], block4, sizeof(u16));

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_DP_BUFFER_SET_GROUP,
			CMD_DP_BUFFER_SET_GROUP_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_tx_buffer_set_size(
			u16 buffer_size
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_DP_BUFFER_SET_SIZE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = buffer_size;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_DP_BUFFER_SET_SIZE,
			CMD_DP_BUFFER_SET_SIZE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;

}

int fmd_tx_get_rds(
			bool *on
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (on == NULL) {
		err = -EINVAL;
		goto error;
	}

	*on = fmd_state_info.tx_rds_on;
	err = 0;

error:
	return err;
}

int fmd_set_balance(
			s8 balance
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SET_BALANCE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	/* Convert balance from percentage to chip number */
	parameters[0] = (((s16)balance) * FMD_MAX_BALANCE) / 100;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_AUP_SET_BALANCE,
			CMD_SET_BALANCE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_set_volume(
			u8 volume
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SET_VOLUME_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	/* Convert volume from percentage to chip number */
	parameters[0] = (((u16)volume) * FMD_MAX_VOLUME) / 100;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_AUP_SET_VOLUME,
			CMD_SET_VOLUME_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	fmd_state_info.rx_volume = volume;
	err = 0;

error:
	return err;
}

int fmd_bt_set_volume(
			u8 volume
			)
{
	int err;
	u16 parameters[CMD_SET_AUP_BT_SETVOLUME_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = (((u16)volume) * FMD_MAX_VOLUME) / 100;

	err = fmd_send_cmd_and_read_resp(
	        CMD_AUP_BT_SETVOLUME,
			CMD_SET_AUP_BT_SETVOLUME_PARAM_LEN,
			parameters,
			NULL,
			NULL);


	if (err)
		goto error;

	fmd_state_info.rx_volume = volume;
	err = 0;

error:
	return err;
}

int fmd_get_volume(
			u8 *volume
			)
{
	int err;

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (volume == NULL) {
		err = -EINVAL;
		goto error;
	}

	*volume = fmd_state_info.rx_volume;
	err = 0;

error:
	return err;
}

int fmd_set_mute(
			bool mute_on
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SET_MUTE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (!mute_on)
		parameters[0] = 0x0000;
	else
		parameters[0] = 0x0001;
	parameters[1] = 0x0001;

	fmd_state_info.gocmd = FMD_STATE_MUTE;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_AUP_SET_MUTE,
			CMD_SET_MUTE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_bt_set_mute(
                    bool mute_on
                    )
{
	int err;
	u16 parameters[CMD_BT_SET_MUTE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (!mute_on)
		parameters[0] = 0x0000;
	else
		parameters[0] = 0x0001;

	err = fmd_send_cmd_and_read_resp(
			CMD_AUP_BT_SET_MUTE,
			CMD_BT_SET_MUTE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (err)
		goto error;

	err = 0;

error:
	return err;
}

int fmd_ext_set_mute(
			bool mute_on
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_EXT_SET_MUTE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (!mute_on)
		parameters[0] = 0x0000;
	else
		parameters[0] = 0x0001;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_AUP_EXT_SET_MUTE,
			CMD_EXT_SET_MUTE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_power_up(void)
{
	int err;
	int io_result;

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	fmd_state_info.gocmd = FMD_STATE_GEN_POWERUP;
	FM_ERR_REPORT("Sending Gen Power Up");
	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_POWERUP,
			CMD_POWERUP_PARAM_LEN,
			NULL,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_goto_standby(void)
{
	int err;
	int io_result;

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_GOTO_STANDBY,
			CMD_GOTO_STANDBY_PARAM_LEN,
			NULL,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_goto_power_down(void)
{
	int err;
	int io_result;

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_GOTO_POWERDOWN,
			CMD_GOTO_POWERDOWN_PARAM_LEN,
			NULL,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_select_ref_clk(
			u16 ref_clk
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SELECT_REFERENCE_CLOCK_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = ref_clk;

	fmd_state_info.gocmd = FMD_STATE_SELECT_REF_CLK;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_SELECT_REFERENCE_CLOCK,
			CMD_SELECT_REFERENCE_CLOCK_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_set_ref_clk_pll(
			u16 freq
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_SET_REFERENCE_CLOCK_PLL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	parameters[0] = freq;

	fmd_state_info.gocmd = FMD_STATE_SET_REF_CLK_PLL;
	io_result = fmd_send_cmd_and_read_resp(
			CMD_GEN_SET_REFERENCE_CLOCK_PLL,
			CMD_SET_REFERENCE_CLOCK_PLL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		fmd_state_info.gocmd = FMD_STATE_NONE;
		err = io_result;
		goto error;
	}

	if (fmd_get_cmd_sem())
		err = -ETIME;
	else
		err = 0;

error:
	return err;
}

int fmd_send_fm_ip_enable(void)
{
	int err;
	u8 fm_ip_enable_cmd[CMD_IP_ENABLE_CMD_LEN];

	mutex_lock(&send_cmd_mutex);
	fm_ip_enable_cmd[0] = CMD_IP_ENABLE_PARAM_LEN;
	fm_ip_enable_cmd[1] = FM_CATENA_OPCODE;
	fm_ip_enable_cmd[2] = FM_WRITE ;
	fm_ip_enable_cmd[3] = FM_FUNCTION_ENABLE;

	/* Send the Packet */
	err = fmd_send_packet(
				CMD_IP_ENABLE_CMD_LEN,
				fm_ip_enable_cmd);

	/* Check the ErrorCode */
	if (err != 0)
		goto error;

	/* wait till response comes */
	if (fmd_get_cmd_sem())
		err = -ETIME;

error:
	mutex_unlock(&send_cmd_mutex);
	return err;
}

int fmd_send_fm_ip_disable(void)
{
	int err;
	u8 fm_ip_disable_cmd[CMD_IP_DISABLE_CMD_LEN];

	mutex_lock(&send_cmd_mutex);
	fm_ip_disable_cmd[0] = CMD_IP_DISABLE_PARAM_LEN;
	fm_ip_disable_cmd[1] = FM_CATENA_OPCODE;
	fm_ip_disable_cmd[2] = FM_WRITE ;
	fm_ip_disable_cmd[3] = FM_FUNCTION_DISABLE;

	/* Send the Packet */
	err = fmd_send_packet(
				CMD_IP_DISABLE_CMD_LEN,
				fm_ip_disable_cmd);

	/* Check the ErrorCode */
	if (err != 0)
		goto error;

	/* wait till response comes */
	if (fmd_get_cmd_sem())
		err = -ETIME;

error:
	mutex_unlock(&send_cmd_mutex);
	return err;
}

int fmd_send_fm_firmware(
			u8 *fw_buffer,
			u16 fw_size
			)
{
	int err = -EINVAL;
	u16 bytes_to_write = ST_WRITE_FILE_BLK_SIZE -
				FM_HCI_WRITE_FILE_BLK_PARAM_LEN;
	u16 bytes_remaining = fw_size;
	u8 fm_firmware_data[ST_WRITE_FILE_BLK_SIZE + FM_HCI_CMD_HEADER_LEN];
	u32 block_id = 0;

	if (fw_buffer == NULL) {
		err = -EINVAL;
		goto error;
	}

	while (bytes_remaining > 0) {
		if (bytes_remaining <
			(ST_WRITE_FILE_BLK_SIZE -
			 FM_HCI_WRITE_FILE_BLK_PARAM_LEN))
			bytes_to_write = bytes_remaining;

		/*
		 * Five bytes of HCI Header for FM Firmware
		 * so shift the firmware data by 5 bytes
		 */
		memcpy(
			fm_firmware_data + FM_HCI_WRITE_FILE_BLK_HEADER_LEN,
			fw_buffer, bytes_to_write);
		err = fmd_write_file_block(
				block_id,
				fm_firmware_data,
				bytes_to_write);
		if (err) {
			FM_DEBUG_REPORT("fmd_send_fm_firmware: "
				"Failed to download %d Block "
				"error = %d", (unsigned int)block_id, err);
			goto error;
		}
		/*
		 * Increment the Block Id by 1, since one
		 * block is successfully transmitted
		 * to the chip.
		 */
		block_id++;
		/*
		 * Increment the next firmware buffer equal
		 * to the number of bytes transmitted.
		 */
		fw_buffer += bytes_to_write;
		/*
		 * Decrement the number of bytes remaining
		 * equal to number of bytes transmitted successfully.
		 */
		bytes_remaining -= bytes_to_write;

		if (block_id == ST_MAX_NUMBER_OF_FILE_BLOCKS)
			block_id = 0;
	}

error:
	return err;
}

int fmd_int_bufferfull(
			u16 *number_of_rds_groups
			)
{
	u16 response_count;
	u16 response_data[CMD_DP_BUFFER_GET_GROUP_COUNT_PARAM_LEN];
	u16 index = 0;
	u16 rds_group_count;
	u8 result = -ENOEXEC;
	struct fmd_rds_group rds_group;

	if (!fmd_state_info.rx_rds_on)
		goto error;

	/* get group count*/
	result = fmd_send_cmd_and_read_resp(
			CMD_FMR_DP_BUFFER_GET_GROUP_COUNT,
			CMD_DP_BUFFER_GET_GROUP_COUNT_PARAM_LEN,
			NULL,
			&response_count,
			response_data);

	if (result != 0)
		goto error;

	/* read RDS groups */
	rds_group_count = FM_GET_NUM_RDS_GRPS(response_data);
	if (rds_group_count > MAX_RDS_GROUPS)
		rds_group_count = MAX_RDS_GROUPS;

	*number_of_rds_groups = rds_group_count;

	if (rds_group_count) {
		FM_DEBUG_REPORT("rds_group_count = %d", rds_group_count);
		while (rds_group_count-- && fmd_state_info.rx_rds_on) {
			result = fmd_send_cmd_and_read_resp(
					CMD_FMR_DP_BUFFER_GET_GROUP,
					CMD_DP_BUFFER_GET_GROUP_PARAM_LEN,
					NULL,
					&response_count,
					(u16 *)&rds_group);

			if (result != 0)
				goto error;

			if (fmd_state_info.rx_rds_on)
				fmd_state_info.rds_group[index++] = rds_group;
		}
	}
error:
	return result;
}

void fmd_start_rds_thread(
			cg2900_fm_rds_cb cb_func
			)
{
	FM_INFO_REPORT("fmd_start_rds_thread");
	cb_rds_func = cb_func;
	rds_thread_required = true;
	rds_thread_task = kthread_create(fmd_rds_thread, NULL, "rds_thread");
	if (IS_ERR(rds_thread_task)) {
		FM_ERR_REPORT("fmd_start_rds_thread: "
			      "Unable to Create rds_thread");
		rds_thread_task = NULL;
		rds_thread_required = false;
		return;
	}
	wake_up_process(rds_thread_task);
}

void fmd_stop_rds_thread(void)
{
	FM_INFO_REPORT("fmd_stop_rds_thread");
	/* In case thread is waiting, set the rds sem */
	fmd_set_rds_sem();
	/* Re-initialize RDS Semaphore to zero */
	sema_init(&rds_sem, 0);
	cb_rds_func = NULL;
	rds_thread_required = false;
	/* Wait for RDS thread to exit gracefully */
	fmd_get_rds_sem();

	if (rds_thread_task)
		rds_thread_task = NULL;
}

void fmd_get_rds_sem(void)
{
	int ret_val;

	FM_DEBUG_REPORT("fmd_get_rds_sem");
	ret_val = down_killable(&rds_sem);

	if (ret_val)
		FM_ERR_REPORT("fmd_get_rds_sem: down_killable "
			"returned error = %d", ret_val);
}

void fmd_set_rds_sem(void)
{
	FM_DEBUG_REPORT("fmd_set_rds_sem");
	up(&rds_sem);
}

int fmd_set_dev(struct device *dev)
{
	struct cg2900_user_data *pf_data;

	FM_DEBUG_REPORT("fmd_set_dev");

	if (dev && cg2900_fm_dev) {
		FM_ERR_REPORT("Only one FM device supported");
		return -EACCES;
	}

	cg2900_fm_dev = dev;

	if (!dev)
		return 0;

	pf_data = dev_get_platdata(dev);
	pf_data->dev = dev;
	pf_data->read_cb = fmd_read_cb;
	pf_data->reset_cb = fmd_reset_cb;

	return 0;
}

int fmd_set_test_tone_generator_status(
			u8 test_tone_status
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TST_TONE_ENABLE_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (test_tone_status > FMD_TST_TONE_ON_WO_SRC) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = test_tone_status;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_TST_TONE_ENABLE,
			CMD_TST_TONE_ENABLE_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_test_tone_connect(
			u8 left_audio_mode,
			u8 right_audio_mode
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TST_TONE_CONNECT_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (left_audio_mode > FMD_TST_TONE_AUDIO_TONE_SUM ||
		right_audio_mode > FMD_TST_TONE_AUDIO_TONE_SUM) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = left_audio_mode;
	parameters[1] = right_audio_mode;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_TST_TONE_CONNECT,
			CMD_TST_TONE_CONNECT_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_test_tone_set_params(
			u8 tone_gen,
			u16 frequency,
			u16 volume,
			u16 phase_offset,
			u16 dc,
			u8 waveform
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_TST_TONE_SET_PARAMS_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (tone_gen > FMD_TST_TONE_2 ||
		waveform > FMD_TST_TONE_PULSE ||
		frequency > 0x7FFF ||
		volume > 0x7FFF) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = tone_gen;
	parameters[1] = frequency;
	parameters[2] = volume;
	parameters[3] = phase_offset;
	parameters[4] = dc;
	parameters[5] = waveform;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_TST_TONE_SET_PARAMS,
			CMD_TST_TONE_SET_PARAMS_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

int fmd_limiter_setcontrol(
			u16 audio_deviation,
			u16 notification_hold_off_time
			)
{
	int err;
	int io_result;
	u16 parameters[CMD_FMT_RP_LIMITER_SETCONTROL_PARAM_LEN];

	if (fmd_go_cmd_busy()) {
		err = -EBUSY;
		goto error;
	}

	if (!fmd_state_info.fmd_initialized) {
		err = -ENOEXEC;
		goto error;
	}

	if (audio_deviation < MIN_AUDIO_DEVIATION ||
		audio_deviation > MAX_AUDIO_DEVIATION ||
		notification_hold_off_time > 0x7FFF) {
		err = -EINVAL;
		goto error;
	}

	parameters[0] = audio_deviation;
	parameters[1] = notification_hold_off_time;

	io_result = fmd_send_cmd_and_read_resp(
			CMD_FMT_RP_LIMITER_SETCONTROL,
			CMD_FMT_RP_LIMITER_SETCONTROL_PARAM_LEN,
			parameters,
			NULL,
			NULL);

	if (io_result != 0) {
		err = io_result;
		goto error;
	}

	err = 0;

error:
	return err;
}

MODULE_AUTHOR("Hemant Gupta");
MODULE_LICENSE("GPL v2");

module_param(cg2900_fm_debug_level, ushort, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(cg2900_fm_debug_level, "cg2900_fm_debug_level: "
		" *1: Only Error Logs* "
		" 2: Info Logs "
		" 3: Debug Logs "
		" 4: HCI Logs");

