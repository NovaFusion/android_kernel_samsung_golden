/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * ST-Ericsson MCDE toshiba dsi2lvds display driver
 *
 * Author: Jimmy Rubin <jimmy.rubin@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <video/mcde_display.h>

#define RESET_DURATION_US	10
#define RESET_DELAY_US		12000
#define SLEEP_OUT_DELAY_MS	120
#define IO_REGU			"vdd1"
#define IO_REGU_MIN		1650000
#define IO_REGU_MAX		3300000

#define DSI_HS_FREQ_HZ		570000000
#define DSI_LP_FREQ_HZ		19200000

/* DSI D-PHY Layer Registers */
#define	D0W_DPHYCONTTX		0x0004
#define	CLW_DPHYCONTRX		0x0020
#define	D0W_DPHYCONTRX		0x0024
#define	D1W_DPHYCONTRX		0x0028
#define	D2W_DPHYCONTRX		0x002C
#define	D3W_DPHYCONTRX		0x0030
#define	COM_DPHYCONTRX		0x0038
#define	CLW_CNTRL		0x0040
#define	D0W_CNTRL		0x0044
#define	D1W_CNTRL		0x0048
#define	D2W_CNTRL		0x004C
#define	D3W_CNTRL		0x0050
#define	DFTMODE_CNTRL		0x0054

/* DSI PPI Layer Registers */
#define	PPI_STARTPPI		0x0104
#define	PPI_BUSYPPI		0x0108
#define	PPI_LINEINITCNT		0x0110
#define	PPI_LPTXTIMECNT		0x0114
#define	PPI_LANEENABLE		0x0134
#define	PPI_TX_RX_TA		0x013C
#define	PPI_CLS_ATMR		0x0140
#define	PPI_D0S_ATMR		0x0144
#define	PPI_D1S_ATMR		0x0148
#define	PPI_D2S_ATMR		0x014C
#define	PPI_D3S_ATMR		0x0150
#define	PPI_D0S_CLRSIPOCOUNT	0x0164
#define	PPI_D1S_CLRSIPOCOUNT	0x0168
#define	PPI_D2S_CLRSIPOCOUNT	0x016C
#define	PPI_D3S_CLRSIPOCOUNT	0x0170
#define CLS_PRE			0x0180
#define D0S_PRE			0x0184
#define D1S_PRE			0x0188
#define D2S_PRE			0x018C
#define D3S_PRE			0x0190
#define CLS_PREP		0x01A0
#define D0S_PREP		0x01A4
#define D1S_PREP		0x01A8
#define D2S_PREP		0x01AC
#define D3S_PREP		0x01B0
#define CLS_ZERO		0x01C0
#define	D0S_ZERO		0x01C4
#define	D1S_ZERO		0x01C8
#define	D2S_ZERO		0x01CC
#define	D3S_ZERO		0x01D0
#define PPI_CLRFLG		0x01E0
#define PPI_CLRSIPO		0x01E4
#define PPI_HSTimeout		0x01F0
#define PPI_HSTimeoutEnable	0x01F4

/* DSI Protocol Layer Registers */
#define DSI_STARTDSI		0x0204
#define DSI_BUSYDSI		0x0208
#define DSI_LANEENABLE		0x0210
#define DSI_LANESTATUS0		0x0214
#define DSI_LANESTATUS1		0x0218
#define DSI_INTSTATUS		0x0220
#define DSI_INTMASK		0x0224
#define DSI_INTCLR		0x0228
#define DSI_LPTXTO		0x0230

/* DSI General Registers */
#define	DSIERRCNT		0x0300

/* DSI Application Layer Registers */
#define APLCTRL			0x0400
#define RDPKTLN			0x0404

/* Video Path Registers */
#define	VPCTRL			0x0450
#define HTIM1			0x0454
#define HTIM2			0x0458
#define VTIM1			0x045C
#define VTIM2			0x0460
#define VFUEN			0x0464

/* LVDS Registers */
#define LVMX0003		0x0480
#define LVMX0407		0x0484
#define LVMX0811		0x0488
#define LVMX1215		0x048C
#define LVMX1619		0x0490
#define LVMX2023		0x0494
#define LVMX2427		0x0498
#define LVCFG			0x049C
#define	LVPHY0			0x04A0
#define	LVPHY1			0x04A1

/* System Registers */
#define SYSSTAT			0x0500
#define SYSRST			0x0504

