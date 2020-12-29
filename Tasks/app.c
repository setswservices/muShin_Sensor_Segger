
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
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
TaskHandle_t		m_sa_task = NULL;
static void SamplesTask( void *pvParameters );
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#if MUSH_FEATURE_ENABLED(FREERTOS_TASK_STATS)
static char pcWriteBuffer[2048];
#endif // MUSH_FEATURE_ENABLED(FREERTOS_TASK_STATS)

#if MUSH_FEATURE_ENABLED(APP)
static void AppTask( void *pvParameters );
static app_handle_t app_state = {
	.state = MUSHIN_APP_INITED,
	.timestamp = 0	
};
static p_app_handle handler = &app_state;

static app_msg_t  mAppMsg;
static pAppMsg   pMasterAppMsg;

#if MUSH_FEATURE_ENABLED(APP_DIGITAL_FILTER)
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

#if MUSH_FEATURE_ENABLED(TASK_START_DEBUG)
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
       NRF_LOG_FLUSH();
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
#endif // MUSH_FEATURE_ENABLED(APP)

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

#if MUSH_FEATURE_ENABLED(GO_AHEAD_DEBUG)
static char *Pgga_format = "%s Got 'Go-Ahead' signal ..\r\n";

void PrintGotGoAhead(uint32_t FunctionName)
{
	NRF_LOG_RAW_INFO(Pgga_format, (uint32_t) FunctionName);
//       NRF_LOG_FLUSH();
}
#endif //GO_AHEAD_DEBUG


#if MUSH_FEATURE_ENABLED(APP)

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
#if MUSH_FEATURE_ENABLED(LSM6DS3)
	float x,y,z;
#if MUSH_FEATURE_ENABLED(APP_SPP_DEBUG_OUTPUT)
	uint8_t  lsm_output[64];
#endif // MUSH_FEATURE_ENABLED(APP_SPP_DEBUG_OUTPUT)
#endif // MUSH_FEATURE_ENABLED(LSM6DS3)

#if MUSH_FEATURE_ENABLED(TASK_START_DEBUG)
	NRF_LOG_RAW_INFO("%s Entering ..\r\n", __func__);
#endif
	
#if MUSH_FEATURE_ENABLED(TWI0) /* || NRF_MODULE_ENABLED(TWI1) */
#if MUSH_FEATURE_ENABLED(TASK_START_DEBUG)
	NRF_LOG_RAW_INFO("%s Waiting for I2C Master task started ..\r\n", (uint32_t)__func__);
#endif
	// Wait until the I2C task will be ready ..
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
//#endif
#endif // NRF_MODULE_ENABLED(TWI0) /* || NRF_MODULE_ENABLED(TWI1) */

#if MUSH_FEATURE_ENABLED(GO_AHEAD_DEBUG)
	PrintGotGoAhead((uint32_t)__func__);
#endif
#if MUSH_FEATURE_ENABLED(LSM6DS3)
	if (check_lsm6ds3() == NRF_SUCCESS)
	{
		bLSM6DS3_Ready = true;
	}
	else{
		NRF_LOG_RAW_INFO("%s: Can't access to the LSM3DS3, disabled\r\n", __func__);
#if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
		nrf_log_notify();
#endif	
		bLSM6DS3_Ready = false;

	}
	NRF_LOG_RAW_INFO("%s lsm6ds@0x%x\r\n", (uint32_t)__func__, (uint32_t)m_lsm6ds3_task);
	if (!Wait4ReleaseDependedTask(&m_lsm6ds3_task)) {
		NRF_LOG_RAW_INFO("%s : m_lsm6ds3_task not started\r\n", (uint32_t)__func__);	
	}
#endif // MUSH_FEATURE_ENABLED(LSM6DS3)
	
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
#if MUSH_FEATURE_ENABLED(APP_DEBUG)
 // 						NRF_LOG_RAW_INFO("%s: t=%d\r\n", (uint32_t*)__func__, handler->timestamp);
#if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
						nrf_log_notify();
