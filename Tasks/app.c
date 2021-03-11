
/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		app.c
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, ?? Inc
//
//	Purpose/Functionality:	Application task that control/monitors other tasks
//
/////////////////////////////////////////////////////////////////////////////////////////////

#include "app_config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"

#include "app_timer.h"
#include "nrf_drv_clock.h"

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

// #define NRF_LOG_MODULE_NAME Data_manage
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mushin_config.h"
#include "app.h"
#include "i2c.h"

// #include "lsm6ds3.h"



/////////////////////////////////////////////////////////////////////////////////////////////
//  Defines                                                                                                                                               //
/////////////////////////////////////////////////////////////////////////////////////////////
typedef enum
{
	MUSHIN_APP_IDLE =0,
	MUSHIN_APP_INITED,
	MUSHIN_APP_MEASED,
	MUSHIN_APP_CONNECTED,
	MUSHIN_APP_PREPARED_FOR_SLEEP,
	
	MUSHIN_APP_LAST_STATE
} app_state_t; 


typedef struct {
	app_state_t	state;
	uint32_t 		timestamp;   // last accepted tick
	
} app_handle_t, *p_app_handle; 

#define APP_QUEUE_LENGTH				( 4 )

static xQueueHandle			xAppInQueue = NULL;
static xQueueHandle 			xAppOutQueue = NULL;
static xSemaphoreHandle 		xAppMutex = NULL;

TaskHandle_t		m_app_task = NULL;
#if APP_RINGBUFFER_ENABLED
TaskHandle_t		m_sa_task = NULL;
static void SamplesTask( void *pvParameters );
#endif //APP_RINGBUFFER_ENABLED
#if FREERTOS_TASK_STATS_ENABLED
static char pcWriteBuffer[2048];
#endif // FREERTOS_TASK_STATS_ENABLED

float CrankDataRawThreshold = 0.01;
float CrankDataFilteredThreshold = 0.0175;
	
#if APP_ENABLED
static void AppTask( void *pvParameters );
static app_handle_t app_state = {
	.state = MUSHIN_APP_INITED,
	.timestamp = 0	
};
static p_app_handle handler = &app_state;

static app_msg_t  mAppMsg;
static pAppMsg   pMasterAppMsg;

#if APP_DIGITAL_FILTER_ENABLED
#define DF1FILTER_TAP_NUM 21

typedef struct {
  float history[DF1FILTER_TAP_NUM];
  unsigned int last_index;
} DF1Filter;

static void DF1Filter_init(DF1Filter* f);
static void DF1Filter_put(DF1Filter* f, float input);
static float DF1Filter_get(DF1Filter* f);

static float filter_taps[DF1FILTER_TAP_NUM] = {
  -0.02010411882885732,
  -0.05842798004352509,
  -0.061178403647821976,
  -0.010939393385338943,
  0.05125096443534972,
  0.033220867678947885,
  -0.05655276971833928,
  -0.08565500737264514,
  0.0633795996605449,
  0.310854403656636,
  0.4344309124179415,
  0.310854403656636,
  0.0633795996605449,
  -0.08565500737264514,
  -0.05655276971833928,
  0.033220867678947885,
  0.05125096443534972,
  -0.010939393385338943,
  -0.061178403647821976,
  -0.05842798004352509,
  -0.02010411882885732
};

static void DF1Filter_init(DF1Filter* f) {
  int i;
  for(i = 0; i < DF1FILTER_TAP_NUM; ++i)
    f->history[i] = 0;
  f->last_index = 0;
}

static void DF1Filter_put(DF1Filter* f, float input) {
  f->history[f->last_index++] = input;
  if(f->last_index == DF1FILTER_TAP_NUM)
    f->last_index = 0;
}


static float DF1Filter_get(DF1Filter* f) {
  float acc = 0;
  int index = f->last_index, i;
  for(i = 0; i < DF1FILTER_TAP_NUM; ++i) {
    index = index != 0 ? index-1 : DF1FILTER_TAP_NUM-1;
    acc += f->history[index] * filter_taps[i];
  };
  return acc;
}

static DF1Filter DF1;
static DF1Filter DF2;

#endif //MUSH_FEATURE_ENABLED(DIGITAL_FILTER)

