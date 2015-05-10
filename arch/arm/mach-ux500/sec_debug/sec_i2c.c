/* -------------------------------------------------------------------------
 * sec_i2c.c: i2c driver algorithms for bit-shift adapters while in Panic. This has been 
 * adapted from i2c-algo-bit.c.
 * -------------------------------------------------------------------------
 *   Copyright (C) 1995-2000 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ------------------------------------------------------------------------- */

/* With some changes from Frodo Looijaard <frodol@dds.nl>, Kyösti Mälkki
   <kmalkki@cc.hut.fi> and Jean Delvare <khali@linux-fr.org> */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/gpio.h>
#include <plat/gpio-nomadik.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c-algo-bit.h>
#include <mach/timex.h>
#include <plat/pincfg.h>
#include "../pins-db8500.h"

extern void i2c_remap_fp(struct i2c_algorithm *fp);

/* ----- global defines ----------------------------------------------- */

#ifdef DEBUG
#define bit_dbg(format, args...) \
	do { \
		pr_info(format, ##args); \
	} while (0)
#else
#define bit_dbg(format, args...) \
	do {} while (0)
#endif /* DEBUG */

/* --- setting states on the bus with the right timing: ---------------	*/

/* Toggle SDA by changing the direction of the pin */
static void panic_setsda(void *data, int state)
{
	struct i2c_gpio_platform_data *pdata = data;

	if (state)
		gpio_direction_input(pdata->sda_pin);
	else
		gpio_direction_output(pdata->sda_pin, 0);
}

/* Toggle SCL by changing the direction of the pin. */
static void panic_setscl(void *data, int state)
{
	struct i2c_gpio_platform_data *pdata = data;

	if (state)
		gpio_direction_input(pdata->scl_pin);
	else
		gpio_direction_output(pdata->scl_pin, 0);
}

static int panic_getsda(void *data)
{
	struct i2c_gpio_platform_data *pdata = data;

	return gpio_get_value(pdata->sda_pin);
}

static int panic_getscl(void *data)
{
	struct i2c_gpio_platform_data *pdata = data;

	return gpio_get_value(pdata->scl_pin);
}

#define setsda(adap, val)	panic_setsda(adap->data, val)
#define setscl(adap, val)	panic_setscl(adap->data, val)
#define getsda(adap)		panic_getsda(adap->data)
#define getscl(adap)		panic_getscl(adap->data)

static inline void sdalo(struct i2c_algo_bit_data *adap)
{
	setsda(adap, 0);
	udelay((adap->udelay + 1) / 2);
}

static inline void sdahi(struct i2c_algo_bit_data *adap)
{
	setsda(adap, 1);
	udelay((adap->udelay + 1) / 2);
}

static inline void scllo(struct i2c_algo_bit_data *adap)
{
	setscl(adap, 0);
	udelay(adap->udelay / 2);
}

/*
 * Raise scl line, and do checking for delays. This is necessary for slower
 * devices.
 */
static int sclhi(struct i2c_algo_bit_data *adap)
{
	unsigned long start = 0;
	unsigned long end = 0;

#ifdef ARCH_HAS_READ_CURRENT_TIMER
	unsigned long timeout = loops_per_jiffy * adap->timeout;
#else
	unsigned long timeout = 0x0000FFFF;
#endif
	setscl(adap, 1);

#ifdef ARCH_HAS_READ_CURRENT_TIMER
	read_current_timer(&start);
#endif

	while (!getscl(adap)) {
		/* This hw knows how to read the clock line, so we wait
		 * until it actually gets high.  This is safer as some
		 * chips may hold it low ("clock stretching") while they
		 * are processing data internally.
		 */
#ifdef ARCH_HAS_READ_CURRENT_TIMER
		read_current_timer(&end);
#else
		end++;
#endif
		if (time_after(end, start + adap->timeout))
			return -ETIMEDOUT;
	}

	udelay(adap->udelay);
	return 0;
}


/* --- other auxiliary functions --------------------------------------	*/
static void i2c_start(struct i2c_algo_bit_data *adap)
{
	/* assert: scl, sda are high */
	setsda(adap, 0);
	udelay(adap->udelay);
	scllo(adap);
}

static void i2c_repstart(struct i2c_algo_bit_data *adap)
{
	/* assert: scl is low */
	sdahi(adap);
	sclhi(adap);
	setsda(adap, 0);
	udelay(adap->udelay);
	scllo(adap);
}


static void i2c_stop(struct i2c_algo_bit_data *adap)
{
	/* assert: scl is low */
	sdalo(adap);
	sclhi(adap);
	setsda(adap, 1);
	udelay(adap->udelay);
}



/* send a byte without start cond., look for arbitration,
   check ackn. from slave */
/* returns:
 * 1 if the device acknowledged
 * 0 if the device did not ack
 * -ETIMEDOUT if an error occurred (while raising the scl line)
 */
static int i2c_outb(struct i2c_adapter *i2c_adap, unsigned char c)
{
	int i;
	int sb;
	int ack;
	struct i2c_algo_bit_data *adap = i2c_adap->algo_data;

	/* assert: scl is low */
	for (i = 7; i >= 0; i--) {
		sb = (c >> i) & 1;
		setsda(adap, sb);
		udelay((adap->udelay + 1) / 2);
		if (sclhi(adap) < 0) { /* timed out */
			bit_dbg("i2c_outb: 0x%02x, "
				"timeout at bit #%d\n", (int)c, i);
			return -ETIMEDOUT;
		}
		/* FIXME do arbitration here:
		 * if (sb && !getsda(adap)) -> ouch! Get out of here.
		 *
		 * Report a unique code, so higher level code can retry
		 * the whole (combined) message and *NOT* issue STOP.
		 */
		scllo(adap);
	}
	sdahi(adap);
	if (sclhi(adap) < 0) { /* timeout */
		bit_dbg("i2c_outb: 0x%02x, "
			"timeout at ack\n", (int)c);
		return -ETIMEDOUT;
	}

	/* read ack: SDA should be pulled down by slave, or it may
	 * NAK (usually to report problems with the data we wrote).
	 */
	ack = !getsda(adap);    /* ack: sda is pulled low -> success */
	bit_dbg("i2c_outb: 0x%02x %s\n", (int)c,
		ack ? "A" : "NA");

	scllo(adap);
	return ack;
	/* assert: scl is low (sda undef) */
}


static int i2c_inb(struct i2c_adapter *i2c_adap)
{
	/* read byte via i2c port, without start/stop sequence	*/
	/* acknowledge is sent in i2c_read.			*/
	int i;
	unsigned char indata = 0;
	struct i2c_algo_bit_data *adap = i2c_adap->algo_data;

	/* assert: scl is low */
	sdahi(adap);
	for (i = 0; i < 8; i++) {
		if (sclhi(adap) < 0) { /* timeout */
			bit_dbg("i2c_inb: timeout at bit "
				"#%d\n", 7 - i);
			return -ETIMEDOUT;
		}
		indata *= 2;
		if (getsda(adap))
			indata |= 0x01;
		setscl(adap, 0);
		udelay(i == 7 ? adap->udelay / 2 : adap->udelay);
	}
	/* assert: scl is low */
	return indata;
}

/* ----- Utility functions
 */

/* try_address tries to contact a chip for a number of
 * times before it gives up.
 * return values:
 * 1 chip answered
 * 0 chip did not answer
 * -x transmission error
 */
static int try_address(struct i2c_adapter *i2c_adap,
		       unsigned char addr, int retries)
{
	struct i2c_algo_bit_data *adap = i2c_adap->algo_data;
	int i, ret = 0;

	for (i = 0; i <= retries; i++) {
		ret = i2c_outb(i2c_adap, addr);
		if (ret == 1 || i == retries)
			break;
		bit_dbg("emitting stop condition\n");
		i2c_stop(adap);
		udelay(adap->udelay);
		yield();
		bit_dbg("emitting start condition\n");
		i2c_start(adap);
	}
	if (i && ret)
		bit_dbg("Used %d tries to %s client at "
			"0x%02x: %s\n", i + 1,
			addr & 1 ? "read from" : "write to", addr >> 1,
			ret == 1 ? "success" : "failed, timeout?");
	return ret;
}

static int sendbytes(struct i2c_adapter *i2c_adap, struct i2c_msg *msg)
{
	const unsigned char *temp = msg->buf;
	int count = msg->len;
	unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
	int retval;
	int wrcount = 0;

	while (count > 0) {
		retval = i2c_outb(i2c_adap, *temp);

		/* OK/ACK; or ignored NAK */
		if ((retval > 0) || (nak_ok && (retval == 0))) {
			count--;
			temp++;
			wrcount++;

		/* A slave NAKing the master means the slave didn't like
		 * something about the data it saw.  For example, maybe
		 * the SMBus PEC was wrong.
		 */
		} else if (retval == 0) {
			pr_emerg( "sendbytes: NAK bailout.\n");
			return -EIO;

		/* Timeout; or (someday) lost arbitration
		 *
		 * FIXME Lost ARB implies retrying the transaction from
		 * the first message, after the "winning" master issues
		 * its STOP.  As a rule, upper layer code has no reason
		 * to know or care about this ... it is *NOT* an error.
		 */
		} else {
			pr_emerg("sendbytes: error %d\n",retval);
			return retval;
		}
	}
	return wrcount;
}

static int acknak(struct i2c_adapter *i2c_adap, int is_ack)
{
	struct i2c_algo_bit_data *adap = i2c_adap->algo_data;

	/* assert: sda is high */
	if (is_ack)		/* send ack */
		setsda(adap, 0);
	udelay((adap->udelay + 1) / 2);
	if (sclhi(adap) < 0) {	/* timeout */
		pr_emerg( "readbytes: ack/nak timeout\n");
		return -ETIMEDOUT;
	}
	scllo(adap);
	return 0;
}

static int readbytes(struct i2c_adapter *i2c_adap, struct i2c_msg *msg)
{
	int inval;
	int rdcount = 0;	/* counts bytes read */
	unsigned char *temp = msg->buf;
	int count = msg->len;
	const unsigned flags = msg->flags;

	while (count > 0) {
		inval = i2c_inb(i2c_adap);
		if (inval >= 0) {
			*temp = inval;
			rdcount++;
		} else {   /* read timed out */
			break;
		}

		temp++;
		count--;

		/* Some SMBus transactions require that we receive the
		   transaction length as the first read byte. */
		if (rdcount == 1 && (flags & I2C_M_RECV_LEN)) {
			if (inval <= 0 || inval > I2C_SMBUS_BLOCK_MAX) {
				if (!(flags & I2C_M_NO_RD_ACK))
					acknak(i2c_adap, 0);
				pr_emerg("readbytes: invalid "
					"block length (%d)\n", inval);
				return -EREMOTEIO;
			}
			/* The original count value accounts for the extra
			   bytes, that is, either 1 for a regular transaction,
			   or 2 for a PEC transaction. */
			count += inval;
			msg->len += inval;
		}

		bit_dbg("readbytes: 0x%02x %s\n",
			inval,
			(flags & I2C_M_NO_RD_ACK)
				? "(no ack/nak)"
				: (count ? "A" : "NA"));

		if (!(flags & I2C_M_NO_RD_ACK)) {
			inval = acknak(i2c_adap, count);
			if (inval < 0)
				return inval;
		}
	}
	return rdcount;
}

/* doAddress initiates the transfer by generating the start condition (in
 * try_address) and transmits the address in the necessary format to handle
 * reads, writes as well as 10bit-addresses.
 * returns:
 *  0 everything went okay, the chip ack'ed, or IGNORE_NAK flag was set
 * -x an error occurred (like: -EREMOTEIO if the device did not answer, or
 *	-ETIMEDOUT, for example if the lines are stuck...)
 */
static int bit_doAddress(struct i2c_adapter *i2c_adap, struct i2c_msg *msg)
{
	unsigned short flags = msg->flags;
	unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
	struct i2c_algo_bit_data *adap = i2c_adap->algo_data;

	unsigned char addr;
	int ret, retries;

	retries = nak_ok ? 0 : i2c_adap->retries;

	if (flags & I2C_M_TEN) {
		/* a ten bit address */
		addr = 0xf0 | ((msg->addr >> 7) & 0x03);
		bit_dbg("addr0: %d\n", addr);
		/* try extended address code...*/
		ret = try_address(i2c_adap, addr, retries);
		if ((ret != 1) && !nak_ok)  {
			pr_emerg("died at extended address code\n");
			return -EREMOTEIO;
		}
		/* the remaining 8 bit address */
		ret = i2c_outb(i2c_adap, msg->addr & 0x7f);
		if ((ret != 1) && !nak_ok) {
			/* the chip did not ack / xmission error occurred */
			pr_emerg("died at 2nd address code\n");
			return -EREMOTEIO;
		}
		if (flags & I2C_M_RD) {
			pr_emerg("emitting repeated start condition\n");
			i2c_repstart(adap);
			/* okay, now switch into reading mode */
			addr |= 0x01;
			ret = try_address(i2c_adap, addr, retries);
			if ((ret != 1) && !nak_ok) {
				pr_emerg("died at repeated address code\n");
				return -EREMOTEIO;
			}
		}
	} else {		/* normal 7bit address	*/
		addr = msg->addr << 1;
		if (flags & I2C_M_RD)
			addr |= 1;
		if (flags & I2C_M_REV_DIR_ADDR)
			addr ^= 1;
		ret = try_address(i2c_adap, addr, retries);
		if ((ret != 1) && !nak_ok)
			return -ENXIO;
	}

	return 0;
}

static int panic_bit_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg msgs[], int num)
{
	struct i2c_msg *pmsg;
	struct i2c_algo_bit_data *adap = i2c_adap->algo_data;
	int i, ret;
	unsigned short nak_ok;

	if (adap->pre_xfer) {
		ret = adap->pre_xfer(i2c_adap);
		if (ret < 0)
			return ret;
	}

	bit_dbg("emitting start condition\n");
	i2c_start(adap);
	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		nak_ok = pmsg->flags & I2C_M_IGNORE_NAK;
		if (!(pmsg->flags & I2C_M_NOSTART)) {
			if (i) {
				bit_dbg("emitting "
					"repeated start condition\n");
				i2c_repstart(adap);
			}
			ret = bit_doAddress(i2c_adap, pmsg);
			if ((ret != 0) && !nak_ok) {
				bit_dbg("NAK from "
					"device addr 0x%02x msg #%d\n",
					msgs[i].addr, i);
				goto bailout;
			}
		}
		if (pmsg->flags & I2C_M_RD) {
			/* read bytes into buffer*/
			ret = readbytes(i2c_adap, pmsg);
			if (ret >= 1)
				bit_dbg("read %d byte%s\n",
					ret, ret == 1 ? "" : "s");
			if (ret < pmsg->len) {
				if (ret >= 0)
					ret = -EREMOTEIO;
				goto bailout;
			}
		} else {
			/* write bytes from buffer */
			ret = sendbytes(i2c_adap, pmsg);
			if (ret >= 1)
				bit_dbg("wrote %d byte%s\n",
					ret, ret == 1 ? "" : "s");
			if (ret < pmsg->len) {
				if (ret >= 0)
					ret = -EREMOTEIO;
				goto bailout;
			}
		}
	}
	ret = i;

