/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson HDMI display driver
 *
 * Author: Per Persson <per-xb-persson@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/compdev.h>
#include <linux/clonedev.h>

#include <video/mcde_fb.h>
#include <video/mcde_display.h>
#include <video/mcde_display-av8100.h>
#include <video/av8100.h>
#include <video/hdmi.h>

#define SWITCH_HELPSTR ", 0=HDMI, 1=SDTV, 2=DVI\n"

#ifdef CONFIG_DISPLAY_AV8100_TRIPPLE_BUFFER
#define NUM_FB_BUFFERS 3
#else
#define NUM_FB_BUFFERS 2
#endif

#define DSI_HS_FREQ_HZ 840320000
#define DSI_LP_FREQ_HZ 19200000

#define SIDE_SIDE_3D		0x01
#define TOP_BOTTOM_3D		0x02
#define FRAME_PACKING_3D	0x04

struct cea_vesa_video_mode {
	u32 cea;
	u32 vesa_cea_nr;
	u8 threed_format;
	struct mcde_video_mode *video_mode;
};

static int hdmi_try_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode);
static int hdmi_set_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode);
static int hdmi_set_pixel_format(
	struct mcde_display_device *ddev, enum mcde_ovly_pix_fmt format);
static struct mcde_video_mode *video_mode_get(struct mcde_display_device *ddev,
		u8 cea, u8 vesa_cea_nr);
static int ceanr_convert(struct mcde_display_device *ddev,
					u8 cea, u8 vesa_cea_nr, u16 *w, u16 *h);

