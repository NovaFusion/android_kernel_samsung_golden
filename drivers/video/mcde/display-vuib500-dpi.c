/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson MCDE DPI display driver
 * The VUIB500 is an user interface board the can be attached to an HREF. It
 * supports the DPI pixel interface and converts this to an analog VGA signal,
 * which can be connected to a monitor using a DSUB connector. The VUIB board
 * uses an external power supply of 5V.
 *
 * Author: Marcel Tunnissen <marcel.tuennissen@stericsson.com>
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <video/mcde_display.h>
#include <video/mcde_display-vuib500-dpi.h>

#define DPI_DISP_TRACE	dev_dbg(&ddev->dev, "%s\n", __func__)

static int try_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode);
static int set_video_mode(struct mcde_display_device *ddev,
				struct mcde_video_mode *video_mode);

static int __devinit dpi_display_probe(struct mcde_display_device *ddev)
{
	int ret = 0;
	struct mcde_display_dpi_platform_data *pdata = ddev->dev.platform_data;
	DPI_DISP_TRACE;

	if (pdata == NULL) {
		dev_err(&ddev->dev, "%s:Platform data missing\n", __func__);
		ret = -EINVAL;
		goto no_pdata;
	}

	if (ddev->port->type != MCDE_PORTTYPE_DPI) {
		dev_err(&ddev->dev,
			"%s:Invalid port type %d\n",
			__func__, ddev->port->type);
		ret = -EINVAL;
		goto invalid_port_type;
	}

	ddev->try_video_mode = try_video_mode;
	ddev->set_video_mode = set_video_mode;
	dev_info(&ddev->dev, "DPI display probed\n");

	goto out;
invalid_port_type:
no_pdata:
out:
	return ret;
}

static int __devexit dpi_display_remove(struct mcde_display_device *ddev)
{
	DPI_DISP_TRACE;

	ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);

	return 0;
}

static int dpi_display_resume(struct mcde_display_device *ddev)
{
	int ret;
	DPI_DISP_TRACE;

	/* set_power_mode will handle call platform_enable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_STANDBY);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to resume display\n"
			, __func__);
	return ret;
}

static int dpi_display_suspend(struct mcde_display_device *ddev,
							pm_message_t state)
{
	int ret;
	DPI_DISP_TRACE;

	/* set_power_mode will handle call platform_disable */
	ret = ddev->set_power_mode(ddev, MCDE_DISPLAY_PM_OFF);
	if (ret < 0)
		dev_warn(&ddev->dev, "%s:Failed to suspend display\n"
			, __func__);
	return ret;
}

static void print_vmode(struct mcde_video_mode *vmode)
{
	pr_debug("resolution: %dx%d\n", vmode->xres, vmode->yres);
	pr_debug("  pixclock: %d\n",    vmode->pixclock);
	pr_debug("       hbp: %d\n",    vmode->hbp);
	pr_debug("       hfp: %d\n",    vmode->hfp);
	pr_debug("       hsw: %d\n",    vmode->hsw);
	pr_debug("       vbp: %d\n",    vmode->vbp);
	pr_debug("       vfp: %d\n",    vmode->vfp);
	pr_debug("       vsw: %d\n",    vmode->vsw);
	pr_debug("interlaced: %s\n", vmode->interlaced ? "true" : "false");
}

/* Taken from the programmed value of the LCD clock in PRCMU */
#define PIX_CLK_FREQ		25000000
#define VMODE_XRES		640
#define VMODE_YRES		480

static int try_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode)
{
	int res = -EINVAL;
	DPI_DISP_TRACE;

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		return res;
	}

	if (video_mode->xres == VMODE_XRES && video_mode->yres == VMODE_YRES) {
		video_mode->hbp = 40;
		video_mode->hfp = 8;
		video_mode->hsw = 96;
		video_mode->vbp = 25;
		video_mode->vfp = 2;
		video_mode->vsw = 2;
		/*
		 * The pixclock setting is not used within MCDE. The clock is
		 * setup elsewhere. But the pixclock value is visible in user
		 * space.
		 */
		video_mode->pixclock =	(int) (1e+12 * (1.0 / PIX_CLK_FREQ));
		res = 0;
	} /* TODO: add more supported resolutions here */
	video_mode->interlaced = false;

	if (res == 0)
		print_vmode(video_mode);
	else
		dev_warn(&ddev->dev,
			"%s:Failed to find video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);

	return res;

}

static int set_video_mode(
	struct mcde_display_device *ddev, struct mcde_video_mode *video_mode)
{
	int res;
	DPI_DISP_TRACE;

	if (ddev == NULL || video_mode == NULL) {
		dev_warn(&ddev->dev, "%s:ddev = NULL or video_mode = NULL\n",
			__func__);
		return -EINVAL;
	}
	if (video_mode->xres != VMODE_XRES || video_mode->yres != VMODE_YRES) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode x=%d, y=%d\n",
			__func__, video_mode->xres, video_mode->yres);
		return -EINVAL;
	}
	ddev->video_mode = *video_mode;
	print_vmode(video_mode);

	res = mcde_chnl_set_video_mode(ddev->chnl_state, &ddev->video_mode);
	if (res < 0) {
		dev_warn(&ddev->dev, "%s:Failed to set video mode on channel\n",
			__func__);

	}
	/* notify mcde display driver about updated video mode */
	ddev->update_flags |= UPDATE_FLAG_VIDEO_MODE;
	return res;
}

static struct mcde_display_driver dpi_display_driver = {
	.probe	= dpi_display_probe,
	.remove = dpi_display_remove,
	.suspend = dpi_display_suspend,
	.resume = dpi_display_resume,
	.driver = {
		.name	= "mcde_display_dpi",
	},
};

/* Module init */
static int __init mcde_dpi_display_init(void)
{
	pr_info("%s\n", __func__);

	return mcde_display_driver_register(&dpi_display_driver);
}
module_init(mcde_dpi_display_init);

static void __exit mcde_dpi_display_exit(void)
{
	pr_info("%s\n", __func__);

	mcde_display_driver_unregister(&dpi_display_driver);
}
module_exit(mcde_dpi_display_exit);

MODULE_AUTHOR("Marcel Tunnissen <marcel.tuennissen@stericsson.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ST-Ericsson MCDE DPI display driver fro VUIB500 display");
