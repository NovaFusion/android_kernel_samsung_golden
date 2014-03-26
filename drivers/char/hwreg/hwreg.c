#include <linux/version.h>
#include <linux/kernel.h>	/* printk ( ) */
#include <linux/module.h>	/* MODULE_ macros */
#include <linux/init.h>		/* __init, __exit */
#include <linux/errno.h>	/* error codes */
#include <linux/fs.h>		/* register_chrdev, unregister_chrdev */
#include <linux/slab.h>		/* kmalloc, kfree */
#include <asm/uaccess.h>	/* copy_[to|from]_user */
#include <asm/io.h>		/* readl */
#include <linux/cdev.h>		/* cdev */

#include <mach/hardware.h>

/* temporary definitions
   The following declarations are to be removed as kernel/arch/arm/mach-ux8500/include/mach/db8500-regs.h is up-to-date */

/* DDR-SDRAM chip-select 0 (0x0000 0000 : 0x1FFF FFFF) */
#ifndef U8500_SCU_CD_R4_BASE
#define U8500_SCU_CD_R4_BASE      0x17c40000
#endif

#ifndef U8500_SCU_AD_R4_BASE
#define U8500_SCU_AD_R4_BASE      0x17d40000
#endif

#ifndef U8500_HSI2CMODEMR4_BASE
#define U8500_HSI2CMODEMR4_BASE   0x17e02000
#endif
/* End of temporary definitions */

struct hwreg_io_range {
	u32 base;
	u32 size;
	u8 *addr;
};

static struct hwreg_io_range hwreg_io_map[] = {
	{.base = U8500_PER1_BASE,	.size = 0x10000,},      /* Periph1 Peripherals */
    {.base = U8500_PER2_BASE,	.size = 0x10000,},      /* Periph2 Peripherals */
	{.base = U8500_PER3_BASE,	.size = 0x10000,},      /* Periph3 Peripherals */
	{.base = U8500_PER4_BASE,	.size = 0x70000,},      /* Periph4 Peripherals */
	{.base = U8500_PER5_BASE,	.size = 0x20000,},      /* Periph5 Periphals */
	{.base = U8500_PER6_BASE,	.size = 0x10000,},      /* Periph6 Peripherals */
    /* ++ CAP_1732_001 ++ */
/*	{.base = U8500_PER7_BASE_ED, .size = 0x10000,}, */  /* Periph7 Peripherals: removed in V1 */
    /* -- CAP_1732_001 -- */
    /* ++ PR_CAP_87_001 ++ */
    {.base = U8500_SCU_BASE, .size = 0x4000, },       /* Snoop Control Unit, A9 Private interrupt IF, 
                                                           A9 private peripherals, Level-2 Cache Configuration registers,
                                                           and some reserved area */       
	{.base = U8500_MCDE_BASE,	.size = SZ_4K,},        /* DISPLAY Ctrl. configuration registers */
	{.base = U8500_DSI_LINK1_BASE,.size = SZ_4K,},    /* DSI1 link registers */
	{.base = U8500_DSI_LINK2_BASE,.size = SZ_4K,},    /* DSI2 link registers */
	{.base = U8500_DSI_LINK3_BASE,.size = SZ_4K,},    /* DSI3 link registers */
	{.base = U8500_DMA_BASE,	.size = SZ_4K,},        /* DMA Ctrl. configuration registers (base address changed in DB8500 V1) */
	/* ++ CAP_1732_001 ++ */
    {.base = U8500_MODEM_I2C, .size = 0x404000, },    /* 0xB7A00000 -> 0xB7E04000: Modem I2C */
    /* -- CAP_1732_001 -- */
    {.base = U8500_SBAG_BASE, .size = SZ_4K, },       /* 0xA0390000 -> 0xA039FFFF: SBAG configuration registers */
    /* ++ CAP_1732_001 ++ */
    {.base = U8500_SGA_BASE, .size = 0x10000, },      /* 0xA0300000 -> 0xA031FFFF: SGA configuration registers */
    /* -- CAP_1732_001 -- */
    {.base = U8500_SIA_BASE, .size = 0x60000, },      /* 0xA0200000 -> 0xA02FFFFF: Smart Imaging Acc. Data Memory space (SIA) */
    {.base = U8500_SVA_BASE, .size = 0x60000, },      /* 0xA0100000 -> 0xA01FFFFF: Smart Video Acc. Data Memory space (SVA) */
    {.base = U8500_ICN_BASE, .size = 0x2000, },       /* 0x81000000 -> 0x8103FFFF: Main ICN Crossbar configuration registers */
    {.base = U8500_HSEM_BASE, .size = SZ_4K, },       /* 0x80140000 -> 0x8014FFFF: HSEM (Semaphores) configuration registers */
    {.base = U8500_B2R2_BASE, .size = SZ_4K, },       /* 0x80130000 -> 0x8013FFFF: B2R2 configuration registers */
    {.base = U8500_STM_BASE, .size = 0x10000, },      /* 0x80100000 -> 0x8010FFFF: STM */
    /* -- PR_CAP_87_001 -- */
	/* ++ PR_CAP_667 ++*/
	{.base = U8500_ASIC_ID_BASE, .size = SZ_4K,},     /* High part of embedded boot ROM */
	/* -- PR_CAP_667 --*/
    /* ++ CR CAP_4280_001: addon V2 registers */
    {.base = U8500_SCU_CD_R4_BASE,    .size = SZ_4K, },  /* 0x17C4 0000 : 0x17C4 007C */
    {.base = U8500_SCU_AD_R4_BASE,    .size = SZ_4K, },  /* 0x17D4 0000 : 0x17D4 041C */
    {.base = U8500_HSI2CMODEMR4_BASE, .size = SZ_4K, },  /* 0x17E0 2000 : 0x17E0 2FFC */
    /* -- CR CAP_4280_001 */
	/* ++ PR_CAP_1155_001 ++ */
	{.base = 0, .size=0, },
    /* -- PR_CAP_1155_001 -- */
};

