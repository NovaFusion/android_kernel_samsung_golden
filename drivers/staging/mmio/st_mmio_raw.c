/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Pankaj Chauhan <pankaj.chauhan@stericsson.com> for ST-Ericsson.
 * Author: Vincent Abriou <vincent.abriou@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#include "st_mmio_common.h"

#define ISP_REGION_IO				(0xE0000000)
#define SIA_ISP_REG_ADDR			(0x521E4)
#define SIA_BASE_ADDR				(0x54000)
#define SIA_ISP_MEM				(0x56000)
#define SIA_ISP_MEM_PAGE_REG			(0x54070)
#define SIA_ISP_MCU_SYS_ADDR0_OFFSET	(SIA_BASE_ADDR + 0x40)
#define SIA_ISP_MCU_SYS_SIZE0_OFFSET	(SIA_BASE_ADDR + 0x42)
#define SIA_ISP_MCU_SYS_ADDR1_OFFSET	(SIA_ISP_MCU_SYS_ADDR0_OFFSET + 0x04)
#define SIA_ISP_MCU_SYS_SIZE1_OFFSET	(SIA_ISP_MCU_SYS_SIZE0_OFFSET + 0x04)
#define SIA_ISP_MCU_IO_ADDR0_HI		(SIA_BASE_ADDR + 0x60)

/* HTimer enable in CR register */
#define PICTOR_IN_XP70_L2_MEM_BASE_ADDR		(0x40000)
#define PICTOR_IN_XP70_TCDM_MEM_BASE_ADDR	(0x60000)
#define L2_PSRAM_MEM_SIZE			(0x10000)

#define FW_TO_HOST_ADDR_MASK		(0x00001FFF)
#define FW_TO_HOST_ADDR_SHIFT		(0xD)
#define FW_TO_HOST_CLR_MASK		(0x3F)
#define PHY_TO_ISP_MCU_IO_ADDR0_HI(x)	(((x) >> 24) << 8)
#define XP70_ADDR_MASK			(0x00FFFFFF)

#define ISP_WRITE_DATA_SIZE		(0x4)

/*
 * For 30 fps video, there is 33 msec delay between every two frames
 * MMIO driver reads traces from trace buffer every XP70_TIMEOUT_MSEC.
 * If traces are not read in time from trace buffer, camera firmware
 * will start overwiting the traces as size of trace buffer is limited.
 */
#define XP70_TIMEOUT_MSEC	30
#define XP70_DEFAULT_MSG_ID	(0xCDCDCDCD)
#define XP70_MAX_BLOCK_ID	(0xFFFFFFFF)

/*
 * The one and only private data holder. Default inited to NULL.
 * Declare it here so no code above can use it directly.
 */
static struct mmio_info *raw_info;

/*
 * This function converts a given logical memory region size
 * to appropriate ISP_MCU_SYS_SIZEx register value.
 */
static int get_mcu_sys_size(u32 size, u32 *val)
{
	int ret = 0;

	if (size > 0 && size <= SZ_4K)
		*val = 4;
	else if (size > SZ_4K && size <= SZ_8K)
		*val = 5;
	else if (size > SZ_8K && size <= SZ_16K)
		*val = 6;
	else if (size > SZ_16K && size <= SZ_32K)
		*val = 7;
	else if (size > SZ_32K && size <= SZ_64K)
		*val = 0;
	else if (size > SZ_64K && size <= SZ_1M)
		*val = 1;
	else if (size > SZ_1M  && size <= SZ_16M)
		*val = 2;
	else if (size > SZ_16M && size <= SZ_256M)
		*val = 3;
	else
		ret = -EINVAL;

	return ret;
}

static u32 t1_to_arm(u32 t1_addr, void __iomem *smia_base_address,
		u16 *p_mem_page)
{
	u16 mem_page_update = 0;
	mem_page_update = (t1_addr >> FW_TO_HOST_ADDR_SHIFT) &
		FW_TO_HOST_CLR_MASK;

	if (mem_page_update != *p_mem_page) {
		/* Update sia_mem_page register */
		dev_dbg(raw_info->dev, "mem_page_update=0x%x, mem_page=0x%x\n",
				mem_page_update, *p_mem_page);
		writew(mem_page_update, smia_base_address +
				SIA_ISP_MEM_PAGE_REG);
		*p_mem_page = mem_page_update;
	}

	return SIA_ISP_MEM + (t1_addr & FW_TO_HOST_ADDR_MASK);
}

