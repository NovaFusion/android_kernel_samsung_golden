/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pankaj Chauhan <pankaj.chauhan@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */
#include <linux/delay.h>
#include <linux/init.h>		/* Initiliasation support */
#include <linux/module.h>	/* Module support */
#include <linux/kernel.h>	/* Kernel support */
#include <linux/version.h>	/* Kernel version */
#include <linux/fs.h>		/* File operations (fops) defines */
#include <linux/errno.h>	/* Defines standard err codes */
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/mmio.h>
#include <linux/ratelimit.h>
#include "st_mmio.h"

#define ISP_REGION_IO				(0xE0000000)
#define SIA_ISP_REG_ADDR			(0x521E4)
#define SIA_BASE_ADDR				(0x54000)
#define SIA_ISP_MEM				(0x56000)
#define SIA_TIMER_ITC				(0x5BC00)
#define SIA_ISP_MCU_SYS_SIZE			(0x100000)
#define SIA_ISP_MEM_PAGE_REG			(0x54070)
#define SIA_ISP_MCU_SYS_ADDR0_OFFSET	(SIA_BASE_ADDR + 0x40)
#define SIA_ISP_MCU_SYS_SIZE0_OFFSET	(SIA_BASE_ADDR + 0x42)
#define SIA_ISP_MCU_SYS_ADDR1_OFFSET	(SIA_ISP_MCU_SYS_ADDR0_OFFSET + 0x04)
#define SIA_ISP_MCU_SYS_SIZE1_OFFSET	(SIA_ISP_MCU_SYS_SIZE0_OFFSET + 0x04)
#define SIA_ISP_MCU_IO_ADDR0_HI		(SIA_BASE_ADDR + 0x60)

/* HTimer enable in CR register */
#define CR_REG0_HTIMEN				(1 << 26)
#define PICTOR_IN_XP70_L2_MEM_BASE_ADDR		(0x40000)
#define PICTOR_IN_XP70_TCDM_MEM_BASE_ADDR	(0x60000)
#define L2_PSRAM_MEM_SIZE			(0x10000)

#define FW_TO_HOST_ADDR_MASK		(0x00001FFF)
#define FW_TO_HOST_ADDR_SHIFT		(0xD)
#define FW_TO_HOST_CLR_MASK		(0x3F)
#define PHY_TO_ISP_MCU_IO_ADDR0_HI(x)	(((x) >> 24) << 8)
#define XP70_ADDR_MASK			(0x00FFFFFF)

#define CLOCK_ENABLE_DELAY		(0x2)

#define MAX_PRCMU_QOS_APP		(0x64)

#define ISP_WRITE_DATA_SIZE		(0x4)

#define clrbits32(_addr, _clear) \
	writel(readl(_addr) & ~(u32)(_clear), _addr)
#define setbits32(_addr, _set) \
	writel(readl(_addr) | (u32)(_set), _addr)

#define XP70_BLOCK_SIZE		124
#define XP70_NB_BLOCK		50
/*
 * For 30 fps video, there is 33 msec delay between every two frames
 * MMIO driver reads traces from trace buffer every XP70_TIMEOUT_MSEC.
 * If traces are not read in time from trace buffer, camera firmware
 * will start overwiting the traces as size of trace buffer is limited.
 */
#define XP70_TIMEOUT_MSEC	30
#define XP70_DEFAULT_MSG_ID	(0xCDCDCDCD)
#define XP70_MAX_BLOCK_ID	(0xFFFFFFFF)

#define upper_16_bits(n) ((u16)((u32)(n) >> 16))

/* Check VT_CAM_ID */
/*  Codina VT_CAM_ID can be checked by CAM_VDDIO_1V8 (LDO2)*/
#define VT_CAM_ID_CHECK_POWER 2

#if defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_SEC_GOLDEN)
#define VT_CAM_ID 226    /* Codina VT_CAM_ID GPIO number*/
#elif defined(CONFIG_MACH_JANICE)
#define VT_CAM_ID 66    /* JANICE VT_CAM_ID GPIO number*/
#else
#define VT_CAM_ID 66    /*  VT_CAM_ID GPIO number*/
#endif

#define ON 1  /* GPIO Power On*/
#define OFF 0 /* GPIO Power Off*/
int vt_id;   /* Global variable  (VT_CAM_ID) value*/
int assistive_mode;

/* Function Pointer Declaration */
int  (*subPMIC_module_init)(void);
void (*subPMIC_module_exit)(void);
int  (*subPMIC_PowerOn)(int);
int  (*subPMIC_PowerOff)(int);
int  (*subPMIC_PinOnOff)(int, int);

struct trace_block {
	u32 msg_id;
	char data[XP70_BLOCK_SIZE];
};

struct mmio_trace {
	u32 nb_block;
	u32 block_size;
	u32 block_id;
	u32 overwrite_count;
	struct trace_block block[XP70_NB_BLOCK];
};

struct trace_buffer_status {
	u32 prev_overwrite_count;
	u32 prev_block_id;
};

struct mmio_info {
	struct mmio_platform_data *pdata;	/* Config from board */
	struct device *dev;	/* My device */
	/* Runtime variables */
	struct miscdevice misc_dev;
	void __iomem *siabase;
	void __iomem *crbase;
	/* States */
	int xshutdown_enabled;
	int xshutdown_is_active_high;
	/* tracing */
	struct trace_buffer_status trace_status;
	struct mmio_trace *trace_buffer;
	struct delayed_work trace_work;
	int trace_allowed;
	struct mutex lock;
};

/* Samsung+ */
#define PRIMARY_CAMERA_STBY  142
#define PRIMARY_CAMERA_RESET 149

#define SECONDARY_CAMERA_STBY 64
#define SECONDARY_CAMERA_RESET 65

#define FLASH_EN  140
#define FLASH_MODE  141

void sec_camera_gpio_init(void)
{
	printk(KERN_DEBUG "camera_gpio_init \n");

	gpio_request(PRIMARY_CAMERA_STBY, "PRI_CAM_STB"); /* GPIO PIN Request*/
	gpio_request(PRIMARY_CAMERA_RESET, "PRI_CAM_RST"); /* GPIO PIN Request*/
	gpio_request(SECONDARY_CAMERA_STBY, "SEC_CAM_STB"); /* GPIO PIN Request*/
	gpio_request(SECONDARY_CAMERA_RESET, "SEC_CAM_RST"); /* GPIO PIN Request*/
#if !defined(CONFIG_MACH_SEC_KYLE)
	gpio_request(FLASH_EN, "FLASH_EN"); /* GPIO PIN Request*/
	gpio_request(FLASH_MODE, "FLASH_MODE"); /* GPIO PIN Request*/
#endif

	gpio_set_value(PRIMARY_CAMERA_RESET, 0);
	gpio_set_value(PRIMARY_CAMERA_STBY, 0);
	gpio_set_value(SECONDARY_CAMERA_RESET, 0);
	gpio_set_value(SECONDARY_CAMERA_STBY, 0);
#if !defined(CONFIG_MACH_SEC_KYLE)
	gpio_set_value(FLASH_EN, 0); /* GPIO PIN Request*/
	gpio_set_value(FLASH_MODE, 0); /* GPIO PIN Request*/
#endif

}
/* Samsung- */

static int mmio_cam_control_clocks(struct mmio_info *info, enum mmio_bool_t power_on);
static int mmio_cam_flash_set_mode(struct mmio_info *info, int lux_val);

/*
 * The one and only private data holder. Default inited to NULL.
 * Declare it here so no code above can use it directly.
 */
static struct mmio_info *info;

static int mmio_cam_gpio_pin_control(int pin, int on)
{
	printk(KERN_DEBUG "mmio_cam_gpio_pin_control pin : %d , state %d\n",
		       pin, on);

	gpio_set_value(pin, on);
	return 0;		/* Always success */
}

static int mmio_cam_power_pin_control(int pin, int on)
{
	printk(KERN_DEBUG "mmio_cam_power_pin_control pin : %d , state %d\n",
		       pin, on);
	subPMIC_PinOnOff(pin, on);
	return 0;		/* Always success */
}

/*
 * static 1K buffer to do I/O write instead of kmalloc,
 * no locking, caller can not have parallel use of
 * MMIO_CAM_LOAD_XP70_FW and MMIO_CAM_ISP_WRITE ioctl's
 */
static u16 copybuff[512];

/*
 * This function converts a given logical memory region size
 * to appropriate ISP_MCU_SYS_SIZEx register value.
 */
static int get_mcu_sys_size(u32 size, u32 *val)
{
	int ret = 0;

	if (size > 0 && size <= SZ_4K)
		*val = 4;
	else if (size > SZ_4K && size <= SZ_8K)
		*val = 5;
	else if (size > SZ_8K && size <= SZ_16K)
		*val = 6;
	else if (size > SZ_16K && size <= SZ_32K)
		*val = 7;
	else if (size > SZ_32K && size <= SZ_64K)
		*val = 0;
	else if (size > SZ_64K && size <= SZ_1M)
		*val = 1;
	else if (size > SZ_1M  && size <= SZ_16M)
		*val = 2;
	else if (size > SZ_16M && size <= SZ_256M)
		*val = 3;
	else
		ret = -EINVAL;

	return ret;
}

