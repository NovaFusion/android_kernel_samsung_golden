/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Ludovic Barre <ludovic.barre@stericsson.com> for ST-Ericsson.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <linux/io.h>

#include <mach/mloader-dbx500.h>
#include <linux/mloader.h>
#include <mach/hardware.h>

#define DEVICE_NAME "dbx500_mloader_fw"
#define MAP_SIZE PAGE_SIZE

struct mloader_priv {
	struct platform_device *pdev;
	struct dbx500_mloader_pdata *pdata;
	struct miscdevice misc_dev;
	u32 aeras_size;
	void __iomem *uid_base;
	u8 size;
};

static struct mloader_priv *mloader_priv;

static int mloader_fw_send(struct dbx500_ml_fw *fw_info)
{
	const struct firmware *fw;
	unsigned long size;
	unsigned long phys_start;
	void *fw_data;
	void __iomem *ioaddr;
	int ret;
	unsigned long copy_offset = 0;
	unsigned long copy_size;

	ret = request_firmware(&fw, fw_info->name, &mloader_priv->pdev->dev);
	if (ret) {
		dev_err(&mloader_priv->pdev->dev, "request firmware failed\n");
		goto out;
	}

	if (fw->size > (fw_info->area->size - fw_info->offset)) {
		dev_err(&mloader_priv->pdev->dev,
				"fw:%s is too big for:%s\n",
				fw_info->name, fw_info->area->name);
		ret = -EINVAL;
		goto err_fw;
	}


	size = PAGE_ALIGN(fw->size);
	phys_start = fw_info->area->start + fw_info->offset;
	phys_start &= PAGE_MASK;

	while (copy_offset < size) {
		ioaddr = ioremap(phys_start + copy_offset, MAP_SIZE);

		if (!ioaddr) {
			dev_err(&mloader_priv->pdev->dev,
				"Failed to remap memory region.\n");
			ret = -EINVAL;
			goto err_fw;
		}

		fw_data = ((void *)fw->data) + copy_offset;

		if ((copy_offset + MAP_SIZE) > size)
			copy_size = fw->size - copy_offset;
		else
			copy_size = MAP_SIZE;

		memcpy(ioaddr, fw_data, copy_size);
		iounmap(ioaddr);

		copy_offset += MAP_SIZE;
	}
	wmb();

err_fw:
	release_firmware(fw);
out:
	return ret;
}

static int mloader_fw_upload(void)
{
	int i, ret;
	struct dbx500_mloader_pdata *pdata = mloader_priv->pdata;

	for (i = 0; i < pdata->nr_fws; i++) {
		ret = mloader_fw_send(&pdata->fws[i]);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_err(&mloader_priv->pdev->dev,
				"Failed to upload %s firmware", pdata->fws[i].name);
	return ret;
}

static int mloader_fw_mmapdump(struct file *file, struct vm_area_struct *vma)
{
	int i;
	unsigned long dump_size = 0;
	unsigned long vma_start = vma->vm_start;

	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	for (i = 0 ; i < mloader_priv->pdata->nr_areas ; i++)
		dump_size += mloader_priv->pdata->areas[i].size;

	if ((vma->vm_end - vma->vm_start) < dump_size)
		return -EINVAL;

	for (i = 0 ; i < mloader_priv->pdata->nr_areas ; i++) {
		if (remap_pfn_range(vma,
			    vma_start,
			    mloader_priv->pdata->areas[i].start >> PAGE_SHIFT,
			    mloader_priv->pdata->areas[i].size,
			    vma->vm_page_prot))
			return -EAGAIN;
		vma_start += mloader_priv->pdata->areas[i].size;
	}
	return 0;
}

static void mloader_fw_dumpinfo(struct dump_image *images)
{
	u32 offset = 0;
	int i;

	for (i = 0 ; i < mloader_priv->pdata->nr_areas ; i++) {
		strncpy(images[i].name,
				mloader_priv->pdata->areas[i].name, MAX_NAME);
		images[i].name[MAX_NAME-1] = 0;
		images[i].offset = offset;
		images[i].size = mloader_priv->pdata->areas[i].size;
		offset += mloader_priv->pdata->areas[i].size;
	}
}

static long mloader_fw_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	long ret = 0;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case ML_UPLOAD:
		ret = mloader_fw_upload();
		break;
	case ML_GET_NBIMAGES:
		ret = put_user(mloader_priv->pdata->nr_areas,
				(unsigned long __user *)argp);
		break;
	case ML_GET_DUMPINFO: {
		struct dump_image *dump_images;
		dump_images = kzalloc(mloader_priv->pdata->nr_areas
				* sizeof(struct dump_image), GFP_ATOMIC);
		mloader_fw_dumpinfo(dump_images);
		ret = copy_to_user(argp, (void *) dump_images,
				mloader_priv->pdata->nr_areas
				* sizeof(struct dump_image)) ? -EFAULT : 0;
		kfree(dump_images);
		break;
	}
	case ML_GET_FUSEINFO: {
		ret = copy_to_user(argp, (void *) mloader_priv->uid_base,
				mloader_priv->size) ? -EFAULT : 0;
		break;
	}
	default:
		ret = -EPERM;
		break;
	}

	return ret;
}

