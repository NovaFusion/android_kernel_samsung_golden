/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License, version 2
 * Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <mach/db5500-keypad.h>
#include <linux/regulator/consumer.h>

#define KEYPAD_CTR		0x0
#define KEYPAD_IRQ_CLEAR	0x4
#define KEYPAD_INT_ENABLE	0x8
#define KEYPAD_INT_STATUS	0xC
#define KEYPAD_ARRAY_01		0x18

#define KEYPAD_NUM_ARRAY_REGS	5

#define KEYPAD_CTR_WRITE_IRQ_ENABLE	(1 << 10)
#define KEYPAD_CTR_WRITE_CONTROL	(1 << 8)
#define KEYPAD_CTR_SCAN_ENABLE		(1 << 7)

#define KEYPAD_ARRAY_CHANGEBIT		(1 << 15)

#define KEYPAD_DEBOUNCE_PERIOD_MIN	5	/* ms */
#define KEYPAD_DEBOUNCE_PERIOD_MAX	80	/* ms */

#define KEYPAD_GND_ROW		8

#define KEYPAD_ROW_SHIFT	3
#define KEYPAD_KEYMAP_SIZE	\
	(KEYPAD_MAX_ROWS * KEYPAD_MAX_COLS)

#define	KEY_PRESSED_DELAY	10
/**
 * struct db5500_keypad  - data structure used by keypad driver
 * @irq:	irq number
 * @base:	keypad registers base address
 * @input:	pointer to input device object
 * @board:	keypad platform data
 * @keymap:	matrix scan code table for keycodes
 * @clk:	clock structure pointer
 * @regulator : regulator used by keypad
 * @switch_work : delayed work variable for switching to gpio
 * @gpio_work : delayed work variable for reporting key event in gpio mode
 * @previous_set: previous set of registers
 * @enable : flag to enable the driver event
 * @enable_on_resume:   set if keypad should be enabled on resume
 * @valid_key : hold the state of valid key press
 * @db5500_rows : rows gpio array for db5500 keypad
 * @db5500_cols : cols gpio array for db5500 keypad
 * @gpio_input_irq : array for gpio irqs
 * @gpio_row : gpio row
 * @gpio_col : gpio_col
 */
struct db5500_keypad {
	int irq;
	void __iomem *base;
	struct input_dev *input;
	const struct db5500_keypad_platform_data *board;
	unsigned short keymap[KEYPAD_KEYMAP_SIZE];
	struct clk *clk;
	struct regulator *regulator;
	struct delayed_work switch_work;
	struct delayed_work gpio_work;
	u8 previous_set[KEYPAD_MAX_ROWS];
	bool enable;
	bool enable_on_resume;
	bool valid_key;
	int db5500_rows[KEYPAD_MAX_ROWS];
	int db5500_cols[KEYPAD_MAX_COLS];
	int gpio_input_irq[KEYPAD_MAX_ROWS];
	int gpio_row;
	int gpio_col;
};

/**
 * db5500_keypad_report() - reports the keypad event
 * @keypad: pointer to device structure
 * @row: row value of keypad
 * @curr: current event
 * @previous: previous event
 *
 * This function uses to reports the event of the keypad
 * and returns NONE.
 *
 * By default all column reads are 1111 1111b.  Any press will pull the column
 * down, leading to a 0 in any of these locations.  We invert these values so
 * that a 1 means means "column pressed". *
 * If curr changes from the previous from 0 to 1, we report it as a key press.
 * If curr changes from the previous from 1 to 0, we report it as a key
 * release.
 */
static void db5500_keypad_report(struct db5500_keypad *keypad, int row,
				 u8 curr, u8 previous)
{
	struct input_dev *input = keypad->input;
	u8 changed  = curr ^ previous;

	while (changed) {
		int col = __ffs(changed);
		bool press = curr & BIT(col);
		int code = MATRIX_SCAN_CODE(row, col, KEYPAD_ROW_SHIFT);

		input_event(input, EV_MSC, MSC_SCAN, code);
		input_report_key(input, keypad->keymap[code], press);
		input_sync(input);

		changed &= ~BIT(col);
	}
}

