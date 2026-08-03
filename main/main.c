#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <iot_button.h>
#include <button_gpio.h>

#include "config.h"
#include "board.h"
#include "charge_status.h"
#include "custom_lcd_display.h"
#include "rawdraw.h"
#include "rtc_pcf8563.h"
#include "zectrix_nfc.h"
#include "settings.h"
#include "audio_player.h"
#include "wifi_manager.h"

static const char* TAG = "main_app";

static charge_status_t s_charge_status;
static int s_current_page = 0;
#define TOTAL_PAGES 3

static TimerHandle_t s_clock_timer = NULL;

static void draw_page(void);
static void navigate_page(int direction);

static void button_up_click_cb(void* arg, void* usr_data) {
    ESP_LOGI(TAG, "UP button clicked");
    navigate_page(-1);
    board_flash_activity_led();
    audio_player_play_tone(400, 80);
}

static void button_down_click_cb(void* arg, void* usr_data) {
    ESP_LOGI(TAG, "DOWN button clicked");
    navigate_page(1);
    board_flash_activity_led();
    audio_player_play_tone(600, 80);
}

static void button_confirm_click_cb(void* arg, void* usr_data) {
    ESP_LOGI(TAG, "BOOT/CONFIRM button clicked -> Force full refresh");
    draw_page();
    request_urgent_full_refresh();
    board_flash_activity_led();
    audio_player_play_tone(800, 80);
    vTaskDelay(pdMS_TO_TICKS(100));
    audio_player_play_tone(1000, 80);
}

static void clock_timer_callback(TimerHandle_t xTimer) {
    if (s_current_page == 2) {
        draw_page();
    }
}

static void draw_page(void) {
    SemaphoreHandle_t mutex = get_display_mutex();
    if (!mutex) return;

    xSemaphoreTake(mutex, portMAX_DELAY);

    epd_clear();
    uint8_t* fb = get_framebuffer();

    if (s_current_page == 0) {
        rawdraw_draw_round_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 10, 10, EXAMPLE_LCD_WIDTH - 20, 60, 10, RAWDRAW_COLOR_RED, RAWDRAW_COLOR_BLACK, 0);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 25, 28, "ZECTRIX BWRY 4-COLOR", &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_WHITE);

        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 90, "Pure C Firmware Port", &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_BLACK);
        
        char ssid_str[48];
        char ip_address[32];
        wifi_manager_get_ip(ip_address, sizeof(ip_address));
        if (wifi_manager_is_connected()) {
            char ssid[32];
            wifi_manager_get_ssid(ssid, sizeof(ssid));
            snprintf(ssid_str, sizeof(ssid_str), "Wi-Fi: %s", ssid);
        } else {
            snprintf(ssid_str, sizeof(ssid_str), "Wi-Fi: Disconnected");
        }
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 120, ssid_str, &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 140, ip_address, &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

        char nfc_buf[128];
        snprintf(nfc_buf, sizeof(nfc_buf), "NFC Field: %s", nfc_has_field() ? "Present (Field Seen)" : "Idle (No Field)");
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 160, nfc_buf, &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

        charge_snapshot_t snap = charge_status_get(&s_charge_status);
        const char* status_str = "Discharging";
        if (snap.charging) status_str = "Charging";
        if (snap.full) status_str = "Full";
        if (snap.no_battery) status_str = "No Battery";

        rawdraw_draw_round_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 172, EXAMPLE_LCD_WIDTH - 40, 68, 8, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_YELLOW, 2);
        
        char buf[128];
        snprintf(buf, sizeof(buf), "Battery Status: %s", status_str);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 35, 195, buf, &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_BLACK);

        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 260, "Page 1 of 3 (UP/DOWN to navigate)", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

    } else if (s_current_page == 1) {
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 20, "4-Color Palette Showcase", &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_RED);

        rawdraw_draw_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 60, 70, 50, RAWDRAW_COLOR_BLACK);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 25, 120, "Black", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

        rawdraw_draw_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 110, 60, 70, 50, RAWDRAW_COLOR_RED);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 125, 120, "Red", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_RED);

        rawdraw_draw_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 200, 60, 70, 50, RAWDRAW_COLOR_YELLOW);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 210, 120, "Yellow", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_YELLOW);

        rawdraw_draw_round_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 290, 60, 70, 50, 4, RAWDRAW_COLOR_WHITE, RAWDRAW_COLOR_BLACK, 1);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 305, 120, "White", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 160, "Dithered gray area (checkerboard pattern):", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);
        rawdraw_draw_dither_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 185, 360, 45);

        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 260, "Page 2 of 3 (UP/DOWN to navigate)", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

    } else if (s_current_page == 2) {
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 20, "RTC Clock & Interactive Refresh", &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_YELLOW);

        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        char clock_buf[64];
        snprintf(clock_buf, sizeof(clock_buf), "Clock: %02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        
        rawdraw_draw_round_rect(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 60, EXAMPLE_LCD_WIDTH - 40, 80, 12, RAWDRAW_COLOR_BLACK, RAWDRAW_COLOR_RED, 3);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 50, 85, "RTC Time:", &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_YELLOW);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 170, 85, clock_buf, &SourceHanSansSC_Medium_slim, RAWDRAW_COLOR_WHITE);

        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 165, "Press BOOT button to trigger a FULL refresh.", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);
        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 195, "Flashing clears panel ghosting.", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);

        rawdraw_draw_text(fb, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, 20, 260, "Page 3 of 3 (UP/DOWN to navigate)", &BUILTIN_TEXT_FONT, RAWDRAW_COLOR_BLACK);
    }

    xSemaphoreGive(mutex);
    request_urgent_refresh();
}

