/**
 * @file ble_gatt_service.c
 * @brief BLE GATT service implementation for image push — C port.
 *
 * Registers a custom GATT service (0xF000) with three characteristics and
 * drives the ble_image_receiver from GATT write events. Supports both the
 * original binary control protocol ([cmd, hi, lo]) and a JSON control form
 * ({"cmd":"begin|cancel|complete","size":N}).
 *
 * Target-only (Bluedroid). The whole translation unit compiles to no-op stubs
 * when @c ESP_PLATFORM is not defined so the network component still links in
 * host tooling.
 */
#include "ble_gatt_service.h"
#include "ble_image_receiver.h"
#include "bluetooth_manager.h"

#include <string.h>

#ifdef ESP_PLATFORM
#    include <esp_log.h>
#    include <esp_gatts_api.h>
#    include <esp_gap_ble_api.h>
#    include <esp_bt_main.h>
#    include <esp_gatt_common_api.h>
#    include "cJSON.h"
#    include "cjson_util.h"

#    include <stdio.h>
#    include <stdlib.h>
#endif

static const char *TAG = "ble_gatt";

#ifdef ESP_PLATFORM

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_ble_mutex = NULL;

static void lock_ble(void)
{
    if (s_ble_mutex) {
        xSemaphoreTake(s_ble_mutex, portMAX_DELAY);
    }
}

static void unlock_ble(void)
{
    if (s_ble_mutex) {
        xSemaphoreGive(s_ble_mutex);
    }
}

/* ------------------------------------------------------------------ */
/* GATT attribute handle storage                                       */
/* ------------------------------------------------------------------ */
static uint16_t      s_service_handle            = 0;
static uint16_t      s_char_image_data_handle    = 0;
static uint16_t      s_char_image_control_handle = 0;
static uint16_t      s_char_device_info_handle   = 0;
static esp_gatt_if_t s_gatts_if                  = 0;
static bool          s_service_ready             = false;
static uint16_t      s_conn_id                   = 0;
static uint16_t      s_local_mtu                 = 23; /* default BLE MTU */

/* Image-ready callback (fires on COMPLETE control command). */
static ble_gatt_image_ready_cb_t s_image_ready_cb        = NULL;
static void                     *s_image_ready_user_data = NULL;

/* Device info reported by the Device Info characteristic. */
static ble_gatt_device_info_t s_device_info;

/* Forward declarations of file-local handlers. */
static void handle_control_command(const uint8_t *data, uint16_t len);
static void handle_json_control_command(const uint8_t *data, uint16_t len);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if_param,
                                esp_ble_gatts_cb_param_t *param);

