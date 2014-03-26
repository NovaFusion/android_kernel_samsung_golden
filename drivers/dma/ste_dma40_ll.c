/*
 * Copyright (C) ST-Ericsson SA 2007-2010
 * Author: Per Friden <per.friden@stericsson.com> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <plat/ste_dma40.h>

#include "ste_dma40_ll.h"
#ifdef CONFIG_STE_DMA40_DEBUG
#include "ste_dma40_debug.h"
#define MARK sted40_history_text((char *)__func__)
#else
#define MARK
#endif

/* Sets up proper LCSP1 and LCSP3 register for a logical channel */
void d40_log_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *lcsp1, u32 *lcsp3)
{
	u32 l3 = 0; /* dst */
	u32 l1 = 0; /* src */

	/* src is mem? -> increase address pos */
	if (cfg->dir ==  STEDMA40_MEM_TO_PERIPH ||
	    cfg->dir ==  STEDMA40_MEM_TO_MEM)
		l1 |= 1 << D40_MEM_LCSP1_SCFG_INCR_POS;

	/* dst is mem? -> increase address pos */
	if (cfg->dir ==  STEDMA40_PERIPH_TO_MEM ||
	    cfg->dir ==  STEDMA40_MEM_TO_MEM)
		l3 |= 1 << D40_MEM_LCSP3_DCFG_INCR_POS;

	/* src is hw? -> master port 1 */
	if (cfg->dir ==  STEDMA40_PERIPH_TO_MEM ||
	    cfg->dir ==  STEDMA40_PERIPH_TO_PERIPH)
		l1 |= 1 << D40_MEM_LCSP1_SCFG_MST_POS;

	/* dst is hw? -> master port 1 */
	if (cfg->dir ==  STEDMA40_MEM_TO_PERIPH ||
	    cfg->dir ==  STEDMA40_PERIPH_TO_PERIPH)
		l3 |= 1 << D40_MEM_LCSP3_DCFG_MST_POS;

	l3 |= 1 << D40_MEM_LCSP3_DCFG_EIM_POS;
	l3 |= cfg->dst_info.psize << D40_MEM_LCSP3_DCFG_PSIZE_POS;
	l3 |= cfg->dst_info.data_width << D40_MEM_LCSP3_DCFG_ESIZE_POS;

	l1 |= 1 << D40_MEM_LCSP1_SCFG_EIM_POS;
	l1 |= cfg->src_info.psize << D40_MEM_LCSP1_SCFG_PSIZE_POS;
	l1 |= cfg->src_info.data_width << D40_MEM_LCSP1_SCFG_ESIZE_POS;

	*lcsp1 = l1;
	*lcsp3 = l3;

}

/* Sets up SRC and DST CFG register for both logical and physical channels */
void d40_phy_cfg(struct stedma40_chan_cfg *cfg,
		 u32 *src_cfg, u32 *dst_cfg, bool is_log)
{
	u32 src = 0;
	u32 dst = 0;

	if (!is_log) {
		/* Physical channel */
		if ((cfg->dir ==  STEDMA40_PERIPH_TO_MEM) ||
		    (cfg->dir == STEDMA40_PERIPH_TO_PERIPH)) {
			/* Set master port to 1 */
			src |= 1 << D40_SREG_CFG_MST_POS;
			src |= D40_TYPE_TO_EVENT(cfg->src_dev_type);

			if (cfg->src_info.flow_ctrl == STEDMA40_NO_FLOW_CTRL)
				src |= 1 << D40_SREG_CFG_PHY_TM_POS;
			else
				src |= 3 << D40_SREG_CFG_PHY_TM_POS;
		}
		if ((cfg->dir ==  STEDMA40_MEM_TO_PERIPH) ||
		    (cfg->dir == STEDMA40_PERIPH_TO_PERIPH)) {
			/* Set master port to 1 */
			dst |= 1 << D40_SREG_CFG_MST_POS;
			dst |= D40_TYPE_TO_EVENT(cfg->dst_dev_type);

			if (cfg->dst_info.flow_ctrl == STEDMA40_NO_FLOW_CTRL)
				dst |= 1 << D40_SREG_CFG_PHY_TM_POS;
			else
				dst |= 3 << D40_SREG_CFG_PHY_TM_POS;
		}
		/* Interrupt on end of transfer for destination */
		dst |= 1 << D40_SREG_CFG_TIM_POS;

		/* Generate interrupt on error */
		src |= 1 << D40_SREG_CFG_EIM_POS;
		dst |= 1 << D40_SREG_CFG_EIM_POS;

		/* PSIZE */
		if (cfg->src_info.psize != STEDMA40_PSIZE_PHY_1) {
			src |= 1 << D40_SREG_CFG_PHY_PEN_POS;
			src |= cfg->src_info.psize << D40_SREG_CFG_PSIZE_POS;
		}
		if (cfg->dst_info.psize != STEDMA40_PSIZE_PHY_1) {
			dst |= 1 << D40_SREG_CFG_PHY_PEN_POS;
			dst |= cfg->dst_info.psize << D40_SREG_CFG_PSIZE_POS;
		}

		/* Element size */
		src |= cfg->src_info.data_width << D40_SREG_CFG_ESIZE_POS;
		dst |= cfg->dst_info.data_width << D40_SREG_CFG_ESIZE_POS;

		/* Set the priority bit to high for the physical channel */
		if (cfg->high_priority) {
			src |= 1 << D40_SREG_CFG_PRI_POS;
			dst |= 1 << D40_SREG_CFG_PRI_POS;
		}

	} else {
		/* Logical channel */
		dst |= 1 << D40_SREG_CFG_LOG_GIM_POS;
		src |= 1 << D40_SREG_CFG_LOG_GIM_POS;
	}


	if (cfg->src_info.big_endian)
		src |= 1 << D40_SREG_CFG_LBE_POS;
	if (cfg->dst_info.big_endian)
		dst |= 1 << D40_SREG_CFG_LBE_POS;

	*src_cfg = src;
	*dst_cfg = dst;
}

