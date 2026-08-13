idf_component_register(SRCS "main.c" "application.c" "app_sync.c" "app_sleep.c" "app_settings_menu.c" "app_sntp.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp_board bsp_power bsp_peripherals bsp_display bsp_storage bsp_connectivity app_state rawdraw network ui lvgl nvs_flash 78__xiaozhi-fonts audio)