static ssize_t show_hdmisdtvswitch(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_hdmisdtvswitch(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_input_pixel_format(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_input_pixel_format(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_disponoff(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_disponoff(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_vesacea(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_timing(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t store_timing(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t store_stayalive(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR(disponoff, S_IRUGO | S_IWUSR, show_disponoff,
		store_disponoff);
static DEVICE_ATTR(vesacea, S_IRUGO, show_vesacea, NULL);
static DEVICE_ATTR(timing, S_IRUGO | S_IWUSR, show_timing, store_timing);
static DEVICE_ATTR(stayalive, S_IWUSR, NULL, store_stayalive);

static DEVICE_ATTR(hdmisdtvswitch, S_IRUGO | S_IWUSR, show_hdmisdtvswitch,
			store_hdmisdtvswitch);
static DEVICE_ATTR(input_pixel_format, S_IRUGO | S_IWUSR,
			show_input_pixel_format, store_input_pixel_format);

static ssize_t show_hdmisdtvswitch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcde_display_device *mdev = to_mcde_display_device(dev);
	int index;

	dev_dbg(dev, "%s\n", __func__);

	sprintf(buf, "%1x%s", mdev->port->hdmi_sdtv_switch, SWITCH_HELPSTR);
	index = 1 + strlen(SWITCH_HELPSTR) + 1;

	return index;
}

static ssize_t store_hdmisdtvswitch(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mcde_display_device *mdev = to_mcde_display_device(dev);
	dev_dbg(dev, "%s\n", __func__);

	if (count > 0) {
		if ((*buf == 0) || (*buf == '0')) {
			dev_dbg(dev, "hdmi/sdtv switch = hdmi\n");
			mdev->port->hdmi_sdtv_switch = HDMI_SWITCH;
			mdev->native_x_res = NATIVE_XRES_HDMI;
			mdev->native_y_res = NATIVE_YRES_HDMI;
		} else if ((*buf == 1) || (*buf == '1')) {
			dev_dbg(dev, "hdmi/sdtv switch = sdtv\n");
			mdev->port->hdmi_sdtv_switch = SDTV_SWITCH;
			mdev->native_x_res = NATIVE_XRES_SDTV;
			mdev->native_y_res = NATIVE_YRES_SDTV;
		} else if ((*buf == 2) || (*buf == '2')) {
			dev_dbg(dev, "hdmi/sdtv switch = dvi\n");
			mdev->port->hdmi_sdtv_switch = DVI_SWITCH;
			mdev->native_x_res = NATIVE_XRES_HDMI;
			mdev->native_y_res = NATIVE_YRES_HDMI;
		}
		/* implicitely read by a memcmp in dss */
		mdev->video_mode.force_update = true;
	}

	return count;
}

static ssize_t show_input_pixel_format(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);

	return sprintf(buf, "%d\n", ddev->port->pixel_format);
}

static ssize_t store_input_pixel_format(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);
	struct display_driver_data *driver_data = dev_get_drvdata(&ddev->dev);

	dev_dbg(dev, "%s\n", __func__);
	if (count > 0) {
		unsigned long input;
		if (strict_strtoul(buf, 10, &input) != 0)
			return -EINVAL;
		switch (input) {
		/* intentional fall through */
		case MCDE_PORTPIXFMT_DSI_16BPP:
		case MCDE_PORTPIXFMT_DSI_18BPP:
		case MCDE_PORTPIXFMT_DSI_18BPP_PACKED:
		case MCDE_PORTPIXFMT_DSI_24BPP:
		case MCDE_PORTPIXFMT_DSI_YCBCR422:
			ddev->port->pixel_format = input;
			break;
		default:
			dev_warn(&ddev->dev, "invalid format (%ld)\n",
								input);
			return -EINVAL;
			break;
		}
		/* implicitely read by a memcmp in dss */
		ddev->video_mode.force_update = true;
		driver_data->update_port_pixel_format = true;
	}

	return count;
}

static ssize_t show_disponoff(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);
	struct display_driver_data *driver_data = dev_get_drvdata(&ddev->dev);

	dev_dbg(dev, "%s\n", __func__);

	if (driver_data->fbdevname) {
		dev_dbg(dev, "name:%s\n", driver_data->fbdevname);
		strcpy(buf, driver_data->fbdevname);
		return strlen(driver_data->fbdevname) + 1;
	}
	return 0;
}

static ssize_t store_disponoff(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mcde_display_device *mdev = to_mcde_display_device(dev);
	bool enable = false;
	u8 cea = 0;
	u8 vesa_cea_nr = 0;
	struct display_driver_data *driver_data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	if ((count != DISPONOFF_SIZE) && (count != DISPONOFF_SIZE + 1))
		return -EINVAL;

	if ((*buf == '0') && (*(buf + 1) == '1'))
		enable = true;
	cea = (hex_to_bin(buf[2]) << 4) + hex_to_bin(buf[3]);
	vesa_cea_nr = (hex_to_bin(buf[4]) << 4) + hex_to_bin(buf[5]);
	dev_dbg(dev, "enable:%d cea:%d nr:%d\n", enable, cea, vesa_cea_nr);

	if (enable && !driver_data->fbdevname) {
		u16 w = mdev->native_x_res;
		u16 h = mdev->native_y_res;
		struct compdev *cd;

		driver_data->video_mode = video_mode_get(mdev, cea,
								vesa_cea_nr);
		if (cea)
			driver_data->cea_nr = vesa_cea_nr;
		else
			driver_data->cea_nr = 0;

		ceanr_convert(mdev, cea, vesa_cea_nr, &w, &h);

#ifdef CONFIG_COMPDEV
		/* TODO need another way for compdev to get actual size */
		mdev->native_x_res = w;
		mdev->native_y_res = h;

		/* Create a compdev overlay for this display */
		if (compdev_create(mdev, NULL, false, &cd) < 0) {
			dev_warn(&mdev->dev,
				"Failed to create compdev for display %s\n",
						mdev->name);
		} else {
			dev_dbg(&mdev->dev, "compdev created for (%s)\n",
						mdev->name);
			driver_data->fbdevname = compdev_get_device_name(cd);
		}
#ifdef CONFIG_CLONEDEV
		if (clonedev_create()) {
			dev_warn(&mdev->dev,
				"Failed to create clonedev for display %s\n",
						mdev->name);
		} else {
			dev_dbg(&mdev->dev, "clonedev created for (%s)\n",
						mdev->name);
		}
#endif
#endif
	} else if (!enable && driver_data->fbdevname) {
#ifdef CONFIG_CLONEDEV
		clonedev_destroy();
#endif
#ifdef CONFIG_COMPDEV
		compdev_destroy(mdev);
#endif
		driver_data->fbdevname = NULL;
	}
	return count;
}

static ssize_t show_timing(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);
	struct display_driver_data *driver_data = dev_get_drvdata(&ddev->dev);
	struct mcde_video_mode *video_mode;
	int index;

	dev_dbg(dev, "%s\n", __func__);

	index = 0;
	if (driver_data->video_mode) {
		video_mode = driver_data->video_mode;
		memcpy(buf + index, &video_mode->xres, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->yres, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->pixclock, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->hbp, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->hfp, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->vbp, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->vfp, sizeof(u32));
		index += sizeof(u32);
		memcpy(buf + index, &video_mode->interlaced, sizeof(u32));
		index += sizeof(u32);
	}
	return index;
}

static ssize_t store_timing(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);
	struct display_driver_data *driver_data = dev_get_drvdata(&ddev->dev);

	dev_dbg(dev, "%s\n", __func__);

	if (count != TIMING_SIZE)
		return -EINVAL;

	driver_data->video_mode = video_mode_get(ddev, *buf, *(buf + 1));
	if (*buf)
		driver_data->cea_nr = *(buf + 1);
	else
		driver_data->cea_nr = 0;

	return count;
}

static ssize_t store_stayalive(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mcde_display_device *ddev = to_mcde_display_device(dev);

	if (count != STAYALIVE_SIZE)
		return -EINVAL;

	if ((*buf == 1) || (*buf == '1'))
		ddev->stay_alive = true;
	else
		ddev->stay_alive = false;

	dev_dbg(dev, "%s %d\n", __func__, ddev->stay_alive);

	return count;
}

static int ceanr_convert(struct mcde_display_device *ddev,
			u8 cea, u8 vesa_cea_nr, u16 *w, u16 *h)
{
	struct mcde_video_mode *video_mode;

	dev_dbg(&ddev->dev, "%s\n", __func__);
	video_mode = video_mode_get(ddev, cea, vesa_cea_nr);
	if (video_mode) {
		*w = video_mode->xres;
		*h = video_mode->yres;
		dev_dbg(&ddev->dev, "cea:%d nr:%d found\n",
				cea, vesa_cea_nr);
		return 0;
	}

	return -EINVAL;
}

/* Supported HDMI modes */
static struct mcde_video_mode video_modes_supp_hdmi[] = {
	/* 0 CEA #1 640_480_60_P */
	{
		.xres = 640,	.yres = 480,
		.pixclock = 39682,
		.hbp = 112,	.hfp = 48,
		.vbp = 33,	.vfp = 12
	},
	/* 1 720_480_60_P */
	{
		.xres = 720,	.yres = 480,
		.pixclock = 37000,
		.hbp = 104,	.hfp = 34,
		.vbp = 30,	.vfp = 15
	},
	/* 2 720_576_50_P */
	{
		.xres = 720,	.yres = 576,
		.pixclock = 37037,
		.hbp = 132,	.hfp = 12,
		.vbp = 44,	.vfp = 5
	},
	/* 3 1280_720_60_P */
	{
		.xres = 1280,	.yres = 720,
		.pixclock = 13468,
		.hbp = 256,	.hfp = 114,
		.vbp = 20,	.vfp = 10
	},
	/* 4 1280_720_50_P */
	{
		.xres = 1280,	.yres = 720,
		.pixclock = 13468,
		.hbp = 260,	.hfp = 440,
		.vbp = 25,	.vfp = 5
	},
	/* 5 1280_720_30_P */
	{
		.xres = 1280,	.yres = 720,
		.pixclock = 13468,
		.hbp = 260,	.hfp = 1760,
		.vbp = 20,	.vfp = 10
	},
	/* 6 1280_720_24_P */
	{
		.xres = 1280,	.yres = 720,
		.pixclock = 16835,
		.hbp = 260,	.hfp = 1760,
		.vbp = 20,	.vfp = 10
	},
	/* 7 1280_720_25_P */
	{
		.xres = 1280,	.yres = 720,
		.pixclock = 13468,
		.hbp = 260,	.hfp = 2420,
		.vbp = 20,	.vfp = 10
	},
	/* 8 1920_1080_30_P */
	{
		.xres = 1920,	.yres = 1080,
		.pixclock = 13468,
		.hbp = 189,	.hfp = 91,
		.vbp = 36,	.vfp = 9
	},
	/* 9 1920_1080_24_P */
	{
		.xres = 1920,	.yres = 1080,
		.pixclock = 13468,
		.hbp = 170,	.hfp = 660,
		.vbp = 36,	.vfp = 9
	},
	/* 10 1920_1080_25_P */
	{
		.xres = 1920,	.yres = 1080,
		.pixclock = 13468,
		.hbp = 192,	.hfp = 528,
		.vbp = 36,	.vfp = 9
	},
	/* 11 720_480_60_I */
	{
		.xres = 720,	.yres = 480,
		.pixclock = 74074,
		.hbp = 126,	.hfp = 12,
		.vbp = 44,	.vfp = 1,
		.interlaced = true,
	},
	/* 12 720_576_50_I */
	{
		.xres = 720,	.yres = 576,
		.pixclock = 74074,
		.hbp = 132,	.hfp = 12,
		.vbp = 44,	.vfp = 5,
		.interlaced = true,
	},
	/* 13 1920_1080_50_I */
	{
		.xres = 1920,	.yres = 1080,
		.pixclock = 13468,
		.hbp = 192,	.hfp = 528,
		.vbp = 20,	.vfp = 25,
		.interlaced = true,
	},
	/* 14 1920_1080_60_I */
	{
		.xres = 1920,	.yres = 1080,
		.pixclock = 13468,
		.hbp = 192,	.hfp = 88,
		.vbp = 20,	.vfp = 25,
		.interlaced = true,
	},
	/* 15 VESA #9 800_600_60_P */
	{
		.xres = 800,	.yres = 600,
		.pixclock = 25000,
		.hbp = 168,	.hfp = 88,
		.vbp = 23,	.vfp = 5,
		.interlaced = false,
	},
	/* 16 VESA #14 848_480_60_P */
	{
		.xres = 848,	.yres = 480,
		.pixclock = 29630,
		.hbp = 128,	.hfp = 112,
		.vbp = 23,	.vfp = 14,
		.interlaced = false,
	},
	/* 17 VESA #16 1024_768_60_P */
	{
		.xres = 1024,	.yres = 768,
		.pixclock = 15385,
		.hbp = 160,	.hfp = 160,
		.vbp = 29,	.vfp = 9,
		.interlaced = false,
	},
	/* 18 VESA #22 1280_768_60_P */
	{
		.xres = 1280,	.yres = 768,
		.pixclock = 14652,
		.hbp = 80,	.hfp = 80,
		.vbp = 12,	.vfp = 10,
		.interlaced = false,
	},
	/* 19 VESA #23 1280_768_60_P */
	{
		.xres = 1280,	.yres = 768,
		.pixclock = 12579,
		.hbp = 192,	.hfp = 192,
		.vbp = 20,	.vfp = 10,
		.interlaced = false,
	},
	/* 20 VESA #27 1280_800_60_P */
	{
		.xres = 1280,	.yres = 800,
		.pixclock = 14085,
		.hbp = 80,	.hfp = 80,
		.vbp = 14,	.vfp = 9,
		.interlaced = false,
	},
	/* 21 VESA #28 1280_800_60_P */
	{
		.xres = 1280,	.yres = 800,
		.pixclock = 11976,
		.hbp = 200,	.hfp = 200,
		.vbp = 22,	.vfp = 9,
		.interlaced = false,
	},
	/* 22 VESA #39 1360_768_60_P */
	{
		.xres = 1360,	.yres = 768,
		.pixclock = 11696,
		.hbp = 176,	.hfp = 256,
		.vbp = 18,	.vfp = 9,
		.interlaced = false,
	},
	/* 23 VESA #81 1366_768_60_P */
	{
		.xres = 1360,	.yres = 768,
		.pixclock = 11764,
		.hbp = 208,	.hfp = 208,
		.vbp = 25,	.vfp = 5,
		.interlaced = false,
	},
};

/* Supported TVout modes */
static struct mcde_video_mode video_modes_supp_sdtv[] = {
	/* 720_480_60_I) */
	{
		.xres = 720,	.yres = 480,
		.pixclock = 74074,
		.hbp = 126,	.hfp = 12,
		.vbp = 44,	.vfp = 1,
		.interlaced = true,
	},
	/* 720_576_50_I) */
	{
		.xres = 720,	.yres = 576,
		.pixclock = 74074,
		.hbp = 132,	.hfp = 12,
		.vbp = 44,	.vfp = 5,
		.interlaced = true,
	},
};

static struct cea_vesa_video_mode cea_vesa_video_mode[] = {
		/* 640_480_60_P */
		{
			.cea = 1,	.vesa_cea_nr = 1,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[0],
		},
		/* 720_480_60_P */
		{
			.cea = 1,	.vesa_cea_nr = 2,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[1],
		},
		/* 720_480_60_P */
		{
			.cea = 1,	.vesa_cea_nr = 3,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[1],
		},
		/* 720_576_50_P */
		{
			.cea = 1,	.vesa_cea_nr = 17,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[2],
		},
		/* 720_576_50_P */
		{
			.cea = 1,	.vesa_cea_nr = 18,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[2],
		},
		/* 1280_720_60_P */
		{
			.cea = 1,	.vesa_cea_nr = 4,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[3],
		},
		/* 1280_720_50_P */
		{
			.cea = 1,	.vesa_cea_nr = 19,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[4],
		},
		/* 1280_720_30_P */
		{
			.cea = 1,	.vesa_cea_nr = 62,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[5],
		},
		/* 1280_720_24_P */
		{
			.cea = 1,	.vesa_cea_nr = 60,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[6],
		},
		/* 1280_720_25_P */
		{
			.cea = 1,	.vesa_cea_nr = 61,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[7],
		},
		/* 1920_1080_30_P */
		{
			.cea = 1,	.vesa_cea_nr = 34,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[8],
		},
		/* 1920_1080_24_P */
		{
			.cea = 1,	.vesa_cea_nr = 32,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[9],
		},
		/* 1920_1080_25_P */
		{
			.cea = 1,	.vesa_cea_nr = 33,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[10],
		},
		/* 720_480_60_I) */
		{
			.cea = 1,	.vesa_cea_nr = 6,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[11],
		},
		/* 720_480_60_I) */
		{
			.cea = 1,	.vesa_cea_nr = 7,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[11],
		},
		/* 720_576_50_I) */
		{
			.cea = 1,	.vesa_cea_nr = 21,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[12],
		},
		/* 720_576_50_I) */
		{
			.cea = 1,	.vesa_cea_nr = 22,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[12],
		},
		/* 1920_1080_50_I) */
		{
			.cea = 1,	.vesa_cea_nr = 20,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[13],
		},
		/* 1920_1080_60_I) */
		{
			.cea = 1,	.vesa_cea_nr = 5,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[14],
		},
		/* VESA #4 640_480_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 4,
			.threed_format = SIDE_SIDE_3D | TOP_BOTTOM_3D,
			.video_mode = &video_modes_supp_hdmi[0],
		},
		/* VESA #9 800_600_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 9,
			.video_mode = &video_modes_supp_hdmi[15],
		},
		/* VESA #14 848_480_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 14,
			.video_mode = &video_modes_supp_hdmi[16],
		},
		/* VESA #16 1024_768_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 16,
			.video_mode = &video_modes_supp_hdmi[17],
		},
		/* VESA #22 1280_768_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 22,
			.video_mode = &video_modes_supp_hdmi[18],
		},
		/* VESA #23 1280_768_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 23,
			.video_mode = &video_modes_supp_hdmi[19],
		},
		/* VESA #27 1280_800_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 27,
			.video_mode = &video_modes_supp_hdmi[20],
		},
		/* VESA #28 1280_800_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 28,
			.video_mode = &video_modes_supp_hdmi[21],
		},
		/* VESA #39 1360_768_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 39,
			.video_mode = &video_modes_supp_hdmi[22],
		},
		/* VESA #81 1366_768_60_P) */
		{
			.cea = 0,	.vesa_cea_nr = 81,
			.video_mode = &video_modes_supp_hdmi[23],
		},
};

static ssize_t show_vesacea(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int findex;

	dev_dbg(dev, "%s\n", __func__);

	for (findex = 0; findex < ARRAY_SIZE(cea_vesa_video_mode); findex++) {
		*(buf + findex * 3) = cea_vesa_video_mode[findex].cea;
		*(buf + findex * 3 + 1) =
				cea_vesa_video_mode[findex].vesa_cea_nr;
		*(buf + findex * 3 + 2) =
				cea_vesa_video_mode[findex].threed_format;
	}
	*(buf + findex * 3) = '\0';

	return findex * 3 + 1;
}

static struct mcde_video_mode *video_mode_get(struct mcde_display_device *ddev,
		u8 cea, u8 vesa_cea_nr)
{
	int findex;

	dev_dbg(&ddev->dev, "%s\n", __func__);

	for (findex = 0; findex < ARRAY_SIZE(cea_vesa_video_mode); findex++)
		if ((cea == cea_vesa_video_mode[findex].cea) &&
				(vesa_cea_nr ==
				cea_vesa_video_mode[findex].vesa_cea_nr)) {
			dev_dbg(&ddev->dev, "cea:%d nr:%d\n", cea, vesa_cea_nr);
			return cea_vesa_video_mode[findex].video_mode;
		}

	return NULL;
}

#define AV8100_MAX_LEVEL 255

static int hdmi_try_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode)
{
	int index = 0;
	int match_level = AV8100_MAX_LEVEL;
	int found_index = -1;
	struct mcde_video_mode *video_modes_supp;
	int array_size;

	if (ddev == NULL || video_mode == NULL) {
		pr_warning("%s:ddev = NULL or video_mode = NULL\n", __func__);
		return -EINVAL;
	}

	dev_vdbg(&ddev->dev, "%s\n", __func__);

	if (ddev->port->hdmi_sdtv_switch == SDTV_SWITCH) {
		video_mode->interlaced = true;
		video_modes_supp = video_modes_supp_sdtv;
		array_size = ARRAY_SIZE(video_modes_supp_sdtv);
	} else {
		video_modes_supp = video_modes_supp_hdmi;
		array_size = ARRAY_SIZE(video_modes_supp_hdmi);
	}

	while (index < array_size) {
		/* 1. Check if all parameters match */
		if ((video_mode->xres == video_modes_supp[index].xres) &&
			(video_mode->yres == video_modes_supp[index].yres) &&
			((video_mode->xres + video_mode->hbp +
				video_mode->hfp) ==
			(video_modes_supp[index].xres +
				video_modes_supp[index].hbp +
				video_modes_supp[index].hfp)) &&
			((video_mode->yres + video_mode->vbp + video_mode->vfp)
				==
			(video_modes_supp[index].yres +
				video_modes_supp[index].vbp +
				video_modes_supp[index].vfp)) &&
			(video_mode->pixclock ==
				video_modes_supp[index].pixclock) &&
			(video_mode->interlaced ==
				video_modes_supp[index].interlaced)) {
					match_level = 1;
					found_index = index;
					break;
		}

		/* 2. Check if xres,yres,htot,vtot,interlaced match */
		if ((match_level > 2) &&
			(video_mode->xres == video_modes_supp[index].xres) &&
			(video_mode->yres == video_modes_supp[index].yres) &&
			((video_mode->xres + video_mode->hbp +
				video_mode->hfp) ==
			(video_modes_supp[index].xres +
				video_modes_supp[index].hbp +
				video_modes_supp[index].hfp)) &&
			((video_mode->yres + video_mode->vbp + video_mode->vfp)
				==
			(video_modes_supp[index].yres +
				video_modes_supp[index].vbp +
				video_modes_supp[index].vfp)) &&
			(video_mode->interlaced ==
				video_modes_supp[index].interlaced)) {
			match_level = 2;
			found_index = index;
		}

		/* 3. Check if xres,yres,pixelclock,interlaced match */
		if ((match_level > 3) &&
			(video_mode->xres == video_modes_supp[index].xres) &&
			(video_mode->yres == video_modes_supp[index].yres) &&
			(video_mode->interlaced ==
				video_modes_supp[index].interlaced) &&
			(video_mode->pixclock ==
				video_modes_supp[index].pixclock)) {
			match_level = 3;
			found_index = index;
		}

		/* 4. Check if xres,yres,interlaced match */
		if ((match_level > 4) &&
			(video_mode->xres == video_modes_supp[index].xres) &&
			(video_mode->yres == video_modes_supp[index].yres) &&
			(video_mode->interlaced ==
				video_modes_supp[index].interlaced)) {
			match_level = 4;
			found_index = index;
		}

		index++;
	}

	if (found_index == -1) {
		dev_dbg(&ddev->dev, "video_mode not accepted\n");
		dev_dbg(&ddev->dev, "xres:%d yres:%d pixclock:%d hbp:%d hfp:%d "
			"vfp:%d vbp:%d intlcd:%d\n",
			video_mode->xres, video_mode->yres,
			video_mode->pixclock,
			video_mode->hbp, video_mode->hfp,
			video_mode->vfp, video_mode->vbp,
			video_mode->interlaced);
		return -EINVAL;
	}

	memset(video_mode, 0, sizeof(struct mcde_video_mode));
	memcpy(video_mode, &video_modes_supp[found_index],
		sizeof(struct mcde_video_mode));

	dev_dbg(&ddev->dev, "%s:HDMI video_mode %d chosen. Level:%d\n",
		__func__, found_index, match_level);

	return 0;
}

static int hdmi_set_video_mode(
	struct mcde_display_device *dev, struct mcde_video_mode *video_mode)
{
	int ret;
	union av8100_configuration av8100_config;
	struct mcde_display_hdmi_platform_data *pdata;
	struct display_driver_data *driver_data;
	struct av8100_status status;

	/* TODO check video_mode_params */
	if (dev == NULL || video_mode == NULL) {
		pr_warning("%s:ddev = NULL or video_mode = NULL\n", __func__);
		return -EINVAL;
	}

	pdata = dev->dev.platform_data;
	driver_data = dev_get_drvdata(&dev->dev);

	dev_dbg(&dev->dev, "%s:\n", __func__);
	dev_vdbg(&dev->dev, "%s:xres:%d yres:%d hbp:%d hfp:%d vbp:%d vfp:%d "
		"interlaced:%d\n", __func__,
		video_mode->xres,
		video_mode->yres,
		video_mode->hbp,
		video_mode->hfp,
		video_mode->vbp,
		video_mode->vfp,
		video_mode->interlaced);

	if (driver_data->update_port_pixel_format) {
		hdmi_set_pixel_format(dev, dev->pixel_format);
		driver_data->update_port_pixel_format = false;
	}

	dev->video_mode = *video_mode;

	if (dev->port->pixel_format == MCDE_PORTPIXFMT_DSI_YCBCR422 &&
						pdata->rgb_2_yCbCr_transform)
		mcde_chnl_set_col_convert(dev->chnl_state,
						pdata->rgb_2_yCbCr_transform,
						MCDE_CONVERT_RGB_2_YCBCR);
	mcde_chnl_stop_flow(dev->chnl_state);

	ret = mcde_chnl_set_video_mode(dev->chnl_state, &dev->video_mode);
	if (ret < 0) {
		dev_warn(&dev->dev, "Failed to set video mode\n");
		return ret;
	}

	status = av8100_status_get();
	if (status.av8100_state == AV8100_OPMODE_UNDEFINED)
		return -EINVAL;

	if (av8100_ver_get() == AV8100_CHIPVER_1) {
		if (status.av8100_state >= AV8100_OPMODE_STANDBY) {
			/* Disable interrupts */
			ret = av8100_disable_interrupt();
			if (ret) {
				dev_err(&dev->dev,
					"%s:av8100_disable_interrupt failed\n",
					__func__);
				return ret;
			}

			ret = av8100_powerdown();
			if (ret) {
				dev_err(&dev->dev,
						"av8100_powerdown failed\n");
				return ret;
			}

			usleep_range(10000, 15000);
		}
	}

	/* Set to powerup with interrupts disabled */
	status = av8100_status_get();
	if (status.av8100_state < AV8100_OPMODE_STANDBY) {
		ret = av8100_powerup();
		if (ret) {
			dev_err(&dev->dev, "av8100_powerup failed\n");
			return ret;
		}
	}

	if (status.av8100_state <= AV8100_OPMODE_INIT) {
		ret = av8100_download_firmware(I2C_INTERFACE);
		if (ret) {
			dev_err(&dev->dev, "av8100_download_firmware failed\n");
			return ret;
		}
	}

	av8100_conf_lock();

	/*
	 * Don't look at dev->port->hdmi_sdtv_switch; it states only which
	 * one should be started, not which one is currently working
	 */
	if (av8100_conf_get(AV8100_COMMAND_HDMI, &av8100_config)) {
		ret = -EFAULT;
		goto hdmi_set_video_mode_end;
	}
	if (av8100_config.hdmi_format.hdmi_mode == AV8100_HDMI_ON) {
		/* Set HDMI mode to OFF */
		av8100_config.hdmi_format.hdmi_mode = AV8100_HDMI_OFF;
		av8100_config.hdmi_format.dvi_format = AV8100_DVI_CTRL_CTL0;
		av8100_config.hdmi_format.hdmi_format = AV8100_HDMI;
		if (av8100_conf_prep(AV8100_COMMAND_HDMI, &av8100_config)) {
			ret = -EFAULT;
			goto hdmi_set_video_mode_end;
		}

		if (av8100_conf_w(AV8100_COMMAND_HDMI, NULL, NULL,
							I2C_INTERFACE)) {
			ret = -EFAULT;
			goto hdmi_set_video_mode_end;
		}
	}
	if (av8100_conf_get(AV8100_COMMAND_DENC, &av8100_config)) {
		ret = -EFAULT;
		goto hdmi_set_video_mode_end;
	}
	if (av8100_config.denc_format.enable) {
		/* Turn off DENC */
		av8100_config.denc_format.enable = 0;
		if (av8100_conf_prep(AV8100_COMMAND_DENC, &av8100_config)) {
			ret = -EFAULT;
			goto hdmi_set_video_mode_end;
		}
		if (av8100_conf_w(AV8100_COMMAND_DENC, NULL, NULL,
							I2C_INTERFACE)) {
			ret = -EFAULT;
			goto hdmi_set_video_mode_end;
		}
	}

	/* Get current av8100 video output format */
	ret = av8100_conf_get(AV8100_COMMAND_VIDEO_OUTPUT_FORMAT,
		&av8100_config);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_get "
			"AV8100_COMMAND_VIDEO_OUTPUT_FORMAT failed\n",
			__func__);
		goto hdmi_set_video_mode_end;
	}

	if (dev->port->hdmi_sdtv_switch == SDTV_SWITCH)
		av8100_config.video_output_format.video_output_cea_vesa =
			dev->video_mode.yres == NATIVE_YRES_SDTV ?
				AV8100_CEA21_22_576I_PAL_50HZ :
				AV8100_CEA6_7_NTSC_60HZ;
	else
		av8100_config.video_output_format.video_output_cea_vesa =
			av8100_video_output_format_get(
				dev->video_mode.xres,
				dev->video_mode.yres,
				dev->video_mode.xres +
				dev->video_mode.hbp + dev->video_mode.hfp,
				dev->video_mode.yres +
				dev->video_mode.vbp + dev->video_mode.vfp,
				dev->video_mode.pixclock,
				dev->video_mode.interlaced);

	if (AV8100_VIDEO_OUTPUT_CEA_VESA_MAX ==
		av8100_config.video_output_format.video_output_cea_vesa) {
		dev_err(&dev->dev, "%s:video output format not found "
			"\n", __func__);
		goto hdmi_set_video_mode_end;
	}

	ret = av8100_conf_prep(AV8100_COMMAND_VIDEO_OUTPUT_FORMAT,
		&av8100_config);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_prep "
			"AV8100_COMMAND_VIDEO_OUTPUT_FORMAT failed\n",
			__func__);
		goto hdmi_set_video_mode_end;
	}

	/* Get current av8100 video input format */
	ret = av8100_conf_get(AV8100_COMMAND_VIDEO_INPUT_FORMAT,
		&av8100_config);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_get "
			"AV8100_COMMAND_VIDEO_INPUT_FORMAT failed\n",
			__func__);
		goto hdmi_set_video_mode_end;
	}

	/* Set correct av8100 video input pixel format */
	switch (dev->port->pixel_format) {
	case MCDE_PORTPIXFMT_DSI_16BPP:
	default:
		av8100_config.video_input_format.input_pixel_format =
			AV8100_INPUT_PIX_RGB565;
		break;
	case MCDE_PORTPIXFMT_DSI_18BPP:
		av8100_config.video_input_format.input_pixel_format =
			AV8100_INPUT_PIX_RGB666;
		break;
	case MCDE_PORTPIXFMT_DSI_18BPP_PACKED:
		av8100_config.video_input_format.input_pixel_format =
			AV8100_INPUT_PIX_RGB666P;
		break;
	case MCDE_PORTPIXFMT_DSI_24BPP:
		av8100_config.video_input_format.input_pixel_format =
			AV8100_INPUT_PIX_RGB888;
		break;
	case MCDE_PORTPIXFMT_DSI_YCBCR422:
		av8100_config.video_input_format.input_pixel_format =
			AV8100_INPUT_PIX_YCBCR422;
		break;
	}

	/*  Set ui_x4 */
	av8100_config.video_input_format.ui_x4 = dev->port->phy.dsi.ui;

	/* Set TE_config */
	switch (dev->port->sync_src) {
	case MCDE_SYNCSRC_TE0:
		av8100_config.video_input_format.TE_config = AV8100_TE_IT_LINE;
		break;
	case MCDE_SYNCSRC_TE1:
		av8100_config.video_input_format.TE_config = AV8100_TE_GPIO_IT;
		break;
	case MCDE_SYNCSRC_TE_POLLING:
		av8100_config.video_input_format.TE_config =
			AV8100_TE_DSI_LANE; /* Only on DSI, no interrupts */
		break;
	case MCDE_SYNCSRC_OFF:
	default:
		av8100_config.video_input_format.TE_config = AV8100_TE_OFF;
		break;
	}

	ret = av8100_conf_prep(AV8100_COMMAND_VIDEO_INPUT_FORMAT,
		&av8100_config);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_prep "
				"AV8100_COMMAND_VIDEO_INPUT_FORMAT failed\n",
				__func__);
		goto hdmi_set_video_mode_end;
	}

	ret = av8100_conf_w(AV8100_COMMAND_VIDEO_INPUT_FORMAT,
		NULL, NULL, I2C_INTERFACE);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_w "
				"AV8100_COMMAND_VIDEO_INPUT_FORMAT failed\n",
				__func__);
		goto hdmi_set_video_mode_end;
	}

	if (dev->port->hdmi_sdtv_switch == SDTV_SWITCH) {
		if (dev->port->pixel_format != MCDE_PORTPIXFMT_DSI_YCBCR422)
			av8100_config.color_transform =
					AV8100_COLOR_TRANSFORM_RGB_TO_DENC;
		else
			av8100_config.color_transform =
					AV8100_COLOR_TRANSFORM_YUV_TO_DENC;
	} else if (dev->port->pixel_format == MCDE_PORTPIXFMT_DSI_YCBCR422) {
		av8100_config.color_transform =
					AV8100_COLOR_TRANSFORM_YUV_TO_RGB;
	} else {
		av8100_config.color_transform =
					AV8100_COLOR_TRANSFORM_INDENTITY;
	}

	ret = av8100_conf_prep(
		AV8100_COMMAND_COLORSPACECONVERSION,
		&av8100_config);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_configuration_prepare "
			"AV8100_COMMAND_COLORSPACECONVERSION failed\n",
			__func__);
		goto hdmi_set_video_mode_end;
	}

	ret = av8100_conf_w(
			AV8100_COMMAND_COLORSPACECONVERSION,
			NULL, NULL, I2C_INTERFACE);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_w "
			"AV8100_COMMAND_COLORSPACECONVERSION failed\n",
			__func__);
		goto hdmi_set_video_mode_end;
	}

	/* Set video output format */
	ret = av8100_conf_w(AV8100_COMMAND_VIDEO_OUTPUT_FORMAT,
		NULL, NULL, I2C_INTERFACE);
	if (ret) {
		dev_err(&dev->dev, "av8100_conf_w failed\n");
		goto hdmi_set_video_mode_end;
	}

	/* Set audio input format */
	ret = av8100_conf_w(AV8100_COMMAND_AUDIO_INPUT_FORMAT,
		NULL, NULL, I2C_INTERFACE);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_w "
				"AV8100_COMMAND_AUDIO_INPUT_FORMAT failed\n",
			__func__);
		goto hdmi_set_video_mode_end;
	}

	av8100_video_mode_changed();

	dev->update_flags |= UPDATE_FLAG_VIDEO_MODE;
	dev->first_update = true;

