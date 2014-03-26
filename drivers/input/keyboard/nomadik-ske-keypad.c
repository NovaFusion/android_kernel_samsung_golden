/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * co-Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *
 * License terms:GNU General Public License (GPL) version 2
 *
 * Keypad controller driver for the SKE (Scroll Key Encoder) module used in
 * the Nomadik 8815 and Ux500 platforms.
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <plat/ske.h>
#include <plat/gpio-nomadik.h>

/* SKE_CR bits */
#define SKE_KPMLT	(0x1 << 6)
#define SKE_KPCN	(0x7 << 3)
#define SKE_KPASEN	(0x1 << 2)
#define SKE_KPASON	(0x1 << 7)

/* SKE_IMSC bits */
#define SKE_KPIMA	(0x1 << 2)

/* SKE_ICR bits */
#define SKE_KPICS	(0x1 << 3)
#define SKE_KPICA	(0x1 << 2)

/* SKE_RIS bits */
#define SKE_KPRISA	(0x1 << 2)

#define SKE_KEYPAD_ROW_SHIFT	3
#define SKE_KPD_KEYMAP_SIZE	(8 * 8)

/* keypad auto scan registers */
#define SKE_ASR0	0x20
#define SKE_ASR1	0x24
#define SKE_ASR2	0x28
#define SKE_ASR3	0x2C

#define SKE_NUM_ASRX_REGISTERS	(4)
#define	KEY_PRESSED_DELAY	10


#define KEY_REPORTED	1
#define KEY_PRESSED	2

/**
 * struct ske_keypad  - data structure used by keypad driver
 * @dev:		Pointer to the structure device
 * @irq:		irq no
 * @reg_base:		ske regsiters base address
 * @input:		pointer to input device object
 * @board:		keypad platform device
 * @keymap:		matrix scan code table for keycodes
 * @clk:		clock structure pointer
 * @ske_keypad_lock:    lock used while writting into registers
 * @enable:		flag to enable the driver event
 * @enable_on_resume:   set if keypad should be enabled on resume
 * @regulator:		pointer to the regulator used for ske kyepad
 * @gpio_input_irq:	array for gpio irqs
 * @key_pressed:	hold the key state
 * @work:		delayed work variable for gpio switch
 * @ske_rows:		rows gpio array for ske
 * @ske_cols:		columns gpio array for ske
 * @gpio_row:		gpio row
 * @gpio_col:		gpio column
 * @gpio_work:		delayed work variable for release gpio key
 * @keys:		matrix holding key status
 * @scan_work:		delayed work for scaning new key actions
 */
struct ske_keypad {
	struct device *dev;
	int irq;
	void __iomem *reg_base;
	struct input_dev *input;
	const struct ske_keypad_platform_data *board;
	unsigned short keymap[SKE_KPD_KEYMAP_SIZE];
	struct clk *clk;
	spinlock_t ske_keypad_lock;
	bool enable;
	bool enable_on_resume;
	struct regulator *regulator;
	int *gpio_input_irq;
	int key_pressed;
	struct delayed_work work;
	int *ske_rows;
	int *ske_cols;
	int gpio_row;
	int gpio_col;
	struct delayed_work gpio_work;
	u8 **keys;
	struct delayed_work scan_work;
};

static void ske_keypad_set_bits(struct ske_keypad *keypad, u16 addr,
		u8 mask, u8 data)
{
	u32 ret;

	spin_lock(&keypad->ske_keypad_lock);

	ret = readl(keypad->reg_base + addr);
	ret &= ~mask;
	ret |= data;
	writel(ret, keypad->reg_base + addr);

	spin_unlock(&keypad->ske_keypad_lock);
}

/**
 * ske_keypad_chip_init: init keypad controller configuration
 * @keypad: pointer to device structure
 * Enable Multi key press detection, auto scan mode
 */