/////////////////////////////////////////////////////////////////////////////////////////////
// Creates and starts FreeRTOS app task
/////////////////////////////////////////////////////////////////////////////////////////////
static BaseType_t xReturned = 0;
  
void vAppStart( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

//int IDX;
#if TASK_START_DEBUG_ENABLED
//for(IDX=0; IDX<10;IDX++)
{
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
	NRF_LOG_FLUSH();
 }
#endif
	/* Create the queues used by application task. */
	xAppInQueue = xQueueCreate( APP_QUEUE_LENGTH, sizeof(app_msg_t) );
	xAppOutQueue = xQueueCreate( 1, sizeof(app_msg_t) );

	/* If the queue could not be created then don't create any tasks that might
	attempt to use the queue. */
	if( xAppInQueue != NULL && xAppOutQueue != NULL)
	{

		xAppMutex = xSemaphoreCreateMutex();
		configASSERT( xAppMutex );

		xReturned = xTaskCreate(	AppTask,				/* The task that implements the audio control. */
			"App",						/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
			usStackSize,					/* The size of the stack allocated to the task. */
			NULL,						/* The parameter is not used, so NULL is passed. */
			uxPriority,					/* The priority allocated to the task. */
			&m_app_task 
			);		
	
		if (xReturned != pdPASS)
		{
			NRF_LOG_ERROR("AppTask task not created.");
			APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
		}
	}
}
#endif // APP_ENABLED

bool Wait4ReleaseDependedTask(TaskHandle_t  *taskHandle)
{
	static uint16_t nIdx;
	
	for(nIdx = 0 ; nIdx < 10 ; nIdx ++)
	{
		if(*taskHandle == NULL) vTaskDelay((TickType_t)(pdMS_TO_TICKS(100))); 
		else 
		{
			xTaskNotifyGive(*taskHandle) ;
			return true;
		}
	}
	return false;
}

#if GO_AHEAD_DEBUG_ENABLED
static char *Pgga_format = "%s Got 'Go-Ahead' signal ..\r\n";

void PrintGotGoAhead(uint32_t FunctionName)
{
	NRF_LOG_RAW_INFO(Pgga_format, (uint32_t) FunctionName);
//       NRF_LOG_FLUSH();
}
#endif //GO_AHEAD_DEBUG


#if APP_ENABLED

/////////////////////////////////////////////////////////////////////////////////////////////
//
// FreeRTOS task to monitor and control some of the other tasks, such as light/sound
//
//	This task performs a number of operations that must be delayed until other tasks
//	are active.  
/////////////////////////////////////////////////////////////////////////////////////////////