void SM5103_MainCamera_On(struct mmio_info *info, int on)
{
            /* CAM_AVDD_2V8 On */
			mmio_cam_power_pin_control(0x1, 1);
			udelay(1);

			/* VT_DVDD_1V8 On */
			mmio_cam_power_pin_control(0x2, 1);
			udelay(1);

#if 0
            		/* VT_AVDD_2V8 On */
			/* Diff between SM5103 and NCP6914 */
            		mmio_cam_power_pin_control(0x4, 1);
			udelay(1);
#endif

			/* CAM_VDDIO_1V8 On */
			mmio_cam_power_pin_control(0x3, 1);
			udelay(1);

			/* 5M_AF_2V8 On */
			mmio_cam_power_pin_control(0x5, 1);
			udelay(1);

			gpio_set_value(SECONDARY_CAMERA_STBY, 1);
			udelay(50);

			mmio_cam_control_clocks(info, true);
			msleep(10);

			gpio_set_value(SECONDARY_CAMERA_RESET, 1);
			msleep(4);

			gpio_set_value(SECONDARY_CAMERA_STBY, 0);
			udelay(10);

            		/* CAM_CORE_1V2, Buck On */
			mmio_cam_power_pin_control(0x0, 1);
			msleep(10);

			gpio_set_value(PRIMARY_CAMERA_STBY, 1);
			msleep(10);

	    	        gpio_set_value(PRIMARY_CAMERA_RESET, 1);
		        msleep(10);

}


void SM5103_MainCamera_Off(struct mmio_info *info, int off)
{
			gpio_set_value(PRIMARY_CAMERA_RESET, 0);
			msleep(4);

			mmio_cam_control_clocks(info, false);
			msleep(10);

			gpio_set_value(PRIMARY_CAMERA_STBY, 0);
			udelay(20);

			gpio_set_value(SECONDARY_CAMERA_RESET, 0);
			udelay(10);

            /* 5M_AF_2V8 Off */
			mmio_cam_power_pin_control(0x5, 0);
			udelay(2);

			/* CAM_CORE_1V2, Buck Off */
			mmio_cam_power_pin_control(0x0, 0);
			udelay(1);

			/* CAM_VDDIO_1V8 Off */
			mmio_cam_power_pin_control(0x3, 0);
			udelay(1);

            /* VT_AVDD_2V8 Off */
			/* Diff between SM5103 and NCP6914 */
            		mmio_cam_power_pin_control(0x4, 0);
			udelay(1);

			/* VT_DVDD_1V8 Off */
			mmio_cam_power_pin_control(0x2, 0);
			udelay(1);

            /* CAM_AVDD_2V8 Off */
			mmio_cam_power_pin_control(0x1, 0);
}

void SM5103_SubCamera_On(struct mmio_info *info, int on)
{

#if 0
    /* CAM_AVDD_2V8 On */
    mmio_cam_power_pin_control(0x1, 1);
    udelay(1);
#endif

    /* VT_AVDD_2V8 On */
    /* Diff between SM5103 and NCP6914 */
    mmio_cam_power_pin_control(0x4, 1);
    udelay(1);

    /* VT_DVDD_1V8 On */
    mmio_cam_power_pin_control(0x2, 1);
    udelay(1);

    /* CAM_VDDIO_1V8 On */
    mmio_cam_power_pin_control(0x3, 1);
    udelay(1);

    /* CAM_CORE_1V2, Buck On */
    mmio_cam_power_pin_control(0x0, 1);
    msleep(15);

    /* CAM_CORE_1V2, Buck Off */
    mmio_cam_power_pin_control(0x0, 0);
    msleep(2);

    mmio_cam_control_clocks(info, true);
    msleep(10);

    gpio_set_value(SECONDARY_CAMERA_STBY, 1);
    msleep(10);

    gpio_set_value(SECONDARY_CAMERA_RESET, 1);
    msleep(10);

}

void SM5103_SubCamera_Off(struct mmio_info *info, int off)
{
			gpio_set_value(SECONDARY_CAMERA_RESET, 0);
    udelay(15);

			gpio_set_value(SECONDARY_CAMERA_STBY, 0);
			msleep(4);

			mmio_cam_control_clocks(info, false);
			msleep(10);

            /* CAM_VDDIO_1V8 Off */
			mmio_cam_power_pin_control(0x3, 0);
			udelay(1);

            /* VT_AVDD_2V8 On */
			/* Diff between SM5103 and NCP6914 */
            		mmio_cam_power_pin_control(0x4, 0);
			udelay(1);

			/* VT_DVDD_1V8 Off */
			mmio_cam_power_pin_control(0x2, 0);
			udelay(1);

            /* CAM_AVDD_2V8 Off */
			mmio_cam_power_pin_control(0x1, 0);
			udelay(1);
		}

void NCP6914_MainCamera_On(struct mmio_info *info, int on)
{
			/* Sensor AVDD 2.8V On*/
			mmio_cam_power_pin_control(0x4, 1);
			udelay(1);

			/* VT VGA Core 1.5V On*/
			mmio_cam_power_pin_control(0x1, 1);
			udelay(1)	;

			/* Sensor I/O  1.8V On*/
			mmio_cam_power_pin_control(0x2, 1);
			udelay(10);

			/*	AF On*/
			mmio_cam_power_pin_control(0x3, 1);
			udelay(20);

			gpio_set_value(SECONDARY_CAMERA_STBY, 1);
			udelay(50);

			mmio_cam_control_clocks(info, true);
			msleep(10);

			gpio_set_value(SECONDARY_CAMERA_RESET, 1);
			msleep(4);

			gpio_set_value(SECONDARY_CAMERA_STBY, 0);
			udelay(10);

			/*	5M Core 1.2V, Buck On*/
			mmio_cam_power_pin_control(0x0, 1);
			msleep(10);

			gpio_set_value(PRIMARY_CAMERA_STBY, 1);
			msleep(10);

			gpio_set_value(PRIMARY_CAMERA_RESET, 1);
			msleep(10);
		}

void NCP6914_MainCamera_Off(struct mmio_info *info, int off)
{
			gpio_set_value(PRIMARY_CAMERA_RESET, 0);
			msleep(4);

			mmio_cam_control_clocks(info, false);
			msleep(10);

			gpio_set_value(PRIMARY_CAMERA_STBY, 0);
			udelay(20);

			gpio_set_value(SECONDARY_CAMERA_RESET, 0);
			udelay(10);

			/*	AF Off*/
			mmio_cam_power_pin_control(0x3, 0);
			udelay(2);

			/*	5M Core 1.2V, Buck Off*/
			mmio_cam_power_pin_control(0x0, 0);
			udelay(1);

			/* Sensor I/O  1.8V Off*/
			mmio_cam_power_pin_control(0x2, 0);
			udelay(1);

			/* VT VGA Core 1.5V Off*/
			mmio_cam_power_pin_control(0x1, 0);
			udelay(1);

			/* Sensor AVDD 2.8V Off*/
			mmio_cam_power_pin_control(0x4, 0);
}

void NCP6914_SubCamera_On(struct mmio_info *info, int on)
{
    /* Sensor AVDD 2.8V On */
    mmio_cam_power_pin_control(0x4, 1);
    udelay(1);

    /* VT VGA Core 1.5V On */
    mmio_cam_power_pin_control(0x1, 1);
    udelay(1);

    /* Sensor I/O  1.8V On */
    mmio_cam_power_pin_control(0x2, 1);
    udelay(1);

    /* 5M Core 1.2V, Buck On */
    mmio_cam_power_pin_control(0x0, 1);
    msleep(15);

    /* 5M Core 1.2V, Buck Off */
    mmio_cam_power_pin_control(0x0, 0);
    msleep(2);

    mmio_cam_control_clocks(info, true);
    msleep(10);

    gpio_set_value(SECONDARY_CAMERA_STBY, 1);
    msleep(10);

    gpio_set_value(SECONDARY_CAMERA_RESET, 1);
    msleep(10);
}

void NCP6914_SubCamera_Off(struct mmio_info *info, int off)
{
			gpio_set_value(SECONDARY_CAMERA_RESET, 0);
			udelay(20);

			gpio_set_value(SECONDARY_CAMERA_STBY, 0);
			msleep(4);

			mmio_cam_control_clocks(info, false);
			msleep(10);

			/* Sensor I/O  1.8V Off*/
			mmio_cam_power_pin_control(0x2, 0);
			udelay(1);

			/* VT VGA Core 1.5V Off*/
			mmio_cam_power_pin_control(0x1, 0);
			udelay(1);

			/* Sensor AVDD 2.8V Off*/
			mmio_cam_power_pin_control(0x4, 0);
		}

