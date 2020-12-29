// Derived from Nordic Examples

#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_bas.h"
#include "ble_hrs.h"
#include "ble_dis.h"
#if MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
#include "ble_spp.h"
#endif //MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
#if MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
#include "ble_nus.h"
#endif // MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
#include "ble_conn_params.h"
#include "sensorsim.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_freertos.h"
#include "nrfx_gpiote.h"
#include "nrf_gpiote.h"
#include "app_timer.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "bsp_btn_ble.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"
#include "fds.h"
#include "ble_conn_state.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_saadc.h"
#include "nrf_drv_ppi.h"
#include "nrf_drv_timer.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "mushin_config.h"
#include "app.h"


#define DEVICE_NAME  	"iMUSH"
#define MANUFACTURER_NAME                   "NordicSemiconductor"                   /**< Manufacturer. Will be passed to Device Information Service. */

#define APP_BLE_OBSERVER_PRIO               3                                       /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define APP_BLE_CONN_CFG_TAG                1                                       /**< A tag identifying the SoftDevice BLE configuration. */

#define APP_ADV_INTERVAL                    300                                     /**< The advertising interval (in units of 0.625 ms. This value corresponds to 187.5 ms). */
#define APP_ADV_DURATION                    18000                                       /**< The advertising duration (180 seconds) in units of 10 milliseconds. */

#define BATTERY_LEVEL_MEAS_INTERVAL         2000                                    /**< Battery level measurement interval (ms). */
#define MIN_BATTERY_LEVEL                   81                                      /**< Minimum simulated battery level. */
#define MAX_BATTERY_LEVEL                   100                                     /**< Maximum simulated battery level. */
#define BATTERY_LEVEL_INCREMENT             1                                       /**< Increment between each simulated battery level measurement. */

#define HEART_RATE_MEAS_INTERVAL            1000                                    /**< Heart rate measurement interval (ms). */
#define MIN_HEART_RATE                      140                                     /**< Minimum heart rate as returned by the simulated measurement function. */
#define MAX_HEART_RATE                      300                                     /**< Maximum heart rate as returned by the simulated measurement function. */
#define HEART_RATE_INCREMENT                10                                      /**< Value by which the heart rate is incremented/decremented for each call to the simulated measurement function. */

#define RR_INTERVAL_INTERVAL                300                                     /**< RR interval interval (ms). */
#define MIN_RR_INTERVAL                     100                                     /**< Minimum RR interval as returned by the simulated measurement function. */
#define MAX_RR_INTERVAL                     500                                     /**< Maximum RR interval as returned by the simulated measurement function. */
#define RR_INTERVAL_INCREMENT               1                                       /**< Value by which the RR interval is incremented/decremented for each call to the simulated measurement function. */

#define SENSOR_CONTACT_DETECTED_INTERVAL    5000                                    /**< Sensor Contact Detected toggle interval (ms). */

#define MIN_CONN_INTERVAL                   MSEC_TO_UNITS(20, UNIT_1_25_MS)        /**< Minimum acceptable connection interval (0.4 seconds). */
#define MAX_CONN_INTERVAL                   MSEC_TO_UNITS(40, UNIT_1_25_MS)        /**< Maximum acceptable connection interval (0.65 second). */
#define SLAVE_LATENCY                       0                                       /**< Slave latency. */
#define CONN_SUP_TIMEOUT                    MSEC_TO_UNITS(4000, UNIT_10_MS)         /**< Connection supervisory time-out (4 seconds). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY      5000                                    /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY       30000                                   /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT        3                                       /**< Number of attempts before giving up the connection parameter negotiation. */

#define SEC_PARAM_BOND                      1                                       /**< Perform bonding. */
#define SEC_PARAM_MITM                      0                                       /**< Man In The Middle protection not required. */
#define SEC_PARAM_LESC                      0                                       /**< LE Secure Connections not enabled. */
#define SEC_PARAM_KEYPRESS                  0                                       /**< Keypress notifications not enabled. */
#define SEC_PARAM_IO_CAPABILITIES           BLE_GAP_IO_CAPS_NONE                    /**< No I/O capabilities. */
#define SEC_PARAM_OOB                       0                                       /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE              7                                       /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE              16                                      /**< Maximum encryption key size. */

#define DEAD_BEEF                           0xDEADBEEF                              /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */
#if MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
#define UART_TX_BUF_SIZE                256                                         /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE                256                                         /**< UART RX buffer size. */
#endif //MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)

#define OSTIMER_WAIT_FOR_QUEUE              2                                       /**< Number of ticks to wait for the timer queue to be ready */
#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */



BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);		/**< BLE NUS service instance. */
NRF_BLE_GATT_DEF(m_gatt);                                           /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                             /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising);                                 /**< Advertising module instance. */

static uint16_t m_conn_handle         = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current connection. */
static uint16_t   m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;            /**< Maximum length of data (in bytes) that can be transmitted to the peer by the Nordic UART service module. */

static ble_uuid_t m_adv_uuids[] =                                   /**< Universally unique service identifiers. */
{
#if MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
    {BLE_UUID_SPP_SERVICE, NUS_SERVICE_UUID_TYPE},  
#endif //MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
#if MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
    {BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}
#endif // MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
    
};

#if MUSH_FEATURE_ENABLED(AD8237)
static const nrf_drv_timer_t m_timer4 = NRF_DRV_TIMER_INSTANCE(4);
static nrf_saadc_value_t     m_buffer_ain_pool[2][ADC_POOL_SIZE];
static uint32_t              	m_adc_evt_counter = 0;
static nrf_ppi_channel_t     m_ppi_channel;
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)

typedef struct {
	uint16_t c0;
	uint16_t c1;
	uint16_t c2;
	uint16_t c3;
} saadc_sample_t, *p_sample;

typedef struct {
	int16_t idx_read;
	int16_t idx_write;
	int16_t rb_size;
	p_sample		samples;
} saadc_ringbuffer_control_t, *p_rb_cntrl;

static saadc_sample_t samples_rb[ SAMPLES_RB_SIZE];
static saadc_ringbuffer_control_t  rb_cntrl;

static void samples_rb_init(p_rb_cntrl rbuf)
{
	rbuf->idx_read = rbuf->idx_write = 0;
	rbuf->rb_size = SAMPLES_RB_SIZE;
	rbuf->samples = samples_rb; 
}