/* GPIO Registers */
#define GPIOC			0x0520
#define GPIOO			0x0524
#define GPIOI			0x0528

/* Chip Revision Registers */
#define IDREG			0x0580

/* Debug Registers */
#define DEBUG00			0x05A0
#define DEBUG01			0x05A4

#define DSI2LVDS_NAME "dsi2lvds_i2c"

static struct i2c_client *i2cclient;

enum lvds_port_fmt {
	LVDS_PORTPIXFMT_18BPP =		0x01,
	LVDS_PORTPIXFMT_24BPP =		0x02,
};

struct lvds_video_mode {
	u32 hdispr;
	u32 vdispr;
	u32 hbpr;	/* horizontal back porch: left margin (excl. hsync) */
	u32 hfpr;	/* horizontal front porch: right margin (excl. hsync) */
	u32 hpw;	/* horizontal sync width */
	u32 vbpr;	/* vertical back porch: upper margin (excl. vsync) */
	u32 vfpr;	/* vertical front porch: lower margin (excl. vsync) */
	u32 vspr;	/* vertical sync width*/
};

struct device_info {
	int reset_gpio;
	struct mcde_port port;
	struct regulator *regulator;
	enum lvds_port_fmt lvds_fmt;
	struct lvds_video_mode vmode;
	bool ddr_is_requested;
	bool use_dsi_write;
	bool print_registers;
};

static inline struct device_info *get_drvdata(struct mcde_display_device *ddev)
{
	return (struct device_info *)dev_get_drvdata(&ddev->dev);
}

static int __devinit toshiba_dsi2lvds_i2c_probe(struct i2c_client *i2c_client,
						const struct i2c_device_id *id)
{
	struct device *dev;
	dev = &i2c_client->dev;

	i2cclient = i2c_client;

	dev_info(dev, "toshiba_dsi2lvds_i2c driver probed\n");

	return 0;
}

