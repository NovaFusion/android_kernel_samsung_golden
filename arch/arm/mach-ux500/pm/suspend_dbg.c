/*
 * Copyright (C) ST-Ericsson SA 2010-2011
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>,
 *	   Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 *
 */
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/wakelock.h>

#include <mach/pm.h>
#include <mach/pm-timer.h>

/* To reach main_wake_lock */
#include "../../../../kernel/power/power.h"

#ifdef CONFIG_UX500_SUSPEND_STANDBY
static u32 sleep_enabled = 1;
#else
static u32 sleep_enabled;
#endif

#ifdef CONFIG_UX500_SUSPEND_MEM
static u32 deepsleep_enabled = 1;
#else
static u32 deepsleep_enabled;
#endif

static u32 suspend_enabled = 1;

static u32 deepsleeps_done;
static u32 deepsleeps_failed;
static u32 sleeps_done;
static u32 sleeps_failed;
static u32 suspend_count;

#ifdef CONFIG_UX500_SUSPEND_DBG_WAKE_ON_UART
void ux500_suspend_dbg_add_wake_on_uart(void)
{
	irq_set_irq_wake(GPIO_TO_IRQ(ux500_console_uart_gpio_pin), 1);
	irq_set_irq_type(GPIO_TO_IRQ(ux500_console_uart_gpio_pin),
			 IRQ_TYPE_EDGE_BOTH);
}

void ux500_suspend_dbg_remove_wake_on_uart(void)
{
	irq_set_irq_wake(GPIO_TO_IRQ(ux500_console_uart_gpio_pin), 0);
}
#endif

bool ux500_suspend_enabled(void)
{
	return suspend_enabled != 0;
}

bool ux500_suspend_sleep_enabled(void)
{
	return sleep_enabled != 0;
}

bool ux500_suspend_deepsleep_enabled(void)
{
	return deepsleep_enabled != 0;
}

void ux500_suspend_dbg_sleep_status(bool is_deepsleep)
{
	enum prcmu_power_status prcmu_status;

	prcmu_status = prcmu_get_power_state_result();

	if (is_deepsleep) {
		pr_info("Returning from ApDeepSleep. PRCMU ret: 0x%x - %s\n",
			prcmu_status,
			prcmu_status == PRCMU_DEEP_SLEEP_OK ?
			"Success" : "Fail!");
		if (prcmu_status == PRCMU_DEEP_SLEEP_OK)
			deepsleeps_done++;
		else
			deepsleeps_failed++;
	} else {
		pr_info("Returning from ApSleep. PRCMU ret: 0x%x - %s\n",
			prcmu_status,
			prcmu_status == PRCMU_SLEEP_OK ? "Success" : "Fail!");
		if (prcmu_status == PRCMU_SLEEP_OK)
			sleeps_done++;
		else
			sleeps_failed++;
	}
}

int ux500_suspend_dbg_begin(suspend_state_t state)
{
	suspend_count++;
	return 0;
}

/* The number of failed suspend attempts in a row before giving up */
#define TEST_FAILS 10

static int suspend_test_count;
static int suspend_test_current;
static int suspend_test_fail_count;

void ux500_suspend_dbg_test_set_wakeup(void)
{
	if (suspend_test_count == 0)
		return;

	ux500_rtcrtt_off();

	/* Make sure the rtc writes have been accepted */
	udelay(120);

	/* Program RTC to generate an interrupt 1s later */
	ux500_rtcrtt_next(1000000);
}

void ux500_suspend_dbg_test_start(int num)
{
	suspend_test_count = num;
	suspend_test_current = deepsleeps_done;
	suspend_test_fail_count = 0;
}

bool ux500_suspend_test_success(bool *ongoing)
{
	(*ongoing) = ((suspend_test_fail_count < TEST_FAILS) &&
		      (suspend_test_count > 0));
	return suspend_test_fail_count < TEST_FAILS;
}

void ux500_suspend_dbg_end(void)
{
	static int attempts;

	if (suspend_test_count > 0) {
		attempts++;
		pr_info("Suspend test: %d done\n", attempts);
		suspend_test_count--;
#ifdef CONFIG_WAKELOCK
		wake_lock(&main_wake_lock);
#endif

		if (suspend_test_current < deepsleeps_done) {
			suspend_test_current = deepsleeps_done;
			suspend_test_fail_count = 0;
		} else {
			suspend_test_fail_count++;
		}

		if (suspend_test_fail_count > TEST_FAILS) {
			suspend_test_count = 0;
			pr_err("suspend: Aborting after %d "
			       "failed suspend in a row\n",
			       TEST_FAILS);
		} else if (suspend_test_count > 0) {
			msleep(100);
#ifdef CONFIG_WAKELOCK
			wake_unlock(&main_wake_lock);
#endif
		}

		if (suspend_test_count == 0)
			attempts = 0;
	}
}

void ux500_suspend_dbg_init(void)
{
	struct dentry *suspend_dir;
	struct dentry *file;

	suspend_dir = debugfs_create_dir("suspend", NULL);
	if (IS_ERR_OR_NULL(suspend_dir))
		return;

	file = debugfs_create_bool("sleep", S_IWUGO | S_IRUGO,
				   suspend_dir,
				   &sleep_enabled);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_bool("deepsleep", S_IWUGO | S_IRUGO,
				   suspend_dir,
				   &deepsleep_enabled);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_bool("enable", S_IWUGO | S_IRUGO,
				   suspend_dir,
				   &suspend_enabled);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_u32("count", S_IRUGO,
				  suspend_dir,
				  &suspend_count);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_u32("sleep_count", S_IRUGO,
				  suspend_dir,
				  &sleeps_done);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_u32("deepsleep_count", S_IRUGO,
				  suspend_dir,
				  &deepsleeps_done);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_u32("sleep_failed", S_IRUGO,
				  suspend_dir,
				  &sleeps_failed);
	if (IS_ERR_OR_NULL(file))
		goto error;

	file = debugfs_create_u32("deepsleep_failed", S_IRUGO,
				  suspend_dir,
				  &deepsleeps_failed);
	if (IS_ERR_OR_NULL(file))
		goto error;

	return;
error:
	if (!IS_ERR_OR_NULL(suspend_dir))
		debugfs_remove_recursive(suspend_dir);
}