bailout:
	bit_dbg("emitting stop condition\n");
	i2c_stop(adap);

	if (adap->post_xfer)
		adap->post_xfer(i2c_adap);
	return ret;
}

static u32 panic_bit_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
	       I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING;
}

static struct i2c_algorithm panic_i2c_algo = {
	.master_panic_xfer = panic_bit_xfer,
	.functionality = panic_bit_func
};

extern void prcmu_reroute_clk_fp( void );

static int reroute_clk_fp(void)
{
	int res = -EPERM;
#ifdef CONFIG_U8500_PRCMU
	prcmu_reroute_clk_fp();
	res = 0;
#endif
	return res;
}

int sec_i2c_panic_config(void)
{
/* clk reroute no longer required
	if (reroute_clk_fp()){
		return -EPERM;
	}
*/
	i2c_remap_fp(&panic_i2c_algo);

	return 0;
}


#ifdef CONFIG_SAMSUNG_PANIC_DISPLAY_I2C_PERIPHS
extern void usb_switch_panic_display(struct i2c_adapter *pAdap);
extern void gp2a_panic_display(struct i2c_adapter *pAdap);
extern void cypress_touchkey_panic_display(struct i2c_adapter *pAdap);
extern void nextchip_touchkey_panic_display(struct i2c_adapter *pAdap);

	/* Proximity Sensor I2C */
 static struct i2c_gpio_platform_data io_data_i2c0 = {
	.sda_pin = GPIO148_GPIO,
	.scl_pin = GPIO147_GPIO,
};

 	/* TSU6111(micro-USB switch  */
