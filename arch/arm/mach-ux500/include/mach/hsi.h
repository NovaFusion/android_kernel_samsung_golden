/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 */

#ifndef __MACH_HSI_H
#define __MACH_HSI_H

#include <plat/ste_dma40.h>

/* HSIT register offsets */
#define STE_HSI_TX_ID                  0x000
#define STE_HSI_TX_MODE                0x004
#define STE_HSI_TX_STATE               0x008
#define STE_HSI_TX_IOSTATE             0x00C
#define STE_HSI_TX_BUFSTATE            0x010
#define STE_HSI_TX_DIVISOR             0x014
#define STE_HSI_TX_BREAK               0x01C
#define STE_HSI_TX_CHANNELS            0x020
#define STE_HSI_TX_FLUSHBITS           0x024
#define STE_HSI_TX_PRIORITY            0x028
#define STE_HSI_TX_STATICCONFID        0x02C
#define STE_HSI_TX_DATASWAP            0x034
#define STE_HSI_TX_FRAMELENX           0x080
#define STE_HSI_TX_BUFFERX             0x0C0
#define STE_HSI_TX_BASEX               0x100
#define STE_HSI_TX_SPANX               0x140
#define STE_HSI_TX_GAUGEX              0x180
#define STE_HSI_TX_WATERMARKX          0x1C0
#define STE_HSI_TX_DMAEN               0x200
#define STE_HSI_TX_WATERMARKMIS        0x204
#define STE_HSI_TX_WATERMARKIM         0x208
#define STE_HSI_TX_WATERMARKIC         0x20C
#define STE_HSI_TX_WATERMARKID         0x210
#define STE_HSI_TX_WATERMARKIS         0x214
#define STE_HSI_TX_PERIPHID0           0xFE0
#define STE_HSI_TX_PERIPHID1           0xFE4
#define STE_HSI_TX_PERIPHID2           0xFE8
#define STE_HSI_TX_PERIPHID3           0xFEC

/* HSIR register offsets */
#define STE_HSI_RX_ID                  0x000
#define STE_HSI_RX_MODE                0x004
#define STE_HSI_RX_STATE               0x008
#define STE_HSI_RX_BUFSTATE            0x00C
#define STE_HSI_RX_THRESHOLD           0x010
#define STE_HSI_RX_DETECTOR            0x018
#define STE_HSI_RX_EXCEP               0x01C
#define STE_HSI_RX_ACK                 0x020
#define STE_HSI_RX_CHANNELS            0x024
#define STE_HSI_RX_REALTIME            0x028
#define STE_HSI_RX_OVERRUN             0x02C
#define STE_HSI_RX_OVERRUNACK          0x030
#define STE_HSI_RX_PREAMBLE            0x034
#define STE_HSI_RX_PIPEGAUGE           0x038
#define STE_HSI_RX_STATICCONFID        0x03C
#define STE_HSI_RX_BUFFERX             0x080
#define STE_HSI_RX_FRAMELENX           0x0C0
#define STE_HSI_RX_BASEX               0x100
#define STE_HSI_RX_SPANX               0x140
#define STE_HSI_RX_GAUGEX              0x180
#define STE_HSI_RX_WATERMARKX          0x1C0
#define STE_HSI_RX_FRAMEBURSTCNT       0x1E0
#define STE_HSI_RX_DMAEN               0x200
#define STE_HSI_RX_WATERMARKMIS        0x204
#define STE_HSI_RX_WATERMARKIM         0x208
#define STE_HSI_RX_WATERMARKIC         0x20C
#define STE_HSI_RX_WATERMARKID         0x210
#define STE_HSI_RX_OVERRUNMIS          0x214
#define STE_HSI_RX_OVERRUNIM           0x218
#define STE_HSI_RX_EXCEPMIS            0x21C
#define STE_HSI_RX_EXCEPIM             0x220
#define STE_HSI_RX_WATERMARKIS         0x224
#define STE_HSI_RX_PERIPHID0           0xFE0
#define STE_HSI_RX_PERIPHID1           0xFE4
#define STE_HSI_RX_PERIPHID2           0xFE8
#define STE_HSI_RX_PERIPHID3           0xFEC

/* HSI states */
#define STE_HSI_STATE_IDLE             0x00
#define STE_HSI_STATE_START            0x01
#define STE_HSI_STATE_TRANSMIT         0x02
#define STE_HSI_STATE_BREAK            0x03
#define STE_HSI_STATE_FLUSH            0x04
#define STE_HSI_STATE_HALT             0x05

/* HSI exceptions */
#define STE_HSI_EXCEP_TIMEOUT          0x01
#define STE_HSI_EXCEP_OVERRUN          0x02
#define STE_HSI_EXCEP_BREAK            0x04
#define STE_HSI_EXCEP_PARITY           0x08

/* HSI modes */
#define STE_HSI_MODE_SLEEP             0x00
#define STE_HSI_MODE_STREAM            0x01
#define STE_HSI_MODE_FRAME             0x02
#define STE_HSI_MODE_PIPELINED         0x03
#define STE_HSI_MODE_FAILSAFE          0x04

#define STE_HSI_MAX_BUFFERS            32

/* Max channels of STE HSI controller */
#define STE_HSI_MAX_CHANNELS           2

#define STE_HSI_DMA_MAX_BURST          1

struct stedma40_chan_cfg;

struct ste_hsi_port_cfg {
#ifdef CONFIG_STE_DMA40
       bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
       struct stedma40_chan_cfg *dma_tx_cfg;
       struct stedma40_chan_cfg *dma_rx_cfg;
#endif
};

struct ste_hsi_platform_data {
       int num_ports;
       int use_dma;
       struct ste_hsi_port_cfg *port_cfg;
};

#endif
