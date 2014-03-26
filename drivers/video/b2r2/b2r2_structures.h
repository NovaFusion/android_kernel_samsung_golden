/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * ST-Ericsson B2R2 register struct
 *
 * Author: Robert Fekete <robert.fekete@stericsson.com>
 * Author: Paul Wannback
 * for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2.
 */


#ifndef __B2R2_STRUCTURES_H
#define __B2R2_STRUCTURES_H

/* C struct view */
struct b2r2_memory_map {
	unsigned char fill0[2304];
	unsigned int BLT_SSBA17; /* @2304 */
	unsigned int BLT_SSBA18; /* @2308 */
	unsigned int BLT_SSBA19; /* @2312 */
	unsigned int BLT_SSBA20; /* @2316 */
	unsigned int BLT_SSBA21; /* @2320 */
	unsigned int BLT_SSBA22; /* @2324 */
	unsigned int BLT_SSBA23; /* @2328 */
	unsigned int BLT_SSBA24; /* @2332 */
	unsigned char fill1[32];
	unsigned int BLT_STBA5; /* @2368 */
	unsigned int BLT_STBA6; /* @2372 */
	unsigned int BLT_STBA7; /* @2376 */
	unsigned int BLT_STBA8; /* @2380 */
	unsigned char fill2[176];
	unsigned int BLT_CTL; /* @2560 */
	unsigned int BLT_ITS; /* @2564 */
	unsigned int BLT_STA1; /* @2568 */
	unsigned char fill3[4];
	unsigned int BLT_SSBA1; /* @2576 */
	unsigned int BLT_SSBA2; /* @2580 */
	unsigned int BLT_SSBA3; /* @2584 */
	unsigned int BLT_SSBA4; /* @2588 */
	unsigned int BLT_SSBA5; /* @2592 */
	unsigned int BLT_SSBA6; /* @2596 */
	unsigned int BLT_SSBA7; /* @2600 */
	unsigned int BLT_SSBA8; /* @2604 */
	unsigned int BLT_STBA1; /* @2608 */
	unsigned int BLT_STBA2; /* @2612 */
	unsigned int BLT_STBA3; /* @2616 */
	unsigned int BLT_STBA4; /* @2620 */
	unsigned int BLT_CQ1_TRIG_IP; /* @2624 */
	unsigned int BLT_CQ1_TRIG_CTL; /* @2628 */
	unsigned int BLT_CQ1_PACE_CTL; /* @2632 */
	unsigned int BLT_CQ1_IP; /* @2636 */
	unsigned int BLT_CQ2_TRIG_IP; /* @2640 */
	unsigned int BLT_CQ2_TRIG_CTL; /* @2644 */
	unsigned int BLT_CQ2_PACE_CTL; /* @2648 */
	unsigned int BLT_CQ2_IP; /* @2652 */
	unsigned int BLT_AQ1_CTL; /* @2656 */
	unsigned int BLT_AQ1_IP; /* @2660 */
	unsigned int BLT_AQ1_LNA; /* @2664 */
	unsigned int BLT_AQ1_STA; /* @2668 */
	unsigned int BLT_AQ2_CTL; /* @2672 */
	unsigned int BLT_AQ2_IP; /* @2676 */
	unsigned int BLT_AQ2_LNA; /* @2680 */
	unsigned int BLT_AQ2_STA; /* @2684 */
	unsigned int BLT_AQ3_CTL; /* @2688 */
	unsigned int BLT_AQ3_IP; /* @2692 */
	unsigned int BLT_AQ3_LNA; /* @2696 */
	unsigned int BLT_AQ3_STA; /* @2700 */
	unsigned int BLT_AQ4_CTL; /* @2704 */
	unsigned int BLT_AQ4_IP; /* @2708 */
	unsigned int BLT_AQ4_LNA; /* @2712 */
	unsigned int BLT_AQ4_STA; /* @2716 */
	unsigned int BLT_SSBA9; /* @2720 */
	unsigned int BLT_SSBA10; /* @2724 */
	unsigned int BLT_SSBA11; /* @2728 */
	unsigned int BLT_SSBA12; /* @2732 */
	unsigned int BLT_SSBA13; /* @2736 */
	unsigned int BLT_SSBA14; /* @2740 */
	unsigned int BLT_SSBA15; /* @2744 */
	unsigned int BLT_SSBA16; /* @2748 */
	unsigned int BLT_SGA1; /* @2752 */
	unsigned int BLT_SGA2; /* @2756 */
	unsigned char fill4[8];
	unsigned int BLT_ITM0; /* @2768 */
	unsigned int BLT_ITM1; /* @2772 */
	unsigned int BLT_ITM2; /* @2776 */
	unsigned int BLT_ITM3; /* @2780 */
	unsigned char fill5[16];
	unsigned int BLT_DFV2; /* @2800 */
	unsigned int BLT_DFV1; /* @2804 */
	unsigned int BLT_PRI; /* @2808 */
	unsigned char fill6[8];
	unsigned int PLUGS1_OP2; /* @2820 */
	unsigned int PLUGS1_CHZ; /* @2824 */
	unsigned int PLUGS1_MSZ; /* @2828 */
	unsigned int PLUGS1_PGZ; /* @2832 */
	unsigned char fill7[16];
	unsigned int PLUGS2_OP2; /* @2852 */
	unsigned int PLUGS2_CHZ; /* @2856 */
	unsigned int PLUGS2_MSZ; /* @2860 */
	unsigned int PLUGS2_PGZ; /* @2864 */
	unsigned char fill8[16];
	unsigned int PLUGS3_OP2; /* @2884 */
	unsigned int PLUGS3_CHZ; /* @2888 */
	unsigned int PLUGS3_MSZ; /* @2892 */
	unsigned int PLUGS3_PGZ; /* @2896 */
	unsigned char fill9[48];
	unsigned int PLUGT_OP2; /* @2948 */
	unsigned int PLUGT_CHZ; /* @2952 */
	unsigned int PLUGT_MSZ; /* @2956 */
	unsigned int PLUGT_PGZ; /* @2960 */
	unsigned char fill10[108];
	unsigned int BLT_NIP; /* @3072 */
	unsigned int BLT_CIC; /* @3076 */
	unsigned int BLT_INS; /* @3080 */
	unsigned int BLT_ACK; /* @3084 */
	unsigned int BLT_TBA; /* @3088 */
	unsigned int BLT_TTY; /* @3092 */
	unsigned int BLT_TXY; /* @3096 */
	unsigned int BLT_TSZ; /* @3100 */
	unsigned int BLT_S1CF; /* @3104 */
	unsigned int BLT_S2CF; /* @3108 */
	unsigned int BLT_S1BA; /* @3112 */
	unsigned int BLT_S1TY; /* @3116 */
	unsigned int BLT_S1XY; /* @3120 */
	unsigned char fill11[4];
	unsigned int BLT_S2BA; /* @3128 */
	unsigned int BLT_S2TY; /* @3132 */
	unsigned int BLT_S2XY; /* @3136 */
	unsigned int BLT_S2SZ; /* @3140 */
	unsigned int BLT_S3BA; /* @3144 */
	unsigned int BLT_S3TY; /* @3148 */
	unsigned int BLT_S3XY; /* @3152 */
	unsigned int BLT_S3SZ; /* @3156 */
	unsigned int BLT_CWO; /* @3160 */
	unsigned int BLT_CWS; /* @3164 */
	unsigned int BLT_CCO; /* @3168 */
	unsigned int BLT_CML; /* @3172 */
	unsigned int BLT_FCTL; /* @3176 */
	unsigned int BLT_PMK; /* @3180 */
	unsigned int BLT_RSF; /* @3184 */
	unsigned int BLT_RZI; /* @3188 */
	unsigned int BLT_HFP; /* @3192 */
	unsigned int BLT_VFP; /* @3196 */
	unsigned int BLT_Y_RSF; /* @3200 */
	unsigned int BLT_Y_RZI; /* @3204 */
	unsigned int BLT_Y_HFP; /* @3208 */
	unsigned int BLT_Y_VFP; /* @3212 */
	unsigned char fill12[16];
	unsigned int BLT_KEY1; /* @3232 */
	unsigned int BLT_KEY2; /* @3236 */
	unsigned char fill13[8];
	unsigned int BLT_SAR; /* @3248 */
	unsigned int BLT_USR; /* @3252 */
	unsigned char fill14[8];
	unsigned int BLT_IVMX0; /* @3264 */
	unsigned int BLT_IVMX1; /* @3268 */
	unsigned int BLT_IVMX2; /* @3272 */
	unsigned int BLT_IVMX3; /* @3276 */
	unsigned int BLT_OVMX0; /* @3280 */
	unsigned int BLT_OVMX1; /* @3284 */
	unsigned int BLT_OVMX2; /* @3288 */
	unsigned int BLT_OVMX3; /* @3292 */
	unsigned char fill15[8];
	unsigned int BLT_VC1R; /* @3304 */
	unsigned char fill16[20];
	unsigned int BLT_Y_HFC0; /* @3328 */
	unsigned int BLT_Y_HFC1; /* @3332 */
	unsigned int BLT_Y_HFC2; /* @3336 */
	unsigned int BLT_Y_HFC3; /* @3340 */
	unsigned int BLT_Y_HFC4; /* @3344 */
	unsigned int BLT_Y_HFC5; /* @3348 */
	unsigned int BLT_Y_HFC6; /* @3352 */
	unsigned int BLT_Y_HFC7; /* @3356 */
	unsigned int BLT_Y_HFC8; /* @3360 */
	unsigned int BLT_Y_HFC9; /* @3364 */
	unsigned int BLT_Y_HFC10; /* @3368 */
	unsigned int BLT_Y_HFC11; /* @3372 */
	unsigned int BLT_Y_HFC12; /* @3376 */
	unsigned int BLT_Y_HFC13; /* @3380 */
	unsigned int BLT_Y_HFC14; /* @3384 */
	unsigned int BLT_Y_HFC15; /* @3388 */
	unsigned char fill17[80];
	unsigned int BLT_Y_VFC0; /* @3472 */
	unsigned int BLT_Y_VFC1; /* @3476 */
	unsigned int BLT_Y_VFC2; /* @3480 */
	unsigned int BLT_Y_VFC3; /* @3484 */
	unsigned int BLT_Y_VFC4; /* @3488 */
	unsigned int BLT_Y_VFC5; /* @3492 */
	unsigned int BLT_Y_VFC6; /* @3496 */
	unsigned int BLT_Y_VFC7; /* @3500 */
	unsigned int BLT_Y_VFC8; /* @3504 */
	unsigned int BLT_Y_VFC9; /* @3508 */
	unsigned char fill18[72];
	unsigned int BLT_HFC0; /* @3584 */
	unsigned int BLT_HFC1; /* @3588 */
	unsigned int BLT_HFC2; /* @3592 */
	unsigned int BLT_HFC3; /* @3596 */
	unsigned int BLT_HFC4; /* @3600 */
	unsigned int BLT_HFC5; /* @3604 */
	unsigned int BLT_HFC6; /* @3608 */
	unsigned int BLT_HFC7; /* @3612 */
	unsigned int BLT_HFC8; /* @3616 */
	unsigned int BLT_HFC9; /* @3620 */
	unsigned int BLT_HFC10; /* @3624 */
	unsigned int BLT_HFC11; /* @3628 */
	unsigned int BLT_HFC12; /* @3632 */
	unsigned int BLT_HFC13; /* @3636 */
	unsigned int BLT_HFC14; /* @3640 */
	unsigned int BLT_HFC15; /* @3644 */
	unsigned char fill19[80];
	unsigned int BLT_VFC0; /* @3728 */
	unsigned int BLT_VFC1; /* @3732 */
	unsigned int BLT_VFC2; /* @3736 */
	unsigned int BLT_VFC3; /* @3740 */
	unsigned int BLT_VFC4; /* @3744 */
	unsigned int BLT_VFC5; /* @3748 */
	unsigned int BLT_VFC6; /* @3752 */
	unsigned int BLT_VFC7; /* @3756 */
	unsigned int BLT_VFC8; /* @3760 */
	unsigned int BLT_VFC9; /* @3764 */
};

#endif /* !defined(__B2R2_STRUCTURES_H) */