/* Camera Main/Sub Power Control */
static int mmio_cam_pwr_sensor(struct mmio_info *info, int on)
{
    int err = 0;
    BUG_ON(info == NULL);
    /* working at MMIO_Camera.cpp */
    dev_dbg(info->dev, "mmio_cam_pwr_sensor %d\n", on);

#if defined(CONFIG_MACH_JANICE) || defined(CONFIG_MACH_GAVINI)

    if (on)
    {
        gpio_set_value(PRIMARY_CAMERA_RESET, 0);
        gpio_set_value(PRIMARY_CAMERA_STBY, 0);
        gpio_set_value(SECONDARY_CAMERA_RESET, 0);
        gpio_set_value(SECONDARY_CAMERA_STBY, 0);

        err = info->pdata->power_enable(info->pdata);

        mmio_cam_control_clocks(info, false);

        mdelay(CLOCK_ENABLE_DELAY);

        subPMIC_PowerOn(0x0);
	}
    else /* Power Off for Janice and Gavini */
    {
        subPMIC_PowerOff(0x0);

		mmio_cam_control_clocks(info, false);

		info->pdata->power_disable(info->pdata);

		mdelay(CLOCK_ENABLE_DELAY);

		gpio_set_value(PRIMARY_CAMERA_RESET, 0);
		gpio_set_value(PRIMARY_CAMERA_STBY, 0);
		gpio_set_value(SECONDARY_CAMERA_RESET, 0);
		gpio_set_value(SECONDARY_CAMERA_STBY, 0);
	}

#elif defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_CODINA)

    if(on) /* Power On For Gorden */
    {
        gpio_set_value(PRIMARY_CAMERA_RESET, 0);
        gpio_set_value(PRIMARY_CAMERA_STBY, 0);
        gpio_set_value(SECONDARY_CAMERA_RESET, 0);
        gpio_set_value(SECONDARY_CAMERA_STBY, 0);

        err = info->pdata->power_enable(info->pdata);

		mmio_cam_control_clocks(info, false);

        mdelay(CLOCK_ENABLE_DELAY);

        subPMIC_PowerOn(0x0);

        if(info->pdata->camera_slot == PRIMARY_CAMERA)
        {
#if defined(CONFIG_MACH_SEC_GOLDEN)
            if(system_rev < GOLDEN_R0_4)
                SM5103_MainCamera_On(info, on); /* Main Camera Power On */
            else
                NCP6914_MainCamera_On(info, on);
#elif defined(CONFIG_MACH_CODINA)
	    switch (system_rev){
		case CODINA_TMO_R0_0:
		case CODINA_TMO_R0_0_A:
		case CODINA_TMO_R0_1:
		case CODINA_TMO_R0_4:
		case CODINA_TMO_R0_5:
		case CODINA_TMO_R0_6:
			NCP6914_MainCamera_On(info, on);
			break;

		case CODINA_TMO_R0_2:
			SM5103_MainCamera_On(info, on);
			break;

		default:
			break;
	    }
#endif
        }
        else
        {
#if defined(CONFIG_MACH_SEC_GOLDEN)
            if(system_rev < GOLDEN_R0_4)
                SM5103_SubCamera_On(info, on); /* Sub Camera Power On */
            else
                NCP6914_SubCamera_On(info, on);
#elif defined(CONFIG_MACH_CODINA)
	    switch (system_rev){
		case CODINA_TMO_R0_0:
		case CODINA_TMO_R0_0_A:
		case CODINA_TMO_R0_1:
		case CODINA_TMO_R0_4:
		case CODINA_TMO_R0_5:
		case CODINA_TMO_R0_6:
			NCP6914_SubCamera_On(info, on);
			break;

		case CODINA_TMO_R0_2:
			SM5103_SubCamera_On(info, on);
			break;

		default:
			break;
	    }
#endif
        }

        /*
               * When switching from secondary YUV camera
               * to primary Raw Bayer Camera, a hang is observed without the
               * below delay. I2C access failure are observed while
               * communicating with primary camera sensor indicating camera
               * sensor was not powered up correctly.
               */

        mdelay(CLOCK_ENABLE_DELAY);
    }
    else /* Power Off Sequence for Golden */
    {
        if(info->pdata->camera_slot == PRIMARY_CAMERA)
        {
#if defined(CONFIG_MACH_SEC_GOLDEN)
            if(system_rev < GOLDEN_R0_4)
                SM5103_MainCamera_Off(info, on); /* Main Camera Off */
            else
                NCP6914_MainCamera_Off(info, on);
#elif defined(CONFIG_MACH_CODINA)
	    switch (system_rev){
		case CODINA_TMO_R0_0:
		case CODINA_TMO_R0_0_A:
		case CODINA_TMO_R0_1:
		case CODINA_TMO_R0_4:
		case CODINA_TMO_R0_5:
		case CODINA_TMO_R0_6:
			NCP6914_MainCamera_Off(info, on);
			break;

		case CODINA_TMO_R0_2:
			SM5103_MainCamera_Off(info, on);
			break;

		default:
			break;
	    }
#endif
		}
        else
        {
#if defined(CONFIG_MACH_SEC_GOLDEN)
            if(system_rev < GOLDEN_R0_4)
                SM5103_SubCamera_Off(info, on); /* Sub Camera Off for Golden*/
            else
                NCP6914_SubCamera_Off(info, on);
#elif defined(CONFIG_MACH_CODINA)
	    switch (system_rev){
		case CODINA_TMO_R0_0:
		case CODINA_TMO_R0_0_A:
		case CODINA_TMO_R0_1:
		case CODINA_TMO_R0_4:
		case CODINA_TMO_R0_5:
		case CODINA_TMO_R0_6:
			NCP6914_SubCamera_Off(info, on);
			break;

		case CODINA_TMO_R0_2:
			SM5103_SubCamera_Off(info, on);
			break;

		default:
			break;
	    }
#endif
		}

		subPMIC_PowerOff(0x0);

		mmio_cam_control_clocks(info, false);
		info->pdata->power_disable(info->pdata);

		mdelay(CLOCK_ENABLE_DELAY);

		gpio_set_value(PRIMARY_CAMERA_RESET, 0);
		gpio_set_value(PRIMARY_CAMERA_STBY, 0);
		gpio_set_value(SECONDARY_CAMERA_RESET, 0);
		gpio_set_value(SECONDARY_CAMERA_STBY, 0);
	}

#elif defined(CONFIG_MACH_SEC_KYLE)

	if(on)
    {
		gpio_set_value(PRIMARY_CAMERA_RESET, 0);
		gpio_set_value(PRIMARY_CAMERA_STBY, 0);
		gpio_set_value(SECONDARY_CAMERA_RESET, 0);
		gpio_set_value(SECONDARY_CAMERA_STBY, 0);

		err = info->pdata->power_enable(info->pdata);

		mmio_cam_control_clocks(info, false);

		mdelay(CLOCK_ENABLE_DELAY);

		subPMIC_PowerOn(0x0);


		if(KYLE_ATT_R0_1 == system_rev)
		{

			if(info->pdata->camera_slot == PRIMARY_CAMERA)
	        {
	            SM5103_MainCamera_On(info, on);
			}
	        else /* Power On Sub Camera */
	        {
	            SM5103_SubCamera_On(info, on);
			}
		} else {

			if(info->pdata->camera_slot == PRIMARY_CAMERA)
	        {
	            NCP6914_MainCamera_On(info, on);
			}
	        else /* Power On Sub Camera */
	        {
	            NCP6914_SubCamera_On(info, on);
			}
		}


		/*
		 * When switching from secondary YUV camera
		 * to primary Raw Bayer Camera, a hang is observed without the
		 * below delay. I2C access failure are observed while
		 * communicating with primary camera sensor indicating camera
		 * sensor was not powered up correctly.
		 */

		mdelay(CLOCK_ENABLE_DELAY);
	}
	else /* Power Off Sequence for Kyle */
    {

		if(KYLE_ATT_R0_1 == system_rev)
		{
			if (info->pdata->camera_slot == PRIMARY_CAMERA) /* Main Camera Power Off */
	        {
	           SM5103_MainCamera_Off(info, on);
			}
	        else /* Sub Camera Power Off */
	        {
	            SM5103_SubCamera_Off(info, on);
			}
		} else {
			if (info->pdata->camera_slot == PRIMARY_CAMERA) /* Main Camera Power Off */
	        {
	           NCP6914_MainCamera_Off(info, on);
			}
	        else /* Sub Camera Power Off */
	        {
	            NCP6914_SubCamera_Off(info, on);
			}

		}
		subPMIC_PowerOff(0x0);

		mmio_cam_control_clocks(info, false);

		info->pdata->power_disable(info->pdata);

		mdelay(CLOCK_ENABLE_DELAY);

		gpio_set_value(PRIMARY_CAMERA_RESET, 0);
		gpio_set_value(PRIMARY_CAMERA_STBY, 0);
		gpio_set_value(SECONDARY_CAMERA_RESET, 0);
		gpio_set_value(SECONDARY_CAMERA_STBY, 0);
	}
#endif
	return err;
}

int cam_clock_state;

static int
mmio_cam_control_clocks(struct mmio_info *info, enum mmio_bool_t power_on)
{
	int err = 0;

	dev_info(info->dev,
		"mmio_clocks  power_on =%d  cam_clock_state = %d",
		 power_on, cam_clock_state);

	if (power_on) {
		err = info->pdata->clock_enable(info->pdata);

		if (err) {
			dev_err(info->dev, "clock_enable failed, err = %d\n",
				err);
		} else {
			cam_clock_state = 1;
		}
	} else if (cam_clock_state == 1) {
		info->pdata->clock_disable(info->pdata);
		cam_clock_state = 0;
	}

	return err;
}

static int mmio_cam_set_pri_hwif(struct mmio_info *info)
{
#if 0
	if (info->xshutdown_enabled)
		info->pdata->set_xshutdown(info->pdata);
#endif
	return 0;
}

