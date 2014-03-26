/*
 * Copyright (C) ST-Ericsson SA 2010-2012
 *
 * Author: Mattias Wallin <mattias.wallin@stericsson.com> for ST-Ericsson
 *
 * License Terms: GNU General Public License v2
 *
 */
#include <linux/kernel.h>
#include <linux/rtc.h>

/* Add a ux500 specific hook from rtc/hctosys.c */
void rtc_hctohc(struct rtc_time tm) {
	/*
	 * RTC_ALARM_DEV_NAME is not the same as rtc device name: rtc0 and rtc1
	 * The rtc numbering can not be configured instead it
	 * will be decided at boot time so for u8500
	 * rtc-ab8500 will get rtc0 becase it is an platform device and
	 * rtc-pl031 will get rtc1 because it is an amba device.
	 * hctosys device is default rtc0
	 * */
	struct rtc_device *rtc = rtc_class_open("rtc1");

	if (rtc == NULL) {
		pr_warn("%s: unable to open 2nd rtc device (%s)\n",
			__FILE__, "rtc1");
		return;
	}

	if (rtc_set_time(rtc, &tm)) {
		dev_err(rtc->dev.parent, ": unable to set the 2nd rtc\n");
		return;
	}

	dev_dbg(rtc->dev.parent, "setting rtc1 to "
		"%d-%02d-%02d %02d:%02d:%02d UTC\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
}
