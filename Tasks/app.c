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
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"
#include "event_groups.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mushin_config.h"
#include "app.h"

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

#if APP_ENABLED
TaskHandle_t		m_app_task = NULL;
static void AppTask( void *pvParameters );
static app_handle_t app_state = {
	.state = MUSHIN_APP_INITED,
	.timestamp = 0	
};
static p_app_handle handler = &app_state;

static app_msg_t  mAppMsg;
static pAppMsg   pMasterAppMsg;

bool Wait4ReleaseDependedTask(TaskHandle_t  *taskHandle)
{
	static uint16_t nIdx;
	
	for(nIdx = 0 ; nIdx < 10 ; nIdx ++)
	{
		if(*taskHandle == NULL)
                  vTaskDelay((TickType_t)(pdMS_TO_TICKS(100))); 
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

void PrintGotGoAhead(uint32_t *FunctionName)
{
	NRF_LOG_RAW_INFO(Pgga_format, FunctionName);
       NRF_LOG_FLUSH();
}
#endif //GO_AHEAD_DEBUG


/////////////////////////////////////////////////////////////////////////////////////////////
// Creates and starts FreeRTOS app task
/////////////////////////////////////////////////////////////////////////////////////////////
static BaseType_t xReturned = 0;
  
void vAppStart( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
//	NRF_LOG_FLUSH();
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
	pMasterAppMsg = &mAppMsg;
#if LSM6DS3_ENABLED
	float x,y,z;
#if APP_SPP_DEBUG_OUTPUT_ENABLED
	uint8_t  lsm_output[64];
#endif // APP_SPP_DEBUG_OUTPUT_ENABLED
#endif // LSM6DS3_ENABLED
#if TWI0_ENABLED /* || NRF_MODULE_ENABLED(TWI1) */
#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Waiting for I2C Master task started ..\r\n", (uint32_t)__func__);
#endif
	// Wait until the I2C task will be ready ..
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
//#endif
#endif // NRF_MODULE_ENABLED(TWI0) /* || NRF_MODULE_ENABLED(TWI1) */

#if GO_AHEAD_DEBUG_ENABLED
	PrintGotGoAhead((uint32_t *)__func__);
#endif
#if LSM6DS3_ENABLED
	if (check_lsm6ds3() == NRF_SUCCESS)
	{
		bLSM6DS3_Ready = true;
	}
	else{
		NRF_LOG_RAW_INFO("%s: Can't access to the LSM3DS3, disabled\r\n", __func__);
		bLSM6DS3_Ready = false;

	}
	NRF_LOG_RAW_INFO("%s lsm6ds@0x%x\r\n", (uint32_t *)__func__, (uint32_t)m_lsm6ds3_task);
	if (!Wait4ReleaseDependedTask(&m_lsm6ds3_task)) {
		NRF_LOG_RAW_INFO("%s : m_lsm6ds3_task not started\r\n", (uint32_t *)__func__);	
	}
#endif // LSM6DS3_ENABLED

	while(true)
	{
		if( xQueueReceive( xAppInQueue, pMasterAppMsg, portMAX_DELAY) == pdPASS)
		{
//			NRF_LOG_RAW_INFO("%s: xQueueReceive - req=%d\r\n", (uint32_t*)__func__, pMasterAppMsg->req);

			switch(pMasterAppMsg->req) {
				case MUSHIN_APP_HANDLE_CMD_REQ:
					cmd_parser(uiGlobalCmdLine);
					break;
				case MUSHIN_APP_HANDLE_START_CMD_REQ:
again1:					
					switch (uiGlobalEmuTaskCntrl) {
						case MUSHIN_TASK_MAY_ACTIVATE:
							if (m_emu_task != NULL)
							{
								if (Wait4ReleaseDependedTask(&m_emu_task))
									NRF_LOG_RAW_INFO("%s: Emulation released\r\n", (uint32_t*)__func__);
							}
							uiGlobalEmuTaskCntrl = MUSHIN_TASK_ACTIVE;
							break;
						case MUSHIN_TASK_NEED_SUSPEND:  
							if (m_emu_task != NULL)
							{
								vTaskSuspend(m_emu_task);
							}	
							uiGlobalEmuTaskCntrl = MUSHIN_TASK_SUSPENDED;
							goto again1;
							break;
						case  MUSHIN_TASK_NOT_ACTIVE:
						case  MUSHIN_TASK_SUSPENDED:
							if (m_data_task != NULL)
							{
								if (Wait4ReleaseDependedTask(&m_data_task))
									NRF_LOG_RAW_INFO("%s: Data released\r\n", (uint32_t*)__func__);
							}
							uiGlobalDataTaskCntrl = MUSHIN_TASK_ACTIVE;
							break;
						default:	
							NRF_LOG_RAW_INFO("%s: Emulation Task state %d not handled\r\n", (uint32_t*)__func__, uiGlobalEmuTaskCntrl);
					}	
					break;
				case MUSHIN_APP_XL_REQ:
					LSM_RxXL(&fXL_x, &fXL_y, &fXL_z);
					break;
				case MUSHIN_APP_GYRO_REQ:
					LSM_RxGyro(&fGY_x, &fGY_y, &fGY_z);
					break;
				case MUSHIN_APP_REQ_DISCONNECT_DONE:
					uiGlobalSysFlags = 0;
					uiGlobalSysFlags = GLOBAL_STOP_FLAG;
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

void vAppCmdStart(void) 
{
 	app_msg_t  _mAppMsg;

	if (m_app_task == NULL ) return;
	_mAppMsg.req = MUSHIN_APP_HANDLE_START_CMD_REQ;
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

