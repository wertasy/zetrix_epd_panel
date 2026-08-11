/**
 * @file application.h
 * @brief Application singleton — C port of C++ Application.
 *
 * Owns the RawDraw UI manager, routes button events, manages the WiFi /
 * sync-sleep state machine, updates the status bar, and runs the WSS
 * text-streaming pipeline (Phase 4 text skeleton; audio/Opus deferred).
 */
#ifndef MAIN_APPLICATION_H_
#define MAIN_APPLICATION_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVICE_STATE_UNKNOWN = 0,
    DEVICE_STATE_STARTING,
    DEVICE_STATE_IDLE,
    DEVICE_STATE_WIFI_CONFIG,
    DEVICE_STATE_SLEEPING,
    DEVICE_STATE_FATAL_ERROR,
} device_state_t;

/* Singleton accessor. */
void           application_init(void);
void           application_run(void);
device_state_t application_get_device_state(void);
bool           application_set_device_state(device_state_t state);

/* Button event routing (called from iot_button callbacks). */
void application_on_up_click(void);
void application_on_down_click(void);
void application_on_up_long_press(void);
void application_on_down_long_press(void);
void application_on_up_double_click(void);
void application_on_boot_double_click(void);
void application_on_down_double_click(void);
void application_on_wifi_config_combo_long_press(void);
void application_on_boot_click(void);
void application_on_boot_long_press(void);

/* Status bar sync. */
void application_update_status_bar(void);
void application_notify_wifi_if_connected(void);

/* Sleep control. */
void application_enter_manual_sleep(void);

/* UI manager access (for main wiring). */
void *application_get_ui_manager(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_APPLICATION_H_ */
