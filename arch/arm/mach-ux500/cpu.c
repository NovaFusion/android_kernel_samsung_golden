/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/sys_soc.h>
#include <linux/clksrc-dbx500-prcmu.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/irq.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <asm/localtimer.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/reboot_reasons.h>
#include <mach/system.h>
#include <mach/pm.h>

#include "clock.h"

void __iomem *_PRCMU_BASE;

#ifdef CONFIG_CACHE_L2X0
static void __iomem *l2x0_base;
#endif

void __init ux500_init_devices(void)
{
#ifdef CONFIG_CACHE_L2X0
	BUG_ON(!l2x0_base);

	/*
	 * Unlock Data and Instruction Lock if locked.  This is done here
	 * instead of in l2x0_init since doing it there appears to cause the
	 * second core boot to occasionaly fail.
	 */
	if (readl_relaxed(l2x0_base + L2X0_LOCKDOWN_WAY_D) & 0xFF)
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_D);

	if (readl_relaxed(l2x0_base + L2X0_LOCKDOWN_WAY_I) & 0xFF)
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_I);
#endif
}

#ifdef CONFIG_SAMSUNG_KERNEL_DEBUG
extern unsigned int unhandled_reset_count ;
#endif /*CONFIG_SAMSUNG_KERNEL_DEBUG*/
void ux500_restart(char mode, const char *cmd)
{
	unsigned short reset_code;
	int i ;
	printk("ux500_restart: Call arch_reset(), mode: %c, cmd: %s\n", mode, cmd );

#ifdef CONFIG_SAMSUNG_KERNEL_DEBUG
        if( 'L' == mode || 'U' == mode || 'K' == mode)
        {
        	for( i = 0 ; i < 100 ; i++ ) {
        		arch_reset( mode, NULL ) ;
        		unhandled_reset_count++ ;
                }
        }
#endif /*CONFIG_SAMSUNG_KERNEL_DEBUG*/

        arch_reset( mode, cmd ) ;

	mdelay(1000);

	printk(KERN_ERR "Reboot via PRCMU failed -- System halted\n");

	while (1);
}

static int ux500_gic_irq_set_wake(struct irq_data *d, unsigned int on)
{
	return 0;
}

void __init ux500_init_irq(void)
{
	void __iomem *dist_base;
	void __iomem *cpu_base;

	gic_arch_extn.irq_set_wake = ux500_gic_irq_set_wake;

	if (cpu_is_u5500()) {
		dist_base = __io_address(U5500_GIC_DIST_BASE);
		cpu_base = __io_address(U5500_GIC_CPU_BASE);
	} else if (cpu_is_u8500() || cpu_is_u9540()) {
		dist_base = __io_address(U8500_GIC_DIST_BASE);
		cpu_base = __io_address(U8500_GIC_CPU_BASE);
	} else
		ux500_unknown_soc();

	gic_init(0, 29, dist_base, cpu_base);

	/*
	 * On WD reboot gic is in some cases decoupled.
	 * This will make sure that the GIC is correctly configured.
	 */
	ux500_pm_gic_recouple();

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	prcmu_early_init();
	/* backwards compatible */
	if (!arm_pm_restart)
		arm_pm_restart = ux500_restart;
	clk_init();
}

#ifdef CONFIG_CACHE_L2X0
static inline void ux500_cache_wait_way(void __iomem *reg, unsigned long mask)
{
	/* wait for the operation to complete */
	while (readl_relaxed(reg) & mask)
		;
}

static inline void ux500_cache_sync(void)
{
	void __iomem *base = l2x0_base;

#ifdef CONFIG_ARM_ERRATA_753970
	/* write to an unmmapped register */
	writel_relaxed(0, base + L2X0_DUMMY_REG);
#else
	writel_relaxed(0, base + L2X0_CACHE_SYNC);
#endif
	/* cache operations by line are atomic in PL310 */
}

/*
 * The L2 cache cannot be turned off in the non-secure world.
 * Dummy until a secure service is in place.
 */
static void ux500_l2x0_disable(void)
{
}

/*
 * This is only called when doing a kexec, just after turning off the L2
 * and L1 cache, and it is surrounded by a spinlock in the generic version.
 * However, we're not really turning off the L2 cache right now and the
 * PL310 does not support exclusive accesses (used to implement the spinlock).
 * So, the invalidation needs to be done without the spinlock.
 */
