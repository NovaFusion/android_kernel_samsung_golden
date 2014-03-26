/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors:
 * Par-Gunnar Hjalmdahl (par-gunnar.p.hjalmdahl@stericsson.com) for ST-Ericsson.
 * Henrik Possung (henrik.possung@stericsson.com) for ST-Ericsson.
 * Josef Kindberg (josef.kindberg@stericsson.com) for ST-Ericsson.
 * Dariusz Szymszak (dariusz.xd.szymczak@stericsson.com) for ST-Ericsson.
 * Kjell Andersson (kjell.k.andersson@stericsson.com) for ST-Ericsson.
 * Hemant Gupta (hemant.gupta@stericsson.com) for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Linux Bluetooth HCI H:4 Driver for ST-Ericsson CG2900 GPS/BT/FM controller.
 */

#ifndef _CG2900_CHIP_H_
#define _CG2900_CHIP_H_

/*
 *	Utility
 */

static inline void set_low_nibble(__u8 *var, __u8 value)
{
	*var = (*var & 0xf0) | (value & 0x0f);
}

static inline void set_high_nibble(__u8 *var, __u8 value)
{
	*var = (*var & 0x0f) | (value << 4);
}

static inline void store_bit(__u8 *var, size_t bit, __u8 value)
{
	*var = (*var & ~(1u << bit)) | (value << bit);
}

/*
 *	General chip defines
 */

/* Supported chips */
#define CG2900_SUPP_MANUFACTURER			0x30

/*
 *	Bluetooth
 */

#define BT_SIZE_OF_HDR				(sizeof(__le16) + sizeof(__u8))
#define BT_PARAM_LEN(__pkt_len)			(__pkt_len - BT_SIZE_OF_HDR)

struct bt_cmd_cmpl_event {
	__u8	eventcode;
	__u8	plen;
	__u8	n_commands;
	__le16	opcode;
	/*
	 * According to BT-specification what follows is "parameters"
	 * and unique to every command, but all commands start the
	 * parameters with the status field so include it here for
	 * convenience
	 */
	__u8	status;
	__u8	data[];
} __packed;

/* BT VS Store In FS command */
#define CG2900_BT_OP_VS_STORE_IN_FS			0xFC22
struct bt_vs_store_in_fs_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	user_id;
	__u8	len;
	__u8	data[];
} __packed;

#define CG2900_VS_STORE_IN_FS_USR_ID_BD_ADDR		0xFE

#define HCI_EV_VENDOR_SPECIFIC					0xFF
#define CG2900_EV_VS_WRITE_FILE_BLOCK_COMPLETE	0x60
/* BT VS Event */
struct bt_vs_evt {
	__u8	evt_id;
	__u8	data[];
} __packed;

/* BT VS Write File Block Event */
struct bt_vs_write_file_block_evt {
	__u8	status;
	__u8	file_blk_id;
} __packed;

/* BT VS Write File Block command */
#define CG2900_BT_OP_VS_WRITE_FILE_BLOCK		0xFC2E
struct bt_vs_write_file_block_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	id;
	__u8	data[];
} __packed;

#define CG2900_BT_DISABLE				0x00
#define CG2900_BT_ENABLE				0x01

/* BT VS BT Enable command */
#define CG2900_BT_OP_VS_BT_ENABLE			0xFF10
struct bt_vs_bt_enable_cmd {
	__le16	op_code;
	u8	plen;
	u8	enable;
} __packed;

/* BT VS SetBaudRate command */
#define CG2900_BT_OP_VS_SET_BAUD_RATE			0xFC09
struct bt_vs_set_baud_rate_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	baud_rate;
} __packed;

#define CG2900_BT_SELFTEST_SUCCESSFUL			0x00
#define CG2900_BT_SELFTEST_FAILED			0x01
#define CG2900_BT_SELFTEST_NOT_COMPLETED		0x02

/* BT VS ReadSelfTestsResult command & event */
#define CG2900_BT_OP_VS_READ_SELTESTS_RESULT		0xFC10
struct bt_vs_read_selftests_result_evt {
	__u8	status;
	__u8	result;
} __packed;

