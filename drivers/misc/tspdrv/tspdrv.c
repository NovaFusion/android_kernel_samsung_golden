/*
** =========================================================================
** File:
**     tspdrv.c
**
** Description:
**     TouchSense Kernel Module main entry-point.
**
** Portions Copyright (c) 2008-2010 Immersion Corporation. All Rights Reserved.
**
** This file contains Original Code and/or Modifications of Original Code
** as defined in and that are subject to the GNU Public License v2 -
** (the 'License'). You may not use this file except in compliance with the
** License. You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES,
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see
** the License for the specific language governing rights and limitations
** under the License.
** =========================================================================
*/

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/module.h>	/* Kernel module header */
#include <linux/kernel.h>	/* snprintf() */
#include <linux/init.h>		/* __init, __exit, __devinit, __devexit */
#include <linux/slab.h>		/* kzalloc(), kfree() */
#include <linux/mutex.h>	/* DEFINE_MUTEX(), mutex_[un]lock() */
#include <linux/delay.h>	/* msleep() */
#include <linux/i2c.h>		/* struct i2c_client, i2c_*() */
#include <linux/pm.h>		/* struct dev_pm_ops */
#include <linux/miscdevice.h>	/* struct miscdevice, misc_[de]register() */
#include <linux/mod_devicetable.h> /* MODULE_DEVICE_TABLE() */
#include <linux/sysfs.h>	/* sysfs stuff */
#include <linux/gpio.h>		/* GPIO generic functions */
#include <linux/input.h>	/* input_*() */
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>	/* request_irq(), free_irq(),
							enable_irq(), disable_irq(), */
#include "tspdrv.h"
#include "ImmVibeSPI.c"
#include <mach/isa1200.h>
#include <mach/board-sec-ux500.h>
#if defined(VIBE_DEBUG) && defined(VIBE_RECORD)
#include <tspdrvRecorder.c>
#endif

#include <linux/mfd/dbx500-prcmu.h>

#include "../../staging/android/timed_output.h"

#define MAX_TIMEOUT		10000 /* 10s */

static struct vibrator {
	struct wake_lock wklock;
	struct hrtimer timer;
	struct mutex lock;
	struct work_struct work;
	bool running;
} vibdata;

/* Uncomment the next line to enable debug prints */
/* #define	IMMVIBE_DEBUG */

#if !defined(IMMVIBE_DEBUG) && defined(DEBUG)
#define	IMMVIBE_DEBUG
#endif