hdmi_set_video_mode_end:
	av8100_conf_unlock();

	return ret;
}

static u16 rotate_byte_left(u8 c, int nr)
{
	return (0xff & (c << nr)) | (0xff & (c >> (8 - nr)));
}

static u16 map_yv(u8 in)
{
	return rotate_byte_left(in, 3) << 4;
}

static u16 map_u(u8 in)
{
	return rotate_byte_left(in, 5) << 4;
}

static int hdmi_set_pixel_format(
	struct mcde_display_device *ddev, enum mcde_ovly_pix_fmt format)
{
	dev_dbg(&ddev->dev, "%s\n", __func__);
	ddev->pixel_format = format;

	return 0;
}

static int hdmi_set_port_pixel_format(struct mcde_display_device *ddev)
{
	int ret;

	dev_dbg(&ddev->dev, "%s\n", __func__);
	mcde_chnl_stop_flow(ddev->chnl_state);
	ret = mcde_chnl_set_pixel_format(ddev->chnl_state,
						ddev->port->pixel_format);

	if (ret < 0) {
		dev_warn(&ddev->dev, "%s: Failed to set pixel format = %d\n",
					__func__, ddev->port->pixel_format);
		return ret;
	}

	if (ddev->port->pixel_format == MCDE_PORTPIXFMT_DSI_YCBCR422 &&
						av8100_ver_get() == 2) {
		/* The V2 version has an error for unpacking YUV422 */
		struct mcde_palette_table palette = {
			.map_col_ch0 = *map_yv,
			.map_col_ch1 = *map_u,
			.map_col_ch2 = *map_yv,
		};
		ret = mcde_chnl_set_palette(ddev->chnl_state, &palette);
	} else {
		ret = mcde_chnl_set_palette(ddev->chnl_state, NULL);
	}

	return 0;
}

