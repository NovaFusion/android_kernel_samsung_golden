/*
 * Kinetic KTD259 LED driver.
 *
 * Copyright (c) 2011 Samsung Electronics (UK) Ltd.
 *
 * Author: Gareth Phillips  <gareth.phillips@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/earlysuspend.h>
#include <video/ktd259x_bl.h>


/* to be removed when driver works */
/*#define dev_dbg	dev_info */


#define KTD259_BACKLIGHT_OFF		0
#define KTD259_MIN_CURRENT_RATIO	1	/* 1/32 full current */
#define KTD259_MAX_CURRENT_RATIO	32	/* 32/32 full current */


/* WARNING:
    If incrementing T_LOW_NS or T_HIGH_NS see WARNING comment in function ktd259_set_brightness().
*/
#define T_LOW_NS       (200 + 10) /* Additional 10 as safety factor */
#define T_HIGH_NS      (200 + 10) /* Additional 10 as safety factor */

#define T_STARTUP_MS   1
#define T_OFF_MS       3

struct ktd259 {
	unsigned int currentRatio;
	unsigned int brightness;
	const struct ktd259x_bl_platform_data *pd;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend	earlysuspend;
#endif
};


static int ktd259_set_brightness(struct backlight_device *bd)
{
	struct ktd259 *pKtd259Data = bl_get_data(bd);
	struct ktd259x_bl_platform_data *pd = pKtd259Data->pd;
	int reqBrightness = bd->props.brightness;
	int currentRatio  = pKtd259Data->currentRatio;
	int newCurrentRatio;
	int step_count = 0;
	unsigned long irqFlags;

	dev_dbg(&bd->dev, "%s fuction enter\n", __func__);

	if ((bd->props.power != FB_BLANK_UNBLANK) ||
		(bd->props.fb_blank != FB_BLANK_UNBLANK)) {
		reqBrightness = KTD259_BACKLIGHT_OFF;
	} else if (reqBrightness < KTD259_BACKLIGHT_OFF) {
		reqBrightness = KTD259_BACKLIGHT_OFF;
	} else if (reqBrightness > pd->max_brightness) {
		reqBrightness = pd->max_brightness;
	}

	for (newCurrentRatio = KTD259_MAX_CURRENT_RATIO; newCurrentRatio > KTD259_BACKLIGHT_OFF; newCurrentRatio--) {
		if (reqBrightness > pd->brightness_to_current_ratio[newCurrentRatio - 1])
			break;
	}

	dev_info(&bd->dev,"brightness = %d, current ratio = %d\n",reqBrightness, newCurrentRatio);

	if (newCurrentRatio > KTD259_MAX_CURRENT_RATIO) {
		dev_warn(&bd->dev, "%s: new current ratio (%d) exceeds max (%d)\n",
			__func__, newCurrentRatio, KTD259_MAX_CURRENT_RATIO);
		newCurrentRatio = KTD259_MAX_CURRENT_RATIO;
	}

	if (newCurrentRatio != currentRatio) {
		if (newCurrentRatio == KTD259_BACKLIGHT_OFF) {
			/* Switch off backlight.
			*/
			dev_dbg(&bd->dev, "%s: switching backlight off\n", __func__);
			gpio_set_value(pd->ctrl_gpio, pd->ctrl_low);
			msleep(T_OFF_MS);
		} else {
			if (currentRatio == KTD259_BACKLIGHT_OFF) {
				/* Switch on backlight. */
				dev_dbg(&bd->dev, "%s: switching backlight on\n", __func__);
				gpio_set_value(pd->ctrl_gpio, pd->ctrl_high);
				msleep(T_STARTUP_MS);

				/* Backlight is always at full intensity when switched on. */
				currentRatio = KTD259_MAX_CURRENT_RATIO;
			}

			/* WARNING:
			The loop to set the correct current level is performed
			with interrupts disabled as it is timing critical.
			The maximum number of cycles of the loop is 32,
			so the time taken will be (T_LOW_NS + T_HIGH_NS + loop_time) * 32,
			where loop_time equals the time taken in executing the
			loop (excluding delays). If T_LOW_NS or T_HIGH_NS are
			increased care must be taken to ensure that the time
			during which interrupts are disabled will not result
			in interrupts being lost.
			*/
			local_irq_save(irqFlags);

			while (currentRatio != newCurrentRatio) {
				gpio_set_value(pd->ctrl_gpio, pd->ctrl_low);
				ndelay(T_LOW_NS);
				gpio_set_value(pd->ctrl_gpio, pd->ctrl_high);
				ndelay(T_HIGH_NS);

				/* Each time CTRL is toggled, current level drops by one step.
				     ...but loops from minimum (1/32) to maximum (32/32).
				*/
				if (currentRatio == KTD259_MIN_CURRENT_RATIO) {
					currentRatio = KTD259_MAX_CURRENT_RATIO;
				} else {
					currentRatio--;
				}
				step_count++;
			}

			local_irq_restore(irqFlags);

			dev_dbg(&bd->dev, "%s: stepped current by %d\n", __func__, step_count);

		}

		pKtd259Data->currentRatio = newCurrentRatio;
		pKtd259Data->brightness   = reqBrightness;
	}

	dev_dbg(&bd->dev, "%s fuction exit\n", __func__);

	return 0;
}


