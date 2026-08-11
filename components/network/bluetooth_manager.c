/**
 * @file bluetooth_manager.c
 * @brief BLE initialization and advertising control for ESP32-S3 — C port.
 *
 * Brings up the Bluedroid controller, registers the GAP callback for
 * advertising lifecycle events, and starts connectable advertising with a raw
 * advertising payload (Flags + 0xF000 service UUID + Complete Local Name).
 *
 * Touch & Go: encodes the local BLE MAC address plus the GATT service UUID as
 * an NDEF Text record ("BLE:MAC=aa:bb:cc:dd:ee:ff;SVC=0xF000;NAME=InkScreen"),
 * wraps it in the NDEF Message TLV (0x03 ... 0xFE), and hands the raw bytes to
 * an injectable NFC writer so a phone tap can auto-connect and stream a
 * background image over GATT.
 *
 * Target-only (Bluedroid). Non-ESP_PLATFORM callers get no-op stubs.
 */
#include "bluetooth_manager.h"
#include "ble_gatt_service.h"
#include "ble_image_receiver.h"

#include <string.h>

#ifdef ESP_PLATFORM
#    include <esp_log.h>
#    include <esp_bt.h>
#    include <esp_bt_main.h>
#    include <esp_gap_ble_api.h>
#    include <esp_mac.h>
#endif

static const char *TAG = "bt_mgr";

#ifdef ESP_PLATFORM

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static bool s_ble_enabled     = false;
static bool s_ble_initialized = false;
static bool s_advertising     = false;

/* Injectable NFC NDEF writer (Touch & Go). */
static bluetooth_nfc_ndef_writer_t s_nfc_writer           = NULL;
static void                       *s_nfc_writer_user_data = NULL;

/* Advertising parameters (matches the original C++ configuration). */
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr         = {0},
    .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* ------------------------------------------------------------------ */
/* GAP event handler                                                   */
/* ------------------------------------------------------------------ */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Raw adv data config failed: %d", param->adv_data_raw_cmpl.status);
            break;
        }
        {
            esp_err_t ret = esp_ble_gap_start_advertising(&s_adv_params);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Start advertising failed: %s", esp_err_to_name(ret));
            }
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Adv start failed: %d", param->adv_start_cmpl.status);
        } else {
            s_ble_enabled = true;
            s_advertising = true;
            ESP_LOGI(TAG, "BLE advertising started");
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Adv stop failed: %d", param->adv_stop_cmpl.status);
        } else {
            s_advertising = false;
            ESP_LOGI(TAG, "BLE advertising stopped");
        }
        break;

    default:
        break;
    }
}

#endif /* ESP_PLATFORM */

/* ------------------------------------------------------------------ */
/* Public API — BLE lifecycle                                          */
/* ------------------------------------------------------------------ */
bool bluetooth_manager_init(void)
{
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "Initializing BLE");
    esp_err_t ret;

    /* BT controller. */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret                               = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* Enable BLE mode (disable Classic BT). */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return false;
    }

    /* Bluedroid stack. */
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }

    /* GAP callback. */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register failed: %s", esp_err_to_name(ret));
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        return false;
    }
    s_ble_initialized = true;
    ESP_LOGI(TAG, "BLE initialized successfully");

    /* GATT image-push service + receiver. */
    if (!ble_image_receiver_init()) {
        ESP_LOGE(TAG, "BLE image receiver init failed");
    }
    if (!ble_gatt_service_init()) {
        ESP_LOGE(TAG, "BLE GATT service init failed");
    }

    return true;
#else
    return false;
#endif
}

bool bluetooth_manager_enable(void)
{
#ifdef ESP_PLATFORM
    if (!s_ble_initialized) {
        ESP_LOGW(TAG, "BLE not initialized, attempting init");
        if (!bluetooth_manager_init()) {
            return false;
        }
    }

    if (s_ble_enabled) {
        ESP_LOGW(TAG, "BLE already enabled");
        return true;
    }

    const char *dev_name = "InkScreen";
    esp_ble_gap_set_device_name(dev_name);

    /* Raw advertising data:
     *   Flags (LE General Discoverable, BR/EDR Not Supported)
     *   Complete List of 16-bit Service UUIDs: 0xF000
     *   Complete Local Name "InkScreen" (9 chars -> length field 10). */
    static const uint8_t adv_data[] = {
        0x02, 0x01, 0x06, /* Flags            */
        0x03, 0x03, 0x00, 0xF0, /* Service UUID     */
        0x0A, 0x09, 'I',  'n',  'k', 'S', 'c', 'r', 'e', 'e', 'n', /* Name  */
    };
    esp_err_t ret = esp_ble_gap_config_adv_data_raw((uint8_t *)adv_data, sizeof(adv_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config adv data failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "BLE adv data configured, waiting to advertise as '%s'", dev_name);
    return true;
#else
    return false;
#endif
}

bool bluetooth_manager_restart_advertising(void)
{
#ifdef ESP_PLATFORM
    if (!s_ble_initialized || !s_ble_enabled) {
        ESP_LOGI(TAG, "Skip BLE advertising restart: initialized=%d enabled=%d", s_ble_initialized ? 1 : 0,
                 s_ble_enabled ? 1 : 0);
        return true;
    }

    ESP_LOGI(TAG, "Restarting BLE advertising after disconnect: advertising=%d", s_advertising ? 1 : 0);
    s_advertising = false;
    esp_err_t ret = esp_ble_gap_start_advertising(&s_adv_params);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Restart advertising failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "BLE advertising restart requested");
    return true;
#else
    return true;
#endif
}

