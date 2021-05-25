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
#include "nrf_delay.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mushin_config.h"
#include "app.h"

typedef enum
{
	DATA_SEND_BLE =0,
	DATA_RESEND_BLE,
	DATA_WAIT_TX_DONE,
} data_send_state_t; 

#define MAX_OUT_BUF_LEN	(240)

TaskHandle_t		m_data_task = NULL;
static app_msg_t  mAppMsg;
static void vDataTask( void *pvParameters );
static BaseType_t xReturned = 0;
static uint16_t uiPktLength;
static uint8_t outbuf[MAX_OUT_BUF_LEN];
static uint16_t uiBufLen;
static data_send_state_t  spp_send_state = DATA_SEND_BLE;
static float adc_volts125 = 0.0, adc_voltsLeft = 0.0, adc_voltsRight = 0.0;
float fXL_x, fXL_y, fXL_z;
float fGY_x, fGY_y, fGY_z;
static float fXL_xh = 0.0, fXL_yh = 0.0, fXL_zh = 0.0;
static float fGY_xh = 0.0, fGY_yh =0.0, fGY_zh = 0.0;

void vDataStart( uint16_t usStackSize, portBASE_TYPE uxPriority )
{

#if TASK_START_DEBUG_ENABLED
	NRF_LOG_RAW_INFO("%s Entered ..\r\n", (uint32_t)__func__);
       NRF_LOG_FLUSH();
#endif

	xReturned = xTaskCreate(	vDataTask,				/* The task that implements the audio control. */
			"Dt",						/* Text name assigned to the task.  This is just to assist debugging.  The kernel does not use this name itself. */
			usStackSize,					/* The size of the stack allocated to the task. */
			NULL,						/* The parameter is not used, so NULL is passed. */
			uxPriority,					/* The priority allocated to the task. */
			&m_data_task 
		);		
	
	if (xReturned != pdPASS)
	{
		NRF_LOG_ERROR("Data Task not created.");
		APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
	}
}

static void databuf_init(void)
{
	TickType_t t = xTaskGetTickCount();

	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		uiPktLength =0;
		if (uiGlobalEmuTaskCntrl == MUSHIN_TASK_ACTIVE) {
			memcpy(&outbuf[uiPktLength], &uiGlobalPktN, sizeof(uint32_t));
			uiPktLength += sizeof(uint32_t);
		}
		else{
			
//			memcpy(&outbuf[uiPktLength], &uiGlobalTick, sizeof(uint32_t));
			memcpy(&outbuf[uiPktLength], &t, sizeof(TickType_t));
//			uiGlobalTick++;
			uiPktLength += sizeof(TickType_t);
		}
		uiGlobalPktN++;	
	}else{
		char str_value[16];
		sprintf((char *)outbuf,"%04lu", uiGlobalPktN++);
	}
}
static void databuf_next_pkt_init(void)
{
	TickType_t t = xTaskGetTickCount();
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		outbuf[uiPktLength++] = ';';
		if (uiGlobalEmuTaskCntrl == MUSHIN_TASK_ACTIVE) {
			memcpy(&outbuf[uiPktLength], &uiGlobalPktN, sizeof(uint32_t));
			uiPktLength += sizeof(uint32_t);
		}
		else{
//			memcpy(&outbuf[uiPktLength], &uiGlobalTick, sizeof(uint32_t));
			memcpy(&outbuf[uiPktLength], &t, sizeof(TickType_t));
//			uiGlobalTick++;
			uiPktLength += sizeof(TickType_t);
		}
		uiGlobalPktN++;	
	}else{
		char str_value[16];
		sprintf(str_value,";%04lu", uiGlobalPktN++);
		strcat((char *)outbuf, str_value);
		}
}

static uint32_t add_data_to_buffer(char *name, float data)
{
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		outbuf[uiPktLength++] = *name;
		memcpy(&outbuf[uiPktLength], &data, sizeof(float));
		uiPktLength += sizeof(float);
		return uiPktLength;
	}else{
		char chank[64];
		char str_value[16];
		sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(data));
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

static uint32_t add_data_to_buffer3(char *name, float data1, float data2, float data3)
{
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
	{
		outbuf[uiPktLength++] = *name;
		memcpy(&outbuf[uiPktLength], &data1, sizeof(float));
		uiPktLength += sizeof(float);
		memcpy(&outbuf[uiPktLength], &data2, sizeof(float));
		uiPktLength += sizeof(float);
		memcpy(&outbuf[uiPktLength], &data3, sizeof(float));
		uiPktLength += sizeof(float);
		return uiPktLength;
	}else{
		char chank[64];
		char str_value[16];
		sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(data1));
		strcpy(chank, ",");
		strcat(chank, name);
		strcat(chank, " ");
		strcat(chank, str_value);
		strcat(chank, ",");
		sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(data2));
		strcat(chank, ",");
		sprintf(str_value, MUSHIN_LOG_FLOAT_MARKER, MUSHIN_SPRINTF_FLOAT(data3));
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