static void ux500_l2x0_inv_all(void)
{
	void __iomem *base = l2x0_base;
	uint32_t l2x0_way_mask = (1<<16) - 1;	/* Bitmask of active ways */

	/* invalidate all ways */
	writel_relaxed(l2x0_way_mask, base + L2X0_INV_WAY);
	ux500_cache_wait_way(base + L2X0_INV_WAY, l2x0_way_mask);
	ux500_cache_sync();
}

static int ux500_l2x0_init(void)
{
	uint32_t aux_val = 0x3e000000;

	if (cpu_is_u5500())
		l2x0_base = __io_address(U5500_L2CC_BASE);
	else if (cpu_is_u8500() || cpu_is_u9540())
		l2x0_base = __io_address(U8500_L2CC_BASE);
	else
		ux500_unknown_soc();

	/* u9540's L2 has 128KB way size */
	if (cpu_is_u9540())
		aux_val |=
		(0x4 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT); /* 128KB way size */
	else
		aux_val |=
		(0x3 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT); /* 64KB way size */

	/* 8 way associativity, force WA */
	l2x0_init(l2x0_base, aux_val, 0xc0000fff);

	/* Override invalidate function */
	outer_cache.disable = ux500_l2x0_disable;
	outer_cache.inv_all = ux500_l2x0_inv_all;

	return 0;
}
early_initcall(ux500_l2x0_init);
#endif

#ifdef CONFIG_SYS_SOC
#define U8500_BB_UID_BASE (U8500_BACKUPRAM1_BASE + 0xFC0)
#define U8500_BB_UID_LENGTH 5

static ssize_t ux500_get_machine(char *buf, struct sysfs_soc_info *si)
{
	return sprintf(buf, "DB%2x00\n", dbx500_id.partnumber);
}

static ssize_t ux500_get_soc_id(char *buf, struct sysfs_soc_info *si)
{
	void __iomem *uid_base;
	int i;
	ssize_t sz = 0;

	if (dbx500_id.partnumber == 0x85) {
		uid_base = __io_address(U8500_BB_UID_BASE);
		for (i = 0; i < U8500_BB_UID_LENGTH; i++)
			sz += sprintf(buf + sz, "%08x",
					readl(uid_base + i * sizeof(u32)));
		sz += sprintf(buf + sz, "\n");
	} else {
		/* Don't know where it is located for U5500 */
		sz = sprintf(buf, "N/A\n");
	}

	return sz;
}

static ssize_t ux500_get_revision(char *buf, struct sysfs_soc_info *si)
{
	unsigned int rev = dbx500_id.revision;

	if (rev == 0x01)
		return sprintf(buf, "%s\n", "ED");
	else if (rev >= 0xA0)
		return sprintf(buf, "%d.%d\n" ,
				(rev >> 4) - 0xA + 1, rev & 0xf);

	return sprintf(buf, "%s", "Unknown\n");
}

static ssize_t ux500_get_process(char *buf, struct sysfs_soc_info *si)
{
	if (dbx500_id.process == 0x00)
		return sprintf(buf, "Standard\n");

	return sprintf(buf, "%02xnm\n", dbx500_id.process);
}

static ssize_t ux500_get_reset_code(char *buf, struct sysfs_soc_info *si)
{
	return sprintf(buf, "0x%04x\n", prcmu_get_reset_code());
}

static ssize_t ux500_get_reset_reason(char *buf, struct sysfs_soc_info *si)
{
	return sprintf(buf, "%s\n",
		reboot_reason_string(prcmu_get_reset_code()));
}

static ssize_t ux500_get_reset_status(char *buf, struct sysfs_soc_info *si)
{
	return sprintf(buf, "0x%08x\n", prcmu_get_reset_status());
}

static struct sysfs_soc_info soc_info[] = {
	SYSFS_SOC_ATTR_CALLBACK("machine", ux500_get_machine),
	SYSFS_SOC_ATTR_VALUE("family", "Ux500"),
	SYSFS_SOC_ATTR_CALLBACK("soc_id", ux500_get_soc_id),
	SYSFS_SOC_ATTR_CALLBACK("revision", ux500_get_revision),
	SYSFS_SOC_ATTR_CALLBACK("process", ux500_get_process),
	SYSFS_SOC_ATTR_CALLBACK("reset_code", ux500_get_reset_code),
	SYSFS_SOC_ATTR_CALLBACK("reset_reason", ux500_get_reset_reason),
	SYSFS_SOC_ATTR_CALLBACK("reset_status", ux500_get_reset_status),
};

static int __init ux500_sys_soc_init(void)
{
	return register_sysfs_soc(soc_info, ARRAY_SIZE(soc_info));
}

module_init(ux500_sys_soc_init);
#endif