static ssize_t xp70_data_show(struct device *device,
		struct device_attribute *attr, char *buf)
{
	int i;
	int len;
	int size = 0;
	int count = 0;
	int first_index;
	mutex_lock(&raw_info->lock);
	first_index = raw_info->trace_status.prev_block_id + 1;

	if (!raw_info->trace_buffer || raw_info->trace_buffer->block_id ==
			XP70_MAX_BLOCK_ID)
		goto out_unlock;

	if (raw_info->trace_allowed != 1) {
		dev_warn(raw_info->dev, "xp70 trace disabled in kernel\n");
		size = sprintf(buf, "xp70 trace disabled in kernel, "
				"use sysfs to enable\n");
		goto out_unlock;
	}

	count = raw_info->trace_buffer->block_id -
		raw_info->trace_status.prev_block_id;

	if ((raw_info->trace_buffer->overwrite_count
				- raw_info->trace_status.prev_overwrite_count)
			* XP70_NB_BLOCK
			+ (raw_info->trace_buffer->block_id
				- raw_info->trace_status.prev_block_id)
			>= XP70_NB_BLOCK) {
		/* overflow case */
		raw_info->trace_status.prev_block_id =
			raw_info->trace_buffer->block_id - XP70_NB_BLOCK;
		first_index = raw_info->trace_buffer->block_id + 1;
		count = XP70_NB_BLOCK;
		len = sprintf(buf, "XP70 trace overflow\n");
		size += len;
		buf += len;
	}

	for (i = first_index; count; count--) {
		int msg_len;

		if (i < 0 || i >= XP70_NB_BLOCK || count > XP70_NB_BLOCK) {
			dev_err(raw_info->dev, "trace index out-of-bounds\n");
			goto out_unlock;
		}

		msg_len = strnlen(raw_info->trace_buffer->block[i].data,
				XP70_BLOCK_SIZE);

		if (msg_len > 0) {
			/* zero terminate full length message */
			if (msg_len == XP70_BLOCK_SIZE)
				raw_info->trace_buffer->block[i].data[
					XP70_BLOCK_SIZE - 1] = '\0';

			len = snprintf(buf, PAGE_SIZE - size, "%d %s\n",
					raw_info->trace_buffer->block[i].msg_id,
					raw_info->trace_buffer->block[i].data);

			if (len > PAGE_SIZE - size) {
				dev_err(raw_info->dev,
						"sysfs buffer overflow\n");
				size = PAGE_SIZE;
				goto out_unlock;
			}

			size += len;
			buf += len;
		}

		i = (i + 1) % XP70_NB_BLOCK;
	}

out_unlock:
	mutex_unlock(&raw_info->lock);
	return size;
}

static ssize_t xp70_trace_allowed_show(struct device *device,
		struct device_attribute *attr,
		char *buf)
{
	int len;
	len = sprintf(buf, "%d\n", raw_info->trace_allowed);
	return len;
}

static ssize_t xp70_trace_allowed_store(struct device *device,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	if (count <= 0) {
		dev_err(raw_info->dev, "empty buffer to store\n");
		return 0;
	}

	if (buf[0] == '1')
		raw_info->trace_allowed = 1;
	else if (buf[0] == '0')
		raw_info->trace_allowed = 0;
	else
		dev_err(raw_info->dev, "illegal trace_allowed val %c\n",
				buf[0]);

	return count;
}

static struct device_attribute xp70_device_attrs[] = {
	__ATTR_RO(xp70_data),
	__ATTR(trace_allowed, S_IRUGO | S_IWUSR, xp70_trace_allowed_show,
			xp70_trace_allowed_store),
	__ATTR_NULL
};