static void navigate_page(int direction) {
    s_current_page = (s_current_page + direction + TOTAL_PAGES) % TOTAL_PAGES;
    ESP_LOGI(TAG, "Switched to page %d", s_current_page);
    draw_page();
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting ZecTrix EPD Panel C Firmware");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    charge_status_init(&s_charge_status, CHARGE_DETECT_GPIO, CHARGE_FULL_GPIO, esp_timer_get_time() / 1000);

    board_init(&s_charge_status);

    board_power_vbat_on();
    board_power_audio_on();
    board_power_epd_on();

    while (!gpio_get_level(VBAT_PWR_GPIO)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Board Power Rails and I2C initialized");

    pcf8563_init(RTC_INT_GPIO);
    
    struct tm rtc_tm = {0};
    if (pcf8563_get_time(&rtc_tm)) {
        time_t t = mktime(&rtc_tm);
        if (t != -1) {
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "System time synchronized with RTC: %02d:%02d:%02d", rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
        }
    } else {
        ESP_LOGW(TAG, "RTC read failed; using system time");
    }

    nfc_init(NFC_PWR_GPIO, NFC_FD_GPIO, NFC_FD_ACTIVE_LEVEL);
    audio_player_init();
    wifi_manager_init();

    audio_player_play_tone(1000, 100);

    settings_handle_t wifi_handle = settings_open("wifi", false);
    if (wifi_handle) {
        char ssid[32] = {0};
        char password[64] = {0};
        settings_get_string(wifi_handle, "ssid", ssid, sizeof(ssid), "");
        settings_get_string(wifi_handle, "password", password, sizeof(password), "");
        settings_close(wifi_handle);
        if (strlen(ssid) > 0) {
            wifi_manager_connect(ssid, password);
        } else {
            settings_handle_t wifi_wr = settings_open("wifi", true);
            if (wifi_wr) {
                settings_set_string(wifi_wr, "ssid", "ZecTrix-AP");
                settings_set_string(wifi_wr, "password", "12345678");
                settings_close(wifi_wr);
                ESP_LOGI(TAG, "No Wi-Fi credentials found. Saved default 'ZecTrix-AP' to NVS settings.");
            }
        }
    }

    custom_lcd_spi_t spi_data = {
        .cs = EPD_CS_PIN,
        .dc = EPD_DC_PIN,
        .rst = EPD_RST_PIN,
        .busy = EPD_BUSY_PIN,
        .mosi = EPD_MOSI_PIN,
        .scl = EPD_SCK_PIN,
        .power = EPD_PWR_PIN,
        .spi_host = EPD_SPI_NUM,
        .buffer_len = ((EXAMPLE_LCD_WIDTH * 2 + 7) / 8) * EXAMPLE_LCD_HEIGHT,
        .panel_type = EPD_PANEL_4COLOR_SSD2683,
    };
    custom_lcd_display_init(&spi_data);
    ESP_LOGI(TAG, "SSD2683 EPD display initialized");

    button_config_t btn_cfg = {
        .long_press_time = 1000,
        .short_press_time = 50,
    };

    button_gpio_config_t up_gpio_cfg = {
        .gpio_num = TODO_UP_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t up_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &up_gpio_cfg, &up_btn));

    button_gpio_config_t down_gpio_cfg = {
        .gpio_num = TODO_DOWN_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t down_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &down_gpio_cfg, &down_btn));

    button_gpio_config_t confirm_gpio_cfg = {
        .gpio_num = TODO_CONFIRM_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t confirm_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, &confirm_gpio_cfg, &confirm_btn));

    iot_button_register_cb(up_btn, BUTTON_SINGLE_CLICK, NULL, button_up_click_cb, NULL);
    iot_button_register_cb(down_btn, BUTTON_SINGLE_CLICK, NULL, button_down_click_cb, NULL);
    iot_button_register_cb(confirm_btn, BUTTON_SINGLE_CLICK, NULL, button_confirm_click_cb, NULL);

    ESP_LOGI(TAG, "Buttons configured: UP (39), DOWN (18), BOOT (0)");

    draw_page();

    s_clock_timer = xTimerCreate("clock_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, clock_timer_callback);
    if (s_clock_timer) {
        xTimerStart(s_clock_timer, 0);
    }

    ESP_LOGI(TAG, "Main Application running");
}
