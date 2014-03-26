#ifndef	__PROJECTOR_H__
#define	__PROJECTOR_H__

#define USE_DPP2601

enum {
	IOCTL_PROJECTOR_SET_STATE = 1,
	IOCTL_PROJECTOR_GET_STATE,
	IOCTL_PROJECTOR_SET_ROTATION,
	IOCTL_PROJECTOR_GET_ROTATION,
	IOCTL_PROJECTOR_SET_BRIGHTNESS,
	IOCTL_PROJECTOR_GET_BRIGHTNESS,
	IOCTL_PROJECTOR_SET_FUNCTION,
	IOCTL_PROJECTOR_GET_FUNCTION
};

enum {
	PRJ_WRITE = 0,
	PRJ_READ
};

enum {
	LCD_VIEW = 0,
	INTERNAL_PATTERN
};

enum {
	PRJ_OFF = 0,
	PRJ_ON_RGB_LCD,
	PRJ_ON_INTERNAL,
	RGB_LED_OFF,
	PRJ_MAX_STATE
};

enum {
	CHECKER = 0,
	WHITE,
	BLACK,
	LEDOFF,
	RED,
	GREEN,
	BLUE,
	BEAUTY,
	STRIPE,
	CURTAIN_ON = 10,
	CURTAIN_OFF
};

enum {
	PRJ_ROTATE_0 = 0,
	PRJ_ROTATE_90,
	PRJ_ROTATE_180,
	PRJ_ROTATE_270,
	PRJ_MAX_ROTATE
};

enum {
	ROTATION_LOCK = 0,
	CURTAIN_ENABLE,
	HIGH_AMBIENT_LIGHT
};

struct proj_val {
	int bRotationLock;
	int bCurtainEnable;
	int bHighAmbientLight;
};

struct projector_dpp2601_platform_data {
	unsigned	gpio_scl;
	unsigned	gpio_sda;
	unsigned	gpio_int;
	unsigned	gpio_prj_en;
	unsigned	gpio_mp_on;
	unsigned	gpio_parkz;
	unsigned	gpio_prj_led_en;
	unsigned	gpio_en_lcd;
};

enum {
	MOTOR_CW = 0,
	MOTOR_CCW
};

enum {
	BRIGHT_HIGH = 1,
	BRIGHT_MID,
	BRIGHT_LOW
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void projector_module_early_suspend(struct early_suspend *h);
void projector_module_late_resume(struct early_suspend *h);
#endif

int dpp_flash(unsigned char *DataSetArray, int iNumArray);
void set_proj_status(int enProjectorStatus);
int get_proj_status(void);
void projector_motor_cw(void);
void projector_motor_ccw(void);
void set_led_current(int level);
void ProjectorPowerOnSequence(void);
void ProjectorPowerOffSequence(void);
void rotate_proj_screen(int bLandscape);
int get_proj_rotation(void);
int get_proj_brightness(void);
void project_internal(int pattern);

void pwron_seq_gpio(void);
void pwron_seq_direction(void);
void pwron_seq_source_res(int value);
void pwron_seq_fdr(void);
void proj_testmode_pwron_seq(void);

int __devinit dpp2601_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id);
__devexit int dpp2601_i2c_remove(struct i2c_client *client);
int projector_module_open(struct inode *inode, struct file *file);
int projector_module_release(struct inode *inode, struct file *file);
int projector_module_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg);
int __init projector_module_init(void);
void __exit projector_module_exit(void);
#endif
