idf_component_register(SRCS "protocol.c" "stream_pipeline.c" "text_chunker.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_websocket_client cjson ui rawdraw bsp)
