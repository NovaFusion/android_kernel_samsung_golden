/*
 * Copyright (C) 2011 ST-Ericsson SA
 *
 * Author: Etienne CARRIERE <etienne.carriere@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 *  HWREG: debug purpose module to map declared IOs and read/write
 *         access from debugfs entries.
 *
 * HWREG 32bit DB8500 v2.0 register access
 * =======================================
 *
 * 32bit read:
 * # echo <addr>  >  <debugfs>/mem/reg-addr
 * # cat <debugfs>/mem/reg-val
 *
 * 32bit write:
 * # echo <addr>   >  <debugfs>/mem/reg-addr
 * # echo <value>  >  <debugfs>/mem/reg-val
 *
 * <addr>   0x-prefixed hexadecimal
 * <value>  decimal or 0x-prefixed hexadecimal
 *
 * HWREG DB8500 formated read/write access
 * =======================================
 *
 * Read:  read data, data>>SHIFT, data&=MASK, output data
 *        [0xABCDEF98] shift=12 mask=0xFFF => 0x00000CDE
 * Write: read data, data &= ~(MASK<<SHIFT), data |= (VALUE<<SHIFT), write data
 *        [0xABCDEF98] shift=12 mask=0xFFF value=0x123 => [0xAB123F98]
 *
 * Usage:
 * # echo "CMD [OPTIONS] ADRESS [VALUE]" > $debugfs/mem/hwreg
 *
 * CMD      read      read access
 *          write     write access
 *
 * ADDRESS  target reg physical addr (0x-hexa)
 *
 * VALUE    (write) value to be updated
 *
 * OPTIONS
 *  -d|-dec            (read) output in decimal
 *  -h|-hexa           (read) output in 0x-hexa (default)
 *  -l|-w|-b           32bit (default), 16bit or 8bit reg access
 *  -m|-mask MASK      0x-hexa mask (default 0xFFFFFFFF)
 *  -s|-shift SHIFT    bit shift value (read:left, write:right)
 *  -o|-offset OFFSET  address offset to add to ADDRESS value
 *
 * Warning: bit shift operation is applied to bit-mask.
 * Warning: bit shift direction depends on read or right command.
 *
 * Examples:
 *
 *  before: [*ADDRESS = 0xABCDEF98]
 *  # echo read -h -mask 0xFFF -shift 12 ADDRESS > hwreg
 *  # cat hwreg-shift
 *  0x0000CDE
 *  # echo write -h -mask 0xFFF -shift 12 ADDRESS 0x123 > hwreg
 *  # cat hwreg-shift
 *  0x0000123
 *  after [*ADDRESS = 0xAB123F98]
 *
 *  before: [*ADDRESS = 0xABCDEF98]
 *  # echo read -h -mask 0x00F0F000 ADDRESS 0x12345678 > hwreg
 *  # cat hwreg-shift
 *  0x00C0E000
 *  # echo write -h -mask 0x00F0F000 ADDRESS 0x12345678 > hwreg
 *  # cat hwreg-shift
 *  0xAB3D5F98
 *  after [*ADDRESS = 0xAB123F98]
 *
 * Read DB8500 version (full ID, chip version ID, chip version ID):
 *
 * echo read 0x9001DBF4 > hwreg
 * cat hwreg
 * echo read -m 0xFFFF -s 8 0x9001DBF4 > hwreg
 * cat hwreg
 * echo read -m 0xFF -s 0 0x9001DBF4 > hwreg
 * cat hwreg
 *
 * Read and Enable/Disable I2C PRCMU clock:
 *
 * printf "I2CCLK = "  && echo read  -m 1 -s 8 0x80157520 > hwreg
 * cat /sys/kernel/debug/db8500/hwreg
 * printf "I2CCLK off" && echo write -m 1 -s 8 0x80157518 1 > hwreg
 * printf "I2CCLK on"  && echo write -m 1 -s 8 0x80157510 1 > hwreg
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <mach/hardware.h>

/*
 * temporary definitions
 * The following declarations are to be removed
 * when kernel/arch/arm/mach-ux8500/include/mach/db8500-regs.h is up-to-date
 */

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

static struct dentry	*hwreg_debugfs_dir;

