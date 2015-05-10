/*
 * Copyright ST-Ericsson 2011.
 *
 * Author: Jarmo K. Kuronen <jarmo.kuronen@symbio.com> for ST Ericsson.
 * Licensed under GPLv2.
 */

#ifndef _ABx500_ACCDET_H
#define _ABx500_ACCDET_H

#include <linux/interrupt.h>

/*
* Debounce times for AccDet1 input
* @0x880 [2:0]
*/
#define ACCDET1_DB_0ms		0x00
#define ACCDET1_DB_10ms		0x01
#define ACCDET1_DB_20ms		0x02
#define ACCDET1_DB_30ms		0x03
#define ACCDET1_DB_40ms		0x04
#define ACCDET1_DB_50ms		0x05
#define ACCDET1_DB_60ms		0x06
#define ACCDET1_DB_70ms		0x07

/*
* Voltage threshold for AccDet1 input
* @0x880 [6:3]
*/
#define ACCDET1_TH_1100mV	0x40
#define ACCDET1_TH_1200mV	0x48
#define ACCDET1_TH_1300mV	0x50
#define ACCDET1_TH_1400mV	0x58
#define ACCDET1_TH_1500mV	0x60
#define ACCDET1_TH_1600mV	0x68
#define ACCDET1_TH_1700mV	0x70
#define ACCDET1_TH_1800mV	0x78

/*
* Voltage threshold for AccDet21 input
* @0x881 [3:0]
*/
#define ACCDET21_TH_300mV	0x00
#define ACCDET21_TH_400mV	0x01
#define ACCDET21_TH_500mV	0x02
#define ACCDET21_TH_600mV	0x03
#define ACCDET21_TH_700mV	0x04
#define ACCDET21_TH_800mV	0x05
#define ACCDET21_TH_900mV	0x06
#define ACCDET21_TH_1000mV	0x07
#define ACCDET21_TH_1100mV	0x08
#define ACCDET21_TH_1200mV	0x09
#define ACCDET21_TH_1300mV	0x0a
#define ACCDET21_TH_1400mV	0x0b
#define ACCDET21_TH_1500mV	0x0c
#define ACCDET21_TH_1600mV	0x0d
#define ACCDET21_TH_1700mV	0x0e
#define ACCDET21_TH_1800mV	0x0f

/*
* Voltage threshold for AccDet22 input
* @0x881 [7:4]
*/
#define ACCDET22_TH_300mV	0x00
#define ACCDET22_TH_400mV	0x10
#define ACCDET22_TH_500mV	0x20
#define ACCDET22_TH_600mV	0x30
#define ACCDET22_TH_700mV	0x40
#define ACCDET22_TH_800mV	0x50
#define ACCDET22_TH_900mV	0x60
#define ACCDET22_TH_1000mV	0x70
#define ACCDET22_TH_1100mV	0x80
#define ACCDET22_TH_1200mV	0x90
#define ACCDET22_TH_1300mV	0xa0
#define ACCDET22_TH_1400mV	0xb0
#define ACCDET22_TH_1500mV	0xc0
#define ACCDET22_TH_1600mV	0xd0
#define ACCDET22_TH_1700mV	0xe0
#define ACCDET22_TH_1800mV	0xf0

/*
* Voltage threshold for AccDet1 input
* @0x880 [6:3]
*/
#define ACCDET1_TH_300mV	0x00
#define ACCDET1_TH_400mV	0x01
#define ACCDET1_TH_500mV	0x02
#define ACCDET1_TH_600mV	0x03
#define ACCDET1_TH_700mV	0x04
#define ACCDET1_TH_800mV	0x05
#define ACCDET1_TH_900mV	0x06
#define ACCDET1_TH_1000mV	0x07

#define MAX_DET_COUNT			10
#define MAX_VOLT_DIFF			30
#define MIN_MIC_POWER			-100

/**
 * struct abx500_accdet_platform_data - AV Accessory detection specific
 * platform data
 * @btn_keycode		Keycode to be sent when accessory button is pressed.
 * @accdet1_dbth	Debounce time + voltage threshold for accdet 1 input.
 * @accdet2122_th	Voltage thresholds for accdet21 and accdet22 inputs.
 * @is_detection_inverted	Whether the accessory insert/removal, button
 * press/release irq's are inverted.
 * @mic_ctrl	Gpio to select between CVBS and MIC.
 * @nahj_ctrl	Gpio to select between NAHJ and OMTP Headset.
 * @video_ctrl_gpio_inverted	video_ctrl Gpio settings are inverted, as
 * compared to the previously used settings.
 */
struct abx500_accdet_platform_data {
	int btn_keycode;
	u8 accdet1_dbth;
	u8 accdet2122_th;
	unsigned int video_ctrl_gpio;
	bool is_detection_inverted;
	unsigned int mic_ctrl;
	unsigned int nahj_ctrl;
	bool video_ctrl_gpio_inverted;
};

