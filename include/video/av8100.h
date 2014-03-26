/*
 * Copyright (C) ST-Ericsson AB 2010
 *
 * AV8100 driver
 *
 * Author: Per Persson <per.xb.persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */
#ifndef __AV8100__H__
#define __AV8100__H__

#define AV8100_CEC_MESSAGE_SIZE		16
#define AV8100_HDCP_SEND_KEY_SIZE	16
#define AV8100_INFOFRAME_SIZE		28
#define AV8100_FUSE_KEY_SIZE		16
#define AV8100_CHIPVER_1		1
#define AV8100_CHIPVER_2		2

struct av8100_platform_data {
	int		(*init)(void);
	int		(*exit)(void);
	unsigned	gpio_base;
	int		irq;
	int		reset;
	const char	*inputclk_id;
	const char	*regulator_pwr_id;
	bool		alt_powerupseq;
	unsigned char	mclk_freq;
};

enum av8100_command_type {
	AV8100_COMMAND_VIDEO_INPUT_FORMAT  = 0x1,
	AV8100_COMMAND_AUDIO_INPUT_FORMAT,
	AV8100_COMMAND_VIDEO_OUTPUT_FORMAT,
	AV8100_COMMAND_VIDEO_SCALING_FORMAT,
	AV8100_COMMAND_COLORSPACECONVERSION,
	AV8100_COMMAND_CEC_MESSAGE_WRITE,
	AV8100_COMMAND_CEC_MESSAGE_READ_BACK,
	AV8100_COMMAND_DENC,
	AV8100_COMMAND_HDMI,
	AV8100_COMMAND_HDCP_SENDKEY,
	AV8100_COMMAND_HDCP_MANAGEMENT,
	AV8100_COMMAND_INFOFRAMES,
	AV8100_COMMAND_EDID_SECTION_READBACK,
	AV8100_COMMAND_PATTERNGENERATOR,
	AV8100_COMMAND_FUSE_AES_KEY,
	AV8100_COMMAND_AUDIO_INFOFRAME,
};

enum interface_type {
	I2C_INTERFACE = 0x0,
	DSI_INTERFACE = 0x1,
};

enum av8100_dsi_mode {
	AV8100_HDMI_DSI_OFF,
	AV8100_HDMI_DSI_COMMAND_MODE,
	AV8100_HDMI_DSI_VIDEO_MODE
};

enum av8100_pixel_format {
	AV8100_INPUT_PIX_RGB565,
	AV8100_INPUT_PIX_RGB666,
	AV8100_INPUT_PIX_RGB666P,
	AV8100_INPUT_PIX_RGB888,
	AV8100_INPUT_PIX_YCBCR422
};

enum av8100_video_mode {
	AV8100_VIDEO_INTERLACE,
	AV8100_VIDEO_PROGRESSIVE
};

enum av8100_dsi_nb_data_lane {
	AV8100_DATA_LANES_USED_0,
	AV8100_DATA_LANES_USED_1,
	AV8100_DATA_LANES_USED_2,
	AV8100_DATA_LANES_USED_3,
	AV8100_DATA_LANES_USED_4
};

enum av8100_te_config {
	AV8100_TE_OFF,		/* NO TE*/
	AV8100_TE_DSI_LANE,	/* TE generated on DSI lane */
	AV8100_TE_IT_LINE,	/* TE generated on IT line (GPIO) */
	AV8100_TE_DSI_IT,	/* TE generatedon both DSI lane & IT line*/
	AV8100_TE_GPIO_IT	/* TE on GPIO I2S DAT3 & or IT line*/
};

enum av8100_audio_if_format {
	AV8100_AUDIO_I2S_MODE,
	AV8100_AUDIO_I2SDELAYED_MODE, /* I2S Mode by default*/
	AV8100_AUDIO_TDM_MODE         /* 8 Channels by default*/
};

enum av8100_sample_freq {
	AV8100_AUDIO_FREQ_32KHZ,
	AV8100_AUDIO_FREQ_44_1KHZ,
	AV8100_AUDIO_FREQ_48KHZ,
	AV8100_AUDIO_FREQ_64KHZ,
	AV8100_AUDIO_FREQ_88_2KHZ,
	AV8100_AUDIO_FREQ_96KHZ,
	AV8100_AUDIO_FREQ_128KHZ,
	AV8100_AUDIO_FREQ_176_1KHZ,
	AV8100_AUDIO_FREQ_192KHZ
};

