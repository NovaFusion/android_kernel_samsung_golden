/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab5500.h>

#define AB5500_RTC_CLOCK_RATE	32768
#define AB5500_RTC		0x00
#define AB5500_RTC_ALARM	(1 << 1)
#define AB5500_READREQ		0x01
#define AB5500_READREQ_REQ	0x01
#define AB5500_AL0		0x02
#define AB5500_TI0		0x06

/**
 * struct ab_rtc - variant specific data
 * @irqname: optional name for the alarm interrupt resource
 * @epoch: epoch to adjust year to
 * @bank: AB bank where this block is present
 * @rtc: address of the "RTC" (control) register
 * @rtc_alarmon: mask of the alarm enable bit in the above register
 * @ti0: address of the TI0 register.  The rest of the TI
 * registers are assumed to contiguously follow this one.
 * @nr_ti: number of TI* registers
 * @al0: address of the AL0 register.  The rest of the
 * AL registers are assumed to contiguously follow this one.
 * @nr_al: number of AL* registers
 * @startup: optional function to initialize the RTC
 * @alarm_to_regs: function to convert alarm time in seconds
 * to a list of AL register values
 * @time_to_regs: function to convert alarm time in seconds
 * to a list of TI register values
 * @regs_to_alarm: function to convert a list of AL register
 * values to the alarm time in seconds
 * @regs_to_time: function to convert a list of TI register
 * values to the alarm time in seconds
 * @request_read: optional function to request a read from the TI* registers
 * @request_write: optional function to request a write to the TI* registers
 */
struct ab_rtc {
	const char *irqname;
	unsigned int epoch;

	u8 bank;
	u8 rtc;
	u8 rtc_alarmon;
	u8 ti0;
	int nr_ti;
	u8 al0;
	int nr_al;

	int (*startup)(struct device *dev);
	void (*alarm_to_regs)(struct device *dev, unsigned long secs, u8 *regs);
	void (*time_to_regs)(struct device *dev, unsigned long secs, u8 *regs);
	unsigned long (*regs_to_alarm)(struct device *dev, u8 *regs);
	unsigned long (*regs_to_time)(struct device *dev, u8 *regs);
	int (*request_read)(struct device *dev);
	int (*request_write)(struct device *dev);
};

static const struct ab_rtc *to_ab_rtc(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	return (struct ab_rtc *)pdev->id_entry->driver_data;
}

/* Calculate the number of seconds since year, for epoch adjustment */
static unsigned long ab_rtc_get_elapsed_seconds(unsigned int year)
{
	unsigned long secs;
	struct rtc_time tm = {
		.tm_year = year - 1900,
		.tm_mday = 1,
	};

	rtc_tm_to_time(&tm, &secs);

	return secs;
}

static int ab5500_rtc_request_read(struct device *dev)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);
	unsigned long timeout;
	int err;

	err = abx500_set_register_interruptible(dev, variant->bank,
						AB5500_READREQ,
						AB5500_READREQ_REQ);
	if (err < 0)
		return err;

	timeout = jiffies + HZ;
	while (time_before(jiffies, timeout)) {
		u8 value;

		err = abx500_get_register_interruptible(dev, variant->bank,
				AB5500_READREQ, &value);
		if (err < 0)
			return err;

		if (!(value & AB5500_READREQ_REQ))
			return 0;

		usleep_range(1000, 2000);
	}

	return -EIO;
}

static void
ab5500_rtc_time_to_regs(struct device *dev, unsigned long secs, u8 *regs)
{
	unsigned long mins = secs / 60;
	u64 fat_time;

	secs %= 60;

	fat_time = secs * AB5500_RTC_CLOCK_RATE;
	fat_time |= (u64)mins << 21;

	regs[0] = (fat_time) & 0xFF;
	regs[1] = (fat_time >> 8) & 0xFF;
	regs[2] = (fat_time >> 16) & 0xFF;
	regs[3] = (fat_time >> 24) & 0xFF;
	regs[4] = (fat_time >> 32) & 0xFF;
	regs[5] = (fat_time >> 40) & 0xFF;
}

static unsigned long
ab5500_rtc_regs_to_time(struct device *dev, u8 *regs)
{
	u64 fat_time = ((u64)regs[5] << 40) | ((u64)regs[4] << 32) |
		       ((u64)regs[3] << 24) | ((u64)regs[2] << 16) |
		       ((u64)regs[1] << 8)  | regs[0];
	unsigned long secs = (fat_time & 0x1fffff) / AB5500_RTC_CLOCK_RATE;
	unsigned long mins = fat_time >> 21;

	return mins * 60 + secs;
}