static void db5500_keypad_scan(struct db5500_keypad *keypad)
{
	u8 current_set[ARRAY_SIZE(keypad->previous_set)];
	int tries = 100;
	bool changebit;
	u32 data_reg;
	u8 allrows;
	u8 common;
	int i;

	writel(0x1, keypad->base + KEYPAD_IRQ_CLEAR);

again:
	if (!tries--) {
		dev_warn(&keypad->input->dev, "values failed to stabilize\n");
		return;
	}

	changebit = readl(keypad->base + KEYPAD_ARRAY_01)
		    & KEYPAD_ARRAY_CHANGEBIT;

	for (i = 0; i < KEYPAD_NUM_ARRAY_REGS; i++) {
		data_reg = readl(keypad->base + KEYPAD_ARRAY_01 + 4 * i);

		/* If the change bit changed, we need to reread the data */
		if (changebit != !!(data_reg & KEYPAD_ARRAY_CHANGEBIT))
			goto again;

		current_set[2 * i] = ~(data_reg & 0xff);

		/* Last array reg has only one valid set of columns */
		if (i != KEYPAD_NUM_ARRAY_REGS - 1)
			current_set[2 * i + 1] = ~((data_reg & 0xff0000) >> 16);
	}

	allrows = current_set[KEYPAD_GND_ROW];

	/*
	 * Sometimes during a GND row release, an incorrect report is received
	 * where the ARRAY8 all rows setting does not match the other ARRAY*
	 * rows.  Ignore this report; the correct one has been observed to
	 * follow it.
	 */
	common = 0xff;
	for (i = 0; i < KEYPAD_GND_ROW; i++)
		common &= current_set[i];

	if ((allrows & common) != common)
		return;

	for (i = 0; i < ARRAY_SIZE(current_set); i++) {
		/*
		 * If there is an allrows press (GND row), we need to ignore
		 * the allrows values from the reset of the ARRAYs.
		 */
		if (i < KEYPAD_GND_ROW && allrows)
			current_set[i] &= ~allrows;

		if (keypad->previous_set[i] == current_set[i])
			continue;

		db5500_keypad_report(keypad, i, current_set[i],
				     keypad->previous_set[i]);
	}

	/* update the reference set of array registers */
	memcpy(keypad->previous_set, current_set, sizeof(keypad->previous_set));

	return;
}

/**
 * db5500_keypad_writel() - write into keypad registers
 * @keypad: pointer to device structure
 * @val: value to write into register
 * @reg: register offset
 *
 * This function uses to write into the keypad registers
 * and returns NONE.
 */
static void db5500_keypad_writel(struct db5500_keypad *keypad, u32 val, u32 reg)
{
	int timeout = 4;
	int allowedbit;

	switch (reg) {
	case KEYPAD_CTR:
		allowedbit = KEYPAD_CTR_WRITE_CONTROL;
		break;
	case KEYPAD_INT_ENABLE:
		allowedbit = KEYPAD_CTR_WRITE_IRQ_ENABLE;
		break;
	default:
		BUG();
	}

	do {
		u32 ctr = readl(keypad->base + KEYPAD_CTR);

		if (ctr & allowedbit)
			break;

		udelay(50);
	} while (--timeout);

	/* Five 32k clk cycles (~150us) required, we waited 200us */
	WARN_ON(!timeout);

	writel(val, keypad->base + reg);
}

/**
 * db5500_keypad_chip_init() - initialize the keypad chip
 * @keypad: pointer to device structure
 *
 * This function uses to initializes the keypad controller
 * and returns integer.
 */
