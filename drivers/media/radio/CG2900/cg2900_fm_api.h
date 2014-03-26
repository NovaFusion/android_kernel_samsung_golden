/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Linux FM Host API's for ST-Ericsson FM Chip.
 *
 * Author: Hemant Gupta <hemant.gupta@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#ifndef CG2900_FM_API_H
#define CG2900_FM_API_H

#include <linux/device.h>
#include <linux/skbuff.h>

/* Callback function to receive RDS Data. */
typedef void  (*cg2900_fm_rds_cb)(void);

extern struct sk_buff_head		fm_interrupt_queue;

/**
 * struct cg2900_fm_rds_buf - RDS Group Receiving Structure
 *
 * @block1: RDS Block A
 * @block2: RDS Block B
 * @block3: RDS Block C
 * @block4: RDS Block D
 * @status1: Status of received RDS Block A
 * @status2: Status of received RDS Block B
 * @status3: Status of received RDS Block C
 * @status4: Status of received RDS Block D
 *
 * Structure for receiving the RDS Group from FM Chip.
 */
struct cg2900_fm_rds_buf {
	u16 block1;
	u16 block2;
	u16 block3;
	u16 block4;
	u8 status1;
	u8 status2;
	u8 status3;
	u8 status4;
};

/**
 * struct cg2900_fm_rds_info - RDS Information Structure
 *
 * @rds_head: RDS Queue Head for storing next valid data.
 * @rds_tail: RDS Queue Tail for retreiving next valid data.
 * @rds_group_sent: Number of RDS Groups sent to Application.
 * @rds_block_sent: Number of RDS Blocks sent to Application.
 *
 * Structure for storing the RDS data queue information.
 */
struct cg2900_fm_rds_info {
	u8 rds_head;
	u8 rds_tail;
	u8 rds_group_sent;
	u8 rds_block_sent;
};

/**
 * struct cg2900_version_info - Chip HCI Version Info
 *
 * @revision:	Revision of the controller, e.g. to indicate that it is
 *			a CG2900 controller.
 * @sub_version:	Subversion of the controller, e.g. to indicate a certain
 *			tape-out of the controller.
 *
 * Structure for storing the HCI Version Information of the Controller.
 */
struct cg2900_version_info {
	u16 revision;
	u16 sub_version;
};

/**
 * enum cg2900_fm_state - States of FM Driver.
 *
 * @CG2900_FM_STATE_DEINITIALIZED: FM driver is not initialized.
 * @CG2900_FM_STATE_INITIALIZED: FM driver is initialized.
 * @CG2900_FM_STATE_SWITCHED_ON: FM driver is switched on and in active state.
 * @CG2900_FM_STATE_STAND_BY: FM Radio is switched on but not in active state.
 *
 * Various states of FM Driver.
 */
enum cg2900_fm_state {
	CG2900_FM_STATE_DEINITIALIZED,
	CG2900_FM_STATE_INITIALIZED,
	CG2900_FM_STATE_SWITCHED_ON,
	CG2900_FM_STATE_STAND_BY
};

/**
 * enum cg2900_fm_mode - FM Driver Command state .
 *
 * @CG2900_FM_IDLE_MODE: FM Radio is in Idle Mode.
 * @CG2900_FM_RX_MODE: FM Radio is configured in Rx mode.
 * @CG2900_FM_TX_MODE: FM Radio is configured in Tx mode.
 *
 * Various Modes of the FM Radio.
 */
enum cg2900_fm_mode {
	CG2900_FM_IDLE_MODE,
	CG2900_FM_RX_MODE,
	CG2900_FM_TX_MODE
};

/**
 * enum cg2900_fm_band - Various Frequency band supported.
 *
 * @CG2900_FM_BAND_US_EU: European / US Band.
 * @CG2900_FM_BAND_JAPAN: Japan Band.
 * @CG2900_FM_BAND_CHINA: China Band.
 * @CG2900_FM_BAND_CUSTOM: Custom Band.
 *
 * Various Frequency band supported.
 */