static int __devinit ske_keypad_chip_init(struct ske_keypad *keypad)
{
	u32 value;
	int timeout = keypad->board->debounce_ms;

	/* check SKE_RIS to be 0 */
	while ((readl(keypad->reg_base + SKE_RIS) != 0x00000000) && timeout--)
		cpu_relax();

	if (!timeout)
		return -EINVAL;

	/**
	 * set debounce value
	 * keypad dbounce is configured in DBCR[15:8]
	 * dbounce value in steps of 32/32.768 ms
	 */
	spin_lock(&keypad->ske_keypad_lock);
	value = readl(keypad->reg_base + SKE_DBCR);
	value = value & 0xff;
	value |= ((keypad->board->debounce_ms * 32000)/32768) << 8;
	writel(value, keypad->reg_base + SKE_DBCR);
	spin_unlock(&keypad->ske_keypad_lock);

	/* enable multi key detection */
	ske_keypad_set_bits(keypad, SKE_CR, 0x0, SKE_KPMLT);

	/**
	 * set up the number of columns
	 * KPCN[5:3] defines no. of keypad columns to be auto scanned
	 */
	value = (keypad->board->kcol - 1) << 3;
	ske_keypad_set_bits(keypad, SKE_CR, SKE_KPCN, value);

	/* clear keypad interrupt for auto(and pending SW) scans */
	ske_keypad_set_bits(keypad, SKE_ICR, 0x0, SKE_KPICA | SKE_KPICS);

	/* un-mask keypad interrupts */
	ske_keypad_set_bits(keypad, SKE_IMSC, 0x0, SKE_KPIMA);

	/* enable automatic scan */
	ske_keypad_set_bits(keypad, SKE_CR, 0x0, SKE_KPASEN);

	return 0;
}

static void ske_mode_enable(struct ske_keypad *keypad, bool enable)
{
	int i;

	if (!enable) {
		dev_dbg(keypad->dev, "%s disable keypad\n", __func__);
		writel(0, keypad->reg_base + SKE_CR);
		if (keypad->board->exit)
			keypad->board->exit();
		for (i = 0; i < keypad->board->kconnected_rows; i++) {
			enable_irq(keypad->gpio_input_irq[i]);
			enable_irq_wake(keypad->gpio_input_irq[i]);
		}
		clk_disable(keypad->clk);
		regulator_disable(keypad->regulator);
	} else {
		dev_dbg(keypad->dev, "%s enable keypad\n", __func__);
		regulator_enable(keypad->regulator);
		clk_enable(keypad->clk);
		for (i = 0; i < keypad->board->kconnected_rows; i++) {
			disable_irq_nosync(keypad->gpio_input_irq[i]);
			disable_irq_wake(keypad->gpio_input_irq[i]);
		}
		if (keypad->board->init)
			keypad->board->init();
		ske_keypad_chip_init(keypad);
	}
}
static void ske_enable(struct ske_keypad *keypad, bool enable)
{
	keypad->enable = enable;
	if (keypad->enable) {
		enable_irq(keypad->irq);
		ske_mode_enable(keypad, true);
	} else {
		ske_mode_enable(keypad, false);
		disable_irq(keypad->irq);
	}
}

static ssize_t ske_show_attr_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ske_keypad *keypad = platform_get_drvdata(pdev);
	return sprintf(buf, "%d\n", keypad->enable);
}

static ssize_t ske_store_attr_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ske_keypad *keypad = platform_get_drvdata(pdev);
	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if ((val != 0) && (val != 1))
		return -EINVAL;

	if (keypad->enable != val) {
		keypad->enable = val ? true : false;
		ske_enable(keypad, keypad->enable);
	}
	return count;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO,
	ske_show_attr_enable, ske_store_attr_enable);

static struct attribute *ske_keypad_attrs[] = {
	&dev_attr_enable.attr,
	NULL,
};

static struct attribute_group ske_attr_group = {
	.attrs = ske_keypad_attrs,
};

