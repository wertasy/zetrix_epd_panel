idf_component_register(SRCS "wifi_manager.c" "zectrix_nfc.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif esp_timer esp_rom esp_driver_gpio esp_driver_i2c esp_common log freertos bsp_board)
