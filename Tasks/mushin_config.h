#pragma once

/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		mushin_config.h
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, muShin Inc
//
//	Purpose/Functionality:	Defines that control the build type, global externs for functions and variables 
//
/////////////////////////////////////////////////////////////////////////////////////////////

#ifndef MUSHIN_CONFIG_H
#define MUSHIN_CONFIG_H

//#include "Nrf_delay.h"
//#include "nrf_drv_gpiote.h"

/**
 * @brief Macro to be used in a formatted string to a pass float number to the log.
 *
 * Macro should be used in formatted string instead of the %f specifier together with
 * @ref NRF_LOG_FLOAT macro.
 * Example: NRF_LOG_INFO("My float number" NRF_LOG_FLOAT_MARKER "\r\n", NRF_LOG_FLOAT(f)))
 */
#define MUSHIN_LOG_FLOAT_MARKER "%s%d.%05d"
//#define MUSHIN_LOG_FLOAT_MARKER_4 "%s%d.%5d"
//#define MUSHIN_LOG_FLOAT_MARKER_3 "%s%d.%5d"
//#define MUSHIN_LOG_FLOAT_MARKER_2 "%s%d.%5d"
//#define MUSHIN_LOG_FLOAT_MARKER_1 "%s%d.%5d"

/**
 * @brief Macro for dissecting a float number into two numbers (integer and residuum).
 */

#define MUSHIN_LOG_FLOAT(val) (uint32_t)(((val) < 0 && (val) > -1.0) ? "-" : ""),   \
                           (int32_t)(val),                                       \
                           (int32_t)((((val) > 0) ? (val) - (int32_t)(val)       \
                                                : (int32_t)(val) - (val))*100000)


#define MUSHIN_SPRINTF_FLOAT(val) (char *)(((val) < 0 && (val) > -1.0) ? "-" : ""),   \
                           (int)(val),                                       \
                           (int)((((val) > 0) ? (val) - (int32_t)(val)       \
                                                : (int32_t)(val) - (val))*100000)


#define MUSHIN_MIN(_x, _y)   (((_x) > (_y))? (_y):(_x))
	

///////////////////////////////////////////////////////////
//
// Task enable switches
//
// Though in a proper release, these all need to be defined, 
// if we're debugging a faulty mask, it is very handy to be
// disable one or more of these tasks.
//
///////////////////////////////////////////////////////////

// [ADK] 2021-02-26  Changes for new scheme ..

#define NEW_HW_SCHEME_ENABLED				1
#define APP_ENABLED								1
#define LSM6DS3_ENABLED						1
#define LSM6DS3_BLE_OUTPUT_ENABLED			1
#define TIMER2_COUNTER_ENABLED				0
#define AD8237_ENABLED							1
#define SAADC_AIN0_ENABLED					1
#define SAADC_AIN1_ENABLED					1
#define SAADC_AIN2_ENABLED					1
#define SAADC_AIN3_ENABLED					0
#define SAADC_DIFF_AIN13_ENABLED				0
#define SAADC_DIFF_AIN23_ENABLED				0
#define SAADC_DIFF_AIN01_ENABLED				0
#define SAADC_AIN4_ENABLED					0
#define SAADC_AIN5_ENABLED					0


///////////////////////////////////////////////////////////
//
// Debugging enable switches
//
//
///////////////////////////////////////////////////////////

#define LOG_TASK_NOTIFICATION_ENABLED		1
#define TASK_START_DEBUG_ENABLED		1
#define GO_AHEAD_DEBUG_ENABLED			1
#define MEMORY_DEBUG_ENABLED			1
#define LSM6DS3_DEBUG_ENABLED					1
#define LSM6DS3_MAG_RAW_DEBUG_ENABLED		0
#define LSM6DS3_ACC_RAW_DEBUG_ENABLED		0
#define LSM6DS3_WKUP_ENABLED					1
#define APP_DEBUG_ENABLED						1
#define APP_DEBUG_PRINT_FLOAT_ENABLED		1
#define APP_RINGBUFFER_ENABLED				1
#define APP_DIGITAL_FILTER_ENABLED			1
#define APP_SPP_DEBUG_OUTPUT_ENABLED		0


///////////////////////////////////////////////////////
// Task Priorities and Stack Sizes
///////////////////////////////////////////////////////

#define	APP_PRIORITY					( tskIDLE_PRIORITY + 3 )
#define	LSM6DS3_PRIORITY				( tskIDLE_PRIORITY + 4 )
#define	TWI0_PRIORITY					( tskIDLE_PRIORITY + 3 )

#define	APP_STACKSIZE					( 256 ) // was 128
#define	LSM6DS3_STACKSIZE				( 250 )
#define	TWI0_STACKSIZE				( 550 )
#define 	CMD_LINE_MAX_LEN				(64)


//#if APP_USE_DK_BOARD
#define 	LSM6DS3_I2C_ADDR				(0x6A)
//#endif
typedef enum {
	LSM6DS3_MODE_DEFAULT 		 = 0x00,
	LSM6DS3_MODE_HWTAP 		 	 = 0x01,
} LSM6DS3_MODE_t;
	
///////////////////////////////////////////////////////
// ADC scanner parameters
///////////////////////////////////////////////////////
#define ADC_SAMPLES_IN_BUFFER		(3)
#define ADC_POOL_SIZE		((SAADC_AIN0_ENABLED + SAADC_AIN1_ENABLED + SAADC_AIN2_ENABLED + SAADC_AIN3_ENABLED + SAADC_AIN4_ENABLED + SAADC_AIN5_ENABLED  + SAADC_DIFF_AIN13_ENABLED + SAADC_DIFF_AIN23_ENABLED + SAADC_DIFF_AIN01_ENABLED) * ADC_SAMPLES_IN_BUFFER)

#define SAMPLES_RB_SIZE		512
extern	void *get_samples_rb_ctrl(void);
extern	void samples_rb_read(void *arg,  uint16_t *s0, uint16_t *s1, uint16_t *s2, uint16_t *s3);
extern	int16_t samples_rb_available_size(void *arg);


#endif // MUSHIN_CONFIG_H	

