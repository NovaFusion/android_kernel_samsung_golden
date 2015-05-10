/*****************************************************************************
 * kernel/kheart-beat/khb_main.c
 * 
 * This file contains functions which aid the kernel debugging
 ******************************************************************************/

#include "khb_main.h"
#include "khb_lcd.h"
#include <asm/memory.h> 
#include <linux/fb.h>

extern struct fb_info* get_primary_display_fb_info(void);
extern unsigned int *fbpMem1;
extern unsigned int *fbpMem2;
extern int fd_line_length;

void setup_primary_display_fb_info(void)
{
	struct fb_info *p_fb_info;

	if (fbpMem1 == NULL){
		p_fb_info = get_primary_display_fb_info();
		if (p_fb_info != NULL){
			fbpMem1 = (unsigned int *)p_fb_info->screen_base;
			fbpMem2 = (unsigned int *)p_fb_info->screen_base + (p_fb_info->screen_size / (2 * sizeof(unsigned int)));
			fd_line_length = p_fb_info->fix.line_length;

			khb_dbg_msg("fd_line_length=%d\n", fd_line_length);			
		}
	}
}

/*
 * This function toggles the display colour of the rowHeight x colHeight matrix on the
 * left corner of the screen.
 */
int kernel_alive_indicator(int x, int y, int colHeight,  int rowHeight, int mode)
{
	static int flag1 = COLOR_WHITE;
	static int flag2 = COLOR_RED;

	khb_dbg_msg("entered kernel alive indicator function\n");			 

	setup_primary_display_fb_info();

	if(fbpMem1 != NULL)
	{
		rowHeight = (rowHeight<0)?KHB_MIN_BOX_W:rowHeight;
		rowHeight = (rowHeight>PANEL_W)?PANEL_W:rowHeight;
		colHeight = (colHeight<0)?KHB_MIN_BOX_H:colHeight;
		colHeight = (colHeight>PANEL_H)?PANEL_H:colHeight;
		
			if(mode==0) {
				lcd_FilledRectangle(x,y,colHeight, rowHeight, flag1);
				flag1 = ~flag1;
			}
			else if(mode==1) {
				lcd_FilledRectangle(x,y,colHeight, rowHeight, flag2);
				flag2 = ~flag2;
			}		
	}
	else
	{
		khb_dbg_msg("fbpMem1 NULL\n");
	}	

return 0;
}

EXPORT_SYMBOL(kernel_alive_indicator);

/*
 * txtMsg	 :	text message to display
 * color	 :	Background colour
 * fontSize	 :  0 for default font size; 1 for x4 magnification 
 * retainPrevMSg : 0 to overwrite the previous message; 1 to retain previously dispalyed message
 */
int display_dbg_msg(char* txtMsg, unsigned char fontSize, unsigned char retainPrevMsg)
{ 
	int colHeight=3;
	static int rowHeight=0;

	if((txtMsg == NULL))
	{
		khb_dbg_msg("NULL pointer\n");
		return -1;
	}

	setup_primary_display_fb_info();

	if(fbpMem1 == NULL)
	{
		khb_dbg_msg("NULL pointer\n");
		return -1;
	}

	/* display debug screen */
	if(!retainPrevMsg)
	{
		rowHeight = 3;
	}
	else
	{
		rowHeight += 8*(fontSize+2);
	}

	rowHeight = (rowHeight<0)?KHB_MIN_BOX_W:rowHeight;
	rowHeight = (rowHeight>PANEL_W)?PANEL_W:rowHeight;

	khb_dbg_msg("%s\n",txtMsg); // display info on the console

	/* display debug message */
	lcd_draw_font(colHeight, rowHeight, COLOR_BLACK, COLOR_WHITE, strlen(txtMsg), (unsigned char*)txtMsg, fontSize);

return 0;
}

EXPORT_SYMBOL(display_dbg_msg);