static void AppTask( void *pvParameters )
{
	( void ) pvParameters;
	uint8_t  *data;
	uint16_t data_len;
#if LSM6DS3_ENABLED
	float x,y,z;
#if APP_SPP_DEBUG_OUTPUT_ENABLED
	uint8_t  lsm_output[64];
#endif // APP_SPP_DEBUG_OUTPUT_ENABLED
#endif // LSM6DS3_ENABLED

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entering ..\r\n", __func__);
#endif
	
#if TWI0_ENABLED /* || NRF_MODULE_ENABLED(TWI1) */
#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Waiting for I2C Master task started ..\r\n", (uint32_t)__func__);
#endif
	// Wait until the I2C task will be ready ..
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
//#endif
#endif // NRF_MODULE_ENABLED(TWI0) /* || NRF_MODULE_ENABLED(TWI1) */

#if GO_AHEAD_DEBUG_ENABLED
	PrintGotGoAhead((uint32_t)__func__);
#endif
#if LSM6DS3_ENABLED
	if (check_lsm6ds3() == NRF_SUCCESS)
	{
		bLSM6DS3_Ready = true;
	}
	else{
		NRF_LOG_RAW_INFO("%s: Can't access to the LSM3DS3, disabled\r\n", __func__);
#if LOG_TASK_NOTIFICATION_ENABLED
		nrf_log_notify();
#endif	
		bLSM6DS3_Ready = false;

	}
	NRF_LOG_RAW_INFO("%s lsm6ds@0x%x\r\n", (uint32_t)__func__, (uint32_t)m_lsm6ds3_task);
	if (!Wait4ReleaseDependedTask(&m_lsm6ds3_task)) {
		NRF_LOG_RAW_INFO("%s : m_lsm6ds3_task not started\r\n", (uint32_t)__func__);	
	}
#endif // LSM6DS3_ENABLED
	
	pMasterAppMsg = &mAppMsg;

	while(true)
	{
		if( xQueueReceive( xAppInQueue, pMasterAppMsg, portMAX_DELAY) == pdPASS)
		{
			// NRF_LOG_RAW_INFO("%s: xQueueReceive - req=%d\r\n", (uint32_t*)__func__, pMasterAppMsg->req);

			switch(pMasterAppMsg->req) {
				case MUSHIN_APP_REQ_TICK:	
					// Skip messages with previous tick ..
					if (uiGlobalTick == pMasterAppMsg->d.tick)
					{
						handler->timestamp = pMasterAppMsg->d.tick;
#if APP_DEBUG_ENABLED
 // 						NRF_LOG_RAW_INFO("%s: t=%d\r\n", (uint32_t*)__func__, handler->timestamp);
#if LOG_TASK_NOTIFICATION_ENABLED
						nrf_log_notify();
#endif // LOG_TASK_NOTIFICATION_ENABLED
#endif // APP_DEBUG_ENABLED
#if LSM6DS3_ENABLED
// [ADK] Debugging ..
						if(bLSM6DS3_Ready)
						{
							LSM_RxGyro(&x, &y, &z);
#if APP_SPP_DEBUG_OUTPUT_ENABLED
							sprintf((char *)lsm_output, MUSHIN_LOG_FLOAT_MARKER", "MUSHIN_LOG_FLOAT_MARKER", "MUSHIN_LOG_FLOAT_MARKER"", 
							MUSHIN_SPRINTF_FLOAT(x), MUSHIN_SPRINTF_FLOAT(y), MUSHIN_SPRINTF_FLOAT(z));
      							NRF_LOG_RAW_INFO("\tG:[%s]\r\n", (uint32_t)lsm_output);
#endif //APP_SPP_DEBUG_OUTPUT_ENABLED
						}
#endif // LSM6DS3_ENABLED
					}
					break;
				case MUSHIN_APP_REQ_CONNECT_DONE:
#if APP_DEBUG_ENABLED
  						NRF_LOG_RAW_INFO("%s: connected to the ESP32 ..\r\n", (uint32_t*)__func__);
#if LOG_TASK_NOTIFICATION_ENABLED
						nrf_log_notify();
#endif // LOG_TASK_NOTIFICATION_ENABLED
#endif // APP_DEBUG_ENABLED
						handler->state = MUSHIN_APP_CONNECTED;
						data = (uint8_t *)"Sensor connected";
						data_len = strlen((const char *)data);
						spp_send_data(data, data_len);
					break;
				case MUSHIN_APP_REQ_DISCONNECT_DONE:
/*
#if APP_DEBUG_ENABLED
  						NRF_LOG_RAW_INFO("%s: disconnected from the ESP32 ..\r\n", (uint32_t*)__func__);
#if LOG_TASK_NOTIFICATION_ENABLED
						nrf_log_notify();
#endif // LOG_TASK_NOTIFICATION_ENABLED
#endif // APP_DEBUG_ENABLED
						handler->state = MUSHIN_APP_IDLE;
*/						
					break;
				case MUSHIN_APP_HANDLE_CMD_REQ:
					cmd_parser();
					break;
				case MUSHIN_APP_XL_REQ:
					LSM_RxXL(&fXL_x, &fXL_y, &fXL_z);
					break;
				case MUSHIN_APP_GYRO_REQ:
					LSM_RxGyro(&fGY_x, &fGY_y, &fGY_z);
					break;
				default:
					break;
			}
		}

	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
//
// API for the application task
//
//	
//	  
/////////////////////////////////////////////////////////////////////////////////////////////
portBASE_TYPE bAppMsgPut(pAppMsg msg)
{
	if(xQueueSend(xAppInQueue, msg, ( portTickType ) 10) != pdPASS)
		{
			NRF_LOG_RAW_INFO("%s()/%d \r\n", (uint32_t *)__func__, __LINE__);
			APP_ERROR_CHECK(NRF_ERROR_TIMEOUT);
		}
	return pdPASS;
}

void vAppTick(void) 
{
 	app_msg_t  _mAppMsg;

	if (m_app_task == NULL ) return;
	_mAppMsg.req = MUSHIN_APP_REQ_TICK;
	_mAppMsg.d.tick = uiGlobalTick;
	_mAppMsg.app__doneCallback = NULL;
	bAppMsgPut(&_mAppMsg);
}

void vAppGotSppConnection(void)
{
 	app_msg_t  _mAppMsg;

	if (m_app_task == NULL ) return;
	_mAppMsg.req = MUSHIN_APP_REQ_CONNECT_DONE;
	_mAppMsg.app__doneCallback = NULL;
	bAppMsgPut(&_mAppMsg);
}

void	vAppSppDisconnect(void)
{
 	app_msg_t  _mAppMsg;

	if (m_app_task == NULL ) return;
	_mAppMsg.req = MUSHIN_APP_REQ_DISCONNECT_DONE;
	_mAppMsg.app__doneCallback = NULL;
	bAppMsgPut(&_mAppMsg);
}

#endif // APP_ENABLED

#if APP_RINGBUFFER_ENABLED
void vSamplesStart( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
       NRF_LOG_FLUSH();
#endif

	xReturned = xTaskCreate(	SamplesTask,				/* The task that implements the audio control. */
			"SaT",						/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
			usStackSize,					/* The size of the stack allocated to the task. */
			NULL,						/* The parameter is not used, so NULL is passed. */
			uxPriority,					/* The priority allocated to the task. */
			&m_sa_task 
		);		
	
	if (xReturned != pdPASS)
	{
		NRF_LOG_ERROR("Samples Task task not created.");
		APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
	}
}

/* Convert float to char */
static char *app_fcvt(float f, char *buf)
{
	uint8_t idx =0;
	int32_t  w = (int32_t)f;
	int32_t  d = (f > 0.0)? ((int32_t)((f - w)*10000.0)):((int32_t)((w - f)*10000.0));

	if (f < 0.0) {
		buf[ idx++] = '-';
	}
	itoa(w, &buf[ idx], 10);
	strcat(buf, ".");
	idx = strlen(buf);
	itoa(d, &buf[ idx], 10);

	return buf;
}


//static uint8_t raw_data[16];
static uint16_t cntL =0, cntR =0;

static void SamplesTask( void *pvParameters )
{
	( void ) pvParameters;
	uint16_t  s0, s1, s2, s3;
	void *rbuf;
	// void *rout;
	float adc_volts0;
	uint8_t adc_volts_sign0;
	float adc_volts0_prev = 0.0;
	float df1_current;
	float	df1_prev = 0.0;
	uint8_t adc_volts_sign2;
	float	 adc_volts2;
	float adc_volts2_prev = 0.0;
	float	 adc_volts1;
	float adc_volts1_prev = 0.0;
	float	df2_prev = 0.0;
	float	df2_current;
//	uint32_t  prev_tick = 0;

	uint8_t skip_cnt = 16;
	int16_t s_rb_as = 0;
	

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entering ..\r\n", __func__);
#endif
	rbuf = get_samples_rb_ctrl();
	// rout = get_out_rb_ctrl();
	DF1Filter_init(&DF1);
	DF1Filter_init(&DF2);
	
	NRF_LOG_RAW_INFO("Print/Send Delay = %d Hz\n", 1000/PRINT_SEND_DELAY_IN_MSEC);
	NRF_LOG_RAW_INFO("Tick Timer Frequency = %d Hz\n", 1000 /APP_TICK_INTERVAL);
	NRF_LOG_RAW_INFO("Data Acquisition Frequency = %d Hz\n", 1000 /ADC_SAMPLING_TIME_IN_MSEC);
	NRF_LOG_RAW_INFO("Raw Data Threshold = "MUSHIN_LOG_FLOAT_MARKER"\n", MUSHIN_LOG_FLOAT(CrankDataRawThreshold));
	NRF_LOG_RAW_INFO("Filtered Data Threshold = "MUSHIN_LOG_FLOAT_MARKER"\n", MUSHIN_LOG_FLOAT(CrankDataFilteredThreshold));
		
	while(true)
	{
		s_rb_as = samples_rb_available_size(rbuf);
		if (s_rb_as)
		{
			samples_rb_read(rbuf, &s0, &s1, &s2, &s3);
//		NRF_LOG_RAW_INFO("[%d,%d], [%d,%d]\r\n", s0, s1, s2, s3);

				adc_volts0 = (s0/4096.0)*3.6;  // Gain = 1/6
				adc_volts1 = (s1/4096.0)*3.6;  // Gain = 1/6
				adc_volts2 = (s2 /4096.0)*3.6; // Gain = 1/6

			if ((uiCrankOutControl&CRANK_DIGIFI_FLAG) == CRANK_DIGIFI_FLAG) 
			{
				DF1Filter_put(&DF1, adc_volts0);
				DF1Filter_put(&DF2, adc_volts2);
			
				if ((adc_volts0 - adc_volts0_prev) > CrankDataRawThreshold || (adc_volts0 - adc_volts0_prev) < (CrankDataRawThreshold*(-1.0))) 
				{
					adc_volts0_prev = adc_volts0;
					df1_current = DF1Filter_get(&DF1);
					if ((df1_current - df1_prev) > CrankDataFilteredThreshold || (df1_current - df1_prev) < (CrankDataFilteredThreshold*(-1.0))) 
					{ 
						df1_prev = df1_current;
					
						if (skip_cnt == 0)
						{
							if ((uiCrankOutControl&CRANK_LEFT_OUT_FLAG) == CRANK_LEFT_OUT_FLAG) 
							{
								if (	(uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG
//									&& (uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == 0
								) {
									send_value_from_sensor(uiGlobalTick, "L", df1_current);
									cntL++;
									}
								}
							}else
							skip_cnt--;
						}
					}

					if ((adc_volts2 - adc_volts2_prev) > CrankDataRawThreshold || (adc_volts2 - adc_volts2_prev) < (CrankDataRawThreshold*(-1.0))) 
					{
						adc_volts2_prev = adc_volts2;
						df2_current = DF1Filter_get(&DF2);
						if ((df2_current - df2_prev) > CrankDataFilteredThreshold || (df2_current - df2_prev) < (CrankDataFilteredThreshold*(-1.0)))
						{ 
							df2_prev = df2_current;
							if (skip_cnt == 0) {
								if ((uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == CRANK_RIGHT_OUT_FLAG) 
								{
									if ((uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG
										) {
									send_value_from_sensor(uiGlobalTick, "R", df2_current);
									cntR++;
									}
								}
							}else
								skip_cnt--;
						}
					}
				}
				else  // Digital Filtering disabled ..
				{
					if ((adc_volts0 - adc_volts0_prev) > CrankDataRawThreshold || (adc_volts0 - adc_volts0_prev) < (CrankDataRawThreshold*(-1.0))) 
					{
						adc_volts0_prev = adc_volts0;
						if (skip_cnt == 0)
						{
							if ((uiCrankOutControl&CRANK_LEFT_OUT_FLAG) == CRANK_LEFT_OUT_FLAG) 
							{
								if (	(uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG
//									&& 	(uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == 0
								) {
									send_value_from_sensor(uiGlobalTick, "L", adc_volts0);
									cntL++;
								}
							}
						}else
							skip_cnt--;
					}
					if ((adc_volts2 - adc_volts2_prev) > CrankDataRawThreshold || (adc_volts2 - adc_volts2_prev) < (CrankDataRawThreshold*(-1.0))) 
					{
						adc_volts2_prev = adc_volts2;
						if (skip_cnt == 0) {
							if ((uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == CRANK_RIGHT_OUT_FLAG) 
							{
								if ((uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG
								) {
									send_value_from_sensor(uiGlobalTick, "R", adc_volts2);
									cntR++;
								}
							}
						}else
							skip_cnt--;
					}
				}
				if ((uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG
				) {
					send_value_from_sensor(uiGlobalTick, "V", adc_volts1);
				}
			}else
				vTaskDelay((TickType_t)(pdMS_TO_TICKS(PRINT_SEND_DELAY_IN_MSEC)));
		}
}


void out_dbg_cnts(void)
{
	NRF_LOG_RAW_INFO("Got L/R=%d/%d\r\n", cntL, cntR);
	cntL = cntR = 0;
}
#endif //APP_RINGBUFFER_ENABLED
