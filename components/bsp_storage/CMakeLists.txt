idf_component_register(SRCS "storage_manager.c" "settings.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash log esp_common
                       PRIV_REQUIRES joltwallet__littlefs)