int d40_phy_fill_lli(struct d40_phy_lli *lli,
			    dma_addr_t data,
			    u32 data_size,
			    dma_addr_t next_lli,
			    u32 reg_cfg,
			    struct stedma40_half_channel_info *info,
			    unsigned int flags)
{
	bool addr_inc = flags & LLI_ADDR_INC;
	bool term_int = flags & LLI_TERM_INT;
	unsigned int data_width = info->data_width;
	int psize = info->psize;
	int num_elems;

	if (psize == STEDMA40_PSIZE_PHY_1)
		num_elems = 1;
	else
		num_elems = 2 << psize;

	/*
	 * Size is 16bit. data_width is 8, 16, 32 or 64 bit
	 * Block large than 64 KiB must be split.
	 */
	if (data_size > (0xffff << data_width))
		return -EINVAL;

	/* Must be aligned */
	if (!IS_ALIGNED(data, 0x1 << data_width))
		return -EINVAL;

	/* Transfer size can't be smaller than (num_elms * elem_size) */
	if (data_size < num_elems * (0x1 << data_width))
		return -EINVAL;

	/* The number of elements. IE now many chunks */
	lli->reg_elt = (data_size >> data_width) << D40_SREG_ELEM_PHY_ECNT_POS;

	/*
	 * Distance to next element sized entry.
	 * Usually the size of the element unless you want gaps.
	 */
	if (addr_inc)
		lli->reg_elt |= (0x1 << data_width) <<
			D40_SREG_ELEM_PHY_EIDX_POS;

	/* Where the data is */
	lli->reg_ptr = data;
	lli->reg_cfg = reg_cfg;

	/* If this scatter list entry is the last one, no next link */
	if (next_lli == 0)
		lli->reg_lnk = 0x1 << D40_SREG_LNK_PHY_TCP_POS;
	else
		lli->reg_lnk = next_lli;

	/* Set/clear interrupt generation on this link item.*/
	if (term_int)
		lli->reg_cfg |= 0x1 << D40_SREG_CFG_TIM_POS;
	else
		lli->reg_cfg &= ~(0x1 << D40_SREG_CFG_TIM_POS);

	/* Post link */
	lli->reg_lnk |= 0 << D40_SREG_LNK_PHY_PRE_POS;

	return 0;
}

int d40_phy_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t target,
		      struct d40_phy_lli *lli,
		      dma_addr_t lli_phys,
		      u32 reg_cfg,
		      bool cyclic,
		      bool cyclic_int,
		      struct stedma40_half_channel_info *info)
{
	int total_size = 0;
	int i;
	struct scatterlist *current_sg = sg;
	dma_addr_t next_lli_phys;
	bool interrupt;
	int err = 0;
	unsigned long flags = 0;

	if (!target)
		flags |= LLI_ADDR_INC;

