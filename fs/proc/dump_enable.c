#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>

#include <linux/uaccess.h>
#include <linux/file.h>


#define PARAM_FILE_NAME	"/mnt/.lfs/debug_level.inf"
#define PARAM_RD	0
#define PARAM_WR	1
#define DEBUG_LOW	"DLOW"
#define DEBUG_MID	"DMID"
#define DEBUG_HIGH	"DHIG"

static char __user param_buf[4];

static int lfs_param_op(int dir, int flags)
{
	struct file *filp;
	mm_segment_t fs;

	int ret;

	filp = filp_open(PARAM_FILE_NAME, flags, 0);

	if (IS_ERR(filp)) {
		pr_err("%s: filp_open failed. (%ld)\n", __FUNCTION__,
				PTR_ERR(filp));
		return -1;
	}

	fs = get_fs();
	set_fs(get_ds());

	if (dir == PARAM_RD)
		ret = filp->f_op->read(filp, param_buf, sizeof(param_buf), &filp->f_pos);
	else
		ret = filp->f_op->write(filp, param_buf, sizeof(param_buf), &filp->f_pos);

	set_fs(fs);
	filp_close(filp, NULL);

	return ret;
}

static int dump_enable_proc_show(struct seq_file *m, void *v)
{
	int ret;

	ret = lfs_param_op(PARAM_RD, O_RDONLY);

	if (ret == sizeof(param_buf))
		pr_info("%s: read debug_level.inf successfully.\n", __FUNCTION__);

	pr_info("%s: DEBUG_LEVEL = (%s)\n", __FUNCTION__, param_buf);

	if (!(strncmp(param_buf, DEBUG_LOW, sizeof(param_buf))))
		seq_printf(m, "0\n");

	else if (!(strncmp(param_buf, DEBUG_MID, sizeof(param_buf))))
		seq_printf(m, "1\n");

	else if (!(strncmp(param_buf, DEBUG_HIGH, sizeof(param_buf))))
		seq_printf(m, "2\n");

	else
		pr_err("%s: read invalid debug level\n", __FUNCTION__);

	return 0;
}

static int dump_enable_proc_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	char ctl[2];
	int ret;

	if (count != 2 || *ppos)
		return -EINVAL;

	if (copy_from_user(ctl, buf, count))
		return -EFAULT;

	switch (ctl[0]) {
	case '0':
		strncpy(param_buf, DEBUG_LOW, sizeof(param_buf));
		break;
	case '1':
		strncpy(param_buf, DEBUG_MID, sizeof(param_buf));
		break;
	case '2':
		strncpy(param_buf, DEBUG_HIGH, sizeof(param_buf));
		break;
	default:
		pr_err("%s: invalid value of dump_enable\n", __FUNCTION__);
		return -EINVAL;
	}

	pr_info("%s: DEBUG_LEVEL = (%s)\n", __FUNCTION__, param_buf);

	ret = lfs_param_op(PARAM_WR, O_RDWR|O_SYNC);

	if (ret == sizeof(param_buf))
		pr_info("%s: write debug_level.inf successfully.\n", __FUNCTION__);

	return count;
}

static int dump_enable_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dump_enable_proc_show, NULL);
}

static const struct file_operations dump_enable_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= dump_enable_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= dump_enable_proc_write,
	.release	= single_release,
};

static int __init proc_dump_enable_init(void)
{
	proc_create("dump_enable", 0644, NULL, &dump_enable_proc_fops);
	return 0;
}
module_init(proc_dump_enable_init);