/* Bluetooth Vendor Specific Opcodes */
#define CG2900_BT_OP_VS_POWER_SWITCH_OFF		0xFD40
#define CG2900_BT_OP_VS_SYSTEM_RESET			0xFF12

#define CG2900_BT_OPCODE_NONE				0xFFFF

/*
 *	Common multimedia
 */

#define CG2900_CODEC_TYPE_NONE				0x00
#define CG2900_CODEC_TYPE_SBC				0x01

#define CG2900_PCM_MODE_SLAVE				0x00
#define CG2900_PCM_MODE_MASTER				0x01

#define CG2900_I2S_MODE_MASTER				0x00
#define CG2900_I2S_MODE_SLAVE				0x01

/*
 *	CG2900 PG1 multimedia API
 */

#define CG2900_BT_VP_TYPE_PCM				0x00
#define CG2900_BT_VP_TYPE_I2S				0x01
#define CG2900_BT_VP_TYPE_SLIMBUS			0x02
#define CG2900_BT_VP_TYPE_FM				0x03
#define CG2900_BT_VP_TYPE_BT_SCO			0x04
#define CG2900_BT_VP_TYPE_BT_A2DP			0x05
#define CG2900_BT_VP_TYPE_ANALOG			0x07

#define CG2900_BT_VS_SET_HARDWARE_CONFIG		0xFD54
/* These don't have the same length, so a union won't work */
struct bt_vs_set_hw_cfg_cmd_pcm {
	__le16	opcode;
	__u8	plen;
	__u8	vp_type;
	__u8	port_id;
	__u8	mode_dir; /* NB: mode is in bit 1 (not 0) */
	__u8	bit_clock;
	__le16	frame_len;
} __packed;
#define HWCONFIG_PCM_SET_MODE(pcfg, mode)		\
	set_low_nibble(&(pcfg)->mode_dir, (mode) << 1)
#define HWCONFIG_PCM_SET_DIR(pcfg, idx, dir)		\
	store_bit(&(pcfg)->mode_dir, (idx) + 4, (dir))

struct bt_vs_set_hw_cfg_cmd_i2s {
	__le16	opcode;
	__u8	plen;
	__u8	vp_type;
	__u8	port_id;
	__u8	half_period;
	__u8	master_slave;
} __packed;

/* Max length for allocating */
#define CG2900_BT_LEN_VS_SET_HARDWARE_CONFIG	\
	(sizeof(struct bt_vs_set_hw_cfg_cmd_pcm))

#define CG2900_BT_VS_SET_SESSION_CONFIG			0xFD55
struct session_config_vport {
	__u8	type;
	union {
		struct {
			__le16	acl_handle;
			__u8	reserved[10];
		} sco;
		struct {
			__u8	reserved[12];
		} fm;
		struct {
			__u8	index;
			__u8	slots_used;
			__u8	slot_start[4];
			__u8	reserved[6];
		} pcm;
		struct {
			__u8	index;
			__u8	channel;
			__u8	reserved[10];
		} i2s;
	};
} __packed;
#define SESSIONCFG_PCM_SET_USED(port, idx, use)		\
	store_bit(&(port).pcm.slots_used, (idx), (use))

struct session_config_stream {
	__u8	media_type;
	__u8	csel_srate;
	__u8	codec_type;
	__u8	codec_mode;
	__u8	codec_params[3];
	struct session_config_vport inport;
	struct session_config_vport outport;
} __packed;
#define SESSIONCFG_SET_CHANNELS(pcfg, chnl)		\
	set_low_nibble(&(pcfg)->csel_srate, (chnl))
#define SESSIONCFG_I2S_SET_SRATE(pcfg, rate)		\
	set_high_nibble(&(pcfg)->csel_srate, (rate))

struct bt_vs_session_config_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	n_streams; /* we only support one here */
	struct session_config_stream stream;
} __packed;

#define CG2900_BT_SESSION_MEDIA_TYPE_AUDIO		0x00

