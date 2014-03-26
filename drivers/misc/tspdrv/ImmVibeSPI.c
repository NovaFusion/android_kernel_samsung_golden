/*
** =========================================================================
** File:
**     ImmVibeSPI.c
**
** Description:
**     Device-dependent functions called by Immersion TSP API
**     to control PWM duty cycle, amp enable/disable, save IVT file, etc...
**
** Portions Copyright (c) 2008-2010 Immersion Corporation. All Rights Reserved.
**
** This file contains Original Code and/or Modifications of Original Code
** as defined in and that are subject to the GNU Public License v2 -
** (the 'License'). You may not use this file except in compliance with the
** License. You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES,
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see
** the License for the specific language governing rights and limitations
** under the License.
** =========================================================================
*/

#ifdef IMMVIBESPIAPI
#undef IMMVIBESPIAPI
#endif
#define IMMVIBESPIAPI static

#include <linux/gpio.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <mach/isa1200.h>
#include "tspdrv.h"

#define ISA1200_I2C_ADDRESS 0x90 /*0x92 when SADD is high*/
#define SCTRL         (0)     /* 0x0F, System(LDO) Register Group 0*/
#define HCTRL0     (0x30)     /* 0x09 */ /* Haptic Motor Driver Control Register Group 0*/
#define HCTRL1     (0x31)     /* 0x4B */ /* Haptic Motor Driver Control Register Group 1*/
#define HCTRL2     (0x32)     /* 0x00*/ /* Haptic Motor Driver Control Register Group 2*/
#define HCTRL3     (0x33)     /* 0x13 */ /* Haptic Motor Driver Control Register Group 3*/
#define HCTRL4     (0x34)     /* 0x00 */ /* Haptic Motor Driver Control Register Group 4*/
#define HCTRL5     (0x35)     /* 0x6B */ /* Haptic Motor Driver Control Register Group 5*/
#define HCTRL6     (0x36)     /* 0xD6 */ /* Haptic Motor Driver Control Register Group 6*/
#define HCTRL7     (0x37)     /* 0x00 */ /* Haptic Motor Driver Control Register Group 7*/
#define HCTRL8     (0x38)     /* 0x00 */ /* Haptic Motor Driver Control Register Group 8*/
#define HCTRL9     (0x39)     /* 0x40 */ /* Haptic Motor Driver Control Register Group 9*/
#define HCTRLA     (0x3A)     /* 0x2C */ /* Haptic Motor Driver Control Register Group A*/
#define HCTRLB     (0x3B)     /* 0x6B */ /* Haptic Motor Driver Control Register Group B*/
#define HCTRLC     (0x3C)     /* 0xD6 */ /* Haptic Motor Driver Control Register Group C*/
#define HCTRLD     (0x3D)     /* 0x19 */ /* Haptic Motor Driver Control Register Group D*/

#define LDO_VOLTAGE_23V 0x08
#define LDO_VOLTAGE_24V 0x09
#define LDO_VOLTAGE_25V 0x0A
#define LDO_VOLTAGE_26V 0x0B
#define LDO_VOLTAGE_27V 0x0C
#define LDO_VOLTAGE_28V 0x0D
#define LDO_VOLTAGE_29V 0x0E
#define LDO_VOLTAGE_30V 0x0F
#define LDO_VOLTAGE_31V 0x00
#define LDO_VOLTAGE_32V 0x01
#define LDO_VOLTAGE_33V 0x02
#define LDO_VOLTAGE_34V 0x03
#define LDO_VOLTAGE_35V 0x04
#define LDO_VOLTAGE_36V 0x05
#define LDO_VOLTAGE_37V 0x06
#define LDO_VOLTAGE_38V 0x07

#define DEBUG_MSG pr_info

#if 0
/***********************************************************************************************
please complete defines below for your hardware design
************************************************************************************************/
#define SYS_API_LEN_HIGH
#define SYS_API_LEN_LOW
#define SYS_API_HEN_HIGH
#define SYS_API_HEN_LOW
#define SYS_API_POWER_ENABLE
#define SYS_API_POWER_DISABLE
#define SYS_API__I2C__Write(_addr, _dataLength, _data)
#define BASE_CLK_ENABLE
#define BASE_CLK_DISABLE
#define SLEEP

#define DEBUG_MSG