static float fDiff(float _a, float _b)
{
	if (_a > _b)  return (float)(_a -_b);
	if (_a < _b)  return (float)(_b -_a);
	return 0.0;
	
}
#if LSM6DS3_BLE_OUTPUT_ENABLED

static uint32_t iAccFirstPkt( void)
{
	if ((uiXLGYOutControl & (ACC_X_OUT_FLAG |ACC_Y_OUT_FLAG | ACC_Z_OUT_FLAG)) == (ACC_X_OUT_FLAG |ACC_Y_OUT_FLAG | ACC_Z_OUT_FLAG))
	{
		add_data_to_buffer3("A", fXL_xh, fXL_yh, fXL_zh);
	}else{
		if ((uiXLGYOutControl & ACC_X_OUT_FLAG) == ACC_X_OUT_FLAG) {
			add_data_to_buffer("X", fXL_xh);
		}
		if ((uiXLGYOutControl & ACC_Y_OUT_FLAG) == ACC_Y_OUT_FLAG) {
			add_data_to_buffer("Y", fXL_yh);
		}
		if ((uiXLGYOutControl & ACC_Z_OUT_FLAG) == ACC_Z_OUT_FLAG) {
			add_data_to_buffer("Z", fXL_zh);
		}
	}
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
		return uiPktLength;
	else
		return strlen((char *)outbuf);
}

static uint32_t iGyroFirstPkt( void)
{
	if ((uiXLGYOutControl & (GY_X_OUT_FLAG |GY_Y_OUT_FLAG | GY_Z_OUT_FLAG)) == (GY_X_OUT_FLAG |GY_Y_OUT_FLAG | GY_Z_OUT_FLAG))
	{
		add_data_to_buffer3("g", fGY_xh, fGY_yh, fGY_zh);
	}else{
		if ((uiXLGYOutControl & GY_X_OUT_FLAG) == GY_X_OUT_FLAG) {
			add_data_to_buffer("x", fGY_xh);
		}
		if ((uiXLGYOutControl & GY_Y_OUT_FLAG) == GY_Y_OUT_FLAG) {
			add_data_to_buffer("y", fGY_yh);
		}
		if ((uiXLGYOutControl & GY_Z_OUT_FLAG) == GY_Z_OUT_FLAG) {
		add_data_to_buffer("z", fGY_zh);
		}
	}
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
		return uiPktLength;
	else
		return strlen((char *)outbuf);
}

static uint32_t iAccPkt( void)
{
	if ((uiXLGYOutControl & (ACC_X_OUT_FLAG |ACC_Y_OUT_FLAG | ACC_Z_OUT_FLAG)) == (ACC_X_OUT_FLAG |ACC_Y_OUT_FLAG | ACC_Z_OUT_FLAG))
	{
		bool bNeedAll = false;
		
		if (fDiff(fXL_x, fXL_xh) > 0.01)	{
			bNeedAll = true;
		}
		if (fDiff(fXL_y, fXL_yh) > 0.01)	{
			bNeedAll = true;
		}
		if (fDiff(fXL_z, fXL_zh) > 0.01)	{
			bNeedAll = true;
		}
		if (bNeedAll) {
			add_data_to_buffer3("A", fXL_x, fXL_y, fXL_z);
			fXL_xh = fXL_x;
			fXL_yh = fXL_y;
			fXL_zh = fXL_z;
		}
	}else{
		if ((uiXLGYOutControl & ACC_X_OUT_FLAG) == ACC_X_OUT_FLAG) {
			if (fDiff(fXL_x, fXL_xh) > 0.01)	{
				add_data_to_buffer("X", fXL_x);
				fXL_xh = fXL_x;
			}
		}
		if ((uiXLGYOutControl & ACC_Y_OUT_FLAG) == ACC_Y_OUT_FLAG) {
			if (fDiff(fXL_y, fXL_yh) > 0.01)	{
				add_data_to_buffer("Y", fXL_y);
				fXL_yh = fXL_y;
			}
		}
		if ((uiXLGYOutControl & ACC_Z_OUT_FLAG) == ACC_Z_OUT_FLAG) {
			if (fDiff(fXL_z, fXL_zh) > 0.01)	{
				add_data_to_buffer("Z", fXL_z);
				fXL_zh = fXL_z;
			}
		}
	}
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
		return uiPktLength;
	else
		return strlen((char *)outbuf);
}