#define CG2900_BT_SESSION_RATE_8K			0x01
#define CG2900_BT_SESSION_RATE_16K			0x02
#define CG2900_BT_SESSION_RATE_44_1K			0x04
#define CG2900_BT_SESSION_RATE_48K			0x05

#define CG2900_BT_MEDIA_CONFIG_MONO			0x00
#define CG2900_BT_MEDIA_CONFIG_STEREO			0x01
#define CG2900_BT_MEDIA_CONFIG_JOINT_STEREO		0x02
#define CG2900_BT_MEDIA_CONFIG_DUAL_CHANNEL		0x03

#define CG2900_BT_SESSION_I2S_INDEX_I2S			0x00
#define CG2900_BT_SESSION_PCM_INDEX_PCM_I2S		0x00


#define CG2900_BT_VS_SESSION_CTRL			0xFD57
struct bt_vs_session_ctrl_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	id;
	__u8	control;
} __packed;

#define CG2900_BT_SESSION_START				0x00
#define CG2900_BT_SESSION_STOP				0x01
#define CG2900_BT_SESSION_PAUSE				0x02
#define CG2900_BT_SESSION_RESUME			0x03

#define CG2900_BT_VS_RESET_SESSION_CONFIG		0xFD56
struct bt_vs_reset_session_cfg_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	id;
} __packed;

/*
 *	CG2900 PG2 multimedia API
 */

#define CG2900_MC_PORT_PCM_I2S				0x00
#define CG2900_MC_PORT_I2S				0x01
#define CG2900_MC_PORT_BT_SCO				0x04
#define CG2900_MC_PORT_FM_RX_0				0x07
#define CG2900_MC_PORT_FM_RX_1				0x08
#define CG2900_MC_PORT_FM_TX				0x09

#define CG2900_MC_VS_PORT_CONFIG			0xFD64
struct mc_vs_port_cfg_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	type;
	/*
	 * one of the following configuration structs should follow, but they
	 * have different lengths so a union will not work
	 */
} __packed;

struct mc_vs_port_cfg_pcm_i2s {
	__u8 role_dir;
	__u8 sco_a2dp_slots_used;
	__u8 fm_slots_used;
	__u8 ring_slots_used;
	__u8 slot_start[4];
	__u8 ratio_mode;
	__u8 frame_len;
	__u8 bitclk_srate;
} __packed;
#define PORTCFG_PCM_SET_ROLE(cfg, role)			\
	set_low_nibble(&(cfg).role_dir, (role))
#define PORTCFG_PCM_SET_DIR(cfg, idx, dir)		\
	store_bit(&(cfg).role_dir, (idx) + 4, (dir))
static inline void portcfg_pcm_set_sco_used(struct mc_vs_port_cfg_pcm_i2s *cfg,
					    size_t index, __u8 use)
{
	if (use) {
		/* clear corresponding slot in all cases */
		cfg->sco_a2dp_slots_used &= ~(0x11 << index);
		cfg->fm_slots_used &= ~(0x11 << index);
		cfg->ring_slots_used &= ~(0x11 << index);
		/* set for sco */
		cfg->sco_a2dp_slots_used |= (1u << index);
	} else {
		/* only clear for sco */
		cfg->sco_a2dp_slots_used &= ~(1u << index);
	}
}
#define PORTCFG_PCM_SET_SCO_USED(cfg, idx, use)		\
	portcfg_pcm_set_sco_used(&cfg, idx, use)
#define PORTCFG_PCM_SET_RATIO(cfg, r)			\
	set_low_nibble(&(cfg).ratio_mode, (r))
#define PORTCFG_PCM_SET_MODE(cfg, mode)			\
	set_high_nibble(&(cfg).ratio_mode, (mode))
#define PORTCFG_PCM_SET_BITCLK(cfg, clk)		\
	set_low_nibble(&(cfg).bitclk_srate, (clk))
#define PORTCFG_PCM_SET_SRATE(cfg, rate)		\
	set_high_nibble(&(cfg).bitclk_srate, (rate))