bool samples_rb_empty(void *arg)
{
	p_rb_cntrl rbuf = (p_rb_cntrl)arg;
	return (rbuf->idx_read == rbuf->idx_write);
}

void samples_rb_reset(void *arg)
{
	p_rb_cntrl rbuf = (p_rb_cntrl)arg;
	rbuf->idx_read = rbuf->idx_write = 0;
}

int16_t samples_rb_free_size(void *arg)
{
	p_rb_cntrl rbuf = (p_rb_cntrl)arg;
	int16_t free;

	free = rbuf->idx_read - rbuf->idx_write;
	if (free <= 0)
		free += rbuf->rb_size;
	return free-1;
}

int16_t samples_rb_available_size(void *arg)
{
	p_rb_cntrl rbuf = (p_rb_cntrl)arg;
	int16_t avail;

	avail = rbuf->idx_write - rbuf->idx_read;
	if (avail < 0)
		avail += rbuf->rb_size;
	return avail;
}

void samples_rb_read(void *arg,  uint16_t *s0, uint16_t *s1, uint16_t *s2, uint16_t *s3)
{
	p_rb_cntrl rbuf = (p_rb_cntrl)arg;
	
	*s0 = rbuf->samples[ rbuf->idx_read].c0;
	*s1 = rbuf->samples[ rbuf->idx_read].c1;
	*s2 = rbuf->samples[ rbuf->idx_read].c2;
	*s3 = rbuf->samples[ rbuf->idx_read].c3;

	if (rbuf->idx_read +1 >= rbuf->rb_size)
		rbuf->idx_read = 0;
	else
		rbuf->idx_read++;

}

void samples_rb_write(void *arg,  uint16_t s0, uint16_t s1, uint16_t s2, uint16_t s3)
{
	p_rb_cntrl rbuf = (p_rb_cntrl)arg;

	if (rbuf->idx_write +1 >= rbuf->rb_size) {
		rbuf->idx_write = 0;
	}else{
		rbuf->idx_write++;
	}

	rbuf->samples[ rbuf->idx_write].c0 = s0;
	rbuf->samples[ rbuf->idx_write].c1 = s1;
	rbuf->samples[ rbuf->idx_write].c2 = s2;
	rbuf->samples[ rbuf->idx_write].c3 = s3;
}

void *get_samples_rb_ctrl(void)
{
	return (void *)&rb_cntrl;
}

#endif // MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#endif // MUSH_FEATURE_ENABLED(AD8237)

static TimerHandle_t m_app_timer;                               /**< Definition of application timer. */

#if NRF_LOG_ENABLED
static TaskHandle_t m_logger_thread;                                /**< Definition of Logger thread. */
#endif
static ble_gap_addr_t MAC_Address;
uint32_t  uiGlobalTick = 0;
uint16_t	uiGlobalSysFlags = 0;
uint8_t	uiCrankOutControl = 0x7;  // [default] Enable the Digital Filter 

static void advertising_start(void * p_erase_bonds);


/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in]   line_num   Line number of the failing ASSERT call.
 * @param[in]   file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}


/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const * p_evt)
{
    bool delete_bonds = false;

    pm_handler_on_pm_evt(p_evt);
    pm_handler_flash_clean(p_evt);

    switch (p_evt->evt_id)
    {
        case PM_EVT_PEERS_DELETE_SUCCEEDED:
            advertising_start(&delete_bonds);
            break;

        default:
            break;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// The ble_log_mac_address() function retrieves the MAC address and forms a mask name.
//
// Parameter(s):
//
//	devname		string representing  mask name
//
// May terminate in APP_ERROR_CHECK
//
//////////////////////////////////////////////////////////////////////////////

static void ble_log_mac_address(uint8_t *devname)
{
	// Log our BLE address (6 bytes).
	static uint32_t err_code;
// Get BLE address.
#if (NRF_SD_BLE_API_VERSION >= 3)
	err_code = sd_ble_gap_addr_get(&MAC_Address);
#else
	err_code = sd_ble_gap_address_get(&MAC_Address);
#endif

	APP_ERROR_CHECK(err_code);

	// Because the NRF_LOG macros only support up to five varargs after the format string, we need to break this into two calls.
	NRF_LOG_RAW_INFO("\r\n---------------------------------------------------------------------\r\n");
	NRF_LOG_RAW_INFO("\r\nMAC Address = %02X:%02X:%02X:", MAC_Address.addr[5], MAC_Address.addr[4], MAC_Address.addr[3]);
	NRF_LOG_RAW_INFO("%02X:%02X:%02X\r\n", MAC_Address.addr[2], MAC_Address.addr[1], MAC_Address.addr[0]);

	if (devname != NULL)
		sprintf((char *)devname, "-%02X%02X%02X", MAC_Address.addr[2], MAC_Address.addr[1], MAC_Address.addr[0]);
}

uint16_t get_max_ble_len(void)
{
	return m_ble_nus_max_data_len;
}

/**@brief Function for handling the application measurement timer time-out.
 *
 * @details This function will be called each time the application level measurement timer expires.
 *
 * @param[in] xTimer Handler to the timer that called this function.
 *                   You may get identifier given to the function xTimerCreate using pvTimerGetTimerID.
 */
static void app_level_meas_timeout_handler(TimerHandle_t xTimer)
{
    UNUSED_PARAMETER(xTimer);

#if 0
//#if MUSH_FEATURE_ENABLED(APP_DEBUG)
    NRF_LOG_RAW_INFO("%s:\r\n", __func__); 
#if NRF_LOG_ENABLED
#if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
	nrf_log_notify();
#endif // MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
#endif // NRF_LOG_ENABLED	
#endif // MUSH_FEATURE_ENABLED(APP_DEBUG)
	uiGlobalTick++;
//	vAppTick();
}



/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timers_init(void)
{
    // Initialize timer module.
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    uiGlobalTick = 0; 		
    // Create timers.
    m_app_timer = xTimerCreate("APP",
                                   APP_TICK_INTERVAL,
                                   pdTRUE,
                                   NULL,
                                   app_level_meas_timeout_handler);

    /* Error checking */
    if ( NULL == m_app_timer) 
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
}


/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    ret_code_t              err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;
    uint8_t  devname[BLE_GAP_DEVNAME_MAX_LEN] ;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    strcpy((char * restrict)devname, DEVICE_NAME);
    ble_log_mac_address(&devname[strlen(DEVICE_NAME)]);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *)devname,
                                          strlen(DEVICE_NAME) +7);

    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling events from the GATT library. */