static uint32_t iGyroPkt( void)
{
	if ((uiXLGYOutControl & (GY_X_OUT_FLAG |GY_Y_OUT_FLAG | GY_Z_OUT_FLAG)) == (GY_X_OUT_FLAG |GY_Y_OUT_FLAG | GY_Z_OUT_FLAG))
	{
		bool bNeedAll = false;
		if (fDiff(fGY_x, fGY_xh) > 0.01)	{
			bNeedAll = true;
		}
		if (fDiff(fGY_y, fGY_yh) > 0.01)	{
			bNeedAll = true;
		}
		if (fDiff(fGY_z, fGY_zh) > 0.01)	{
			bNeedAll = true;
		}
		if (bNeedAll) {
			add_data_to_buffer3("g", fGY_x, fGY_y, fGY_z);
			fGY_xh = fGY_x;
			fGY_yh = fGY_y;
			fGY_zh = fGY_z;
		}
	}else{
		if ((uiXLGYOutControl & GY_X_OUT_FLAG) == GY_X_OUT_FLAG) {
			if (fDiff(fGY_x, fGY_xh) > 0.01)	{
				add_data_to_buffer("x", fGY_x);
				fGY_xh = fGY_x;
			}
		}
		if ((uiXLGYOutControl & GY_Y_OUT_FLAG) == GY_Y_OUT_FLAG) {
			if (fDiff(fGY_y, fGY_yh) > 0.01)	{
				add_data_to_buffer("y", fGY_y);
				fGY_yh = fGY_y;
			}
		}
		if ((uiXLGYOutControl & GY_Z_OUT_FLAG) == GY_Z_OUT_FLAG) {
			if (fDiff(fGY_z, fGY_zh) > 0.01)	{
				add_data_to_buffer("z", fGY_z);
				fGY_zh = fGY_z;
			}
		}
	}
	if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
		return uiPktLength;
	else
		return strlen((char *)outbuf);
}

#endif // LSM6DS3_BLE_OUTPUT_ENABLED


static float adc_volts0, adc_volts1, adc_volts2;
static 	uint32_t  s0, s1, s2;
	
