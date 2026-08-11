#ifndef MAIN_SETTINGS_H_
#define MAIN_SETTINGS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <nvs_flash.h>

typedef nvs_handle_t settings_handle_t;

settings_handle_t settings_open(const char *ns, bool read_write);
void              settings_close(settings_handle_t handle);

esp_err_t settings_get_string(settings_handle_t handle, const char *key, char *out_val, size_t max_len,
                              const char *default_value);
esp_err_t settings_set_string(settings_handle_t handle, const char *key, const char *value);

int32_t   settings_get_int(settings_handle_t handle, const char *key, int32_t default_value);
esp_err_t settings_set_int(settings_handle_t handle, const char *key, int32_t value);

bool      settings_get_bool(settings_handle_t handle, const char *key, bool default_value);
esp_err_t settings_set_bool(settings_handle_t handle, const char *key, bool value);

esp_err_t settings_erase_key(settings_handle_t handle, const char *key);
esp_err_t settings_erase_all(settings_handle_t handle);

#endif // MAIN_SETTINGS_H_