void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt)
{
    if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED))
    {
        m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
        NRF_LOG_RAW_INFO("Data len is set to 0x%X(%d)\r\n", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
    }
    NRF_LOG_RAW_INFO("ATT MTU exchange completed. central 0x%x peripheral 0x%x\r\n",
                  p_gatt->att_mtu_desired_central,
                  p_gatt->att_mtu_desired_periph);
   NRF_LOG_FLUSH();	
}

/**@brief Function for initializing the GATT library. */
void gatt_init(void)
{
    ret_code_t err_code;

    err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling the data from the Nordic UART Service.
 *
 * @details This function will process the data received from the Nordic UART BLE Service and send
 *          it to the UART module.
 *
 * @param[in] p_evt       Nordic UART Service event.
 */
/**@snippet [Handling the data received over BLE] */
static void spp_data_handler(ble_nus_evt_t * p_evt)
{

//    NRF_LOG_RAW_INFO("%s(): event=%d\r\n", __func__, p_evt->type);

    if (p_evt->type == BLE_NUS_EVT_RX_DATA)
    {
        uint32_t err_code = NRF_SUCCESS;

//        NRF_LOG_RAW_INFO("Received data from BLE NUS:\r\n");
//        NRF_LOG_RAW_HEXDUMP_INFO(p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);
//	 NRF_LOG_RAW_INFO("\r\n");

	 err_code = copy_cmd((uint8_t *)(p_evt->params.rx_data.p_data), p_evt->params.rx_data.length);
        if ((err_code != NRF_SUCCESS))
        {
              NRF_LOG_ERROR("Receiving not correct message from the Android app, ignored. Error 0x%x. ", err_code);
//              APP_ERROR_CHECK(err_code);
          }
	
#if NRF_MODULE_ENABLED(APP_UART)
        for (uint32_t i = 0; i < p_evt->params.rx_data.length; i++)
        {
            do
            {

                err_code = app_uart_put(p_evt->params.rx_data.p_data[i]);

                if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_BUSY))
                {
                    NRF_LOG_ERROR("Failed receiving NUS message. Error 0x%x. ", err_code);
                    APP_ERROR_CHECK(err_code);
                }
            } while (err_code == NRF_ERROR_BUSY);
        }
        if (p_evt->params.rx_data.p_data[p_evt->params.rx_data.length - 1] == '\r')
        {
            while (app_uart_put('\n') == NRF_ERROR_BUSY);
	     err_code = NRF_SUCCESS;	
        }
#endif //NRF_MODULE_ENABLED(APP_UART)
    }
#if MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
    else
    if (p_evt->type == BLE_NUS_EVT_COMM_STARTED) 
    {
    	NRF_LOG_RAW_INFO("%s(): SPP connection enabled ..\r\n", __func__);
// TODO: We can start Application from here in the case when connect to the ESP32
	vAppGotSppConnection();
    }else
    if (p_evt->type == BLE_NUS_EVT_COMM_STOPPED) 
    {
    	NRF_LOG_RAW_INFO("%s(): SPP connection disabled ..\r\n", __func__);
// TODO: We can start Application from here in the case when connect to the ESP32
    }else
#endif // MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
    else	
    if (p_evt->type == BLE_NUS_EVT_TX_RDY)
    {
	/* done with TX to the App, start a next one */    
//	NRF_LOG_RAW_INFO("-!");
    }
     	

}
/**@snippet [Handling the data received over BLE] */

ret_code_t spp_send_data(uint8_t *data, uint16_t length)
{
    uint32_t       err_code = NRF_SUCCESS;

 //   NRF_LOG_RAW_INFO("%s(): [%s]\r\n", __func__, (char*)data);

#if MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
                        err_code = ble_spp_data_send(&m_nus, data, &length, m_conn_handle);
                        if ((err_code != NRF_ERROR_INVALID_STATE) &&
                            (err_code != NRF_ERROR_RESOURCES) &&
                            (err_code != NRF_ERROR_NOT_FOUND))
                        {
                            APP_ERROR_CHECK(err_code);
                        }
#endif // MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
#if MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
                        err_code = ble_nus_data_send(&m_nus, data, &length, m_conn_handle);
                        if ((err_code != NRF_ERROR_INVALID_STATE) &&
                            (err_code != NRF_ERROR_RESOURCES) &&
                            (err_code != NRF_ERROR_NOT_FOUND))
                        {
                            APP_ERROR_CHECK(err_code);
                        }
#endif // MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
	return err_code;					
}

/**@brief Function for initializing services that will be used by the application.
 *
 * @details Initialize the Heart Rate, Battery and Device Information services.
 */
static void services_init(void)
{
    ret_code_t         err_code;
    nrf_ble_qwr_init_t qwr_init = {0};
    ble_nus_init_t     nus_init;

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;

    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);

    memset(&nus_init, 0, sizeof(nus_init));

#if MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
    nus_init.data_handler = spp_data_handler;

    err_code = ble_spp_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
#endif // MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
#if MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)
// [ADK]   The spp_data_handler() is equal to nus_data_handler() 
    nus_init.data_handler = spp_data_handler;

    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
#endif // MUSH_FEATURE_ENABLED(BLE_NUS_SERVICE)

}

/**@brief   Function for starting application timers.
 * @details Timers are run after the scheduler has started.
 */
static void application_timers_start(void)
{
    // Start application timers.

    if (pdPASS != xTimerStart(m_app_timer, APP_TICK_INTERVAL))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
}


/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in]   p_evt   Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    ret_code_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module. */
static void conn_params_init(void)
{
    ret_code_t             err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void)
{
    ret_code_t err_code;
#if MUSH_FEATURE_ENABLED(LSM6DS3)
#if MUSH_FEATURE_ENABLED(LSM6DS3_WKUP)
    // Prepare wakeup pin.
//    nrf_gpio_pin_set(DEBUG_LED_PIN);
    nrf_gpio_cfg_sense_set(LSM6DS3_INT1_PIN, NRF_GPIO_PIN_SENSE_HIGH);
    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    err_code = sd_power_system_off();
//    nrf_gpio_pin_clear(DEBUG_LED_PIN);
    APP_ERROR_CHECK(err_code);
#endif
#else

    err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    APP_ERROR_CHECK(err_code);

    // Prepare wakeup buttons.
    err_code = bsp_btn_ble_sleep_mode_prepare();
    APP_ERROR_CHECK(err_code);

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    err_code = sd_power_system_off();
    APP_ERROR_CHECK(err_code);
#endif	
}


