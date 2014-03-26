/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 global definitions
 *
 * Author: Robert Fekete <robert.fekete@stericsson.com>
 * Author: Paul Wannback
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __B2R2_GLOBAL_H
#define __B2R2_GLOBAL_H

/** Sources involved */

struct b2r2_system {
	unsigned int B2R2_NIP;
	unsigned int B2R2_CIC;
	unsigned int B2R2_INS;
	unsigned int B2R2_ACK;
};

struct b2r2_target {
	unsigned int B2R2_TBA;
	unsigned int B2R2_TTY;
	unsigned int B2R2_TXY;
	unsigned int B2R2_TSZ;
};

struct b2r2_color_fill {
	unsigned int  B2R2_S1CF;
	unsigned int  B2R2_S2CF;
};

struct b2r2_src_config {
	unsigned int B2R2_SBA;
	unsigned int B2R2_STY;
	unsigned int B2R2_SXY;
	unsigned int B2R2_SSZ;
};

struct b2r2_clip {
	 unsigned int B2R2_CWO;
	 unsigned int B2R2_CWS;
};

struct b2r2_color_key {
	 unsigned int B2R2_KEY1;
	 unsigned int B2R2_KEY2;
};

struct b2r2_clut {
	unsigned int B2R2_CCO;
	unsigned int B2R2_CML;
};

struct b2r2_rsz_pl_mask {
	unsigned int B2R2_FCTL;
	unsigned int B2R2_PMK;
};

struct b2r2_Cr_luma_rsz {
	unsigned int B2R2_RSF;
	unsigned int B2R2_RZI;
	unsigned int B2R2_HFP;
	unsigned int B2R2_VFP;
};

struct b2r2_flikr_filter {
	unsigned int B2R2_FF0;
	unsigned int B2R2_FF1;
	unsigned int B2R2_FF2;
	unsigned int B2R2_FF3;
};

struct b2r2_xyl {
	unsigned int B2R2_XYL;
	unsigned int B2R2_XYP;
};

struct b2r2_sau {
	unsigned int B2R2_SAR;
	unsigned int B2R2_USR;
};

struct b2r2_vm {
	unsigned int B2R2_VMX0;
	unsigned int B2R2_VMX1;
	unsigned int B2R2_VMX2;
	unsigned int B2R2_VMX3;
};

struct b2r2_link_list {

	struct b2r2_system       GROUP0;
	struct b2r2_target 	 GROUP1;
	struct b2r2_color_fill   GROUP2;
	struct b2r2_src_config	 GROUP3;
	struct b2r2_src_config   GROUP4;
	struct b2r2_src_config   GROUP5;
	struct b2r2_clip         GROUP6;
	struct b2r2_clut         GROUP7;
	struct b2r2_rsz_pl_mask	 GROUP8;
	struct b2r2_Cr_luma_rsz  GROUP9;
	struct b2r2_Cr_luma_rsz  GROUP10;
	struct b2r2_flikr_filter GROUP11;
	struct b2r2_color_key    GROUP12;
	struct b2r2_xyl  	 GROUP13;
	struct b2r2_sau		 GROUP14;
	struct b2r2_vm		 GROUP15;
	struct b2r2_vm		 GROUP16;

	unsigned int B2R2_RESERVED[2];
};


#endif /* !defined(__B2R2_GLOBAL_H) */