static void hwreg_printk_map(void)
{
    int i;

    printk(KERN_INFO "---------- HWREG TABLE -------------\n");
    for (i = 0; hwreg_io_map[i].base; ++i) {
        printk(KERN_INFO "%d: 0x%08X => 0x%08X\n", i, hwreg_io_map[i].base, hwreg_io_map[i].base + hwreg_io_map[i].size );
    }
    printk(KERN_INFO "------------------------------------\n");
}

static void hwreg_io_init(void)
{
	int i;

    /* enable the following line when designing HWREG's memory mapping for a new
       revision of the chipset */
    /*hwreg_printk_map();*/

	for (i = 0; hwreg_io_map[i].base; ++i) {
		hwreg_io_map[i].addr = ioremap(hwreg_io_map[i].base,
						 hwreg_io_map[i].size);
		if (!hwreg_io_map[i].addr)
			printk(KERN_WARNING 
				"%s: ioremap for %d (%08x) failed\n",
				__func__, i, hwreg_io_map[i].base);
	}
}

static void hwreg_io_exit(void)
{
	int i;

	for (i = 0; hwreg_io_map[i].base; ++i)
		if (hwreg_io_map[i].addr)
			iounmap(hwreg_io_map[i].addr);
}

static void *hwreg_io_ptov(u32 phys)
{
	int i;

	for (i = 0; hwreg_io_map[i].base; ++i) {
		u32 base = hwreg_io_map[i].base;
		u32 size = hwreg_io_map[i].size;
		u8 *addr = hwreg_io_map[i].addr;

		if (phys < base || phys >= base + size)
			continue;

		if (addr)
			return addr + phys - base;

		break;
	}

	return NULL;
}

#define MY_NAME "hwreg"
#define PREFIX MY_NAME ": "

struct hwreg_ioctl_param {
	unsigned int addr;	/* address */
	unsigned long val;	/* value */
};

typedef struct hwreg_ioctl_param st_hwreg;

#define HWREG_READ	_IOWR(0x03, 0xc6, st_hwreg)
#define HWREG_WRITE	_IOW (0x03, 0xc7, st_hwreg)
#define HWREG_CHECK_ADDR    _IOWR(0x03, 0xcA, st_hwreg)

