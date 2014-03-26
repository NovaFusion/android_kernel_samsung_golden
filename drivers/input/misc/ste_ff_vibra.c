/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com>
 *	   for ST-Ericsson
 * License Terms: GNU General Public License v2
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <mach/ste_audio_io_vibrator.h>

#define FF_VIBRA_DOWN		0x0000 /* 0 degrees */
#define FF_VIBRA_LEFT		0x4000 /* 90 degrees */
#define FF_VIBRA_UP		0x8000 /* 180 degrees */
#define FF_VIBRA_RIGHT		0xC000 /* 270 degrees */

/**
 * struct vibra_info - Vibrator information structure
 * @idev:		Pointer to input device structure
 * @vibra_workqueue:	Pointer to vibrator workqueue structure
 * @vibra_work:		Vibrator work
 * @direction:		Vibration direction
 * @speed:		Vibration speed
 *
 * Structure vibra_info holds vibrator informations
 **/
struct vibra_info {
	struct input_dev	*idev;
	struct workqueue_struct *vibra_workqueue;
	struct work_struct	vibra_work;
	int			direction;
	unsigned char		speed;
};

/**
 * vibra_play_work() - Vibrator work, sets speed and direction
 * @work:	Pointer to work structure
 *
 * This function is called from workqueue, turns on/off vibrator
 **/
static void vibra_play_work(struct work_struct *work)
{
	struct vibra_info *vinfo = container_of(work,
				struct vibra_info, vibra_work);
	struct ste_vibra_speed left_speed = {
		.positive = 0,
		.negative = 0,
	};
	struct ste_vibra_speed right_speed = {
		.positive = 0,
		.negative = 0,
	};

	/* Divide by 2 because supported range by PWM is 0-100 */
	vinfo->speed /= 2;

	if ((vinfo->direction > FF_VIBRA_DOWN) &&
				(vinfo->direction < FF_VIBRA_UP)) {
		/* 1 - 179 degrees, turn on left vibrator */
		left_speed.positive = vinfo->speed;
	} else if (vinfo->direction > FF_VIBRA_UP) {
		/* more than 180 degrees, turn on right vibrator */
		right_speed.positive = vinfo->speed;
	} else {
		/* 0 (down) or 180 (up) degrees, turn on 2 vibrators */
		left_speed.positive = vinfo->speed;
		right_speed.positive = vinfo->speed;
	}

	ste_audioio_vibrator_pwm_control(STE_AUDIOIO_CLIENT_FF_VIBRA,
					 left_speed, right_speed);
}

/**
 * vibra_play() - Memless device control function
 * @idev:	Pointer to input device structure
 * @data:	Pointer to private data (not used)
 * @effect:	Pointer to force feedback effect structure
 *
 * This function controls memless device
 *
 * Returns:
 *	0 - success
 **/
static int vibra_play(struct input_dev *idev, void *data,
			struct ff_effect *effect)
{
	struct vibra_info *vinfo = input_get_drvdata(idev);

	vinfo->direction = effect->direction;
	vinfo->speed = effect->u.rumble.strong_magnitude >> 8;
	if (!vinfo->speed)
		/* Shift weak magnitude to make it feelable on vibrator */
		vinfo->speed = effect->u.rumble.weak_magnitude >> 9;

	queue_work(vinfo->vibra_workqueue, &vinfo->vibra_work);

	return 0;
}

/**
 * ste_ff_vibra_open() - Input device open function
 * @idev:      Pointer to input device structure
 *
 * This function is called on opening input device
 *
 * Returns:
 *	-ENOMEM - no memory left
 *	0	- success
 **/
static int ste_ff_vibra_open(struct input_dev *idev)
{
	struct vibra_info *vinfo = input_get_drvdata(idev);

	vinfo->vibra_workqueue =
			create_singlethread_workqueue("ste_ff-ff-vibra");
	if (!vinfo->vibra_workqueue) {
		dev_err(&idev->dev, "couldn't create vibra workqueue\n");
		return -ENOMEM;
	}
	return 0;
}

/**
 * ste_ff_vibra_close() - Input device close function
 * @idev:      Pointer to input device structure
 *
 * This function is called on closing input device
 **/
static void ste_ff_vibra_close(struct input_dev *idev)
{
	struct vibra_info *vinfo = input_get_drvdata(idev);

	cancel_work_sync(&vinfo->vibra_work);
	INIT_WORK(&vinfo->vibra_work, vibra_play_work);
	destroy_workqueue(vinfo->vibra_workqueue);
	vinfo->vibra_workqueue = NULL;
}

static int __devinit ste_ff_vibra_probe(struct platform_device *pdev)
{
	struct vibra_info *vinfo;
	int ret;

	vinfo = kmalloc(sizeof *vinfo, GFP_KERNEL);
	if (!vinfo) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	vinfo->idev = input_allocate_device();
	if (!vinfo->idev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto exit_vinfo_free;
	}

	vinfo->idev->name = "ste-ff-vibra";
	vinfo->idev->dev.parent = pdev->dev.parent;
	vinfo->idev->open = ste_ff_vibra_open;
	vinfo->idev->close = ste_ff_vibra_close;
	INIT_WORK(&vinfo->vibra_work, vibra_play_work);
	__set_bit(FF_RUMBLE, vinfo->idev->ffbit);

	ret = input_ff_create_memless(vinfo->idev, NULL, vibra_play);
	if (ret) {
		dev_err(&pdev->dev, "failed to create memless device\n");
		goto exit_idev_free;
	}

	ret = input_register_device(vinfo->idev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto exit_destroy_memless;
	}

	input_set_drvdata(vinfo->idev, vinfo);
	platform_set_drvdata(pdev, vinfo);
	return 0;

exit_destroy_memless:
	input_ff_destroy(vinfo->idev);
exit_idev_free:
	input_free_device(vinfo->idev);
exit_vinfo_free:
	kfree(vinfo);
	return ret;
}

static int __devexit ste_ff_vibra_remove(struct platform_device *pdev)
{
	struct vibra_info *vinfo = platform_get_drvdata(pdev);

	/*
	 * Function device_release() will call input_dev_release()
	 * which will free ff and input device. No need to call
	 * input_ff_destroy() and input_free_device() explicitly.
	 */
	input_unregister_device(vinfo->idev);
	kfree(vinfo);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ste_ff_vibra_driver = {
	.driver = {
		.name = "ste_ff_vibra",
		.owner = THIS_MODULE,
	},
	.probe	= ste_ff_vibra_probe,
	.remove	= __devexit_p(ste_ff_vibra_remove)
};

static int __init ste_ff_vibra_init(void)
{
	return platform_driver_register(&ste_ff_vibra_driver);
}
module_init(ste_ff_vibra_init);

static void __exit ste_ff_vibra_exit(void)
{
	platform_driver_unregister(&ste_ff_vibra_driver);
}
module_exit(ste_ff_vibra_exit);

MODULE_AUTHOR("Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com>");
MODULE_DESCRIPTION("STE Force Feedback Vibrator Driver");
MODULE_LICENSE("GPL v2");