#endif // MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
#endif // MUSH_FEATURE_ENABLED(APP_DEBUG)
#if MUSH_FEATURE_ENABLED(LSM6DS3)
// [ADK] Debugging ..
						if(bLSM6DS3_Ready)
						{
							LSM_RxGyro(&x, &y, &z);
#if MUSH_FEATURE_ENABLED(APP_SPP_DEBUG_OUTPUT)
							sprintf((char *)lsm_output, MUSHIN_LOG_FLOAT_MARKER", "MUSHIN_LOG_FLOAT_MARKER", "MUSHIN_LOG_FLOAT_MARKER"", 
							MUSHIN_SPRINTF_FLOAT(x), MUSHIN_SPRINTF_FLOAT(y), MUSHIN_SPRINTF_FLOAT(z));
      							NRF_LOG_RAW_INFO("\tG:[%s]\r\n", (uint32_t)lsm_output);
#endif //MUSH_FEATURE_ENABLED(APP_SPP_DEBUG_OUTPUT)
						}
#endif // MUSH_FEATURE_ENABLED(LSM6DS3)
					}
					break;
				case MUSHIN_APP_REQ_CONNECT_DONE:
#if MUSH_FEATURE_ENABLED(APP_DEBUG)
  						NRF_LOG_RAW_INFO("%s: connected to the ESP32 ..\r\n", (uint32_t*)__func__);
#if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
						nrf_log_notify();
#endif // MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
#endif // MUSH_FEATURE_ENABLED(APP_DEBUG)
						handler->state = MUSHIN_APP_CONNECTED;
						data = (uint8_t *)"Sensor connected";
						data_len = strlen((const char *)data);
						spp_send_data(data, data_len);
					break;
				case MUSHIN_APP_REQ_DISCONNECT_DONE:
/*
#if MUSH_FEATURE_ENABLED(APP_DEBUG)
  						NRF_LOG_RAW_INFO("%s: disconnected from the ESP32 ..\r\n", (uint32_t*)__func__);
#if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
						nrf_log_notify();
#endif // MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
#endif // MUSH_FEATURE_ENABLED(APP_DEBUG)
						handler->state = MUSHIN_APP_IDLE;
*/						
					break;
				case MUSHIN_APP_HANDLE_CMD_REQ:
					cmd_parser();
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

#endif // MUSH_FEATURE_ENABLED(APP)

#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
void vSamplesStart( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

#if MUSH_FEATURE_ENABLED(TASK_START_DEBUG)
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

void send_value_from_sensor(uint32_t tick, char *name, float value) 
{
	uint8_t str[64];
	char str_value[16];

	sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(value));
	sprintf((char *)str,"%lu", tick);

	strcat((char *)str, ",");
	strcat((char *)str, name);
	strcat((char *)str, " ");
	strcat((char *)str, str_value);

	NRF_LOG_RAW_INFO("[%s]\n", (uint32_t *)str);

	if (uiGlobalSysFlags == GLOBAL_STOP_FLAG) return;
	spp_send_data(str, strlen((char *)str));
}


static uint8_t raw_data[16];
static uint16_t cntL =0, cntR =0;

