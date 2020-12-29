#pragma once

/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		app.h
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2019, ?? Inc
//
//	Purpose/Functionality:	Standard Nordic header file
//
/////////////////////////////////////////////////////////////////////////////////////////////

#ifndef APP_H
#define APP_H
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////
// Version
///////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
// Defines for uiGlobalSysFlags bits
///////////////////////////////////////////////////////////////////////////////////////////////
#define GLOBAL_START_FLAG		0x01
#define GLOBAL_STOP_FLAG		0x02
#define GLOBAL_RESUME_FLAG	0x04
#define GLOBAL_SLEEP_FLAG		0x08

///////////////////////////////////////////////////////////////////////////////////////////////
// Defines for uiCrankOutControl bits
///////////////////////////////////////////////////////////////////////////////////////////////
#define CRANK_LEFT_OUT_FLAG	0x01
#define CRANK_RIGHT_OUT_FLAG	0x02
#define CRANK_DIGIFI_FLAG		0x04

///////////////////////////////////////////////////////////////////////////////////////////////
// Data Types
///////////////////////////////////////////////////////////////////////////////////////////////

typedef enum
{
	MUSHIN_APP_REQ_NONE =0,
	MUSHIN_APP_REQ_FOR_INIT,
	MUSHIN_APP_REQ_FOR_MEAS,
	MUSHIN_APP_REQ_CONNECT_DONE,
	MUSHIN_APP_REQ_DISCONNECT_DONE,
	MUSHIN_APP_REQ_PREPARE_FOR_SLEEP,
	MUSHIN_APP_REQ_TICK,
	MUSHIN_APP_XL_GYRO_REQ,
	MUSHIN_APP_GYRO_REQ,
	MUSHIN_APP_XL_REQ,
	MUSHIN_APP_HANDLE_CMD_REQ,
	
	MUSHIN_APP_LAST_REQ
} app_request_t; 

typedef struct {
	app_request_t  req;
	union {
		uint32_t  tick;
		uint32_t  meas_result;
	} d;
	portBASE_TYPE 			(*app__doneCallback)(void *);
} app_msg_t, *pAppMsg;

typedef struct {
	app_request_t  req;
	float xl_data[3];
	float gyro_data[3];
	portBASE_TYPE 			(*lsm__doneCallback)(void *);
} lsm6ds3_msg_t, *plsm6ds3Msg;


///////////////////////////////////////////////////////////////////////////////////////////////
// Global Variable Prototypes and Declarations
///////////////////////////////////////////////////////////////////////////////////////////////
extern bool bLSM6DS3_Ready;
extern uint32_t  uiGlobalTick;
extern uint16_t  uiGlobalSysFlags;
extern uint8_t  uiCrankOutControl;

///////////////////////////////////////////////////////////////////////////////////////////////
// Tasks pointers
///////////////////////////////////////////////////////////////////////////////////////////////
extern TaskHandle_t			m_app_task;
extern TaskHandle_t			m_lsm6ds3_task;
extern TaskHandle_t			m_twim_task;
//  extern TaskHandle_t			m_out_task;

///////////////////////////////////////////////////////////////////////////////////////////////
// Function Prototypes
///////////////////////////////////////////////////////////////////////////////////////////////

extern void nrf_log_notify(void);
extern void PrintGotGoAhead(uint32_t FunctionName);
extern bool Wait4ReleaseDependedTask(TaskHandle_t  *taskHandle);
void vAppStart( uint16_t usStackSize, portBASE_TYPE uxPriority );
void vTWI0_MasterStart( uint16_t usStackSize, portBASE_TYPE uxPriority);
void vLSM6DS3TapStart(void);
void vLSM6DS3DefaultStart(void);
void  vLSM6DS3Reset(void);
void vLSM6DS3fifoBegin( void );
void vLSM6DS3Start( uint16_t usStackSize, portBASE_TYPE uxPriority );
portBASE_TYPE LSM_Rx(plsm6ds3Msg msg);
portBASE_TYPE LSM_RxXL(float *x, float*y, float *z);
portBASE_TYPE LSM_RxGyro(float *x, float*y, float *z);

void Timer2_Init(uint32_t repeat_rate);
void Timer2_Start(void);
void Timer2_Stop(void);
uint32_t GetStatsTimer(void);
extern void TWI0_TxRx(uint8_t Address, uint8_t *txdata, uint8_t txlength, uint8_t *rxdata, uint8_t rxlength);
extern void TWI0_Tx(uint8_t Address, uint8_t *data, uint8_t length);
extern ret_code_t  check_lsm6ds3(void);
extern portBASE_TYPE bAppMsgPut(pAppMsg msg);
extern void vAppTick(void);
extern void vAppGotSppConnection(void);
extern void vAppSppDisconnect(void);
ret_code_t spp_send_data(uint8_t *data, uint16_t length);

void vSamplesStart( uint16_t usStackSize, portBASE_TYPE uxPriority );
ret_code_t copy_cmd(uint8_t *data, uint16_t length);
void cmd_parser(void);

uint16_t get_max_ble_len(void);
//void vOutStart( uint16_t usStackSize, portBASE_TYPE uxPriority );
void *get_out_rb_ctrl(void);
void out_rb_write(void *arg,  uint8_t n, uint8_t *v);
void out_dbg_cnts(void);


void send_value_from_sensor(uint32_t tick, char *name, float value);

void output_data_reset();
void output_data_set_tick(uint32_t tick);
void output_data_add(uint8_t *name, uint8_t *value);
void output_data_build_string(uint8_t *dest);
uint8_t output_data_is_valid();

#endif
