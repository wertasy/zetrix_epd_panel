#include "zectrix_nfc.h"
#include "board.h"
#include <string.h>
#include <stdatomic.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "nfc";

static gpio_num_t s_power_gpio = GPIO_NUM_NC;
static gpio_num_t s_fd_gpio = GPIO_NUM_NC;
static int s_fd_active_level = 1;
static TaskHandle_t s_field_task = NULL;
static nfc_field_callback_t s_field_callback = NULL;
static void* s_field_callback_user_data = NULL;

static _Atomic bool s_initialized = false;
static _Atomic bool s_powered = false;
static _Atomic bool s_field_present = false;

static void nfc_fd_isr_handler(void* arg) {
    if (s_field_task) {
        BaseType_t high_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_field_task, &high_task_woken);
        if (high_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static void nfc_update_field_state(bool field_present) {
    bool previous = atomic_exchange(&s_field_present, field_present);
    if (previous != field_present && s_field_callback) {
        s_field_callback(field_present, s_field_callback_user_data);
    }
}

static void nfc_field_task(void* arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(20)); 
        bool field = nfc_is_powered() && (gpio_get_level(s_fd_gpio) == s_fd_active_level);
        nfc_update_field_state(field);
    }
}

static esp_err_t nfc_begin_transfer_session(void) {
    xSemaphoreTake(g_board.i2c_mutex, portMAX_DELAY);
    gpio_hold_dis(s_power_gpio);
    gpio_set_level(s_power_gpio, 0);
    esp_rom_delay_us(5000); 
    gpio_set_level(s_power_gpio, 1);
    gpio_hold_en(s_power_gpio);
    esp_rom_delay_us(1000); 
    
    esp_err_t ret = i2c_master_probe(g_board.i2c_bus, NFC_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NFC session probe failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(g_board.i2c_mutex);
    }
    return ret;
}

static void nfc_end_transfer_session(void) {
    esp_rom_delay_us(5000); 
    bool field = nfc_is_powered() && (gpio_get_level(s_fd_gpio) == s_fd_active_level);
    nfc_update_field_state(field);
    xSemaphoreGive(g_board.i2c_mutex);
}

bool nfc_init(gpio_num_t power_gpio, gpio_num_t fd_gpio, int fd_active_level) {
    if (atomic_exchange(&s_initialized, true)) {
        return true;
    }

    s_power_gpio = power_gpio;
    s_fd_gpio = fd_gpio;
    s_fd_active_level = fd_active_level;

    gpio_config_t power_cfg = {
        .pin_bit_mask = 1ULL << s_power_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&power_cfg) != ESP_OK) {
        atomic_store(&s_initialized, false);
        return false;
    }
    nfc_power_off();

    gpio_config_t fd_cfg = {
        .pin_bit_mask = 1ULL << s_fd_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    if (gpio_config(&fd_cfg) != ESP_OK) {
        atomic_store(&s_initialized, false);
        return false;
    }

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        atomic_store(&s_initialized, false);
        return false;
    }

    BaseType_t task_ok = xTaskCreate(nfc_field_task, "nfc_fd", 3 * 1024, NULL, 3, &s_field_task);
    if (task_ok != pdPASS || !s_field_task) {
        atomic_store(&s_initialized, false);
        return false;
    }

    gpio_isr_handler_add(s_fd_gpio, nfc_fd_isr_handler, NULL);

    if (!nfc_power_on()) {
        atomic_store(&s_initialized, false);
        return false;
    }

    uint8_t uid[7];
    esp_err_t uid_ret = nfc_read_uid(uid);
    bool has_field = nfc_is_powered() && (gpio_get_level(s_fd_gpio) == s_fd_active_level);
    nfc_update_field_state(has_field);

    ESP_LOGI(TAG, "NFC C port initialized, uid_ret=%s", esp_err_to_name(uid_ret));
    return true;
}

bool nfc_power_on(void) {
    xSemaphoreTake(g_board.i2c_mutex, portMAX_DELAY);
    gpio_hold_dis(s_power_gpio);
    gpio_set_level(s_power_gpio, 1);
    gpio_hold_en(s_power_gpio);
    esp_rom_delay_us(1000); 
    
    esp_err_t ret = i2c_master_probe(g_board.i2c_bus, NFC_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NFC probe failed after power on: %s", esp_err_to_name(ret));
        atomic_store(&s_powered, false);
        xSemaphoreGive(g_board.i2c_mutex);
        return false;
    }
    atomic_store(&s_powered, true);
    xSemaphoreGive(g_board.i2c_mutex);
    
    bool has_field = nfc_is_powered() && (gpio_get_level(s_fd_gpio) == s_fd_active_level);
    nfc_update_field_state(has_field);
    return true;
}