static void SamplesTask( void *pvParameters )
{
	( void ) pvParameters;
	uint16_t  s0, s1, s2, s3;
	void *rbuf;
	// void *rout;
	float adc_volts01;
	uint8_t adc_volts_sign01;
	float adc_volts01_prev = 0.0;
	float df1_current;
//	float	df1_prev = 0.0;
	uint8_t adc_volts_sign23;
	float	 adc_volts23;
	float adc_volts23_prev = 0.0;
	float	df2_prev = 0.0;
	float	df2_current;
//	uint32_t  prev_tick = 0;

	uint8_t skip_cnt = 16;
	int16_t s_rb_as = 0;
	

#if MUSH_FEATURE_ENABLED(TASK_START_DEBUG)
	NRF_LOG_RAW_INFO("%s Entering ..\r\n", __func__);
#endif
	rbuf = get_samples_rb_ctrl();
	// rout = get_out_rb_ctrl();
	DF1Filter_init(&DF1);
	DF1Filter_init(&DF2);
	
	NRF_LOG_RAW_INFO("Print/Send Delay = %d Hz\r\n", 1000/PRINT_SEND_DELAY_IN_MSEC);
	NRF_LOG_RAW_INFO("Tick Timer Frequency = %d Hz\r\n", 1000 /APP_TICK_INTERVAL);
	NRF_LOG_RAW_INFO("Data Acquisition Frequency = %d Hz\r\n", 1000 /ADC_SAMPLING_TIME_IN_MSEC);
		
	while(true)
	{
		s_rb_as = samples_rb_available_size(rbuf);
		if (s_rb_as)
		{
			samples_rb_read(rbuf, &s0, &s1, &s2, &s3);
//		NRF_LOG_RAW_INFO("[%d,%d], [%d,%d]\r\n", s0, s1, s2, s3);
		// Calculate AIN01 diff

			if ( s0 > s1 ) {
//				adc_volts01 = ((s0 - s1)/4096.0)*2.4; adc_volts_sign01 = 0;   // Gain = 1/4
				adc_volts01 = ((s0 - s1)/4096.0)*3.6; adc_volts_sign01 = 0;   // Gain = 1/6
// 				NRF_LOG_RAW_INFO(">>"MUSHIN_LOG_FLOAT_MARKER"\r\n", MUSHIN_LOG_FLOAT(adc_volts01));
			}else
			if ( s0 < s1 ) {
//				adc_volts01 = ((s1 - s0)/4096.0)*2.4; adc_volts_sign01 = 1;   // Gain = 1/4
				adc_volts01 = ((s1 - s0)/4096.0)*3.6; adc_volts_sign01 = 1;   // Gain = 1/6
//				NRF_LOG_RAW_INFO("<<"MUSHIN_LOG_FLOAT_MARKER"\r\n", MUSHIN_LOG_FLOAT(adc_volts01));
			}else{
				adc_volts01 = 0.0; adc_volts_sign01 = 0;
 //				NRF_LOG_RAW_INFO("=="MUSHIN_LOG_FLOAT_MARKER"\r\n", MUSHIN_LOG_FLOAT(adc_volts01));
			}
		// Calculate AIN23 diff

			if ( s2 > s3 ) {
//				adc_volts23 = ((s2 - s3)/4096.0)*2.4; adc_volts_sign23 = 0;   // Gain = 1/4
				adc_volts23 = ((s2 - s3)/4096.0)*3.6; adc_volts_sign23 = 0;   // Gain = 1/6
// 				NRF_LOG_RAW_INFO(">>"MUSHIN_LOG_FLOAT_MARKER"\r\n", MUSHIN_LOG_FLOAT(adc_volts23));
			}else
			if ( s2 < s3 ) {
//				adc_volts23 = ((s3 - s2)/4096.0)*2.4; adc_volts_sign23 = 1;   // Gain = 1/4
				adc_volts23 = ((s3 - s2)/4096.0)*3.6; adc_volts_sign23 = 1;   // Gain = 1/6
//				NRF_LOG_RAW_INFO("<<"MUSHIN_LOG_FLOAT_MARKER"\r\n", MUSHIN_LOG_FLOAT(adc_volts23));
			}else{
				adc_volts23 = 0.0; adc_volts_sign23 = 0;
 //				NRF_LOG_RAW_INFO("=="MUSHIN_LOG_FLOAT_MARKER"\r\n", MUSHIN_LOG_FLOAT(adc_volts23));
			}

			DF1Filter_put(&DF1, ((adc_volts_sign01)?(adc_volts01*(-1.0)) : adc_volts01));
			DF1Filter_put(&DF2, ((adc_volts_sign23)?(adc_volts23*(-1.0)) : adc_volts23));

			
			if ((adc_volts01 - adc_volts01_prev) > 0.001 || (adc_volts01 - adc_volts01_prev) < (-0.001)) 
			{
				adc_volts01_prev = adc_volts01;
				// df1_current = DF1Filter_get(&DF1);
				{ 
					
					if (skip_cnt == 0)
					{
						if ((uiCrankOutControl&CRANK_LEFT_OUT_FLAG) == CRANK_LEFT_OUT_FLAG) 
						{
							if (/* handler->state == MUSHIN_APP_CONNECTED  */
								(uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG 
								//&& 	(uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == 0
							) {
								send_value_from_sensor(uiGlobalTick, "L", adc_volts01); //df2_current);
								cntL++;
							}
						}
					}else
							skip_cnt--;
				}
			}

#ifdef DISPLAY_VOLT_DIFFS

char s_adc_volts23[16];
char s_adc_volts23_prev[16];

sprintf((char *)s_adc_volts23, MUSHIN_LOG_FLOAT_MARKER",", MUSHIN_SPRINTF_FLOAT(adc_volts23));
sprintf((char *)s_adc_volts23_prev, MUSHIN_LOG_FLOAT_MARKER",", MUSHIN_SPRINTF_FLOAT(adc_volts23_prev));
NRF_LOG_RAW_INFO("%s vs %s\r\n", (uint32_t)s_adc_volts23, (uint32_t)s_adc_volts23_prev);
#endif


			if ((adc_volts23 - adc_volts23_prev) > 0.001 || (adc_volts23 - adc_volts23_prev) < (-0.001)) 
			{
				adc_volts23_prev = adc_volts23;
				// df2_current = DF1Filter_get(&DF2);

#ifdef DISPLAY_FILTER_DIFFS
char s_df2_current[16];
char s_df2_prev[16];

sprintf((char *)s_df2_current, MUSHIN_LOG_FLOAT_MARKER",", MUSHIN_SPRINTF_FLOAT(df2_current));
sprintf((char *)s_df2_prev, MUSHIN_LOG_FLOAT_MARKER",", MUSHIN_SPRINTF_FLOAT(df2_prev));
NRF_LOG_RAW_INFO("%s vs %s\r\n", (uint32_t)s_df2_current, (uint32_t)s_df2_prev);

#endif

			{ 
					if (skip_cnt == 0) {
						if ((uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == CRANK_RIGHT_OUT_FLAG) 
						{
							if (/* handler->state == MUSHIN_APP_CONNECTED */
								(uiGlobalSysFlags&GLOBAL_START_FLAG) == GLOBAL_START_FLAG
							) 
							{
								send_value_from_sensor(uiGlobalTick, "R", adc_volts23); //df2_current);
								cntR++;

							}
						}
					}else
						skip_cnt--;
				}
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
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)

// =======================================================================================
#include "app_error.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "app_util_platform.h"
#include "nrf_strerror.h"

#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT
#include "nrf_sdm.h"
#endif

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
	
	if (id == 1) return;  // JLB for debugging
	
	__disable_irq();
	NRF_LOG_FINAL_FLUSH();

#ifndef DEBUG
	NRF_LOG_ERROR("Fatal error");
#else
	switch (id)
	{
#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT
	case NRF_FAULT_ID_SD_ASSERT:
		NRF_LOG_ERROR("SOFTDEVICE: ASSERTION FAILED");
		break;
	case NRF_FAULT_ID_APP_MEMACC:
		NRF_LOG_ERROR("SOFTDEVICE: INVALID MEMORY ACCESS");
		break;
#endif
	case NRF_FAULT_ID_SDK_ASSERT:
		{
			assert_info_t * p_info = (assert_info_t *)info;
			NRF_LOG_ERROR("ASSERTION FAILED at %s:%u",
				p_info->p_file_name,
				p_info->line_num);
			break;
		}
	case NRF_FAULT_ID_SDK_ERROR:
		{
			error_info_t * p_info = (error_info_t *)info;
			NRF_LOG_ERROR("ERROR %u [%s] at %s:%u\r\nPC at: 0x%08x",
				p_info->err_code,
				nrf_strerror_get(p_info->err_code),
				p_info->p_file_name,
				p_info->line_num,
				pc);
			NRF_LOG_ERROR("End of error report");
			break;
		}
	default:
		NRF_LOG_ERROR("UNKNOWN FAULT at 0x%08X", pc);
		break;
	}
#endif

	NRF_BREAKPOINT_COND;
	// On assert, the system can only recover with a reset.

#ifndef DEBUG
	    NRF_LOG_WARNING("System reset");
	NVIC_SystemReset();
#else
	app_error_save_and_stop(id, pc, info);
#endif // DEBUG
}