/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
#if !MUSH_FEATURE_ENABLED(LSM6DS3)
    uint32_t err_code;
#endif
    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
            NRF_LOG_INFO("Fast advertising.");
#if MUSH_FEATURE_ENABLED(LSM6DS3)
            break;
#else
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
            APP_ERROR_CHECK(err_code);
            break;
#endif
        case BLE_ADV_EVT_IDLE:
            NRF_LOG_RAW_INFO("BLE_ADV_EVT_IDLE...\r\n");
	     NRF_LOG_FLUSH();
 //           sleep_mode_enter();
            break;

        default:
            break;
    }
}


/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
//static bool volatile m_conn_interval_configured;

static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    uint32_t err_code;
    ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;

//    NRF_LOG_RAW_INFO("%s() entered, evt_id 0x%x\r\n", __func__, p_ble_evt->header.evt_id);
//    NRF_LOG_FLUSH();
 	

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_RAW_INFO("\tConnected\r\n");
//            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
//            APP_ERROR_CHECK(err_code);
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);
	        vAppGotSppConnection();
	        break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_RAW_INFO("\tDisconnected\r\n");
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
      	    vAppSppDisconnect();
			sd_nvic_SystemReset();
            break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE:
        {
            //m_conn_interval_configured = true;
            NRF_LOG_INFO("Connection interval updated: 0x%x, 0x%x.",
                p_gap_evt->params.conn_param_update.conn_params.min_conn_interval,
                p_gap_evt->params.conn_param_update.conn_params.max_conn_interval);
        } break;


        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            NRF_LOG_DEBUG("GATT Client Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            NRF_LOG_DEBUG("GATT Server Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            break;
    }
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}


/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated by button press.
 */
static void bsp_event_handler(bsp_event_t event)
{
    ret_code_t err_code;
	NRF_LOG_RAW_INFO("%s entered\r\n", __func__);
	
    switch (event)
    {
        case BSP_EVENT_SLEEP:
            sleep_mode_enter();
            break;

        case BSP_EVENT_DISCONNECT:
            err_code = sd_ble_gap_disconnect(m_conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if (err_code != NRF_ERROR_INVALID_STATE)
            {
                APP_ERROR_CHECK(err_code);
            }
            break;

        case BSP_EVENT_WHITELIST_OFF:
            if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
            {
                err_code = ble_advertising_restart_without_whitelist(&m_advertising);
                if (err_code != NRF_ERROR_INVALID_STATE)
                {
                    APP_ERROR_CHECK(err_code);
                }
            }
            break;

        default:
            break;
    }
}


/**@brief Function for the Peer Manager initialization. */
static void peer_manager_init(void)
{
    ble_gap_sec_params_t sec_param;
    ret_code_t           err_code;

    err_code = pm_init();
    APP_ERROR_CHECK(err_code);

    memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

    // Security parameters to be used for all security procedures.
    sec_param.bond           = SEC_PARAM_BOND;
    sec_param.mitm           = SEC_PARAM_MITM;
    sec_param.lesc           = SEC_PARAM_LESC;
    sec_param.keypress       = SEC_PARAM_KEYPRESS;
    sec_param.io_caps        = SEC_PARAM_IO_CAPABILITIES;
    sec_param.oob            = SEC_PARAM_OOB;
    sec_param.min_key_size   = SEC_PARAM_MIN_KEY_SIZE;
    sec_param.max_key_size   = SEC_PARAM_MAX_KEY_SIZE;
    sec_param.kdist_own.enc  = 1;
    sec_param.kdist_own.id   = 1;
    sec_param.kdist_peer.enc = 1;
    sec_param.kdist_peer.id  = 1;

    err_code = pm_sec_params_set(&sec_param);
    APP_ERROR_CHECK(err_code);

    err_code = pm_register(pm_evt_handler);
    APP_ERROR_CHECK(err_code);
}


/**@brief Clear bond information from persistent storage. */
static void delete_bonds(void)
{
    ret_code_t err_code;

    NRF_LOG_INFO("Erase bonds!");

    err_code = pm_peers_delete();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Advertising functionality. */
static void advertising_init(void)
{
    ret_code_t             err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));

    init.advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance      = true;
    init.advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
//    init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
//    init.advdata.uuids_complete.p_uuids  = m_adv_uuids;
    init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids  = m_adv_uuids;

    init.config.ble_adv_fast_enabled  = true;
    init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;

    init.evt_handler = on_adv_evt;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}


/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}


/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void buttons_leds_init(bool * p_erase_bonds)
{
    ret_code_t err_code;
    bsp_event_t startup_event;

#if !APP_USE_DK_BOARD
	*p_erase_bonds = false;
	return;
#endif	
#if MUSH_FEATURE_ENABLED(LSM6DS3)
#if MUSH_FEATURE_ENABLED(LSM6DS3_WKUP)
	*p_erase_bonds = true;
	return;
#endif
#endif
    err_code = bsp_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS, bsp_event_handler);
    APP_ERROR_CHECK(err_code);

    err_code = bsp_btn_ble_init(NULL, &startup_event);
    APP_ERROR_CHECK(err_code);

    *p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);
}


/**@brief Function for starting advertising. */
static void advertising_start(void * p_erase_bonds)
{
    bool erase_bonds = *(bool*)p_erase_bonds;

    if (erase_bonds)
    {
        delete_bonds();
        // Advertising is started by PM_EVT_PEERS_DELETE_SUCCEEDED event.
    }
    else
    {
        ret_code_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
        APP_ERROR_CHECK(err_code);
    }
}


#if NRF_LOG_ENABLED
#if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
void nrf_log_notify(void) 
{
	if(m_logger_thread != NULL)
	{
		xTaskNotifyGive(m_logger_thread);
	}
}
#endif // MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)

/**@brief Thread for handling the logger.
 *
 * @details This thread is responsible for processing log entries if logs are deferred.
 *          Thread flushes all log entries and suspends. It is resumed by idle task hook.
 *
 * @param[in]   arg   Pointer used for passing some arbitrary information (context) from the
 *                    osThreadCreate() call to the thread.
 */