static void xp70_buffer_wqtask(struct work_struct *data)
{
	int i;
	int first_index = raw_info->trace_status.prev_block_id + 1;
	int count;
	mutex_lock(&raw_info->lock);

	if (!raw_info->trace_buffer)
		goto out_err;

	dev_dbg(raw_info->dev, "xp70 overwrite_cnt=%d (0x%x) blk_id=%d (0x%x)",
			raw_info->trace_buffer->overwrite_count,
			raw_info->trace_buffer->overwrite_count,
			raw_info->trace_buffer->block_id,
			raw_info->trace_buffer->block_id);

	/* check if trace already started */
	if (raw_info->trace_buffer->block_id == XP70_MAX_BLOCK_ID ||
			raw_info->trace_buffer->block_id ==
			XP70_DEFAULT_MSG_ID ||
			raw_info->trace_buffer->overwrite_count ==
			XP70_DEFAULT_MSG_ID)
		goto out;

	if ((raw_info->trace_buffer->overwrite_count
				- raw_info->trace_status.prev_overwrite_count)
			* XP70_NB_BLOCK
			+ (raw_info->trace_buffer->block_id
				- raw_info->trace_status.prev_block_id)
			>= XP70_NB_BLOCK) {
		/* overflow case */
		raw_info->trace_status.prev_block_id =
			raw_info->trace_buffer->block_id - XP70_NB_BLOCK;
		first_index = raw_info->trace_buffer->block_id + 1;
		count = XP70_NB_BLOCK;

		pr_info_ratelimited("XP70 trace overflow\n");
	} else if (raw_info->trace_buffer->block_id
			>= raw_info->trace_status.prev_block_id) {
		count = raw_info->trace_buffer->block_id -
			raw_info->trace_status.prev_block_id;
	} else {
		u32 block_id, prev_block_id, diff;
		block_id = (u32)(raw_info->trace_buffer->block_id);
		prev_block_id = (u32)(raw_info->trace_status.prev_block_id);
		diff = (block_id + XP70_NB_BLOCK) - prev_block_id;
		count = (u32)diff;
	}

	for (i = first_index; count; count--) {
		if (i < 0 || i >= XP70_NB_BLOCK || count > XP70_NB_BLOCK) {
			pr_info_ratelimited("trace index out-of-bounds"
					"i=%d count=%d XP70_NB_BLOCK=%d\n",
					i, count, XP70_NB_BLOCK);

			break;
		}

		if (raw_info->trace_buffer->block[i].msg_id !=
				XP70_DEFAULT_MSG_ID) {
			int msg_len = strnlen(
					raw_info->trace_buffer->block[i].data,
					XP70_BLOCK_SIZE);

			/* zero terminate full length message */
			if (msg_len > 0) {
				if (msg_len == XP70_BLOCK_SIZE)
					raw_info->trace_buffer->block[i].data[
						XP70_BLOCK_SIZE - 1] = '\0';

				dev_info(raw_info->dev,
						"%d %s\n",
						raw_info->trace_buffer->
						block[i].msg_id,
						raw_info->trace_buffer->
						block[i].data);
			}
		}

		i = (i + 1) % XP70_NB_BLOCK;
	}

	raw_info->trace_status.prev_overwrite_count =
		raw_info->trace_buffer->overwrite_count;
	raw_info->trace_status.prev_block_id = raw_info->trace_buffer->block_id;
out:
	/* Schedule work */
	if (!schedule_delayed_work(&raw_info->trace_work,
				msecs_to_jiffies(XP70_TIMEOUT_MSEC)))
		dev_info(raw_info->dev, "failed to schedule work\n");

out_err:
	mutex_unlock(&raw_info->lock);
	return;
}

static int mmio_raw_load_xp70_fw(struct mmio_info *raw_info,
		struct xp70_fw_t *xp70_fw)
{
	u32 i = 0;
	u32 offset = 0;
	u32 itval = 0;
	u16 mem_page = 0;
	void __iomem *addr_split = NULL;
	void __iomem *addr_data = NULL;
	int err = 0;

	if (xp70_fw->size_split != 0) {
		err = copy_user_buffer(&addr_split, xp70_fw->addr_split,
				xp70_fw->size_split);

		if (err)
			goto err_exit;

		writel(0x0, raw_info->siabase + SIA_ISP_REG_ADDR);

		/* Put the low 64k IRP firmware in ISP MCU L2 PSRAM */
		for (i = PICTOR_IN_XP70_L2_MEM_BASE_ADDR;
				i < (PICTOR_IN_XP70_L2_MEM_BASE_ADDR +
					L2_PSRAM_MEM_SIZE); i = i + 2) {
			itval = t1_to_arm(i, raw_info->siabase, &mem_page);
			itval = ((u32) raw_info->siabase) + itval;
			/* Copy fw in L2 */
			writew((*((u16 *) addr_split + offset++)), itval);
		}

		kfree(addr_split);
	}