int hwreg_major = 0;
int hwreg_minor = 0;

static int hwreg_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int hwreg_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int hwreg_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	int retval;
	int i;
	struct hwreg_ioctl_param param;
	void *ptr;

	switch (cmd)
	{
	case HWREG_READ:
	
		retval = copy_from_user(&param, (void __user *)arg, sizeof(param));
		if (retval == 0)
		{
		    ptr = hwreg_io_ptov(param.addr);
		    if (ptr != NULL)
		    {
                param.val = readl(ptr);
                retval = copy_to_user((void __user *)arg, &param, sizeof(param));
		    }
		    else
		    {
			    retval = -EFAULT;
		    }
		}
		break;

	case HWREG_WRITE:
	
		retval = copy_from_user(&param, (void __user *)arg, sizeof(param));
		if (retval == 0)
		{
		    ptr = hwreg_io_ptov(param.addr);
		    if (ptr != NULL)
		    {
                writel(param.val, ptr);
		    }
		    else
		    {
			    retval = -EFAULT;
		    }
		}
        break;

    /* ++ PR_CAP_87_001 ++ */
    case HWREG_CHECK_ADDR:

		retval = copy_from_user(&param, (void __user *)arg, sizeof(param));
		if ( retval == 0 )
		{
		    param.val = 0;
	        for (i=0; (hwreg_io_map[i].base > 0) && (param.val == 0) ; i++)
	        {
	            if ( (hwreg_io_map[i].base <= param.addr ) && ( param.addr <= (hwreg_io_map[i].base + hwreg_io_map[i].size) ) )
	            {
	                /* this io is in remap range */
	                param.val = (u32)(i + 1);
	            }
	        }
	        
	        retval = copy_to_user((void __user *)arg, &param, sizeof(param));
		}
        break;
    /* -- PR_CAP_87_001 -- */

	default:
		printk(KERN_WARNING "HWREG bad ioctl cmd:%x\n", cmd);
		retval = -EINVAL;
	}


	return retval;
}

static struct file_operations hwreg_fops = {
	.open = hwreg_open,
	.release = hwreg_release,
	.unlocked_ioctl = hwreg_ioctl,
	.owner = THIS_MODULE,
};

static struct cdev *cdev = NULL;
static dev_t dev;

static int __init hwreg_initialize(void)
{
	int retval;

	if (hwreg_major != 0) {
		dev = MKDEV(hwreg_major, hwreg_minor);
		retval = register_chrdev_region(dev, 1, MY_NAME);

		if (retval < 0) {
			printk(KERN_WARNING PREFIX "can't get major %d\n",
			       hwreg_major);
			hwreg_major = 0;
			goto bail_out;
		}
	} else {
		retval = alloc_chrdev_region(&dev, hwreg_minor, 1, MY_NAME);

		if (retval < 0) {
			printk(KERN_WARNING PREFIX "can't get major\n");
			goto bail_out;
		}
		hwreg_major = MAJOR(dev);
	}

	cdev = cdev_alloc();
	if (!cdev) {
		retval = -ENOMEM;
		goto bail_out;
	}
	cdev->owner = THIS_MODULE;
	cdev->ops = &hwreg_fops;
	retval = cdev_add(cdev, dev, 1);
	if (retval)
		printk(KERN_WARNING PREFIX "Error %d adding device.\n", retval);

	hwreg_io_init();

bail_out:
	return retval;
}

static void __exit hwreg_finalize(void)
{
	hwreg_io_exit();

	if (cdev)
		cdev_del(cdev);

	if (hwreg_major)
		unregister_chrdev_region(dev, 1);
}

module_init(hwreg_initialize);
module_exit(hwreg_finalize);

module_param_named(major, hwreg_major, int, S_IRUGO);
module_param_named(minor, hwreg_minor, int, S_IRUGO);

MODULE_AUTHOR("Arnaud TROEL - Christophe GUIBOUT - Gabriel FERNANDEZ");
MODULE_DESCRIPTION("HW registers access module");
MODULE_LICENSE("GPL");
