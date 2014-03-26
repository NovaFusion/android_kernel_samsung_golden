/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_UX500_ID
#define __MACH_UX500_ID

/**
 * struct dbx500_asic_id - fields of the ASIC ID
 * @process: the manufacturing process, 0x40 is 40 nm 0x00 is "standard"
 * @partnumber: hithereto 0x8500 for DB8500
 * @revision: version code in the series
 */
struct dbx500_asic_id {
	u16	partnumber;
	u8	revision;
	u8	process;
};

extern struct dbx500_asic_id dbx500_id;

static inline unsigned int __attribute_const__ dbx500_partnumber(void)
{
	return dbx500_id.partnumber;
}

static inline unsigned int __attribute_const__ dbx500_revision(void)
{
	return dbx500_id.revision;
}

/*
 * SOCs
 */

static inline bool __attribute_const__ cpu_is_u8500(void)
{
#ifdef CONFIG_UX500_SOC_DB8500
	/* partnumber 8520 also comes under 8500 */
	return ((dbx500_partnumber() >> 8) & 0xff) == 0x85;
#else
	return false;
#endif
}

static inline bool __attribute_const__ cpu_is_u8520(void)
{
#ifdef CONFIG_UX500_SOC_DB8500
	return dbx500_partnumber() == 0x8520;
#else
	return false;
#endif
}

static inline bool __attribute_const__ cpu_is_u5500(void)
{
#ifdef CONFIG_UX500_SOC_DB5500
	return dbx500_partnumber() == 0x5500;
#else
	return false;
#endif
}

#ifdef CONFIG_UX500_SOC_DB8500
bool cpu_is_u9500(void);
#else
static inline bool cpu_is_u9500(void)
{
	return false;
}
#endif
static inline bool __attribute_const__ cpu_is_u9540(void)
{
#ifdef CONFIG_UX500_SOC_DB8500
	return dbx500_partnumber() == 0x9540;
#else
	return false;
#endif
}

#define ux500_unknown_soc()	BUG()

#endif