	if (xp70_fw->size_data != 0) {
		mem_page = 0;
		offset = 0;
		err = copy_user_buffer(&addr_data, xp70_fw->addr_data,
				xp70_fw->size_data);

		if (err)
			goto err_exit;

		writel(0x0, raw_info->siabase + SIA_ISP_REG_ADDR);

		for (i = PICTOR_IN_XP70_TCDM_MEM_BASE_ADDR;
				i < (PICTOR_IN_XP70_TCDM_MEM_BASE_ADDR +
					(xp70_fw->size_data)); i = i + 2) {
			itval = t1_to_arm(i, raw_info->siabase, &mem_page);
			itval = ((u32) raw_info->siabase) + itval;
			/* Copy fw data in TCDM */
			writew((*((u16 *) addr_data + offset++)), itval);
		}

		kfree(addr_data);
	}

	if (xp70_fw->size_esram_ext != 0) {
		/*
		 * ISP_MCU_SYS_ADDRx XP70 register (@ of ESRAM where the
		 * external code has been loaded
		 */
		writew(upper_16_bits(xp70_fw->addr_esram_ext),
				raw_info->siabase +
				SIA_ISP_MCU_SYS_ADDR0_OFFSET);
		/* ISP_MCU_SYS_SIZEx XP70 register (size of the code =64KB) */
		writew(0x0, raw_info->siabase + SIA_ISP_MCU_SYS_SIZE0_OFFSET);
	}

	if (xp70_fw->size_sdram_ext != 0) {
		/*
		 * ISP_MCU_SYS_ADDRx XP70 register (@ of SDRAM where the
		 * external code has been loaded
		 */
		writew(upper_16_bits(xp70_fw->addr_sdram_ext),
				raw_info->siabase +
				SIA_ISP_MCU_SYS_ADDR1_OFFSET);
		/* ISP_MCU_SYS_SIZEx XP70 register */
		err = get_mcu_sys_size(xp70_fw->size_sdram_ext, &itval);

		if (err)
			goto err_exit;

		writew(itval, raw_info->siabase + SIA_ISP_MCU_SYS_SIZE1_OFFSET);
	}

	return 0;
err_exit:
	dev_err(raw_info->dev, "Loading XP70 fw failed\n");
	return -EFAULT;
}

static int mmio_raw_map_statistics_mem_area(
		struct mmio_info *raw_info,
		void __iomem *addr_to_map)
{
	u16 value;
	BUG_ON(addr_to_map == NULL);
	/* 16 Mbyte aligned page */
	value = PHY_TO_ISP_MCU_IO_ADDR0_HI(*((u32 *)addr_to_map));
	writew(value, raw_info->siabase + SIA_ISP_MCU_IO_ADDR0_HI);
	/* Return the address in the XP70 address space */
	*((u32 *)addr_to_map) = (*((u32 *)addr_to_map) & XP70_ADDR_MASK) |
		ISP_REGION_IO;
	return 0;
}


static int mmio_raw_isp_write(struct mmio_info *raw_info,
		struct isp_write_t *isp_write_p)
{
	int err = 0, i;
	void __iomem *data = NULL;
	void __iomem *addr = NULL;
	u16 mem_page = 0;

	dev_dbg(raw_info->dev, "%s\n", __func__);

	if (!isp_write_p->count) {
		dev_warn(raw_info->dev, "no data to write to isp\n");
		return -EINVAL;
	}

	err = copy_user_buffer(&data, isp_write_p->data,
			isp_write_p->count * ISP_WRITE_DATA_SIZE);

	if (err)
		goto out;

	for (i = 0; i < isp_write_p->count; i++) {
		addr = (void *)(raw_info->siabase +
				t1_to_arm(isp_write_p->t1_dest
					+ ISP_WRITE_DATA_SIZE * i,
					raw_info->siabase, &mem_page));
		*((u32 *)addr) = *((u32 *)data + i);
	}