static void ske_keypad_report(struct ske_keypad *keypad, u8 status, int col)
{
	int row = 0, code, pos;
	u32 ske_ris;
	int num_of_rows;

	/* find out the row */
	num_of_rows = hweight8(status);
	do {
		pos = __ffs(status);
		row = pos;
		status &= ~(1 << pos);

		if (row >= keypad->board->krow)
			/* no more rows supported by this keypad */
			break;

		code = MATRIX_SCAN_CODE(row, col, SKE_KEYPAD_ROW_SHIFT);
		ske_ris = readl(keypad->reg_base + SKE_RIS);
		keypad->key_pressed = ske_ris & SKE_KPRISA;

		dev_dbg(keypad->dev,
			"%s key_pressed:%d code:%d row:%d col:%d\n",
			__func__, keypad->key_pressed, code, row, col);

		if (keypad->key_pressed)
			keypad->keys[row][col] |= KEY_PRESSED;

		num_of_rows--;
	} while (num_of_rows);
}

static void ske_keypad_read_data(struct ske_keypad *keypad)
{
	u8 status;
	int col = 0;
	int ske_asr, i;

	/**
	 * Read the auto scan registers
	 *
	 * Each SKE_ASRx (x=0 to x=3) contains two row values.
	 * lower byte contains row value for column 2*x,
	 * upper byte contains row value for column 2*x + 1
	 */
	for (i = 0; i < SKE_NUM_ASRX_REGISTERS; i++) {
		ske_asr = readl(keypad->reg_base + SKE_ASR0 + (4 * i));
		if (!ske_asr)
			continue;

		/* now that ASRx is zero, find out the coloumn x and row y */
		status = ske_asr & 0xff;
		if (status) {
			col = i * 2;
			if (col >= keypad->board->kcol)
				/* no more columns supported by this keypad */
				break;
			ske_keypad_report(keypad, status, col);
		}
		status = (ske_asr & 0xff00) >> 8;
		if (status) {
			col = (i * 2) + 1;
			if (col >= keypad->board->kcol)
				/* no more columns supported by this keypad */
				break;
			ske_keypad_report(keypad, status, col);
		}
	}
}

static void ske_keypad_scan_work(struct work_struct *work)
{
	int timeout = 10;
	int i, j, code;
	struct ske_keypad *keypad = container_of(work,
					struct ske_keypad, scan_work.work);
	struct input_dev *input = keypad->input;

	/* Wait for autoscan to complete */
	while (readl(keypad->reg_base + SKE_CR) & SKE_KPASON)
		cpu_relax();

	/* SKEx registers are stable and can be read */
	ske_keypad_read_data(keypad);

	/* Check for key actions */
	for (i = 0; i < keypad->board->krow; i++) {
		for (j = 0; j < keypad->board->kcol; j++) {
			switch (keypad->keys[i][j]) {
			case KEY_REPORTED:
				/**
				 * Key was reported but is no longer pressed,
				 * report it as released.
				 */
				code = MATRIX_SCAN_CODE(i, j,
							SKE_KEYPAD_ROW_SHIFT);
				input_event(input, EV_MSC, MSC_SCAN, code);
				input_report_key(input, keypad->keymap[code],
						 0);
				input_sync(input);
				keypad->keys[i][j] = 0;
				dev_dbg(keypad->dev,
					"%s Key release reported, code:%d "
					"(key %d)\n",
					__func__, code, keypad->keymap[code]);
				break;
			case KEY_PRESSED:
				/* Key pressed but not yet reported, report */
				code = MATRIX_SCAN_CODE(i, j,
							SKE_KEYPAD_ROW_SHIFT);
				input_event(input, EV_MSC, MSC_SCAN, code);
				input_report_key(input, keypad->keymap[code],
						 1);
				input_sync(input);
				dev_dbg(keypad->dev,
					"%s Key press reported, code:%d "
					"(key %d)\n",
					__func__, code, keypad->keymap[code]);
				/* Intentional fall though */
			case (KEY_REPORTED | KEY_PRESSED):
				/**
				 * Key pressed and reported, just reset
				 * KEY_PRESSED for next scan
				 */
				keypad->keys[i][j] = KEY_REPORTED;
				break;
			}
		}
	}

	if (keypad->key_pressed) {
		/*
		 * Key still pressed, schedule work to poll changes in 100 ms
		 * After increasing the delay from 50 to 100 it is taking
		 * 2% to 3% load on average.
		 */
		schedule_delayed_work(&keypad->scan_work,
				      msecs_to_jiffies(100));
	} else {
		/* For safty measure, clear interrupt once more */
		ske_keypad_set_bits(keypad, SKE_ICR, 0x0, SKE_KPICA);

		/* Wait for raw interrupt to clear */
		while ((readl(keypad->reg_base + SKE_RIS) & SKE_KPRISA) &&
		       --timeout) {
			udelay(10);
		}

		if (!timeout)
			dev_err(keypad->dev,
				"%s Timeed out waiting on irq to clear\n",
				__func__);

		/* enable auto scan interrupts */
		ske_keypad_set_bits(keypad, SKE_IMSC, 0x0, SKE_KPIMA);

		/**
		 * Schedule the work queue to change it to GPIO mode
		 * if there is no activity in SKE mode
		 */
		if (!keypad->key_pressed && keypad->enable)
			schedule_delayed_work(&keypad->work,
					      keypad->board->switch_delay);
	}
}