static int ktd259_get_brightness(struct backlight_device *bd)
{
	struct ktd259 *pKtd259Data = bl_get_data(bd);

	dev_dbg(&bd->dev, "%s returning %d\n", __func__, pKtd259Data->brightness);
	return pKtd259Data->brightness;
}


static const struct backlight_ops ktd259_ops = {
	.get_brightness = ktd259_get_brightness,
	.update_status  = ktd259_set_brightness,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ktd259_early_suspend(struct early_suspend *earlysuspend)
{
	struct ktd259 *pKtd259Data = container_of(earlysuspend,
						struct ktd259,
						earlysuspend);

	/* Ensures backlight is turned off */
	gpio_set_value(pKtd259Data->pd->ctrl_gpio, pKtd259Data->pd->ctrl_low);
}
static void ktd259_late_resume(struct early_suspend *earlysuspend)
{
	struct ktd259 *pKtd259Data = container_of(earlysuspend,
						struct ktd259,
						earlysuspend);

	/* backlight should be turned on when required */
}
#endif

static int ktd259_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *ktd259_backlight_device;
	struct ktd259 *pKtd259Data;

	int ret = 0;

	dev_dbg(&pdev->dev, "%s fuction enter\n", __func__);

	pKtd259Data = kmalloc(sizeof(struct ktd259), GFP_KERNEL);
	memset(pKtd259Data, 0, sizeof(struct ktd259));
	pKtd259Data->currentRatio = KTD259_BACKLIGHT_OFF;
	pKtd259Data->brightness = KTD259_BACKLIGHT_OFF;
	pKtd259Data->pd = pdev->dev.platform_data;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = pKtd259Data->pd->max_brightness;
	props.type = BACKLIGHT_RAW;

	ktd259_backlight_device = backlight_device_register(	"panel",
								&pdev->dev,
								pKtd259Data,
							     	&ktd259_ops,
							     	&props);

	if (IS_ERR(ktd259_backlight_device)) {
		dev_info(&pdev->dev, "backlight_device_register() failed\n");

		ret = PTR_ERR(ktd259_backlight_device);
	} else {
		ktd259_backlight_device->props.power      = FB_BLANK_UNBLANK;
		ktd259_backlight_device->props.brightness = ktd259_backlight_device->props.max_brightness;
		ktd259_set_brightness(ktd259_backlight_device);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	pKtd259Data->earlysuspend.level   = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	pKtd259Data->earlysuspend.suspend = ktd259_early_suspend;
	pKtd259Data->earlysuspend.resume  = ktd259_late_resume;
	register_early_suspend(&pKtd259Data->earlysuspend);
#endif

	dev_dbg(&pdev->dev, "%s fuction exit\n", __func__);

	return ret;
}


static int ktd259_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);
	struct ktd259 *pKtd259Data  = bl_get_data(bd);

	dev_dbg(&pdev->dev, "%s: unregistering backlight device\n", __func__);

	backlight_device_unregister(bd);
	kfree(pKtd259Data);

	return 0;
}

static struct platform_driver ktd259_driver = {
	.probe = ktd259_probe,
	.remove = ktd259_remove,
	.driver = {
		.name = BL_DRIVER_NAME_KTD259,
	},
};


static int __init ktd259_init(void)
{
	return platform_driver_probe(&ktd259_driver, ktd259_probe);
}

static void __exit ktd259_exit(void)
{
	platform_driver_unregister(&ktd259_driver);
}

module_init(ktd259_init);
module_exit(ktd259_exit);

MODULE_AUTHOR("Gareth Phillips <gareth.phillips@samsung.com>");
MODULE_DESCRIPTION("KTD259 Backlight Driver");
MODULE_LICENSE("GPL");

