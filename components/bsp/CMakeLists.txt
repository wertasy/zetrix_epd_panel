idf_component_register(SRCS "epd_refresh.c" "storage_manager.c" "nvs_state.c" "system_info.c"
                            "board.c" "custom_lcd_display.c" "rtc_pcf8563.c" "zectrix_nfc.c"
                            "audio_player.c" "wifi_manager.c" "charge_status.c" "settings.c"
                            "sleep_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_driver_gpio esp_driver_spi esp_driver_i2c esp_codec_dev esp_wifi esp_driver_i2s esp_adc esp_app_format rawdraw nvs_flash
                       PRIV_REQUIRES littlefs)