	kfree(data);
out:
	return err;
}

static int mmio_raw_set_trace_buffer(struct mmio_info *raw_info,
		struct trace_buf_t *buf)
{
	u32 i;
	int ret = 0;

	dev_dbg(raw_info->dev, "%s\n", __func__);

	if (raw_info->trace_allowed != 1) {
		dev_warn(raw_info->dev, "trace disabled in kernel\n");
		ret = -EPERM;
		goto out;
	}

	if (!buf->size || !buf->address
			|| buf->size < sizeof(struct mmio_trace)) {
		dev_err(raw_info->dev, "invalid xp70 trace buffer\n");
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&raw_info->lock);
	if (raw_info->trace_buffer) {
		dev_info(raw_info->dev, "unmap old buffer");
		iounmap(raw_info->trace_buffer);
		raw_info->trace_buffer = NULL;
	}

	raw_info->trace_buffer = ioremap((u32)buf->address, buf->size);

	if (!raw_info->trace_buffer) {
		dev_err(raw_info->dev, "failed to map trace buffer\n");
		ret = -ENOMEM;
		goto out_unlock;
	}

	dev_info(raw_info->dev,
			"xp70 overwrite_cnt=%d (0x%x) blk_id=%d (0x%x)",
			raw_info->trace_buffer->overwrite_count,
			raw_info->trace_buffer->overwrite_count,
			raw_info->trace_buffer->block_id,
			raw_info->trace_buffer->block_id);
#ifndef CAM_SHARED_MEM_DEBUG

	/* Reset the allocated buffer contents */
	for (i = 0; i < XP70_NB_BLOCK; i++)
		raw_info->trace_buffer->block[i].msg_id = XP70_DEFAULT_MSG_ID;

#endif /* CAM_SHARED_MEMORY_DEBUG */
	dev_info(raw_info->dev,
			"xp70 overwrite_cnt=%d (0x%x) blk_id=%d (0x%x)\n",
			raw_info->trace_buffer->overwrite_count,
			raw_info->trace_buffer->overwrite_count,
			raw_info->trace_buffer->block_id,
			raw_info->trace_buffer->block_id);
	raw_info->trace_status.prev_overwrite_count = 0;
	raw_info->trace_status.prev_block_id = 0;

	/* schedule work */
	if (!schedule_delayed_work(&raw_info->trace_work,
				msecs_to_jiffies(XP70_TIMEOUT_MSEC)))
		dev_err(raw_info->dev, "failed to schedule work\n");

out_unlock:
	mutex_unlock(&raw_info->lock);
out:
	return ret;
}