/* 32bit read/write ressources */
static u32 debug_address; /* shared: single read/write access */

/* hwreg entry ressources */
struct hwreg_cfg {
	uint addr;		/* target physical addr to access */
	uint fmt;		/* format */
	uint mask;		/* read/write mask, applied before any bit shift */
	int  shift;		/* bit shift (read:right shift, write:left shift */
};
#define REG_FMT_DEC(c) ((c)->fmt & 0x1)		/* bit 0: 0=hexa, 1=dec */
#define REG_FMT_HEX(c) (!REG_FMT_DEC(c))	/* bit 0: 0=hexa, 1=dec */
#define REG_FMT_32B(c) (((c)->fmt & 0x6) == 0x0)	/* bit[2:1]=0 => 32b access */
#define REG_FMT_16B(c) (((c)->fmt & 0x6) == 0x2)	/* bit[2:1]=1 => 16b access */
#define REG_FMT_8B(c)  (((c)->fmt & 0x6) == 0x4)	/* bit[2:1]=2 => 8b access */

static struct hwreg_cfg hwreg_cfg = {
	.addr = 0,			/* default: invalid phys addr */
	.fmt = 0,			/* default: 32bit access, hex output */
	.mask = 0xFFFFFFFF,		/* default: no mask */
	.shift = 0,			/* default: no bit shift */
};

/* HWREG guts: mapping table */

struct hwreg_io_range {
	u32 base;
	u32 size;
	u8 *addr;
};

static struct hwreg_io_range *hwreg_io_current_map;

/*
 * HWREG guts: mapping table
 */
static struct hwreg_io_range hwreg_u8500_io_map[] = {
	/* Periph1 Peripherals */
	{.base = U8500_PER1_BASE,	.size = 0x10000},
	/* Periph2 Peripherals */
	{.base = U8500_PER2_BASE,	.size = 0x10000},
	/* Periph3 Peripherals */
	{.base = U8500_PER3_BASE,	.size = 0x10000},
	/* Periph4 Peripherals */
	{.base = U8500_PER4_BASE,	.size = 0x70000},
	/* Periph5 Periphals */
	{.base = U8500_PER5_BASE,	.size = 0x20000},
	/* Periph6 Peripherals */
	{.base = U8500_PER6_BASE,	.size = 0x10000},
	/*
	 * Snoop Control Unit, A9 Private interrupt IF,
	 * A9 private peripherals, Level-2 Cache Configuration registers,
	 * and some reserved area
	 */
	{.base = U8500_SCU_BASE,	.size = 0x4000},

	/* DISPLAY Ctrl. configuration registers */
	{.base = U8500_MCDE_BASE,	.size = SZ_4K},

	/* DSI1 link registers */
	{.base = U8500_DSI_LINK1_BASE,	.size = SZ_4K},

	/* DSI2 link registers */
	{.base = U8500_DSI_LINK2_BASE,	.size = SZ_4K},

	/* DSI3 link registers */
	{.base = U8500_DSI_LINK3_BASE,	.size = SZ_4K},

	/* DMA Ctrl. configuration registers (base address changed in V1) */
	{.base = U8500_DMA_BASE,	.size = SZ_4K},

	/* 0xB7A00000 -> 0xB7E04000: Modem I2C */
	{.base = U8500_MODEM_I2C,	.size = 0x404000},

	/* 0xA0390000 -> 0xA039FFFF: SBAG configuration registers */
	{.base = U8500_SBAG_BASE,	.size = SZ_4K},

	/* 0xA0300000 -> 0xA031FFFF: SGA configuration registers */
	{.base = U8500_SGA_BASE,	.size = 0x10000},

	/* 0xA0200000 -> 0xA02FFFFF: Smart Imaging Acc. Data Memory space (SIA) */
	{.base = U8500_SIA_BASE,	.size = 0x60000},

	/* 0xA0100000 -> 0xA01FFFFF: Smart Video Acc. Data Memory space (SVA) */
	{.base = U8500_SVA_BASE,	.size = 0x60000},

	/* 0x81000000 -> 0x8103FFFF: Main ICN Crossbar configuration registers */
	{.base = U8500_ICN_BASE,	.size = 0x2000},