static int hdmi_apply_config(struct mcde_display_device *ddev)
{
	int ret;

	if (!ddev->update_flags)
		return 0;

	ret = mcde_chnl_apply(ddev->chnl_state);
	if (ret < 0) {
		dev_warn(&ddev->dev, "%s:Failed to apply to channel\n",
							__func__);
		return ret;
	}
	ddev->update_flags = 0;

	return 0;
}

static int hdmi_on_first_update(struct mcde_display_device *dev)
{
	int ret;
	union av8100_configuration av8100_config;

	dev->first_update = false;

	/*
	 * Prepare HDMI configuration
	 * Avoid simultaneous output of DENC and HDMI/DVI.
	 * Only one of them should be enabled.
	 * Note HDMI/DVI and DENC are always turned off in set_video_mode.
	 */
	av8100_conf_lock();
	switch (dev->port->hdmi_sdtv_switch) {
	case SDTV_SWITCH:
		if (av8100_conf_get(AV8100_COMMAND_DENC, &av8100_config)) {
			ret = -EFAULT;
			goto hdmi_on_first_update_end;
		}
		av8100_config.denc_format.enable = 1;
		if (dev->video_mode.yres == NATIVE_YRES_SDTV) {
			av8100_config.denc_format.standard_selection =
					AV8100_PAL_BDGHI;
			av8100_config.denc_format.cvbs_video_format  =
					AV8100_CVBS_625;
		} else {
			av8100_config.denc_format.standard_selection =
					AV8100_NTSC_M;
			av8100_config.denc_format.cvbs_video_format  =
					AV8100_CVBS_525;
		}
		ret = av8100_conf_prep(AV8100_COMMAND_DENC, &av8100_config);
		break;
	case DVI_SWITCH:
		av8100_config.hdmi_format.hdmi_mode = AV8100_HDMI_ON;
		av8100_config.hdmi_format.hdmi_format = AV8100_DVI;
		av8100_config.hdmi_format.dvi_format = AV8100_DVI_CTRL_CTL0;
		ret = av8100_conf_prep(AV8100_COMMAND_HDMI, &av8100_config);
		break;
	case HDMI_SWITCH:
	default:
		av8100_config.hdmi_format.hdmi_mode = AV8100_HDMI_ON;
		av8100_config.hdmi_format.hdmi_format = AV8100_HDMI;
		av8100_config.hdmi_format.dvi_format = AV8100_DVI_CTRL_CTL0;
		ret = av8100_conf_prep(AV8100_COMMAND_HDMI, &av8100_config);
		break;
	}

	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_prep "
			"AV8100_COMMAND_HDMI/DENC failed\n", __func__);
		goto hdmi_on_first_update_end;
	}

	if (dev->port->hdmi_sdtv_switch == SDTV_SWITCH)
		ret = av8100_conf_w(AV8100_COMMAND_DENC, NULL, NULL,
								I2C_INTERFACE);
	else
		ret = av8100_conf_w(AV8100_COMMAND_HDMI, NULL, NULL,
								I2C_INTERFACE);
	if (ret) {
		dev_err(&dev->dev, "%s:av8100_conf_w "
			"AV8100_COMMAND_HDMI/DENC failed\n", __func__);
		goto hdmi_on_first_update_end;
	}
