idf_component_register(SRCS "epd_driver.c" "epd_refresh.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_spi esp_timer heap log rawdraw freertos bsp_power bsp_board)