/***********************************************************************************************
please modify values below for your base clock frequency and LRA resonent frequency

how to calculate output frequency = base clock frequency / (128 - PWM_FREQ_DEFAULT) / 2 / (PLLDIV) / PWM_PERIOD_DEFAULT

example -
if LRA resonantfrequency  = 175 hz,  base clock frequency = 26 Mhz and  PWM_FREQ_DEFAULT = 0
then calculation is 175 = 26000000 / 128 / 2 / PLLDIV / PWM_PERIOD_DEFAULT
and you can find right values for PLLDIV and PWM_PERIOD_DEFAULT


************************************************************************************************/
#endif

#ifdef CONFIG_MACH_JANICE
#define PWM_PLLDIV_DEFAULT		0x02
#define PWM_FREQ_DEFAULT		0x00
#define PWM_PERIOD_DEFAULT		0x77
#define PWM_DUTY_DEFAULT		0x3B
#define LDO_VOLTAGE_DEFAULT		LDO_VOLTAGE_30V

#elif defined(CONFIG_MACH_GAVINI)
#define PWM_PLLDIV_DEFAULT		0x02
#define PWM_FREQ_DEFAULT		0x00
#define PWM_PERIOD_DEFAULT		0x8C
#define PWM_DUTY_DEFAULT		0x46
#define LDO_VOLTAGE_DEFAULT		LDO_VOLTAGE_27V
#endif

static VibeUInt32 g_nPWM_PLLDiv = PWM_PLLDIV_DEFAULT;
static VibeUInt32 g_nPWM_Freq = PWM_FREQ_DEFAULT;
static VibeUInt32 g_nPWM_Period = PWM_PERIOD_DEFAULT;
static VibeUInt32 g_nPWM_Duty = PWM_DUTY_DEFAULT;
static VibeUInt32 g_nLDO_Voltage = LDO_VOLTAGE_DEFAULT;

/*
** This SPI supports only one actuator.
*/
#define NUM_ACTUATORS 1
#define RETRY_CNT 4

static bool g_bAmpEnabled;

/* isa1200 specific */
extern struct isa1200_data *isa_data;

static int vib_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	int cnt = 0;
	int ret = VIBE_S_SUCCESS;

	do {
		ret = immvibe_i2c_write(client, reg, val);
		cnt++;
	} while (VIBE_S_SUCCESS != ret && cnt < RETRY_CNT);
	if (VIBE_S_SUCCESS != ret)
		DEBUG_MSG("[ImmVibeSPI]	I2C_Write Error, Slave Address = [%02x], ret = [%d]\n", reg, ret);

	return ret;
}

/*
** Called to disable amp (disable output force)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 nActuatorIndex)
{
	if (g_bAmpEnabled == true) {
		g_bAmpEnabled = false;
		vib_i2c_write(isa_data->client, HCTRL0, 0x00);

		gpio_direction_output(isa_data->pdata->mot_hen_gpio, 0);
		gpio_direction_output(isa_data->pdata->mot_len_gpio, 0);

		clk_disable(isa_data->mot_clk);
	}

	return VIBE_S_SUCCESS;
}

/*
** Called to enable amp (enable output force)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 nActuatorIndex)
{
	if (g_bAmpEnabled == false) {
		clk_enable(isa_data->mot_clk);

		gpio_direction_output(isa_data->pdata->mot_hen_gpio, 1);
		gpio_direction_output(isa_data->pdata->mot_len_gpio, 1);

		udelay(200);

		vib_i2c_write(isa_data->client, SCTRL, g_nLDO_Voltage);

		/* If the PWM frequency is 44.8kHz, then the output frequency will be 44.8/div_factor
		HCTRL0[1:0] is the div_factor, below setting sets div_factor to 256, so o/p frequency is 175 Hz
		*/
		vib_i2c_write(isa_data->client, HCTRL0, 0x11);
		vib_i2c_write(isa_data->client, HCTRL1, 0xC0);
		vib_i2c_write(isa_data->client, HCTRL2, 0x00);
		vib_i2c_write(isa_data->client, HCTRL3, (0x03 + (g_nPWM_PLLDiv<<4)));
		vib_i2c_write(isa_data->client, HCTRL4, g_nPWM_Freq);
		vib_i2c_write(isa_data->client, HCTRL5, g_nPWM_Duty);
		vib_i2c_write(isa_data->client, HCTRL6, g_nPWM_Period);

		/* Haptic Enable + PWM generation mode */
		vib_i2c_write(isa_data->client, HCTRL0, 0x91);

		g_bAmpEnabled = true;	/* to force ImmVibeSPI_ForceOut_AmpDisable disabling the amp */
	}
	return VIBE_S_SUCCESS;
}