static int db5500_keypad_chip_init(struct db5500_keypad *keypad)
{
	int debounce = keypad->board->debounce_ms;
	int debounce_hits = 0;

	if (debounce < KEYPAD_DEBOUNCE_PERIOD_MIN)
		debounce = KEYPAD_DEBOUNCE_PERIOD_MIN;

	if (debounce > KEYPAD_DEBOUNCE_PERIOD_MAX) {
		debounce_hits = DIV_ROUND_UP(debounce,
					     KEYPAD_DEBOUNCE_PERIOD_MAX) - 1;
		debounce = KEYPAD_DEBOUNCE_PERIOD_MAX;
	}

	/* Convert the milliseconds to the bit mask */
	debounce = DIV_ROUND_UP(debounce, KEYPAD_DEBOUNCE_PERIOD_MIN) - 1;

	clk_enable(keypad->clk);

	db5500_keypad_writel(keypad,
			     KEYPAD_CTR_SCAN_ENABLE
			     | ((debounce_hits & 0x7) << 4)
			     | debounce,
			     KEYPAD_CTR);

	db5500_keypad_writel(keypad, 0x1, KEYPAD_INT_ENABLE);

	return 0;
}

static void db5500_mode_enable(struct db5500_keypad *keypad, bool enable)
{
	int i;

	if (!enable) {
		db5500_keypad_writel(keypad, 0, KEYPAD_CTR);
		db5500_keypad_writel(keypad, 0, KEYPAD_INT_ENABLE);
		if (keypad->board->exit)
			keypad->board->exit();
		for (i = 0; i < keypad->board->krow; i++) {
			enable_irq(keypad->gpio_input_irq[i]);
			enable_irq_wake(keypad->gpio_input_irq[i]);
		}
		clk_disable(keypad->clk);
		regulator_disable(keypad->regulator);
	} else {
		regulator_enable(keypad->regulator);
		clk_enable(keypad->clk);
		for (i = 0; i < keypad->board->krow; i++) {
			disable_irq_nosync(keypad->gpio_input_irq[i]);
			disable_irq_wake(keypad->gpio_input_irq[i]);
		}
		if (keypad->board->init)
			keypad->board->init();
		db5500_keypad_chip_init(keypad);
	}
}

static void db5500_gpio_switch_work(struct work_struct *work)
{
	struct db5500_keypad *keypad = container_of(work,
					struct db5500_keypad, switch_work.work);

	db5500_mode_enable(keypad, false);
	keypad->enable = false;
}

static void db5500_gpio_release_work(struct work_struct *work)
{
	int code;
	struct db5500_keypad *keypad = container_of(work,
					struct db5500_keypad, gpio_work.work);
	struct input_dev *input = keypad->input;

	code = MATRIX_SCAN_CODE(keypad->gpio_col, keypad->gpio_row,
						KEYPAD_ROW_SHIFT);
	input_event(input, EV_MSC, MSC_SCAN, code);
	input_report_key(input, keypad->keymap[code], 1);
	input_sync(input);
	input_report_key(input, keypad->keymap[code], 0);
	input_sync(input);
}

static int db5500_read_get_gpio_row(struct db5500_keypad *keypad)
{
	int row;
	int value = 0;
	int ret;

	/* read all rows GPIO data register values */
	for (row = 0; row < keypad->board->krow; row++) {
		ret  = gpio_get_value(keypad->db5500_rows[row]);
		value += (1 << row) *  ret;
	}

	/* get the exact row */
	for (row = 0; row < keypad->board->krow; row++) {
		if (((1 << row) & value) == 0)
			return row;
	}

	return -1;
}

static void db5500_set_cols(struct db5500_keypad *keypad, int col)
{
	int i, ret;
	int value;

	/*
	 * Set all columns except the requested column
	 * output pin as high
	 */
	for (i = 0; i < keypad->board->kcol; i++) {
		if (i == col)
			value = 0;
		else
			value = 1;
		ret = gpio_request(keypad->db5500_cols[i], "db5500-kpd");

		if (ret < 0) {
			pr_err("db5500_set_cols: gpio request failed\n");
			continue;
		}

		gpio_direction_output(keypad->db5500_cols[i], value);
		gpio_free(keypad->db5500_cols[i]);
	}
}

