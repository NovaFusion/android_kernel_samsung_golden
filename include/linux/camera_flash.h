#ifndef __CAMERA_FLASH_H__
#define __CAMERA_FLASH_H__

#define FLASH_NAME_SIZE	(20)

struct flash_mode_details {
	unsigned long led_type;
	unsigned long max_intensity_uAmp;
	unsigned long min_intensity_uAmp;
	unsigned long max_strobe_duration_uSecs;
	unsigned long feature_bitmap;
	unsigned char nbFaultRegisters;
};

/*feature_bitmap (in struct flash_mode_details) bit values*/
#define INTENSITY_PROGRAMMABLE (0x01)
#define DURATION_PROGRAMMABLE  (0x02)
#define TIMEOUT_PROGRAMMABLE   (0x04)

/*Status word returned by driver has status in lower 16 bits
 *and Error in higher 16 bits. definition of status and error
 *bits are there in flash_bitfields.h
 */
#define SET_FLASH_STATUS(_bitmap, _status) (_bitmap |= (_status & 0xffff))
#define CLR_FLASH_STATUS(_bitmap, _status) (_bitmap &= ~(_status & 0xffff))
#define SET_FLASH_ERROR(_bitmap, _status) (_bitmap |= (_status << 16))
#define CLR_FLASH_ERROR(_bitmap, _status) (_bitmap &= ~(_status << 16))
#define GET_FLASH_STATUS(_bitmap) (_bitmap & 0xffff)
#define GET_FLASH_ERROR(_bitmap) (_bitmap >> 16)

struct flash_mode_params {
	unsigned long duration_uSecs;
	unsigned long intensity_uAmp;
	unsigned long timeout_uSecs;
};

struct flash_ioctl_args_t {
	unsigned long flash_mode;
	unsigned long cam;
	unsigned long status;
	union mode_arg{
		struct flash_mode_details details;
		struct flash_mode_params  params;
		unsigned long strobe_enable;
	} mode_arg;
};

#define FLASH_MAGIC_NUMBER 0x17
#define FLASH_GET_MODES			_IOR(FLASH_MAGIC_NUMBER, 1,\
struct flash_ioctl_args_t *)
#define FLASH_GET_MODE_DETAILS		_IOWR(FLASH_MAGIC_NUMBER, 2,\
struct flash_ioctl_args_t *)
#define FLASH_ENABLE_MODE		_IOW(FLASH_MAGIC_NUMBER, 3,\
struct flash_ioctl_args_t *)
#define FLASH_DISABLE_MODE		_IOW(FLASH_MAGIC_NUMBER, 4,\
struct flash_ioctl_args_t *)
#define FLASH_CONFIGURE_MODE		_IOW(FLASH_MAGIC_NUMBER, 5,\
struct flash_ioctl_args_t *)
#define FLASH_TRIGGER_STROBE		_IOW(FLASH_MAGIC_NUMBER, 6,\
struct flash_ioctl_args_t *)
#define FLASH_GET_STATUS		_IOW(FLASH_MAGIC_NUMBER, 7,\
struct flash_ioctl_args_t *)
#define FLASH_GET_LIFE_COUNTER		_IOW(FLASH_MAGIC_NUMBER, 8,\
struct flash_ioctl_args_t *)
#define FLASH_GET_SELF_TEST_MODES	_IOR(FLASH_MAGIC_NUMBER, 9,\
struct flash_ioctl_args_t *)
#define FLASH_SELF_TEST			_IOW(FLASH_MAGIC_NUMBER, 10,\
struct flash_ioctl_args_t *)
#define FLASH_GET_FAULT_REGISTERS	_IOR(FLASH_MAGIC_NUMBER, 11,\
struct flash_ioctl_args_t *)
#define FLASH_GET_SELF_TEST_RESULT	_IOR(FLASH_MAGIC_NUMBER, 12,\
struct flash_ioctl_args_t *)

#endif
