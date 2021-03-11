
/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		lsm6ds3.c
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, muShin
//
//	Purpose/Functionality:	LSM6DS3 Driver
//
/////////////////////////////////////////////////////////////////////////////////////////////


#include "app_config.h"
#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"

#include "app_timer.h"
#include "nrf_drv_clock.h"
#include "nrfx_gpiote.h"
#include "nrf_gpiote.h"

#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_freertos.h"

#include "peer_manager.h"
#include "bsp_btn_ble.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"
#include "fds.h"
#include "ble_conn_state.h"
#include "nrf_ble_gatt.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mushin_config.h"
#include "i2c.h"
#include "app.h"
#include "lsm6ds3.h"

TaskHandle_t			m_lsm6ds3_task = NULL;
bool bLSM6DS3_Ready = false;
bool bXLGY_Reader_Ready = false;
bool bXL_Data_Ready = false; 
bool bGY_Data_Ready = false; 
#if LSM6DS3_ENABLED
#define LSM_QUEUE_LENGTH				( 4 )
#define LSM_WAIT_DATA_MS				( 24 )

static lsm6ds3_settings_t  lsm6ds3_settings;
static plsm6ds3_settings settings = &lsm6ds3_settings;
static xQueueHandle			xLsmInQueue = NULL;
static xQueueHandle 			xLsmOutQueue = NULL;
static xSemaphoreHandle 		xLsmMutex = NULL;

static plsm6ds3Msg		pLsmMsg;

static void vLSM6DS3Task( void *pvParameters );
static BaseType_t xReturned =0;


//****************************************************************************//
//
//  ReadRegisterRegion
//
//  Parameters:
//    *outputPointer -- Pass &variable (base address of) to save read data to
//    offset -- register to read
//    length -- number of bytes to read
//
//  Note:  Does not know if the target memory space is an array or not, or
//    if there is the array is big enough.  if the variable passed is only
//    two bytes long and 3 bytes are requested, this will over-write some
//    other memory!
//
//****************************************************************************//
static ret_code_t  readRegisterRegion(uint8_t *outputPointer , uint8_t offset, uint8_t length)
{
	uint8_t reg = offset;
       TWI0_TxRx(LSM6DS3_I2C_ADDR, &reg, 1, outputPointer, length);
	return NRF_SUCCESS;
}

//****************************************************************************//
//
//  ReadRegister
//
//  Parameters:
//    *outputPointer -- Pass &variable (address of) to save read data to
//    offset -- register to read
//
//****************************************************************************//
static ret_code_t  readRegister(uint8_t* outputPointer, uint8_t offset)
{
	uint8_t reg = offset;
       TWI0_TxRx(LSM6DS3_I2C_ADDR, &reg, 1, outputPointer, 1);
	return NRF_SUCCESS;
}

//****************************************************************************//
//
//  readRegisterInt16
//
//  Parameters:
//    *outputPointer -- Pass &variable (base address of) to save read data to
//    offset -- register to read
//
//****************************************************************************//
static ret_code_t  readRegisterInt16( int16_t* outputPointer, uint8_t offset )
{
	uint8_t myBuffer[2];
	int16_t output;
	ret_code_t returnError = NRF_SUCCESS;
	readRegisterRegion(myBuffer, offset, 2);  //Does memory transfer
	output = (int16_t)myBuffer[0] | (int16_t)(myBuffer[1] << 8);
	
	*outputPointer = output;
	return returnError;
}

//****************************************************************************//
//
//  writeRegister
//
//  Parameters:
//    offset -- register to write
//    dataToWrite -- 8 bit data to write to register
//
//****************************************************************************//
static ret_code_t  writeRegister(uint8_t offset, uint8_t dataToWrite)
{
	uint8_t myBuffer[2];

	myBuffer[0] = offset;
	myBuffer[1] =dataToWrite;
	
	TWI0_Tx(LSM6DS3_I2C_ADDR, myBuffer, 2);
	return NRF_SUCCESS;
}