static const struct file_operations modem_fw_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl		= mloader_fw_ioctl,
	.mmap			= mloader_fw_mmapdump,
};

static int __devinit mloader_fw_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct resource *res = NULL;

	mloader_priv = kzalloc(sizeof(*mloader_priv), GFP_ATOMIC);
	if (!mloader_priv) {
		ret = -ENOMEM;
		goto out;
	}

	mloader_priv->pdev = pdev;
	mloader_priv->pdata = pdev->dev.platform_data;

	mloader_priv->misc_dev.minor = MISC_DYNAMIC_MINOR;
	mloader_priv->misc_dev.name = DEVICE_NAME;
	mloader_priv->misc_dev.fops = &modem_fw_fops;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(res == NULL) {
		ret = -EINVAL;
		goto err_free_priv;
	}

	mloader_priv->size = resource_size(res);
	mloader_priv->uid_base = ioremap(res->start, mloader_priv->size);

	if (!mloader_priv->uid_base) {
		   ret = -ENOMEM;
		   goto err_free_priv;
	}

	ret = misc_register(&mloader_priv->misc_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't misc_register\n");
		goto err_free_priv;
	}

	dev_info(&mloader_priv->pdev->dev, "mloader device register\n");

	if (mloader_priv->pdata  == NULL) {
		ret = -EINVAL;
		goto err_free_priv;
	}

	for (i = 0 ; i < mloader_priv->pdata->nr_areas ; i++) {
		dev_dbg(&mloader_priv->pdev->dev,
				"Area:%d (name:%s start:%x size:%x)\n",
				i, mloader_priv->pdata->areas[i].name,
				mloader_priv->pdata->areas[i].start,
				mloader_priv->pdata->areas[i].size);
	}

	for (i = 0 ; i < mloader_priv->pdata->nr_fws ; i++) {
		dev_dbg(&mloader_priv->pdev->dev,
				"Firmware:%d (name:%s offset:%x "
				"area_name:%s area_start:%x area_size:%x)\n",
				i, mloader_priv->pdata->fws[i].name,
				mloader_priv->pdata->fws[i].offset,
				mloader_priv->pdata->fws[i].area->name,
				mloader_priv->pdata->fws[i].area->start,
				mloader_priv->pdata->fws[i].area->size);
	}

	return ret;

err_free_priv:
	kfree(mloader_priv);
out:
	return ret;
}

static int __devexit mloader_fw_remove(struct platform_device *pdev)
{
	int err;

	err = misc_register(&mloader_priv->misc_dev);
	if (err < 0)
		dev_err(&pdev->dev, "can't misc_deregister, %d\n", err);

	kfree(mloader_priv);

	return err;
}

static struct platform_driver mloader_fw_driver = {
	.driver.name	= DEVICE_NAME,
	.driver.owner	= THIS_MODULE,
	.probe		= mloader_fw_probe,
	.remove		= __devexit_p(mloader_fw_remove),
};

static int __init mloader_fw_init(void)
{
	return platform_driver_register(&mloader_fw_driver);
}

static void __exit mloader_fw_exit(void)
{
	kfree(mloader_priv);
	platform_driver_unregister(&mloader_fw_driver);
}

module_init(mloader_fw_init);
module_exit(mloader_fw_exit);
MODULE_DESCRIPTION("ST-Ericsson modem loader firmware");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ludovic Barre <ludovic.barre@stericsson.com>");