static int mmio_cam_set_sec_hwif(struct mmio_info *info)
{
#if 0
	if (info->xshutdown_enabled)
		info->pdata->set_xshutdown(info->pdata);
#endif
	return 0;
}

static int mmio_cam_init_mmdsp_timer(struct mmio_info *info)
{
	/* Disabling Accelerators timers */
	clrbits32(info->crbase, CR_REG0_HTIMEN);
	/* Write MMDSPTimer */
	writel(0, info->siabase + SIA_TIMER_ITC);
	/* Enabling Accelerators timers */
	setbits32(info->crbase, CR_REG0_HTIMEN);
	return 0;
}

static u32
t1_to_arm(u32 t1_addr, void __iomem *smia_base_address, u16 *p_mem_page)
{
	u16 mem_page_update = 0;
	mem_page_update =
	    (t1_addr >> FW_TO_HOST_ADDR_SHIFT) & FW_TO_HOST_CLR_MASK;

	if (mem_page_update != *p_mem_page) {
		/* Update sia_mem_page register */
		dev_dbg(info->dev, "mem_page_update=0x%x, mem_page=0x%x\n",
			mem_page_update, *p_mem_page);
		writew(mem_page_update,
		       smia_base_address + SIA_ISP_MEM_PAGE_REG);
		*p_mem_page = mem_page_update;
	}

	return SIA_ISP_MEM + (t1_addr & FW_TO_HOST_ADDR_MASK);
}

static int write_user_buffer(struct mmio_info *info, u32 ioaddr,
					void __iomem *src_buf, u32 size)
{
	u32 i, count, offset = 0;
	u32 itval = 0;
	u16 mem_page = 0;
	int err = 0;

	if (!src_buf || !ioaddr) {
		dev_err(info->dev, "invalid parameters: %p, 0x%x",
				src_buf, ioaddr);

		return -EINVAL;
	}

	for (offset = 0; offset < size; ) {

		if ((size - offset) > sizeof(copybuff))
			count = sizeof(copybuff);
		else
			count = (size - offset);

		if (copy_from_user(copybuff, src_buf + offset, count)) {

			dev_err(info->dev, "failed to copy user buffer"
				" %p at offset=%d, count=%d\n",
				src_buf, offset, count);

			err = -EFAULT;
			goto cp_failed;
		}

		for (i = 0; i < count; ) {
			itval = t1_to_arm(ioaddr + offset,
					info->siabase, &mem_page);
			itval = ((u32) info->siabase) + itval;

			writew(copybuff[i/2], itval);
			offset += 2;
			i = i + 2;
		}
	}

cp_failed:
	return err;
}

static int mmio_load_xp70_fw(struct mmio_info *info,
			     struct xp70_fw_t *xp70_fw)
{
	u32 itval = 0;
	int err = 0;

	if (xp70_fw->size_split != 0) {
		/* if buff size is not as expected */
		if (xp70_fw->size_split != L2_PSRAM_MEM_SIZE) {
			dev_err(info->dev, "xp70_fw_t.size_split must be "
				"%d bytes!\n", L2_PSRAM_MEM_SIZE);
			err = -EINVAL;
			goto err_exit;
		}

		writel(0x0, info->siabase + SIA_ISP_REG_ADDR);

		/* Put the low 64k IRP firmware in ISP MCU L2 PSRAM */
		err = write_user_buffer(info, PICTOR_IN_XP70_L2_MEM_BASE_ADDR,
					xp70_fw->addr_split,
					L2_PSRAM_MEM_SIZE);
		if (err)
			goto err_exit;
	}

	if (xp70_fw->size_data != 0) {

		writel(0x0, info->siabase + SIA_ISP_REG_ADDR);

		err = write_user_buffer(info, PICTOR_IN_XP70_TCDM_MEM_BASE_ADDR,
					xp70_fw->addr_data,
					xp70_fw->size_data);

		if (err)
			goto err_exit;
	}

	if (xp70_fw->size_esram_ext != 0) {
		/*
		 * ISP_MCU_SYS_ADDRx XP70 register (@ of ESRAM where the
		 * external code has been loaded
		 */
		writew(upper_16_bits(xp70_fw->addr_esram_ext),
		       info->siabase + SIA_ISP_MCU_SYS_ADDR0_OFFSET);
		/* ISP_MCU_SYS_SIZEx XP70 register (size of the code =64KB) */
		writew(0x0, info->siabase + SIA_ISP_MCU_SYS_SIZE0_OFFSET);
	}

	if (xp70_fw->size_sdram_ext != 0) {
		/*
		 * ISP_MCU_SYS_ADDRx XP70 register (@ of SDRAM where the
		 * external code has been loaded
		 */
		writew(upper_16_bits(xp70_fw->addr_sdram_ext),
		       info->siabase + SIA_ISP_MCU_SYS_ADDR1_OFFSET);
		/* ISP_MCU_SYS_SIZEx XP70 register */
		err = get_mcu_sys_size(xp70_fw->size_sdram_ext, &itval);

		if (err)
			goto err_exit;

		writew(itval, info->siabase + SIA_ISP_MCU_SYS_SIZE1_OFFSET);
	}

	return 0;
 err_exit:
	dev_err(info->dev, "Loading XP70 fw failed\n");
	return -EFAULT;
}

static int
mmio_map_statistics_mem_area(struct mmio_info *info, void __iomem * addr_to_map)
{
	u16 value;
	BUG_ON(addr_to_map == NULL);
	/* 16 Mbyte aligned page */
	value = PHY_TO_ISP_MCU_IO_ADDR0_HI(*((u32 *) addr_to_map));
	writew(value, info->siabase + SIA_ISP_MCU_IO_ADDR0_HI);
	/* Return the address in the XP70 address space */
	*((u32 *) addr_to_map) = (*((u32 *) addr_to_map) & XP70_ADDR_MASK) |
	    ISP_REGION_IO;
	return 0;
}

static int mmio_activate_i2c2(struct mmio_info *info, unsigned long enable)
{
	int err = 0;

	switch (enable) {
	case MMIO_ACTIVATE_I2C_HOST:
		/* Select I2C-2 */
		err = info->pdata->config_i2c_pins(info->pdata,
						   MMIO_ACTIVATE_I2C_HOST);

		if (err) {
			dev_err(info->dev, "Failed to Enable I2C-2, err %d\n",
				err);
			goto out;
		}
		break;
	case MMIO_ACTIVATE_IPI2C2:
		/* Select IPI2C */
		err =
		    info->pdata->config_i2c_pins(info->pdata,
						 MMIO_ACTIVATE_IPI2C2);

		if (err) {
			dev_err(info->dev, "Failed to Enable IPI2C, err %d\n",
				err);
			goto out;
		}

		break;
	case MMIO_DEACTIVATE_I2C:
		{
			info->pdata->config_i2c_pins(info->pdata,
						     MMIO_DEACTIVATE_I2C);
		}
		break;
	default:
		dev_warn(info->dev, "Invalid I2C2 config\n");
		err = -EINVAL;
		break;
	}

 out:
	return err;
}

static int
mmio_enable_xshutdown_from_host(struct mmio_info *info, unsigned long enable)
{
#if 0
	int err = 0;
	info->xshutdown_is_active_high = enable & MMIO_XSHUTDOWN_ACTIVE_HIGH;

	if (enable & MMIO_XSHUTDOWN_ENABLE) {
		err = info->pdata->config_xshutdown_pins(info->pdata,
						 MMIO_ENABLE_XSHUTDOWN_HOST,
						 enable &
						 MMIO_XSHUTDOWN_ACTIVE_HIGH);
	} else {
		info->pdata->config_xshutdown_pins(info->pdata,
						   MMIO_ENABLE_XSHUTDOWN_FW,
						   -1);
		/*
		 * XShutdown is controlled by firmware, initial output value is
		 * provided by firmware
		 */
	}

	info->xshutdown_enabled = enable & MMIO_XSHUTDOWN_ENABLE;
#endif
	return 0;
}

static int mmio_cam_initboard(struct mmio_info *info)
{
	int err = 0;
	err = prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP, MMIO_NAME,
			MAX_PRCMU_QOS_APP);

	if (err) {
		dev_err(info->dev, "Error adding PRCMU QoS requirement %d\n",
				err);
		goto out;
	}

	/* Initialize platform specific data */
	err = info->pdata->platform_init(info->pdata);

	if (err) {
		dev_err(info->dev,
				"Failed to execute platform init: %d\n",
				err);
		goto out;
	}

	/* Configure xshutdown to be disabled by default */
	err = mmio_enable_xshutdown_from_host(info, 0);

	if (err)
		goto out;
#if 0				/*godin */
	/* Enable IPI2C */
	err = mmio_activate_i2c2(info, MMIO_ACTIVATE_IPI2C2);
#endif
 out:
	return err;
}

static int mmio_cam_desinitboard(struct mmio_info *info)
{
	mmio_activate_i2c2(info, MMIO_DEACTIVATE_I2C);

	info->pdata->config_xshutdown_pins(info->pdata, MMIO_DISABLE_XSHUTDOWN,
			-1);

	info->pdata->platform_exit(info->pdata);

	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, MMIO_NAME);
	return 0;
}