hdmi_on_first_update_end:
	av8100_conf_unlock();
	return ret;
}

#define HDMI_GET_RETRY_TIME	500000
#define HDMI_GET_RETRY_CNT_MAX	4

static int hdmi_set_power_mode(struct mcde_display_device *ddev,
	enum mcde_display_power_mode power_mode)
{
	struct display_driver_data *driver_data = dev_get_drvdata(&ddev->dev);
	int ret = 0;

	/* OFF -> STANDBY */
	if (ddev->power_mode == MCDE_DISPLAY_PM_OFF &&
					power_mode != MCDE_DISPLAY_PM_OFF) {
		int cnt;
		int get_res;

		/*
		 * the regulator for analog TV out is only enabled here,
		 * this means that one needs to switch to the OFF state
		 * to be able to switch from HDMI to CVBS.
		 */
		if (ddev->port->hdmi_sdtv_switch == SDTV_SWITCH) {
			ret = regulator_enable(driver_data->cvbs_regulator);
			if (ret)
				return ret;
			driver_data->cvbs_regulator_enabled = true;
		}
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;

		/* Get HDMI resource */
		for (cnt = 0; cnt < HDMI_GET_RETRY_CNT_MAX; cnt++) {
			get_res = av8100_hdmi_get(AV8100_HDMI_USER_VIDEO);
			if (get_res >= 0)
				break;
			else
				usleep_range(HDMI_GET_RETRY_TIME,
						HDMI_GET_RETRY_TIME * 12 / 10);
		}

		if (get_res < 0)
			return -EFAULT;
		else if (get_res > 1)
			av8100_hdmi_video_on();

		hdmi_set_port_pixel_format(ddev);
	}
	/* STANDBY -> ON */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
				power_mode == MCDE_DISPLAY_PM_ON) {
		av8100_powerwakeup(true);
		ddev->power_mode = MCDE_DISPLAY_PM_ON;
		goto set_power_and_exit;
	}
	/* ON -> STANDBY */
	else if (ddev->power_mode == MCDE_DISPLAY_PM_ON &&
				power_mode <= MCDE_DISPLAY_PM_STANDBY) {
		ddev->power_mode = MCDE_DISPLAY_PM_STANDBY;
	}

	/* STANDBY -> OFF */
	if (ddev->power_mode == MCDE_DISPLAY_PM_STANDBY &&
				power_mode == MCDE_DISPLAY_PM_OFF) {
		memset(&(ddev->video_mode), 0, sizeof(struct mcde_video_mode));

		/* Put HDMI resource */
		if (av8100_hdmi_put(AV8100_HDMI_USER_VIDEO)) {
			/* Audio is still used */
			av8100_hdmi_video_off();
		}

		if (driver_data->cvbs_regulator_enabled) {
			ret = regulator_disable(driver_data->cvbs_regulator);
			if (ret)
				return ret;
			driver_data->cvbs_regulator_enabled = false;
		}
		ddev->power_mode = MCDE_DISPLAY_PM_OFF;
	}