static void ske_gpio_switch_work(struct work_struct *work)
{
	struct ske_keypad *keypad = container_of(work,
					struct ske_keypad, work.work);

	ske_mode_enable(keypad, false);
	keypad->enable = false;
}

static void ske_gpio_release_work(struct work_struct *work)
{
	int code;
	struct ske_keypad *keypad = container_of(work,
					struct ske_keypad, gpio_work.work);
	struct input_dev *input = keypad->input;

	code = MATRIX_SCAN_CODE(keypad->gpio_row, keypad->gpio_col,
						SKE_KEYPAD_ROW_SHIFT);

	dev_dbg(keypad->dev, "%s Key press reported, code:%d (key %d)\n",
		__func__, code, keypad->keymap[code]);

	input_event(input, EV_MSC, MSC_SCAN, code);
	input_report_key(input, keypad->keymap[code], 1);
	input_sync(input);
	input_report_key(input, keypad->keymap[code], 0);
	input_sync(input);
}

static int ske_read_get_gpio_row(struct ske_keypad *keypad)
{
	int row;
	int value = 0;
	int ret;

	/* read all rows GPIO data register values */
	for (row = 0; row < keypad->board->kconnected_rows ; row++) {
		ret  = gpio_get_value(keypad->ske_rows[row]);
		value += (1 << row) *  ret;
	}

	/* get the exact row */
	for (row = 0; row < keypad->board->kconnected_rows; row++) {
		if (((1 << row) & value) == 0)
			return row;
	}

	return -1;
}

static void ske_set_cols(struct ske_keypad *keypad, int col)
{
	int i ;
	int value;

	/**
	 * Set all columns except the requested column
	 * output pin as high
	 */
	for (i = 0; i < keypad->board->kconnected_cols; i++) {
		if (i == col)
			value = 0;
		else
			value = 1;
		gpio_request(keypad->ske_cols[i], "ske-kp");
		gpio_direction_output(keypad->ske_cols[i], value);
		gpio_free(keypad->ske_cols[i]);
	}
}

static void ske_free_cols(struct ske_keypad *keypad)
{
	int i ;

	for (i = 0; i < keypad->board->kconnected_cols; i++) {
		gpio_request(keypad->ske_cols[i], "ske-kp");
		gpio_direction_output(keypad->ske_cols[i], 0);
		gpio_free(keypad->ske_cols[i]);
	}
}

static void ske_manual_scan(struct ske_keypad *keypad)
{
	int row;
	int col;

	for (col = 0; col < keypad->board->kconnected_cols; col++) {
		ske_set_cols(keypad, col);
		row = ske_read_get_gpio_row(keypad);
		if (row >= 0) {
			keypad->key_pressed = 1;
			keypad->gpio_row = row;
			keypad->gpio_col = col;
			break;
		}
	}
	ske_free_cols(keypad);
}

