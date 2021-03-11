
/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		I2C.c
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, muShin
//
//	Purpose/Functionality:	I2C Master Task
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
#include "nrf_delay.h"

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
#include "nrf_drv_twi.h"

//#define NRF_LOG_MODULE_NAME I2c
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mushin_config.h"
#include "i2c.h"
#include "app.h"

#if TWI0_ENABLED

/////////////////////////////////////////////////////////////////////////////////////////////
//  Defines                                                                                                                                               //
/////////////////////////////////////////////////////////////////////////////////////////////

#define TWI0_QUEUE_LENGTH				( 2 )
/** Delay between two continue I2C read or write operation in microsecond */
#define TWI0_CONTINUE_RW_DELAY  50
#define 	I2C_DEBUG_ENABLED		0

#define I2C_WAIT_TIME	1
#define I2C_WAIT_MAX	100

static xQueueHandle			xTWI0_MasterInQueue = NULL;
static xQueueHandle 			xTWI0_MasterOutQueue = NULL;
static xSemaphoreHandle 		xTWI0_Mutex = NULL;
static TaskHandle_t                 	TWI0_i2c_task = NULL;  //!< Reference to the I2C FreeRTOS task.

static pTwiMessage master_pTWI0_Msg = NULL;   // current transaction ..
static unsigned char TWI0_init = 0;

#if APP_USE_DK_BOARD
static nrf_drv_twi_t              m_TWI0_instance   = NRF_DRV_TWI_INSTANCE(TWI0_INSTANCE_ID);
#else
static nrf_drv_twi_t              m_TWI0_instance   = NRF_DRV_TWI_INSTANCE(TWI0_INSTANCE_ID);
#endif
static void TWI0_event_handler(nrf_drv_twi_evt_t const * p_event, void * p_context);

static BaseType_t xReturned =0;
/* Indicates if operation on TWI has ended before Free-RTOS is activated */
static volatile bool m_xfer_done = false;
bool volatile bI2C_Task_Started = false;
static TwiMessage_t			twi_msg;
static nrf_drv_twi_xfer_desc_t 	*ptwi_xfer = &twi_msg.twi_xfer;


//////////////////////////////////////////////////////////////////////////////////////////////////////
// TWI/I2C Channel 0
//////////////////////////////////////////////////////////////////////////////////////////////////////

static void prvTWI0_MasterTask( void *pvParameters );

/////////////////////////////////////////////////////////////////////////////////////////////
// Creates and starts I2C service task
/////////////////////////////////////////////////////////////////////////////////////////////

void vTWI0_MasterStart( uint16_t usStackSize, portBASE_TYPE uxPriority)
{
	static uint32_t err_code;	
	// Initialize TWI interface 
	nrf_drv_twi_config_t twi_config = 
	{
		.frequency = 			NRF_TWI_FREQ_100K,    // LSM303 use 100kHz I2C interface
		.interrupt_priority = 	APP_IRQ_PRIORITY_HIGH,
		.scl = 				_TWI0_PIN_SCL,
		.sda = 				_TWI0_PIN_SDA
	};

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s()  Started\r\n", (uint32_t)__func__);
#endif

	err_code = nrf_drv_twi_init(&m_TWI0_instance, &twi_config, TWI0_event_handler, 0);

	if (err_code != NRF_SUCCESS)
	{
		NRF_LOG_ERROR("I2C-TWI0 not initialized");
		APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
	}

	nrf_drv_twi_enable(&m_TWI0_instance);


	/* Create the queues used by tasks and interrupts to send/receive SPI data. */
	xTWI0_MasterInQueue = xQueueCreate( TWI0_QUEUE_LENGTH, sizeof(TwiMessage_t) );
	xTWI0_MasterOutQueue = xQueueCreate( 1, sizeof(TwiMessage_t) );

	/* If the queue could not be created then don't create any tasks that might
	attempt to use the queue. */
	if( xTWI0_MasterInQueue != NULL && xTWI0_MasterOutQueue != NULL)
	{

		/* Create the semaphore used to access the I2C transmission. */
		xTWI0_Mutex = xSemaphoreCreateMutex();
		configASSERT( xTWI0_Mutex );
		
		/* Create that task that handles the I2C Master itself. */
		xReturned = xTaskCreate(	prvTWI0_MasterTask,			/* The task that implements the command console. */
			"TWI0",						/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
			usStackSize,					/* The size of the stack allocated to the task. */
			NULL,						/* The parameter is not used, so NULL is passed. */
			uxPriority,					/* The priority allocated to the task. */
			&TWI0_i2c_task );			/* A handle is not required, so just pass NULL. */
		
		if (xReturned != pdPASS)
		{
			NRF_LOG_ERROR("I2C-TWI0 task not created.");

			APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
		}
	}
	else
	{
		NRF_LOG_ERROR("I2C-TWI0 queues not created.");
		APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
	}
}

