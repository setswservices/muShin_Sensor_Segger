/**
 * Copyright (c) 2012 - 2019, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "sdk_common.h"
#if MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)
#include "ble.h"
#include "ble_spp.h"
#include "ble_srv_common.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

/* ---
#define NRF_LOG_MODULE_NAME ble_spp
#if BLE_NUS_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       BLE_NUS_CONFIG_LOG_LEVEL
#define NRF_LOG_INFO_COLOR  BLE_NUS_CONFIG_INFO_COLOR
#define NRF_LOG_DEBUG_COLOR BLE_NUS_CONFIG_DEBUG_COLOR
#else // BLE_NUS_CONFIG_LOG_ENABLED
#define NRF_LOG_LEVEL       0
#endif // BLE_NUS_CONFIG_LOG_ENABLED
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();
-- */


#define BLE_UUID_NUS_RX_CHARACTERISTIC 0xABF1               /**< The UUID of the RX Characteristic. */
#define BLE_UUID_NUS_TX_CHARACTERISTIC 0xABF2               /**< The UUID of the TX Characteristic. */
#define BLE_UUID_NUS_RX_CMD_CHARACTERISTIC 0xABF3               /**< The UUID of the RX Command Characteristic. */
#define BLE_UUID_NUS_TX_CMD_CHARACTERISTIC 0xABF4               /**< The UUID of the TX Command Characteristic. */

#define BLE_NUS_MAX_RX_CHAR_LEN        BLE_NUS_MAX_DATA_LEN /**< Maximum length of the RX Characteristic (in bytes). */
#define BLE_NUS_MAX_TX_CHAR_LEN        BLE_NUS_MAX_DATA_LEN /**< Maximum length of the TX Characteristic (in bytes). */

#define SPP_BASE_UUID                  {{0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x00, 0x00, 0x40, 0x6E}} /**< Used vendor specific UUID. */

/**@brief Function for handling the @ref BLE_GAP_EVT_CONNECTED event from the SoftDevice.
 *
 * @param[in] p_nus     Nordic UART Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_connect(ble_nus_t * p_nus, ble_evt_t const * p_ble_evt)
{
    ret_code_t                 err_code;
    ble_nus_evt_t              evt;
    ble_gatts_value_t          gatts_val;
    uint8_t                    cccd_value[2];
    ble_nus_client_context_t * p_client = NULL;

    err_code = blcm_link_ctx_get(p_nus->p_link_ctx_storage,
                                 p_ble_evt->evt.gap_evt.conn_handle,
                                 (void *) &p_client);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Link context for 0x%02X connection handle could not be fetched.",
                      p_ble_evt->evt.gap_evt.conn_handle);
    }

    /* Check the hosts CCCD value to inform of readiness to send data using the RX characteristic */
    memset(&gatts_val, 0, sizeof(ble_gatts_value_t));
    gatts_val.p_value = cccd_value;
    gatts_val.len     = sizeof(cccd_value);
    gatts_val.offset  = 0;

    err_code = sd_ble_gatts_value_get(p_ble_evt->evt.gap_evt.conn_handle,
                                      p_nus->rx_handles.cccd_handle,
                                      &gatts_val);

    if ((err_code == NRF_SUCCESS)     &&
        (p_nus->data_handler != NULL) &&
        ble_srv_is_notification_enabled(gatts_val.p_value))
    {
        if (p_client != NULL)
        {
            p_client->is_notification_enabled = true;
        }

        memset(&evt, 0, sizeof(ble_nus_evt_t));
        evt.type        = BLE_NUS_EVT_COMM_STARTED;
        evt.p_nus       = p_nus;
        evt.conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        evt.p_link_ctx  = p_client;

        p_nus->data_handler(&evt);
    }
}