enum cg2900_fm_band {
	CG2900_FM_BAND_US_EU,
	CG2900_FM_BAND_JAPAN,
	CG2900_FM_BAND_CHINA,
	CG2900_FM_BAND_CUSTOM
};

/**
 * enum cg2900_fm_grid - Various Frequency grids supported.
 *
 * @CG2900_FM_GRID_50: 50 kHz spacing.
 * @CG2900_FM_GRID_100: 100 kHz spacing.
 * @CG2900_FM_GRID_200: 200 kHz spacing.
 *
 * Various Frequency grids supported.
 */
enum cg2900_fm_grid {
	CG2900_FM_GRID_50,
	CG2900_FM_GRID_100,
	CG2900_FM_GRID_200
};

/**
 * enum cg2900_fm_event - Various Events reported by FM API layer.
 *
 * @CG2900_EVENT_NO_EVENT: No Event.
 * @CG2900_EVENT_SEARCH_CHANNEL_FOUND: Seek operation is completed.
 * @CG2900_EVENT_SCAN_CHANNELS_FOUND: Band Scan is completed.
 * @CG2900_EVENT_BLOCK_SCAN_CHANNELS_FOUND: Block Scan is completed.
 * @CG2900_EVENT_SCAN_CANCELLED: Scan/Seek is cancelled.
 * @CG2900_EVENT_MONO_STEREO_TRANSITION: Mono/Stereo Transition has taken place.
 * @CG2900_EVENT_DEVICE_RESET: CG2900 has been reset by some other IP.
 * @CG2900_EVENT_RDS_EVENT: RDS data interrupt has been received from chip.
 *
 * Various Events reported by FM API layer.
 */
enum cg2900_fm_event {
	CG2900_EVENT_NO_EVENT,
	CG2900_EVENT_SEARCH_CHANNEL_FOUND,
	CG2900_EVENT_SCAN_CHANNELS_FOUND,
	CG2900_EVENT_BLOCK_SCAN_CHANNELS_FOUND,
	CG2900_EVENT_SCAN_CANCELLED,
	CG2900_EVENT_MONO_STEREO_TRANSITION,
	CG2900_EVENT_DEVICE_RESET,
	CG2900_EVENT_RDS_EVENT
};

/**
 * enum cg2900_fm_direction - Directions used while seek.
 *
 * @CG2900_DIR_DOWN: Search in downwards direction.
 * @CG2900_DIR_UP: Search in upwards direction.
 *
 * Directions used while seek.
 */
enum cg2900_fm_direction {
	CG2900_DIR_DOWN,
	CG2900_DIR_UP
};

/**
 * enum cg2900_fm_stereo_mode - Stereo Modes.
 *
 * @CG2900_MODE_MONO: Mono Mode.
 * @CG2900_MODE_STEREO: Stereo Mode.
 *
 * Stereo Modes.
 */
enum cg2900_fm_stereo_mode {
	CG2900_MODE_MONO,
	CG2900_MODE_STEREO
};

#define CG2900_FM_DEFAULT_RSSI_THRESHOLD			100
#define MAX_RDS_BUFFER					10
#define MAX_RDS_GROUPS					22
#define MIN_ANALOG_VOLUME				0
#define MAX_ANALOG_VOLUME				15
#define NUM_OF_RDS_BLOCKS				4
#define RDS_BLOCK_MASK					0x1C
#define RDS_ERROR_STATUS_MASK				0x03
#define RDS_UPTO_TWO_BITS_CORRECTED			0x01
#define RDS_UPTO_FIVE_BITS_CORRECTED			0x02
#define MAX_RT_SIZE					65
#define MAX_PSN_SIZE					9
#define DEFAULT_CHANNELS_TO_SCAN			32
#define MAX_CHANNELS_TO_SCAN				99
#define MAX_CHANNELS_FOR_BLOCK_SCAN			198
#define SKB_FM_INTERRUPT_DATA				2