static int
mmio_isp_write(struct mmio_info *info, struct isp_write_t *isp_write_p)
{
	int err = 0, i;
	u32 __iomem *data = NULL;
	void __iomem *addr = NULL;
	u16 mem_page = 0;
	u32 size, count, offset;

	if (!isp_write_p->count) {
		dev_warn(info->dev, "no data to write to isp\n");
		return -EINVAL;
	}

	size = isp_write_p->count * ISP_WRITE_DATA_SIZE;
	data = (u32 *) copybuff;

	for (offset = 0; offset < size; ) {

		/* 'offset' and 'size' and 'count' is in bytes */
		if ((size - offset) > sizeof(copybuff))
			count = sizeof(copybuff);
		else
			count = (size - offset);

		if (copy_from_user(data, ((u8 *)isp_write_p->data) + offset,
		    count)) {
			dev_err(info->dev, "failed to copy user buffer"
				" %p at offset=%d, count=%d\n",
				isp_write_p->data, offset, count);

			err = -EFAULT;
			goto out;
		}

		/* index 'i' and 'offset' is in bytes */
		for (i = 0; i < count; ) {
			addr = (void *)(info->siabase + t1_to_arm(
					isp_write_p->t1_dest
					+ offset,
					info->siabase, &mem_page));

			*((u32 *)addr) = data[i/ISP_WRITE_DATA_SIZE];

			offset += ISP_WRITE_DATA_SIZE;
			i = i + ISP_WRITE_DATA_SIZE;
		}
	}

out:
	return err;
}

static int mmio_set_trace_buffer(struct mmio_info *info,
				 struct trace_buf_t *buf)
{
	u32 i;
	int ret = 0;

	if (info->trace_allowed != 1) {
		dev_warn(info->dev, "trace disabled in kernel\n");
		ret = -EPERM;
		goto out;
	}

	if (!buf->size || !buf->address
			|| buf->size < sizeof(struct mmio_trace)) {
		dev_err(info->dev, "invalid xp70 trace buffer\n");
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&info->lock);
	if (info->trace_buffer) {
		dev_info(info->dev, "unmap old buffer");
		iounmap(info->trace_buffer);
		info->trace_buffer = NULL;
	}

	info->trace_buffer = ioremap((u32)buf->address, buf->size);

	if (!info->trace_buffer) {
		dev_err(info->dev, "failed to map trace buffer\n");
		ret = -ENOMEM;
		goto out_unlock;
	}

	dev_info(info->dev, "xp70 overwrite_cnt=%d (0x%x) blk_id=%d (0x%x)",
		 info->trace_buffer->overwrite_count,
		 info->trace_buffer->overwrite_count,
		 info->trace_buffer->block_id, info->trace_buffer->block_id);
#ifndef CAM_SHARED_MEM_DEBUG

	/* Reset the allocated buffer contents */
	for (i = 0; i < XP70_NB_BLOCK; i++)
		info->trace_buffer->block[i].msg_id = XP70_DEFAULT_MSG_ID;

#endif /* CAM_SHARED_MEMORY_DEBUG */
	dev_info(info->dev, "xp70 overwrite_cnt=%d (0x%x) blk_id=%d (0x%x)\n",
		 info->trace_buffer->overwrite_count,
		 info->trace_buffer->overwrite_count,
		 info->trace_buffer->block_id, info->trace_buffer->block_id);
	info->trace_status.prev_overwrite_count = 0;
	info->trace_status.prev_block_id = 0;

	/* schedule work */
	if (!schedule_delayed_work(&info->trace_work,
				   msecs_to_jiffies(XP70_TIMEOUT_MSEC)))
		dev_err(info->dev, "failed to schedule work\n");

out_unlock:
	mutex_unlock(&info->lock);
out:
	return ret;
}

static int mmio_cam_flash_set_mode(struct mmio_info *info, int lux_val)
{
	return 0;		/* Always success */
}

static int mmio_cam_flash_on_off(struct mmio_info *info, int set, int on)
{
	int i = 0;
	int lux_val = on;

#if defined(CONFIG_MACH_JANICE) || defined(CONFIG_MACH_GAVINI)
	if (lux_val == 100) {
		gpio_set_value(FLASH_EN, 0);
		for (i = lux_val; i > 1; i--) {
			gpio_set_value(FLASH_MODE, 1);
			udelay(1);
			gpio_set_value(FLASH_MODE, 0);
			udelay(1);
		}
		gpio_set_value(FLASH_MODE, 1);
		msleep(2);
	} else if (lux_val > 0 && lux_val <= 16) {  /*flash mode*/
		gpio_set_value(FLASH_EN, 1);
		if (lux_val >= 2) {
			udelay(20);
			for (i = 0; i < lux_val ; i++) {
				gpio_set_value(FLASH_MODE, 0);
				udelay(1);
				gpio_set_value(FLASH_MODE, 1);
				udelay(1);
			}
		}
	} else if (lux_val > 100) {  /*movie mode*/
		gpio_set_value(FLASH_EN, 0);

		/*MAX current  * 79%( if set  3) = Flash current*/
		for (i = 0; i < set; i++) {
			gpio_set_value(FLASH_MODE, 0);
			udelay(1);
			gpio_set_value(FLASH_MODE, 1);
			udelay(1);
		}
		msleep(1);

		for (i = 0; i < 20; i++) { /* register 3*/
			gpio_set_value(FLASH_MODE, 0);
			udelay(1);
			gpio_set_value(FLASH_MODE, 1);
			udelay(1);
		}
		msleep(1);
		/* Flash current  * ratio  = movie current*/
		for (i = 0; i < lux_val - 100; i++) {
			gpio_set_value(FLASH_MODE, 0);
			udelay(1);
			gpio_set_value(FLASH_MODE, 1);
			udelay(1);
		}

	} else {
		gpio_set_value(FLASH_EN, 0);
		gpio_set_value(FLASH_MODE, 0);
	}
#elif defined(CONFIG_MACH_SEC_GOLDEN) || defined(CONFIG_MACH_SEC_KYLE) /* RT8515 */
    if(0 < lux_val && lux_val <= 16)  /* Flash mode -> Static Brightness */
    {
        gpio_set_value(FLASH_EN, 0);
		udelay(1);
		gpio_set_value(FLASH_EN, 1);
		udelay(1);
    }
	else if( 100 < lux_val && lux_val < 200) /* AF & Flash mode */
	{
        for(i=0; i<lux_val-100; i++)
        {
            gpio_set_value(FLASH_MODE, 0);
			udelay(1);
			gpio_set_value(FLASH_MODE, 1);
			udelay(1);
        }
	}
	else /* Turn off */
	{
        gpio_set_value(FLASH_EN, 0);
		gpio_set_value(FLASH_MODE, 0);
	}

#else     /* KTD262*/
	 if (lux_val > 0 && lux_val <= 16) { /* Flash mode */
		for (i = 0; i < lux_val ; i++) {
			gpio_set_value(FLASH_EN, 0);
			udelay(1);
			gpio_set_value(FLASH_EN, 1);
			udelay(1);
		}
	} else if (lux_val > 100 && lux_val < 200) {  /*movie mode*/
		for (i = 0; i < lux_val-100 ; i++) {
			gpio_set_value(FLASH_EN, 0);
			udelay(1);
			gpio_set_value(FLASH_EN, 1);
			udelay(1);
		}
	} else if (lux_val >= 200) {  /*movie mode fix current*/
		gpio_set_value(FLASH_MODE, 1);
	} else {
		gpio_set_value(FLASH_EN, 0);
		gpio_set_value(FLASH_MODE, 0);
		msleep(10);
	}
#endif
	return 0;		/* Always success */
}