/* Enumerations */

/**
 * @JACK_TYPE_UNSPECIFIED Not known whether any accessories are connected.
 * @JACK_TYPE_DISCONNECTED No accessories connected.
 * @JACK_TYPE_CONNECTED Accessory is connected but functionality was unable to
 * detect the actual type. In this mode, possible button events are reported.
 * @JACK_TYPE_HEADPHONE Headphone type of accessory (spkrs only) connected
 * @JACK_TYPE_HEADSET Headset type of accessory (mic+spkrs) connected
 * @JACK_TYPE_UNSUPPORTED_HEADSET Unsupported headset of type accessory connected
 * @JACK_TYPE_CARKIT Carkit type of accessory connected
 * @JACK_TYPE_OPENCABLE Open cable connected
 */
enum accessory_jack_type {
	JACK_TYPE_UNSPECIFIED,
	JACK_TYPE_DISCONNECTED,
	JACK_TYPE_CONNECTED,
	JACK_TYPE_HEADPHONE,
	JACK_TYPE_HEADSET,
	JACK_TYPE_UNSUPPORTED_HEADSET,
	JACK_TYPE_CARKIT,
	JACK_TYPE_OPENCABLE
};

/**
 * @BUTTON_UNK Button state not known
 * @BUTTON_PRESSED Button "down"
 * @BUTTON_RELEASED Button "up"
 */
enum accessory_button_state {
	BUTTON_UNK,
	BUTTON_PRESSED,
	BUTTON_RELEASED
};

/**
 * @PLUG_IRQ Interrupt gen. when accessory plugged in
 * @UNPLUG_IRQ Interrupt gen. when accessory plugged out
 * @BUTTON_PRESS_IRQ Interrupt gen. when accessory button pressed.
 * @BUTTON_RELEASE_IRQ Interrupt gen. when accessory button released.
 */
enum accessory_irq {
	PLUG_IRQ,
	UNPLUG_IRQ,
	BUTTON_PRESS_IRQ,
	BUTTON_RELEASE_IRQ,
};

/**
 * Enumerates the op. modes of the avcontrol switch
 * @AUDIO_IN Audio input is selected
 * @VIDEO_OUT Video output is selected
 * @NOT_SET The av-switch control signal is disconnected.
 */
enum accessory_avcontrol_dir {
	AUDIO_IN,
	VIDEO_OUT,
	NOT_SET,
};

/**
 * @REGULATOR_VAUDIO v-audio regulator
 * @REGULATOR_VAMIC1 v-amic1 regulator
 * @REGULATOR_AVSWITCH Audio/Video select switch regulator
 * @REGULATOR_ALL All regulators combined
 */
enum accessory_regulator {
	REGULATOR_NONE = 0x0,
	REGULATOR_VAUDIO = 0x1,
	REGULATOR_VAMIC1 = 0x2,
	REGULATOR_AVSWITCH = 0x4,
	REGULATOR_ALL = 0xFF
};

/* Structures */

/**
 * Describes an interrupt
 * @irq interrupt identifier
 * @name name of the irq in platform data
 * @isr interrupt service routine
 * @register are we currently registered to receive interrupts from this source.
 */
struct accessory_irq_descriptor {
	enum accessory_irq irq;
	const char *name;
	irq_handler_t isr;
	int registered;
};

/**
 * Encapsulates info of single regulator.
 * @id regulator identifier
 * @name name of the regulator
 * @enabled flag indicating whether regu is currently enabled.
 * @handle regulator handle
 */
struct accessory_regu_descriptor {
	enum accessory_regulator id;
	const char *name;
	int enabled;
	struct regulator *handle;
};

/**
 * Defines attributes for accessory detection operation.
 * @typename type as string
 * @type Type of accessory this task tests
 * @req_det_count How many times this particular type of accessory
 * needs to be detected in sequence in order to accept. Multidetection
 * implemented to avoid false detections during plug-in.
 * @meas_mv Should ACCDETECT2 input voltage be measured just before
 * making the decision or can cached voltage be used instead.
 * @minvol minimum voltage (mV) for decision
 * @maxvol maximum voltage (mV) for decision
 * @alt_minvol minimum alternative voltage (mV) for decision
 * @alt_maxvol maximum alternative voltage (mV) for decision
 * @nahj_headset is a nahj headset
 */
struct accessory_detect_task {
	const char *typename;
	enum accessory_jack_type type;
	int req_det_count;
	int meas_mv;
	int minvol;
	int maxvol;
	int alt_minvol;
	int alt_maxvol;
	bool nahj_headset;
};

