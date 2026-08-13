#include "settings.h"
#include <esp_log.h>
#include <string.h>

static const char *TAG = "settings";

settings_handle_t settings_open(const char *ns, bool read_write)
{
    nvs_handle_t handle = 0;
    esp_err_t    err    = nvs_open(ns, read_write ? NVS_READWRITE : NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to open NVS namespace %s: %s", ns, esp_err_to_name(err));
    }
    return handle;
}

void settings_close(settings_handle_t handle)
{
    if (handle != 0) {
        nvs_close(handle);
    }
}

esp_err_t settings_get_string(settings_handle_t handle, const char *key, char *out_val, size_t max_len,
                              const char *default_value)
{
    if (handle == 0 || !out_val || max_len == 0) {
        if (out_val && max_len > 0) {
            strncpy(out_val, default_value ? default_value : "", max_len - 1);
            out_val[max_len - 1] = '\0';
        }
        return ESP_ERR_INVALID_ARG;
    }

    size_t    length = max_len;
    esp_err_t err    = nvs_get_str(handle, key, out_val, &length);
    if (err != ESP_OK) {
        strncpy(out_val, default_value ? default_value : "", max_len - 1);
        out_val[max_len - 1] = '\0';
    }
    return err;
}

esp_err_t settings_set_string(settings_handle_t handle, const char *key, const char *value)
{
    if (handle == 0 || !value)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    return err;
}

int32_t settings_get_int(settings_handle_t handle, const char *key, int32_t default_value)
{
    if (handle == 0)
        return default_value;
    int32_t   value = 0;
    esp_err_t err   = nvs_get_i32(handle, key, &value);
    return (err == ESP_OK) ? value : default_value;
}

esp_err_t settings_set_int(settings_handle_t handle, const char *key, int32_t value)
{
    if (handle == 0)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    return err;
}

bool settings_get_bool(settings_handle_t handle, const char *key, bool default_value)
{
    if (handle == 0)
        return default_value;
    uint8_t   value = 0;
    esp_err_t err   = nvs_get_u8(handle, key, &value);
    return (err == ESP_OK) ? (value != 0) : default_value;
}

esp_err_t settings_set_bool(settings_handle_t handle, const char *key, bool value)
{
    if (handle == 0)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_u8(handle, key, value ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    return err;
}

esp_err_t settings_erase_key(settings_handle_t handle, const char *key)
{
    if (handle == 0)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_erase_key(handle, key);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    return err;
}

esp_err_t settings_erase_all(settings_handle_t handle)
{
    if (handle == 0)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    return err;
}