void nfc_power_off(void) {
    atomic_store(&s_powered, false);
    gpio_hold_dis(s_power_gpio);
    gpio_set_level(s_power_gpio, 0);
    gpio_hold_en(s_power_gpio);
    nfc_update_field_state(false);
}

bool nfc_is_powered(void) {
    return atomic_load(&s_powered);
}

bool nfc_has_field(void) {
    return atomic_load(&s_field_present);
}

void nfc_set_field_callback(nfc_field_callback_t callback, void* user_data) {
    s_field_callback = callback;
    s_field_callback_user_data = user_data;
}

esp_err_t nfc_read_block(uint8_t block_addr, uint8_t out[NFC_BLOCK_SIZE]) {
    if (!out || block_addr > NFC_USER_DATA_END_BLOCK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&s_initialized) || !atomic_load(&s_powered)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = nfc_begin_transfer_session();
    if (ret != ESP_OK) return ret;

    ret = i2c_master_transmit(g_board.nfc_device, &block_addr, 1, 100);
    if (ret == ESP_OK) {
        esp_rom_delay_us(10000); 
        ret = i2c_master_receive(g_board.nfc_device, out, NFC_BLOCK_SIZE, 100);
    }

    nfc_end_transfer_session();
    return ret;
}

esp_err_t nfc_write_block(uint8_t block_addr, const uint8_t data[NFC_BLOCK_SIZE]) {
    if (!data || block_addr > NFC_USER_DATA_END_BLOCK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&s_initialized) || !atomic_load(&s_powered)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = nfc_begin_transfer_session();
    if (ret != ESP_OK) return ret;

    uint8_t buffer[NFC_BLOCK_SIZE + 1];
    buffer[0] = block_addr;
    memcpy(buffer + 1, data, NFC_BLOCK_SIZE);

    ret = i2c_master_transmit(g_board.nfc_device, buffer, sizeof(buffer), 100);

    nfc_end_transfer_session();
    return ret;
}

esp_err_t nfc_read_user_data(uint16_t offset, uint8_t* out, size_t len) {
    if ((out == NULL && len != 0) || (offset + len) > NFC_USER_DATA_CAPACITY) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) return ESP_OK;

    size_t remaining = len;
    size_t dst_offset = 0;
    uint16_t current_offset = offset;

    while (remaining > 0) {
        uint16_t block_index = current_offset / NFC_BLOCK_SIZE;
        uint8_t block_addr = (uint8_t)(NFC_USER_DATA_START_BLOCK + block_index);
        size_t in_block_offset = current_offset % NFC_BLOCK_SIZE;
        size_t chunk = (remaining < (NFC_BLOCK_SIZE - in_block_offset)) ? remaining : (NFC_BLOCK_SIZE - in_block_offset);
        
        uint8_t block[NFC_BLOCK_SIZE] = {0};
        esp_err_t ret = nfc_read_block(block_addr, block);
        if (ret != ESP_OK) return ret;

        memcpy(out + dst_offset, block + in_block_offset, chunk);
        current_offset += chunk;
        dst_offset += chunk;
        remaining -= chunk;
    }
    return ESP_OK;
}

esp_err_t nfc_write_user_data(uint16_t offset, const uint8_t* data, size_t len) {
    if ((data == NULL && len != 0) || (offset % NFC_BLOCK_SIZE) != 0 || (offset + len) > NFC_USER_DATA_CAPACITY) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) return ESP_OK;

    size_t remaining = len;
    size_t src_offset = 0;
    uint16_t current_offset = offset;

    while (remaining > 0) {
        uint8_t block_addr = (uint8_t)(NFC_USER_DATA_START_BLOCK + (current_offset / NFC_BLOCK_SIZE));
        size_t chunk = (remaining < NFC_BLOCK_SIZE) ? remaining : NFC_BLOCK_SIZE;

        uint8_t block[NFC_BLOCK_SIZE] = {0};
        if (chunk < NFC_BLOCK_SIZE) {
            esp_err_t ret = nfc_read_block(block_addr, block);
            if (ret != ESP_OK) return ret;
        }
        memcpy(block, data + src_offset, chunk);

        esp_err_t ret = nfc_write_block(block_addr, block);
        if (ret != ESP_OK) return ret;

        current_offset += chunk;
        src_offset += chunk;
        remaining -= chunk;
    }
    return ESP_OK;
}