static long mmio_ioctl(struct file *filp, u32 cmd,
		      unsigned long arg)
{
	struct mmio_input_output_t data;
	int no_of_bytes;
	int enable;
	int ret = 0;
	struct mmio_info *info = (struct mmio_info *)filp->private_data;
	BUG_ON(info == NULL);

	switch (cmd) {
	case MMIO_CAM_INITBOARD:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_INITBOARD\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user(&data, (struct mmio_input_output_t *)arg,
				   no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		info->pdata->camera_slot = data.mmio_arg.camera_slot;
		ret = mmio_cam_initboard(info);
		break;
	case MMIO_CAM_DESINITBOARD:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_DESINITBOARD\n");
		ret = mmio_cam_desinitboard(info);
		info->pdata->camera_slot = -1;
		break;
	case MMIO_CAM_PWR_SENSOR:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_PWR_SENSOR\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_cam_pwr_sensor(info, data.mmio_arg.power_on);
		break;
	case MMIO_CAM_SET_EXT_CLK:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_SET_EXT_CLK\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_cam_control_clocks(info, data.mmio_arg.power_on);
		break;
	case MMIO_CAM_LOAD_XP70_FW:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_LOAD_XP70_FW\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_load_xp70_fw(info, &data.mmio_arg.xp70_fw);
		break;
	case MMIO_CAM_MAP_STATS_AREA:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_MAP_STATS_AREA\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_map_statistics_mem_area(info,
						   &data.mmio_arg.addr_to_map);

		if (0 != ret) {
			dev_err(info->dev,
				"Unable to map Statistics Mem area\n");
			break;
		}

		if (copy_to_user((struct mmio_input_output_t *)arg,
				 &data, sizeof(no_of_bytes))) {
			dev_err(info->dev, "Copy to userspace failed\n");
			ret = -EFAULT;
			break;
		}

		break;
	case MMIO_CAM_SET_PRI_HWIF:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_SET_PRI_HWIF\n");
		ret = mmio_cam_set_pri_hwif(info);
		break;
	case MMIO_CAM_SET_SEC_HWIF:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_SET_SEC_HWIF\n");
		ret = mmio_cam_set_sec_hwif(info);
		break;
	case MMIO_CAM_INITMMDSPTIMER:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_INITMMDSPTIMER\n");
		ret = mmio_cam_init_mmdsp_timer(info);
		break;
	case MMIO_CAM_ISP_WRITE:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_ISP_WRITE\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_isp_write(info, &data.mmio_arg.isp_write);
		break;
	case MMIO_ACTIVATE_I2C2:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_ACTIVATE_I2C2\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user(&enable, (int *)arg, sizeof(enable))) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_activate_i2c2(info, enable);
		break;
	case MMIO_ENABLE_XSHUTDOWN_FROM_HOST:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_ENABLE_XSHUTDOWN_FROM_HOST\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user(&enable, (int *)arg, sizeof(enable))) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_enable_xshutdown_from_host(info, enable);
		break;
	case MMIO_CAM_GET_IP_GPIO:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_GET_IP_GPIO\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		data.mmio_arg.xshutdown_info.ip_gpio =
			info->pdata->reset_ipgpio
			[data.mmio_arg.xshutdown_info.camera_function];

		if (copy_to_user((struct mmio_input_output_t *)arg,
				 &data, sizeof(no_of_bytes))) {
			dev_err(info->dev, "Copy to userspace failed\n");
			ret = -EFAULT;
			break;
		}

		break;
	case MMIO_CAM_SET_TRACE_BUFFER:
		dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_SET_TRACE_BUFFER\n");
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
		    (&data, (struct mmio_input_output_t *)arg, no_of_bytes)) {
			dev_err(info->dev, "Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_set_trace_buffer(info, &data.mmio_arg.trace_buf);
		break;

    /* Samsung+ */
    case MMIO_CAM_FLASH_SET_MODE:
	    dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_FLASH_SET_MODE\n");
	    {
		    int mode = 0;
		    if (copy_from_user(&mode, (int *)arg, sizeof(mode))) {
			    dev_err(info->dev,
				    "Copy from userspace failed\n");
			    ret = -EFAULT;
			    break;
		    }
		    if (assistive_mode == 0)
			    ret = mmio_cam_flash_set_mode(info, mode);
	    }
	    break;

    case MMIO_CAM_FLASH_ON_OFF:
	    dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_FLASH_ON_OFF\n");
	    {
		    int on = 0;
		    if (copy_from_user(&on, (int *)arg, sizeof(on))) {
			    dev_err(info->dev,
				    "Copy from userspace failed\n");
			    ret = -EFAULT;
			    break;
		    }
		    if (assistive_mode == 0)
			    ret = mmio_cam_flash_on_off(info, 3, on);
	    }

	    break;

    case MMIO_CAM_SYSTEM_REV:
	    dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_SYSTEM_REV\n");
	    {
		    if (copy_to_user((unsigned int *)arg,
				     &system_rev, sizeof(system_rev))) {
			    dev_err(info->dev,
				    "Copy to userspace failed\n");
			    ret = -EFAULT;
			    break;
		    }
	    }
	    break;

case MMIO_CAM_FRONT_CAM_ID:
	dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_FRONT_CAM_ID\n");
	    {
		/* copy id (VT_CAM_ID) to userspace using IOCTL*/
	       if (copy_to_user((unsigned int *)arg, &vt_id, sizeof(vt_id))) {
		    /* When failed */
		    dev_err(info->dev, "Copy to userspace failed\n");
		    ret = -EFAULT;
		    break;
	       }
	    }
	    break;

    case MMIO_CAM_GPIO_PIN_CONTROL:
	    dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_GPIO_PIN_CONTROL\n");
	    {
		    int value = 0;
		    if (copy_from_user(&value, (int *)arg, sizeof(value))) {
			    dev_err(info->dev,
				    "Copy from userspace failed\n");
			    ret = -EFAULT;
			    break;
		    }
		    ret = mmio_cam_gpio_pin_control((value & 0x0000FFFF),
						    ((value & 0xFFFF0000) >>
						     16));
	    }
	    break;

    case MMIO_CAM_POWER_PIN_CONTROL:
	    dev_dbg(info->dev, "mmio_ioctl: MMIO_CAM_POWER_PIN_CONTROL\n");
	    {
		    int value = 0;
		    if (copy_from_user(&value, (int *)arg, sizeof(value))) {
			    dev_err(info->dev,
				    "Copy from userspace failed\n");
			    ret = -EFAULT;
			    break;
		    }
		    ret = mmio_cam_power_pin_control((value & 0x0000FFFF),
						     ((value & 0xFFFF0000)
						      >> 16));
	    }
	    break;

    /* Samsung- */

	default:
		dev_err(info->dev, "Not an ioctl for this module (cmd = 0x%X)\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mmio_release(struct inode *node, struct file *filp)
{
	struct mmio_info *info = filp->private_data;
	BUG_ON(info == NULL);
	printk(KERN_DEBUG "mmio_release\n");

	mutex_lock(&info->lock);
	if (info->trace_buffer) {
		flush_delayed_work_sync(&info->trace_work);
		iounmap(info->trace_buffer);
		info->trace_buffer = NULL;
	}
	mutex_unlock(&info->lock);

	/* If not already done... */
	if (info->pdata->camera_slot != -1) {
		/* ...force camera to power off */
		mmio_cam_pwr_sensor(info, false);
		mmio_cam_control_clocks(info, false);
		/* ...force to desinit board */
		mmio_cam_desinitboard(info);
		info->pdata->camera_slot = -1;
	}

	return 0;
}

static int mmio_open(struct inode *node, struct file *filp)
{
	filp->private_data = info;	/* Hook our mmio info */
	return 0;
}

static const struct file_operations mmio_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mmio_ioctl,
	.open = mmio_open,
	.release = mmio_release,
};

static ssize_t
xp70_data_show(struct device *device, struct device_attribute *attr, char *buf)
{
	int i;
	int len;
	int size = 0;
	int count = 0;
	int first_index;
	mutex_lock(&info->lock);
	first_index = info->trace_status.prev_block_id + 1;

	if (!info->trace_buffer || info->trace_buffer->block_id ==
			XP70_MAX_BLOCK_ID)
		goto out_unlock;

	if (info->trace_allowed != 1) {
		dev_warn(info->dev, "xp70 trace disabled in kernel\n");
		size = sprintf(buf, "xp70 trace disabled in kernel, "
			       "use sysfs to enable\n");
		goto out_unlock;
	}

	count = info->trace_buffer->block_id - info->trace_status.prev_block_id;

	if ((info->trace_buffer->overwrite_count -
	     info->trace_status.prev_overwrite_count) * XP70_NB_BLOCK
	    + (info->trace_buffer->block_id - info->trace_status.prev_block_id)
	    >= XP70_NB_BLOCK) {
		/* overflow case */
		info->trace_status.prev_block_id =
			info->trace_buffer->block_id - XP70_NB_BLOCK;
		first_index = info->trace_buffer->block_id + 1;
		count = XP70_NB_BLOCK;
		len = sprintf(buf, "XP70 trace overflow\n");
		size += len;
		buf += len;
	}

	for (i = first_index; count; count--) {
		int msg_len;

		if (i < 0 || i >= XP70_NB_BLOCK || count > XP70_NB_BLOCK) {
			dev_err(info->dev, "trace index out-of-bounds\n");
			goto out_unlock;
		}

		msg_len = strnlen(info->trace_buffer->block[i].data,
				  XP70_BLOCK_SIZE);

		if (msg_len > 0) {
			/* zero terminate full length message */
			if (msg_len == XP70_BLOCK_SIZE)
				info->trace_buffer->block[i].data[
					XP70_BLOCK_SIZE - 1] = '\0';

			len = snprintf(buf, PAGE_SIZE - size, "%d %s\n",
				       info->trace_buffer->block[i].msg_id,
				       info->trace_buffer->block[i].data);

			if (len > PAGE_SIZE - size) {
				dev_err(info->dev, "sysfs buffer overflow\n");
				size = PAGE_SIZE;
				goto out_unlock;
			}

			size += len;
			buf += len;
		}

		i = (i + 1) % XP70_NB_BLOCK;
	}

out_unlock:
	mutex_unlock(&info->lock);
	return size;
}

static ssize_t
xp70_trace_allowed_show(struct device *device,
			struct device_attribute *attr, char *buf)
{
	int len;
	len = sprintf(buf, "%d\n", info->trace_allowed);
	return len;
}

static ssize_t xp70_trace_allowed_store(struct device *device,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (count <= 0) {
		dev_err(info->dev, "empty buffer to store\n");
		return 0;
	}

	if (buf[0] == '1')
		info->trace_allowed = 1;
	else if (buf[0] == '0')
		info->trace_allowed = 0;
	else
		dev_err(info->dev, "illegal trace_allowed val %c\n", buf[0]);

	return count;
}

static struct device_attribute xp70_device_attrs[] = {
	__ATTR_RO(xp70_data),
	__ATTR(trace_allowed, S_IRUGO | S_IWUSR, xp70_trace_allowed_show,
	xp70_trace_allowed_store),
	__ATTR_NULL
};

static void xp70_buffer_wqtask(struct work_struct *data)
{
	int i;
	int first_index = info->trace_status.prev_block_id + 1;
	int count;
	mutex_lock(&info->lock);

	if (!info->trace_buffer)
		goto out_err;

	dev_info(info->dev, "xp70 overwrite_cnt=%d (0x%x) blk_id=%d (0x%x)",
		 info->trace_buffer->overwrite_count,
		 info->trace_buffer->overwrite_count,
		 info->trace_buffer->block_id, info->trace_buffer->block_id);

	/* check if trace already started */
	if (info->trace_buffer->block_id == XP70_MAX_BLOCK_ID ||
		info->trace_buffer->block_id == XP70_DEFAULT_MSG_ID ||
		info->trace_buffer->overwrite_count == XP70_DEFAULT_MSG_ID)
		goto out;

	if ((info->trace_buffer->overwrite_count -
			info->trace_status.prev_overwrite_count) * XP70_NB_BLOCK
			+ (info->trace_buffer->block_id -
			   info->trace_status.prev_block_id)
			>= XP70_NB_BLOCK) {
		/* overflow case */
		info->trace_status.prev_block_id =
			info->trace_buffer->block_id - XP70_NB_BLOCK;
		first_index = info->trace_buffer->block_id + 1;
		count = XP70_NB_BLOCK;

		pr_info_ratelimited("XP70 trace overflow\n");
	} else if (info->trace_buffer->block_id
			>= info->trace_status.prev_block_id) {
		count = info->trace_buffer->block_id -
			info->trace_status.prev_block_id;
	} else {
		u32 block_id, prev_block_id, diff;
		block_id = (u32)(info->trace_buffer->block_id);
		prev_block_id = (u32)(info->trace_status.prev_block_id);
		diff = (block_id + XP70_NB_BLOCK) - prev_block_id;
		count = (u32)diff;
	}

	for (i = first_index; count; count--) {
		if (i < 0 || i >= XP70_NB_BLOCK || count > XP70_NB_BLOCK) {
				pr_info_ratelimited("trace index out-of-bounds"
					 "i=%d count=%d XP70_NB_BLOCK=%d\n",
					 i, count, XP70_NB_BLOCK);

			break;
		}

		if (info->trace_buffer->block[i].msg_id
					!= XP70_DEFAULT_MSG_ID) {
			int msg_len = strnlen(info->trace_buffer->block[i].data,
					XP70_BLOCK_SIZE);

			/* zero terminate full length message */
			if (msg_len > 0) {
				if (msg_len == XP70_BLOCK_SIZE)
					info->trace_buffer->block[i].
					    data[XP70_BLOCK_SIZE - 1] = '\0';

				dev_info(info->dev, "%d %s\n",
					 info->trace_buffer->block[i].msg_id,
					 info->trace_buffer->block[i].data);
			}
		}

		i = (i + 1) % XP70_NB_BLOCK;
	}

	info->trace_status.prev_overwrite_count =
		info->trace_buffer->overwrite_count;
	info->trace_status.prev_block_id = info->trace_buffer->block_id;
out:
	/* Schedule work */
	if (!schedule_delayed_work(&info->trace_work,
				   msecs_to_jiffies(XP70_TIMEOUT_MSEC)))
		dev_info(info->dev, "failed to schedule work\n");

out_err:
	mutex_unlock(&info->lock);
	return;
}

/*godin +*/

struct device *cam_dev;
struct device *flash_dev;
struct device *cam_dev_front;
struct device *cam_dev_rear;
struct device *cam_dev_flash;

static ssize_t
front_camera_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
      char camType[128] = {0};
#if defined (CONFIG_MACH_CODINA) || defined(CONFIG_MACH_SEC_GOLDEN)
      strncpy(camType, "SF_SR030PC50_NONE\n" , 128);
#elif defined CONFIG_MACH_JANICE
      strncpy(camType, "SLSI_S5K6AAFX_NONE\n" , 128);
#else
      strncpy(camType, "DB_DB8131M_NONE\n" , 128);
#endif
      return sprintf(buf, "%s", camType);
}

static ssize_t
rear_camera_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
      char camType[128] = {0};
#if defined CONFIG_MACH_GAVINI
      strncpy(camType, "SONY_ISX012_NONE\n" , 128);
#else
      strncpy(camType, "SLSI_S5K4ECGX_NONE\n" , 128);
#endif
	return sprintf(buf, "%s", camType);
}