	for_each_sg(sg, current_sg, sg_len, i) {
		dma_addr_t sg_addr = sg_dma_address(current_sg);
		unsigned int len = sg_dma_len(current_sg);
		dma_addr_t dst = target ?: sg_addr;

		total_size += sg_dma_len(current_sg);

		/* If this scatter list entry is the last one, no next link */
		if (sg_len - 1 == i)
			next_lli_phys = cyclic ? lli_phys : 0;
		else
			next_lli_phys = ALIGN(lli_phys + (i + 1) *
					      sizeof(struct d40_phy_lli),
					      D40_LLI_ALIGN);

		interrupt = cyclic ? cyclic_int : !next_lli_phys;

		if (i == sg_len - 1)
			flags |= LLI_TERM_INT;

		err = d40_phy_fill_lli(&lli[i], dst, len, next_lli_phys,
				       reg_cfg, info, flags);

		if (err)
			goto err;
	}

	return total_size;
err:
	return err;
}


/* DMA logical lli operations */

static void d40_log_lli_link(struct d40_log_lli *lli_dst,
			     struct d40_log_lli *lli_src,
			     int next,
			     bool interrupt)
{
	u32 slos = 0;
	u32 dlos = 0;

	if (next != -EINVAL) {
		slos = next * 2;
		dlos = next * 2 + 1;
	}

	if (interrupt) {
		lli_dst->lcsp13 |= D40_MEM_LCSP1_SCFG_TIM_MASK;
		lli_dst->lcsp13 |= D40_MEM_LCSP3_DTCP_MASK;
	}

	lli_src->lcsp13 = (lli_src->lcsp13 & ~D40_MEM_LCSP1_SLOS_MASK) |
		(slos << D40_MEM_LCSP1_SLOS_POS);

	lli_dst->lcsp13 = (lli_dst->lcsp13 & ~D40_MEM_LCSP1_SLOS_MASK) |
		(dlos << D40_MEM_LCSP1_SLOS_POS);
}

void d40_log_lli_lcpa_write(struct d40_log_lli_full *lcpa,
			   struct d40_log_lli *lli_dst,
			   struct d40_log_lli *lli_src,
			   int next,
			   bool interrupt)
{
	d40_log_lli_link(lli_dst, lli_src, next, interrupt);

	writel_relaxed(lli_src->lcsp02, &lcpa[0].lcsp0);
	writel_relaxed(lli_src->lcsp13, &lcpa[0].lcsp1);
	writel_relaxed(lli_dst->lcsp02, &lcpa[0].lcsp2);
	writel_relaxed(lli_dst->lcsp13, &lcpa[0].lcsp3);
}

void d40_log_lli_lcla_write(struct d40_log_lli *lcla,
			   struct d40_log_lli *lli_dst,
			   struct d40_log_lli *lli_src,
			   int next,
			   bool interrupt)
{
	d40_log_lli_link(lli_dst, lli_src, next, interrupt);

	writel_relaxed(lli_src->lcsp02, &lcla[0].lcsp02);
	writel_relaxed(lli_src->lcsp13, &lcla[0].lcsp13);
	writel_relaxed(lli_dst->lcsp02, &lcla[1].lcsp02);
	writel_relaxed(lli_dst->lcsp13, &lcla[1].lcsp13);
}

void d40_log_fill_lli(struct d40_log_lli *lli,
			     dma_addr_t data, u32 data_size,
			     u32 reg_cfg,
			     u32 data_width,
			     unsigned int flags)
{
	bool addr_inc = flags & LLI_ADDR_INC;

	lli->lcsp13 = reg_cfg;

	/* The number of elements to transfer */
	lli->lcsp02 = ((data_size >> data_width) <<
		       D40_MEM_LCSP0_ECNT_POS) & D40_MEM_LCSP0_ECNT_MASK;
	/* 16 LSBs address of the current element */
	lli->lcsp02 |= data & D40_MEM_LCSP0_SPTR_MASK;
	/* 16 MSBs address of the current element */
	lli->lcsp13 |= data & D40_MEM_LCSP1_SPTR_MASK;

	if (addr_inc)
		lli->lcsp13 |= D40_MEM_LCSP1_SCFG_INCR_MASK;

}

int d40_log_sg_to_lli(struct scatterlist *sg,
		      int sg_len,
		      dma_addr_t dev_addr,
		      struct d40_log_lli *lli_sg,
		      u32 lcsp13, /* src or dst*/
		      u32 data_width)
{
	int total_size = 0;
	struct scatterlist *current_sg = sg;
	int i;
	unsigned long flags = 0;

	if (!dev_addr)
		flags |= LLI_ADDR_INC;

	for_each_sg(sg, current_sg, sg_len, i) {
		dma_addr_t sg_addr = sg_dma_address(current_sg);
		unsigned int len = sg_dma_len(current_sg);
		dma_addr_t addr = dev_addr ?: sg_addr;

		total_size += sg_dma_len(current_sg);

		d40_log_fill_lli(&lli_sg[i], addr, len,
				 lcsp13, data_width,
				 flags);
	}
	return total_size;
}