#ifdef IMMVIBE_DEBUG
#define	vibdbg(_fmt, ...)	\
	printk(KERN_INFO "IMMVIBE DEBUG: " _fmt "\n", ## __VA_ARGS__)
#else
#define	vibdbg(_fmt, ...)
#endif

#define	vibinfo(_fmt, ...)	\
	printk(KERN_NOTICE "IMMVIBE INFO: " _fmt "\n", ## __VA_ARGS__)

#define	vibwarn(_fmt, ...)	\
	printk(KERN_WARNING "IMMVIBE WARN: " _fmt "\n", ## __VA_ARGS__)

#define	viberr(_fmt, ...)	\
	printk(KERN_ERR "IMMVIBE ERROR: " _fmt "\n", ## __VA_ARGS__)


/* Device name and version information */
#define VERSION_STR " v3.4.55.5\n"                  /* DO NOT CHANGE - this is auto-generated */
#define VERSION_STR_LEN 16                          /* account extra space for future extra digits in version number */
static char g_szDeviceName[(VIBE_MAX_DEVICE_NAME_LENGTH
				+ VERSION_STR_LEN) * NUM_ACTUATORS];       /* initialized in init_module */
static size_t g_cchDeviceName;                      /* initialized in init_module */

/* Flag indicating whether the driver is in use */
static char g_bIsPlaying;

/* Buffer to store data sent to SPI */
#define SPI_BUFFER_SIZE (NUM_ACTUATORS * (VIBE_OUTPUT_SAMPLE_SIZE + SPI_HEADER_SIZE))
static int g_bStopRequested;
static actuator_samples_buffer g_SamplesBuffer[NUM_ACTUATORS] = {{0},};
static char g_cWriteBuffer[SPI_BUFFER_SIZE];

/* For QA purposes */
#ifdef QA_TEST
#define FORCE_LOG_BUFFER_SIZE   128
#define TIME_INCREMENT          5
static int g_nTime;
static int g_nForceLogIndex;
static VibeInt8 g_nForceLog[FORCE_LOG_BUFFER_SIZE];
#endif

#if ((LINUX_VERSION_CODE & 0xFFFF00) < KERNEL_VERSION(2, 6, 0))
#error Unsupported Kernel version
#endif

#ifdef IMPLEMENT_AS_CHAR_DRIVER
static int g_nMajor;
#endif

/* Needs to be included after the global variables because it uses them */
#ifdef CONFIG_HIGH_RES_TIMERS
    #include "VibeOSKernelLinuxHRTime.c"
#else
    #include "VibeOSKernelLinuxTime.c"
#endif

/* isa1200 haptic controller specific */
struct isa1200_data *isa_data;
/* isa1200 specifc end */

/* When vibrator issue occur in Janice project, it need to usd APE 100% operation. */
#define REQUEST_APE_DDR_OPP     1
static int vib_opp_requested;

/* static int VibeOSKernelProcessData(struct isa1200_data *mot_data, void* data); */


/* File IO */
static int open(struct inode *inode, struct file *file);
static int release(struct inode *inode, struct file *file);
static ssize_t read(struct file *file, char *buf, size_t count, loff_t *ppos);
static ssize_t write(struct file *file, const char *buf, size_t count, loff_t *ppos);
static long ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static struct file_operations fops = {
    .owner =    THIS_MODULE,
    .read =     read,
    .write =    write,
    .unlocked_ioctl =    ioctl,
    .open =     open,
    .release =  release,
    .llseek =   default_llseek
};

#ifndef IMPLEMENT_AS_CHAR_DRIVER
static struct miscdevice miscdev = {
	.minor =    MISC_DYNAMIC_MINOR,
	.name =     "tspdrv",
	.fops =     &fops
};
#endif

static int janice_vibrator_get_time(struct timed_output_dev *dev);
static void janice_vibrator_enable(struct timed_output_dev *dev, int value);
static struct timed_output_dev to_dev = {
	.name		= "vibrator",
	.get_time	= janice_vibrator_get_time,
	.enable		= janice_vibrator_enable,
};

static enum hrtimer_restart vibrator_timer_func(struct hrtimer *timer)
{
	schedule_work(&vibdata.work);
	return HRTIMER_NORESTART;
}

static void vibrator_work(struct work_struct *work)
{
	if (!vibdata.running)
		return;

	vibdata.running = false;

	immvibe_i2c_write(isa_data->client, HCTRL0, 0x00);
	gpio_direction_output(isa_data->pdata->mot_hen_gpio, 0);
	gpio_direction_output(isa_data->pdata->mot_len_gpio, 0);

	clk_disable(isa_data->mot_clk);
#if REQUEST_APE_DDR_OPP
	/* Remove APE OPP requirement */
	if (vib_opp_requested) {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
			(char *)miscdev.name, PRCMU_QOS_DEFAULT_VALUE);
	vib_opp_requested = false;
	}
#endif
	wake_unlock(&vibdata.wklock);
}

static void janice_vibrator_enable(struct timed_output_dev *dev, int value)
{
	mutex_lock(&vibdata.lock);

	/* cancel previous timer and set GPIO according to value */
	hrtimer_cancel(&vibdata.timer);
	cancel_work_sync(&vibdata.work);

	if (value) {
		wake_lock(&vibdata.wklock);
		if (!vibdata.running) {
#if REQUEST_APE_DDR_OPP
			/* Request 100% APE OPP */
			if (vib_opp_requested == false) {
				prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
					(char *)miscdev.name, PRCMU_QOS_APE_OPP_MAX);
				vib_opp_requested = true;
			}
#endif
			clk_enable(isa_data->mot_clk);
			gpio_direction_output(isa_data->pdata->mot_hen_gpio, 1);
			gpio_direction_output(isa_data->pdata->mot_len_gpio, 1);

			udelay(200);

			immvibe_i2c_write(isa_data->client, SCTRL, g_nLDO_Voltage);

			/* If the PWM frequency is 44.8kHz, then the output frequency will be 44.8/div_factor
			HCTRL0[1:0] is the div_factor, below setting sets div_factor to 256, so o/p frequency is 175 Hz
			*/
			immvibe_i2c_write(isa_data->client, HCTRL0, 0x11);
			immvibe_i2c_write(isa_data->client, HCTRL1, 0xC0);
			immvibe_i2c_write(isa_data->client, HCTRL2, 0x00);
			immvibe_i2c_write(isa_data->client, HCTRL3, (0x03 + (g_nPWM_PLLDiv<<4)));
			immvibe_i2c_write(isa_data->client, HCTRL4, g_nPWM_Freq);
			immvibe_i2c_write(isa_data->client, HCTRL5, g_nPWM_Duty);
			immvibe_i2c_write(isa_data->client, HCTRL6, g_nPWM_Period);

			/* PWM generation mode */
			immvibe_i2c_write(isa_data->client, HCTRL0, 0x91);
			/* Duty 0x64 == nForce 90 */
			immvibe_i2c_write(isa_data->client, HCTRL5, 0x64);

			vibdata.running = true;
		} else
			pr_info("%s: value = %d, already running, rescheduling timer\n",
				__func__, value);

		if (value > 0) {
			value = value + 30;
			if (value > MAX_TIMEOUT)
				value = MAX_TIMEOUT;

			hrtimer_start(&vibdata.timer,
				ns_to_ktime((u64)value * NSEC_PER_MSEC),
				HRTIMER_MODE_REL);
		}
	}

	mutex_unlock(&vibdata.lock);
}


static int janice_vibrator_get_time(struct timed_output_dev *dev)
{
	if (hrtimer_active(&vibdata.timer)) {
		ktime_t r = hrtimer_get_remaining(&vibdata.timer);
		return ktime_to_ms(r);
	}

	return 0;
}


static int suspend(struct platform_device *pdev, pm_message_t state);
static int resume(struct platform_device *pdev);
static struct platform_driver platdrv = {
    .suspend =  suspend,
    .resume =   resume,
    .driver = {
		.name = MODULE_NAME,
    },
};

int init_tspdrv_module(void)
{
	int nRet, i;   /* initialized below */

	nRet = misc_register(&miscdev);
	if (nRet < 0) {
		DbgOut((KERN_ERR "tspdrv: misc_register failed\n"));
		return nRet;
	}

	nRet = platform_driver_register(&platdrv);
	if (nRet) {
		DbgOut((KERN_ERR "tspdrv: platform_driver_register failed.\n"));
		return nRet;
	}

	DbgRecorderInit(());

	ImmVibeSPI_ForceOut_Initialize();
    VibeOSKernelLinuxInitTimer();

	/* Get and concatenate device name and initialize data buffer */
	g_cchDeviceName = 0;
	for (i = 0; i < NUM_ACTUATORS; i++) {
		char *szName = g_szDeviceName + g_cchDeviceName;
		ImmVibeSPI_Device_GetName(i, szName, VIBE_MAX_DEVICE_NAME_LENGTH);

		/* Append version information and get buffer length */
		strcat(szName, VERSION_STR);
		g_cchDeviceName += strlen(szName);

		g_SamplesBuffer[i].nIndexPlayingBuffer = -1; /* Not playing */
		g_SamplesBuffer[i].actuatorSamples[0].nBufferSize = 0;
		g_SamplesBuffer[i].actuatorSamples[1].nBufferSize = 0;
	}

	return 0;
}

void cleanup_tspdrv_module(void)
{
	vibdbg("%s(): cleanup tspdrv module\n", __func__);

	DbgRecorderTerminate(());
    VibeOSKernelLinuxTerminateTimer();
	ImmVibeSPI_ForceOut_Terminate();

	platform_driver_unregister(&platdrv);

	misc_deregister(&miscdev);
}

static int open(struct inode *inode, struct file *file)
{
	DbgOut((KERN_INFO "tspdrv: open.\n"));

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return 0;
}

static int release(struct inode *inode, struct file *file)
{
    DbgOut((KERN_INFO "tspdrv: release.\n"));
    VibeOSKernelLinuxStopTimer();

    /*
    ** Clear the variable used to store the magic number to prevent
    ** unauthorized caller to write data. TouchSense service is the only
    ** valid caller.
    */
    file->private_data = (void *)NULL;

    module_put(THIS_MODULE);

    return 0;
}

static ssize_t read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	const size_t nBufSize = (g_cchDeviceName > (size_t)(*ppos)) ? min(count, g_cchDeviceName - (size_t)(*ppos)) : 0;

	/* End of buffer, exit */
	if (0 == nBufSize)
		return 0;

	if (0 != copy_to_user(buf, g_szDeviceName + (*ppos), nBufSize)) {
		/* Failed to copy all the data, exit */
		DbgOut((KERN_ERR "tspdrv: copy_to_user failed.\n"));
		return 0;
	}

	/* Update file position and return copied buffer size */
	*ppos += nBufSize;
	return nBufSize;
}

static ssize_t write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int i = 0;

	*ppos = 0;  /* file position not used, always set to 0 */

	/*
	** Prevent unauthorized caller to write data.
	** TouchSense service is the only valid caller.
	*/
	if (file->private_data != (void *)TSPDRV_MAGIC_NUMBER) {
		DbgOut((KERN_ERR "tspdrv: unauthorized write.\n"));
		return 0;
	}

	/* Copy immediately the input buffer */
	if (0 != copy_from_user(g_cWriteBuffer, buf, count)) {
		/* Failed to copy all the data, exit */
		DbgOut((KERN_ERR "tspdrv: copy_from_user failed.\n"));
		return 0;
	}

	/* Check buffer size */
	if ((count <= SPI_HEADER_SIZE) || (count > SPI_BUFFER_SIZE)) {
		DbgOut((KERN_ERR "tspdrv: invalid write buffer size.\n"));
		return 0;
	}

	while (i < count) {
		int nIndexFreeBuffer;   /* initialized below */

		samples_buffer* pInputBuffer = (samples_buffer *)(&g_cWriteBuffer[i]);

		if ((i + SPI_HEADER_SIZE) >= count) {
			/*
			** Index is about to go beyond the buffer size.
			** (Should never happen).
			*/
			DbgOut((KERN_EMERG "tspdrv: invalid buffer index.\n"));
		}

		/* Check bit depth */
		if (8 != pInputBuffer->nBitDepth) {
			DbgOut((KERN_WARNING "tspdrv: invalid bit depth. Use default value (8).\n"));
		}

		/* The above code not valid if SPI header size is not 3 */
#if (SPI_HEADER_SIZE != 3)
#error "SPI_HEADER_SIZE expected to be 3"
#endif

		/* Check buffer size */
		if ((i + SPI_HEADER_SIZE + pInputBuffer->nBufferSize) > count) {
			/*
			** Index is about to go beyond the buffer size.
			** (Should never happen).
			*/
			DbgOut((KERN_EMERG "tspdrv: invalid data size.\n"));
		}

		/* Check actuator index */
		if (NUM_ACTUATORS <= pInputBuffer->nActuatorIndex) {
			DbgOut((KERN_ERR "tspdrv: invalid actuator index.\n"));
			i += (SPI_HEADER_SIZE + pInputBuffer->nBufferSize);
			continue;
		}

		if (0 == g_SamplesBuffer[pInputBuffer->nActuatorIndex].actuatorSamples[0].nBufferSize)
			nIndexFreeBuffer = 0;
		else if (0 == g_SamplesBuffer[pInputBuffer->nActuatorIndex].actuatorSamples[1].nBufferSize)
			 nIndexFreeBuffer = 1;
		else {
			/* No room to store new samples  */
			DbgOut((KERN_ERR "tspdrv: no room to store new samples.\n"));
			return 0;
		}

		/* Store the data in the free buffer of the given actuator */
		memcpy(&(g_SamplesBuffer[pInputBuffer->nActuatorIndex].actuatorSamples[nIndexFreeBuffer]),
					&g_cWriteBuffer[i], (SPI_HEADER_SIZE + pInputBuffer->nBufferSize));

		/* If the no buffer is playing, prepare to play g_SamplesBuffer[pInputBuffer->nActuatorIndex].actuatorSamples[nIndexFreeBuffer] */
		if (-1 == g_SamplesBuffer[pInputBuffer->nActuatorIndex].nIndexPlayingBuffer) {
		   g_SamplesBuffer[pInputBuffer->nActuatorIndex].nIndexPlayingBuffer = nIndexFreeBuffer;
		   g_SamplesBuffer[pInputBuffer->nActuatorIndex].nIndexOutputValue = 0;
		}

		/* Increment buffer index */
		i += (SPI_HEADER_SIZE + pInputBuffer->nBufferSize);
	}

#ifdef QA_TEST
	g_nForceLog[g_nForceLogIndex++] = g_cSPIBuffer[0];
	if (g_nForceLogIndex >= FORCE_LOG_BUFFER_SIZE) {
		for (i = 0; i < FORCE_LOG_BUFFER_SIZE; i++) {
			printk(KERN_INFO "%d\t%d\n", g_nTime, g_nForceLog[i]);
			g_nTime += TIME_INCREMENT;
		}
		g_nForceLogIndex = 0;
	}
#endif

	/* Start the work after receiving new output force */
	g_bIsPlaying = true;
	VibeOSKernelLinuxStartTimer();

	return count;
}

