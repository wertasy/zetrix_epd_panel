idf_component_register(SRCS "board.c" "system_info.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp_peripherals esp_driver_gpio esp_driver_i2c esp_timer esp_hw_support esp_common esp_system heap log esp_app_format freertos)