extern u8 fm_event;
extern struct cg2900_fm_rds_buf fm_rds_buf[MAX_RDS_BUFFER][MAX_RDS_GROUPS];
extern struct cg2900_fm_rds_info fm_rds_info;

/**
 * cg2900_fm_init()- Initializes FM Radio.
 *
 * Initializes the Variables and structures required for FM Driver.
 * It also registers the callback to receive the events for command
 * completion, etc
 *
 * Returns:
 *   0,  if Initialization successful
 *   -EINVAL, otherwise.
 */
int cg2900_fm_init(void);

/**
 * cg2900_fm_deinit()- De-initializes FM Radio.
 *
 * De-initializes the Variables and structures required for FM Driver.
 *
 * Returns:
 *	 0, if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_deinit(void);

/**
 * cg2900_fm_switch_on()- Start up procedure of the FM radio.
 *
 * @device: Character device requesting the operation.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_switch_on(
			struct device *device
			);

/**
 * cg2900_fm_switch_off()- Switches off FM radio
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_switch_off(void);

/**
 * cg2900_fm_standby()- Makes the FM Radio Go in Standby mode.
 *
 * The FM Radio memorizes the the last state, i.e. Volume, last
 * tuned station, etc that helps in resuming quickly to previous state.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_standby(void);

/**
 * cg2900_fm_power_up_from_standby()- Power Up FM Radio from Standby mode.
 *
 * It retruns the FM radio to the same state as it was before
 * going to Standby.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_power_up_from_standby(void);

/**
 * cg2900_fm_set_rx_default_settings()- Loads FM Rx Default Settings.
 *
 * @freq: Frequency in Hz to be set on the FM Radio.
 * @band: Band To be Set.
 * (0: US/EU, 1: Japan, 2: China, 3: Custom)
 * @grid: Grid specifying Spacing.
 * (0: 50 KHz, 1: 100 KHz, 2: 200 Khz)
 * @enable_rds: Flag indicating enable or disable rds transmission.
 * @enable_stereo: Flag indicating enable or disable stereo mode.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_rx_default_settings(
			u32 freq,
			u8 band,
			u8 grid,
			bool enable_rds,
			bool enable_stereo
			);

/**
 * cg2900_fm_set_tx_default_settings()- Loads FM Tx Default Settings.
 *
 * @freq: Frequency in Hz to be set on the FM Radio.
 * @band: Band To be Set.
 * (0: US/EU, 1: Japan, 2: China, 3: Custom)
 * @grid: Grid specifying Spacing.
 * (0: 50 KHz, 1: 100 KHz, 2: 200 Khz)
 * @enable_rds: Flag indicating enable or disable rds transmission.
 * @enable_stereo: Flag indicating enable or disable stereo mode.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_tx_default_settings(
			u32 freq,
			u8 band,
			u8 grid,
			bool enable_rds,
			bool enable_stereo
			);

/**
 * cg2900_fm_set_grid()- Sets the Grid on the FM Radio.
 *
 * @grid: Grid specifying Spacing.
 * (0: 50 KHz,1: 100 KHz,2: 200 Khz)
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_grid(
			u8 grid
			);

/**
 * cg2900_fm_set_band()- Sets the Band on the FM Radio.
 *
 * @band: Band specifying Region.
 * (0: US_EU,1: Japan,2: China,3: Custom)
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_band(
			u8 band
			);

/**
 * cg2900_fm_search_up_freq()- seek Up.
 *
 * Searches the next available station in Upward Direction
 * starting from the Current freq.
 *
 * If the operation is started successfully, the chip will generate the
 * irpt_OperationSucced. interrupt when the operation is completed
 * and will tune to the next available frequency.
 * If no station is found, the chip is still tuned to the original station
 * before starting the search
 * Till the interrupt is received, no more API's should be called
 * except cg2900_fm_stop_scan
 *
 * Returns:
 *	 0,  if operation started successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_search_up_freq(void);

/**
 * cg2900_fm_search_down_freq()- seek Down.
 *
 * Searches the next available station in Downward Direction
 * starting from the Current freq.
 *
 * If the operation is started successfully, the chip will generate
 * the irpt_OperationSucced. interrupt when the operation is completed.
 * and will tune to the next available frequency. If no station is found,
 * the chip is still tuned to the original station before starting the search.
 * Till the interrupt is received, no more API's should be called
 * except cg2900_fm_stop_scan.
 *
 * Returns:
 *	 0,  if operation started successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_search_down_freq(void);

/**
 * cg2900_fm_start_band_scan()- Band Scan.
 *
 * Searches for available Stations in the entire Band starting from
 * current freq.
 * If the operation is started successfully, the chip will generate
 * the irpt_OperationSucced. interrupt when the operation is completed.
 * After completion the chip will still be tuned the original station before
 * starting the Scan. on reception of interrupt, the host should call the AP
 * cg2900_fm_get_scan_result() to retrieve the Stations and corresponding
 * RSSI of stations found in the Band.
 * Till the interrupt is received, no more API's should be called
 * except cg2900_fm_stop_scan, cg2900_fm_switch_off, cg2900_fm_standby and
 * cg2900_fm_get_frequency.
 *
 * Returns:
 *	 0,  if operation started successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_start_band_scan(void);

/**
 * cg2900_fm_stop_scan()- Stops an active ongoing seek or Band Scan.
 *
 * If the operation is started successfully, the chip will generate the
 * irpt_OperationSucced interrupt when the operation is completed.
 * Till the interrupt is received, no more API's should be called.
 *
 * Returns:
 *	 0,  if operation started successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_stop_scan(void);

/**
 * cg2900_fm_get_scan_result()- Retreives Band Scan Result
 *
 * Retrieves the Scan Band Results of the stations found and
 * the corressponding RSSI values of the stations.
 * @num_of_scanfreq: (out) Number of Stations found
 * during Scanning.
 * @scan_freq: (out) Frequency of Stations in Hz
 * found during Scanning.
 * @scan_freq_rssi_level: (out) RSSI level of Stations
 * found during Scanning.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_scan_result(
			u16 *num_of_scanfreq,
			u32 *scan_freq,
			u32 *scan_freq_rssi_level
			);

/**
 * cg2900_fm_start_block_scan()- Block Scan.
 *
 * Searches for RSSI level of all the channels between the start and stop
 * channels. If the operation is started successfully, the chip will generate
 * the irpt_OperationSucced interrupt when the operation is completed.
 * After completion the chip will still be tuned the original station before
 * starting the Scan. On reception of interrupt, the host should call the AP
 * cg2900_fm_get_block_scan_result() to retrieve the RSSI of channels.
 * Till the interrupt is received, no more API's should be called from Host
 * except cg2900_fm_stop_scan, cg2900_fm_switch_off, cg2900_fm_standby and
 * cg2900_fm_get_frequency.
 * @start_freq: Start channel block scan Frequency.
 * @end_freq:  End channel block scan Frequency
 *
 * Returns:
 *	 0,  if operation started successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_start_block_scan(
			u32 start_freq,
			u32 end_freq
			);

/**
 * cg2900_fm_get_scan_result()- Retreives Band Scan Result
 *
 * Retrieves the Scan Band Results of the stations found and
 * the corressponding RSSI values of the stations.
 * @num_of_scanchan: (out) Number of Stations found
 * during Scanning.
 * @scan_freq_rssi_level: (out) RSSI level of Stations
 * found during Scanning.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_block_scan_result(
			u16 *num_of_scanchan,
			u16 *scan_freq_rssi_level
			);

/**
 * cg2900_fm_tx_get_rds_deviation()- Gets RDS Deviation.
 *
 * Retrieves the RDS Deviation level set for FM Tx.
 * @deviation: (out) Rds Deviation.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_get_rds_deviation(
			u16 *deviation
			);

/**
 * cg2900_fm_tx_set_rds_deviation()- Sets RDS Deviation.
 *
 * Sets the RDS Deviation level on FM Tx.
 * @deviation: Rds Deviation to set on FM Tx.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_rds_deviation(
			u16 deviation
			);

/**
 * cg2900_fm_tx_set_pi_code()- Sets PI code for RDS Transmission.
 *
 * Sets the Program Identification code to be transmitted.
 * @pi_code: PI code to be transmitted.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_pi_code(
			u16 pi_code
			);

/**
 * cg2900_fm_tx_set_pty_code()- Sets PTY code for RDS Transmission.
 *
 * Sets the Program Type code to be transmitted.
 * @pty_code: PTY code to be transmitted.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_pty_code(
			u16 pty_code
			);

/**
 * cg2900_fm_tx_set_program_station_name()- Sets PSN for RDS Transmission.
 *
 * Sets the Program Station Name to be transmitted.
 * @psn: Program Station Name to be transmitted.
 * @len: Length of Program Station Name to be transmitted.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_program_station_name(
			char *psn,
			u8 len
			);

/**
 * cg2900_fm_tx_set_radio_text()- Sets RT for RDS Transmission.
 *
 * Sets the radio text to be transmitted.
 * @rt: Radio Text to be transmitted.
 * @len: Length of Radio Text to be transmitted.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_radio_text(
			char *rt,
			u8 len
			);

/**
 * cg2900_fm_tx_get_rds_deviation()- Gets Pilot Tone status
 *
 * Gets the current status of pilot tone for FM Tx.
 * @enable: (out) Flag indicating Pilot Tone is enabled or disabled.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_get_pilot_tone_status(
			bool *enable
			);

/**
 * cg2900_fm_tx_set_pilot_tone_status()- Enables/Disables Pilot Tone.
 *
 * Enables or disables the pilot tone for FM Tx.
 * @enable: Flag indicating enabling or disabling Pilot Tone.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_pilot_tone_status(
			bool enable
			);

/**
 * cg2900_fm_tx_get_pilot_deviation()- Gets Pilot Deviation.
 *
 * Retrieves the Pilot Tone Deviation level set for FM Tx.
 * @deviation: (out) Pilot Tone Deviation.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_get_pilot_deviation(
			u16 *deviation
			);

/**
 * cg2900_fm_tx_set_pilot_deviation()- Sets Pilot Deviation.
 *
 * Sets the Pilot Tone Deviation level on FM Tx.
 * @deviation: Pilot Tone Deviation to set.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_pilot_deviation(
			u16 deviation
			);

/**
 * cg2900_fm_tx_get_preemphasis()- Gets Pre-emhasis level.
 *
 * Retrieves the Preemphasis level set for FM Tx.
 * @preemphasis: (out) Preemphasis level.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_get_preemphasis(
			u8 *preemphasis
			);

/**
 * cg2900_fm_tx_set_preemphasis()- Sets Pre-emhasis level.
 *
 * Sets the Preemphasis level on FM Tx.
 * @preemphasis: Preemphasis level.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_preemphasis(
			u8 preemphasis
			);

/**
 * cg2900_fm_tx_get_power_level()- Gets Power level.
 *
 * Retrieves the Power level set for FM Tx.
 * @power_level: (out) Power level.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_get_power_level(
			u16 *power_level
			);

/**
 * cg2900_fm_tx_set_power_level()- Sets Power level.
 *
 * Sets the Power level for FM Tx.
 * @power_level: Power level.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_set_power_level(
			u16 power_level
			);

/**
 * cg2900_fm_tx_rds()- Enable or disable Tx RDS.
 *
 * Enable or disable RDS transmission.
 * @enable_rds: Flag indicating enabling or disabling RDS.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_tx_rds(
			bool enable_rds
			);

/**
 * cg2900_fm_set_audio_balance()- Sets Audio Balance.
 *
 * @balance: Audio Balnce to be Set in Percentage.
 * (-100: Right Mute.... 0: Both on.... 100: Left Mute)
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_audio_balance(
			s8 balance
			);

/**
 * cg2900_fm_set_aup_bt_setvolume()- Sets the Digital Out Gain of FM Chip.
 *
 * @vol_level: Volume Level to be set on Tuner (0-20).
 *
 * Returns:
 *   0,  if operation completed successfully.
 *   -EINVAL, otherwise.
 */