#if 0
static ret_code_t  embeddedPage( void )
{
	ret_code_t returnError = NRF_SUCCESS;
	writeRegister( LSM6DS3_ACC_GYRO_RAM_ACCESS, 0x80 );
	
	return returnError;
}

static ret_code_t  basePage( void )
{
	ret_code_t returnError = NRF_SUCCESS;
	writeRegister( LSM6DS3_ACC_GYRO_RAM_ACCESS, 0x00 );
	
	return returnError;
}
#endif

//****************************************************************************//
//
//  Main user class -- wrapper for the core class + maths
//
//  Construct with same rules as the core ( uint8_t busType, uint8_t inputArg )
//
//****************************************************************************//
static void  vLSM6DS3_InitDefault(void)
{
	//Construct with these default settings

	settings->mode = LSM6DS3_MODE_DEFAULT;
	settings->gyroEnabled = 1;  //Can be 0 or 1
	settings->gyroRange = 2000;   //Max deg/s.  Can be: 125, 245, 500, 1000, 2000
	settings->gyroSampleRate = 416;   //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666
	settings->gyroBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;
	settings->gyroFifoEnabled = 1;  //Set to include gyro in FIFO
	settings->gyroFifoDecimation = 1;  //set 1 for on /1

	settings->accelEnabled = 1;
	settings->accelODROff = 1;
	settings->accelRange = 16;      //Max G force readable.  Can be: 2, 4, 8, 16
	settings->accelSampleRate = 416;  //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666, 3332, 6664, 13330
	settings->accelBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;
	settings->accelFifoEnabled = 1;  //Set to include accelerometer in the FIFO
	settings->accelFifoDecimation = 1;  //set 1 for on /1

	settings->tempEnabled = 1;

	//Select interface mode
//	settings.commMode = 1;  //Can be modes 1, 2 or 3

	//FIFO control data
	settings->fifoThreshold = 3000;  //Can be 0 to 4096 (16 bit bytes)
	settings->fifoSampleRate = 50;  //default 10Hz, set 50Hz
	settings->fifoModeWord = 0;  //Default off

}

static void  vLSM6DS3_InitHWTap(void)
{
	//Construct with these default settings

	settings->mode = LSM6DS3_MODE_HWTAP;
	settings->gyroEnabled = 0;  //Can be 0 or 1

	settings->accelEnabled = 1;
	settings->accelODROff = 0;
	settings->accelRange = 2;      //Max G force readable.  Can be: 2, 4, 8, 16
	settings->accelSampleRate = 416;  //Hz.  Can be: 13, 26, 52, 104, 208, 416, 833, 1666, 3332, 6664, 13330
	settings->accelBandWidth = 200;  //Hz.  Can be: 50, 100, 200, 400;

	settings->accelFifoEnabled = 0;  //Set to include accelerometer in the FIFO
	settings->tempEnabled = 0;
	settings->tap_cfg1 = ( LSM6DS3_ACC_GYRO_TAP_Z_EN_ENABLED | LSM6DS3_ACC_GYRO_TAP_Y_EN_ENABLED |LSM6DS3_ACC_GYRO_TAP_X_EN_ENABLED); 
	settings->tap_ths = 0x03;
	settings->int_dur2 = 0x7F;

}


