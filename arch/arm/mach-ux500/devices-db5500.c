/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 *
 * Author: Pierre Peiffer <pierre.peiffer@stericsson.com> for ST-Ericsson.
 * for the System Trace Module part.
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio/nomadik.h>

#include <mach/hardware.h>
#include <mach/devices.h>

#include <video/mcde.h>
#include <video/nova_dsilink.h>
#include <mach/db5500-regs.h>
#include <linux/cpufreq.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/pm.h>

#define GPIO_DATA(_name, first, num)					\
	{								\
		.name		= _name,				\
		.first_gpio	= first,				\
		.first_irq	= NOMADIK_GPIO_TO_IRQ(first),		\
		.num_gpio	= num,					\
		.get_secondary_status = ux500_pm_gpio_read_wake_up_status, \
		.set_ioforce	= ux500_pm_prcmu_set_ioforce,		\
	}

#define GPIO_RESOURCE(block)						\
	{								\
		.start	= U5500_GPIOBANK##block##_BASE,			\
		.end	= U5500_GPIOBANK##block##_BASE + 127,		\
		.flags	= IORESOURCE_MEM,				\
	},								\
	{								\
		.start	= IRQ_DB5500_GPIO##block,			\
		.end	= IRQ_DB5500_GPIO##block,			\
		.flags	= IORESOURCE_IRQ,				\
	},								\
	{								\
		.start	= IRQ_DB5500_PRCMU_GPIO##block,			\
		.end	= IRQ_DB5500_PRCMU_GPIO##block,			\
		.flags	= IORESOURCE_IRQ,				\
	}

#define GPIO_DEVICE(block)						\
	{								\
		.name		= "gpio",				\
		.id		= block,				\
		.num_resources	= 3,					\
		.resource	= &u5500_gpio_resources[block * 3],	\
		.dev = {						\
			.platform_data = &u5500_gpio_data[block],	\
		},							\
	}

static struct nmk_gpio_platform_data u5500_gpio_data[] = {
	GPIO_DATA("GPIO-0-31", 0, 32),
	GPIO_DATA("GPIO-32-63", 32, 4), /* 36..63 not routed to pin */
	GPIO_DATA("GPIO-64-95", 64, 19), /* 83..95 not routed to pin */
	GPIO_DATA("GPIO-96-127", 96, 6), /* 102..127 not routed to pin */
	GPIO_DATA("GPIO-128-159", 128, 21), /* 149..159 not routed to pin */
	GPIO_DATA("GPIO-160-191", 160, 32),
	GPIO_DATA("GPIO-192-223", 192, 32),
	GPIO_DATA("GPIO-224-255", 224, 4), /* 228..255 not routed to pin */
};

static struct resource u5500_gpio_resources[] = {
	GPIO_RESOURCE(0),
	GPIO_RESOURCE(1),
	GPIO_RESOURCE(2),
	GPIO_RESOURCE(3),
	GPIO_RESOURCE(4),
	GPIO_RESOURCE(5),
	GPIO_RESOURCE(6),
	GPIO_RESOURCE(7),
};

struct platform_device u5500_gpio_devs[] = {
	GPIO_DEVICE(0),
	GPIO_DEVICE(1),
	GPIO_DEVICE(2),
	GPIO_DEVICE(3),
	GPIO_DEVICE(4),
	GPIO_DEVICE(5),
	GPIO_DEVICE(6),
	GPIO_DEVICE(7),
};