int cg2900_fm_set_aup_bt_setvolume(
            u8 vol_level
            );


/**
 * cg2900_fm_set_volume()- Sets the Analog Out Gain of FM Chip.
 *
 * @vol_level: Volume Level to be set on Tuner (0-20).
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_volume(
			u8 vol_level
			);

/**
 * cg2900_fm_get_volume()- Gets the currently set Digital\Analog Out Gain of FM Chip.
 *
 * @vol_level: (out)Volume Level set on Tuner (0-20).
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_volume(
			u8 *vol_level
			);

/**
 * cg2900_fm_rds_off()- Disables the RDS decoding algorithm in FM chip
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_rds_off(void);

/**
 * cg2900_fm_rds_on()- Enables the RDS decoding algorithm in FM chip
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_rds_on(void);

/**
 * cg2900_fm_get_rds_status()- Retrieves the status whether RDS is enabled or not
 *
 * @rds_status: (out) Status of RDS
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_rds_status(
			bool *rds_status
			);

/**
 * cg2900_fm_mute()- Mutes the Audio output from FM Chip
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_mute(void);

/**
 * cg2900_fm_unmute()- Unmutes the Audio output from FM Chip
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_unmute(void);

/**
 * cg2900_fm_get_frequency()- Gets the Curently tuned Frequency on FM Radio
 *
 * @freq: (out) Frequency in Hz set on the FM Radio.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_frequency(
			u32 *freq
			);

/**
 * cg2900_fm_set_frequency()- Sets the frequency on FM Radio
 *
 * @new_freq: Frequency in Hz to be set on the FM Radio.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_frequency(
			u32 new_freq
			);

/**
 * cg2900_fm_get_signal_strength()- Gets the RSSI level.
 *
 * @signal_strength: (out) RSSI level of the currently
 * tuned frequency.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_signal_strength(
			u16 *signal_strength
			);

/**
 * cg2900_fm_get_af_updat()- Retrives results of AF Update
 *
 * @af_update_rssi: (out) RSSI level of the Alternative frequency.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_af_update_get_result(
			u16 *af_update_rssi
			);


/**
 * cg2900_fm_af_update_start()- PErforms AF Update.
 *
 * @af_freq: AF frequency in Hz whose RSSI is to be retrived.
 * tuned frequency.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */

int cg2900_fm_af_update_start(
			u32 af_freq
			);

/**
 * cg2900_fm_af_switch_get_result()- Retrives the AF switch result.
 *
 * @af_switch_conclusion: (out) Conclusion of the AF Switch.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_af_switch_get_result(
			u16 *af_switch_conclusion
			);

/**
 * cg2900_fm_af_switch_start()- PErforms AF switch.
 *
 * @af_switch_freq: Alternate Frequency in Hz to be switched.
 * @af_switch_pi: picode of the Alternative frequency.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_af_switch_start(
			u32 af_switch_freq,
			u16 af_switch_pi
			);

/**
 * cg2900_fm_get_mode()- Gets the mode of the Radio tuner.
 *
 * @cur_mode: (out) Current mode set on FM Radio
 * (0: Stereo, 1: Mono, 2: Blending, 3: Switching).
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_mode(
			u8 *cur_mode
			);

/**
 * cg2900_fm_set_mode()- Sets the mode on the Radio tuner.
 *
 * @mode: mode to be set on FM Radio
 * (0: Stereo, 1: Mono, 2: Blending, 3: Switching.)
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_mode(
			u8 mode
			);

/**
 * cg2900_fm_select_antenna()- Selects the Antenna of the Radio tuner.
 *
 * @antenna: (0: Embedded, 1: Wired.)
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_select_antenna(
			u8 antenna
			);

/**
 * cg2900_fm_get_antenna()- Retreives the currently selected antenna.
 *
 * @antenna: out (0: Embedded, 1: Wired.)
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_antenna(
			u8 *antenna
			);

/**
 * cg2900_fm_get_rssi_threshold()- Gets the rssi threshold currently
 *
 * set on FM radio.
 * @rssi_thresold: (out) Current rssi threshold set.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_get_rssi_threshold(
			u16 *rssi_thresold
			);

/**
 * cg2900_fm_set_rssi_threshold()- Sets the rssi threshold to be used during
 *
 * Band Scan and seek Stations
 * @rssi_thresold: rssi threshold to be set.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_rssi_threshold(
			u16 rssi_thresold
			);

/**
 * cg2900_handle_device_reset()- Handle The reset of Device
 */