//****************************************************************************//
//
//  Configuration section
//
//  This uses the stored SensorSettings to start
//
//****************************************************************************//
static ret_code_t  vLSM6DS3_SetUp(plsm6ds3_settings pSettingsYouWanted)
{
	//Check the settings structure values to determine how to setup the device
	uint8_t dataToWrite = 0;  //Temporary variable

	ret_code_t returnError = NRF_SUCCESS;
	
	// Copy the values from the user's settings into the output 'pSettingsYouWanted'
	// compare settings with 'pSettingsYouWanted' after 'begin' to see if anything changed
	if(pSettingsYouWanted != NULL){ 
		settings->gyroEnabled			= pSettingsYouWanted->gyroEnabled;
		settings->gyroRange			= pSettingsYouWanted->gyroRange;
		settings->gyroSampleRate		= pSettingsYouWanted->gyroSampleRate;
		settings->gyroBandWidth		= pSettingsYouWanted->gyroBandWidth;
		settings->gyroFifoEnabled		= pSettingsYouWanted->gyroFifoEnabled;
		settings->gyroFifoDecimation	= pSettingsYouWanted->gyroFifoDecimation;
		settings->accelEnabled		= pSettingsYouWanted->accelEnabled;
		settings->accelODROff		= pSettingsYouWanted->accelODROff;
		settings->accelRange			= pSettingsYouWanted->accelRange;
		settings->accelSampleRate		= pSettingsYouWanted->accelSampleRate;
		settings->accelBandWidth		= pSettingsYouWanted->accelBandWidth;
		settings->accelFifoEnabled		= pSettingsYouWanted->accelFifoEnabled;
		settings->accelFifoDecimation	= pSettingsYouWanted->accelFifoDecimation;
		settings->tempEnabled		= pSettingsYouWanted->tempEnabled;
		settings->commMode			= pSettingsYouWanted->commMode;
		settings->fifoThreshold		= pSettingsYouWanted->fifoThreshold;
		settings->fifoSampleRate		= pSettingsYouWanted->fifoSampleRate;
		settings->fifoModeWord		= pSettingsYouWanted->fifoModeWord;
	}

	//Setup the accelerometer******************************
	dataToWrite = 0; //Start Fresh!
	if ( settings->accelEnabled == 1) {
		//Build config reg
		//First patch in filter bandwidth
		switch (settings->accelBandWidth) {
		case 50:
			dataToWrite |= LSM6DS3_ACC_GYRO_BW_XL_50Hz;
			break;
		case 100:
			dataToWrite |= LSM6DS3_ACC_GYRO_BW_XL_100Hz;
			break;
		case 200:
			dataToWrite |= LSM6DS3_ACC_GYRO_BW_XL_200Hz;
			break;
		default:  //set default case to max passthrough
			settings->accelBandWidth = 400;
		case 400:
			dataToWrite |= LSM6DS3_ACC_GYRO_BW_XL_400Hz;
			break;
		}
		//Next, patch in full scale
		switch (settings->accelRange) {
		case 2:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_XL_2g;
			break;
		case 4:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_XL_4g;
			break;
		case 8:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_XL_8g;
			break;
		default:  //set default case to 16(max)
			settings->accelRange = 16;
		case 16:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_XL_16g;
			break;
		}
		//Lastly, patch in accelerometer ODR
		switch (settings->accelSampleRate) {
		case 13:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_13Hz;
			break;
		case 26:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_26Hz;
			break;
		case 52:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_52Hz;
			break;
		default:  //Set default to 104
			settings->accelSampleRate = 104;
		case 104:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_104Hz;
			break;
		case 208:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_208Hz;
			break;
		case 416:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_416Hz;
			break;
		case 833:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_833Hz;
			break;
		case 1660:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_1660Hz;
			break;
		case 3330:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_3330Hz;
			break;
		case 6660:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_6660Hz;
			break;
		case 13330:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_XL_13330Hz;
			break;
		}
	}
	else
	{
		//dataToWrite already = 0 (powerdown);
	}

	//Now, write the patched together data
	writeRegister(LSM6DS3_ACC_GYRO_CTRL1_XL, dataToWrite);

	//Set the ODR bit
	readRegister(&dataToWrite, LSM6DS3_ACC_GYRO_CTRL4_C);
	dataToWrite &= ~((uint8_t)LSM6DS3_ACC_GYRO_BW_SCAL_ODR_ENABLED);
	if ( settings->accelODROff == 1) {
		dataToWrite |= LSM6DS3_ACC_GYRO_BW_SCAL_ODR_ENABLED;
	}
	writeRegister(LSM6DS3_ACC_GYRO_CTRL4_C, dataToWrite);

	//Setup the gyroscope**********************************************
	dataToWrite = 0; //Start Fresh!
	if ( settings->gyroEnabled == 1) {
		//Build config reg
		//First, patch in full scale
		switch (settings->gyroRange) {
		case 125:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_125_ENABLED;
			break;
		case 245:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_G_245dps;
			break;
		case 500:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_G_500dps;
			break;
		case 1000:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_G_1000dps;
			break;
		default:  //Default to full 2000DPS range
			settings->gyroRange = 2000;
		case 2000:
			dataToWrite |= LSM6DS3_ACC_GYRO_FS_G_2000dps;
			break;
		}
		//Lastly, patch in gyro ODR
		switch (settings->gyroSampleRate) {
		case 13:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_13Hz;
			break;
		case 26:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_26Hz;
			break;
		case 52:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_52Hz;
			break;
		default:  //Set default to 104
			settings->gyroSampleRate = 104;
		case 104:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_104Hz;
			break;
		case 208:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_208Hz;
			break;
		case 416:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_416Hz;
			break;
		case 833:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_833Hz;
			break;
		case 1660:
			dataToWrite |= LSM6DS3_ACC_GYRO_ODR_G_1660Hz;
			break;
		}
	}
	else
	{
		//dataToWrite already = 0 (powerdown);
	}
	//Write the byte
	writeRegister(LSM6DS3_ACC_GYRO_CTRL2_G, dataToWrite);

	//Setup the internal temperature sensor
	if ( settings->tempEnabled == 1) {
	}

	if (settings->mode == LSM6DS3_MODE_HWTAP) 
	{
		writeRegister( LSM6DS3_ACC_GYRO_TAP_CFG1, settings->tap_cfg1);
		writeRegister( LSM6DS3_ACC_GYRO_TAP_THS_6D, settings->tap_ths);
		writeRegister( LSM6DS3_ACC_GYRO_INT_DUR2, settings->int_dur2);
		// Single & Double tap enabled (SINGLE_DOUBLE_TAP = 1)
		// Write 80h into WAKE_UP_THS
		writeRegister( LSM6DS3_ACC_GYRO_WAKE_UP_THS, 0x80 );
	
		// Single tap interrupt driven to INT1 pin -- enable latch
		// Write 48h into MD1_CFG
		writeRegister( LSM6DS3_ACC_GYRO_MD1_CFG, 0x48 );

#if LSM6DS3_WKUP_ENABLED

		nrfx_gpiote_in_event_enable(LSM6DS3_INT1_PIN, true);
#endif // LSM6DS3_WKUP_ENABLED
		
	}

//	Return WHO AM I reg  //Not no mo!
//	uint8_t result;
//	readRegister(&result, LSM6DS3_ACC_GYRO_WHO_AM_I_REG);

	return returnError;
}