#define CG2900_MC_PCM_SAMPLE_RATE_8			1
#define CG2900_MC_PCM_SAMPLE_RATE_16			2
#define CG2900_MC_PCM_SAMPLE_RATE_44_1			4
#define CG2900_MC_PCM_SAMPLE_RATE_48			6

struct mc_vs_port_cfg_i2s {
	__u8 role_hper;
	__u8 csel_srate;
	__u8 wordlen;
};
#define PORTCFG_I2S_SET_ROLE(cfg, role)			\
	set_low_nibble(&(cfg).role_hper, (role))
#define PORTCFG_I2S_SET_HALFPERIOD(cfg, hper)		\
	set_high_nibble(&(cfg).role_hper, (hper))
#define PORTCFG_I2S_SET_CHANNELS(cfg, chnl)		\
	set_low_nibble(&(cfg).csel_srate, (chnl))
#define PORTCFG_I2S_SET_SRATE(cfg, rate)		\
	set_high_nibble(&(cfg).csel_srate, (rate))
#define PORTCFG_I2S_SET_WORDLEN(cfg, len)		\
	set_low_nibble(&(cfg).wordlen, len)

#define CG2900_MC_I2S_RIGHT_CHANNEL			1
#define CG2900_MC_I2S_LEFT_CHANNEL			2
#define CG2900_MC_I2S_BOTH_CHANNELS			3

#define CG2900_MC_I2S_SAMPLE_RATE_8			0
#define CG2900_MC_I2S_SAMPLE_RATE_16			1
#define CG2900_MC_I2S_SAMPLE_RATE_44_1			2
#define CG2900_MC_I2S_SAMPLE_RATE_48			4

#define CG2900_MC_I2S_WORD_16				1
#define CG2900_MC_I2S_WORD_32				3

struct mc_vs_port_cfg_fm {
	__u8 srate; /* NB: value goes in _upper_ nibble! */
};
#define PORTCFG_FM_SET_SRATE(cfg, rate)		\
	set_high_nibble(&(cfg).srate, (rate))

struct mc_vs_port_cfg_sco {
	__le16	acl_id;
	__u8	wbs_codec;
} __packed;
#define PORTCFG_SCO_SET_CODEC(cfg, codec)	\
	set_high_nibble(&(cfg).wbs_codec, (codec))

#define CG2900_MC_VS_CREATE_STREAM			0xFD66
struct mc_vs_create_stream_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	id;
	__u8	inport;
	__u8	outport;
	__u8	order; /* NB: not used by chip */
} __packed;

#define CG2900_MC_VS_DELETE_STREAM			0xFD67
struct mc_vs_delete_stream_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	stream;
} __packed;

#define CG2900_MC_VS_STREAM_CONTROL			0xFD68
struct mc_vs_stream_ctrl_cmd {
	__le16	opcode;
	__u8	plen;
	__u8	command;
	__u8	n_streams;
	__u8	stream[];
} __packed;

#define CG2900_MC_STREAM_START				0x00
#define CG2900_MC_STREAM_STOP				0x01
#define CG2900_MC_STREAM_STOP_FLUSH			0x02

#define CG2900_MC_VS_SET_FM_START_MODE			0xFD69

/*
 *	FM
 */

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

/* FM Opcode generic*/
#define CG2900_FM_GEN_ID_LEGACY				0xFE

/* FM event*/
#define CG2900_FM_EVENT_UNKNOWN				0
#define CG2900_FM_EVENT_CMD_COMPLETE			1
#define CG2900_FM_EVENT_INTERRUPT			2