	/* 0x80140000 -> 0x8014FFFF: HSEM (Semaphores) configuration  */
	{.base = U8500_HSEM_BASE,	.size = SZ_4K},

	/* 0x80130000 -> 0x8013FFFF: B2R2 configuration registers */
	{.base = U8500_B2R2_BASE,	.size = SZ_4K},

	/* 0x80100000 -> 0x8010FFFF: STM */
	{.base = U8500_STM_BASE,	.size = 0x10000},

	/* High part of embedded boot ROM */
	{.base = U8500_ASIC_ID_BASE,	.size = SZ_4K},

	/* 0x17C4 0000 : 0x17C4 007C */
	{.base = U8500_SCU_CD_R4_BASE,  .size = SZ_4K},

	/* 0x17D4 0000 : 0x17D4 041C */
	{.base = U8500_SCU_AD_R4_BASE,  .size = SZ_4K},

	/* 0x17E0 2000 : 0x17E0 2FFC */
	{.base = U8500_HSI2CMODEMR4_BASE, .size = SZ_4K},

	{.base = 0, .size = 0, },

};

static struct hwreg_io_range hwreg_u9540_io_map[] = {
	/* Periph1 Peripherals */
	{.base = U8500_PER1_BASE,	.size = 0x10000},
	/* Periph2 Peripherals */
	{.base = U8500_PER2_BASE,	.size = 0x10000},
	/* Periph3 Peripherals */
	{.base = U8500_PER3_BASE,	.size = 0x10000},
	/* Periph4 Peripherals */
	{.base = U8500_PER4_BASE,	.size = 0x70000},
	/* Periph5 Periphals */
	{.base = U8500_PER5_BASE,	.size = 0x20000},
	/* Periph6 Peripherals */
	{.base = U8500_PER6_BASE,	.size = 0x10000},
	/*
	 * Snoop Control Unit, A9 Private interrupt IF,
	 * A9 private peripherals, Level-2 Cache Configuration registers,
	 * and some reserved area
	 */
	{.base = U8500_SCU_BASE,	.size = 0x4000},

	/* DISPLAY Ctrl. configuration registers */
	{.base = U8500_MCDE_BASE,	.size = SZ_4K},

	/* DSI1 link registers */
	{.base = U8500_DSI_LINK1_BASE,	.size = SZ_4K},

	/* DSI2 link registers */
	{.base = U8500_DSI_LINK2_BASE,	.size = SZ_4K},

	/* DSI3 link registers */
	{.base = U8500_DSI_LINK3_BASE,	.size = SZ_4K},

	/* DMA Ctrl. configuration registers (base address changed in V1) */
	{.base = U8500_DMA_BASE,	.size = SZ_4K},

	/* 0xB7A00000 -> 0xB7E04000: Modem I2C */
	{.base = U8500_MODEM_I2C,	.size = 0x404000},

	/* 0xA0390000 -> 0xA039FFFF: SBAG configuration registers */
	{.base = U8500_SBAG_BASE,	.size = SZ_4K},

	/* 0xA0300000 -> 0xA031FFFF: SGA configuration registers */
	{.base = U8500_SGA_BASE,	.size = 0x10000},

	/* 0xA0200000 -> 0xA02FFFFF: Smart Imaging Acc. Data Memory space (SIA)
	 */
	{.base = U8500_SIA_BASE,	.size = 0x60000},

	/* 0xA0100000 -> 0xA01FFFFF: Smart Video Acc. Data Memory space (SVA) */
	{.base = U8500_SVA_BASE,	.size = 0x60000},

	/* 0x81000000 -> 0x8103FFFF: Main ICN Crossbar configuration registers
	 */
	{.base = U8500_ICN_BASE,	.size = 0x2000},

	/* 0x80140000 -> 0x8014FFFF: HSEM (Semaphores) configuration  */
	{.base = U8500_HSEM_BASE,	.size = SZ_4K},

	/* 0x80130000 -> 0x8013FFFF: B2R2 configuration registers */
	{.base = U8500_B2R2_BASE,	.size = SZ_4K},

	/* 0x80100000 -> 0x8010FFFF: STM */
	{.base = U8500_STM_BASE,	.size = 0x10000},