static void logger_thread(void * arg)
{
    UNUSED_PARAMETER(arg);

    while (1)
    {
 #if MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
//#warning "######### LOG Notify #########\n" 
	(void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        NRF_LOG_FLUSH();
#else
       NRF_LOG_FLUSH();

        vTaskSuspend(NULL); // Suspend myself
#endif	
    }
}
#endif //NRF_LOG_ENABLED

/**@brief A function which is hooked to idle task.
 * @note Idle hook must be enabled in FreeRTOS configuration (configUSE_IDLE_HOOK).
 */
void vApplicationIdleHook( void )
{
#if NRF_LOG_ENABLED
#if !MUSH_FEATURE_ENABLED(LOG_TASK_NOTIFICATION)
     vTaskResume(m_logger_thread);
#endif
#endif
}


/**@brief Function for initializing the clock.
 */
static void clock_init(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
}

#if MUSH_FEATURE_ENABLED(LSM6DS3)
#if MUSH_FEATURE_ENABLED(LSM6DS3_WKUP)

static void in_pin_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
//    nrfx_gpiote_out_toggle(DEBUG_LED_PIN);
}
#endif // MUSH_FEATURE_ENABLED(LSM6DS3_WKUP)
#endif // MUSH_FEATURE_ENABLED(LSM6DS3)

/**
 * @brief Function for configuring: PIN_IN pin for input, PIN_OUT pin for output,
 * and configures GPIOTE to give an interrupt on pin change.
 */
static void gpio_init(void)
{
    ret_code_t err_code;

    err_code = nrfx_gpiote_init();
    APP_ERROR_CHECK(err_code);

    nrfx_gpiote_out_config_t out_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(false);

//    err_code = nrfx_gpiote_out_init(DEBUG_LED_PIN, &out_config);
//    APP_ERROR_CHECK(err_code);
#if MUSH_FEATURE_ENABLED(AD8237)
    err_code = nrfx_gpiote_out_init(ADC_POWER_PIN, &out_config);
    APP_ERROR_CHECK(err_code);
    err_code = nrfx_gpiote_out_init(ADC_VREF_PIN, &out_config);
    APP_ERROR_CHECK(err_code);
#ifdef DEBUG_SAADC_PIN
    err_code = nrfx_gpiote_out_init(DEBUG_SAADC_PIN, &out_config);
    APP_ERROR_CHECK(err_code);
#endif // DEBUG_SAADC_PIN
#endif // MUSH_FEATURE_ENABLED(AD8237)
#if MUSH_FEATURE_ENABLED(LSM6DS3)
#if MUSH_FEATURE_ENABLED(LSM6DS3_WKUP)

    nrfx_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(true);
    in_config.pull = NRF_GPIO_PIN_PULLUP;

    err_code = nrfx_gpiote_in_init(LSM6DS3_INT1_PIN, &in_config, in_pin_handler);
    APP_ERROR_CHECK(err_code);

//    nrf_drv_gpiote_in_event_enable(LSM6DS3_INT1_PIN, true);
#endif // MUSH_FEATURE_ENABLED(LSM6DS3_WKUP)
#endif // MUSH_FEATURE_ENABLED(LSM6DS3)
}


#if MUSH_FEATURE_ENABLED(AD8237)
void timer4_handler(nrf_timer_event_t event_type, void * p_context)
{
}
///////////////////////////////////////////////////////
// ADC configuration for scan mode
// See: https://github.com/NordicPlayground/nRF52-ADC-examples/blob/master/ble_app_uart__saadc_timer_driven__scan_mode/main.c
//         https://devzone.nordicsemi.com/f/nordic-q-a/14777/ble_app_proximity-saadc-channel-data#post-id-85497
///////////////////////////////////////////////////////
static void saadc_ainX_init(void)
{
    ret_code_t err_code;
    uint32_t ticks;
    uint32_t timer_compare_event_addr;
    uint32_t saadc_sample_task_addr;
	
    nrf_drv_timer_config_t timer_cfg = NRF_DRV_TIMER_DEFAULT_CONFIG;
    timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
    err_code = nrf_drv_timer_init(&m_timer4, &timer_cfg, timer4_handler);
    APP_ERROR_CHECK(err_code);

#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
      ticks = nrf_drv_timer_ms_to_ticks(&m_timer4, ADC_SAMPLING_TIME_IN_MSEC);   // 100ms sampling
//	ticks = 20000;
    
//    ticks = nrf_drv_timer_ms_to_ticks(&m_timer4, 1000 /*400 */);   // 1 sec for debugging, need a configuration ..
#else
    /* setup m_timer4 cc0 for compare event every 1000/400ms */
    ticks = nrf_drv_timer_ms_to_ticks(&m_timer4, 1000 /*400 */);   // 1 sec for debugging, need a configuration ..
#endif // MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
//NRF_LOG_RAW_INFO("\tTimer4=[%d] ticks\r\n", ticks);
    nrf_drv_timer_extended_compare(&m_timer4,
                                   NRF_TIMER_CC_CHANNEL0,
                                   ticks,
                                   NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
                                   false);

    nrf_drv_timer_enable(&m_timer4);
    saadc_sample_task_addr   = nrf_drv_saadc_sample_task_get();
    timer_compare_event_addr = nrf_drv_timer_compare_event_address_get(&m_timer4, NRF_TIMER_CC_CHANNEL0);

    /* setup ppi channel so that timer compare event is triggering sample task in SAADC */
    err_code = nrf_drv_ppi_channel_alloc(&m_ppi_channel);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_ppi_channel_assign(m_ppi_channel,
                                          timer_compare_event_addr,
                                          saadc_sample_task_addr);
    APP_ERROR_CHECK(err_code);
}

void saadc_event_ain_enable(void)
{
    ret_code_t err_code;	

#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
#else
#ifdef DEBUG_SAADC_PIN
#else
    nrf_gpio_pin_set(ADC_POWER_PIN);
//    nrf_gpio_pin_set(ADC_VREF_PIN);
#endif // DEBUG_SAADC_PIN

#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
    err_code = nrf_drv_ppi_channel_enable(m_ppi_channel);

    APP_ERROR_CHECK(err_code);
}