/*-----------------------------------------------------------*/
static void TWI0_event_handler(nrf_drv_twi_evt_t const * p_event, void * p_context)
{    
    static BaseType_t yield_req = pdFALSE;

    switch (p_event->type)
    {
	case NRF_DRV_TWI_EVT_DONE:
		master_pTWI0_Msg->m_twi_transfer_state = TWI_TRANSFER_SUCCESS;
		break;
        
	case NRF_DRV_TWI_EVT_ADDRESS_NACK:
		master_pTWI0_Msg->m_twi_transfer_state = TWI_TRANSFER_FAILED;
		break;
        
	case NRF_DRV_TWI_EVT_DATA_NACK:
		master_pTWI0_Msg->m_twi_transfer_state = TWI_TRANSFER_FAILED;
		break;
    }

    if (!bI2C_Task_Started) {
		m_xfer_done = true;
    }else{
    		vTaskNotifyGiveFromISR(TWI0_i2c_task, &yield_req);

    		/* Switch the task if required. */
    		portYIELD_FROM_ISR(yield_req);
   }
	
}


/*-----------------------------------------------------------*/
static void prvTWI0_MasterTask( void *pvParameters )
{
	static TwiMessage_t _TwiMsg;   // current transaction ..
	static uint32_t                err_code;

	( void ) pvParameters;
	
#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Started ..\r\n", (uint32_t)__func__);
#endif
		master_pTWI0_Msg = &_TwiMsg;

#if APP_ENABLED
	if (!Wait4ReleaseDependedTask(&m_app_task))
		NRF_LOG_RAW_INFO("%s : m_app_task not started\r\n", (uint32_t)__func__);	
#endif // APP_ENABLED

#if 0
// #if LSM6DS3_ENABLED
NRF_LOG_RAW_INFO("%s lsm6ds@0x%x\r\n", (uint32_t)__func__, (uint32_t)m_lsm6ds3_task);
	if (!Wait4ReleaseDependedTask(&m_lsm6ds3_task)) {
		NRF_LOG_RAW_INFO("%s : m_lsm6ds3_task not started\r\n", (uint32_t)__func__);	
	}
#endif

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s All depended tasks released\r\n", (uint32_t)__func__);
#endif
	bI2C_Task_Started = true;

	while(1)
	{
		TWI0_init = 1;
		if( xQueueReceive( xTWI0_MasterInQueue, master_pTWI0_Msg, portMAX_DELAY) == pdPASS)
		{
			err_code = nrf_drv_twi_xfer(&m_TWI0_instance, &master_pTWI0_Msg->twi_xfer, master_pTWI0_Msg->twi_flags);
			
			if (err_code != NRF_SUCCESS)
			{

				NRF_LOG_RAW_INFO("%s:  err_code= %d \r\n", (uint32_t *)__func__,err_code);
				master_pTWI0_Msg->m_twi_transfer_state = TWI_TRANSFER_PENDING;
			}
			else
			{

				(void) ulTaskNotifyTake(pdTRUE,	/* Clear the notification value before exiting (equivalent to the binary semaphore). */
				        		portMAX_DELAY);				/* Block indefinitely (INCLUDE_vTaskSuspend has to be enabled).*/
			}
		}
		if (master_pTWI0_Msg->twi__doneCallback != NULL)
		master_pTWI0_Msg->twi__doneCallback(master_pTWI0_Msg);

	}
}