esp_err_t nfc_read_uid(uint8_t uid[7]) {
    if (!uid) return ESP_ERR_INVALID_ARG;
    uint8_t block[NFC_BLOCK_SIZE] = {0};
    esp_err_t ret = nfc_read_block(0x00, block);
    if (ret == ESP_OK) {
        memcpy(uid, block, 7);
    }
    return ret;
}

esp_err_t nfc_write_uri_ndef(const char* uri) {
    if (!uri) return ESP_ERR_INVALID_ARG;
    
    uint8_t prefix_code = 0x00;
    const char* suffix = uri;
    if (strncmp(uri, "https://www.", 12) == 0) {
        prefix_code = 0x02;
        suffix = uri + 12;
    } else if (strncmp(uri, "http://www.", 11) == 0) {
        prefix_code = 0x01;
        suffix = uri + 11;
    } else if (strncmp(uri, "https://", 8) == 0) {
        prefix_code = 0x04;
        suffix = uri + 8;
    } else if (strncmp(uri, "http://", 7) == 0) {
        prefix_code = 0x03;
        suffix = uri + 7;
    }
    
    size_t suffix_len = strlen(suffix);
    size_t payload_len = 1 + suffix_len;
    
    uint8_t ndef_msg[512] = {0};
    size_t ndef_len = 0;
    
    if (payload_len <= 0xFF) {
        ndef_msg[0] = 0xD1;
        ndef_msg[1] = 0x01;
        ndef_msg[2] = (uint8_t)payload_len;
        ndef_msg[3] = 'U';
        ndef_msg[4] = prefix_code;
        memcpy(ndef_msg + 5, suffix, suffix_len);
        ndef_len = 5 + suffix_len;
    } else {
        ndef_msg[0] = 0xC1;
        ndef_msg[1] = 0x01;
        ndef_msg[2] = (uint8_t)((payload_len >> 24) & 0xFF);
        ndef_msg[3] = (uint8_t)((payload_len >> 16) & 0xFF);
        ndef_msg[4] = (uint8_t)((payload_len >> 8) & 0xFF);
        ndef_msg[5] = (uint8_t)(payload_len & 0xFF);
        ndef_msg[6] = 'U';
        ndef_msg[7] = prefix_code;
        memcpy(ndef_msg + 8, suffix, suffix_len);
        ndef_len = 8 + suffix_len;
    }
    
    uint8_t tlv[512 + 8] = {0};
    size_t tlv_len = 0;
    if (ndef_len <= 0xFE) {
        tlv[0] = 0x03; 
        tlv[1] = (uint8_t)ndef_len;
        memcpy(tlv + 2, ndef_msg, ndef_len);
        tlv[2 + ndef_len] = 0xFE; 
        tlv_len = 3 + ndef_len;
    } else {
        tlv[0] = 0x03;
        tlv[1] = 0xFF; 
        tlv[2] = (uint8_t)((ndef_len >> 8) & 0xFF);
        tlv[3] = (uint8_t)(ndef_len & 0xFF);
        memcpy(tlv + 4, ndef_msg, ndef_len);
        tlv[4 + ndef_len] = 0xFE;
        tlv_len = 5 + ndef_len;
    }
    
    return nfc_write_user_data(0, tlv, tlv_len);
}

esp_err_t nfc_read_ndef(uint8_t* out_message, size_t max_len, size_t* out_len) {
    if (!out_message || !out_len) return ESP_ERR_INVALID_ARG;
    
    static uint8_t raw[NFC_USER_DATA_CAPACITY];
    esp_err_t ret = nfc_read_user_data(0, raw, sizeof(raw));
    if (ret != ESP_OK) return ret;
    
    size_t cursor = 0;
    while (cursor < sizeof(raw)) {
        uint8_t tlv_type = raw[cursor++];
        if (tlv_type == 0x00) continue;
        if (tlv_type == 0xFE) return ESP_ERR_NOT_FOUND;
        
        if (cursor >= sizeof(raw)) return ESP_ERR_INVALID_RESPONSE;
        
        size_t length = raw[cursor++];
        if (length == 0xFF) {
            if (cursor + 1 >= sizeof(raw)) return ESP_ERR_INVALID_RESPONSE;
            length = ((size_t)raw[cursor] << 8) | (size_t)raw[cursor + 1];
            cursor += 2;
        }
        
        if (cursor + length > sizeof(raw)) return ESP_ERR_INVALID_RESPONSE;
        
        if (tlv_type == 0x03) { 
            if (length == 0) return ESP_ERR_NOT_FOUND;
            if (length > max_len) return ESP_ERR_INVALID_SIZE;
            memcpy(out_message, raw + cursor, length);
            *out_len = length;
            return ESP_OK;
        }
        
        cursor += length;
    }
    
    return ESP_ERR_NOT_FOUND;
}