	/* High part of embedded boot ROM */
	{.base = U9540_ASIC_ID_BASE,	.size = SZ_4K},

	{.base = 0, .size = 0, },

};

static void hwreg_io_init(void)
{
	int i;

	for (i = 0; hwreg_io_current_map[i].base; ++i) {
		hwreg_io_current_map[i].addr =
			ioremap(hwreg_io_current_map[i].base,
				hwreg_io_current_map[i].size);
		if (!hwreg_io_current_map[i].addr)
			printk(KERN_WARNING
				"%s: ioremap for %d (%08x) failed\n",
				__func__, i, hwreg_io_current_map[i].base);
	}
}

static void hwreg_io_exit(void)
{
	int i;

	for (i = 0; hwreg_io_current_map[i].base; ++i)
		if (hwreg_io_current_map[i].addr)
			iounmap(hwreg_io_current_map[i].addr);
}

static void *hwreg_io_ptov(u32 phys)
{
	int i;

	for (i = 0; hwreg_io_current_map[i].base; ++i) {
		u32 base = hwreg_io_current_map[i].base;
		u32 size = hwreg_io_current_map[i].size;
		u8 *addr = hwreg_io_current_map[i].addr;

		if (phys < base || phys >= base + size)
			continue;

		if (addr)
			return addr + phys - base;

		break;
	}

	return NULL;
}


/*
 * HWREG 32bit DB8500 register read/write access debugfs part
 */

static int hwreg_address_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "0x%08X\n", debug_address);
}

static int hwreg_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwreg_address_print, inode->i_private);
}

static ssize_t hwreg_address_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	int err;
	unsigned long user_address;

	err = kstrtoul_from_user(user_buf, count, 0, &user_address);

	if (err)
		return err;

	if (hwreg_io_ptov(user_address) == NULL)
		return -EADDRNOTAVAIL;

	debug_address = user_address;
	return count;
}

static int hwreg_value_print(struct seq_file *s, void *p)
{
	void *ptr;

	ptr = hwreg_io_ptov(debug_address);
	if (ptr == NULL)
		return -EADDRNOTAVAIL;
	seq_printf(s, "0x%X\n", readl(ptr));
	return 0;
}

static int hwreg_value_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwreg_value_print, inode->i_private);
}

static ssize_t hwreg_value_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	int err;
	unsigned long user_val;
	void *ptr;

	err = kstrtoul_from_user(user_buf, count, 0, &user_val);

	if (err)
		return err;

	if ((ptr = hwreg_io_ptov(debug_address)) == NULL)
		return -EFAULT;
	writel(user_val, ptr);
	return count;
}

static const struct file_operations hwreg_address_fops = {
	.open = hwreg_address_open,
	.write = hwreg_address_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};
static const struct file_operations hwreg_value_fops = {
	.open = hwreg_value_open,
	.write = hwreg_value_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/* 'map' read entry: display current HWREG IO mapping table */
static int hwreg_map_print(struct seq_file *s, void *p)
{
	int err, i;

	for (i = 0; hwreg_io_current_map[i].base; ++i) {
		err = seq_printf(s, "%d: 0x%08X => 0x%08X\n",
			i, hwreg_io_current_map[i].base,
			hwreg_io_current_map[i].base +
			hwreg_io_current_map[i].size);
		if (err < 0)
			return -ENOMEM;
	}
	return 0;
}
static int hwreg_map_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwreg_map_print, inode->i_private);
}