portBASE_TYPE TWI0_MsgPut(pTwiMessage msg)
{
	if(xQueueSend(xTWI0_MasterInQueue, msg, ( portTickType ) 10) != pdPASS)
		{
			NRF_LOG_RAW_INFO("%s()/%d \r\n", (uint32_t *)__func__, __LINE__);
			APP_ERROR_CHECK(NRF_ERROR_TIMEOUT);
		}
	return pdPASS;
}

portBASE_TYPE TWI0_MsgGet(pTwiMessage msg)
{
	return xQueueReceive(xTWI0_MasterOutQueue, msg, portMAX_DELAY);
}

portBASE_TYPE  TWI0_OnDone(void *pvParameters)
{
	pTwiMessage msg = (pTwiMessage)pvParameters;

	if(xQueueSend(xTWI0_MasterOutQueue, msg, ( portTickType ) (10 )) != pdPASS)
	{
			APP_ERROR_CHECK(NRF_ERROR_TIMEOUT);
	}
		
	return pdPASS;
}	
portBASE_TYPE TWI0_Take(void *pvParameters)
{
	( void ) pvParameters;
	
	if (xTWI0_Mutex == NULL)  return pdFAIL;

        // Obtain the semaphore - don't block even if the semaphore is not
        // available during a time more then a reasonable.
	if( xSemaphoreTake( xTWI0_Mutex, (portMAX_DELAY -10) )  != pdPASS )
	{
		// Never return from  ..
		{
			APP_ERROR_CHECK(NRF_ERROR_MUTEX_LOCK_FAILED);
		}
	}
	return pdPASS;
}
portBASE_TYPE TWI0_Give(void *pvParameters)
{
	( void ) pvParameters;
	
	if (xTWI0_Mutex == NULL)  return pdFAIL;

	if( xSemaphoreGive( xTWI0_Mutex ) != pdPASS )
	{
		// Never return from  ..
		{
			APP_ERROR_CHECK(NRF_ERROR_MUTEX_UNLOCK_FAILED);
		}
	}
	return pdPASS;
}


//////////////////////////////////////////////////////////////////////////////
//
// The Queued_TWI0Register_Register_TxRx() function issues an I2C read operation by creating a message and placing
// it on the TWI queue for processing.  If processing happens withing timeout ticks, 
// the callback/event-handler sends a message that releases the TWI0_MsgGet() call.
//
// The function body is protected by a mutex.
//
//  Parameter(s) are:
//
//	address		the I2C address of the device
//	data			a pointer to the buffer to be sent
//	length		the length of the buffer to be sent
//
//////////////////////////////////////////////////////////////////////////////

static uint8_t Queued_TWI0_Register_TxRx(uint8_t Address, uint8_t Register)
{

	uint8_t Packet[2] = {0,0};
	uint8_t RegValue; 

#if I2C_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s(%d)\r\n", (uint32_t) __func__, Register);
#endif

	Packet[0] = Register;	

	TWI0_Take(NULL);

	twi_msg.twi_xfer.address 					= Address;
	twi_msg.twi_xfer.type 						= NRF_DRV_TWI_XFER_TXRX;
	twi_msg.twi_xfer.primary_length				= 1;
	twi_msg.twi_xfer.p_primary_buf				= Packet;
	twi_msg.twi_xfer.p_secondary_buf			= &RegValue;
	twi_msg.twi_xfer.secondary_length 			= 1;
	twi_msg.twi__doneCallback 				= TWI0_OnDone;
	twi_msg.m_twi_transfer_state 				= TWI_TRANSFER_PENDING;

	TWI0_MsgPut(&twi_msg);

	TWI0_MsgGet(&twi_msg);

	TWI0_Give(NULL);

	return RegValue;

}