void saadc_ain_callback(nrf_drv_saadc_evt_t const * p_event)
{
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
    {
        ret_code_t err_code;
	 uint8_t idx, idy;	
#if MUSH_FEATURE_ENABLED(SAADC_AIN0)
        uint32_t adc_value0 = 0;
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN0)
#if MUSH_FEATURE_ENABLED(SAADC_AIN1)
        uint32_t adc_value1 = 0;
//	 float adc_volts1;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN1)
#if MUSH_FEATURE_ENABLED(SAADC_AIN2)
        uint32_t adc_value2 = 0;
//	 float adc_volts3;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN2)
#if MUSH_FEATURE_ENABLED(SAADC_AIN3)
        uint32_t adc_value3 = 0;
//	 float adc_volts3;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN3)
#if MUSH_FEATURE_ENABLED(SAADC_AIN4)
        uint32_t adc_value4 = 0;
//	 float adc_volts4;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN4)
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
        uint32_t adc_value5 = 0;
//	 float adc_volts5;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN5)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
        uint32_t adc01_value = 0;
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
        uint32_t adc23_value = 0;
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#else
	 float adc_volts;	
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN23)

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, ADC_POOL_SIZE);
        APP_ERROR_CHECK(err_code);

       // print samples on hardware UART and parse data for BLE transmission
//        NRF_LOG_RAW_INFO("ADC event number: %d\r\n",(int)m_adc_evt_counter);
	 idx = 0;
	 idy = 0;
	 while(idx < ADC_POOL_SIZE)
	 {

#if MUSH_FEATURE_ENABLED(SAADC_AIN0)
//	     value_ain0[ idy] = p_event->data.done.p_buffer[idx];
//            NRF_LOG_RAW_INFO("\tAIN0[%d]=[%d]0x%04x\r\n", idy, idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value0 += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN0)
#if MUSH_FEATURE_ENABLED(SAADC_AIN1)
//	     value_ain0[ idy] = p_event->data.done.p_buffer[idx];
//            NRF_LOG_RAW_INFO("\tAIN1[%d]=[%d]0x%04x\r\n", idy, idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value1 += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN1)
#if MUSH_FEATURE_ENABLED(SAADC_AIN2)
//	     value_ain2[ idy] = p_event->data.done.p_buffer[idx];
//            NRF_LOG_RAW_INFO("\tAIN2[%d]=[%d]0x%04x\r\n", idy, idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value2 += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN2)
#if MUSH_FEATURE_ENABLED(SAADC_AIN3)
//	     value_ain0[ idy] = p_event->data.done.p_buffer[idx];
//            NRF_LOG_RAW_INFO("\tAIN3[%d]=[%d]0x%04x\r\n", idy, idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value3 += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN3)
#if MUSH_FEATURE_ENABLED(SAADC_AIN4)
//	     value_ain0[ idy] = p_event->data.done.p_buffer[idx];
//            NRF_LOG_RAW_INFO("\tAIN4[%d]=[%d]0x%04x\r\n", idy, idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value4 += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN4)
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
//	     value_ain0[ idy] = p_event->data.done.p_buffer[idx];
//            NRF_LOG_RAW_INFO("\tAIN5[%d]=[%d]0x%04x\r\n", idy, idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value5 += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN5)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
//            NRF_LOG_RAW_INFO("\tAIN13[%d]=0x%04x\r\n", idx, p_event->data.done.p_buffer[idx]);
	     if ((p_event->data.done.p_buffer[idx] & (~0xFFF)) == 0) {
		 	adc_value += (p_event->data.done.p_buffer[idx] & 0xFFF);
	     }
//		 else
//		 	adc_value += ((~p_event->data.done.p_buffer[idx]) & 0xFFF);
		 
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
//            NRF_LOG_RAW_INFO("\tAIN: 01[%d]=0x%04x, 23[%d]=0x%04x\r\n", idx, p_event->data.done.p_buffer[idx], idx+1, p_event->data.done.p_buffer[idx +1]);
//            NRF_LOG_RAW_INFO("\tAIN01[%d]=0x%04x\r\n", idx, p_event->data.done.p_buffer[idx]);
	     if (((p_event->data.done.p_buffer[idx] & (~0x07FF)) == 0)  && ((p_event->data.done.p_buffer[idx] & 0x7FF) != 0x7FF)) {
		 	adc01_value += (p_event->data.done.p_buffer[idx] & 0x07FF);
	     }
		 
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
//            NRF_LOG_RAW_INFO("\tAIN23[%d]=0x%04x\r\n", idx, p_event->data.done.p_buffer[idx]);
	     if (((p_event->data.done.p_buffer[idx] & (~0x07FF)) == 0) && ((p_event->data.done.p_buffer[idx] & 0x7FF) != 0x7FF)) {
		 	adc23_value += (p_event->data.done.p_buffer[idx] & 0x07FF);
	     }
		 
	     idx++;	
#endif //MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)

	    idy++; 	
	 }

#ifdef DEBUG_SAADC_PIN
	nrf_gpio_pin_toggle(DEBUG_SAADC_PIN);
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
	if (samples_rb_free_size((void *)&rb_cntrl))
		samples_rb_write((void *)&rb_cntrl, adc_value4/ADC_SAMPLES_IN_BUFFER, adc_value5/ADC_SAMPLES_IN_BUFFER);
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN5)
#if MUSH_FEATURE_ENABLED(SAADC_AIN1)
	if (samples_rb_free_size((void *)&rb_cntrl))
		samples_rb_write((void *)&rb_cntrl, adc_value1/ADC_SAMPLES_IN_BUFFER, adc_value3/ADC_SAMPLES_IN_BUFFER);
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN5)
	
#else  // DEBUG_SAADC_PIN	
//	NRF_LOG_RAW_INFO("adc_value=%d\r\n", adc_value);
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
	 adc_value13/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts =(adc_value13/2048.0)*0.6; 
      	NRF_LOG_RAW_INFO("\tadc="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts));
#else
#if 0
#if MUSH_FEATURE_ENABLED(SAADC_AIN0)
	 adc_value0/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts =(adc_value0/4096.0)*3.6; 
	 adc_volts /= 151.0;
      	NRF_LOG_RAW_INFO("\tadc="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts));
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN0)
#if MUSH_FEATURE_ENABLED(SAADC_AIN1)
	 adc_value1/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts1 =(adc_value1/4096.0)*3.6; 
#if MUSH_FEATURE_ENABLED(SAADC_AIN3)
      	NRF_LOG_RAW_INFO("\tadc1="MUSHIN_LOG_FLOAT_MARKER"V, ", MUSHIN_LOG_FLOAT(adc_volts1));