/**@brief Function for handling the @ref BLE_GATTS_EVT_WRITE event from the SoftDevice.
 *
 * @param[in] p_nus     Nordic UART Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_write(ble_nus_t * p_nus, ble_evt_t const * p_ble_evt)
{
    ret_code_t                    err_code;
    ble_nus_evt_t                 evt;
    ble_nus_client_context_t    * p_client;
    ble_gatts_evt_write_t const * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    NRF_LOG_RAW_INFO("on_write: UUID=%04x\r\n", p_evt_write->uuid.uuid);
	
    err_code = blcm_link_ctx_get(p_nus->p_link_ctx_storage,
                                 p_ble_evt->evt.gatts_evt.conn_handle,
                                 (void *) &p_client);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("Link context for 0x%02X connection handle could not be fetched.",
                      p_ble_evt->evt.gatts_evt.conn_handle);
    }
    NRF_LOG_RAW_INFO("on_write: p_client=%p\r\n", p_client);
    memset(&evt, 0, sizeof(ble_nus_evt_t));
    evt.p_nus       = p_nus;
    evt.conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
    evt.p_link_ctx  = p_client;

NRF_LOG_RAW_INFO("on write: (%d): write_handle=%04x\r\n", p_evt_write->len, p_evt_write->handle);

    if ((p_evt_write->handle == p_nus->tx_handles.cccd_handle) &&
        (p_evt_write->len == 2))
    {
        if (p_client != NULL)
        {
	if (ble_srv_is_notification_enabled(p_evt_write->data))
            {
NRF_LOG_RAW_INFO("on write: tx notification  \r\n");
                p_client->is_notification_enabled = true;
                evt.type                          = BLE_NUS_EVT_COMM_STARTED;
            }
            else
            {
                p_client->is_notification_enabled = false;
                evt.type                          = BLE_NUS_EVT_COMM_STOPPED;
            }

            if (p_nus->data_handler != NULL)
            {
                p_nus->data_handler(&evt);
            }

        }
    }
    else if ((p_evt_write->handle == p_nus->rx_handles.value_handle) &&
             (p_nus->data_handler != NULL))
    {
NRF_LOG_RAW_INFO("on write: >>> \r\n");
        evt.type                  = BLE_NUS_EVT_RX_DATA;
        evt.params.rx_data.p_data = p_evt_write->data;
        evt.params.rx_data.length = p_evt_write->len;

        p_nus->data_handler(&evt);
    }
    else
    {
        // Do Nothing. This event is not relevant for this service.
    }
}


/**@brief Function for handling the @ref BLE_GATTS_EVT_HVN_TX_COMPLETE event from the SoftDevice.
 *
 * @param[in] p_nus     Nordic UART Service structure.
 * @param[in] p_ble_evt Pointer to the event received from BLE stack.
 */
static void on_hvx_tx_complete(ble_nus_t * p_nus, ble_evt_t const * p_ble_evt)
{
    ret_code_t                 err_code;
    ble_nus_evt_t              evt;
    ble_nus_client_context_t * p_client;

    err_code = blcm_link_ctx_get(p_nus->p_link_ctx_storage,
                                 p_ble_evt->evt.gatts_evt.conn_handle,
                                 (void *) &p_client);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Link context for 0x%02X connection handle could not be fetched.",
                      p_ble_evt->evt.gatts_evt.conn_handle);
        return;
    }

    if (p_client->is_notification_enabled)
    {
        memset(&evt, 0, sizeof(ble_nus_evt_t));
        evt.type        = BLE_NUS_EVT_TX_RDY;
        evt.p_nus       = p_nus;
        evt.conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
        evt.p_link_ctx  = p_client;

        p_nus->data_handler(&evt);
    }
}


void ble_spp_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    if ((p_context == NULL) || (p_ble_evt == NULL))
    {
        return;
    }

    ble_nus_t * p_nus = (ble_nus_t *)p_context;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            on_connect(p_nus, p_ble_evt);
            break;

        case BLE_GATTS_EVT_WRITE:
            on_write(p_nus, p_ble_evt);
            break;

        case BLE_GATTS_EVT_HVN_TX_COMPLETE:
            on_hvx_tx_complete(p_nus, p_ble_evt);
            break;

        default:
            // No implementation needed.
            break;
    }
}


uint32_t ble_spp_init(ble_nus_t * p_nus, ble_nus_init_t const * p_nus_init)
{
    ret_code_t            err_code;
    ble_uuid_t            ble_uuid;
    ble_uuid128_t         nus_base_uuid = SPP_BASE_UUID;

    ble_add_char_params_t add_char_params;

    VERIFY_PARAM_NOT_NULL(p_nus);
    VERIFY_PARAM_NOT_NULL(p_nus_init);


    // Initialize the service structure.
    p_nus->data_handler = p_nus_init->data_handler;

    /**@snippet [Adding proprietary Service to the SoftDevice] */
    // Add a custom base UUID.
    err_code = sd_ble_uuid_vs_add(&nus_base_uuid, &p_nus->uuid_type);
    VERIFY_SUCCESS(err_code);

    ble_uuid.type = p_nus->uuid_type;
    ble_uuid.uuid = BLE_UUID_SPP_SERVICE;

    // Add the service.
    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
                                        &ble_uuid,
                                        &p_nus->service_handle);
    /**@snippet [Adding proprietary Service to the SoftDevice] */
    VERIFY_SUCCESS(err_code);

    // Add the RX Characteristic.
    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid                     = BLE_UUID_NUS_RX_CHARACTERISTIC;
    add_char_params.uuid_type                = p_nus->uuid_type;
    add_char_params.max_len                  = BLE_NUS_MAX_RX_CHAR_LEN;
    add_char_params.init_len                 = sizeof(uint8_t);
    add_char_params.is_var_len               = true;
    add_char_params.char_props.read         = 1;
