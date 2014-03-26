/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com>
 *	   for ST-Ericsson
 * License Terms: GNU General Public License v2
 */

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/ste_timed_vibra.h>
#include <linux/delay.h>
#include "timed_output.h"

/**
 * struct vibra_info - Vibrator information structure
 * @tdev:		Pointer to timed output device structure
 * @linear_workqueue:   Pointer to linear vibrator workqueue structure
 * @linear_work:	Linear Vibrator work
 * @linear_tick:	Linear Vibrator high resolution timer
 * @vibra_workqueue:    Pointer to vibrator workqueue structure
 * @vibra_work:		Vibrator work
 * @vibra_timer:	Vibrator high resolution timer
 * @vibra_lock:		Vibrator lock
 * @vibra_state:	Actual vibrator state
 * @state_force:	Indicates if oppositive state is requested
 * @timeout:		Indicates how long time the vibrator will be enabled
 * @time_passed:	Total time passed in states
 * @pdata:		Local pointer to platform data with vibrator parameters
 *
 * Structure vibra_info holds vibrator information
 **/
struct vibra_info {
	struct timed_output_dev			tdev;
	struct workqueue_struct			*linear_workqueue;
	struct work_struct			linear_work;
	struct hrtimer				linear_tick;
	struct workqueue_struct			*vibra_workqueue;
	struct work_struct			vibra_work;
	struct hrtimer				vibra_timer;
	spinlock_t				vibra_lock;
	enum ste_timed_vibra_states		vibra_state;
	bool					state_force;
	unsigned int				timeout;
	unsigned int				time_passed;
	struct ste_timed_vibra_platform_data	*pdata;
};

/*
 * Linear vibrator hardware operates on a particular resonance
 * frequency. The resonance frequency (f) may also vary with h/w.
 * This define is half time period (t) in micro seconds (us).
 * For resonance frequency f = 150 Hz
 * t = T/2 = ((1/150) / 2) = 3333 usec.
 */
#define LINEAR_RESONANCE	3333

/**
 * linear_vibra_work() - Linear Vibrator work, turns on/off vibrator
 * @work:	Pointer to work structure
 *
 * This function is called from workqueue, turns on/off vibrator
 **/
static void linear_vibra_work(struct work_struct *work)
{
	struct vibra_info *vinfo =
			container_of(work, struct vibra_info, linear_work);
	unsigned char speed_pos = 0, speed_neg = 0;
	ktime_t ktime;
	static unsigned char toggle;

	if (toggle) {
		speed_pos = vinfo->pdata->boost_level;
		speed_neg = 0;
	} else {
		speed_neg = vinfo->pdata->boost_level;
		speed_pos = 0;
	}

	toggle = !toggle;
	vinfo->pdata->timed_vibra_control(speed_pos, speed_neg,
			speed_pos, speed_neg);

	if ((vinfo->vibra_state != STE_VIBRA_IDLE) &&
		(vinfo->vibra_state != STE_VIBRA_OFF)) {
		ktime = ktime_set((LINEAR_RESONANCE / USEC_PER_SEC),
			(LINEAR_RESONANCE % USEC_PER_SEC) * NSEC_PER_USEC);
		hrtimer_start(&vinfo->linear_tick, ktime, HRTIMER_MODE_REL);
	}
}

/**
 * vibra_control_work() - Vibrator work, turns on/off vibrator
 * @work:	Pointer to work structure
 *
 * This function is called from workqueue, turns on/off vibrator
 **/
static void vibra_control_work(struct work_struct *work)
{
	struct vibra_info *vinfo =
			container_of(work, struct vibra_info, vibra_work);
	unsigned val = 0;
	unsigned char speed_pos = 0, speed_neg = 0;
	unsigned long flags;

	/*
	 * Cancel scheduled timer if it has not started
	 * else it will wait for timer callback to complete.
	 * It should be done before taking vibra_lock to
	 * prevent race condition, as timer callback also
	 * takes same lock.
	 */
	hrtimer_cancel(&vinfo->vibra_timer);

	spin_lock_irqsave(&vinfo->vibra_lock, flags);

	switch (vinfo->vibra_state) {
	case STE_VIBRA_BOOST:
		/* Turn on both vibrators with boost speed */
		speed_pos = vinfo->pdata->boost_level;
		val = vinfo->pdata->boost_time;
		break;
	case STE_VIBRA_ON:
		/* Turn on both vibrators with speed */
		speed_pos = vinfo->pdata->on_level;
		val = vinfo->timeout - vinfo->pdata->boost_time;
		break;
	case STE_VIBRA_OFF:
		/* Turn on both vibrators with reversed speed */
		speed_neg = vinfo->pdata->off_level;
		val = vinfo->pdata->off_time;
		break;
	case STE_VIBRA_IDLE:
		vinfo->time_passed = 0;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&vinfo->vibra_lock, flags);

	/* Send new settings (only for rotary vibrators) */
	if (!vinfo->pdata->is_linear_vibra)
		vinfo->pdata->timed_vibra_control(speed_pos, speed_neg,
				speed_pos, speed_neg);

	if (vinfo->vibra_state != STE_VIBRA_IDLE) {
		/* Start timer if it's not in IDLE state */
		ktime_t ktime;
		ktime = ktime_set((val / MSEC_PER_SEC),
				(val % MSEC_PER_SEC) * NSEC_PER_MSEC),
		hrtimer_start(&vinfo->vibra_timer, ktime, HRTIMER_MODE_REL);
	} else if (vinfo->pdata->is_linear_vibra) {
		/* Cancel work and timers of linear vibrator in IDLE state */
		hrtimer_cancel(&vinfo->linear_tick);
		flush_workqueue(vinfo->linear_workqueue);
		vinfo->pdata->timed_vibra_control(0, 0, 0, 0);
	}
}