static long ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
#ifdef QA_TEST
	int i;
#endif

	switch (cmd) {
	case TSPDRV_STOP_KERNEL_TIMER:
		/*
		** As we send one sample ahead of time, we need to finish playing the last sample
		** before stopping the timer. So we just set a flag here.
		*/
		if (true == g_bIsPlaying)
			g_bStopRequested = true;

#ifdef VIBEOSKERNELPROCESSDATA
		/* Last data processing to disable amp and stop timer */
		VibeOSKernelProcessData(NULL);
#endif

#ifdef QA_TEST
		if (g_nForceLogIndex) {
			for (i = 0; i < g_nForceLogIndex; i++) {
				printk(KERN_INFO "%d\t%d\n", g_nTime, g_nForceLog[i]);
				g_nTime += TIME_INCREMENT;
			}
		}
		g_nTime = 0;
		g_nForceLogIndex = 0;
#endif
		break;

	case TSPDRV_MAGIC_NUMBER:
		file->private_data = (void *)TSPDRV_MAGIC_NUMBER;
		break;

	case TSPDRV_ENABLE_AMP:
#if REQUEST_APE_DDR_OPP
	/* Request 100% APE OPP */
		if (vib_opp_requested == false) {
			prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
				(char *)miscdev.name, PRCMU_QOS_APE_OPP_MAX);
		vib_opp_requested = true;
		}