static void db5500_free_cols(struct db5500_keypad *keypad)
{
	int i, ret;

	for (i = 0; i < keypad->board->kcol; i++) {
		ret = gpio_request(keypad->db5500_cols[i], "db5500-kpd");

		if (ret < 0) {
			pr_err("db5500_free_cols: gpio request failed\n");
			continue;
		}

		gpio_direction_output(keypad->db5500_cols[i], 0);
		gpio_free(keypad->db5500_cols[i]);
	}
}

static void db5500_manual_scan(struct db5500_keypad *keypad)
{
	int row;
	int col;

	keypad->valid_key = false;

	for (col = 0; col < keypad->board->kcol; col++) {
		db5500_set_cols(keypad, col);
		row = db5500_read_get_gpio_row(keypad);
		if (row >= 0) {
			keypad->valid_key = true;
			keypad->gpio_row = row;
			keypad->gpio_col = col;
			break;
		}
	}
	db5500_free_cols(keypad);
}

static irqreturn_t db5500_keypad_gpio_irq(int irq, void *dev_id)
{
	struct db5500_keypad *keypad = dev_id;

	if (!gpio_get_value(IRQ_TO_GPIO(irq))) {
		db5500_manual_scan(keypad);
		if (!keypad->enable) {
			keypad->enable = true;
			db5500_mode_enable(keypad, true);
		}

		/*
		 * Schedule the work queue to change it to
		 * report the key pressed, if it is not detected in keypad mode.
		 */
		if (keypad->valid_key) {
			schedule_delayed_work(&keypad->gpio_work,
						KEY_PRESSED_DELAY);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t db5500_keypad_irq(int irq, void *dev_id)
{
	struct db5500_keypad *keypad = dev_id;

	cancel_delayed_work_sync(&keypad->gpio_work);
	cancel_delayed_work_sync(&keypad->switch_work);
	db5500_keypad_scan(keypad);

	/*
	 * Schedule the work queue to change it to
	 * GPIO mode, if there is no activity in keypad mode
	 */
	if (keypad->enable)
		schedule_delayed_work(&keypad->switch_work,
				keypad->board->switch_delay);

	return IRQ_HANDLED;
}

/**
 * db5500_keypad_probe() - Initialze the the keypad driver
 * @pdev: pointer to platform device structure
 *
 * This function will allocate and initialize the instance
 * data and request the irq and register to input subsystem driver.
 */
static int __devinit db5500_keypad_probe(struct platform_device *pdev)
{
	struct db5500_keypad_platform_data *plat;
	struct db5500_keypad *keypad;
	struct resource *res;
	struct input_dev *input;
	void __iomem *base;
	struct clk *clk;
	int ret;
	int irq;
	int i;

	plat = pdev->dev.platform_data;
	if (!plat) {
		dev_err(&pdev->dev, "invalid keypad platform data\n");
		ret = -EINVAL;
		goto out_ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keypad irq\n");
		ret = -EINVAL;
		goto out_ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "missing platform resources\n");
		ret = -EINVAL;
		goto out_ret;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		ret = -EBUSY;
		goto out_ret;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		ret = -ENXIO;
		goto out_freerequest_memregions;
	}

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to clk_get\n");
		ret = PTR_ERR(clk);
		goto out_iounmap;
	}

	keypad = kzalloc(sizeof(struct db5500_keypad), GFP_KERNEL);
	if (!keypad) {
		dev_err(&pdev->dev, "failed to allocate keypad memory\n");
		ret = -ENOMEM;
		goto out_freeclk;
	}

	input = input_allocate_device();
	if (!input) {
		dev_err(&pdev->dev, "failed to input_allocate_device\n");
		ret = -ENOMEM;
		goto out_freekeypad;
	}

	keypad->regulator = regulator_get(&pdev->dev, "v-ape");
	if (IS_ERR(keypad->regulator)) {
		dev_err(&pdev->dev, "regulator_get failed\n");
		keypad->regulator = NULL;
		ret = -EINVAL;
		goto out_regulator_get;
	} else {
		ret = regulator_enable(keypad->regulator);
		if (ret < 0) {
			dev_err(&pdev->dev, "regulator_enable failed\n");
			goto out_regulator_enable;
		}
	}

	input->id.bustype = BUS_HOST;
	input->name = "db5500-keypad";
	input->dev.parent = &pdev->dev;

	input->keycode = keypad->keymap;
	input->keycodesize = sizeof(keypad->keymap[0]);
	input->keycodemax = ARRAY_SIZE(keypad->keymap);

	input_set_capability(input, EV_MSC, MSC_SCAN);

	__set_bit(EV_KEY, input->evbit);
	if (!plat->no_autorepeat)
		__set_bit(EV_REP, input->evbit);

	matrix_keypad_build_keymap(plat->keymap_data, KEYPAD_ROW_SHIFT,
				   input->keycode, input->keybit);

	ret = input_register_device(input);
	if (ret) {
		dev_err(&pdev->dev,
			"unable to register input device: %d\n", ret);
		goto out_freeinput;
	}

	keypad->irq	= irq;
	keypad->board	= plat;
	keypad->input	= input;
	keypad->base	= base;
	keypad->clk	= clk;

	INIT_DELAYED_WORK(&keypad->switch_work, db5500_gpio_switch_work);
	INIT_DELAYED_WORK(&keypad->gpio_work, db5500_gpio_release_work);

	clk_enable(keypad->clk);
if (!keypad->board->init) {
		dev_err(&pdev->dev, "init funtion not defined\n");
		ret = -EINVAL;
		goto out_unregisterinput;
	}

	if (keypad->board->init() < 0) {
		dev_err(&pdev->dev, "keyboard init config failed\n");
		ret = -EINVAL;
		goto out_unregisterinput;
	}

	if (!keypad->board->exit) {
		dev_err(&pdev->dev, "exit funtion not defined\n");
		ret = -EINVAL;
		goto out_unregisterinput;
	}

	if (keypad->board->exit() < 0) {
		dev_err(&pdev->dev,  "keyboard exit config failed\n");
		ret = -EINVAL;
		goto out_unregisterinput;
	}

	for (i = 0; i < keypad->board->krow; i++) {
		keypad->db5500_rows[i] = *plat->gpio_input_pins;
		keypad->gpio_input_irq[i] =
				GPIO_TO_IRQ(keypad->db5500_rows[i]);
		plat->gpio_input_pins++;
	}

	for (i = 0; i < keypad->board->kcol; i++) {
		keypad->db5500_cols[i] = *plat->gpio_output_pins;
		plat->gpio_output_pins++;
	}

	for (i = 0; i < keypad->board->krow; i++) {
		ret =  request_threaded_irq(keypad->gpio_input_irq[i],
				NULL, db5500_keypad_gpio_irq,
				IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND,
				"db5500-keypad-gpio", keypad);
		if (ret) {
			dev_err(&pdev->dev, "allocate gpio irq %d failed\n",
						keypad->gpio_input_irq[i]);
			goto out_unregisterinput;
		}
		enable_irq_wake(keypad->gpio_input_irq[i]);
	}

	ret = request_threaded_irq(keypad->irq, NULL, db5500_keypad_irq,
				   IRQF_ONESHOT, "db5500-keypad", keypad);
	if (ret) {
		dev_err(&pdev->dev, "allocate irq %d failed\n", keypad->irq);
		goto out_unregisterinput;
	}

	platform_set_drvdata(pdev, keypad);

	clk_disable(keypad->clk);
	regulator_disable(keypad->regulator);
	return 0;

out_unregisterinput:
	input_unregister_device(input);
	input = NULL;
	clk_disable(keypad->clk);
out_freeinput:
	input_free_device(input);
out_regulator_enable:
	regulator_put(keypad->regulator);
out_regulator_get:
	input_free_device(input);
out_freekeypad:
	kfree(keypad);
out_freeclk:
	clk_put(clk);
out_iounmap:
	iounmap(base);
out_freerequest_memregions:
	release_mem_region(res->start, resource_size(res));
out_ret:
	return ret;
}

/**
 * db5500_keypad_remove() - Removes the keypad driver
 * @pdev: pointer to platform device structure
 *
 * This function uses to remove the keypad
 * driver and returns integer.
 */
static int __devexit db5500_keypad_remove(struct platform_device *pdev)
{
	struct db5500_keypad *keypad = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	cancel_delayed_work_sync(&keypad->gpio_work);
	cancel_delayed_work_sync(&keypad->switch_work);
	free_irq(keypad->irq, keypad);
	input_unregister_device(keypad->input);

	clk_disable(keypad->clk);
	clk_put(keypad->clk);

	if (keypad->board->exit)
		keypad->board->exit();

	regulator_put(keypad->regulator);

	iounmap(keypad->base);

	if (res)
		release_mem_region(res->start, resource_size(res));

	kfree(keypad);

	return 0;
}

#ifdef CONFIG_PM
/**
 * db5500_keypad_suspend() - suspend the keypad controller
 * @dev: pointer to device structure
 *
 * This function is used to suspend the
 * keypad controller and returns integer
 */
static int db5500_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct db5500_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		enable_irq_wake(irq);
	else {
		cancel_delayed_work_sync(&keypad->gpio_work);
		cancel_delayed_work_sync(&keypad->switch_work);
		disable_irq(irq);
		keypad->enable_on_resume = keypad->enable;
		if (keypad->enable) {
			db5500_mode_enable(keypad, false);
			keypad->enable = false;
		}
	}

	return 0;
}