set_power_and_exit:
	mcde_chnl_set_power_mode(ddev->chnl_state, ddev->power_mode);

	return ret;
}

static int hdmi_set_rotation(struct mcde_display_device *ddev,
				enum mcde_display_rotation rotation)
{
	/* Not possible to rotate HDMI */
	return 0;
}

static bool secure_output(void)
{
	return av8100_encryption_ongoing();
}

static int __devinit hdmi_probe(struct mcde_display_device *dev)
{
	int ret = 0;
	struct mcde_port *port;
	struct display_driver_data *driver_data;
	struct mcde_display_hdmi_platform_data *pdata =
		dev->dev.platform_data;

	if (pdata == NULL) {
		dev_err(&dev->dev, "%s:Platform data missing\n", __func__);
		return -EINVAL;
	}

	if (dev->port->type != MCDE_PORTTYPE_DSI) {
		dev_err(&dev->dev, "%s:Invalid port type %d\n",
			__func__, dev->port->type);
		return -EINVAL;
	}

	driver_data = (struct display_driver_data *)
		kzalloc(sizeof(struct display_driver_data), GFP_KERNEL);
	if (!driver_data) {
		dev_err(&dev->dev, "Failed to allocate driver data\n");
		return -ENOMEM;
	}

	/* DSI use clock continous mode if AV8100_CHIPVER_1 > 1 */
	if (av8100_ver_get() > AV8100_CHIPVER_1)
		dev->port->phy.dsi.clk_cont = true;

	dev->on_first_update = hdmi_on_first_update;
	dev->try_video_mode = hdmi_try_video_mode;
	dev->set_video_mode = hdmi_set_video_mode;
	dev->apply_config = hdmi_apply_config;
	dev->set_pixel_format = hdmi_set_pixel_format;
	dev->set_power_mode = hdmi_set_power_mode;
	dev->set_rotation = hdmi_set_rotation;
	dev->secure_output = secure_output;

	port = dev->port;

	port->phy.dsi.host_eot_gen = true;
	port->phy.dsi.num_data_lanes = 2;
	port->phy.dsi.hs_freq = DSI_HS_FREQ_HZ;
	port->phy.dsi.lp_freq = DSI_LP_FREQ_HZ;
	if (dev->port->sync_src == MCDE_SYNCSRC_TE0 ||
				dev->port->sync_src == MCDE_SYNCSRC_TE1) {
		port->vsync_polarity = VSYNC_ACTIVE_HIGH;
		port->vsync_clock_div = 0;
		port->vsync_min_duration = 1;
		port->vsync_max_duration = 255;
	}

	/* Create sysfs files */
	if (device_create_file(&dev->dev, &dev_attr_hdmisdtvswitch))
		dev_info(&dev->dev,
			"Unable to create hdmisdtvswitch attr\n");
	if (device_create_file(&dev->dev, &dev_attr_input_pixel_format))
		dev_info(&dev->dev,
			"Unable to create input_pixel_format attr\n");
	if (device_create_file(&dev->dev, &dev_attr_disponoff))
		dev_info(&dev->dev,
			"Unable to create disponoff attr\n");
	if (device_create_file(&dev->dev, &dev_attr_vesacea))
		dev_info(&dev->dev,
			"Unable to create ceavesa attr\n");
	if (device_create_file(&dev->dev, &dev_attr_timing))
		dev_info(&dev->dev,
			"Unable to create timing attr\n");
	if (device_create_file(&dev->dev, &dev_attr_stayalive))
		dev_info(&dev->dev,
			"Unable to create stayalive attr\n");

	if (pdata->cvbs_regulator_id) {
		driver_data->cvbs_regulator = regulator_get(&dev->dev,
						pdata->cvbs_regulator_id);
		if (IS_ERR(driver_data->cvbs_regulator)) {
			ret = PTR_ERR(driver_data->cvbs_regulator);
			dev_warn(&dev->dev, "%s:Failed to get regulator %s\n",
				__func__, pdata->cvbs_regulator_id);
			driver_data->cvbs_regulator = NULL;
			goto av_regulator_get_failed;
		}
	}

	dev_set_drvdata(&dev->dev, driver_data);
	dev_info(&dev->dev, "HDMI display probed\n");

	return 0;

av_regulator_get_failed:
	kfree(driver_data);
	return ret;
}