#endif
		ImmVibeSPI_ForceOut_AmpEnable(arg);
		DbgRecorderReset((arg));
		DbgRecord((arg, ";------- TSPDRV_ENABLE_AMP ---------\n"));
		break;

	case TSPDRV_DISABLE_AMP:
		/* Small fix for now to handle proper combination of TSPDRV_STOP_KERNEL_TIMER and TSPDRV_DISABLE_AMP together */
		/* If a stop was requested, ignore the request as the amp will be disabled by the timer proc when it's ready */
		if (!g_bStopRequested) {
#if REQUEST_APE_DDR_OPP
	/* Remove APE OPP requirement */
		if (vib_opp_requested) {
			prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
				(char *)miscdev.name, PRCMU_QOS_DEFAULT_VALUE);
		vib_opp_requested = false;
		}
#endif
			ImmVibeSPI_ForceOut_AmpDisable(arg);
		}
		break;

	case TSPDRV_GET_NUM_ACTUATORS:
		return NUM_ACTUATORS;
	}

	return 0;
}

static int suspend(struct platform_device *pdev, pm_message_t state)
{
	if (g_bIsPlaying) {
		DbgOut((KERN_INFO "tspdrv: can't suspend, still playing effects.\n"));
		return -EBUSY;
	} else {
		DbgOut((KERN_INFO "tspdrv: suspend.\n"));
		return 0;
	}
}