#else
      	NRF_LOG_RAW_INFO("\tadc1="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts1));
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN3)
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN1)
#if MUSH_FEATURE_ENABLED(SAADC_AIN3)
	 adc_value3/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts3 =(adc_value3/4096.0)*3.6; 
      	NRF_LOG_RAW_INFO("adc3="MUSHIN_LOG_FLOAT_MARKER"V", MUSHIN_LOG_FLOAT(adc_volts3));
      	NRF_LOG_RAW_INFO("\tsig="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT((adc_volts3 - adc_volts1)));
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN3)
#endif // 0
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
	if (samples_rb_free_size((void *)&rb_cntrl))
		samples_rb_write((void *)&rb_cntrl, adc_value0/ADC_SAMPLES_IN_BUFFER, adc_value1/ADC_SAMPLES_IN_BUFFER,
			adc_value2/ADC_SAMPLES_IN_BUFFER, adc_value3/ADC_SAMPLES_IN_BUFFER
		);
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#endif //MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
	if (samples_rb_free_size((void *)&rb_cntrl))
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
		samples_rb_write((void *)&rb_cntrl, adc01_value/ADC_SAMPLES_IN_BUFFER, adc23_value/ADC_SAMPLES_IN_BUFFER);
#else
		samples_rb_write((void *)&rb_cntrl, adc23_value/ADC_SAMPLES_IN_BUFFER, 0/*adc_value5/ADC_SAMPLES_IN_BUFFER */);
#endif //MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
#else //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
	 adc23_value/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts =(adc23_value/2048.0)*1.8; 
      	NRF_LOG_RAW_INFO("\tdiff23="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts));
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#else // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
	if (samples_rb_free_size((void *)&rb_cntrl))
		samples_rb_write((void *)&rb_cntrl, adc01_value/ADC_SAMPLES_IN_BUFFER, 0);
#else //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
	 adc01_value/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts =(adc01_value/2048.0)*1.8; 
      	NRF_LOG_RAW_INFO("\tdiff01="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts));
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#endif //MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)

#if MUSH_FEATURE_ENABLED(SAADC_AIN4)
	 adc_value4/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts4 =(adc_value4/4096.0)*3.6; 
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
      	NRF_LOG_RAW_INFO("\tadc4="MUSHIN_LOG_FLOAT_MARKER"V, ", MUSHIN_LOG_FLOAT(adc_volts4));
#else
      	NRF_LOG_RAW_INFO("\tadc4="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT(adc_volts4));
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN5)
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN4)
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
	 adc_value5/=ADC_SAMPLES_IN_BUFFER;
	 adc_volts5 =(adc_value5/4096.0)*3.6; 
      	NRF_LOG_RAW_INFO("adc5="MUSHIN_LOG_FLOAT_MARKER"V", MUSHIN_LOG_FLOAT(adc_volts5));
      	NRF_LOG_RAW_INFO("\tsig="MUSHIN_LOG_FLOAT_MARKER"V\r\n", MUSHIN_LOG_FLOAT((adc_volts5 - adc_volts4)));
#endif //MUSH_FEATURE_ENABLED(SAADC_AIN5)
#endif // DEBUG_SAADC_PIN

//	NRF_LOG_RAW_INFO("int val=%05d\r\n", (int32_t)((adc_volts)*100000.0));


/*
        for (idx =0; ; idx++)
        {
            adc_value = p_event->data.done.p_buffer[i];
            value[i*2] = adc_value;
            value[(i*2)+1] = adc_value >> 8;
        }
*/            
        m_adc_evt_counter++;
    }
}

void saadc_init(void)
{
    ret_code_t err_code;
    uint8_t channel = 0;	

    nrf_drv_saadc_config_t saadc_config = NRF_DRV_SAADC_DEFAULT_CONFIG;
    saadc_config.resolution = NRF_SAADC_RESOLUTION_12BIT;
	
#if MUSH_FEATURE_ENABLED(SAADC_AIN0)
    nrf_saadc_channel_config_t channel_config_ain0 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN0);
        channel_config_ain0.gain = NRF_SAADC_GAIN1_6;
//    channel_config_ain0.reference = NRF_SAADC_REFERENCE_VDD4;
#else
    nrf_gpio_cfg_input(2, NRF_GPIO_PIN_NOPULL);
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN0)

#if MUSH_FEATURE_ENABLED(SAADC_AIN1)
    nrf_saadc_channel_config_t channel_config_ain1 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN1);
    	channel_config_ain1.gain = NRF_SAADC_GAIN1_6;
//    channel_config_ain0.reference = NRF_SAADC_REFERENCE_VDD4;
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN1)

#if MUSH_FEATURE_ENABLED(SAADC_AIN2)
    nrf_saadc_channel_config_t channel_config_ain2 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN2);
        channel_config_ain2.gain = NRF_SAADC_GAIN1_6;
//    channel_config_ain2.reference = NRF_SAADC_REFERENCE_VDD4;
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN2)
#if MUSH_FEATURE_ENABLED(SAADC_AIN3)
    nrf_saadc_channel_config_t channel_config_ain3 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN3);
    	channel_config_ain3.gain = NRF_SAADC_GAIN1_6;
//    channel_config_ain0.reference = NRF_SAADC_REFERENCE_VDD4;
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN3)
#if MUSH_FEATURE_ENABLED(SAADC_AIN4)
    nrf_saadc_channel_config_t channel_config_ain4 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN4);
    	channel_config_ain4.gain = NRF_SAADC_GAIN1_2;
//    channel_config_ain2.reference = NRF_SAADC_REFERENCE_VDD4;
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN4)
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
    nrf_saadc_channel_config_t channel_config_ain5 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN5);
    	channel_config_ain5.gain = NRF_SAADC_GAIN1_2;
//    channel_config_ain0.gain = NRF_SAADC_GAIN1_4;
//    channel_config_ain0.reference = NRF_SAADC_REFERENCE_VDD4;
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN5)

#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
    nrf_saadc_channel_config_t channel_config_ain13 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_DIFFERENTIAL(NRF_SAADC_INPUT_AIN1, NRF_SAADC_INPUT_AIN3);
    channel_config_ain13.gain = NRF_SAADC_GAIN1;
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
    nrf_saadc_channel_config_t channel_config_ain01 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_DIFFERENTIAL(NRF_SAADC_INPUT_AIN0, NRF_SAADC_INPUT_AIN1);
    channel_config_ain01.gain = NRF_SAADC_GAIN1_3;
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
    nrf_saadc_channel_config_t channel_config_ain23 =
        NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_DIFFERENTIAL(NRF_SAADC_INPUT_AIN3, NRF_SAADC_INPUT_AIN2);
    channel_config_ain23.gain = NRF_SAADC_GAIN1_3;
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)


    err_code = nrf_drv_saadc_init(&saadc_config, saadc_ain_callback);
    APP_ERROR_CHECK(err_code);

