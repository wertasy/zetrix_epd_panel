set(UI_SRCS
    "src/data_refresh.c"
    "src/ui_text_util.c"
    "src/photo_blit.c"
    "page_registry.c"
    "pages/weather_page.c"
    "pages/photo_gallery_page.c"
    "pages/almanac_page.c"
    "pages/calendar_page.c"
    "pages/chat_page.c"
    "pages/wifi_page.c"
    "pages/weather_detail_page.c"
    "pages/photo_detail_page.c"
    "pages/coding_plan_page.c"
    "pages/settings_page.c"
    "pages/settings_dialogs.c"
    "pages/settings_about.c"
    "pages/settings_themes.c"
    "pages/ap_transfer_page.c"
    "ui_manager.c"
    "ui_status_bar.c"
    "ui_quick_switch.c"
)

if(CONFIG_PAGE_EBOOK_ENABLE)
    list(APPEND UI_SRCS "pages/ebook_page.c")
endif()
if(CONFIG_PAGE_FONT_DEBUG_ENABLE)
    list(APPEND UI_SRCS "pages/font_debug_page.c")
endif()
if(CONFIG_PAGE_FONT_METRICS_ENABLE)
    list(APPEND UI_SRCS "pages/font_metrics_page.c")
endif()
if(CONFIG_PAGE_CAR_MOVE_ENABLE)
    list(APPEND UI_SRCS "pages/car_move_page.c")
endif()
if(CONFIG_PAGE_LIFEBAR_ENABLE)
    list(APPEND UI_SRCS "pages/lifebar_page.c")
endif()
if(CONFIG_PAGE_LOG_ENABLE)
    list(APPEND UI_SRCS "pages/log_page.c")
endif()
if(CONFIG_PAGE_NEWS_ENABLE)
    list(APPEND UI_SRCS "pages/news_page.c")
endif()
if(CONFIG_PAGE_YEARPROGRESS_ENABLE)
    list(APPEND UI_SRCS "pages/yearprogress_page.c")
endif()

idf_component_register(WHOLE_ARCHIVE
                       INCLUDE_DIRS "include" "pages" "src"
                       SRCS ${UI_SRCS}
                       REQUIRES rawdraw bsp_display bsp_storage app_state data_types network zetrix_fonts espressif__qrcode)
