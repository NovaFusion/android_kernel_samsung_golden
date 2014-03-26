/*
 * include/linux/spi/stm_msp.h
 *
 * Copyright (C) 2010 STMicroelectronics Pvt. Ltd.
 *
 * Author: Sachin Verma <sachin.verma@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _STM_MSP_H
#define _STM_MSP_H

#include <linux/device.h>

/* CHIP select/deselect commands */
enum spi_chip_select {
	SPI_CHIP_SELECT,
	SPI_CHIP_DESELECT
};

/* Common configuration for different SPI controllers */
enum spi_loopback {
	SPI_LOOPBACK_DISABLED,
	SPI_LOOPBACK_ENABLED
};

enum spi_hierarchy {
	SPI_MASTER,
	SPI_SLAVE
};

/* Endianess of FIFO Data */
enum spi_fifo_endian {
	SPI_FIFO_MSB,
	SPI_FIFO_LSB
};

/* SPI mode of operation (Communication modes) */
enum spi_mode {
	SPI_INTERRUPT_TRANSFER,
	SPI_POLLING_TRANSFER,
};

enum msp_data_size {
	MSP_DATA_BITS_DEFAULT = -1,
	MSP_DATA_BITS_8 = 0x00,
	MSP_DATA_BITS_10,
	MSP_DATA_BITS_12,
	MSP_DATA_BITS_14,
	MSP_DATA_BITS_16,
	MSP_DATA_BITS_20,
	MSP_DATA_BITS_24,
	MSP_DATA_BITS_32,
};

enum msp_clk_src {
	MSP_INTERNAL_CLK = 0x0,
	MSP_EXTERNAL_CLK,
};

struct msp_clock_params {
	enum msp_clk_src clk_src;
	/* value from 0 to 1023 */
	u16 sckdiv;
	/* Used only when MSPSCK clocks the sample rate
	 * generator (SCKSEL = 1Xb):
	 * 0b: The rising edge of MSPSCK clocks the sample rate generator
	 * 1b: The falling edge of MSPSCK clocks the sample rate generator */
	int sckpol;
};

/* Motorola SPI protocol specific definitions */
enum spi_clk_phase {
	SPI_CLK_ZERO_CYCLE_DELAY = 0x0,	/* Receive data on rising edge. */
	SPI_CLK_HALF_CYCLE_DELAY	/* Receive data on falling edge. */
};

/* SPI Clock Polarity */
enum spi_clk_pol {
	SPI_CLK_POL_IDLE_LOW,		/* Low inactive level */
	SPI_CLK_POL_IDLE_HIGH		/* High inactive level */
};

struct motorola_spi_proto_params {
	enum spi_clk_phase clk_phase;
	enum spi_clk_pol clk_pol;
};

struct stm_msp_config_chip {
	struct device *dev;
	enum spi_loopback lbm;
	enum spi_hierarchy hierarchy;
	enum spi_fifo_endian endian_rx;
	enum spi_fifo_endian endian_tx;
	enum spi_mode com_mode;
	enum msp_data_size data_size;
	struct msp_clock_params clk_freq;
	int spi_burst_mode_enable;
	struct motorola_spi_proto_params proto_params;
	u32 freq;
	void (*cs_control)(u32 control);
};

/**
 * struct stm_msp_controller - device.platform_data for SPI controller devices.
 *
 * @num_chipselect: chipselects are used to distinguish individual
 *      SPI slaves, and are numbered from zero to num_chipselects - 1.
 *      each slave has a chipselect signal, but it's common that not
 *      every chipselect is connected to a slave.
 */
struct stm_msp_controller {
	u8 num_chipselect;
	u32 id;
	u32 base_addr;
	char *device_name;
};
#endif /* _STM_MSP_H */