void  vLSM6DS3Reset(void)
{
	uint8_t ctrl3;

	readRegister(&ctrl3, LSM6DS3_ACC_GYRO_CTRL3_C);
	ctrl3 |= LSM6DS3_ACC_GYRO_SW_RESET_RESET_DEVICE;
	writeRegister(LSM6DS3_ACC_GYRO_CTRL3_C, ctrl3);

	do {
		readRegister(&ctrl3, LSM6DS3_ACC_GYRO_CTRL3_C);
		if (!(ctrl3&LSM6DS3_ACC_GYRO_SW_RESET_RESET_DEVICE)) break;
	}while(1);
}

//****************************************************************************//
//
//  Accelerometer section
//
//****************************************************************************//
int16_t readRawAccelX( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUTX_L_XL );
	return output;
}
float readFloatAccelX( void )
{
	float output = calcAccel(readRawAccelX());
	return output;
}

int16_t readRawAccelY( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUTY_L_XL );
	return output;
}
float readFloatAccelY( void )
{
	float output = calcAccel(readRawAccelY());
	return output;
}

int16_t readRawAccelZ( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUTZ_L_XL );
	return output;
}
float readFloatAccelZ( void )
{
	float output = calcAccel(readRawAccelZ());
	return output;
}

float calcAccel( int16_t input )
{
	float output = (float)input * 0.061 * (settings->accelRange >> 1) / 1000;
	return output;
}

//****************************************************************************//
//
//  Gyroscope section
//
//****************************************************************************//
int16_t readRawGyroX( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUTX_L_G );
	return output;
}
float readFloatGyroX( void )
{
	float output = calcGyro(readRawGyroX());
	return output;
}