static int resume(struct platform_device *pdev)
{
	DbgOut((KERN_INFO "tspdrv: resume.\n"));

	return 0;   /* can resume */
}


/* -------------------------------------------------------------------------
 * I2C interface functions
 * ------------------------------------------------------------------------- */
int immvibe_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	int	ret;
	u8	data[2];

	data[0] = reg;
	data[1] = val;
	ret = i2c_master_send(client, data, 2);
	if (ret < 0) {
		viberr("Failed to send data to isa1200 [errno=%d]", ret);
	} else if (ret != 2) {
		viberr("Failed to send exactly 2 bytes to isa1200 (sent %d)", ret);
		ret = -EIO;
	} else {
		ret = 0;
	}

	return ret;
}

int immvibe_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret = 0;
	vibdbg("%s()", __func__);

	/* ret = ImmVibeSPI_ForceOut_AmpDisable(0); */
	g_bAmpEnabled = false;
	/* immvibe_i2c_write(isa_data->client, HCTRL0, 0x00); */
	gpio_direction_output(isa_data->pdata->mot_hen_gpio, 0);
	gpio_direction_output(isa_data->pdata->mot_len_gpio, 0);
	/* clk_disable(isa_data->mot_clk); */

	vibdbg("%s() : hen %d, len %d \n", __func__,
		isa_data->pdata->mot_hen_gpio, isa_data->pdata->mot_len_gpio);
	return ret;
}