/* ------------------------------------------------------------------ */
/* GATT event handler                                                  */
/* ------------------------------------------------------------------ */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if_param,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATTS REG_EVT, status=%d, app_id=%d", param->reg.status, param->reg.app_id);
        if (param->reg.status == ESP_GATT_OK) {
            s_gatts_if                    = gatts_if_param;
            esp_gatt_srvc_id_t service_id = {
                .id =
                    {
                        .uuid =
                            {
                                .len  = ESP_UUID_LEN_16,
                                .uuid = {.uuid16 = BLE_GATT_SERVICE_UUID},
                            },
                        .inst_id = 0,
                    },
                .is_primary = true,
            };
            esp_ble_gatts_create_service(gatts_if_param, &service_id, 7); /* 7 handles */
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "CREATE_EVT, service_handle=%d", param->create.service_handle);
        s_service_handle = param->create.service_handle;

        /* Image Data characteristic (Write only). */
        esp_bt_uuid_t data_uuid = {
            .len  = ESP_UUID_LEN_16,
            .uuid = {.uuid16 = BLE_GATT_CHAR_UUID_IMAGE_DATA},
        };
        esp_attr_value_t data_val = {
            .attr_max_len = 512,
            .attr_len     = 0,
            .attr_value   = NULL,
        };
        esp_ble_gatts_add_char(s_service_handle, &data_uuid, ESP_GATT_PERM_WRITE, ESP_GATT_CHAR_PROP_BIT_WRITE,
                               &data_val, NULL);
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(TAG, "ADD_CHAR_EVT, attr_handle=%d, uuid=0x%04x", param->add_char.attr_handle,
                 param->add_char.char_uuid.uuid.uuid16);

        if (param->add_char.char_uuid.uuid.uuid16 == BLE_GATT_CHAR_UUID_IMAGE_DATA) {
            s_char_image_data_handle = param->add_char.attr_handle;

            /* Image Control characteristic (Write/Read). */
            esp_bt_uuid_t ctrl_uuid = {
                .len  = ESP_UUID_LEN_16,
                .uuid = {.uuid16 = BLE_GATT_CHAR_UUID_IMAGE_CTRL},
            };
            esp_ble_gatts_add_char(s_service_handle, &ctrl_uuid, ESP_GATT_PERM_WRITE | ESP_GATT_PERM_READ,
                                   ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_READ, NULL, NULL);
        } else if (param->add_char.char_uuid.uuid.uuid16 == BLE_GATT_CHAR_UUID_IMAGE_CTRL) {
            s_char_image_control_handle = param->add_char.attr_handle;

            /* Device Info characteristic (Read only). */
            esp_bt_uuid_t info_uuid = {
                .len  = ESP_UUID_LEN_16,
                .uuid = {.uuid16 = BLE_GATT_CHAR_UUID_DEVICE_INFO},
            };
            esp_ble_gatts_add_char(s_service_handle, &info_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, NULL,
                                   NULL);
        } else if (param->add_char.char_uuid.uuid.uuid16 == BLE_GATT_CHAR_UUID_DEVICE_INFO) {
            s_char_device_info_handle = param->add_char.attr_handle;
            esp_ble_gatts_start_service(s_service_handle);
        }
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "START_EVT, service_handle=%d, status=%d", param->start.service_handle, param->start.status);
        if (param->start.status == ESP_GATT_OK) {
            s_service_ready = true;
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "CONNECT_EVT, conn_id=%d", param->connect.conn_id);
        s_conn_id = param->connect.conn_id;
        /* Request a larger MTU for faster transfer. */
        esp_ble_gatt_set_local_mtu(BLE_GATT_TARGET_MTU);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "DISCONNECT_EVT, conn_id=%d", param->disconnect.conn_id);
        s_conn_id   = 0;
        s_local_mtu = 23;
        ble_image_receiver_reset(); /* cancel any pending transfer */
        bluetooth_manager_restart_advertising();
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU_EVT, mtu=%d", param->mtu.mtu);
        s_local_mtu = param->mtu.mtu;
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_char_image_data_handle) {
            ESP_LOGD(TAG, "ImageData write, len=%d", param->write.len);
            ble_image_receiver_receive_chunk(param->write.value, param->write.len);
        } else if (param->write.handle == s_char_image_control_handle) {
            ESP_LOGI(TAG, "ImageControl write, len=%d, cmd=%d", param->write.len,
                     param->write.len ? param->write.value[0] : -1);
            handle_control_command(param->write.value, param->write.len);
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(s_gatts_if, s_conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;

    case ESP_GATTS_READ_EVT:
        if (param->read.handle == s_char_image_control_handle) {
            /* Status block: [status, recv_hi, recv_lo, exp_hi, exp_lo]. */
            uint8_t status[5];
            status[0]     = (uint8_t)ble_image_receiver_get_status();
            uint16_t recv = ble_image_receiver_get_received_bytes();
            status[1]     = (uint8_t)(recv >> 8);
            status[2]     = (uint8_t)(recv & 0xFF);
            uint16_t expected = ble_image_receiver_get_expected_size();
            status[3]     = (uint8_t)(expected >> 8);
            status[4]     = (uint8_t)(expected & 0xFF);

            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.attr_value.len = 5;
            memcpy(rsp.attr_value.value, status, 5);
            esp_ble_gatts_send_response(s_gatts_if, s_conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        } else if (param->read.handle == s_char_device_info_handle) {
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            lock_ble();
            rsp.attr_value.len = sizeof(s_device_info);
            memcpy(rsp.attr_value.value, &s_device_info, sizeof(s_device_info));
            unlock_ble();
            esp_ble_gatts_send_response(s_gatts_if, s_conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Control command handlers                                            */
/* ------------------------------------------------------------------ */
static void fire_image_ready(void)
{
    if (ble_image_receiver_is_complete()) {
        ESP_LOGI(TAG, "Transfer complete, triggering display");
        if (s_image_ready_cb) {
            s_image_ready_cb(ble_image_receiver_get_data(), ble_image_receiver_get_received_bytes(),
                             s_image_ready_user_data);
        } else {
            ESP_LOGW(TAG, "Image ready callback is not registered");
        }
    } else {
        ESP_LOGW(TAG, "Complete cmd but not all data received");
    }
    ble_image_receiver_reset();
}

static void handle_control_command(const uint8_t *data, uint16_t len)
{
    if (len < 1) {
        return;
    }

    /* JSON commands start with '{'. */
    if (data[0] == '{') {
        handle_json_control_command(data, len);
        return;
    }

    switch (data[0]) {
    case BLE_GATT_CMD_START:
        /* Binary form: [cmd, size_hi, size_lo]. */
        if (len >= 3) {
            uint16_t total_size = ((uint16_t)data[1] << 8) | data[2];
            ble_image_receiver_start_transfer(total_size);
            ESP_LOGI(TAG, "Start transfer, total_size=%u", (unsigned)total_size);
        }
        break;

    case BLE_GATT_CMD_CANCEL:
        ble_image_receiver_reset();
        ESP_LOGI(TAG, "Transfer cancelled");
        break;

    case BLE_GATT_CMD_COMPLETE:
        fire_image_ready();
        break;

    case BLE_GATT_CMD_QUERY_STATUS:
        /* No-op: status is returned by the READ handler. */
        break;

    default:
        ESP_LOGW(TAG, "Unknown control cmd: %d", data[0]);
        break;
    }
}

static void handle_json_control_command(const uint8_t *data, uint16_t len)
{
    /* cJSON requires a NUL-terminated string. */
    char   json[128];
    cJSON *root;
    cJSON *cmd_item;
    char   cmd[16];
    if (len >= sizeof(json)) {
        len = sizeof(json) - 1;
    }
    memcpy(json, data, len);
    json[len] = '\0';

    root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON control command");
        return;
    }
    cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd_item) || !cmd_item->valuestring) {
        ESP_LOGW(TAG, "Invalid JSON control command (no 'cmd' string)");
        cJSON_Delete(root);
        return;
    }
    snprintf(cmd, sizeof(cmd), "%s", cmd_item->valuestring);

    if (strcmp(cmd, "begin") == 0 || strcmp(cmd, "start") == 0) {
        int size = cjson_get_int(root, "size", 0);
        if (size > 0 && size <= (int)BLE_IMAGE_MAX_SIZE) {
            ble_image_receiver_start_transfer((uint16_t)size);
            ESP_LOGI(TAG, "JSON start transfer, total_size=%d", size);
        } else {
            ESP_LOGW(TAG, "Invalid JSON transfer size: %d", size);
        }
    } else if (strcmp(cmd, "cancel") == 0) {
        ble_image_receiver_reset();
        ESP_LOGI(TAG, "JSON transfer cancelled");
    } else if (strcmp(cmd, "complete") == 0 || strcmp(cmd, "finish") == 0 || strcmp(cmd, "end") == 0) {
        fire_image_ready();
    } else {
        ESP_LOGW(TAG, "Unknown JSON control cmd: %s", cmd);
    }
    cJSON_Delete(root);
}

#endif /* ESP_PLATFORM */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
bool ble_gatt_service_init(void)
{
#ifdef ESP_PLATFORM
    if (!s_ble_mutex) {
        s_ble_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Registering GATT service");

    esp_err_t ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gatts register callback failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_ble_gatts_app_register(0); /* App ID 0 */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gatts app register failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* Device-info defaults (firmware 6.7.0, 400x300 panel). */
    lock_ble();
    memset(&s_device_info, 0, sizeof(s_device_info));
    s_device_info.firmware_major      = 6;
    s_device_info.firmware_minor      = 7;
    s_device_info.firmware_patch      = 0;
    s_device_info.display_width_high  = (uint8_t)(400 >> 8);
    s_device_info.display_width_low   = (uint8_t)(400 & 0xFF);
    s_device_info.display_height_high = (uint8_t)(300 >> 8);
    s_device_info.display_height_low  = (uint8_t)(300 & 0xFF);
    unlock_ble();

    ESP_LOGI(TAG, "GATT service init complete, waiting for registration event");
    return true;
#else
    return false;
#endif
}

void ble_gatt_service_set_image_ready_callback(ble_gatt_image_ready_cb_t callback, void *user_data)
{
    s_image_ready_cb        = callback;
    s_image_ready_user_data = user_data;
}

void ble_gatt_service_update_device_info(uint8_t battery_percent, uint8_t storage_percent)
{
    lock_ble();
    s_device_info.battery_percent = battery_percent;
    s_device_info.storage_percent = storage_percent;
    unlock_ble();
}


ble_gatt_status_t ble_gatt_service_get_status(void)
{
    switch (ble_image_receiver_get_status()) {
    case BLE_IMAGE_STATUS_IDLE:
        return BLE_GATT_STATUS_IDLE;
    case BLE_IMAGE_STATUS_RECEIVING:
        return BLE_GATT_STATUS_RECEIVING;
    case BLE_IMAGE_STATUS_COMPLETE:
        return BLE_GATT_STATUS_COMPLETE;
    default:
        return BLE_GATT_STATUS_ERROR;
    }
}

uint16_t ble_gatt_service_get_received_bytes(void)
{
    return ble_image_receiver_get_received_bytes();
}

uint16_t ble_gatt_service_get_expected_size(void)
{
    return ble_image_receiver_get_expected_size();
}

bool ble_gatt_service_is_ready(void)
{
    return s_service_ready;
}
