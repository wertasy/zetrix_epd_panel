idf_component_register(SRCS "weather_api.c" "holiday_fetcher.c" "photo_downloader.c" "photo_storage.c"
                            "http_client_util.c" "cjson_util.c" "bluetooth_manager.c" "ble_gatt_service.c" "ble_image_receiver.c"
                            "coding_plan_api.c" "ap_transfer_server.c"
                       INCLUDE_DIRS "include"
                       EMBED_FILES "globalsign_root_ca.pem"
                       REQUIRES bsp_power bsp_storage app_state data_types rawdraw esp_http_client esp_http_server esp_wifi esp_netif esp_timer nvs_flash bt cjson mbedtls esp_rom)