enum av8100_audio_word_length {
	AV8100_AUDIO_16BITS,
	AV8100_AUDIO_20BITS,
	AV8100_AUDIO_24BITS
};

enum av8100_audio_format {
	AV8100_AUDIO_LPCM_MODE,
	AV8100_AUDIO_COMPRESS_MODE
};

enum av8100_audio_if_mode {
	AV8100_AUDIO_SLAVE,
	AV8100_AUDIO_MASTER
};

enum av8100_audio_mute {
	AV8100_AUDIO_MUTE_DISABLE,
	AV8100_AUDIO_MUTE_ENABLE
};

enum av8100_output_CEA_VESA {
	AV8100_CUSTOM,
	AV8100_CEA1_640X480P_59_94HZ,
	AV8100_CEA2_3_720X480P_59_94HZ,
	AV8100_CEA4_1280X720P_60HZ,
	AV8100_CEA5_1920X1080I_60HZ,
	AV8100_CEA6_7_NTSC_60HZ,
	AV8100_CEA14_15_480p_60HZ,
	AV8100_CEA16_1920X1080P_60HZ,
	AV8100_CEA17_18_720X576P_50HZ,
	AV8100_CEA19_1280X720P_50HZ,
	AV8100_CEA20_1920X1080I_50HZ,
	AV8100_CEA21_22_576I_PAL_50HZ,
	AV8100_CEA29_30_576P_50HZ,
	AV8100_CEA31_1920x1080P_50Hz,
	AV8100_CEA32_1920X1080P_24HZ,
	AV8100_CEA33_1920X1080P_25HZ,
	AV8100_CEA34_1920X1080P_30HZ,
	AV8100_CEA60_1280X720P_24HZ,
	AV8100_CEA61_1280X720P_25HZ,
	AV8100_CEA62_1280X720P_30HZ,
	AV8100_VESA9_800X600P_60_32HZ,
	AV8100_VESA14_848X480P_60HZ,
	AV8100_VESA16_1024X768P_60HZ,
	AV8100_VESA22_1280X768P_59_99HZ,
	AV8100_VESA23_1280X768P_59_87HZ,
	AV8100_VESA27_1280X800P_59_91HZ,
	AV8100_VESA28_1280X800P_59_81HZ,
	AV8100_VESA39_1360X768P_60_02HZ,
	AV8100_VESA81_1366X768P_59_79HZ,
	AV8100_VIDEO_OUTPUT_CEA_VESA_MAX
};

enum av8100_video_sync_pol {
	AV8100_SYNC_POSITIVE,
	AV8100_SYNC_NEGATIVE
};

enum av8100_hdmi_mode {
	AV8100_HDMI_OFF,
	AV8100_HDMI_ON,
	AV8100_HDMI_AVMUTE
};

enum av8100_hdmi_format {
	AV8100_HDMI,
	AV8100_DVI
};

enum av8100_DVI_format {
	AV8100_DVI_CTRL_CTL0,
	AV8100_DVI_CTRL_CTL1,
	AV8100_DVI_CTRL_CTL2
};

enum av8100_pattern_type {
	AV8100_PATTERN_OFF,
	AV8100_PATTERN_GENERATOR,
	AV8100_PRODUCTION_TESTING
};

enum av8100_pattern_format {
	AV8100_NO_PATTERN,
	AV8100_PATTERN_VGA,
	AV8100_PATTERN_720P,
	AV8100_PATTERN_1080P
};

enum av8100_pattern_audio {
	AV8100_PATTERN_AUDIO_OFF,
	AV8100_PATTERN_AUDIO_ON,
	AV8100_PATTERN_AUDIO_I2S_MEM
};

enum av8100_hdmi_user {
	AV8100_HDMI_USER_AUDIO,
	AV8100_HDMI_USER_VIDEO
};

struct av8100_video_input_format_cmd {
	enum av8100_dsi_mode		dsi_input_mode;
	enum av8100_pixel_format	input_pixel_format;
	unsigned short			total_horizontal_pixel;
	unsigned short			total_horizontal_active_pixel;
	unsigned short			total_vertical_lines;
	unsigned short			total_vertical_active_lines;
	enum av8100_video_mode		video_mode;
	enum av8100_dsi_nb_data_lane	nb_data_lane;
	unsigned char			nb_virtual_ch_command_mode;
	unsigned char			nb_virtual_ch_video_mode;
	unsigned short			TE_line_nb;
	enum av8100_te_config		TE_config;
	unsigned long			master_clock_freq;
	unsigned char			ui_x4;
};