int16_t readRawGyroY( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUTY_L_G );
	return output;
}
float readFloatGyroY( void )
{
	float output = calcGyro(readRawGyroY());
	return output;
}

int16_t readRawGyroZ( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUTZ_L_G );
	return output;
}
float readFloatGyroZ( void )
{
	float output = calcGyro(readRawGyroZ());
	return output;
}

float calcGyro( int16_t input )
{
	uint8_t gyroRangeDivisor = settings->gyroRange / 125;
	if ( settings->gyroRange == 245 ) {
		gyroRangeDivisor = 2;
	}

	float output = (float)input * 4.375 * (gyroRangeDivisor) / 1000;
	return output;
}

//****************************************************************************//
//
//  Temperature section
//
//****************************************************************************//
int16_t readRawTemp( void )
{
	int16_t output;
	readRegisterInt16( &output, LSM6DS3_ACC_GYRO_OUT_TEMP_L );
	return output;
}  

float readTempC( void )
{
	float output = (float)readRawTemp() / 16; //divide by 16 to scale
	output += 25; //Add 25 degrees to remove offset

	return output;

}

float readTempF( void )
{
	float output = (float)readRawTemp() / 16; //divide by 16 to scale
	output += 25; //Add 25 degrees to remove offset
	output = (output * 9) / 5 + 32;

	return output;

}

float calcTempC( uint16_t input )
{
	float output = (float)input/16; //divide by 16 to scale
	output += 25; //Add 25 degrees to remove offset

	return output;

}

//****************************************************************************//
//
//  FIFO section
//
//****************************************************************************//
void vLSM6DS3fifoBegin( void )
{
	//CONFIGURE THE VARIOUS FIFO SETTINGS
	//
	//
	//This section first builds a bunch of config words, then goes through
	//and writes them all.

	//Split and mask the threshold
	uint8_t thresholdLByte = settings->fifoThreshold & 0x00FF;
	uint8_t thresholdHByte = (settings->fifoThreshold & 0x0F00) >> 8;
	//Pedo bits not configured (ctl2)

	//CONFIGURE FIFO_CTRL3
	uint8_t tempFIFO_CTRL3 = 0;
	if (settings->gyroFifoEnabled == 1)
	{
		//Set up gyro stuff
		//Build on FIFO_CTRL3
		//Set decimation
		tempFIFO_CTRL3 |= (settings->gyroFifoDecimation & 0x07) << 3;

	}
	if (settings->accelFifoEnabled == 1)
	{
		//Set up accelerometer stuff
		//Build on FIFO_CTRL3
		//Set decimation
		tempFIFO_CTRL3 |= (settings->accelFifoDecimation & 0x07);
	}

	//CONFIGURE FIFO_CTRL4  (nothing for now-- sets data sets 3 and 4
	uint8_t tempFIFO_CTRL4 = 0;


	//CONFIGURE FIFO_CTRL5
	uint8_t tempFIFO_CTRL5 = 0;
	switch (settings->fifoSampleRate) {
	default:  //set default case to 10Hz(slowest)
	case 10:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_10Hz;
		break;
	case 25:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_25Hz;
		break;
	case 50:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_50Hz;
		break;
	case 100:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_100Hz;
		break;
	case 200:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_200Hz;
		break;
	case 400:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_400Hz;
		break;
	case 800:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_800Hz;
		break;
	case 1600:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_1600Hz;
		break;
	case 3300:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_3300Hz;
		break;
	case 6600:
		tempFIFO_CTRL5 |= LSM6DS3_ACC_GYRO_ODR_FIFO_6600Hz;
		break;
	}
	//Hard code the fifo mode here:
	tempFIFO_CTRL5 |= settings->fifoModeWord = 6;  //set mode: continuous mode (as new data arrives, the older data is discarded).

	//Write the data
	writeRegister(LSM6DS3_ACC_GYRO_FIFO_CTRL1, thresholdLByte);
	//Serial.println(thresholdLByte, HEX);
	writeRegister(LSM6DS3_ACC_GYRO_FIFO_CTRL2, thresholdHByte);
	//Serial.println(thresholdHByte, HEX);
	writeRegister(LSM6DS3_ACC_GYRO_FIFO_CTRL3, tempFIFO_CTRL3);
	writeRegister(LSM6DS3_ACC_GYRO_FIFO_CTRL4, tempFIFO_CTRL4);
	writeRegister(LSM6DS3_ACC_GYRO_FIFO_CTRL5, tempFIFO_CTRL5);

}
void fifoClear( void ) {
	//Drain the fifo data and dump it
	while( (fifoGetStatus() & 0x1000 ) == 0 ) {
		fifoRead();
	}

}
int16_t fifoRead( void ) {
	//Pull the last data from the fifo
	uint8_t tempReadByte = 0;
	uint16_t tempAccumulator = 0;
	readRegister(&tempReadByte, LSM6DS3_ACC_GYRO_FIFO_DATA_OUT_L);
	tempAccumulator = tempReadByte;
	readRegister(&tempReadByte, LSM6DS3_ACC_GYRO_FIFO_DATA_OUT_H);
	tempAccumulator |= ((uint16_t)tempReadByte << 8);

	return tempAccumulator;
}