static const struct file_operations hwreg_map_fops = {
	.open = hwreg_map_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/*
 * HWREG DB8500 formated routines
 */

static int hwreg_print(struct seq_file *s, void *d)
{
	struct hwreg_cfg *c = (struct hwreg_cfg *) s->private;
	void *p;
	uint  v;

	if ((c == NULL) || ((p = hwreg_io_ptov(c->addr)) == NULL))
		return -EADDRNOTAVAIL;

	v = (uint) (REG_FMT_32B(c) ? readl(p) : REG_FMT_16B(c) ? readw(p) : readb(p));
	v = (c->shift >= 0) ? v >> c->shift : v << (-c->shift);
	v = v & c->mask;

	if (REG_FMT_DEC(c))
		seq_printf(s, "%d\n", v);
	else if (REG_FMT_32B(c))
		seq_printf(s, "0x%08X\n", v);
	else if (REG_FMT_32B(c))
		seq_printf(s, "0x%04X\n", v);
	else
		seq_printf(s, "0x%02X\n", v);
	return 0;
}

static int hwreg_open(struct inode *inode, struct file *file)
{
	return single_open(file, hwreg_print, inode->i_private);
}

/*
 * return length of an ASCII numerical value, 0 is string is not a numerical
 * value. string shall start at value 1st char.
 * string can be tailed with \0 or space or newline chars only.
 * value can be decimal or hexadecimal (prefixed 0x or 0X).
 */
static int strval_len(char *b)
{
	char *s = b;
	if ((*s == '0') && ((*(s+1) == 'x') || (*(s+1) == 'X'))) {
		s += 2;
		for (; *s && (*s != ' ') && (*s != '\n'); s++) {
			if (!isxdigit(*s))
				return 0;
		}
	} else {
		if (*s == '-')
			s++;
		for (; *s && (*s != ' ') && (*s != '\n'); s++) {
			if (!isdigit(*s))
				return 0;
		}
	}
	return (int) (s-b);
}

/*
 * parse hwreg input data.
 * update global hwreg_cfg only if input data syntax is ok.
 */
static ssize_t hwreg_common_write(char *b, struct hwreg_cfg *cfg)
{
	uint write, val = 0, offset = 0;
	struct hwreg_cfg loc = {
		.addr = 0,		/* default: invalid phys addr */
		.fmt = 0,		/* default: 32bit access, hex output */
		.mask = 0xFFFFFFFF,	/* default: no mask */
		.shift = 0,		/* default: no bit shift */
	};

	/* read or write ? */
	if (!strncmp(b, "read ", 5)) {
		write = 0;
		b += 5;
	} else if (!strncmp(b, "write ", 6)) {
		write = 1;
		b += 6;
	} else {
		return -EINVAL;
	}

	/* OPTIONS -l|-w|-b -s -m -o */
	while ((*b == ' ') || (*b == '-')) {
		if (*(b-1) != ' ') {
			b++;
			continue;
		}
		if ((!strncmp(b, "-d ", 3)) || (!strncmp(b, "-dec ", 5))) {
			b += (*(b+2) == ' ') ? 3 : 5;
			loc.fmt |= (1<<0);
		} else if ((!strncmp(b, "-h ", 3)) || (!strncmp(b, "-hex ", 5))) {
			b += (*(b+2) == ' ') ? 3 : 5;
			loc.fmt &= ~(1<<0);
		} else if ((!strncmp(b, "-m ", 3)) || (!strncmp(b, "-mask ", 6))) {
			b += (*(b+2) == ' ') ? 3 : 6;
			if (strval_len(b) == 0)
				return -EINVAL;
			loc.mask = simple_strtoul(b, &b, 0);
		} else if ((!strncmp(b, "-s ", 3)) || (!strncmp(b, "-shift ", 7))) {
			b += (*(b+2) == ' ') ? 3 : 7;
			if (strval_len(b) == 0)
				return -EINVAL;
			loc.shift = simple_strtol(b, &b, 0);

		} else if ((!strncmp(b, "-o ", 3)) || (!strncmp(b, "-offset ", 8))) {
			b += (*(b+2) == ' ') ? 3 : 8;
			if (strval_len(b) == 0)
				return -EINVAL;
			offset = simple_strtol(b, &b, 0);
		} else if (!strncmp(b, "-l ", 3)) {
			b += 3;
			loc.fmt = (loc.fmt & ~(3<<1)) | (0<<1);
		} else if (!strncmp(b, "-w ", 3)) {
			b += 3;
			loc.fmt = (loc.fmt & ~(3<<1)) | (1<<1);
		} else if (!strncmp(b, "-b ", 3)) {
			b += 3;
			loc.fmt = (loc.fmt & ~(3<<1)) | (2<<1);
		} else {
			return -EINVAL;
		}
	}
	/* get arg ADDRESS */
	if (strval_len(b) == 0)
		return -EINVAL;
	loc.addr = simple_strtoul(b, &b, 0);
	loc.addr += offset;
	if (hwreg_io_ptov(loc.addr) == NULL)
		return -EINVAL;

	if (write) {
		while (*b == ' ')
			b++; /* skip spaces up to arg VALUE */
		if (strval_len(b) == 0)
			return -EINVAL;
		val = simple_strtoul(b, &b, 0);
	}

	/* args are ok, update target cfg (mainly for read) */
	*cfg = loc;

#ifdef DEBUG
	printk(KERN_INFO "HWREG request: %s %d-bit reg, %s, addr=0x%08X, "
	       "mask=0x%X, shift=%d value=0x%X\n",
	       (write) ? "write" : "read",
	       REG_FMT_32B(cfg) ? 32 : REG_FMT_16B(cfg) ? 16 : 8,
	       REG_FMT_DEC(cfg) ? "decimal" : "hexa",
	       cfg->addr, cfg->mask, cfg->shift, val);
#endif

	if (write) {
		void *p = hwreg_io_ptov(cfg->addr);
		uint d = (uint) (REG_FMT_32B(cfg)) ? readl(p) :
			(REG_FMT_16B(cfg)) ? readw(p) : readb(p);

		if (cfg->shift>=0) {
			d &= ~(cfg->mask << (cfg->shift));
			val = (val & cfg->mask) << (cfg->shift);
		} else {
			d &= ~(cfg->mask >> (-cfg->shift));
			val = (val & cfg->mask) >> (-cfg->shift);
		}
		val = val | d;

		/*  read reg, reset mask field and update value bit-field */
		if (REG_FMT_32B(cfg))
			writel(val, p);
		else if (REG_FMT_16B(cfg))
			writew(val, p);
		else
			writeb(val, p);
	}
	return 0;
}

static ssize_t hwreg_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	char buf[128];
	int buf_size, ret;

	/* Get userspace string and assure termination */
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	/* get args and process */
	ret = hwreg_common_write(buf, &hwreg_cfg);
	return (ret) ? ret : buf_size;
}

