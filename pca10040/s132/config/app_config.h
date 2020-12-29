#pragma once

/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		app_config.h
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, 2020  *** Inc
//
//	Purpose/Functionality:	Version information and overrides to sdk_config.h, as per Nordic convention
//
/////////////////////////////////////////////////////////////////////////////////////////////


#ifndef APP_CONFIG_H
#define APP_CONFIG_H

//lint -emacro(491,MUSH_FEATURE_ENABLED) // Suppers warning 491 "non-standard use of 'defined' preprocessor operator"
#define MUSH_FEATURE_ENABLED(feature) \
    ((defined(feature ## _ENABLED) && (feature ## _ENABLED)) ? 1 : 0)

#define APP_USE_DK_BOARD						0
#define BLE_SPP_SERVICE_ENABLED				0
#define BLE_NUS_SERVICE_ENABLED				1
#define NRF_SDH_BLE_VS_UUID_COUNT			1
#define BLE_HRS_ENABLED						0
#define NRF_SDH_BLE_GATT_MAX_MTU_SIZE		100

#if MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
#define BLE_NUS_ENABLED 						1
#endif // MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)


#define TWI_ENABLED 							1
#define NRFX_TWIM_ENABLED						1
#define NRFX_TWIM0_ENABLED					1
#define TWI0_ENABLED 							1
#define TWI0_USE_EASY_DMA						1
#define _TWI0_PIN_SCL							ARDUINO_SCL_PIN
#define _TWI0_PIN_SDA							ARDUINO_SDA_PIN
#define TWI0_INSTANCE_ID						0
#define LSM6DS3_INT1_PIN						24
#define LSM6DS3_INT2_PIN						25
#if APP_USE_DK_BOARD
#define DEBUG_LED_PIN							BSP_LED_3
#else
//#define DEBUG_SAADC_PIN						22
#endif

// For ADCs inputs..
#define TIMER_ENABLED 							1
#define NRFX_TIMER_ENABLED 					1
#define TIMER4_ENABLED 							1 //Timer used for adc (via ppi)
#define NRFX_TIMER4_ENABLED					1
#define SAADC_ENABLED 							1
#define PPI_ENABLED								1
#define NRFX_PPI_ENABLED						1
/*  +3.3V_POT signal */
#define  ADC_POWER_PIN							7
#define  ADC_VREF_PIN							23
#if TIMER_ENABLED
#define ENABLED_TIMER_COUNT (TIMER0_ENABLED+TIMER1_ENABLED+TIMER2_ENABLED+TIMER3_ENABLED+TIMER4_ENABLED)
#endif

#define NRF_LOG_BUFSIZE   8192
#define NRF_LOG_DEFERRED  0
	
#define NRF_LOG_BACKEND_RTT_ENABLED		1
#define NRF_LOG_BACKEND_UART_ENABLED	0

#define APP_TICK_INTERVAL         			(1)                                    /**< Application level measurement interval (ms). */
#define PRINT_SEND_DELAY_IN_MSEC		10
#define ADC_SAMPLING_TIME_IN_MSEC	10

#endif