/* FM do-command identifiers. */
#define CG2900_FM_DO_AIP_FADE_START			0x0046
#define CG2900_FM_DO_AUP_BT_FADE_START			0x01C2
#define CG2900_FM_DO_AUP_EXT_FADE_START			0x0102
#define CG2900_FM_DO_AUP_FADE_START			0x00A2
#define CG2900_FM_DO_FMR_SETANTENNA			0x0663
#define CG2900_FM_DO_FMR_SP_AFSWITCH_START		0x04A3
#define CG2900_FM_DO_FMR_SP_AFUPDATE_START		0x0463
#define CG2900_FM_DO_FMR_SP_BLOCKSCAN_START		0x0683
#define CG2900_FM_DO_FMR_SP_PRESETPI_START		0x0443
#define CG2900_FM_DO_FMR_SP_SCAN_START			0x0403
#define CG2900_FM_DO_FMR_SP_SEARCH_START		0x03E3
#define CG2900_FM_DO_FMR_SP_SEARCHPI_START		0x0703
#define CG2900_FM_DO_FMR_SP_TUNE_SETCHANNEL		0x03C3
#define CG2900_FM_DO_FMR_SP_TUNE_STEPCHANNEL		0x04C3
#define CG2900_FM_DO_FMT_PA_SETCTRL			0x01A4
#define CG2900_FM_DO_FMT_PA_SETMODE			0x01E4
#define CG2900_FM_DO_FMT_SP_TUNE_SETCHANNEL		0x0064
#define CG2900_FM_DO_GEN_ANTENNACHECK_START		0x02A1
#define CG2900_FM_DO_GEN_GOTOMODE			0x0041
#define CG2900_FM_DO_GEN_POWERSUPPLY_SETMODE		0x0221
#define CG2900_FM_DO_GEN_SELECTREFERENCECLOCK		0x0201
#define CG2900_FM_DO_GEN_SETPROCESSINGCLOCK		0x0241
#define CG2900_FM_DO_GEN_SETREFERENCECLOCKPLL		0x01A1
#define CG2900_FM_DO_TST_TX_RAMP_START			0x0147
#define CG2900_FM_CMD_NONE				0xFFFF
#define CG2900_FM_CMD_ID_GEN_GOTO_POWER_DOWN		0x0081
#define CG2900_FM_CMD_ID_GEN_GOTO_STANDBY		0x0061

/* FM Command IDs */
#define CG2900_FM_CMD_ID_AUP_EXT_SET_MODE		0x0162
#define CG2900_FM_CMD_ID_AUP_EXT_SET_CTRL		0x0182
#define CG2900_FM_CMD_ID_AUP_BT_SET_MODE		0x00C2
#define CG2900_FM_CMD_ID_AUP_BT_SET_CTRL		0x00E2
#define CG2900_FM_CMD_ID_AIP_SET_MODE			0x01C6
#define CG2900_FM_CMD_ID_AIP_BT_SET_CTRL		0x01A6
#define CG2900_FM_CMD_ID_AIP_BT_SET_MODE		0x01E6

/* FM Command Parameters. */
#define CG2900_FM_CMD_PARAM_ENABLE			0x00
#define CG2900_FM_CMD_PARAM_DISABLE			0x01
#define CG2900_FM_CMD_PARAM_RESET			0x02
#define CG2900_FM_CMD_PARAM_WRITECOMMAND		0x10
#define CG2900_FM_CMD_PARAM_SET_INT_MASK_ALL		0x20
#define CG2900_FM_CMD_PARAM_GET_INT_MASK_ALL		0x21
#define CG2900_FM_CMD_PARAM_SET_INT_MASK		0x22
#define CG2900_FM_CMD_PARAM_GET_INT_MASK		0x23
#define CG2900_FM_CMD_PARAM_FM_FW_DOWNLOAD		0x30
#define CG2900_FM_CMD_PARAM_NONE			0xFF

/* FM Legacy Command Parameters */
#define CG2900_FM_CMD_LEG_PARAM_WRITE			0x00
#define CG2900_FM_CMD_LEG_PARAM_IRQ			0x01

/* FM Command Status. */
#define CG2900_FM_CMD_STATUS_COMMAND_SUCCEEDED		0x00
#define CG2900_FM_CMD_STATUS_HW_FAILURE			0x03
#define CG2900_FM_CMD_STATUS_INVALID_PARAMS		0x12
#define CG2900_FM_CMD_STATUS_UNINITILIZED		0x15
#define CG2900_FM_CMD_STATUS_UNSPECIFIED_ERROR		0x1F
#define CG2900_FM_CMD_STATUS_COMMAND_DISALLOWED		0x0C
#define CG2900_FM_CMD_STATUS_FW_WRONG_SEQUENCE_NR	0xF1
#define CG2900_FM_CMD_STATUS_FW_UNKNOWN_FILE		0xF2
#define CG2900_FM_CMD_STATUS_FW_FILE_VER_MISMATCH	0xF3