static ssize_t
rear_flash_enable_store(struct device *dev,
		   struct device_attribute *attr, char *buf, size_t size)
{
	if (buf[0] == '0') {
		assistive_mode = 0;
		mmio_cam_flash_on_off(info, 3, 0);
	} else {
		assistive_mode = 1;
#if defined CONFIG_MACH_GAVINI
		mmio_cam_flash_on_off(info, 2, (100+5));
#else
		mmio_cam_flash_on_off(info, 3, (100+3));
#endif
	}
	return size;
}

static DEVICE_ATTR(camtype, 0440, rear_camera_type_show, NULL);
static DEVICE_ATTR(enable, 0220, NULL, rear_flash_enable_store);
static DEVICE_ATTR(front_camtype, 0440, front_camera_type_show, NULL);
static DEVICE_ATTR(rear_camtype, 0440, rear_camera_type_show, NULL);
static DEVICE_ATTR(rear_flash, 0220, NULL, rear_flash_enable_store);

void sec_cam_init(void)
{

	cam_dev = device_create(sec_class, NULL, 0, NULL, "sec_cam");
	if (IS_ERR(cam_dev))
		pr_err("Failed to create device(sec_cam)!\n");
	if (device_create_file(cam_dev, &dev_attr_camtype) < 0) {
		printk(KERN_DEBUG "%s: failed to create device file, %s\n",
		       __func__, dev_attr_camtype.attr.name);
	}

	flash_dev = device_create(sec_class, NULL, 0, NULL, "flash");
	if (IS_ERR(flash_dev))
		pr_err("Failed to create device(flash_dev)!\n");
	if (device_create_file(flash_dev, &dev_attr_enable) < 0) {
		printk(KERN_DEBUG "%s: failed to create device file, %s\n",
		       __func__, dev_attr_enable.attr.name);
	}

	cam_dev_front = device_create(camera_class, NULL, 0, NULL, "front");
	if (IS_ERR(cam_dev_front))
		pr_err("Failed to create device(cam_dev_front)!\n");

	if (device_create_file(cam_dev_front, &dev_attr_front_camtype) < 0) {
		printk(KERN_DEBUG "%s: failed to create device file, %s\n",
		       __func__, dev_attr_front_camtype.attr.name);
	}

	cam_dev_rear = device_create(camera_class, NULL, 0, NULL, "rear");
	if (IS_ERR(cam_dev_rear))
		pr_err("Failed to create device(cam_dev_rear)!\n");

	if (device_create_file(cam_dev_rear, &dev_attr_rear_camtype) < 0) {
		printk(KERN_DEBUG "%s: failed to create device file, %s\n",
		       __func__, dev_attr_rear_camtype.attr.name);
	}

	if (device_create_file(cam_dev_rear, &dev_attr_rear_flash) < 0) {
		printk(KERN_DEBUG "%s: failed to create device file, %s\n",
		       __func__, dev_attr_rear_flash.attr.name);
	}


	cam_dev_flash = device_create(camera_class, NULL, 0, NULL, "flash");
	if (device_create_file(cam_dev_flash, &dev_attr_rear_flash) < 0) {
		printk(KERN_DEBUG "%s: failed to create device file, %s\n",
		       __func__, dev_attr_rear_flash.attr.name);
	}
}