static irqreturn_t ske_keypad_gpio_irq(int irq, void *dev_id)
{
	struct ske_keypad *keypad = dev_id;

	if (!gpio_get_value(NOMADIK_IRQ_TO_GPIO(irq))) {
		ske_manual_scan(keypad);
		if (!keypad->enable) {
			keypad->enable = true;
			ske_mode_enable(keypad, true);
			/**
			 * Schedule the work queue to change it back to GPIO
			 * mode if there is no activity in SKE mode
			 */
			schedule_delayed_work(&keypad->work,
					      keypad->board->switch_delay);
		}
		/**
		 * Schedule delayed work to report key press if it is not
		 * detected in SKE mode.
		 */
		if (keypad->key_pressed)
			schedule_delayed_work(&keypad->gpio_work,
						KEY_PRESSED_DELAY);
	}

	return IRQ_HANDLED;
}
static irqreturn_t ske_keypad_irq(int irq, void *dev_id)
{
	struct ske_keypad *keypad = dev_id;
	cancel_delayed_work_sync(&keypad->gpio_work);
	cancel_delayed_work_sync(&keypad->work);

	/* disable auto scan interrupt; mask the interrupt generated */
	ske_keypad_set_bits(keypad, SKE_IMSC, SKE_KPIMA, 0x0);
	ske_keypad_set_bits(keypad, SKE_ICR, 0x0, SKE_KPICA);

	schedule_delayed_work(&keypad->scan_work, 0);

	return IRQ_HANDLED;
}