//////////////////////////////////////////////////////////////////////////////
//
// The Queued_TWI0_Register_TxRx() function issues an I2C read operation by creating a message and placing
// it on the TWI queue for processing.  If processing happens withing timeout ticks, 
// the callback/event-handler sends a message that releases the TWI0_MsgGet() call.
//
// The function body is protected by a mutex.
//
//  Parameter(s) are:
//
//	address		the I2C address of the device
//	data			a pointer to the buffer to be sent
//	length		the length of the buffer to be sent
//
//////////////////////////////////////////////////////////////////////////////

static uint8_t Polled_TWI0_Register_TxRx(uint8_t Address, uint8_t Register)
{

	uint8_t Packet[2] = {0,0};
	uint8_t RegValue; 

#if I2C_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s(%d)\r\n", (uint32_t) __func__, Register);
#endif

	Packet[0] = Register;	
	
	ptwi_xfer->address				= Address;
	ptwi_xfer->type					= NRF_DRV_TWI_XFER_TXRX;
	ptwi_xfer->primary_length			= 1;
	ptwi_xfer->p_primary_buf			= Packet;
	ptwi_xfer->p_secondary_buf		= &RegValue;
	ptwi_xfer->secondary_length 		= 1;

	m_xfer_done = false;
	ret_code_t err_code = nrf_drv_twi_xfer(&m_TWI0_instance, ptwi_xfer, 0);
	APP_ERROR_CHECK(err_code);

	uint8_t I2C_Wait = I2C_WAIT_MAX;
	while (I2C_Wait--)
	{	
		if (m_xfer_done) break;
		nrf_delay_ms(I2C_WAIT_TIME);
	}
	if (!m_xfer_done) APP_ERROR_CHECK(TWI_TRANSFER_FAILED);

	
	return RegValue;

}


uint8_t TWI0_Register_TxRx(uint8_t Address, uint8_t Register)
{
	uint8_t RtnVal;
	
	if( bI2C_Task_Started)
	{
	// Using I2C Task
		RtnVal = Queued_TWI0_Register_TxRx(Address, Register);
	}
	else
	{
		RtnVal = Polled_TWI0_Register_TxRx(Address, Register);
	}

	return RtnVal;
}

//////////////////////////////////////////////////////////////////////////////
//
// The Queued_TWI0_Tx() function issues an I2C write operation by creating a message and placing
// it on the TWI queue for processing.  If processing happens withing portMAX_DELAY ticks, 
// the callback/event-handler sends a message that releases the TWI0_MsgGet() call.
//
// The function body is protected by a mutex.
//
//  Parameter(s) are:
//
//	address		the I2C address of the device
//	data			a pointer to the buffer to be sent
//	length		the length of the buffer to be sent
//
//////////////////////////////////////////////////////////////////////////////

static void Queued_TWI0_Tx(uint8_t address, uint8_t *data, uint8_t length)
{

#if I2C_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered\r\n", (uint32_t) __func__);
#endif

	TWI0_Take(NULL);
	twi_msg.twi_xfer.address = address;
	twi_msg.twi_xfer.type = NRF_DRV_TWI_XFER_TX;
	twi_msg.twi_xfer.primary_length = length;
	twi_msg.twi_xfer.p_primary_buf = data;
	twi_msg.twi__doneCallback = TWI0_OnDone;
	twi_msg.m_twi_transfer_state = TWI_TRANSFER_PENDING;

	TWI0_MsgPut(&twi_msg);

	TWI0_MsgGet(&twi_msg);

	TWI0_Give(NULL);	
}