static struct i2c_gpio_platform_data io_data_i2c1 = {
	.sda_pin = GPIO17_GPIO,
	.scl_pin = GPIO16_GPIO,
};

	/* Sensors(Gyro/Accel MPU3050/BMA222) */
static struct i2c_gpio_platform_data io_data_i2c2 = {
	.sda_pin = GPIO10_GPIO,
	.scl_pin = GPIO11_GPIO,
};

 	/* Touchscreen I2C */
static struct i2c_gpio_platform_data io_data_i2c3 = {
	.sda_pin = GPIO229_GPIO,
	.scl_pin = GPIO230_GPIO,
};

static struct i2c_algo_bit_data algo_data = {
	.timeout = 100,
	.udelay = 3,
};

static struct i2c_adapter adap = {
	.algo = &panic_i2c_algo,
	.algo_data = &algo_data,
};

void sec_disp_i2c_info(void)
{
	unsigned int gpio;
	struct i2c_gpio_platform_data **pData = (struct i2c_gpio_platform_data **)&algo_data.data;

	/*
	 * Remap I2C driver for HW I2C controller 0 (I2C0)
	*/
	algo_data.data = &io_data_i2c0;

	gpio = ((*pData)->scl_pin);

	gpio_request(gpio,"scl");

	gpio = ((*pData)->sda_pin);

	gpio_request(gpio,"sda");

#if defined(CONFIG_PROXIMITY_GP2A)
	/* Display Registers for GP2A proximity on I2C0 */
	gp2a_panic_display(&adap);
#endif
#if defined(CONFIG_PROXIMITY_TMD2672)
		/* Display Registers for TMD2672 proximity on I2C0 */
//		tmd2672_panic_display(&adap);
#endif


	/*
	 * Remap I2C driver for HW I2C controller 1 (I2C1)
	*/
	algo_data.data = &io_data_i2c1,

	gpio = ((*pData)->scl_pin);

	gpio_request(gpio,"scl");

	gpio = ((*pData)->sda_pin);

	gpio_request(gpio,"sda");

#if defined(CONFIG_USB_SWITCHER)
	/* Display Registers for uUSB switch on I2C1 */
	usb_switch_panic_display(&adap);
#endif

	/*
	 * No need to supply Algo_data for devices using SW I2C as GPIOs will
	 * be configured already. Still need to pass Panic safe SW I2C algorithms.
	*/
	adap.algo_data = NULL;

#if defined(CONFIG_KEYBOARD_CYPRESS_TOUCH)
	/* Display Registers for Cypress Touchkey */
	cypress_touchkey_panic_display(&adap);
#endif

#if defined (CONFIG_KEYBOARD_NEXTCHIP_TOUCH)
	/* Display Registers for Nextchip Touchkey */
	nextchip_touchkey_panic_display(&adap);
#endif
}
#endif