/**
 * Device data, capsulates all relevant device data structures.
 *
 * @pdev: pointer to platform device
 * @pdata: Platform data
 * @gpadc: interface for ADC data
 * @irq_work_queue: Work queue for deferred interrupt processing
 * @detect_work: work item to perform detection work
 * @unplug_irq_work: work item to process unplug event
 * @init_work: work item to process initialization work.
 * @btn_input_dev: button input device used to report btn presses
 * @btn_state: Current state of accessory button
 * @jack_type: type of currently connected accessory
 * @reported_jack_type: previously reported jack type.
 * @jack_type_temp: temporary storage for currently connected accessory
 * @jack_det_count: counter how many times in sequence the accessory
 *	type detection has produced same result.
 * @total_jack_det_count: after plug-in irq, how many times detection
 *	has totally been made in order to detect the accessory type
 * @detect_jiffies: Used to save timestamp when detection was made. Timestamp
 *	used to filter out spurious button presses that might occur during the
 *	plug-in procedure.
 * @accdet1_th_set: flag to indicate whether accdet1 threshold and debounce
 *	times are configured
 * @accdet2_th_set: flag to indicate whether accdet2 thresholds are configured
 * @irq_desc_norm: irq's as specified in the initial versions of ab
 * @irq_desc_inverted: irq's inverted as seen in the latest versions of ab
 * @no_irqs: Total number of irq's
 * @regu_desc: Pointer to the regulator descriptors.
 * @no_of_regu_desc: Total nummber of descriptors.
 * @config_accdetect2_hw: Callback for configuring accdet2 comparator.
 * @config_accdetect1_hw: Callback for configuring accdet1 comparator.
 * @detect_plugged_in: Callback to detect type of accessory connected.
 * @meas_voltage_stable: Callback to read present accdet voltage.
 * @meas_alt_voltage_stable: Callback to read present alt accdet voltage.
 * @config_hw_test_basic_carkit: Callback to configure hw for carkit
 *	detect.
 * @turn_of_accdet_comparator: Call back to turn off comparators.
 * @turn_on_accdet_comparator: Call back to turn ON comparators.
 * @accdet_abx500_gpadc_get Call back to get a instance of the
 *	GPADC convertor.
 * @config_hw_test_plug_connected: Call back to configure the hw for
 *	accessory detection.
 * @set_av_switch: Call back to configure the switch for tvout or audioout.
 * @get_platform_data: call to get platform specific data.
 */
struct abx500_ad {
	struct platform_device *pdev;
	struct abx500_accdet_platform_data *pdata;
	void *gpadc;
	struct workqueue_struct *irq_work_queue;

	struct delayed_work detect_work;
	struct delayed_work unplug_irq_work;
	struct delayed_work init_work;

	struct input_dev *btn_input_dev;
	enum accessory_button_state btn_state;

	enum accessory_jack_type jack_type;
	enum accessory_jack_type reported_jack_type;
	enum accessory_jack_type jack_type_temp;

	int jack_det_count;
	int total_jack_det_count;

	unsigned long detect_jiffies;

	int accdet1_th_set;
	int accdet2_th_set;

	struct accessory_irq_descriptor *irq_desc_norm;
	struct accessory_irq_descriptor *irq_desc_inverted;
	int no_irqs;

	struct accessory_regu_descriptor *regu_desc;
	int no_of_regu_desc;

	void (*config_accdetect2_hw)(struct abx500_ad *, int);
	void (*config_accdetect1_hw)(struct abx500_ad *, int);
	int (*detect_plugged_in)(struct abx500_ad *);
	int (*meas_voltage_stable)(struct abx500_ad *);
	int (*meas_alt_voltage_stable)(struct abx500_ad *);
	void (*config_hw_test_basic_carkit)(struct abx500_ad *, int);
	void (*turn_off_accdet_comparator)(struct platform_device *pdev);
	void (*turn_on_accdet_comparator)(struct platform_device *pdev);
	void* (*accdet_abx500_gpadc_get)(void);
	void (*config_hw_test_plug_connected)(struct abx500_ad *dd, int enable);
	void (*set_av_switch)(struct abx500_ad *dd,
		enum accessory_avcontrol_dir dir, bool);
	struct abx500_accdet_platform_data *
	(*get_platform_data)(struct platform_device *pdev);
};

/* Forward declarations */
extern irqreturn_t unplug_irq_handler(int irq, void *_userdata);
extern irqreturn_t plug_irq_handler(int irq, void *_userdata);
extern irqreturn_t button_press_irq_handler(int irq, void *_userdata);
extern irqreturn_t button_release_irq_handler(int irq, void *_userdata);
extern void accessory_regulator_enable(struct abx500_ad *dd,
		enum accessory_regulator reg);
extern void accessory_regulator_disable(struct abx500_ad *dd,
		enum accessory_regulator reg);
extern void report_jack_status(struct abx500_ad *dd);

#ifdef CONFIG_INPUT_AB5500_ACCDET
extern struct abx500_ad ab5500_accessory_det_callbacks;
#endif

#ifdef CONFIG_INPUT_AB8500_ACCDET
extern struct abx500_ad ab8500_accessory_det_callbacks;
#endif

#endif /* _ABx500_ACCDET_H */