static int __devinit ske_keypad_probe(struct platform_device *pdev)
{
	struct ske_keypad *keypad;
	struct resource *res = NULL;
	struct input_dev *input;
	struct clk *clk;
	void __iomem *reg_base;
	int ret = 0;
	int irq;
	int i;
	struct ske_keypad_platform_data *plat = pdev->dev.platform_data;

	if (!plat) {
		dev_err(&pdev->dev, "invalid keypad platform data\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keypad irq\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "missing platform resources\n");
		return -ENXIO;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		return -EBUSY;
	}

	reg_base = ioremap(res->start, resource_size(res));
	if (!reg_base) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		ret = -ENXIO;
		goto out_freerequest_memregions;
	}

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to clk_get\n");
		ret = PTR_ERR(clk);
		goto out_freeioremap;
	}

	/* resources are sane; we begin allocation */
	keypad = kzalloc(sizeof(struct ske_keypad), GFP_KERNEL);
	if (!keypad) {
		dev_err(&pdev->dev, "failed to allocate keypad memory\n");
		goto out_freeclk;
	}
	keypad->dev = &pdev->dev;

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
		goto out_regulator_get;
	} else {
		ret = regulator_enable(keypad->regulator);
		if (ret < 0) {
			dev_err(&pdev->dev, "regulator_enable failed\n");
			goto out_regulator_enable;
		}
	}

	input->id.bustype = BUS_HOST;
	input->name = "ux500-ske-keypad";
	input->dev.parent = &pdev->dev;

	input->keycode = keypad->keymap;
	input->keycodesize = sizeof(keypad->keymap[0]);
	input->keycodemax = ARRAY_SIZE(keypad->keymap);

	input_set_capability(input, EV_MSC, MSC_SCAN);
	input_set_drvdata(input, keypad);

	__set_bit(EV_KEY, input->evbit);
	if (!plat->no_autorepeat)
		__set_bit(EV_REP, input->evbit);

	matrix_keypad_build_keymap(plat->keymap_data, SKE_KEYPAD_ROW_SHIFT,
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
	keypad->reg_base = reg_base;
	keypad->clk	= clk;
	INIT_DELAYED_WORK(&keypad->work, ske_gpio_switch_work);
	INIT_DELAYED_WORK(&keypad->gpio_work, ske_gpio_release_work);
	INIT_DELAYED_WORK(&keypad->scan_work, ske_keypad_scan_work);

	/* allocations are sane, we begin HW initialization */
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

	if (plat->kconnected_rows == 0) {
		/*
		 * Board config data does not specify the number of connected
		 * rows and columns; assume that it matches the specified max
		 * values.
		 */
		plat->kconnected_rows = plat->krow;
		plat->kconnected_cols = plat->kcol;
	}

	/* this code doesn't currently support non-square keypad */
	if (plat->kconnected_rows != plat->kconnected_cols) {
		dev_err(&pdev->dev,
			"invalid keypad configuration (not square)\n"),
		ret = -EINVAL;
		goto out_unregisterinput;
	}

	if (plat->kconnected_rows > SKE_KPD_MAX_ROWS ||
		plat->kconnected_cols > SKE_KPD_MAX_COLS) {
		dev_err(&pdev->dev,
			"invalid keypad configuration (too many rows/cols)\n"),
		ret = -EINVAL;
		goto out_unregisterinput;
	}

	keypad->gpio_input_irq = kmalloc(sizeof(*keypad->gpio_input_irq) *
						plat->kconnected_rows,
						GFP_KERNEL);
	if (!keypad->gpio_input_irq) {
		dev_err(&pdev->dev, "failed to allocate input_irq memory\n");
		goto out_unregisterinput;
	}

	keypad->ske_rows = kmalloc(sizeof(*keypad->ske_rows) *
					plat->kconnected_rows, GFP_KERNEL);
	if (!keypad->ske_rows) {
		dev_err(&pdev->dev, "failed to allocate ske_rows memory\n");
		goto out_freemem_input_irq;
	}

	keypad->ske_cols = kmalloc(sizeof(*keypad->ske_cols) *
					plat->kconnected_cols, GFP_KERNEL);
	if (!keypad->ske_cols) {
		dev_err(&pdev->dev, "failed to allocate ske_cols memory\n");
		goto out_freemem_rows;
	}

	keypad->keys = kzalloc(sizeof(*keypad->keys) * plat->krow, GFP_KERNEL);
	if (!keypad->keys) {
		dev_err(&pdev->dev, "failed to allocate keys:rows memory\n");
		goto out_freemem_cols;
	}
	for (i = 0; i < plat->krow; i++) {
		keypad->keys[i] = kzalloc(sizeof(*keypad->keys[i]) *
						plat->kcol, GFP_KERNEL);
		if (!keypad->keys[i]) {
			dev_err(&pdev->dev,
				"failed to allocate keys:cols memory\n");
			goto out_freemem_keys;
		}
	}

	for (i = 0; i < plat->kconnected_rows; i++) {
		keypad->ske_rows[i] = plat->gpio_input_pins[i];
		keypad->ske_cols[i] = plat->gpio_output_pins[i];
		keypad->gpio_input_irq[i] =
				NOMADIK_GPIO_TO_IRQ(keypad->ske_rows[i]);
	}

	for (i = 0; i < keypad->board->kconnected_rows; i++) {
		ret =  request_threaded_irq(keypad->gpio_input_irq[i],
				NULL, ske_keypad_gpio_irq,
				IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND,
				"ske-keypad-gpio", keypad);
		if (ret) {
			dev_err(&pdev->dev, "allocate gpio irq %d failed\n",
						keypad->gpio_input_irq[i]);
			goto out_freemem_keys;
		}
		enable_irq_wake(keypad->gpio_input_irq[i]);
	}

	ret =  request_threaded_irq(keypad->irq, NULL, ske_keypad_irq,
				IRQF_ONESHOT, "ske-keypad", keypad);
	if (ret) {
		dev_err(&pdev->dev, "allocate irq %d failed\n", keypad->irq);
		goto out_freemem_keys;
	}

	/* sysfs implementation for dynamic enable/disable the input event */
	ret = sysfs_create_group(&pdev->dev.kobj, &ske_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs entries\n");
		goto out_free_irq;
	}

	if (plat->wakeup_enable)
		device_init_wakeup(&pdev->dev, true);

	platform_set_drvdata(pdev, keypad);

	clk_disable(keypad->clk);
	regulator_disable(keypad->regulator);

	return 0;

out_free_irq:
	free_irq(keypad->irq, keypad);
out_freemem_keys:
	for (i = 0; i < plat->krow; i++)
		kfree(keypad->keys[i]);
	kfree(keypad->keys);
out_freemem_cols:
	kfree(keypad->ske_cols);
out_freemem_rows:
	kfree(keypad->ske_rows);
out_freemem_input_irq:
	kfree(keypad->gpio_input_irq);
out_unregisterinput:
	input_unregister_device(input);
	input = NULL;
	clk_disable(keypad->clk);
out_freeinput:
	regulator_disable(keypad->regulator);
out_regulator_enable:
	regulator_put(keypad->regulator);
out_regulator_get:
	input_free_device(input);
out_freekeypad:
	kfree(keypad);
out_freeclk:
	clk_put(clk);
out_freeioremap:
	iounmap(reg_base);
out_freerequest_memregions:
	release_mem_region(res->start, resource_size(res));
	return ret;
}

