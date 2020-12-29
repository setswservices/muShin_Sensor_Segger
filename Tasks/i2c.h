
/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		i2c.h
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, ?? Inc
//
//	Purpose/Functionality:	Defines and externs associated with i2c.c
//
/////////////////////////////////////////////////////////////////////////////////////////////

#ifndef I2C_H
#define I2C_H

#include "nrf_drv_twi.h"

///////////////////////////////////////////////////////////////////////////////////////////////
//Defines
///////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////
// Variable Prototypes
///////////////////////////////////////////////////////////////////////////////////////////////

typedef volatile enum
{ 
    TWI_TRANSFER_IDLE,
    TWI_TRANSFER_PENDING,
    TWI_TRANSFER_SUCCESS, 
    TWI_TRANSFER_FAILED
} m_twi_transfer_state_t;

typedef struct {
	nrf_drv_twi_xfer_desc_t 	twi_xfer;
    	uint32_t                			twi_flags;
	m_twi_transfer_state_t  	m_twi_transfer_state;   // Return via the TWI handler
	portTickType  				tx_wait_time;    // It can be a long time for write operatuions (for EEPROM, for example)
	portBASE_TYPE 			(*twi__doneCallback)(void *);
} TwiMessage_t, *pTwiMessage;




///////////////////////////////////////////////////////////////////////////////////////////////
// Function Prototypes
///////////////////////////////////////////////////////////////////////////////////////////////

extern portBASE_TYPE isTWI0_Ready(void);
extern portBASE_TYPE TWI0_MsgPut(pTwiMessage msg);
extern portBASE_TYPE TWI0_MsgGet(pTwiMessage msg);
extern portBASE_TYPE  TWI0_OnDone(void *pvParameters);
extern portBASE_TYPE TWI0_Take(void *pvParameters);
extern portBASE_TYPE TWI0_Give(void *pvParameters);
extern void vTWI0_MasterStart( uint16_t usStackSize, portBASE_TYPE uxPriority);

#endif // I2C_H

