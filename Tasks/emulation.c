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

typedef enum
{
	EMU_SEND_BLE =0,
	EMU_RESEND_BLE,
	EMU_WAIT_TX_DONE,
} emu_send_state_t; 

#define MAX_OUT_BUF_LEN	(240)

TaskHandle_t		m_emu_task = NULL;
static void vEmulationTask( void *pvParameters );
static BaseType_t xReturned = 0;
static uint16_t uiPktLength;
static uint8_t outbuf[MAX_OUT_BUF_LEN];
static uint16_t uiBufLen;
static emu_send_state_t  spp_send_state = EMU_SEND_BLE;

void vEmulationStart( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
       NRF_LOG_FLUSH();
#endif

	xReturned = xTaskCreate(	vEmulationTask,				/* The task that implements the audio control. */
			"Emu",						/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
			usStackSize,					/* The size of the stack allocated to the task. */
			NULL,						/* The parameter is not used, so NULL is passed. */
			uxPriority,					/* The priority allocated to the task. */
			&m_emu_task 
		);		
	
	if (xReturned != pdPASS)
	{
		NRF_LOG_ERROR("Emulation Task  not created.");
		APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
	}
}


static void outbuf_init(void)
{
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		uiPktLength =0;
		memcpy(&outbuf[uiPktLength], &uiGlobalPktN, sizeof(uint32_t));
		uiGlobalPktN++;	
		uiPktLength += sizeof(uint32_t);
	}else{
		char str_value[16];
		sprintf((char *)outbuf,"%04lu", uiGlobalPktN++);
	}
}

static void outbuf_next_pkt_init(void)
{
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		outbuf[uiPktLength++] = ';';
		memcpy(&outbuf[uiPktLength], &uiGlobalPktN, sizeof(uint32_t));
		uiGlobalPktN++;	
		uiPktLength += sizeof(uint32_t);
	}else{
		char str_value[16];
		sprintf(str_value,";%04lu", uiGlobalPktN++);
		strcat((char *)outbuf, str_value);
		}
}

uint32_t add_value_to_buffer(char *name, float value)
{
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		outbuf[uiPktLength++] = *name;
		memcpy(&outbuf[uiPktLength], &value, sizeof(float));
		uiPktLength += sizeof(float);
		return uiPktLength;
	}else{
		char chank[64];
		char str_value[16];
		sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(value));
		strcpy(chank, ",");
		strcat(chank, name);
		strcat(chank, " ");
		strcat(chank, str_value);
#if 0		
		if (print_cnt > 1 && print_cnt < 16) {
			if ( print_flt_cnt < 3) {
				sprintf(dbg_str_value[print_flt_cnt], MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(value));
				NRF_LOG_RAW_INFO("[%s]\r\n", (uint32_t *)dbg_str_value[print_flt_cnt]);
				df = value;
				NRF_LOG_RAW_HEXDUMP_INFO((uint32_t *)&df, sizeof(float));
				print_flt_cnt++;
			}
	}
#endif // 0		
		if (strlen((char *)outbuf) + strlen(chank) < uiBufLen)
			strcat((char *)outbuf, chank);
		return strlen((char *)outbuf);
	}
  }

float crank_emulation(float *value, uint8_t *up_down) 
{
	if (*value >= 1.0f) { *value -=0.03f; *up_down = 1; return *value; }
	if (*value <= 0.002f) { *value +=0.03f; *up_down = 0; return *value; }
	if (*up_down) 
		{*value-=0.03f; return *value; }
	*value += 0.03f;
	return *value;
}

static void vEmulationTask( void *pvParameters )
{
	( void ) pvParameters;
	uint8_t uiFirstPkt = 1, print_cnt = 0;
	float lc = 0.002f, rc = 1.00f;
	uint8_t lc_dir = 1;
	uint8_t rc_dir = 0;
	TickType_t start;
       ret_code_t spp_rc;

	NRF_LOG_RAW_INFO("%s Waiting for START notification\r\n", (uint32_t *)__func__);
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	PrintGotGoAhead((uint32_t *)__func__);	
	NRF_LOG_FLUSH();

	uiBufLen = (get_max_ble_len() > ioBufLen)? ioBufLen : get_max_ble_len();
	uiPktLength = 0;
	uiGlobalPktN = 0;
 	NRF_LOG_RAW_INFO("ioBuff size=%d\r\n", uiBufLen);

	start = xTaskGetTickCount();
	while(true)
	{
		if (uiGlobalSysFlags == GLOBAL_STOP_FLAG) 
		{
			NRF_LOG_RAW_INFO("pkts=%d, duration=%d\r\n", uiGlobalPktN, (xTaskGetTickCount()-start));
			(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			continue;
		}
		if (spp_send_state == EMU_SEND_BLE)
		{
			if (uiPktLength < (uiBufLen -16) )
			{
				if (uiPktLength == 0) {
					outbuf_init();
					if (uiFirstPkt == 1) {
						add_value_to_buffer("V", 1.25f);
						uiFirstPkt = 0;
					}
				}else {
					outbuf_next_pkt_init();
				}
                  		add_value_to_buffer("L", crank_emulation(&lc, &lc_dir));
                  		add_value_to_buffer("R", crank_emulation(&rc, &rc_dir));
				continue;		
			}else{
				if (print_cnt == 0) {
//					printf("pkt size=%d\r\n", strlen((char *)outbuf));
					if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
					{
				 		NRF_LOG_RAW_INFO("pkt size=%d\r\n", uiPktLength);
//					NRF_LOG_RAW_HEXDUMP_INFO(outbuf, 32);

					}else{	
						NRF_LOG_RAW_INFO("pkt size=%d\r\n", strlen((char *)outbuf));
					}
					print_cnt = 1;
				}
			}
		}
		switch(spp_send_state) {
			case EMU_SEND_BLE:
				if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT) {
					spp_rc = spp_send_data(outbuf, uiPktLength);
				}else{
					spp_rc = spp_send_data(outbuf, strlen((char *)outbuf));
				}
				if (spp_rc == NRF_ERROR_RESOURCES)
					spp_send_state = EMU_WAIT_TX_DONE;
				else
				if (spp_rc == NRF_SUCCESS)	
					uiPktLength = 0;
				else{
				 	   NRF_LOG_RAW_INFO("err=%x\r\n", spp_rc);
					}
				break;
			case EMU_WAIT_TX_DONE:	
				(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
				spp_send_state = EMU_RESEND_BLE;
				break;
			case EMU_RESEND_BLE:
				if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT) {
					spp_rc = spp_send_data(outbuf, uiPktLength);
				}else{
					spp_rc = spp_send_data(outbuf, strlen((char *)outbuf));
				}
				if (spp_rc == NRF_ERROR_RESOURCES)
					spp_send_state = EMU_WAIT_TX_DONE;
				else
				if (spp_rc == NRF_SUCCESS) {	
					uiPktLength = 0;
					spp_send_state = EMU_SEND_BLE;
				}
				break;
			}	
	}
}
