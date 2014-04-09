mcde-objs		+= mcde_mod.o
mcde-objs		+= mcde_hw.o
mcde-objs		+= mcde_dss.o
mcde-objs		+= mcde_display.o
mcde-objs		+= mcde_bus.o
ifdef CONFIG_FB_MCDE
mcde-objs		+= mcde_fb.o
endif
mcde-objs		+= mcde_debugfs.o
mcde-objs		+= dsilink.o
mcde-objs		+= dsilink_v2.o
mcde-objs		+= dsilink_v3.o
mcde-objs		+= dsilink_debugfs.o
obj-$(CONFIG_MCDE)	+= mcde.o

obj-$(CONFIG_MCDE_DISPLAY_WS2401_DPI)   	+= display-ws2401_dpi.o
obj-$(CONFIG_MCDE_DISPLAY_S6D27A1_DPI)   	+= display-s6d27a1_dpi.o
obj-$(CONFIG_MCDE_DISPLAY_KYLE_DSI)			+= display-sec-HVA40WV1_dsi.o display-sec-nt35512_dsi.o
obj-$(CONFIG_MCDE_DISPLAY_SAMSUNG_S6D16D0)	+= display-samsung_s6d16d0.o
obj-$(CONFIG_MCDE_DISPLAY_SONY_ACX424AKP_DSI)   += display-sony_acx424akp_dsi.o
obj-$(CONFIG_MCDE_DISPLAY_TOSHIBA_DSI2LVDS)	+= display-toshiba_dsi2lvds.o
obj-$(CONFIG_MCDE_DISPLAY_HIMAX_HX8392)		+= display-himax_hx8392.o
obj-$(CONFIG_MCDE_DISPLAY_VUIB500_DPI)		+= display-vuib500-dpi.o
obj-$(CONFIG_MCDE_DISPLAY_I9060_DPI)		+= display-i9060-dpi.o
obj-$(CONFIG_MCDE_DISPLAY_AB8500_DENC)		+= display-ab8500.o
obj-$(CONFIG_MCDE_DISPLAY_AV8100)		+= display-av8100.o
obj-$(CONFIG_DISPLAY_FICTIVE)			+= display-fictive.o
obj-$(CONFIG_MCDE_DISPLAY_LMS397KF_DPI)		+= display-gavini-lms397kf.o
obj-$(CONFIG_MCDE_DISPLAY_S6E63MN0_DPI)		+= display-s6e63m0.o smart_mtp_s6e63m0.o
obj-$(CONFIG_MCDE_DISPLAY_S6E63M0_DSI)		+= display-s6e63m0_dsi.o smart_mtp_s6e63m0_dsi.o
obj-$(CONFIG_MCDE_DISPLAY_NT35512_DSI)		+= display-nt35510_dsi.o display-nt35512_dsi.o
obj-$(CONFIG_MCDE_DISPLAY_LD9040_DPI)		+= display-ld9040.o
obj-$(CONFIG_MCDE_DISPLAY_M1316A0_DSI)      	+= display-m1316a0-dsi.o

ifdef CONFIG_FB_MCDE_DEBUG
EXTRA_CFLAGS += -DDEBUG
endif
ifdef CONFIG_FB_MCDE_VDEBUG
EXTRA_CFLAGS += -DVERBOSE_DEBUG
endif