/**
 * db5500_keypad_resume() - resume the keypad controller
 * @dev: pointer to device structure
 *
 * This function is used to resume the keypad
 * controller and returns integer.
 */
static int db5500_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct db5500_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		disable_irq_wake(irq);
	else {
		if (keypad->enable_on_resume && !keypad->enable) {
			keypad->enable = true;
			db5500_mode_enable(keypad, true);
			/*
			 * Schedule the work queue to change it to GPIO mode
			 * if there is no activity keypad mode
			 */
			 schedule_delayed_work(&keypad->switch_work,
				 keypad->board->switch_delay);
		}
		enable_irq(irq);
	}

	return 0;
}

static const struct dev_pm_ops db5500_keypad_dev_pm_ops = {
	.suspend = db5500_keypad_suspend,
	.resume = db5500_keypad_resume,
};
#endif

static struct platform_driver db5500_keypad_driver = {
	.driver = {
		.name	= "db5500-keypad",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &db5500_keypad_dev_pm_ops,
#endif
	},
	.probe	= db5500_keypad_probe,
	.remove	= __devexit_p(db5500_keypad_remove),
};

/**
 * db5500_keypad_init() - Initialize the keypad driver
 *
 * This function uses to initializes the db5500
 * keypad driver and returns integer.
 */
static int __init db5500_keypad_init(void)
{
	return platform_driver_register(&db5500_keypad_driver);
}
module_init(db5500_keypad_init);

/**
 * db5500_keypad_exit() - De-initialize the keypad driver
 *
 * This function uses to de-initialize the db5500
 * keypad driver and returns none.
 */
static void __exit db5500_keypad_exit(void)
{
	platform_driver_unregister(&db5500_keypad_driver);
}
module_exit(db5500_keypad_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_DESCRIPTION("DB5500 Keypad Driver");
MODULE_ALIAS("platform:db5500-keypad");
