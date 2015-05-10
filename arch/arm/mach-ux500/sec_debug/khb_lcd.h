/*****************************************************************************
** COPYRIGHT(C) : Samsung Electronics Co.Ltd, 2002-2006 ALL RIGHTS RESERVED
** AUTHOR               : KyoungHOON Kim (khoonk)
******************************************************************************/

#ifndef _KHB_LCD_H_
#define _KHB_LCD_H_
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
//#include <linux/init.h>
//#include <linux/interrupt.h>
//#include <linux/console.h>
//#include <asm/irq.h>
//#include <asm/arch/clock.h>
//#include <asm/uaccess.h>


#ifdef CONFIG_PM
#define PM_DEBUG 1
//#include <linux/notifier.h>
//#include <linux/pm.h>
#endif

#ifdef CONFIG_DPM
//#include <linux/dpm.h>
#endif

#if 0
//#include <asm/arch/display.h>
//#include "../../drivers/video/omap/omap_fb.h"


/*****************************************************************************
** structure definitions
** This structure is actually defined in drivers/video/omap/omap_fb.c
** It is copied here as we access it to get the framebuffer base address
******************************************************************************/
struct omap24xxfb_info {
	/* The fb_info structure must be first! */
	struct fb_info info;

	dma_addr_t mmio_base_phys;
	dma_addr_t fb_base_phys;
	unsigned long fb_size;
	unsigned long mmio_base;
	unsigned long fb_base;

	wait_queue_head_t vsync_wait;
	unsigned long vsync_cnt;

	u32 pseudo_palette[17];
	u32 *palette;
	dma_addr_t palette_phys;

	/* Period of the graphics clock in picoseconds.
	 * This is is the input to the pixel clock divider.
	 */
	unsigned long gfx_clk_period;
	unsigned int hsync;	/* horizontal sync frequency in Hz */
	unsigned int vsync;	/* vertical sync frequency in Hz */
	unsigned long timeout;	/* register update timeout period in ticks */

	int alloc_fb_mem;
	int asleep;
	int blanked;

	int rotation_support;
	int rotation_deg;
	dma_addr_t sms_rot_phy[4];
	unsigned long sms_rot_virt[4];
	unsigned long vrfb_size;
};

#endif
 
/*****************************************************************************
** Macro definitions 
******************************************************************************/

#if defined (CONFIG_MACH_NOWPLUS)
#define CONFIG_FB_LCD_WVGA
#elif defined (CONFG_MACH_NOWPLUS_MASS)
#define CONFIG_FB_LCD_WQVGA
#else
#define CONFIG_FB_LCD_WVGA
#endif

//#define CONFIG_LCD_COLOR_DEPTH_16
#undef CONFIG_LCD_COLOR_DEPTH_16


#if defined(CONFIG_FB_LCD_WVGA)
#define PANEL_W                                         (480)
#define PANEL_H                                         (800)
#elif defined(CONFIG_FB_LCD_WQVGA)
#define PANEL_W                     (240)
#define PANEL_H                     (400)
#endif


#define COLOR_RED                               0xff0000
#define COLOR_GREEN                             0x00ff00
#define COLOR_BLUE                              0x0000ff
#define COLOR_WHITE                             0xffffff
#define COLOR_BLACK                             0x000000


/*****************************************************************************
** externs
******************************************************************************/
#if defined(CONFIG_LCD_COLOR_DEPTH_16)
	extern unsigned short*  fbpMem;
#else
	extern unsigned int*    fbpMem;
#endif

extern int fd_line_length;

//extern struct omap24xxfb_info *saved_oinfo;  uma commented

/*****************************************************************************
** forward declarations
******************************************************************************/
void lcd_PutPixel(int x, int y, unsigned int color);
void lcd_ClearScr(unsigned int color);
void lcd_Line(int x1, int y1, int x2, int y2, unsigned int color);
void lcd_Rectangle(int x1, int y1, int x2, int y2, unsigned int color);
void lcd_FilledRectangle(int x1, int y1, int x2, int y2, unsigned int color);
void lcd_draw_font(int x,int y,int fgc,int bgc,unsigned char len, unsigned char * data, unsigned char fontSize);

#endif /* #ifndef _KHB_LCD_H_  */
