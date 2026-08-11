idf_component_register(SRCS "main.c" "application.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp rawdraw network ui lvgl nvs_flash 78__xiaozhi-fonts audio)