uint16_t fifoGetStatus( void ) {
	//Return some data on the state of the fifo
	uint8_t tempReadByte = 0;
	uint16_t tempAccumulator = 0;
	readRegister(&tempReadByte, LSM6DS3_ACC_GYRO_FIFO_STATUS1);
	tempAccumulator = tempReadByte;
	readRegister(&tempReadByte, LSM6DS3_ACC_GYRO_FIFO_STATUS2);
	tempAccumulator |= (tempReadByte << 8);

	return tempAccumulator;  

}
void fifoEnd( void ) {
	// turn off the fifo
	writeRegister(LSM6DS3_ACC_GYRO_FIFO_CTRL5, 0x00);  //Disable
}

ret_code_t  check_lsm6ds3(void)
{
	ret_code_t  returnError = NRF_SUCCESS;
//	vTaskDelay(pdMS_TO_TICKS(50));  
	//Check the ID register to determine if the operation was a success.
	uint8_t readCheck;
	readRegister(&readCheck, LSM6DS3_ACC_GYRO_WHO_AM_I_REG);
	NRF_LOG_RAW_INFO("%s: Im=%02x\r\n", (uint32_t)__func__, readCheck);
	
	if( readCheck != 0x69 )
	{
		returnError = NRF_ERROR_NOT_FOUND;
	}

	return returnError;

}

void vLSM6DS3DefaultStart(void)
{
	vLSM6DS3_InitDefault();
	vLSM6DS3_SetUp(NULL);
	
}

void vLSM6DS3TapStart(void)
{
	vLSM6DS3_InitHWTap();
	vLSM6DS3_SetUp(NULL);
	
	bLSM6DS3_Ready = true;
}

void vLSM6DS3Start( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
       NRF_LOG_FLUSH();
#endif
	/* Create the queues used by application task. */
	xLsmInQueue = xQueueCreate( LSM_QUEUE_LENGTH, sizeof(lsm6ds3_msg_t) );
	xLsmOutQueue = xQueueCreate( 1, sizeof(lsm6ds3_msg_t) );

	/* If the queue could not be created then don't create any tasks that might
	attempt to use the queue. */
	if( xLsmInQueue != NULL && xLsmOutQueue != NULL)
	{

		xLsmMutex = xSemaphoreCreateMutex();
		configASSERT( xLsmMutex );


		xReturned = xTaskCreate(	vLSM6DS3Task,				/* The task that implements the audio control. */
			"LSM",						/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
			usStackSize,					/* The size of the stack allocated to the task. */
			NULL,						/* The parameter is not used, so NULL is passed. */
			uxPriority,					/* The priority allocated to the task. */
			&m_lsm6ds3_task 
			);		
	
		if (xReturned != pdPASS)
		{
			NRF_LOG_ERROR("LSM6DS3 Task not created.");
			APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
		}
	}
	else
	{
		NRF_LOG_ERROR("LSM6DS3 Queue not created.");
		APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
	}
}