/**
 * vibra_enable() - Enables vibrator
 * @tdev:      Pointer to timed output device structure
 * @timeout:	Time indicating how long vibrator will be enabled
 *
 * This function enables vibrator
 **/
static void vibra_enable(struct timed_output_dev *tdev, int timeout)
{
	struct vibra_info *vinfo = dev_get_drvdata(tdev->dev);
	unsigned long flags;

	spin_lock_irqsave(&vinfo->vibra_lock, flags);
	switch (vinfo->vibra_state) {
	case STE_VIBRA_IDLE:
		if (timeout)
			vinfo->vibra_state = STE_VIBRA_BOOST;
		else    /* Already disabled */
			break;

		vinfo->state_force = false;
		/* Trim timeout */
		vinfo->timeout = timeout < vinfo->pdata->boost_time ?
				vinfo->pdata->boost_time : timeout;

		if (vinfo->pdata->is_linear_vibra)
			queue_work(vinfo->linear_workqueue,
					&vinfo->linear_work);
		queue_work(vinfo->vibra_workqueue, &vinfo->vibra_work);
		break;
	case STE_VIBRA_BOOST:
		/* Force only when user requested OFF while BOOST */
		if (!timeout)
			vinfo->state_force = true;
		break;
	case STE_VIBRA_ON:
		/* If user requested OFF */
		if (!timeout) {
			if (vinfo->pdata->is_linear_vibra)
				hrtimer_cancel(&vinfo->linear_tick);
			/* Cancel timer if it has not expired yet.
			 * Else setting the vibra_state to STE_VIBRA_OFF
			 * will make take care that vibrator will move to
			 * STE_VIBRA_IDLE in timer callback just after
			 * this function call.
			 */
			hrtimer_try_to_cancel(&vinfo->vibra_timer);
			vinfo->vibra_state = STE_VIBRA_OFF;
			queue_work(vinfo->vibra_workqueue, &vinfo->vibra_work);
		}
		break;
	case STE_VIBRA_OFF:
		/* Force only when user requested ON while OFF */
		if (timeout)
			vinfo->state_force = true;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&vinfo->vibra_lock, flags);
}

/**
 * linear_vibra_tick() - Generate resonance frequency waveform
 * @hrtimer: Pointer to high resolution timer structure
 *
 * This function helps in generating the resonance frequency
 * waveform required for linear vibrators
 *
 * Returns:
 *	Returns value which indicates whether hrtimer should be restarted
 **/
static enum hrtimer_restart linear_vibra_tick(struct hrtimer *hrtimer)
{
	struct vibra_info *vinfo =
			container_of(hrtimer, struct vibra_info, linear_tick);

	if ((vinfo->vibra_state != STE_VIBRA_IDLE) &&
		(vinfo->vibra_state != STE_VIBRA_OFF)) {
		queue_work(vinfo->linear_workqueue, &vinfo->linear_work);
	}

	return HRTIMER_NORESTART;
}

/**
 * vibra_timer_expired() - Handles vibrator machine state
 * @hrtimer:      Pointer to high resolution timer structure
 *
 * This function handles vibrator machine state
 *
 * Returns:
 *	Returns value which indicates wether hrtimer should be restarted
 **/
static enum hrtimer_restart vibra_timer_expired(struct hrtimer *hrtimer)
{
	struct vibra_info *vinfo =
			container_of(hrtimer, struct vibra_info, vibra_timer);
	unsigned long flags;

	spin_lock_irqsave(&vinfo->vibra_lock, flags);
	switch (vinfo->vibra_state) {
	case STE_VIBRA_BOOST:
		/* If BOOST finished and force, go to OFF */
		if (vinfo->state_force)
			vinfo->vibra_state = STE_VIBRA_OFF;
		else
			vinfo->vibra_state = STE_VIBRA_ON;
		vinfo->time_passed = vinfo->pdata->boost_time;
		break;
	case STE_VIBRA_ON:
		vinfo->vibra_state = STE_VIBRA_OFF;
		vinfo->time_passed = vinfo->timeout;
		break;
	case STE_VIBRA_OFF:
		/* If OFF finished and force, go to ON */
		if (vinfo->state_force)
			vinfo->vibra_state = STE_VIBRA_ON;
		else
			vinfo->vibra_state = STE_VIBRA_IDLE;
		vinfo->time_passed += vinfo->pdata->off_time;
		break;
	case STE_VIBRA_IDLE:
		break;
	default:
		break;
	}
	vinfo->state_force = false;
	spin_unlock_irqrestore(&vinfo->vibra_lock, flags);