void cg2900_handle_device_reset(void);

/**
 * wake_up_poll_queue()- Wakes up the Task waiting on Poll Queue.
 * This function is called when Scan Band or seek has completed.
 */
void wake_up_poll_queue(void);

/**
 * void cg2900_fm_set_chip_version()- Sets the Version of the Controller.
 *
 * This function is used to update the Chip Version information at time
 * of intitialization of FM driver.
 * @revision:	Revision of the controller, e.g. to indicate that it is
 *			a CG2900 controller.
 * @sub_version:	Subversion of the controller, e.g. to indicate a certain
 *			tape-out of the controller.
 */
void cg2900_fm_set_chip_version(
			u16 revision,
			u16 sub_version
			);

/**
 * cg2900_fm_rx_set_deemphasis()- Sets de-emhasis level.
 *
 * Sets the Deemphasis level on FM Rx.
 * @deemphasis: Deemphasis level.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_rx_set_deemphasis(
			u8 deemphasis
			);

/**
 * cg2900_fm_set_test_tone_generator()- Sets the Test Tone Generator.
 *
 * This function is used to enable/disable the Internal Tone Generator of
 * CG2900.
 * @test_tone_status: Status of tone generator.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_set_test_tone_generator(
			u8 test_tone_status
			);


/**
 * cg2900_fm_test_tone_connect()- Connect Audio outputs/inputs.
 *
 * This function connects the audio outputs/inputs of the Internal Tone
 * Generator of CG2900.
 * @left_audio_mode: Left Audio Output Mode.
 * @right_audio_mode: Right Audio Output Mode.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_test_tone_connect(
			u8 left_audio_mode,
			u8 right_audio_mode
			);

/**
 * cg2900_fm_test_tone_set_params()- Sets the Test Tone Parameters.
 *
 * This function is used to set the parameters of the Internal Tone Generator of
 * CG2900.
 * @tone_gen: Tone to be configured (Tone 1 or Tone 2)
 * @frequency: Frequency of the tone.
 * @volume: Volume of the tone.
 * @phase_offset: Phase offset of the tone.
 * @dc: DC to add to tone.
 * @waveform: Waveform to generate, sine or pulse.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise.
 */
int cg2900_fm_test_tone_set_params(
			u8 tone_gen,
			u16 frequency,
			u16 volume,
			u16 phase_offset,
			u16 dc,
			u8 waveform
			);

#endif /* CG2900_FM_API_H */