static void vLSM6DS3Task( void *pvParameters )
{
	( void ) pvParameters;
//	uint8_t sensor_data[64];
	lsm6ds3_msg_t _msg;
	uint8_t tempStatus;
	uint8_t  data_wait_cnt;

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entering ..\r\n", __func__);
#endif
	
#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Waiting for I2C Master task started and AppTask check the WHO_I_AM register..\r\n", (uint32_t)__func__);
#endif
	// Wait until the I2C task will be ready and LSM6DS3 is functional ..
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

#if GO_AHEAD_DEBUG_ENABLED
	PrintGotGoAhead((uint32_t)__func__);
#endif
	if (!bLSM6DS3_Ready) {
		vTaskDelete(NULL);
		return;
	}
	vLSM6DS3Reset();
// TODO:  [ADK] Use this initialization before going to deep sleep:		vLSM6DS3TapStart();
	vLSM6DS3DefaultStart();
//	vLSM6DS3fifoBegin();
//	fifoClear();
#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s: Start polling FIFO ..\r\n", __func__);
#endif
	pLsmMsg = &_msg;

	while(true) 
	{
		if( xQueueReceive( xLsmInQueue, pLsmMsg, portMAX_DELAY) == pdPASS)
		{
		
			if (pLsmMsg == NULL) continue;
			if (pLsmMsg->req == MUSHIN_APP_XL_REQ)
			{
				data_wait_cnt = LSM_WAIT_DATA_MS;
				do {
					if (data_wait_cnt == 0)  goto no_data;
					readRegister(&tempStatus, LSM6DS3_ACC_GYRO_STATUS_REG);
					if (tempStatus&LSM6DS3_ACC_GYRO_XLDA_DATA_AVAIL) break;
					vTaskDelay(1);
					data_wait_cnt--;
				} while(true);
				pLsmMsg->xl_data[0] = calcAccel(readRawAccelX());
				pLsmMsg->xl_data[1] = calcAccel(readRawAccelY());
				pLsmMsg->xl_data[2] = calcAccel(readRawAccelZ());
			}else
			if (pLsmMsg->req == MUSHIN_APP_GYRO_REQ)
			{
				data_wait_cnt = LSM_WAIT_DATA_MS;
				do {
					if (data_wait_cnt == 0)  goto no_data;
					readRegister(&tempStatus, LSM6DS3_ACC_GYRO_STATUS_REG);
					if (tempStatus&LSM6DS3_ACC_GYRO_GDA_DATA_AVAIL) break;
					vTaskDelay(1);
					data_wait_cnt--;
				} while(true);
				pLsmMsg->gyro_data[0] = calcGyro(readRawGyroX());
				pLsmMsg->gyro_data[1] = calcGyro(readRawGyroY());
				pLsmMsg->gyro_data[2] = calcGyro(readRawGyroZ());
			}else
			if (pLsmMsg->req == MUSHIN_APP_XL_GYRO_REQ)
			{
				data_wait_cnt = LSM_WAIT_DATA_MS;
				do {
					if (data_wait_cnt == 0)  goto no_data;
					readRegister(&tempStatus, LSM6DS3_ACC_GYRO_STATUS_REG);
					if ((tempStatus&(LSM6DS3_ACC_GYRO_GDA_DATA_AVAIL  | LSM6DS3_ACC_GYRO_XLDA_DATA_AVAIL)) == (LSM6DS3_ACC_GYRO_GDA_DATA_AVAIL  | LSM6DS3_ACC_GYRO_XLDA_DATA_AVAIL)) break;
					vTaskDelay(1);
					data_wait_cnt--;
				} while(true);
				pLsmMsg->xl_data[0] = calcAccel(readRawAccelX());
				pLsmMsg->xl_data[1] = calcAccel(readRawAccelY());
				pLsmMsg->xl_data[2] = calcAccel(readRawAccelZ());
				pLsmMsg->gyro_data[0] = calcGyro(readRawGyroX());
				pLsmMsg->gyro_data[1] = calcGyro(readRawGyroY());
				pLsmMsg->gyro_data[2] = calcGyro(readRawGyroZ());
			}else{
no_data:			
				pLsmMsg->xl_data[0] = 0.0;
				pLsmMsg->xl_data[1] = 0.0;
				pLsmMsg->xl_data[2] = 0.0;
				pLsmMsg->gyro_data[0] = 0.0;
				pLsmMsg->gyro_data[1] = 0.0;
				pLsmMsg->gyro_data[2] = 0.0;
			}

			if (pLsmMsg->lsm__doneCallback != NULL)
				pLsmMsg->lsm__doneCallback(pLsmMsg);

		}
	}

}