	queue_work(vinfo->vibra_workqueue, &vinfo->vibra_work);

	return HRTIMER_NORESTART;
}

/**
 * vibra_get_time() - Returns remaining time to disabling vibration
 * @tdev:      Pointer to timed output device structure
 *
 * This function returns time remaining to disabling vibration
 *
 * Returns:
 *	Returns remaining time to disabling vibration
 **/
static int vibra_get_time(struct timed_output_dev *tdev)
{
	struct vibra_info *vinfo = dev_get_drvdata(tdev->dev);
	u32 ms;

	if (hrtimer_active(&vinfo->vibra_timer)) {
		ktime_t remain = hrtimer_get_remaining(&vinfo->vibra_timer);
		ms = (u32) ktime_to_ms(remain);
		return ms + vinfo->time_passed;
	} else
		return 0;
}

static int __devinit ste_timed_vibra_probe(struct platform_device *pdev)
{
	int ret;
	struct vibra_info *vinfo;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		return -ENODEV;
	}

	vinfo = kmalloc(sizeof *vinfo, GFP_KERNEL);
	if (!vinfo) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	vinfo->tdev.name = "vibrator";
	vinfo->tdev.enable = vibra_enable;
	vinfo->tdev.get_time = vibra_get_time;
	vinfo->time_passed = 0;
	vinfo->vibra_state = STE_VIBRA_IDLE;
	vinfo->state_force = false;
	vinfo->pdata = pdev->dev.platform_data;

	if (vinfo->pdata->is_linear_vibra)
		dev_info(&pdev->dev, "Linear Type Vibrators\n");
	else
		dev_info(&pdev->dev, "Rotary Type Vibrators\n");

	ret = timed_output_dev_register(&vinfo->tdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register timed output device\n");
		goto exit_free_vinfo;
	}

	dev_set_drvdata(vinfo->tdev.dev, vinfo);

	vinfo->linear_workqueue =
		create_singlethread_workqueue("ste-timed-linear-vibra");
	if (!vinfo->linear_workqueue) {
		dev_err(&pdev->dev, "failed to allocate workqueue\n");
		ret = -ENOMEM;
		goto exit_timed_output_unregister;
	}

	/* Create workqueue just for timed output vibrator */
	vinfo->vibra_workqueue =
		create_singlethread_workqueue("ste-timed-output-vibra");
	if (!vinfo->vibra_workqueue) {
		dev_err(&pdev->dev, "failed to allocate workqueue\n");
		ret = -ENOMEM;
		goto exit_destroy_workqueue;
	}

	INIT_WORK(&vinfo->linear_work, linear_vibra_work);
	INIT_WORK(&vinfo->vibra_work, vibra_control_work);
	spin_lock_init(&vinfo->vibra_lock);
	hrtimer_init(&vinfo->linear_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_init(&vinfo->vibra_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vinfo->linear_tick.function = linear_vibra_tick;
	vinfo->vibra_timer.function = vibra_timer_expired;

	platform_set_drvdata(pdev, vinfo);
	return 0;

exit_destroy_workqueue:
	destroy_workqueue(vinfo->linear_workqueue);
exit_timed_output_unregister:
	timed_output_dev_unregister(&vinfo->tdev);
exit_free_vinfo:
	kfree(vinfo);
	return ret;
}

static int __devexit ste_timed_vibra_remove(struct platform_device *pdev)
{
	struct vibra_info *vinfo = platform_get_drvdata(pdev);

	timed_output_dev_unregister(&vinfo->tdev);
	destroy_workqueue(vinfo->linear_workqueue);
	destroy_workqueue(vinfo->vibra_workqueue);
	kfree(vinfo);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ste_timed_vibra_driver = {
	.driver = {
		.name = "ste_timed_output_vibra",
		.owner = THIS_MODULE,
	},
	.probe  = ste_timed_vibra_probe,
	.remove = __devexit_p(ste_timed_vibra_remove)
};

static int __init ste_timed_vibra_init(void)
{
	return platform_driver_register(&ste_timed_vibra_driver);
}
module_init(ste_timed_vibra_init);

static void __exit ste_timed_vibra_exit(void)
{
	platform_driver_unregister(&ste_timed_vibra_driver);
}
module_exit(ste_timed_vibra_exit);

MODULE_AUTHOR("Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com>");
MODULE_DESCRIPTION("STE Timed Output Vibrator");
MODULE_LICENSE("GPL v2");