static void
ab5500_rtc_alarm_to_regs(struct device *dev, unsigned long secs, u8 *regs)
{
	unsigned long mins = secs / 60;

#ifdef CONFIG_ANDROID
	/*
	 * Needed because Android believes all hw have a wake-up resolution in
	 * seconds.
	 */
	mins++;
#endif

	regs[0] = mins & 0xFF;
	regs[1] = (mins >> 8) & 0xFF;
	regs[2] = (mins >> 16) & 0xFF;
}

static unsigned long
ab5500_rtc_regs_to_alarm(struct device *dev, u8 *regs)
{
	unsigned long mins = ((unsigned long)regs[2] << 16) |
			     ((unsigned long)regs[1] << 8) |
			     regs[0];
	unsigned long secs = mins * 60;

	return secs;
}

static const struct ab_rtc ab5500_rtc = {
	.irqname	= "RTC_Alarm",
	.bank		= AB5500_BANK_RTC,
	.rtc		= AB5500_RTC,
	.rtc_alarmon	= AB5500_RTC_ALARM,
	.ti0		= AB5500_TI0,
	.nr_ti		= 6,
	.al0		= AB5500_AL0,
	.nr_al		= 3,
	.epoch		= 2000,
	.time_to_regs	= ab5500_rtc_time_to_regs,
	.regs_to_time	= ab5500_rtc_regs_to_time,
	.alarm_to_regs	= ab5500_rtc_alarm_to_regs,
	.regs_to_alarm	= ab5500_rtc_regs_to_alarm,
	.request_read	= ab5500_rtc_request_read,
};

static int ab_rtc_request_read(struct device *dev)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);

	if (!variant->request_read)
		return 0;

	return variant->request_read(dev);
}

static int ab_rtc_request_write(struct device *dev)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);

	if (!variant->request_write)
		return 0;

	return variant->request_write(dev);
}

static bool ab_rtc_valid_time(struct device *dev, struct rtc_time *time)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);

	if (!variant->epoch)
		return true;

	return time->tm_year >= variant->epoch - 1900;
}

static int
ab_rtc_tm_to_time(struct device *dev, struct rtc_time *tm, unsigned long *secs)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);

	rtc_tm_to_time(tm, secs);

	if (variant->epoch)
		*secs -= ab_rtc_get_elapsed_seconds(variant->epoch);

	return 0;
}

static int
ab_rtc_time_to_tm(struct device *dev, unsigned long secs, struct rtc_time *tm)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);

	if (variant->epoch)
		secs += ab_rtc_get_elapsed_seconds(variant->epoch);

	rtc_time_to_tm(secs, tm);

	return 0;
}

static int ab_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);
	unsigned char buf[variant->nr_ti];
	unsigned long secs;
	int err;

	err = ab_rtc_request_read(dev);
	if (err)
		return err;

	err = abx500_get_register_page_interruptible(dev, variant->bank,
						     variant->ti0,
						     buf, variant->nr_ti);
	if (err)
		return err;

	secs = variant->regs_to_time(dev, buf);
	ab_rtc_time_to_tm(dev, secs, tm);

	return rtc_valid_tm(tm);
}

static int ab_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);
	unsigned char buf[variant->nr_ti];
	unsigned long secs;
	u8 reg = variant->ti0;
	int err;
	int i;

	if (!ab_rtc_valid_time(dev, tm))
		return -EINVAL;

	ab_rtc_tm_to_time(dev, tm, &secs);
	variant->time_to_regs(dev, secs, buf);

	for (i = 0; i < variant->nr_ti; i++, reg++) {
		err = abx500_set_register_interruptible(dev, variant->bank,
							reg, buf[i]);
		if (err)
			return err;
	}

	return ab_rtc_request_write(dev);
}

static int ab_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);
	unsigned long secs;
	u8 buf[variant->nr_al];
	u8 rtcval;
	int err;

	err = abx500_get_register_interruptible(dev, variant->bank,
						variant->rtc, &rtcval);
	if (err)
		return err;

	alarm->enabled = !!(rtcval & variant->rtc_alarmon);
	alarm->pending = 0;

	err = abx500_get_register_page_interruptible(dev, variant->bank,
						     variant->al0, buf,
						     variant->nr_al);
	if (err)
		return err;

	secs = variant->regs_to_alarm(dev, buf);
	ab_rtc_time_to_tm(dev, secs, &alarm->time);

	return rtc_valid_tm(&alarm->time);
}

