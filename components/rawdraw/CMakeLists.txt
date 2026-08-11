idf_component_register(SRCS "rawdraw.c" "rawdraw_ext.c" "framebuffer.c" "layout.c" "theme.c" "clock.c"
                       "widgets/button.c" "widgets/panel.c" "widgets/card.c"
                       "widgets/list_item.c" "widgets/progress_bar.c" "widgets/scrollview.c"
                       "widgets/slider.c" "widgets/toggle.c" "widgets/status_bar.c" "widgets/footer_bar.c"
                       "widgets/modal.c" "widgets/bubble.c" "widgets/weather_card.c" "widgets/voice_wakeup.c" "widgets/calendar.c"
                       INCLUDE_DIRS "include" "widgets"
                       REQUIRES lvgl esp_timer network)
