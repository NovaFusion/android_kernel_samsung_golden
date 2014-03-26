/*
 * Copyright (C) ST-Ericsson SA 2010
 * Authors: Michel JAOUEN <michel.jaouen@stericsson.com>
 *          Maxime COQUELIN <maxime.coquelin-nonst@stericsson.com>
 *				for ST-Ericsson
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <linux/db8500-modem-trace.h>

#include <mach/hardware.h>

#define DEVICE_NAME "db8500-modem-trace"

/* activation of this flag triggers an initialization of 2 buffers
 * 4kbytes , id 0xdeadbeef
 * and 16Kbytes id 0xfadafada
 * we assume that platform provides minimum 20Kbytes. */

struct trace {
	u32 start;
	u32 end;
	u32 mdm_base;
	u32 ape_base;
	void __iomem *area;
	/* this spinlock to forbid concurrent access on the same trace buffer */
	spinlock_t lock;
	struct device *dev;
	struct miscdevice misc_dev;
};

struct trace_modem {
	u32 phys_addr;
	u8 filler;
};

static struct trace *trace_priv;


/*  all this definition are linked to modem interface */
#define MODEM_MARKER 0x88
/*  free marker is also written on filler */
#define FREE_MARKER 0xa5
#define FREE_MARKER_2 0xa5a5
#define READ_MARKER 0x5a

struct buffer_header {
	u8 pattern;
	u8 filler;
	u16 head_size;
};


static int trace_read(unsigned long arg)
{
	struct modem_trace_req req;
	struct buffer_header *pt;
	char tmp_char;

	if (copy_from_user(&req, (struct modem_trace_req *)arg,
				sizeof(struct modem_trace_req)))
		return -EFAULT;

	/* compute Modem physical address to APE physical address range  */
	if (req.phys_addr < trace_priv->mdm_base) {
		dev_err(trace_priv->dev, "MODEM ADDR uncorrect\n");
		return -EINVAL;
	}
	req.phys_addr += trace_priv->ape_base - trace_priv->mdm_base;

	/* check request is in the range and aligned */
	if ((req.phys_addr % 4 != 0)
			|| (req.phys_addr < trace_priv->start)
			|| (req.phys_addr + req.size) >= trace_priv->end) {
		dev_err(trace_priv->dev, "req out of range %x %x\n",
			req.phys_addr, req.size);
		return -EINVAL;
	}

	/* perform access to memory area  */
	pt = (struct buffer_header *)((u32)trace_priv->area +
			req.phys_addr - trace_priv->start);

	/* in case of several request coming on same trace buffer take a
	 * spinlock */
	spin_lock(&trace_priv->lock);
	if (pt->pattern != MODEM_MARKER) {
		/* pattern and size not matching */
		dev_err(trace_priv->dev, "req not matching filler %x/%x \
				or/and pattern %x\n", req.filler, pt->filler,
				pt->pattern);
		spin_unlock(&trace_priv->lock);
		return -EINVAL;
	}
	/* mark pattern as read and unlock spin */
	pt->pattern = READ_MARKER;
	spin_unlock(&trace_priv->lock);

	req.size -= copy_to_user(req.buff, pt, req.size);

	pt->pattern = FREE_MARKER;
	pt->filler = FREE_MARKER;
	tmp_char = MODEM_MARKER;

	/*  Update marker for trace tool */
	if (copy_to_user(req.buff, &tmp_char, 1))
		return -EFAULT;

	/*  Update effective written size */
	if (copy_to_user((struct modem_trace_req *)arg, &req,
				sizeof(struct modem_trace_req)))
		return -EFAULT;

	return 0;
}

static int trace_mmapdump(struct file *file, struct vm_area_struct *vma)
{
	unsigned long vma_start = vma->vm_start;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	if ((vma->vm_end - vma->vm_start) <
			(trace_priv->end - trace_priv->start))
		return -EINVAL;
	if (remap_pfn_range(vma,
				vma_start,
				trace_priv->start >> PAGE_SHIFT,
				trace_priv->end - trace_priv->start,
				vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static long trace_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	long ret = 0;
	void __user *argp = (void __user *)arg;
	unsigned long size = trace_priv->end-trace_priv->start;

	switch (cmd) {
	case TM_GET_DUMPINFO:
		ret = put_user(size, (unsigned long *)argp);
		break;
	case TM_TRACE_REQ:
		ret = trace_read(arg);
		break;

	default:
		ret = -EPERM;
		break;
	}
	return ret;
}

static const struct file_operations trace_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = trace_ioctl,
	.mmap = trace_mmapdump
};

static int trace_probe(struct platform_device *pdev)
{
	int rv = 0;
	struct db8500_trace_platform_data *pdata = pdev->dev.platform_data;
	/* retrieve area descriptor from platform device ressource */
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if(pdata == NULL || mem == NULL){
		rv = -EINVAL;
		goto out;
	}

	if ((mem->start == 0) && (mem->end == 0)) {
		rv = -EINVAL;
		goto out;
	}

	if ((pdata->ape_base == 0) || (pdata->modem_base == 0)) {
		rv = -EINVAL;
		goto out;
	}

	trace_priv = kzalloc(sizeof(*trace_priv), GFP_ATOMIC);
	if (!trace_priv) {
		rv = -ENOMEM;
		goto out;
	}

	trace_priv->dev = &pdev->dev;
	trace_priv->misc_dev.minor = MISC_DYNAMIC_MINOR;
	trace_priv->misc_dev.name = DEVICE_NAME;
	trace_priv->misc_dev.fops = &trace_fops;
	trace_priv->area = (void __iomem *)ioremap_nocache(mem->start,
						resource_size(mem));
	if (!trace_priv->area) {
		rv = -ENOMEM;
		goto outfree;
	}

	trace_priv->start = mem->start;
	trace_priv->end = mem->end;

	trace_priv->mdm_base = pdata->modem_base;
	trace_priv->ape_base = pdata->ape_base;

	/*  spin allowing smp access for reading/writing trace buffer header  */
	spin_lock_init(&trace_priv->lock);

	rv = misc_register(&trace_priv->misc_dev);
	if (rv) {
		dev_err(&pdev->dev, "can't misc_register\n");
		goto outunmap;
	}

	return rv;

outunmap:
	iounmap(trace_priv->area);
outfree:
	kfree(trace_priv);
out:
	return rv;

}

static int trace_remove(struct platform_device *pdev)
{
	int rv = 0;

	if (trace_priv) {
		rv = misc_deregister(&trace_priv->misc_dev);
		iounmap(trace_priv->area);
		kfree(trace_priv);
	}

	return rv;
}

static struct platform_driver trace_driver = {
	.probe = trace_probe,
	.remove = trace_remove,
	.driver = {
		.name = "db8500-modem-trace",
		.owner = THIS_MODULE,
	},
};

static int trace_init(void)
{
	platform_driver_register(&trace_driver);
	return 0;
}
static void trace_exit(void)
{
	platform_driver_unregister(&trace_driver);
}
module_init(trace_init);
module_exit(trace_exit);

MODULE_AUTHOR("ST-Ericsson");
MODULE_LICENSE("GPL");