struct av8100_audio_input_format_cmd {
	enum av8100_audio_if_format	audio_input_if_format;
	unsigned char			i2s_input_nb;
	enum av8100_sample_freq		sample_audio_freq;
	enum av8100_audio_word_length	audio_word_lg;
	enum av8100_audio_format	audio_format;
	enum av8100_audio_if_mode	audio_if_mode;
	enum av8100_audio_mute		audio_mute;
};

struct av8100_video_output_format_cmd {
	enum av8100_output_CEA_VESA	video_output_cea_vesa;
	enum av8100_video_sync_pol	vsync_polarity;
	enum av8100_video_sync_pol	hsync_polarity;
	unsigned short		total_horizontal_pixel;
	unsigned short		total_horizontal_active_pixel;
	unsigned short		total_vertical_in_half_lines;
	unsigned short		total_vertical_active_in_half_lines;
	unsigned short		hsync_start_in_pixel;
	unsigned short		hsync_length_in_pixel;
	unsigned short		vsync_start_in_half_line;
	unsigned short		vsync_length_in_half_line;
	unsigned short		hor_video_start_pixel;
	unsigned short		vert_video_start_pixel;
	enum av8100_video_mode	video_type;
	unsigned short		pixel_repeat;
	unsigned long		pixel_clock_freq_Hz;
};

struct av8100_video_scaling_format_cmd {
	unsigned short	h_start_in_pixel;
	unsigned short	h_stop_in_pixel;
	unsigned short	v_start_in_line;
	unsigned short	v_stop_in_line;
	unsigned short	h_start_out_pixel;
	unsigned short	h_stop_out_pixel;
	unsigned short	v_start_out_line;
	unsigned short	v_stop_out_line;
};

enum av8100_color_transform {
	AV8100_COLOR_TRANSFORM_INDENTITY,
	AV8100_COLOR_TRANSFORM_INDENTITY_CLAMP_YUV,
	AV8100_COLOR_TRANSFORM_YUV_TO_RGB,
	AV8100_COLOR_TRANSFORM_YUV_TO_DENC,
	AV8100_COLOR_TRANSFORM_RGB_TO_DENC,
};

struct av8100_cec_message_write_format_cmd {
	unsigned char buffer_length;
	unsigned char buffer[AV8100_CEC_MESSAGE_SIZE];
};

struct av8100_cec_message_read_back_format_cmd {
};

enum av8100_cvbs_video_format {
	AV8100_CVBS_625,
	AV8100_CVBS_525,
};

enum av8100_standard_selection {
	AV8100_PAL_BDGHI,
	AV8100_PAL_N,
	AV8100_NTSC_M,
	AV8100_PAL_M
};

struct av8100_denc_format_cmd {
	enum av8100_cvbs_video_format cvbs_video_format;
	enum av8100_standard_selection standard_selection;
	unsigned char enable;
	unsigned char macrovision_enable;
	unsigned char internal_generator;
};

struct av8100_hdmi_cmd {
	enum av8100_hdmi_mode	hdmi_mode;
	enum av8100_hdmi_format	hdmi_format;
	enum av8100_DVI_format	dvi_format; /* used only if HDMI_format = DVI*/
};

struct av8100_hdcp_send_key_format_cmd {
	unsigned char key_number;
	unsigned char data_len;
	unsigned char data[AV8100_HDCP_SEND_KEY_SIZE];
};

enum av8100_hdcp_auth_req_type {
	AV8100_HDCP_AUTH_REQ_OFF = 0,
	AV8100_HDCP_AUTH_REQ_ON = 1,
	AV8100_HDCP_REV_LIST_REQ = 2,
	AV8100_HDCP_AUTH_CONT = 3,
};

enum av8100_hdcp_encr_use {
	AV8100_HDCP_ENCR_USE_OESS = 0,
	AV8100_HDCP_ENCR_USE_EESS = 1,
};

struct av8100_hdcp_management_format_cmd {
	unsigned char req_type;
	unsigned char encr_use;
};

struct av8100_infoframes_format_cmd {
	unsigned char type;
	unsigned char version;
	unsigned char length;
	unsigned char crc;
	unsigned char data[AV8100_INFOFRAME_SIZE];
};