static int ab_rtc_alarm_enable(struct device *dev, unsigned int enabled)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);
	u8 mask = variant->rtc_alarmon;
	u8 value = enabled ? mask : 0;

	return abx500_mask_and_set_register_interruptible(dev, variant->bank,
							  variant->rtc, mask,
							  value);
}

static int ab_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	const struct ab_rtc *variant = to_ab_rtc(dev);
	unsigned char buf[variant->nr_al];
	unsigned long secs;
	u8 reg = variant->al0;
	int err;
	int i;

	if (!ab_rtc_valid_time(dev, &alarm->time))
		return -EINVAL;

	ab_rtc_tm_to_time(dev, &alarm->time, &secs);
	variant->alarm_to_regs(dev, secs, buf);

	/*
	 * Disable alarm first.  Otherwise the RTC may not detect an alarm
	 * reprogrammed for the same time without disabling the alarm in
	 * between the programmings.
	 */
	err = ab_rtc_alarm_enable(dev, false);
	if (err)
		return err;

	for (i = 0; i < variant->nr_al; i++, reg++) {
		err = abx500_set_register_interruptible(dev, variant->bank,
							reg, buf[i]);
		if (err)
			return err;
	}

	return alarm->enabled ? ab_rtc_alarm_enable(dev, true) : 0;
}

static const struct rtc_class_ops ab_rtc_ops = {
	.read_time		= ab_rtc_read_time,
	.set_time		= ab_rtc_set_time,
	.read_alarm		= ab_rtc_read_alarm,
	.set_alarm		= ab_rtc_set_alarm,
	.alarm_irq_enable	= ab_rtc_alarm_enable,
};

static irqreturn_t ab_rtc_irq(int irq, void *dev_id)
{
	unsigned long events = RTC_IRQF | RTC_AF;
	struct rtc_device *rtc = dev_id;

	rtc_update_irq(rtc, 1, events);

	return IRQ_HANDLED;
}

static int __devinit ab_rtc_probe(struct platform_device *pdev)
{
	const struct ab_rtc *variant = to_ab_rtc(&pdev->dev);
	int err;
	struct rtc_device *rtc;
	int irq = -ENXIO;

	if (variant->irqname) {
		irq = platform_get_irq_byname(pdev, variant->irqname);
		if (irq < 0)
			return irq;
	}

	if (variant->startup) {
		err = variant->startup(&pdev->dev);
		if (err)
			return err;
	}

	device_init_wakeup(&pdev->dev, true);

	rtc = rtc_device_register("ab8500-rtc", &pdev->dev, &ab_rtc_ops,
			THIS_MODULE);
	if (IS_ERR(rtc)) {
		dev_err(&pdev->dev, "Registration failed\n");
		err = PTR_ERR(rtc);
		return err;
	}

	if (irq >= 0) {
		err = request_any_context_irq(irq, ab_rtc_irq,
					      IRQF_NO_SUSPEND,
					      pdev->id_entry->name,
					      rtc);
		if (err < 0) {
			dev_err(&pdev->dev, "could not get irq: %d\n", err);
			goto out_unregister;
		}
	}

	platform_set_drvdata(pdev, rtc);

	return 0;

out_unregister:
	rtc_device_unregister(rtc);
	return err;
}

static int __devexit ab_rtc_remove(struct platform_device *pdev)
{
	const struct ab_rtc *variant = to_ab_rtc(&pdev->dev);
	struct rtc_device *rtc = platform_get_drvdata(pdev);
	int irq = platform_get_irq_byname(pdev, variant->irqname);

	if (irq >= 0)
		free_irq(irq, rtc);
	rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_device_id ab_rtc_id_table[] = {
	{ "ab5500-rtc", (kernel_ulong_t)&ab5500_rtc, },
	{ },
};
MODULE_DEVICE_TABLE(platform, ab_rtc_id_table);

static struct platform_driver ab_rtc_driver = {
	.driver.name	= "ab-rtc",
	.driver.owner	= THIS_MODULE,
	.id_table	= ab_rtc_id_table,
	.probe		= ab_rtc_probe,
	.remove		= __devexit_p(ab_rtc_remove),
};

static int __init ab_rtc_init(void)
{
	return platform_driver_register(&ab_rtc_driver);
}
module_init(ab_rtc_init);

static void __exit ab_rtc_exit(void)
{
	platform_driver_unregister(&ab_rtc_driver);
}
module_exit(ab_rtc_exit);

MODULE_AUTHOR("Rabin Vincent <rabin.vincent@stericsson.com>");
MODULE_DESCRIPTION("AB5500 RTC Driver");
MODULE_LICENSE("GPL v2");