/* FM Interrupts. */
#define CG2900_FM_IRPT_FIQ				0x0000
#define CG2900_FM_IRPT_OPERATION_SUCCEEDED		0x0001
#define CG2900_FM_IRPT_OPERATION_FAILED			0x0002
#define CG2900_FM_IRPT_BUFFER_FULL			0x0008
#define CG2900_FM_IRPT_BUFFER_EMPTY			0x0008
#define CG2900_FM_IRPT_SIGNAL_QUALITY_LOW		0x0010
#define CG2900_FM_IRPT_MUTE_STATUS_CHANGED		0x0010
#define CG2900_FM_IRPT_MONO_STEREO_TRANSITION		0x0020
#define CG2900_FM_IRPT_OVER_MODULATION			0x0020
#define CG2900_FM_IRPT_RDS_SYNC_FOUND			0x0040
#define CG2900_FM_IRPT_INPUT_OVERDRIVE			0x0040
#define CG2900_FM_IRPT_RDS_SYNC_LOST			0x0080
#define CG2900_FM_IRPT_PI_CODE_CHANGED			0x0100
#define CG2900_FM_IRPT_REQUEST_BLOCK_AVALIBLE		0x0200
#define CG2900_FM_IRPT_BUFFER_CLEARED			0x2000
#define CG2900_FM_IRPT_WARM_BOOT_READY			0x4000
#define CG2900_FM_IRPT_COLD_BOOT_READY			0x8000

/* FM Legacy Function Command Parameters */

/* AUP_EXT_SetMode Output enum */
#define CG2900_FM_CMD_AUP_EXT_SET_MODE_DISABLED		0x0000
#define CG2900_FM_CMD_AUP_EXT_SET_MODE_I2S		0x0001
#define CG2900_FM_CMD_AUP_EXT_SET_MODE_PARALLEL		0x0002

/* AUP_BT_SetMode Output enum */
#define CG2900_FM_CMD_AUP_BT_SET_MODE_DISABLED		0x0000
#define CG2900_FM_CMD_AUP_BT_SET_MODE_BYPASSED		0x0001
#define CG2900_FM_CMD_AUP_BT_SET_MODE_PARALLEL		0x0002

/* SetControl Conversion enum */
#define CG2900_FM_CMD_SET_CTRL_CONV_UP			0x0000
#define CG2900_FM_CMD_SET_CTRL_CONV_DOWN		0x0001

/* AIP_SetMode Input enum */
#define CG2900_FM_CMD_AIP_SET_MODE_INPUT_ANA		0x0000
#define CG2900_FM_CMD_AIP_SET_MODE_INPUT_DIG		0x0001

/* AIP_BT_SetMode Input enum */
#define CG2900_FM_CMD_AIP_BT_SET_MODE_INPUT_RESERVED	0x0000
#define CG2900_FM_CMD_AIP_BT_SET_MODE_INPUT_I2S		0x0001
#define CG2900_FM_CMD_AIP_BT_SET_MODE_INPUT_PAR		0x0002
#define CG2900_FM_CMD_AIP_BT_SET_MODE_INPUT_FIFO	0x0003

/* FM Parameter Lengths = FM command length - length field (1 byte) */
#define CG2900_FM_CMD_PARAM_LEN(len) (len - 1)

/*
 * FM Command ID mapped per byte and shifted 3 bits left
 * Also adds number of parameters at first 3 bits of LSB.
 */
static inline __u16 cg2900_get_fm_cmd_id(__u16 opcode)
{
	return opcode >> 3;
}

static inline __u16 cg2900_make_fm_cmd_id(__u16 id, __u8 num_params)
{
	return (id << 3) | num_params;
}

/*
 *	GNSS
 */

struct gnss_hci_hdr {
	__u8	op_code;
	__le16	plen;
} __packed;

#endif /* _CG2900_CHIP_H_ */