#if MUSH_FEATURE_ENABLED(SAADC_AIN0)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain0);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN0)

#if MUSH_FEATURE_ENABLED(SAADC_AIN1)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain1);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN0)

#if MUSH_FEATURE_ENABLED(SAADC_AIN2)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain2);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN2)
#if MUSH_FEATURE_ENABLED(SAADC_AIN3)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain3);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN0)
#if MUSH_FEATURE_ENABLED(SAADC_AIN4)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain4);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN4)
#if MUSH_FEATURE_ENABLED(SAADC_AIN5)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain5);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_AIN5)

#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain13);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN13)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain23);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN23)
#if MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)
    err_code = nrf_drv_saadc_channel_init(channel, &channel_config_ain01);
    APP_ERROR_CHECK(err_code);
    channel++;	
#endif // MUSH_FEATURE_ENABLED(SAADC_DIFF_AIN01)

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_ain_pool[0], ADC_SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_ain_pool[1], ADC_SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_ppi_init();
    APP_ERROR_CHECK(err_code);

}

#endif // MUSH_FEATURE_ENABLED(AD8237)



/**@brief Function for application main entry.
 */
int main(void)
{
    bool erase_bonds;

    // Initialize modules.
    log_init();
    clock_init();

    // Do not start any interrupt that uses system functions before system initialisation.
    // The best solution is to start the OS before any other initalisation.

#if NRF_LOG_ENABLED
    // Start execution.
    if (pdPASS != xTaskCreate(logger_thread, "LOGGER", 256, NULL, 1, &m_logger_thread))
    {
        APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
    }
#endif

    // Activate deep sleep mode.
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
	
	NRF_LOG_RAW_INFO("\r\n**** muShin Sensor Version 1.2 **\n");

	NRF_LOG_RAW_INFO("Print/Send Delay = %d Hz\n", 1000/PRINT_SEND_DELAY_IN_MSEC);
	NRF_LOG_RAW_INFO("Tick Timer Frequency = %d Hz\n", 1000 /APP_TICK_INTERVAL);
	NRF_LOG_RAW_INFO("Data Acquisition Frequency = %d Hz\n", 1000 /ADC_SAMPLING_TIME_IN_MSEC);
	NRF_LOG_FLUSH();

    // Configure and initialize the BLE stack.
    ble_stack_init();

    // Initialize modules.
    timers_init();
    buttons_leds_init(&erase_bonds);
    gpio_init();
    gap_params_init();
    gatt_init();
    services_init();
    advertising_init();
    conn_params_init();
    peer_manager_init();
    application_timers_start();

#if MUSH_FEATURE_ENABLED(AD8237)
    saadc_ainX_init();
    saadc_init();
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
    samples_rb_init(&rb_cntrl);	
#endif // MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
    saadc_event_ain_enable();
#endif // MUSH_FEATURE_ENABLED(AD8237)

	///////////////////////////////////////////////////////////////////////////////////////////////
	// Create application tasks. 
	///////////////////////////////////////////////////////////////////////////////////////////////

#if MUSH_FEATURE_ENABLED(TWI0) /* || NRF_MODULE_ENABLED(TWI1) */
#if	MUSH_FEATURE_ENABLED(MEMORY_DEBUG)
	if (mush_memory_debug_flag) {
		NRF_LOG_RAW_INFO("#%d  ==== TWI0 =====\r\n", mush_task_start_idx);
		mush_task_start_idx++;
	}
#endif // MEMORY_DEBUG
	vTWI0_MasterStart(TWI0_STACKSIZE, 	TWI0_PRIORITY); 
#endif
#if MUSH_FEATURE_ENABLED(APP)
#if	MUSH_FEATURE_ENABLED(MEMORY_DEBUG)
	if (mush_memory_debug_flag) {
		NRF_LOG_RAW_INFO("#%d:  ==== APP =====\r\n", mush_task_start_idx);
        	NRF_LOG_FLUSH();
		mush_task_start_idx++;
	}
#endif // MEMORY_DEBUG
#if MUSH_FEATURE_ENABLED(FREERTOS_TASK_STATS)
// FreeRTOS tasks stats required a big size for stack.
	vAppStart((APP_STACKSIZE *8), APP_PRIORITY);
#else
	vAppStart((APP_STACKSIZE*4), APP_PRIORITY);
#endif // MUSH_FEATURE_ENABLED(FREERTOS_TASK_STATS)

#endif
#if MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
#if	MUSH_FEATURE_ENABLED(MEMORY_DEBUG)
	if (mush_memory_debug_flag) {
		NRF_LOG_RAW_INFO("#%d:  ==== Samples =====\r\n", mush_task_start_idx);
        	NRF_LOG_FLUSH();
		mush_task_start_idx++;
	}
#endif // MEMORY_DEBUG
	vSamplesStart((APP_STACKSIZE *4), APP_PRIORITY);
#endif //MUSH_FEATURE_ENABLED(APP_RINGBUFFER)
	

#if MUSH_FEATURE_ENABLED(LSM6DS3)
#if	MUSH_FEATURE_ENABLED(MEMORY_DEBUG)
	if (mush_memory_debug_flag) {
		NRF_LOG_RAW_INFO("#%d:  ==== LSM6DS3 =====\r\n", mush_task_start_idx);
        	NRF_LOG_FLUSH();
		mush_task_start_idx++;
	}
#endif // MEMORY_DEBUG
	vLSM6DS3Start((LSM6DS3_STACKSIZE), LSM6DS3_PRIORITY);
#endif //MUSH_FEATURE_ENABLED(LSM6DS3)

#ifdef DO_OUTTASK
	vOutStart((APP_STACKSIZE *4), APP_PRIORITY);
#endif

    // Create a FreeRTOS task for the BLE stack.
    // The task will run advertising_start() before entering its loop.
    nrf_sdh_freertos_init(advertising_start, &erase_bonds);

    NRF_LOG_RAW_INFO("muShin: FreeRTOS started SDK 17\r\n");
    // Start FreeRTOS scheduler.
    vTaskStartScheduler();

    for (;;)
    {
        APP_ERROR_HANDLER(NRF_ERROR_FORBIDDEN);
    }
	
}


