/*
 * =====================================================================================
 * 
 *       Filename:  hwreg.h
 * 
 *    Description:  
 * 
 *        Version:  1.1
 *        Created:  12/04/2008 02:05:57 PM CET
 *                  26/10/2009 A.CROUZET (ACR)
 *                        PR_CAP_87 'Update hwreg for Mont-Blanc virtual adresses'
 *                        HWREG_CHECK_ADDR define added.
 *       Compiler:  gcc
 * 
 *         Author:   (), 
 *        Company:  
 * 
 * =====================================================================================
 */

//#ifndef  HWREG_INC
//#define  HWREG_INC

#define HWREG_MASK_READ  0x00
#define HWREG_MASK_WRITE 0x80

#define HWREG_U8	0x01
#define HWREG_U16	0x02
#define HWREG_U32	0x03

#define HWREG_READ_U8   (HWREG_MASK_READ  | HWREG_U8) 
#define HWREG_READ_16	(HWREG_MASK_READ  | HWREG_U16)
#define HWREG_READ_32   (HWREG_MASK_READ  | HWREG_U32)

#define HWREG_WRITE_U8	(HWREG_MASK_WRITE | HWREG_U8) 
#define HWREG_WRITE_16	(HWREG_MASK_WRITE | HWREG_U16)
#define HWREG_WRITE_32  (HWREG_MASK_WRITE | HWREG_U32)


#define HWREG_READ	        _IOWR(0x03, 0xc6, st_hwreg)
#define HWREG_WRITE	        _IOW (0x03, 0xc7, st_hwreg)
#define HWREG_CLEAR_BIT	    _IOW (0x03, 0xc8, st_hwreg)
#define HWREG_SET_BIT	    _IOW (0x03, 0xc9, st_hwreg)
/* ++ PR_CAP_87 ++ */
#define HWREG_CHECK_ADDR    _IOWR(0x03, 0xcA, st_hwreg)
/* -- PR_CAP_87 -- */

#define HWREG_U8_READ	_IOWR(0x03, HWREG_READ_U8, st_hwreg)
#define HWREG_U16_READ	_IOWR(0x03, HWREG_READ_16, st_hwreg)
#define HWREG_U32_READ	_IOWR(0x03, HWREG_READ_32, st_hwreg)

#define HWREG_U8_WRITE	_IOWR(0x03, HWREG_WRITE_U8, st_hwreg)
#define HWREG_U16_WRITE	_IOWR(0x03, HWREG_WRITE_16, st_hwreg)
#define HWREG_U32_WRITE	_IOWR(0x03, HWREG_WRITE_32, st_hwreg)


#if 0
typedef struct
{
	unsigned int addr;/* address */
	unsigned long val; /* value */
}st_hwreg;
#endif
typedef struct
{
  uint32_t addr;/* address */
  union 
  {
    uint8_t	u8_val; /* value */
    uint16_t u16_val;
    uint32_t u32_val;
  } u;
}st_hwreg;

//#endif   /* ----- #ifndef HWREG_INC  ----- */