/**
* mmio_probe() - Initialize MMIO Camera resources.
* @pdev: Platform device.
*
* Initialize the module and register misc device.
*
* Returns:
*	0 if there is no err.
*	-ENOMEM if allocation fails.
*	-EEXIST if device has already been started.
*	Error codes from misc_register.
*/
static int __devinit mmio_probe(struct platform_device *pdev)
{
	int err = 0;
	int i;
	int ret;
	printk(KERN_INFO "%s\n", __func__);
	/* Initialize private data. */
	info = kzalloc(sizeof(struct mmio_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "Could not alloc info struct\n");
		err = -ENOMEM;
		goto err_alloc;
	}

	/* Fill in private data */
	info->pdata = pdev->dev.platform_data;

    if (info->pdata == NULL) {
        dev_err(&pdev->dev, "info->pdata is NULL pointer\n");
        BUG_ON(info->pdata == NULL);
	goto err_miscreg;
    }

	info->dev = &pdev->dev;
	info->pdata->dev = &pdev->dev;
	info->misc_dev.minor = MISC_DYNAMIC_MINOR;
	info->misc_dev.name = MMIO_NAME;
	info->misc_dev.fops = &mmio_fops;
	info->misc_dev.parent = pdev->dev.parent;

	mutex_init(&info->lock);

	info->xshutdown_enabled = 0;
	info->xshutdown_is_active_high = 0;
	info->trace_allowed = 0;

	/* Register Misc character device */
	err = misc_register(&(info->misc_dev));

	if (err) {
		dev_err(&pdev->dev, "Error %d registering misc dev!", err);
		goto err_miscreg;
	}

	/* Memory mapping */
	info->siabase = ioremap(info->pdata->sia_base, SIA_ISP_MCU_SYS_SIZE);

	if (!info->siabase) {
		dev_err(info->dev, "Could not ioremap SIA_BASE\n");
		err = -ENOMEM;
		goto err_ioremap_sia_base;
	}

	info->crbase = ioremap(info->pdata->cr_base, PAGE_SIZE);

	if (!info->crbase) {
		dev_err(info->dev, "Could not ioremap CR_BASE\n");
		err = -ENOMEM;
		goto err_ioremap_cr_base;
	}

	/* create sysfs entries */
	for (i = 0; attr_name(xp70_device_attrs[i]); i++) {
		ret = device_create_file(info->misc_dev.this_device,
					 &xp70_device_attrs[i]);

		if (ret) {
			dev_err(info->dev, "Error creating SYSFS entry"
				" %s (%d)\n", xp70_device_attrs[i].attr.name,
				ret);
		}
	}

	INIT_DELAYED_WORK(&info->trace_work, xp70_buffer_wqtask);
	dev_info(&pdev->dev, "MMIO driver initialized with minor=%d\n",
		 info->misc_dev.minor);
	/*godin+ */

/* Function Pointer Mapping */
#if defined(CONFIG_MACH_JANICE) || defined(CONFIG_MACH_GAVINI)
    dev_info(info->dev, "NCP6914 Camera Sub-PMIC\n");
    subPMIC_module_init = NCP6914_subPMIC_module_init;
    subPMIC_module_exit = NCP6914_subPMIC_module_exit;
    subPMIC_PowerOn     = NCP6914_subPMIC_PowerOn;
    subPMIC_PowerOff    = NCP6914_subPMIC_PowerOff;
    subPMIC_PinOnOff    = NCP6914_subPMIC_PinOnOff;
#elif defined(CONFIG_MACH_SEC_GOLDEN)
    if(GOLDEN_BRINGUP <= system_rev && system_rev < GOLDEN_R0_4)
    {
        dev_info(info->dev, "system_rev %d, SM5103 Camera Sub-PMIC\n", system_rev);
        subPMIC_module_init = SM5103_subPMIC_module_init;
        subPMIC_module_exit = SM5103_subPMIC_module_exit;
        subPMIC_PowerOn     = SM5103_subPMIC_PowerOn;
        subPMIC_PowerOff    = SM5103_subPMIC_PowerOff;
        subPMIC_PinOnOff    = SM5103_subPMIC_PinOnOff;
    }
    else if(GOLDEN_R0_4 <= system_rev)
    {
        dev_info(info->dev, "system_rev %d, NCP6914 Camera Sub-PMIC\n", system_rev);
        subPMIC_module_init = NCP6914_subPMIC_module_init;
        subPMIC_module_exit = NCP6914_subPMIC_module_exit;
        subPMIC_PowerOn     = NCP6914_subPMIC_PowerOn;
        subPMIC_PowerOff    = NCP6914_subPMIC_PowerOff;
        subPMIC_PinOnOff    = NCP6914_subPMIC_PinOnOff;
    }
    else
    {
        dev_err(info->dev, "system_rev: %d, Could not find Camera Sub-PMIC\n", system_rev);
    }
#elif defined(CONFIG_MACH_CODINA)
    switch (system_rev){
	case CODINA_TMO_R0_0:
	case CODINA_TMO_R0_0_A:
	case CODINA_TMO_R0_1:
	case CODINA_TMO_R0_4:
	case CODINA_TMO_R0_5:
	case CODINA_TMO_R0_6:
		dev_info(info->dev, "system_rev %d, NCP6914 Camera Sub-PMIC\n", system_rev);
		subPMIC_module_init = NCP6914_subPMIC_module_init;
		subPMIC_module_exit = NCP6914_subPMIC_module_exit;
		subPMIC_PowerOn     = NCP6914_subPMIC_PowerOn;
		subPMIC_PowerOff    = NCP6914_subPMIC_PowerOff;
		subPMIC_PinOnOff    = NCP6914_subPMIC_PinOnOff;
		break;

	case CODINA_TMO_R0_2:
		dev_info(info->dev, "system_rev %d, SM5103 Camera Sub-PMIC\n", system_rev);
		subPMIC_module_init = SM5103_subPMIC_module_init;
		subPMIC_module_exit = SM5103_subPMIC_module_exit;
		subPMIC_PowerOn     = SM5103_subPMIC_PowerOn;
		subPMIC_PowerOff    = SM5103_subPMIC_PowerOff;
		subPMIC_PinOnOff    = SM5103_subPMIC_PinOnOff;
		break;

	default:
		dev_err(info->dev, "system_rev: %d, Could not find Camera Sub-PMIC\n", system_rev);
		break;
    }

#elif defined(CONFIG_MACH_SEC_KYLE)

	if(KYLE_ATT_R0_1 == system_rev) {
		dev_info(info->dev, "system_rev %d, SM5103 Camera Sub-PMIC\n", system_rev);
		subPMIC_module_init = SM5103_subPMIC_module_init;
		subPMIC_module_exit = SM5103_subPMIC_module_exit;
		subPMIC_PowerOn 	= SM5103_subPMIC_PowerOn;
		subPMIC_PowerOff	= SM5103_subPMIC_PowerOff;
		subPMIC_PinOnOff	= SM5103_subPMIC_PinOnOff;
	} else {
		dev_info(info->dev, "system_rev %d, NCP6914 Camera Sub-PMIC\n", system_rev);
		subPMIC_module_init = NCP6914_subPMIC_module_init;
		subPMIC_module_exit = NCP6914_subPMIC_module_exit;
		subPMIC_PowerOn 	= NCP6914_subPMIC_PowerOn;
		subPMIC_PowerOff	= NCP6914_subPMIC_PowerOff;
		subPMIC_PinOnOff	= NCP6914_subPMIC_PinOnOff;
    }
#endif

	sec_camera_gpio_init();
	subPMIC_module_init();

	assistive_mode = 0;
	cam_clock_state = 0;
	vt_id = 0;  /* Global variable for (VT_CAM_ID) value*/

#if defined(CONFIG_MACH_CODINA) || defined(CONFIG_MACH_JANICE)
	/*Check fromt camera VT_CAM_ID*/
	check_VT_CAM_ID(VT_CAM_ID_CHECK_POWER);
#endif

	/*godin- */
	return 0;

err_ioremap_cr_base:
	iounmap(info->siabase);
err_ioremap_sia_base:
	misc_deregister(&info->misc_dev);
err_miscreg:
	kfree(info);
	info = NULL;
err_alloc:
	return err;
}

void check_VT_CAM_ID(int pin)
{
   subPMIC_PowerOn(0); /* subPMIC_PowerOn option 0 for Janice and Codina */

   mmio_cam_power_pin_control(pin, ON); /* Power PIN ON*/

   /* delay time is needed because PMIC power up consume small piece of time*/
   udelay(100);        /* delay 100 micro sec for PMIC power up*/

   gpio_request(VT_CAM_ID, "VT_CAM_ID"); /* GPIO PIN Request*/

   vt_id = gpio_get_value(VT_CAM_ID); /* GET GPIO pin value*/

   /* Print out VT_CAM_ID GPIO Value - kernel log*/
   printk(KERN_INFO "VT_CAM_ID = %d\n", vt_id);

   mmio_cam_power_pin_control(pin, OFF); /* Power PIN Off*/

   subPMIC_PowerOff(0); /* subPMIC_PowerOff */
}


/**
* mmio_remove() - Release MMIO Camera resources.
* @pdev:	Platform device.
*
* Remove misc device and free resources.
*
* Returns:
*	0 if success.
*	Error codes from misc_deregister.
*/
static int __devexit mmio_remove(struct platform_device *pdev)
{
	int err;
	int i;

	if (!info)
		return 0;

	/* Samsung+ */
	subPMIC_module_exit();
	/* Samsung- */

	flush_scheduled_work();

	/* sysfs parameters */
	for (i = 0; attr_name(xp70_device_attrs[i]); i++)
		device_remove_file(info->misc_dev.this_device,
				   &xp70_device_attrs[i]);

	err = misc_deregister(&info->misc_dev);

	if (err)
		dev_err(&pdev->dev, "Error %d deregistering misc dev", err);

	iounmap(info->siabase);
	iounmap(info->crbase);
	mutex_destroy(&info->lock);
	kfree(info);
	info = NULL;
	return 0;
}
static struct platform_driver mmio_driver = {
	.driver = {
		.name = MMIO_NAME,
		.owner = THIS_MODULE,
	},
	.probe = mmio_probe,
	.remove = __devexit_p(mmio_remove)
};

/**
* mmio_init() - Initialize module.
*
* Registers platform driver.
*/
static int __init mmio_init(void)
{
	return platform_driver_register(&mmio_driver);
}

/**
* mmio_exit() - Remove module.
*
* Unregisters platform driver.
*/
static void __exit mmio_exit(void)
{
	platform_driver_unregister(&mmio_driver);
}

module_init(mmio_init);
module_exit(mmio_exit);

MODULE_AUTHOR("Joakim Axelsson ST-Ericsson");
MODULE_AUTHOR("Pankaj Chauhan ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MMIO Camera driver");