static void vDataTask( void *pvParameters )
{
	( void ) pvParameters;
	uint8_t uiFirstPkt = 1, print_cnt = 0;
       ret_code_t spp_rc;
	TickType_t start;
	int16_t s_rb_as = 0;
	void *rbuf;
//	EventBits_t rcEvent;
//	uint16_t dbg_cnt = 256;

	NRF_LOG_RAW_INFO("%s Waiting for START notification\r\n", (uint32_t *)__func__);
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	PrintGotGoAhead((uint32_t *)__func__);	
	NRF_LOG_FLUSH();

	uiBufLen = (get_max_ble_len() > ioBufLen)? ioBufLen : get_max_ble_len();
	uiPktLength = 0;
	uiGlobalPktN = 0;
 	NRF_LOG_RAW_INFO("ioBuff size=%d\r\n", uiBufLen);
	rbuf = get_samples_rb_ctrl();

	start = xTaskGetTickCount();
	while(true)
	{
		if (uiGlobalSysFlags == GLOBAL_STOP_FLAG) 
		{
			NRF_LOG_RAW_INFO("pkts=%d, duration=%d\r\n", uiGlobalPktN, (xTaskGetTickCount()-start));
			(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			continue;
		}
		if (spp_send_state == DATA_SEND_BLE)
		{
			s_rb_as = samples_rb_available_size(rbuf);
			if (s_rb_as)
			{
				samples_rb_read(rbuf, &s0, &s1, &s2 /*, &s3 */);
//		NRF_LOG_RAW_INFO("[%04x %04x  %04x]\r\n", s0, s1, s2);
				adc_volts0 = (s0/4096.0)*3.6;  // Gain = 1/6
				adc_volts1 = (s1/4096.0)*3.6;  // Gain = 1/6
				adc_volts2 = (s2 /4096.0)*3.6; // Gain = 1/6
				
//      						NRF_LOG_RAW_INFO(" Vref="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts1));
			}else{
#if 0
// [ADK] 05/18/2021 It will not work. SAADC code is "outside" of the FreeRTOS address space.
//                             Use just passive wait but tiks is too long .. 
				if (xDataTaskEventGroup != NULL) {
    					rcEvent = xEventGroupWaitBits(xDataTaskEventGroup, 
								RINGBUF_DATA_BIT,	// Wait the ADC will done
    								pdTRUE, 				// Clear thi bit
    								pdFALSE,				// Wait ANY bit
    								pdMS_TO_TICKS(10)  //  Wait with 10ms timeout
							);
					if ((rcEvent&RINGBUF_DATA_BIT) == RINGBUF_DATA_BIT)	
						continue;
					else
					if (rcEvent == 0) {	
				 	   	NRF_LOG_RAW_INFO("*ERROR* SAADC timeout\r\n");
					}
				}
#endif			
				portYIELD();
				continue;
			}

			if (uiPktLength < (uiBufLen -24) )
			{
				if (uiPktLength == 0) {
					databuf_init();
				}else {
					databuf_next_pkt_init();
				}

				if (uiFirstPkt) {
//					adc_volts1 = 1.25;
                  			add_data_to_buffer("V", adc_volts1); 
                  			adc_volts125 = adc_volts1;
							
					if ((uiCrankOutControl&CRANK_LEFT_OUT_FLAG) == CRANK_LEFT_OUT_FLAG) 
					{
                  				add_data_to_buffer("L", adc_volts0);
						adc_voltsLeft =  adc_volts0;
					}
									
					if ((uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == CRANK_RIGHT_OUT_FLAG) 
					{
                  				add_data_to_buffer("R", adc_volts2);
						adc_voltsRight = adc_volts2;
					}
/*
      	NRF_LOG_RAW_INFO("-Vref="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts1));
    	NRF_LOG_RAW_INFO("-L="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts0));
    	NRF_LOG_RAW_INFO("-R="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts2));
	NRF_LOG_FLUSH();
*/	
//                  			add_data_to_buffer("L", adc_volts0);
//                  			add_data_to_buffer("R", adc_volts2);
#if LSM6DS3_BLE_OUTPUT_ENABLED
						if (uiXLGYOutControl) {
							if ((uiXLGYOutControl & ACC_XYZ_OUT_FLAG) == 0) {
								bXL_Data_Ready = false;
							}else{
								iAccFirstPkt();
								bXL_Data_Ready = false;
								if (bXLGY_Reader_Ready) {
									mAppMsg.req = MUSHIN_APP_XL_REQ;
									mAppMsg.app__doneCallback = NULL;
									mAppMsg.d.tick =0;
									if (uiGlobalSysFlags == GLOBAL_STOP_FLAG)
										bXLGY_Reader_Ready = false;
									else
										bAppMsgPut(&mAppMsg);
								}
							}
							if ((uiXLGYOutControl & GY_XYZ_OUT_FLAG) == 0) {
								bGY_Data_Ready = false;
							}else{
								iGyroFirstPkt();
								bGY_Data_Ready = false;
								if (bXLGY_Reader_Ready) {
									mAppMsg.req = MUSHIN_APP_GYRO_REQ;
									mAppMsg.app__doneCallback = NULL;
									mAppMsg.d.tick =0;
									if (uiGlobalSysFlags == GLOBAL_STOP_FLAG)
										bXLGY_Reader_Ready = false;
									else
										bAppMsgPut(&mAppMsg);
								}
							}
						}
#endif // LSM6DS3_BLE_OUTPUT_ENABLED
					uiFirstPkt = 0;
				}else{
//					adc_volts1 = 1.25;
					if (fDiff(adc_volts125,  adc_volts1) > 0.02) {
                  				add_data_to_buffer("V", adc_volts1); 
                  				adc_volts125 = adc_volts1;
					}

					if ((uiCrankOutControl&CRANK_LEFT_OUT_FLAG) == CRANK_LEFT_OUT_FLAG) 
					{
						if (fDiff(adc_voltsLeft,  adc_volts0) > 0.0175) {
                  					add_data_to_buffer("L", adc_volts0);
							adc_voltsLeft =  adc_volts0;
						}
					}
									
					if ((uiCrankOutControl&CRANK_RIGHT_OUT_FLAG) == CRANK_RIGHT_OUT_FLAG) 
					{
						if (fDiff(adc_voltsRight,  adc_volts2) > 0.0175) {
                  					add_data_to_buffer("R", adc_volts2);
							adc_voltsRight = adc_volts2;
						}
					}

/*
     	NRF_LOG_RAW_INFO("+Vref="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts1));
    	NRF_LOG_RAW_INFO("+L="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts0));
    	NRF_LOG_RAW_INFO("+R="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts2));
	NRF_LOG_FLUSH();
*/	
//                  			add_data_to_buffer("L", adc_volts0);
//                  			add_data_to_buffer("R", adc_volts2);
#if LSM6DS3_BLE_OUTPUT_ENABLED
					if (uiXLGYOutControl) {
						// Need add a XL/GY data ...
						if ((uiXLGYOutControl & ACC_XYZ_OUT_FLAG) == 0) {
							bXL_Data_Ready = false;
						}else{
							// Need add XL data..
							if (bXL_Data_Ready) {
								iAccPkt();
								bXL_Data_Ready = false;
							}
							if (bXLGY_Reader_Ready) {
								mAppMsg.req = MUSHIN_APP_XL_REQ;
								mAppMsg.app__doneCallback = NULL;
								mAppMsg.d.tick =0;
								if (uiGlobalSysFlags == GLOBAL_STOP_FLAG)
									bXLGY_Reader_Ready = false;
								else
									bAppMsgPut(&mAppMsg);
							}
						}
						if ((uiXLGYOutControl & GY_XYZ_OUT_FLAG) == 0) {
							bGY_Data_Ready = false;
						}else{
							// Need add GY data ..
							if (bGY_Data_Ready) {
								iGyroPkt();
								bGY_Data_Ready = false;
							}
							if (bXLGY_Reader_Ready) {
								mAppMsg.req = MUSHIN_APP_GYRO_REQ;
								mAppMsg.app__doneCallback = NULL;
								mAppMsg.d.tick =0;
								if (uiGlobalSysFlags == GLOBAL_STOP_FLAG)
									bXLGY_Reader_Ready = false;
								else
									bAppMsgPut(&mAppMsg);
							}
						}
					}
#endif //LSM6DS3_BLE_OUTPUT_ENABLED
				}
				continue;		
			}else{
				if (print_cnt == 0) {
//					printf("pkt size=%d\r\n", strlen((char *)outbuf));
					if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT)
					{
				 		NRF_LOG_RAW_INFO("pkt size=%d\r\n", uiPktLength);
/*						
						NRF_LOG_RAW_HEXDUMP_INFO(outbuf, 32);
						NRF_LOG_FLUSH();
						NRF_LOG_RAW_HEXDUMP_INFO(&outbuf[32], 32);
						NRF_LOG_FLUSH();
						NRF_LOG_RAW_HEXDUMP_INFO(&outbuf[64], 32);
						NRF_LOG_FLUSH();
*/						
					}else{	
						NRF_LOG_RAW_INFO("pkt size=%d\r\n", strlen((char *)outbuf));
					}
					print_cnt = 1;
				}
			}
		}
		switch(spp_send_state) {
			case DATA_SEND_BLE:
				if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT) {
					spp_rc = spp_send_data(outbuf, uiPktLength);
				}else{
					spp_rc = spp_send_data(outbuf, strlen((char *)outbuf));
				}
				if (spp_rc == NRF_ERROR_RESOURCES)
					spp_send_state = DATA_WAIT_TX_DONE;
				else
				if (spp_rc == NRF_SUCCESS)	
					uiPktLength = 0;
				else{
				 	   NRF_LOG_RAW_INFO("err=%x\r\n", spp_rc);
					}
				break;
			case DATA_WAIT_TX_DONE:
					(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
					spp_send_state = DATA_RESEND_BLE;
#if 0					
				if (xDataTaskEventGroup != NULL) {
    					rcEvent = xEventGroupWaitBits(xDataTaskEventGroup, 
								BLE_TRX_DONE_BIT,	// Wait the BLE TrX will done
    								pdTRUE, 				// Clear thi bit
    								pdFALSE,				// Wait ANY bit
    								pdMS_TO_TICKS(100)  //  Wait with 100ms timeout
							);
					if ((rcEvent&BLE_TRX_DONE_BIT) == BLE_TRX_DONE_BIT)	
						spp_send_state = DATA_RESEND_BLE;
					else
					if (rcEvent == 0) {	
				 	   	NRF_LOG_RAW_INFO("*ERROR* spp send timeout\r\n");
						uiPktLength = 0;
						spp_send_state = DATA_SEND_BLE;
					}
				}
#endif				
				break;
			case DATA_RESEND_BLE:
				if (uiPktFormatType == MUSHIN_BLE_BINARY_PKT) {
					spp_rc = spp_send_data(outbuf, uiPktLength);
				}else{
					spp_rc = spp_send_data(outbuf, strlen((char *)outbuf));
				}
				if (spp_rc == NRF_ERROR_RESOURCES)
					spp_send_state = DATA_WAIT_TX_DONE;
				else
				if (spp_rc == NRF_SUCCESS) {	
					uiPktLength = 0;
					spp_send_state = DATA_SEND_BLE;
				}
				break;
			}	
		
	}
}
