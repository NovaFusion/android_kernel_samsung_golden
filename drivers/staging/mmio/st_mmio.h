
#ifndef __ST_MMIO_H__
#define __ST_MMIO_H__

extern struct class *sec_class;
extern struct class *camera_class;

#if defined(CONFIG_MACH_CODINA)
#define CODINA_TMO_R0_0	1001
#define CODINA_TMO_R0_0_A 1002
#define CODINA_TMO_R0_1 1003
#define CODINA_TMO_R0_2 1004
#define CODINA_TMO_R0_4 1005
#define CODINA_TMO_R0_5 1006
#define CODINA_TMO_R0_6 1007
#endif

#if defined(CONFIG_MACH_SEC_GOLDEN)
#define	GOLDEN_BRINGUP	102
#define	GOLDEN_R0_2	103
#define	GOLDEN_R0_3	104
#define	GOLDEN_R0_4	105
#endif

#if defined(CONFIG_MACH_SEC_KYLE)
#define KYLE_ATT_R0_0	1001
#define KYLE_ATT_R0_1	1002

#endif

/* NCP6914 Camera Sub-PMIC */
int  NCP6914_subPMIC_module_init(void);
void NCP6914_subPMIC_module_exit(void);
int  NCP6914_subPMIC_PowerOn(int opt);
int  NCP6914_subPMIC_PowerOff(int opt);
int  NCP6914_subPMIC_PinOnOff(int pin, int on_off);

/* SM5103 Camera Sub-PMIC */
int  SM5103_subPMIC_module_init(void);
void SM5103_subPMIC_module_exit(void);
int  SM5103_subPMIC_PowerOn(int opt);
int  SM5103_subPMIC_PowerOff(int opt);
int  SM5103_subPMIC_PinOnOff(int pin, int on_off);

void check_VT_CAM_ID(int pin);

#endif