#define U5500_PWM_SIZE 0x20
static struct resource u5500_pwm0_resource[] = {
	{
		.name = "PWM_BASE",
		.start = U5500_PWM_BASE,
		.end = U5500_PWM_BASE + U5500_PWM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource u5500_pwm1_resource[] = {
	{
		.name = "PWM_BASE",
		.start = U5500_PWM_BASE + U5500_PWM_SIZE,
		.end = U5500_PWM_BASE + U5500_PWM_SIZE * 2 - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource u5500_pwm2_resource[] = {
	{
		.name = "PWM_BASE",
		.start = U5500_PWM_BASE + U5500_PWM_SIZE * 2,
		.end = U5500_PWM_BASE + U5500_PWM_SIZE * 3 - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct resource u5500_pwm3_resource[] = {
	{
		.name = "PWM_BASE",
		.start = U5500_PWM_BASE + U5500_PWM_SIZE * 3,
		.end = U5500_PWM_BASE + U5500_PWM_SIZE * 4 - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device u5500_pwm0_device = {
	.id = 0,
	.name = "pwm",
	.resource = u5500_pwm0_resource,
	.num_resources = ARRAY_SIZE(u5500_pwm0_resource),
};

struct platform_device u5500_pwm1_device = {
	.id = 1,
	.name = "pwm",
	.resource = u5500_pwm1_resource,
	.num_resources = ARRAY_SIZE(u5500_pwm1_resource),
};

struct platform_device u5500_pwm2_device = {
	.id = 2,
	.name = "pwm",
	.resource = u5500_pwm2_resource,
	.num_resources = ARRAY_SIZE(u5500_pwm2_resource),
};

struct platform_device u5500_pwm3_device = {
	.id = 3,
	.name = "pwm",
	.resource = u5500_pwm3_resource,
	.num_resources = ARRAY_SIZE(u5500_pwm3_resource),
};

static struct resource u5500_dsilink_resources[] = {
	[0] = {
		.name  = DSI_IO_AREA,
		.start = U5500_DSI_LINK1_BASE,
		.end   = U5500_DSI_LINK1_BASE + U5500_DSI_LINK_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = DSI_IO_AREA,
		.start = U5500_DSI_LINK2_BASE,
		.end   = U5500_DSI_LINK2_BASE + U5500_DSI_LINK_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device u5500_dsilink_device[] = {
	[0] = {
		.name = "dsilink",
		.id = 0,
		.num_resources = 1,
		.resource = &u5500_dsilink_resources[0],
	},
	[1] = {
		.name = "dsilink",
		.id = 1,
		.num_resources = 1,
		.resource = &u5500_dsilink_resources[1],
	},
};

static struct resource mcde_resources[] = {
	[0] = {
		.name  = MCDE_IO_AREA,
		.start = U5500_MCDE_BASE,
		.end   = U5500_MCDE_BASE + U5500_MCDE_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = MCDE_IO_AREA,
		.start = U5500_DSI_LINK1_BASE,
		.end   = U5500_DSI_LINK1_BASE + U5500_DSI_LINK_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.name  = MCDE_IO_AREA,
		.start = U5500_DSI_LINK2_BASE,
		.end   = U5500_DSI_LINK2_BASE + U5500_DSI_LINK_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[3] = {
		.name  = MCDE_IRQ,
		.start = IRQ_DB5500_DISP,
		.end   = IRQ_DB5500_DISP,
		.flags = IORESOURCE_IRQ,
	},
};

static int mcde_platform_enable_dsipll(void)
{
	return prcmu_enable_dsipll();
}

static int mcde_platform_disable_dsipll(void)
{
	return prcmu_disable_dsipll();
}

static int mcde_platform_set_display_clocks(void)
{
	return prcmu_set_display_clocks();
}

static struct mcde_platform_data mcde_pdata = {
	.syncmux = 0x01,
	.regulator_mcde_epod_id = "vsupply",
	.regulator_esram_epod_id = "v-esram12",
#ifdef CONFIG_MCDE_DISPLAY_DSI
	.clock_dsi_id = "hdmi",
	.clock_dsi_lp_id = "tv",
#endif
	.clock_mcde_id = "mcde",
	.platform_set_clocks = mcde_platform_set_display_clocks,
	.platform_enable_dsipll = mcde_platform_enable_dsipll,
	.platform_disable_dsipll = mcde_platform_disable_dsipll,
	/* TODO: Remove rotation buffers once ESRAM driver is completed */
	.rotbuf1 = U8500_ESRAM_BASE + 0x20000 * 4 + 0x2000,
	.rotbuf2 = U8500_ESRAM_BASE + 0x20000 * 4 + 0x11000,
	.rotbufsize = 0xF000,
};

struct platform_device ux500_mcde_device = {
	.name = "mcde",
	.id = -1,
	.dev = {
		.platform_data = &mcde_pdata,
	},
	.num_resources = ARRAY_SIZE(mcde_resources),
	.resource = mcde_resources,
};

struct platform_device ux500_b2r2_blt_device = {
	.name	= "b2r2_blt",
	.id	= 0,
	.dev	= {
		.init_name = "b2r2_blt_init",
		.coherent_dma_mask = ~0,
	},
};

static struct b2r2_platform_data b2r2_platform_data = {
	.regulator_id = "vsupply",
	.clock_id = "b2r2",
};

static struct resource b2r2_resources[] = {
	[0] = {
		.start	= U5500_B2R2_BASE,
		.end	= U5500_B2R2_BASE + ((4*1024)-1),
		.name	= "b2r2_base",
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name  = "B2R2_IRQ",
		.start = IRQ_DB5500_B2R2,
		.end   = IRQ_DB5500_B2R2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device ux500_b2r2_device = {
	.name	= "b2r2",
	.id	= 0,
	.dev	= {
		.init_name = "b2r2_bus",
		.platform_data = &b2r2_platform_data,
		.coherent_dma_mask = ~0,
	},
	.num_resources	= ARRAY_SIZE(b2r2_resources),
	.resource	= b2r2_resources,
};

static struct cpufreq_frequency_table db5500_freq_table[] = {
	[0] = {
		.index = 0,
		.frequency = 200000,
	},
	[1] = {
		.index = 1,
		.frequency = 396500,
	},
	[2] = {
		.index = 2,
		.frequency = 793000,
	},
	[3] = {
		.index = 3,
		.frequency = CPUFREQ_TABLE_END,
	},
};

struct platform_device db5500_prcmu_device = {
	.name			= "db8500-prcmu",
	.dev = {
		.platform_data = db5500_freq_table,
	},
};