bool bluetooth_manager_disable(void)
{
#ifdef ESP_PLATFORM
    if (!s_ble_enabled) {
        ESP_LOGW(TAG, "BLE already disabled");
        return true;
    }

    esp_err_t ret = esp_ble_gap_stop_advertising();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop advertising failed: %s", esp_err_to_name(ret));
    }

    s_ble_enabled = false;
    s_advertising = false;
    ESP_LOGI(TAG, "BLE disabled");
    return true;
#else
    return false;
#endif
}

bool bluetooth_manager_is_enabled(void)
{
    return s_ble_enabled;
}

/* ------------------------------------------------------------------ */
/* NFC Touch & Go                                                      */
/* ------------------------------------------------------------------ */
void bluetooth_manager_set_nfc_writer(bluetooth_nfc_ndef_writer_t writer, void *user_data)
{
    s_nfc_writer           = writer;
    s_nfc_writer_user_data = user_data;
}

/**
 * @brief Build an NDEF Text-record wrapped in a Message TLV (0x03) + Terminator
 *        (0xFE) from @p text.
 *
 * The result is written directly to the NFC tag user-data area at offset 0.
 *
 * NDEF Text record layout (Well-Known TNF, SR short form):
 *   header(0xD1) | type_len(1) | payload_len | type('T') |
 *   status(0x02) | lang("en")  | text...
 *
 * @param text    NUL-terminated UTF-8 text (truncated to fit 255-byte payload).
 * @param out     output buffer.
 * @param out_cap capacity of @p out.
 * @return total TLV bytes written, or -1 on overflow / bad input.
 */
static int build_ndef_text_tlv(const char *text, uint8_t *out, size_t out_cap)
{
    if (!text || !out || out_cap == 0) {
        return -1;
    }

    size_t text_len = strlen(text);
    /* Payload = status(1) + "en"(2) + text. Cap text so payload <= 255 (SR). */
    size_t payload_len = 1 + 2 + text_len;
    if (payload_len > 255) {
        text_len -= (payload_len - 255);
        payload_len = 255;
    }

    /* NDEF record body: header(1) + type_len(1) + payload_len(1) + type(1) +
     *                    status(1) + lang(2) + text(text_len). */
    size_t ndef_len = 7 + text_len;
    /* Full TLV: tag(1) + len(1) + ndef + terminator(1). */
    size_t tlv_len = 3 + ndef_len;
    if (tlv_len > out_cap) {
        return -1;
    }

    /* Write the NDEF record first at the start of the buffer. */
    out[0] = 0x03; /* NDEF Message TLV tag             */
    size_t hdr;
    if (ndef_len > 254) {
        if (tlv_len + 2 > out_cap) {
            return -1;
        }
        out[1] = 0xFF;
        out[2] = (uint8_t)((ndef_len >> 8) & 0xFF);
        out[3] = (uint8_t)(ndef_len & 0xFF);
        hdr = 4;
    } else {
        out[1] = (uint8_t)ndef_len;
        hdr = 2;
    }
    size_t i   = hdr;

    out[i++] = 0xD1; /* MB=1 ME=1 CF=0 SR=1 IL=0 TNF=001 */
    out[i++] = 0x01; /* TYPE length = 1 ('T')            */
    out[i++] = (uint8_t)payload_len; /* PAYLOAD length                   */
    out[i++] = 'T'; /* TYPE = "T" (Text)                */
    out[i++] = 0x02; /* status: UTF-8, lang length = 2   */
    out[i++] = 'e';
    out[i++] = 'n';
    memcpy(out + i, text, text_len);
    i += text_len;
    out[i++] = 0xFE; /* Terminator TLV                   */

    return (int)i;
}

int bluetooth_manager_publish_touch_and_go(void)
{
#ifdef ESP_PLATFORM
    if (s_nfc_writer == NULL) {
        ESP_LOGW(TAG, "Touch & Go: no NFC writer registered");
        return -1;
    }

    /* Read the BLE MAC (ESP_MAC_BT is the public BLE address). */
    uint8_t   mac[6] = {0};
    esp_err_t ret    = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch & Go: failed to read BLE MAC: %s", esp_err_to_name(ret));
        return -2;
    }

    /* Text payload describing how a phone should connect. */
    char text[96];
    snprintf(text, sizeof(text), "BLE:MAC=%02X:%02X:%02X:%02X:%02X:%02X;SVC=0xF000;NAME=InkScreen", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Touch & Go: publishing NDEF text: %s", text);

    /* Encode as NDEF Text record + Message TLV + Terminator, then hand the
     * raw bytes to the NFC writer for verbatim write to the tag user area. */
    uint8_t tlv[320];
    int     tlv_len = build_ndef_text_tlv(text, tlv, sizeof(tlv));
    if (tlv_len < 0) {
        ESP_LOGE(TAG, "Touch & Go: NDEF message too large");
        return -3;
    }

    int wret = s_nfc_writer(tlv, (size_t)tlv_len, s_nfc_writer_user_data);
    if (wret != 0) {
        ESP_LOGE(TAG, "Touch & Go: NFC writer failed: %d", wret);
        return wret;
    }

    ESP_LOGI(TAG, "Touch & Go: NDEF published (%d bytes)", tlv_len);
    return 0;
#else
    return -1;
#endif
}