portBASE_TYPE LSM_MsgPut(plsm6ds3Msg msg)
{
	if(xQueueSend(xLsmInQueue, msg, ( portTickType ) 10) != pdPASS)
		{
//			NRF_LOG_RAW_INFO("%s()/%d \r\n", (uint32_t *)__func__, __LINE__);
			APP_ERROR_CHECK(NRF_ERROR_TIMEOUT);
		}
	return pdPASS;
}

portBASE_TYPE LSM_MsgGet(plsm6ds3Msg msg)
{
	return xQueueReceive(xLsmOutQueue, msg, portMAX_DELAY);
}

portBASE_TYPE  LSM_OnDone(void *pvParameters)
{
	plsm6ds3Msg msg = (plsm6ds3Msg)pvParameters;

	if(xQueueSend(xLsmOutQueue, msg, ( portTickType ) (10 )) != pdPASS)
	{
			APP_ERROR_CHECK(NRF_ERROR_TIMEOUT);
	}
		
	return pdPASS;
}	
portBASE_TYPE LSM_Take(void *pvParameters)
{
	( void ) pvParameters;
	
	if (xLsmMutex == NULL)  return pdFAIL;

        // Obtain the semaphore - don't block even if the semaphore is not
        // available during a time more then a reasonable.
	if( xSemaphoreTake( xLsmMutex, (portMAX_DELAY -10) )  != pdPASS )
	{
		// Never return from  ..
		{
			APP_ERROR_CHECK(NRF_ERROR_MUTEX_LOCK_FAILED);
		}
	}
	return pdPASS;
}
portBASE_TYPE LSM_Give(void *pvParameters)
{
	( void ) pvParameters;
	
	if (xLsmMutex == NULL)  return pdFAIL;

	if( xSemaphoreGive( xLsmMutex ) != pdPASS )
	{
		// Never return from  ..
		{
			APP_ERROR_CHECK(NRF_ERROR_MUTEX_UNLOCK_FAILED);
		}
	}
	return pdPASS;
}

portBASE_TYPE LSM_Rx(plsm6ds3Msg msg)
{
	if(!bLSM6DS3_Ready) return pdFAIL;
	
	bXLGY_Reader_Ready = false;  // Avoid for next call for RxXL()/RxGyro()
	LSM_Take(NULL);
	msg->lsm__doneCallback = LSM_OnDone;
	LSM_MsgPut(msg);
	LSM_MsgGet(msg);
	bXLGY_Reader_Ready = true;
	LSM_Give(NULL);
	return pdPASS;
}

portBASE_TYPE LSM_RxXL(float *x, float*y, float *z)
{
	lsm6ds3_msg_t msg;
	
	bXL_Data_Ready = false;
	msg.req = MUSHIN_APP_XL_REQ;
	LSM_Rx(&msg);
	*x = msg.xl_data[0];
	*y = msg.xl_data[1];
	*z = msg.xl_data[2];
	bXL_Data_Ready = true;  // The data can be obtained to output ..
	return pdPASS;
}

portBASE_TYPE LSM_RxGyro(float *x, float*y, float *z)
{
	lsm6ds3_msg_t msg;
	bGY_Data_Ready = false;
	msg.req = MUSHIN_APP_GYRO_REQ;
	LSM_Rx(&msg);
	*x = msg.gyro_data[0];
	*y = msg.gyro_data[1];
	*z = msg.gyro_data[2];
	bGY_Data_Ready = true;
	return pdPASS;
}

#endif // LSM6DS3_ENABLED