static long mmio_raw_ioctl(struct file *filp, u32 cmd,
		unsigned long arg)
{
	struct mmio_input_output_t data;
	int no_of_bytes;
	int ret = 0;
	struct mmio_info *raw_info =
		(struct mmio_info *)filp->private_data;
	BUG_ON(raw_info == NULL);

	switch (cmd) {
	case MMIO_CAM_INITBOARD:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user(&data, (struct mmio_input_output_t *)arg,
					no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		raw_info->pdata->camera_slot = data.mmio_arg.camera_slot;

		ret = mmio_cam_initboard(raw_info);
		break;
	case MMIO_CAM_DESINITBOARD:
		ret = mmio_cam_desinitboard(raw_info);
		raw_info->pdata->camera_slot = -1;
		break;
	case MMIO_CAM_PWR_SENSOR:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_cam_pwr_sensor(raw_info, data.mmio_arg.power_on);
		break;
	case MMIO_CAM_SET_EXT_CLK:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_cam_control_clocks(raw_info, data.mmio_arg.power_on);
		break;
	case MMIO_CAM_LOAD_XP70_FW:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_raw_load_xp70_fw(raw_info, &data.mmio_arg.xp70_fw);
		break;
	case MMIO_CAM_MAP_STATS_AREA:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_raw_map_statistics_mem_area(raw_info,
				&data.mmio_arg.addr_to_map);

		if (0 != ret) {
			dev_err(raw_info->dev,
					"Unable to map Statistics Mem area\n");
			break;
		}

		if (copy_to_user((struct mmio_input_output_t *)arg,
					&data, sizeof(no_of_bytes))) {
			dev_err(raw_info->dev,
					"Copy to userspace failed\n");
			ret = -EFAULT;
			break;
		}

		break;
	case MMIO_CAM_INITMMDSPTIMER:
		ret = mmio_cam_init_mmdsp_timer(raw_info);
		break;
	case MMIO_CAM_ISP_WRITE:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_raw_isp_write(raw_info, &data.mmio_arg.isp_write);
		break;
	case MMIO_CAM_GET_IP_GPIO:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *)arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		data.mmio_arg.xshutdown_info.ip_gpio =
			raw_info->pdata->reset_ipgpio
			[data.mmio_arg.xshutdown_info.camera_function];

		if (copy_to_user((struct mmio_input_output_t *)arg,
					&data, sizeof(no_of_bytes))) {
			dev_err(raw_info->dev,
					"Copy to userspace failed\n");
			ret = -EFAULT;
			break;
		}

		break;
	case MMIO_CAM_SET_TRACE_BUFFER:
		no_of_bytes = sizeof(struct mmio_input_output_t);
		memset(&data, 0, sizeof(struct mmio_input_output_t));

		if (copy_from_user
				(&data, (struct mmio_input_output_t *) arg,
				 no_of_bytes)) {
			dev_err(raw_info->dev,
					"Copy from userspace failed\n");
			ret = -EFAULT;
			break;
		}

		ret = mmio_raw_set_trace_buffer(raw_info,
				&data.mmio_arg.trace_buf);
		break;
	default:
		dev_err(raw_info->dev, "Not an ioctl for RAW sensor.\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mmio_raw_release(struct inode *node, struct file *filp)
{
	struct mmio_info *raw_info =
		(struct mmio_info *)filp->private_data;
	BUG_ON(raw_info == NULL);

	mutex_lock(&raw_info->lock);
	if (raw_info->trace_buffer) {
		flush_delayed_work(&raw_info->trace_work);
		iounmap(raw_info->trace_buffer);
		raw_info->trace_buffer = NULL;
	}
	mutex_unlock(&raw_info->lock);

	/* If not already done... */
	if (raw_info->pdata->camera_slot != -1) {
		/* ...force sensor to power off */
		mmio_cam_pwr_sensor(raw_info, false);
		mmio_cam_control_clocks(raw_info, false);
		/* ...force to desinit board */
		mmio_cam_desinitboard(raw_info);
		raw_info->pdata->camera_slot = -1;
	}

	return 0;
}

static int mmio_raw_open(struct inode *node, struct file *filp)
{
	filp->private_data = raw_info;	/* Hook our mmio raw_info */
	return 0;
}

static const struct file_operations mmio_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mmio_raw_ioctl,
	.open = mmio_raw_open,
	.release = mmio_raw_release,
};

/**
 * mmio_raw_probe() - Initialize MMIO raw Camera resources.
 * @pdev: Platform device.
 *
 * Initialize the module and register misc device.
 *
 * Returns:
 *	0 if there is no err.
 *	-ENOMEM if allocation fails.
 *	-EEXIST if device has already been started.
 *	Error codes from misc_register.
 */
static int __devinit mmio_raw_probe(struct platform_device *pdev)
{
	int err;
	int i;
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	/* Initialize private data. */
	raw_info = kzalloc(sizeof(struct mmio_info), GFP_KERNEL);

	if (!raw_info) {
		dev_err(&pdev->dev, "Could not alloc raw_info struct\n");
		err = -ENOMEM;
		goto err_alloc;
	}

	/* Fill in private data */
	raw_info->pdata                    = pdev->dev.platform_data;

    if (raw_info->pdata == NULL) {
        dev_err(&pdev->dev, "raw_info->pdata is NULL pointer\n");
        BUG_ON(raw_info->pdata == NULL);
        goto err_miscreg;
    }
	
	raw_info->dev                      = &pdev->dev;
	raw_info->pdata->dev               = &pdev->dev;
	raw_info->xshutdown_enabled        = 0;
	raw_info->xshutdown_is_active_high = 0;
	raw_info->trace_allowed            = 0;
	mutex_init(&raw_info->lock);

	/* Register Misc character device */
	raw_info->misc_dev.minor           = MISC_DYNAMIC_MINOR;
	raw_info->misc_dev.name            = MMIO_RAW_NAME;
	raw_info->misc_dev.fops            = &mmio_fops;
	raw_info->misc_dev.parent          = pdev->dev.parent;
	err = misc_register(&(raw_info->misc_dev));

	if (err) {
		dev_err(&pdev->dev, "Error %d registering misc dev!", err);
		goto err_miscreg;
	}

	/* Memory mapping */
	raw_info->siabase = ioremap(raw_info->pdata->sia_base,
			SIA_ISP_MCU_SYS_SIZE);

	if (!raw_info->siabase) {
		dev_err(raw_info->dev, "Could not ioremap SIA_BASE\n");
		err = -ENOMEM;
		goto err_ioremap_sia_base;
	}

	raw_info->crbase = ioremap(raw_info->pdata->cr_base, PAGE_SIZE);

	if (!raw_info->crbase) {
		dev_err(raw_info->dev, "Could not ioremap CR_BASE\n");
		err = -ENOMEM;
		goto err_ioremap_cr_base;
	}

	/* create sysfs entries */
	for (i = 0; attr_name(xp70_device_attrs[i]); i++) {
		ret = device_create_file(raw_info->misc_dev.this_device,
				&xp70_device_attrs[i]);

		if (ret) {
			dev_err(raw_info->dev, "Error creating SYSFS entry"
					" %s (%d)\n",
					xp70_device_attrs[i].attr.name,
					ret);
		}
	}

	INIT_DELAYED_WORK(&raw_info->trace_work, xp70_buffer_wqtask);

	dev_info(raw_info->dev, "mmio driver initialized with minor=%d\n",
			raw_info->misc_dev.minor);

	return 0;

err_ioremap_cr_base:
	iounmap(raw_info->siabase);
err_ioremap_sia_base:
	misc_deregister(&raw_info->misc_dev);
err_miscreg:
	kfree(raw_info);
	raw_info = NULL;
err_alloc:
	return err;
}

/**
 * mmio_raw_remove() - Release MMIO raw Camera resources.
 * @pdev:	Platform device.
 *
 * Remove misc device and free resources.
 *
 * Returns:
 *	0 if success.
 *	Error codes from misc_deregister.
 */
static int __devexit mmio_raw_remove(struct platform_device *pdev)
{
	int err;
	int i;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	if (!raw_info)
		return 0;

	flush_scheduled_work();

	/* sysfs parameters */
	for (i = 0; attr_name(xp70_device_attrs[i]); i++)
		device_remove_file(raw_info->misc_dev.this_device,
				&xp70_device_attrs[i]);

	err = misc_deregister(&raw_info->misc_dev);

	if (err)
		dev_err(&pdev->dev, "Error %d deregistering misc dev", err);

	iounmap(raw_info->siabase);
	iounmap(raw_info->crbase);
	mutex_destroy(&raw_info->lock);
	kfree(raw_info);
	raw_info = NULL;
	return 0;
}

/**
 * platform_driver definition:
 * mmio_raw_driver
 */
static struct platform_driver mmio_raw_driver = {
	.driver = {
		.name = MMIO_RAW_NAME,
		.owner = THIS_MODULE,
	},
	.probe = mmio_raw_probe,
	.remove = __devexit_p(mmio_raw_remove)
};

/**
 * mmio_raw_init() - Initialize module.
 *
 * Registers platform driver.
 */
static int __init mmio_raw_init(void)
{
	return platform_driver_register(&mmio_raw_driver);
}

/**
 * mmio_raw_exit() - Remove module.
 *
 * Unregisters platform driver.
 */
static void __exit mmio_raw_exit(void)
{
	platform_driver_unregister(&mmio_raw_driver);
}

module_init(mmio_raw_init);
module_exit(mmio_raw_exit);

MODULE_AUTHOR("Joakim Axelsson ST-Ericsson");
MODULE_AUTHOR("Pankaj Chauhan ST-Ericsson");
MODULE_AUTHOR("Vincent Abriou ST-Ericsson");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MMIO Camera driver");