/*
** Called at initialization time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Initialize(void)
{


#if 0
	clk_enable(isa_data->mot_clk);
	gpio_direction_output(isa_data->pdata->mot_len_gpio, 1);

	udelay(200);

	immvibe_i2c_write(isa_data->client, SCTRL, g_nLDO_Voltage);

	/*If the PWM frequency is 44.8kHz, then the output frequency will be 44.8/div_factor
	HCTRL0[1:0] is the div_factor, below setting sets div_factor to 256, so o/p frequency is 175 Hz
	*/
	immvibe_i2c_write(isa_data->client, HCTRL0, 0x11);

	immvibe_i2c_write(isa_data->client, HCTRL1, 0xC0);

	immvibe_i2c_write(isa_data->client, HCTRL2, 0x00);

	immvibe_i2c_write(isa_data->client, HCTRL3, (0x03 + (g_nPWM_PLLDiv<<4)));

	immvibe_i2c_write(isa_data->client, HCTRL4, g_nPWM_Freq);

	immvibe_i2c_write(isa_data->client, HCTRL5, g_nPWM_Duty);

	immvibe_i2c_write(isa_data->client, HCTRL6, g_nPWM_Period);

	g_bAmpEnabled = true;   /* to force ImmVibeSPI_ForceOut_AmpDisable disabling the amp */

	ImmVibeSPI_ForceOut_AmpDisable(0);
#endif

    return VIBE_S_SUCCESS;
}

/*
** Called at termination time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Terminate(void)
{
	/*	SYS_API_LEN_LOW; */
	/*	SYS_API_POWER_DISABLE; */
	/*	clk_disable(isa_data->mot_clk);	*/
	gpio_direction_output(isa_data->pdata->mot_hen_gpio, 0);
	gpio_direction_output(isa_data->pdata->mot_len_gpio, 0);

	return VIBE_S_SUCCESS;
}

/*
** Called by the real-time loop to set PWM duty cycle
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 nActuatorIndex, VibeUInt16 nOutputSignalBitDepth, VibeUInt16 nBufferSizeInBytes, VibeInt8 * pForceOutputBuffer)
{
    unsigned int duty;
    VibeInt8 nForce;

	switch (nOutputSignalBitDepth) {
	case 8:
		/* pForceOutputBuffer is expected to contain 1 byte */
		if (nBufferSizeInBytes != 1)
			return VIBE_E_FAIL;

		nForce = pForceOutputBuffer[0];
		break;
	case 16:
		/* pForceOutputBuffer is expected to contain 2 byte */
		if (nBufferSizeInBytes != 2)
			return VIBE_E_FAIL;

		/* Map 16-bit value to 8-bit */
		nForce = ((VibeInt16 *)pForceOutputBuffer)[0] >> 8;
		break;
	default:
		/* Unexpected bit depth */
		return VIBE_E_FAIL;
    }

    if (nForce == 0) {
		vib_i2c_write(isa_data->client, HCTRL5, g_nPWM_Duty);
		/*
		if (g_bAmpEnabled == true) {
			ImmVibeSPI_ForceOut_AmpDisable(0);
		}
		*/
    } else {
		/*
		if (g_bAmpEnabled != true) {
			ImmVibeSPI_ForceOut_AmpEnable(0);
		}
		*/
		duty = g_nPWM_Duty + ((g_nPWM_Duty-1)*nForce)/127;
		vib_i2c_write(isa_data->client, HCTRL5, duty);
    }
    return VIBE_S_SUCCESS;
}

/*
** Called to set force output frequency parameters
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetFrequency(VibeUInt8 nActuatorIndex, VibeUInt16 nFrequencyParameterID, VibeUInt32 nFrequencyParameterValue)
{
    return VIBE_S_SUCCESS;
}

/*
** Called to get the device name (device name must be returned as ANSI char)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 nActuatorIndex, char *szDevName, int nSize)
{
    if ((!szDevName) || (nSize < 1))
		return VIBE_E_FAIL;

    DbgOut((KERN_DEBUG "ImmVibeSPI_Device_GetName.\n"));

    strncpy(szDevName, "Generic Linux Device", nSize-1);
    szDevName[nSize - 1] = '\0';    /* make sure the string is NULL terminated */

    return VIBE_S_SUCCESS;
}
