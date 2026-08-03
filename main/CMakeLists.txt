idf_component_register(SRCS "rawdraw.c" "custom_lcd_display.c" "sleep_manager.c" "board.c" "charge_status.c" "main.c" "rtc_pcf8563.c" "zectrix_nfc.c" "settings.c" "audio_player.c" "wifi_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_driver_spi esp_driver_i2c esp_codec_dev esp_wifi esp_driver_i2s
                    PRIV_REQUIRES lvgl nvs_flash)