static int __devexit hdmi_remove(struct mcde_display_device *dev)
{
	struct display_driver_data *driver_data = dev_get_drvdata(&dev->dev);
	struct mcde_display_hdmi_platform_data *pdata =
		dev->dev.platform_data;

	/* Remove sysfs files */
	device_remove_file(&dev->dev, &dev_attr_input_pixel_format);
	device_remove_file(&dev->dev, &dev_attr_hdmisdtvswitch);
	device_remove_file(&dev->dev, &dev_attr_disponoff);
	device_remove_file(&dev->dev, &dev_attr_vesacea);
	device_remove_file(&dev->dev, &dev_attr_timing);
	device_remove_file(&dev->dev, &dev_attr_stayalive);

	dev->set_power_mode(dev, MCDE_DISPLAY_PM_OFF);

	if (driver_data->cvbs_regulator)
		regulator_put(driver_data->cvbs_regulator);
	kfree(driver_data);
	if (pdata->hdmi_platform_enable) {
		if (pdata->regulator)
			regulator_put(pdata->regulator);
		if (pdata->reset_gpio) {
			gpio_direction_input(pdata->reset_gpio);
			gpio_free(pdata->reset_gpio);
		}
	}

	return 0;
}

#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
static int hdmi_resume(struct mcde_display_device *ddev)
{
	int ret;

	if (ddev->chnl_state == NULL)
		return 0;

	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);

	return ret;
}

static int hdmi_suspend(struct mcde_display_device *ddev, pm_message_t state)
{
	int ret;

	if (ddev->chnl_state == NULL)
		return 0;

	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);

	return ret;
}
#endif

static struct mcde_display_driver hdmi_driver = {
	.probe	= hdmi_probe,
	.remove = hdmi_remove,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_PM)
	.suspend = hdmi_suspend,
	.resume = hdmi_resume,
#else
	.suspend = NULL,
	.resume = NULL,
#endif
	.driver = {
		.name	= "av8100_hdmi",
	},
};

/* Module init */
static int __init mcde_display_hdmi_init(void)
{
	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&hdmi_driver);

}
late_initcall(mcde_display_hdmi_init);

static void __exit mcde_display_hdmi_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&hdmi_driver);
}
module_exit(mcde_display_hdmi_exit);

MODULE_AUTHOR("Per Persson <per.xb.persson@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson hdmi display driver");