int immvibe_i2c_resume(struct i2c_client *client)
{
	int ret = 0;
	vibdbg("%s()", __func__);
	return ret;
}

static int __devinit immvibe_i2c_probe(struct i2c_client* client, const struct i2c_device_id* id)
{
	int ret = 0;
	struct isa1200_platform_data *pdata;

	vibdbg("%s(), client = %s", __func__, client->name);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->adapter->dev, "%s failed %d\n", __func__, ret);
		return -ENODEV;
	}

	pdata = (struct isa1200_platform_data *) client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "platform data required for isa1200\n");
		return -EINVAL;
	}
	pdata = client->dev.platform_data;

	isa_data = kzalloc(sizeof(struct isa1200_data), GFP_KERNEL);
	if (isa_data == NULL) {
		dev_err(&client->dev, "error allocating memory\n");
		ret = -ENOMEM;
		goto out_alloc_data_failed;
	}

	isa_data->addr = client->addr;
	isa_data->client = client;
	isa_data->pdata = pdata;

#ifdef CONFIG_MACH_JANICE
	if (system_rev >= JANICE_R0_3)
		isa_data->mot_clk  = clk_get_sys("mot-pwm0", NULL);
	else
		isa_data->mot_clk  = clk_get_sys("mot-pwm1", NULL);
#elif defined(CONFIG_MACH_GAVINI)
	if (system_rev > GAVINI_R0_0_B)
		isa_data->mot_clk  = clk_get_sys("mot-pwm0", NULL);
	else
		isa_data->mot_clk  = clk_get_sys("mot-pwm1", NULL);