static const struct file_operations hwreg_fops = {
	.open = hwreg_open,
	.write = hwreg_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/*
 * hwreg module init/cleanup
 */
static int __init hwreg_initialize(void)
{
	static struct dentry *file;

	if (cpu_is_u9540()) {
		printk(KERN_INFO "hwreg: cpu is U9540\n");
		hwreg_io_current_map = hwreg_u9540_io_map;
	} else {
		printk(KERN_INFO "hwreg: cpu is U8500\n");
		hwreg_io_current_map = hwreg_u8500_io_map;
	}

	hwreg_io_init();

	hwreg_debugfs_dir = debugfs_create_dir("mem", NULL);
	if (!hwreg_debugfs_dir)
		goto debugfs_err;

	file = debugfs_create_file("reg-addr",
				   (S_IRUGO | S_IWUGO), hwreg_debugfs_dir,
				   NULL, &hwreg_address_fops);
	if (!file)
		goto debugfs_err;
	file = debugfs_create_file("reg-val",
				   (S_IRUGO | S_IWUGO), hwreg_debugfs_dir,
				   NULL, &hwreg_value_fops);
	if (!file)
		goto debugfs_err;
	file = debugfs_create_file("reg-map",
				   (S_IRUGO),
				   hwreg_debugfs_dir, NULL, &hwreg_map_fops);
	if (!file)
		goto debugfs_err;
	file = debugfs_create_file("hwreg",
				   (S_IRUGO),
				   hwreg_debugfs_dir, &hwreg_cfg, &hwreg_fops);
	if (!file)
		goto debugfs_err;
	return 0;

debugfs_err:
	if (hwreg_debugfs_dir)
		debugfs_remove_recursive(hwreg_debugfs_dir);
	printk(KERN_ERR "hwreg: failed to register debugfs entries.\n");
	return -1;
}

static void __exit hwreg_finalize(void)
{
	debugfs_remove_recursive(hwreg_debugfs_dir);
	hwreg_io_exit();
}

module_init(hwreg_initialize);
module_exit(hwreg_finalize);

MODULE_AUTHOR("ST-Ericsson");
MODULE_DESCRIPTION("DB8500 HW registers access through debugfs");
MODULE_LICENSE("GPL");