static int __devexit ske_keypad_remove(struct platform_device *pdev)
{
	struct ske_keypad *keypad = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int i;

	cancel_delayed_work_sync(&keypad->gpio_work);
	cancel_delayed_work_sync(&keypad->work);
	cancel_delayed_work_sync(&keypad->scan_work);

	for (i = 0; i < keypad->board->krow; i++)
		kfree(keypad->keys[i]);
	kfree(keypad->keys);
	kfree(keypad->ske_cols);
	kfree(keypad->ske_rows);

	input_unregister_device(keypad->input);
	sysfs_remove_group(&pdev->dev.kobj, &ske_attr_group);
	if (keypad->enable)
		clk_disable(keypad->clk);
	clk_put(keypad->clk);

	if (keypad->enable && keypad->board->exit)
		keypad->board->exit();
	else {
		for (i = 0; i < keypad->board->krow; i++) {
			disable_irq_nosync(keypad->gpio_input_irq[i]);
			disable_irq_wake(keypad->gpio_input_irq[i]);
		}
	}
	for (i = 0; i < keypad->board->krow; i++)
		free_irq(keypad->gpio_input_irq[i], keypad);

	kfree(keypad->gpio_input_irq);
	free_irq(keypad->irq, keypad);
	regulator_put(keypad->regulator);

	iounmap(keypad->reg_base);
	release_mem_region(res->start, resource_size(res));
	kfree(keypad);

	return 0;
}

#ifdef CONFIG_PM
static int ske_keypad_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ske_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		enable_irq_wake(irq);
	else {
		cancel_delayed_work_sync(&keypad->gpio_work);
		cancel_delayed_work_sync(&keypad->work);
		cancel_delayed_work_sync(&keypad->scan_work);
		disable_irq(irq);

		keypad->enable_on_resume = keypad->enable;

		if (keypad->enable) {
			ske_mode_enable(keypad, false);
			keypad->enable = false;
		}
	}

	return 0;
}

static int ske_keypad_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ske_keypad *keypad = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		disable_irq_wake(irq);
	else {
		if (keypad->enable_on_resume && !keypad->enable) {
			keypad->enable = true;
			ske_mode_enable(keypad, true);
			/*
			 * Schedule the work queue to change it to GPIO mode
			 * if there is no activity in SKE mode
			 */
			if (!keypad->key_pressed)
				schedule_delayed_work(&keypad->work,
						keypad->board->switch_delay);
		}
		enable_irq(irq);
	}

	return 0;
}

static const struct dev_pm_ops ske_keypad_dev_pm_ops = {
	.suspend = ske_keypad_suspend,
	.resume = ske_keypad_resume,
};
#endif

struct platform_driver ske_keypad_driver = {
	.driver = {
		.name = "nmk-ske-keypad",
		.owner  = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &ske_keypad_dev_pm_ops,
#endif
	},
	.probe = ske_keypad_probe,
	.remove = __devexit_p(ske_keypad_remove),
};

static int __init ske_keypad_init(void)
{
	return platform_driver_register(&ske_keypad_driver);
}
module_init(ske_keypad_init);

static void __exit ske_keypad_exit(void)
{
	platform_driver_unregister(&ske_keypad_driver);
}
module_exit(ske_keypad_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Naveen Kumar <naveen.gaddipati@stericsson.com>");
MODULE_DESCRIPTION("Nomadik Scroll-Key-Encoder Keypad Driver");
MODULE_ALIAS("platform:nomadik-ske-keypad");