static const struct i2c_device_id toshiba_dsi2lvds_i2c_id[] = {
	{DSI2LVDS_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, toshiba_dsi2lvds_i2c_id);

static struct i2c_driver toshiba_dsi2lvds_i2c_driver = {
	.probe = toshiba_dsi2lvds_i2c_probe,
	.id_table = toshiba_dsi2lvds_i2c_id,
	.driver = {
		.name = DSI2LVDS_NAME,
	},
};

static int dsi_write_cmd(struct mcde_display_device *ddev, u16 cmd, u32 value)
{
	int ret;
	u8 param[6];

	param[0] = cmd & 0xFF;
	param[1] = (cmd >> 8) & 0xFF;
	param[2] = value & 0xFF;
	param[3] = (value >> 8) & 0xFF;
	param[4] = (value >> 16) & 0xFF;
	param[5] = (value >> 24) & 0xFF;
	ret = mcde_dsi_generic_write(ddev->chnl_state, param, 6);
	dev_dbg(&ddev->dev, "%s: cmd %x ret %d\n", __func__, cmd, ret);
	return ret;
}

static int i2c_read_cmd(struct mcde_display_device *ddev, u16 cmd, u8* data)
{
	struct i2c_msg msgs[2];
	int ret;
	u8 param[2];

	if (i2cclient == NULL)
		return -EINVAL;

	param[0] = (cmd & 0xFF00) >> 8;
	param[1] = cmd & 0xFF;

	msgs[0].addr = 0xF;
	msgs[0].len = 2;
	msgs[0].flags = 0;
	msgs[0].buf = param;

	msgs[1].addr = 0xF;
	msgs[1].len = 4;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = data;

	ret = i2c_transfer(i2cclient->adapter, msgs, 2);
	dev_dbg(&ddev->dev, "%s: cmd %x ret %d\n", __func__, cmd, ret);

	return 0;
}

static int i2c_write_cmd(struct mcde_display_device *ddev, u16 cmd, u32 value)
{
	struct i2c_msg msg;
	int ret;
	u8 param[6];

	param[0] = (cmd & 0xFF00) >> 8;
	param[1] = cmd & 0xFF;
	param[2] = value & 0xFF;
	param[3] = (value >> 8) & 0xFF;
	param[4] = (value >> 16) & 0xFF;
	param[5] = (value >> 24) & 0xFF;

	msg.addr = 0xF;
	msg.len = 6;
	msg.flags = 0;
	msg.buf = param;
	ret = i2c_transfer(i2cclient->adapter, &msg, 1);
	dev_dbg(&ddev->dev, "%s: cmd %x ret %d\n", __func__, cmd, ret);

	return 0;
}

static int write_cmd(struct mcde_display_device *ddev, u16 cmd, u32 value)
{
	struct device_info *di = get_drvdata(ddev);

	if (di->use_dsi_write)
		return dsi_write_cmd(ddev, cmd, value);
	else
		return i2c_write_cmd(ddev, cmd, value);
}

static u32 read_cmd(struct mcde_display_device *ddev, u16 cmd)
{
	u8 data[4] = {0};

	i2c_read_cmd(ddev, cmd, data);
	return ((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]);
}

static void dump_registers(struct mcde_display_device *ddev)
{
	dev_info(&ddev->dev, "DSI D-PHY Layer Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D0W_DPHYCONTTX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, CLW_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D0W_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D1W_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D2W_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D3W_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, COM_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, CLW_DPHYCONTRX));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, CLW_CNTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D0W_CNTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D1W_CNTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D2W_CNTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D3W_CNTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DFTMODE_CNTRL));

	dev_info(&ddev->dev, "DSI PPI Layer Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_STARTPPI));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_BUSYPPI));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_LINEINITCNT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_LPTXTIMECNT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_LANEENABLE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_TX_RX_TA));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_CLS_ATMR));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D0S_ATMR));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D1S_ATMR));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D2S_ATMR));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D3S_ATMR));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D0S_CLRSIPOCOUNT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D1S_CLRSIPOCOUNT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D2S_CLRSIPOCOUNT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_D3S_CLRSIPOCOUNT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, CLS_PRE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D0S_PRE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D1S_PRE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D2S_PRE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D3S_PRE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, CLS_PREP));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D0S_PREP));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D1S_PREP));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D2S_PREP));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D3S_PREP));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, CLS_ZERO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D0S_ZERO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D1S_ZERO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D2S_ZERO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, D3S_ZERO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_CLRFLG));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_CLRSIPO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_HSTimeout));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, PPI_HSTimeoutEnable));

	dev_info(&ddev->dev, "DSI Protocol Layer Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_STARTDSI));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_BUSYDSI));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_LANEENABLE));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_LANESTATUS0));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_LANESTATUS1));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_INTSTATUS));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_INTMASK));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_INTCLR));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSI_LPTXTO));

	dev_info(&ddev->dev, "DSI General Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DSIERRCNT));

	dev_info(&ddev->dev, "DSI Application Layer Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, APLCTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, RDPKTLN));

	dev_info(&ddev->dev, "Video Path Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, VPCTRL));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, HTIM1));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, HTIM2));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, HTIM1));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, VTIM2));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, VFUEN));

	dev_info(&ddev->dev, "LVDS Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX0003));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX0407));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX0811));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX1215));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX1619));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX2023));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVMX2427));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVCFG));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVPHY0));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, LVPHY1));

	dev_info(&ddev->dev, "System Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, SYSSTAT));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, SYSRST));

	dev_info(&ddev->dev, "GPIO Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, GPIOC));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, GPIOO));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, GPIOI));

	dev_info(&ddev->dev, "Chip Revision Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, IDREG));

	dev_info(&ddev->dev, "Debug Registers\n");
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DEBUG00));
	dev_info(&ddev->dev, "%8x\n", read_cmd(ddev, DEBUG01));
}

static int start_ppi_and_dsi(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	dev_dbg(&ddev->dev, "%s: data lanes %d\n", __func__,
					di->port.phy.dsi.num_data_lanes);

	switch (di->port.phy.dsi.num_data_lanes) {
	case 2:
		write_cmd(ddev, DSI_LANEENABLE, 0x00000007);
		write_cmd(ddev, PPI_LANEENABLE, 0x00000007);
		break;
	case 3:
		write_cmd(ddev, DSI_LANEENABLE, 0x0000000F);
		write_cmd(ddev, PPI_LANEENABLE, 0x0000000F);
		break;
	case 4:
		write_cmd(ddev, DSI_LANEENABLE, 0x0000001F);
		write_cmd(ddev, PPI_LANEENABLE, 0x0000001F);
		break;
	default:
		break;
	}

	write_cmd(ddev, PPI_STARTPPI, 0x00000001);
	write_cmd(ddev, DSI_STARTDSI, 0x00000001);

	return 0;
}

static int set_video_path_control(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	switch (di->lvds_fmt) {
	case LVDS_PORTPIXFMT_18BPP:
		/* EVTMODE = 1 VTGEN = 0 Magic square = 1*/
		write_cmd(ddev, VPCTRL, 0x00000021);
		/* Use default values from the bridge */
		dev_dbg(&ddev->dev, "%s: 18BPP\n", __func__);
		break;
	case LVDS_PORTPIXFMT_24BPP:
		/* EVTMODE (5) = 1 VTGEN (4) = 0 OPXLFMT (8)=1 (RGB888) */
		write_cmd(ddev, VPCTRL, 0x00000120);
		write_cmd(ddev, LVMX0003, 0x03020100);
		write_cmd(ddev, LVMX0407, 0x08050704);
		write_cmd(ddev, LVMX0811, 0x0F0E0A09);
		write_cmd(ddev, LVMX1215, 0x100D0C0B);
		write_cmd(ddev, LVMX1619, 0x12111716);
		write_cmd(ddev, LVMX2023, 0x1B151413);
		write_cmd(ddev, LVMX2427, 0x1B1A1918);
		dev_dbg(&ddev->dev, "%s: 24BPP\n", __func__);
		break;
	default:
		break;
	}
	return 0;
}

static int set_video_mode_parameters(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);
	u32 htim1;
	u32 htim2;
	u32 vtim1;
	u32 vtim2;

	htim1 = di->vmode.hpw | (di->vmode.hbpr << 16);
	htim2 = di->vmode.hdispr | (di->vmode.hfpr << 16);
	vtim1 = di->vmode.vspr | (di->vmode.vbpr << 16);
	vtim2 = di->vmode.vdispr | (di->vmode.vfpr << 16);

	dev_dbg(&ddev->dev, "%s: htim1 %x\n", __func__, htim1);
	dev_dbg(&ddev->dev, "%s: htim2 %x\n", __func__, htim2);
	dev_dbg(&ddev->dev, "%s: vtim1 %x\n", __func__, vtim1);
	dev_dbg(&ddev->dev, "%s: vtim2 %x\n", __func__, vtim2);

	write_cmd(ddev, HTIM1, htim1);
	write_cmd(ddev, HTIM2, htim2);
	write_cmd(ddev, VTIM1, vtim1);
	write_cmd(ddev, VTIM2, vtim2);

	return 0;
}

static int power_on(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);
	u8 data[4] = {0};
	u32 id;

	dev_dbg(&ddev->dev, "Reset & power on lvds_bridge display\n");

	regulator_enable(di->regulator);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);
	gpio_set_value_cansleep(di->reset_gpio, 0);
	udelay(RESET_DURATION_US);
	gpio_set_value_cansleep(di->reset_gpio, 1);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);

	/* Enable the DSI CLK */
	mcde_formatter_enable(ddev->chnl_state);
	usleep_range(RESET_DELAY_US, RESET_DELAY_US);

	i2c_read_cmd(ddev, IDREG, data);
	id = ((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]);
	dev_info(&ddev->dev, "DSI2LVDS BridgeID = 0x%8x\n", id);

	write_cmd(ddev, PPI_D0S_CLRSIPOCOUNT, 0x00000003);
	write_cmd(ddev, PPI_D1S_CLRSIPOCOUNT, 0x00000003);
	write_cmd(ddev, PPI_D2S_CLRSIPOCOUNT, 0x00000003);
	write_cmd(ddev, PPI_D3S_CLRSIPOCOUNT, 0x00000003);

	start_ppi_and_dsi(ddev);

	/* Disable PLL TEST mode */
	write_cmd(ddev, LVPHY1, 0x00000000);
	/* LV_IS = 01 LV_FS = 00 LV_ND = 00110 = 60 - 85 Mhz */
	write_cmd(ddev, LVPHY0, 0x00004006);

	set_video_path_control(ddev);

	set_video_mode_parameters(ddev);

	/* Enable LVDS transmitter and DCLK / 4 */
	write_cmd(ddev, LVCFG, 0x00000101);

	/* RSTLCD = 1 */
	write_cmd(ddev, SYSRST, 0x00000004);
	write_cmd(ddev, VFUEN, 0x00000001);

	if (di->print_registers)
		dump_registers(ddev);

	return 0;
}

static int power_off(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	dev_dbg(&ddev->dev, "Power off lvds_bridge display\n");

	regulator_disable(di->regulator);

	return 0;
}

static int display_on(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	dev_dbg(&ddev->dev, "Display on lvds_bridge\n");

	/*
	 * In DB8500 the DDR operating point is by default
	 * set to 25%. That's not enough to drive a display
	 * in video mode. To avoid flickering, DDR OPP is
	 * set to 50% at any time the display is on.
	 * Keep in mind that the operating point is relative
	 * to the full DDR capacity, so 50% DDR might not be
	 * enough in other products.
	 */
	if (!di->ddr_is_requested) {
		if (prcmu_qos_add_requirement(PRCMU_QOS_DDR_OPP,
					"ds2lvds", 50)) {
			dev_err(&ddev->dev, "add DDR OPP 50 failed\n");
			return -EFAULT;
		}
		di->ddr_is_requested = true;
	}

	return 0;
}

static int display_off(struct mcde_display_device *ddev)
{
	struct device_info *di = get_drvdata(ddev);

	dev_dbg(&ddev->dev, "Display off lvds_bridge\n");

	if (di->ddr_is_requested) {
		prcmu_qos_remove_requirement(PRCMU_QOS_DDR_OPP,
						"ds2lvds");
		di->ddr_is_requested = false;
	}

	return 0;
}

static int toshiba_dsi2lvds_set_power_mode(struct mcde_display_device *ddev,
					enum mcde_display_power_mode power_mode)
{
	int ret = 0;

	dev_dbg(&ddev->dev, "%s:Set Power mode\n", __func__);

	/* OFF -> STANDBY */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {
		ret = power_on(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_ON) {

		ret = display_on(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_ON;
	}
	/* ON -> STANDBY */
	else if (ddev->power_mode == MCDE_DISPLAY_PM_ON &&
					power_mode <= MCDE_DISPLAY_PM_STANDBY) {

		ret = display_off(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
					power_mode == MCDE_DISPLAY_PM_OFF) {
		ret = power_off(ddev);
		if (ret)
			return ret;
		ddev->power_mode = MCDE_DISPLAY_PM_OFF;
	}

	return mcde_chnl_set_power_mode(ddev->chnl_state, ddev->power_mode);
}

static int toshiba_dsi2lvds_try_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = 0;

	if (!ddev || !video_mode) {
		dev_warn(&ddev->dev,
			"%s: dev or video_mode equals NULL, aborting\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}
	/*
	 * pixel clock (Hz) > (VACT+VBP+VFP+VSA) * (HACT+HBP+HFP+HSA) *
	 *                    framerate * bpp / num_data_lanes * 1.1
	 */
	if (video_mode->xres == ddev->native_x_res ||
			video_mode->xres == ddev->native_y_res) {
		video_mode->vfp = 3;
		video_mode->vbp = 14;
		video_mode->vsw = 6;
		video_mode->hfp = 59;
		video_mode->hbp = 92;
		video_mode->hsw = 0;
		video_mode->interlaced = false;
		video_mode->pixclock = 1760;
	} else {
		dev_warn(&ddev->dev,
			"%s:Failed to find video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		ret = -EINVAL;
	}
out:
	return ret;
}

static int toshiba_dsi2lvds_set_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode)
{
	int ret = -EINVAL;
	struct mcde_video_mode channel_video_mode;

	if (!ddev || !video_mode) {
		dev_warn(&ddev->dev,
			"%s: dev or video_mode equals NULL, aborting\n",
			__func__);
		return ret;
	}

	ddev->video_mode = *video_mode;
	channel_video_mode = ddev->video_mode;
	/* Dependant on if display should rotate or MCDE should rotate */
	if (ddev->rotation == MCDE_DISPLAY_ROT_90_CCW ||
				ddev->rotation == MCDE_DISPLAY_ROT_90_CW) {
		channel_video_mode.xres = ddev->native_x_res;
		channel_video_mode.yres = ddev->native_y_res;
	}
	ret = mcde_chnl_set_video_mode(ddev->chnl_state, &channel_video_mode);
	if (ret < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode\n", __func__);
		return ret;
	}

	ddev->update_flags |= UPDATE_FLAG_VIDEO_MODE;
	return ret;
}

static int toshiba_dsi2lvds_set_rotation(struct mcde_display_device *ddev,
					enum mcde_display_rotation rotation)
{
	int ret;
	enum mcde_display_rotation final;

	final = (360 + rotation - ddev->orientation) % 360;
	ret = mcde_chnl_set_rotation(ddev->chnl_state, final);
	if (WARN_ON(ret))
		return ret;

	WARN_ON(final == MCDE_DISPLAY_ROT_180);

	ddev->rotation = rotation;
	ddev->update_flags |= UPDATE_FLAG_ROTATION;

	return 0;
}

static int __devinit toshiba_dsi2lvds_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct mcde_display_dsi_platform_data *pdata = ddev->dev.platform_data;
	struct device_info *di;
	struct mcde_port *port;

	if (pdata == NULL || !pdata->reset_gpio) {
		dev_err(&ddev->dev, "Invalid platform data\n");
		return -EINVAL;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	port = ddev->port;
	di->reset_gpio = pdata->reset_gpio;
	di->port.link = pdata->link;
	di->port.type = MCDE_PORTTYPE_DSI;
	di->port.mode = MCDE_PORTMODE_VID;
	di->port.pixel_format = MCDE_PORTPIXFMT_DSI_24BPP;
	di->port.sync_src = port->sync_src;
	di->port.frame_trig = port->frame_trig;
	di->port.phy.dsi.num_data_lanes = pdata->num_data_lanes;
	di->port.phy.dsi.host_eot_gen = true;
	di->port.update_auto_trig = true;
	di->port.phy.dsi.vid_wakeup_time = 72;
	di->port.phy.dsi.vid_mode = NON_BURST_MODE_WITH_SYNC_EVENT;
	di->port.phy.dsi.clk_cont = true;
	di->port.phy.dsi.hs_freq = DSI_HS_FREQ_HZ;
	di->port.phy.dsi.lp_freq = DSI_LP_FREQ_HZ;

	/* The panel only supports 262k colors */
	di->lvds_fmt = LVDS_PORTPIXFMT_18BPP;
	/* Settings for the bridge configuration */
	di->vmode.hdispr = 1280;
	di->vmode.vdispr = 800;
	di->vmode.hbpr = 80;
	di->vmode.hfpr = 40;
	di->vmode.hpw = 32;
	di->vmode.vbpr = 14;
	di->vmode.vfpr = 3;
	di->vmode.vspr = 6;

	ret = gpio_request(di->reset_gpio, NULL);
	if (ret)
		goto gpio_request_failed;
	gpio_direction_output(di->reset_gpio, 1);
	di->regulator = regulator_get(&ddev->dev, IO_REGU);
	if (IS_ERR(di->regulator)) {
		di->regulator = NULL;
		goto regulator_get_failed;
	}
	ret = regulator_set_voltage(di->regulator, IO_REGU_MIN, IO_REGU_MAX);
	if (WARN_ON(ret))
		goto regulator_voltage_failed;
	ddev->set_power_mode = toshiba_dsi2lvds_set_power_mode;
	ddev->try_video_mode = toshiba_dsi2lvds_try_video_mode;
	ddev->set_video_mode = toshiba_dsi2lvds_set_video_mode;
	ddev->set_rotation = toshiba_dsi2lvds_set_rotation;
	ddev->port = &di->port;
	ddev->native_x_res = 1280;
	ddev->native_y_res = 800;
	dev_set_drvdata(&ddev->dev, di);

	dev_info(&ddev->dev, "toshiba_dsi2lvds display probed\n");

	return 0;
regulator_voltage_failed:
	regulator_put(di->regulator);
regulator_get_failed:
	gpio_free(di->reset_gpio);
gpio_request_failed:
	kfree(di);
	return ret;
}

static struct mcde_display_driver toshiba_dsi2lvds_driver = {
	.probe	= toshiba_dsi2lvds_probe,
	.driver = {
		.name	= "toshiba_dsi2lvds",
	},
};

static int __init toshiba_dsi2lvds_init(void)
{
	i2c_add_driver(&toshiba_dsi2lvds_i2c_driver);
	return mcde_display_driver_register(&toshiba_dsi2lvds_driver);
}

module_init(toshiba_dsi2lvds_init);

MODULE_DESCRIPTION("Toshiba dsi2lvds driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jimmy Rubin <jimmy.rubin@stericsson.com>");