struct av8100_edid_section_readback_format_cmd {
	unsigned char address;
	unsigned char block_number;
};

struct av8100_pattern_generator_format_cmd {
	enum av8100_pattern_type	pattern_type;
	enum av8100_pattern_format	pattern_video_format;
	enum av8100_pattern_audio	pattern_audio_mode;
};

enum av8100_fuse_operation {
	AV8100_FUSE_READ = 0,
	AV8100_FUSE_WRITE = 1,
};

struct av8100_fuse_aes_key_format_cmd {
	unsigned char fuse_operation;
	unsigned char key[AV8100_FUSE_KEY_SIZE];
};

union av8100_configuration {
	struct av8100_video_input_format_cmd	video_input_format;
	struct av8100_audio_input_format_cmd	audio_input_format;
	struct av8100_video_output_format_cmd	video_output_format;
	struct av8100_video_scaling_format_cmd	video_scaling_format;
	enum   av8100_color_transform           color_transform;
	struct av8100_cec_message_write_format_cmd
		cec_message_write_format;
	struct av8100_cec_message_read_back_format_cmd
		cec_message_read_back_format;
	struct av8100_denc_format_cmd		denc_format;
	struct av8100_hdmi_cmd			hdmi_format;
	struct av8100_hdcp_send_key_format_cmd	hdcp_send_key_format;
	struct av8100_hdcp_management_format_cmd hdcp_management_format;
	struct av8100_infoframes_format_cmd	infoframes_format;
	struct av8100_edid_section_readback_format_cmd
		edid_section_readback_format;
	struct av8100_pattern_generator_format_cmd pattern_generator_format;
	struct av8100_fuse_aes_key_format_cmd	fuse_aes_key_format;
};

enum av8100_operating_mode {
	AV8100_OPMODE_UNDEFINED = 0,
	AV8100_OPMODE_SHUTDOWN,
	AV8100_OPMODE_STANDBY,
	AV8100_OPMODE_SCAN,
	AV8100_OPMODE_INIT,
	AV8100_OPMODE_IDLE,
	AV8100_OPMODE_VIDEO,
};

enum av8100_plugin_status {
	AV8100_PLUGIN_NONE = 0x0,
	AV8100_HDMI_PLUGIN = 0x1,
	AV8100_CVBS_PLUGIN = 0x2,
};

enum av8100_hdmi_event {
	AV8100_HDMI_EVENT_NONE =		0x0,
	AV8100_HDMI_EVENT_HDMI_PLUGIN =		0x1,
	AV8100_HDMI_EVENT_HDMI_PLUGOUT =	0x2,
	AV8100_HDMI_EVENT_CEC =			0x4,
	AV8100_HDMI_EVENT_HDCP =		0x8,
	AV8100_HDMI_EVENT_CECTXERR =		0x10,
	AV8100_HDMI_EVENT_CECTX =		0x20,	/* Transm no error */
	AV8100_HDMI_EVENT_CCIERR =		0x40,	/* 5V short circuit */
};

struct av8100_status {
	enum av8100_operating_mode	av8100_state;
	enum av8100_plugin_status	av8100_plugin_status;
	int				hdmi_on;
};


int av8100_init(void);
void av8100_exit(void);
int av8100_hdmi_get(enum av8100_hdmi_user user);
int av8100_hdmi_put(enum av8100_hdmi_user user);
int av8100_hdmi_video_off(void);
int av8100_hdmi_video_on(void);
void av8100_conf_lock(void);
void av8100_conf_unlock(void);
int av8100_powerwakeup(bool from_scan_mode);
int av8100_powerscan(bool to_scan_mode);
int av8100_powerup(void);
int av8100_powerdown(void);
int av8100_disable_interrupt(void);
int av8100_enable_interrupt(void);
int av8100_download_firmware(enum interface_type if_type);
int av8100_reg_stby_w(
		unsigned char cpd,
		unsigned char stby,
		unsigned char mclkrng);
int av8100_reg_hdmi_5_volt_time_w(
		unsigned char denc_off_time,
		unsigned char hdmi_off_time,
		unsigned char on_time);
int av8100_reg_stby_int_mask_w(
		unsigned char hpdm,
		unsigned char cpdm,
		unsigned char ccm,
		unsigned char stbygpiocfg,
		unsigned char ipol);
