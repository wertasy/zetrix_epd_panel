idf_component_register(SRCS "rtc_pcf8563.c" "audio_player.c" "charge_status.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp_board data_types esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_adc espressif__esp_codec_dev esp_common log freertos)