#endif

	if (pdata->hw_setup) {
		ret = pdata->hw_setup();
		if (ret < 0) {
			viberr("Failed to setup GPIOs for Vibrator [errno=%d]", ret);
			goto out_gpio_failed;
		}
	}

	i2c_set_clientdata(client, isa_data);

	isa_data->input = input_allocate_device();
	if (!isa_data->input) {
		pr_err("Failed to allocate Vibrator input device.");
		return -ENOMEM;
	}

	isa_data->input->name = "Vibrator";

	ret = input_register_device(isa_data->input);
	if (ret < 0) {
		pr_err("Failed to register Vibrator input device [errno=%d]", ret);
		input_free_device(isa_data->input);
		return ret;
	}

	/* initialize tspdrv module */
	ret = init_tspdrv_module();
	if (ret) {
		viberr("%s():Error initializing tspdrv", __func__);
		goto out_tspdrv_init_failed;
	}

#if REQUEST_APE_DDR_OPP
	/* add qos APE OPP */
	prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
		(char *)miscdev.name, PRCMU_QOS_DEFAULT_VALUE);
	dev_info(&client->dev, "tspdrv %s(%s) initialized\n", miscdev.name, (char *)dev_name(&client->dev));
#endif

	return ret;

out_tspdrv_init_failed:
	input_free_device(isa_data->input);

out_gpio_failed:
	clk_put(isa_data->mot_clk);
	kfree(isa_data);

out_alloc_data_failed:
	return ret;
}

static int __devexit immvibe_i2c_remove(struct i2c_client* client)
{
#if REQUEST_APE_DDR_OPP
	/* Remove APE OPP requirement */
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, (char *)miscdev.name);
#endif
	vibdbg("%s(): client %s removed", __func__, client->name);

	return 0;
}

static const struct i2c_device_id immvibe_id[] = {
	{"immvibe", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, immvibe_id);

static struct i2c_driver immvibe_i2c_driver = {
	.driver = {
		.name = "immvibe",			/* immersion touchsense player driver */
		.owner = THIS_MODULE,
	},
	.id_table = immvibe_id,
	.probe    = immvibe_i2c_probe,
	.remove   = immvibe_i2c_remove,
	.suspend = immvibe_i2c_suspend,
	.resume = immvibe_i2c_resume,
};

static int __init immvibe_init(void)
{
	int ret = 0;
	printk(KERN_ERR "%s\n", __func__);
	ret = i2c_add_driver(&immvibe_i2c_driver);

	if (ret < 0)
		viberr("%s(): Failed to add i2c driver for ISA1200, err: %d\n", __func__, ret);

	hrtimer_init(&vibdata.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibdata.timer.function = vibrator_timer_func;
	INIT_WORK(&vibdata.work, vibrator_work);

	wake_lock_init(&vibdata.wklock, WAKE_LOCK_SUSPEND, "vibrator");
	mutex_init(&vibdata.lock);

	ret = timed_output_dev_register(&to_dev);
	if (ret < 0)
		goto err_to_dev_reg;

	return 0;

err_to_dev_reg:
	viberr("%s(): Failed to register timed_output vibrator, err: %d\n", __func__, ret);
	mutex_destroy(&vibdata.lock);
	wake_lock_destroy(&vibdata.wklock);

	return ret;
}

static void __exit immvibe_exit(void)
{
	printk(KERN_DEBUG "%s\n", __func__);
	i2c_del_driver(&immvibe_i2c_driver);
	timed_output_dev_unregister(&to_dev);
	cleanup_tspdrv_module();
}

module_init(immvibe_init);
module_exit(immvibe_exit);

/* Module info */
MODULE_AUTHOR("Immersion Corporation");
MODULE_DESCRIPTION("TouchSense Kernel Module");
MODULE_LICENSE("GPL v2");