static void Polled_TWI0_Tx(uint8_t address, uint8_t *Packet, uint8_t PacketLen)
{
	ret_code_t err_code;

#if I2C_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered\r\n", (uint32_t) __func__);
#endif

	m_xfer_done = false;
	
	err_code = nrf_drv_twi_tx(&m_TWI0_instance, address, Packet, PacketLen, false);
	APP_ERROR_CHECK(err_code);

	uint8_t I2C_Wait = I2C_WAIT_MAX;
	while (I2C_Wait--)
	{	
		if (m_xfer_done) break;
		nrf_delay_ms(I2C_WAIT_TIME);
	}
	if (!m_xfer_done) APP_ERROR_CHECK(TWI_TRANSFER_FAILED);


}

void TWI0_Tx(uint8_t Address, uint8_t *data, uint8_t length)
{

	if( bI2C_Task_Started)
	{
	// Using I2C Task
		Queued_TWI0_Tx(Address, data, length);
	}
	else
	{
		Polled_TWI0_Tx(Address, data, length);
	}
}

void TWI0_TxRegData(uint8_t address, uint8_t Register, uint8_t Data)
{
	uint8_t  Buffer[2];

	Buffer[0] = Register;
	Buffer[1] = Data;
 
	// NRF_LOG_RAW_INFO("%s()/%d \r\n", (uint32_t *)__func__, __LINE__);

	TWI0_Tx(address, Buffer, sizeof(Buffer));

}

static void Polled_TWI0_TxRx(uint8_t address, uint8_t *Request, uint8_t RequestLen, uint8_t *Packet, uint8_t PacketLen)
{
	ret_code_t err_code;

#if I2C_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered\r\n", (uint32_t) __func__);
#endif

	ptwi_xfer->address				= address;
	ptwi_xfer->type					= NRF_DRV_TWI_XFER_TXRX;
	ptwi_xfer->primary_length			= RequestLen;
	ptwi_xfer->p_primary_buf			= Request;
	ptwi_xfer->p_secondary_buf		= Packet;
	ptwi_xfer->secondary_length 		= PacketLen;

	m_xfer_done = false;
	err_code = nrf_drv_twi_xfer(&m_TWI0_instance, ptwi_xfer, 0);
	APP_ERROR_CHECK(err_code);

	uint8_t I2C_Wait = I2C_WAIT_MAX;
	while (I2C_Wait--)
	{	
		if (m_xfer_done) break;
		nrf_delay_ms(I2C_WAIT_TIME);
	}
	if (!m_xfer_done) APP_ERROR_CHECK(TWI_TRANSFER_FAILED);


}

static void Queued_TWI0_TxRx(uint8_t address, uint8_t *Request, uint8_t RequestLen, uint8_t *Packet, uint8_t PacketLen)
{


#if I2C_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s(request len=%d, packet len=%d)\r\n", (uint32_t) __func__, RequestLen, PacketLen);
#endif

	TWI0_Take(NULL);

	twi_msg.twi_xfer.address 					= address;
	twi_msg.twi_xfer.type 						= NRF_DRV_TWI_XFER_TXRX;
	twi_msg.twi_xfer.primary_length				= RequestLen;
	twi_msg.twi_xfer.p_primary_buf				= Request;
	twi_msg.twi_xfer.p_secondary_buf			= Packet;
	twi_msg.twi_xfer.secondary_length 			= PacketLen;
	twi_msg.twi__doneCallback 				= TWI0_OnDone;
	twi_msg.m_twi_transfer_state 				= TWI_TRANSFER_PENDING;

	TWI0_MsgPut(&twi_msg);

	TWI0_MsgGet(&twi_msg);

	TWI0_Give(NULL);

}

void TWI0_TxRx(uint8_t Address, uint8_t *txdata, uint8_t txlength, uint8_t *rxdata, uint8_t rxlength)
{

	if( bI2C_Task_Started)
	{
	// Using I2C Task
		Queued_TWI0_TxRx(Address, txdata, txlength, rxdata, rxlength);
	}
	else
	{
		Polled_TWI0_TxRx(Address, txdata, txlength, rxdata, rxlength);
	}
}

#endif // TWI0_ENABLED
	
