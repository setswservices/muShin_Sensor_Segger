/////////////////////////////////////////////////////////////////////////////////////////////
//
//	File Name:		cmd_parser.c
//	Author(s):		Jeffery Bahr, Dmitriy Antonets
//	Copyright Notice:	Copyright, 2020, ?? Inc
//
//	Purpose/Functionality:	parsing commands from the Android app
//   Notes:   Executed in the main task space, calls from  the spp_data_handler()
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


static uint8_t cmd_line[CMD_LINE_MAX_LEN];
static app_msg_t  mAppMsg;

ret_code_t copy_cmd(uint8_t *data, uint16_t length)
{
	uint16_t 	     cmd_length = ((length > (CMD_LINE_MAX_LEN-1))? (CMD_LINE_MAX_LEN-1):length)	;
	memset(cmd_line, 0x0, CMD_LINE_MAX_LEN);
	
	memcpy(cmd_line, data, cmd_length);
	mAppMsg.req = MUSHIN_APP_HANDLE_CMD_REQ;
	mAppMsg.app__doneCallback = NULL;
	bAppMsgPut(&mAppMsg);
	return NRF_SUCCESS;
}

void cmd_parser(void)
{
	uint16_t 	     cmd_length = strlen((char *)cmd_line);
	
	NRF_LOG_RAW_INFO("CMD: (%d)-[%s]\r\n", cmd_length, cmd_line);

///////////////////////////////////////////////////////////////////////////////////////////////
// Global Control
///////////////////////////////////////////////////////////////////////////////////////////////

	if (!strncmp((char *)cmd_line, "START", 5))
	{
		uiGlobalSysFlags = 0;
		uiGlobalSysFlags = GLOBAL_START_FLAG;
#ifdef DO_OUTTASK
		if (m_out_task != NULL) {
			xTaskNotifyGive(m_out_task);
			NRF_LOG_RAW_INFO("!");
			}
#endif // DO_OUTTASK
	}
	else
	if (!strncmp((char *)cmd_line, "STOP", 4))
	{
		uiGlobalSysFlags = 0;
		uiGlobalSysFlags = GLOBAL_STOP_FLAG;
	}
	else
///////////////////////////////////////////////////////////////////////////////////////////////
// Cranks  Control 
///////////////////////////////////////////////////////////////////////////////////////////////
	if (!strncmp((char *)cmd_line, "CRANK_LEFT_ON", 13))
	{
		uiCrankOutControl |=CRANK_LEFT_OUT_FLAG;
	}
	else
	if (!strncmp((char *)cmd_line, "CRANK_LEFT_OFF", 14))
	{
		uiCrankOutControl &=(~CRANK_LEFT_OUT_FLAG);
	}
	else
	if (!strncmp((char *)cmd_line, "CRANK_RIGHT_ON", 14))
	{
		uiCrankOutControl |=CRANK_RIGHT_OUT_FLAG;
	}
	else
	if (!strncmp((char *)cmd_line, "CRANK_RIGHT_OFF", 15))
	{
		uiCrankOutControl &=(~CRANK_RIGHT_OUT_FLAG);
	}else
	if (!strncmp((char *)cmd_line, "CRANK_RAW_TRSH", 14))
	{
//		char str_value[32];
		CrankDataRawThreshold = strtof((char *)&cmd_line[14], NULL);
/* [ADK] Debugging for convert str-to-float ..
		sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(CrankDataRawThreshold));
		NRF_LOG_RAW_INFO("CrankDataRawThreshold=[%s]\n", (uint32_t *)str_value);
*/
		
	}else
	if (!strncmp((char *)cmd_line, "CRANK_DIGFI_TRSH", 16))
	{
		CrankDataFilteredThreshold = strtof((char *)&cmd_line[16], NULL);
		
	}else
	if (!strncmp((char *)cmd_line, "DIGFI_CRANK_ON", 14))
	{
		uiCrankOutControl |=CRANK_DIGIFI_FLAG;
	}
	else
	if (!strncmp((char *)cmd_line, "DIGFI_CRANK_OFF", 15))
	{
		uiCrankOutControl &=(~CRANK_DIGIFI_FLAG);
	}else
///////////////////////////////////////////////////////////////////////////////////////////////
// XL  Control 
///////////////////////////////////////////////////////////////////////////////////////////////
	if (!strncmp((char *)cmd_line, "ACC_X_ON", 8))
	{
		uiXLGYOutControl |=ACC_X_OUT_FLAG;
		bXLGY_Reader_Ready = true;
		bXL_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "ACC_X_OFF", 9))
	{
		uiXLGYOutControl &=(~ACC_X_OUT_FLAG);
		bXL_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "ACC_Y_ON", 8))
	{
		uiXLGYOutControl |=ACC_Y_OUT_FLAG;
		bXLGY_Reader_Ready = true;
		bXL_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "ACC_Y_OFF", 9))
	{
		uiXLGYOutControl &=(~ACC_Y_OUT_FLAG);
		bXL_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "ACC_Z_ON", 8))
	{
		uiXLGYOutControl |=ACC_Z_OUT_FLAG;
		bXLGY_Reader_Ready = true;
		bXL_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "ACC_Z_OFF", 9))
	{
		uiXLGYOutControl &=(~ACC_Z_OUT_FLAG);
		bXL_Data_Ready = false;
	}else
///////////////////////////////////////////////////////////////////////////////////////////////
// Gyro  Control 
///////////////////////////////////////////////////////////////////////////////////////////////
	if (!strncmp((char *)cmd_line, "GYRO_X_ON", 9))
	{
		uiXLGYOutControl |=GY_X_OUT_FLAG;
		bXLGY_Reader_Ready = true;
		bGY_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "GYRO_X_OFF", 10))
	{
		uiXLGYOutControl &=(~GY_X_OUT_FLAG);
		bGY_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "GYRO_Y_ON", 9))
	{
		uiXLGYOutControl |=GY_Y_OUT_FLAG;
		bXLGY_Reader_Ready = true;
		bGY_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "GYRO_Y_OFF", 10))
	{
		uiXLGYOutControl &=(~GY_Y_OUT_FLAG);
		bGY_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "GYRO_Z_ON", 9))
	{
		uiXLGYOutControl |=GY_Z_OUT_FLAG;
		bXLGY_Reader_Ready = true;
		bGY_Data_Ready = false;
	}else
	if (!strncmp((char *)cmd_line, "GYRO_Z_OFF", 10))
	{
		uiXLGYOutControl &=(~GY_Z_OUT_FLAG);
		bGY_Data_Ready = false;
	}
}