//    add_char_params.char_props.write         = 1;
    add_char_params.char_props.write_wo_resp = 1;

    add_char_params.read_access  = SEC_OPEN;
    add_char_params.write_access = SEC_OPEN;

    err_code = characteristic_add(p_nus->service_handle, &add_char_params, &p_nus->rx_handles);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
NRF_LOG_RAW_INFO("init: user: rx_handle=%04x/%04x/%04x/%04x\r\n",
	p_nus->rx_handles.value_handle, p_nus->rx_handles.user_desc_handle, p_nus->rx_handles.cccd_handle, p_nus->rx_handles.sccd_handle);



    // Add the TX Characteristic.
    /**@snippet [Adding proprietary characteristic to the SoftDevice] */
    memset(&add_char_params, 0, sizeof(add_char_params));
    add_char_params.uuid              = BLE_UUID_NUS_TX_CHARACTERISTIC;
    add_char_params.uuid_type         = p_nus->uuid_type;
    add_char_params.max_len           = BLE_NUS_MAX_TX_CHAR_LEN;
    add_char_params.init_len          = sizeof(uint8_t);
    add_char_params.is_var_len        = true;
    add_char_params.char_props.read         = 1;
    add_char_params.char_props.notify = 1;

    add_char_params.read_access       = SEC_OPEN;
    add_char_params.write_access      = SEC_OPEN;
    add_char_params.cccd_write_access = SEC_OPEN;

    err_code = characteristic_add(p_nus->service_handle, &add_char_params, &p_nus->tx_handles);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
NRF_LOG_RAW_INFO("init: user: tx_handle=%04x/%04x/%04x/%04x\r\n",
	p_nus->tx_handles.value_handle, p_nus->tx_handles.user_desc_handle, p_nus->tx_handles.cccd_handle, p_nus->tx_handles.sccd_handle);
	return err_code;

    /**@snippet [Adding proprietary characteristic to the SoftDevice] */
}


uint32_t ble_spp_data_send(ble_nus_t * p_nus,
                           uint8_t   * p_data,
                           uint16_t  * p_length,
                           uint16_t    conn_handle)
{
    ret_code_t                 err_code;
    ble_gatts_hvx_params_t     hvx_params;
    ble_nus_client_context_t * p_client;

//NRF_LOG_RAW_INFO("--A--\r\n");
    VERIFY_PARAM_NOT_NULL(p_nus);
//NRF_LOG_RAW_INFO("--1--\r\n");
    err_code = blcm_link_ctx_get(p_nus->p_link_ctx_storage, conn_handle, (void *) &p_client);
//    VERIFY_SUCCESS(err_code);
//NRF_LOG_RAW_INFO("--2--err_code=%x\r\n", err_code);
	if (err_code != NRF_SUCCESS) 
		return err_code;

    if ((conn_handle == BLE_CONN_HANDLE_INVALID) || (p_client == NULL))
    {
        return NRF_ERROR_NOT_FOUND;
    }
//NRF_LOG_RAW_INFO("--3--\r\n");

    if (!p_client->is_notification_enabled)
    {
        return NRF_ERROR_INVALID_STATE;
    }
//NRF_LOG_RAW_INFO("--4--\r\n");

    if (*p_length > BLE_NUS_MAX_DATA_LEN)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
//NRF_LOG_RAW_INFO("--> len=%d\r\n", (*p_length));

    memset(&hvx_params, 0, sizeof(hvx_params));

    hvx_params.handle = p_nus->tx_handles.value_handle;
    hvx_params.p_data = p_data;
    hvx_params.p_len  = p_length;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    return sd_ble_gatts_hvx(conn_handle, &hvx_params);
/*
	err_code = sd_ble_gatts_hvx(conn_handle, &hvx_params);
        if ((err_code != NRF_SUCCESS) &&
            (err_code != NRF_ERROR_INVALID_STATE) &&
            (err_code != NRF_ERROR_RESOURCES) &&
            (err_code != BLE_ERROR_GATTS_SYS_ATTR_MISSING)
        )
        {
            APP_ERROR_HANDLER(err_code);
        }
        NRF_LOG_RAW_INFO("send with rc=%d\r\n", err_code);
	return err_code;
    */

}

#endif // MUSH_FEATURE_ENABLED(BLE_SPP_SERVICE)