int av8100_reg_stby_pend_int_w(
		unsigned char hpdi,
		unsigned char cpdi,
		unsigned char oni,
		unsigned char cci,
		unsigned char ccrst,
		unsigned char bpdig);
int av8100_reg_gen_int_mask_w(
		unsigned char eocm,
		unsigned char vsim,
		unsigned char vsom,
		unsigned char cecm,
		unsigned char hdcpm,
		unsigned char uovbm,
		unsigned char tem);
int av8100_reg_gen_int_w(
		unsigned char eoci,
		unsigned char vsii,
		unsigned char vsoi,
		unsigned char ceci,
		unsigned char hdcpi,
		unsigned char uovbi);
int av8100_reg_gpio_conf_w(
		unsigned char dat3dir,
		unsigned char dat3val,
		unsigned char dat2dir,
		unsigned char dat2val,
		unsigned char dat1dir,
		unsigned char dat1val,
		unsigned char ucdbg);
int av8100_reg_gen_ctrl_w(
		unsigned char fdl,
		unsigned char hld,
		unsigned char wa,
		unsigned char ra);
int av8100_reg_fw_dl_entry_w(
	unsigned char mbyte_code_entry);
int av8100_reg_w(
		unsigned char offset,
		unsigned char value);
int av8100_reg_stby_r(
		unsigned char *cpd,
		unsigned char *stby,
		unsigned char *hpds,
		unsigned char *cpds,
		unsigned char *mclkrng);
int av8100_reg_hdmi_5_volt_time_r(
		unsigned char *denc_off_time,
		unsigned char *hdmi_off_time,
		unsigned char *on_time);
int av8100_reg_stby_int_mask_r(
		unsigned char *hpdm,
		unsigned char *cpdm,
		unsigned char *stbygpiocfg,
		unsigned char *ipol);
int av8100_reg_stby_pend_int_r(
		unsigned char *hpdi,
		unsigned char *cpdi,
		unsigned char *oni,
		unsigned char *cci,
		unsigned char *sid);
int av8100_reg_gen_int_mask_r(
		unsigned char *eocm,
		unsigned char *vsim,
		unsigned char *vsom,
		unsigned char *cecm,
		unsigned char *hdcpm,
		unsigned char *uovbm,
		unsigned char *tem);
int av8100_reg_gen_int_r(
		unsigned char *eoci,
		unsigned char *vsii,
		unsigned char *vsoi,
		unsigned char *ceci,
		unsigned char *hdcpi,
		unsigned char *uovbi,
		unsigned char *tei);
int av8100_reg_gen_status_r(
		unsigned char *cectxerr,
		unsigned char *cecrec,
		unsigned char *cectrx,
		unsigned char *uc,
		unsigned char *onuvb,
		unsigned char *hdcps);
int av8100_reg_gpio_conf_r(
		unsigned char *dat3dir,
		unsigned char *dat3val,
		unsigned char *dat2dir,
		unsigned char *dat2val,
		unsigned char *dat1dir,
		unsigned char *dat1val,
		unsigned char *ucdbg);
int av8100_reg_gen_ctrl_r(
		unsigned char *fdl,
		unsigned char *hld,
		unsigned char *wa,
		unsigned char *ra);
int av8100_reg_fw_dl_entry_r(
	unsigned char *mbyte_code_entry);
int av8100_reg_r(
		unsigned char offset,
		unsigned char *value);
int av8100_conf_get(enum av8100_command_type command_type,
	union av8100_configuration *config);
int av8100_conf_prep(enum av8100_command_type command_type,
	union av8100_configuration *config);
int av8100_conf_w(enum av8100_command_type command_type,
		unsigned char *return_buffer_length,
		unsigned char *return_buffer, enum interface_type if_type);
int av8100_conf_w_raw(enum av8100_command_type command_type,
	unsigned char buffer_length,
	unsigned char *buffer,
	unsigned char *return_buffer_length,
	unsigned char *return_buffer);
struct av8100_status av8100_status_get(void);
enum av8100_output_CEA_VESA av8100_video_output_format_get(int xres,
	int yres,
	int htot,
	int vtot,
	int pixelclk,
	bool interlaced);
void av8100_hdmi_event_cb_set(void (*event_callback)(enum av8100_hdmi_event));
u8 av8100_ver_get(void);
bool av8100_encryption_ongoing(void);
void av8100_video_mode_changed(void);

#endif /* __AV8100__H__ */
