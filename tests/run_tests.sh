#!/usr/bin/env bash
# Build and run host-side unit tests for the C firmware components.
# Usage: tests/run_tests.sh [test_name...]
# (omit names to run all)
set -euo pipefail
cd "$(dirname "$0")/.."

C11="-std=c11 -Wall -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE"
INCS="-Icomponents/ui/include -Icomponents/ui/pages \
      -Icomponents/rawdraw/include -Icomponents/rawdraw/widgets \
      -Icomponents/network/include -Icomponents/bsp_display/include \
      -Icomponents/bsp_storage/include -Icomponents/bsp_connectivity/include \
      -Icomponents/bsp_peripherals/include -Icomponents/bsp_power/include \
      -Icomponents/bsp_board/include \
      -Icomponents/app_state/include \
      -Icomponents/data_types/include \
      -Icomponents/78__xiaozhi-fonts/include \
      -Imanaged_components/lvgl__lvgl/src \
      -Imanaged_components/espressif__cjson/cJSON"

RAWDRAW_SRCS="components/rawdraw/rawdraw.c components/rawdraw/rawdraw_ext.c \
              components/rawdraw/layout.c components/rawdraw/theme.c \
              components/rawdraw/framebuffer.c components/rawdraw/clock.c"

run() {
    local name="$1"; shift
    echo "=== $name ==="
    gcc $C11 $INCS -o "/tmp/test_$name" "$@" || { echo "BUILD FAILED: $name"; exit 1; }
    "/tmp/test_$name" || { echo "TEST FAILED: $name"; exit 1; }
    echo "PASS: $name"
}

# If specific tests requested, run only those.
if [ $# -gt 0 ]; then
    TESTS=("$@")
else
    TESTS=(rawdraw layout theme framebuffer network nvs_state ui_text_util ui_pages_smoke audio)
fi

for t in "${TESTS[@]}"; do
    case "$t" in
        rawdraw)
            run rawdraw tests/test_rawdraw.c $RAWDRAW_SRCS
            ;;
        layout)
            run layout tests/test_layout.c components/rawdraw/layout.c components/rawdraw/rawdraw.c components/rawdraw/rawdraw_ext.c
            ;;
        theme)
            run theme tests/test_theme.c components/rawdraw/theme.c components/rawdraw/rawdraw.c components/rawdraw/rawdraw_ext.c
            ;;
        framebuffer)
            run framebuffer tests/test_framebuffer.c components/rawdraw/framebuffer.c components/rawdraw/rawdraw.c components/rawdraw/rawdraw_ext.c
            ;;
        network)
            run network tests/test_network.c \
                components/network/weather_api.c components/network/holiday_fetcher.c \
                components/network/photo_storage.c components/network/photo_downloader.c \
                components/network/http_client_util.c components/network/cjson_util.c \
                components/network/coding_plan_api.c \
                components/bsp_storage/storage_manager.c managed_components/espressif__cjson/cJSON/cJSON.c -lm -lz
            ;;
        nvs_state)
            run nvs_state tests/test_nvs_state.c components/app_state/nvs_state.c -lm
            ;;
        ui_text_util)
            run ui_text_util tests/test_ui_text_util.c \
                components/ui/src/ui_text_util.c components/rawdraw/rawdraw_ext.c components/rawdraw/rawdraw.c
            ;;
        ui_pages_smoke)
            run ui_pages_smoke tests/test_ui_pages_smoke.c \
                components/ui/page_registry.c \
                components/ui/src/data_refresh.c \
                components/ui/pages/chat_page.c \
                components/ui/pages/coding_plan_page.c \
                components/rawdraw/widgets/progress_bar.c \
                components/ui/src/ui_text_util.c \
                components/rawdraw/theme.c \
                components/rawdraw/rawdraw.c components/rawdraw/rawdraw_ext.c components/rawdraw/layout.c -lm
            ;;
        audio)
            run audio tests/test_audio.c \
                components/audio/protocol.c \
                components/audio/text_chunker.c \
                components/audio/stream_pipeline.c \
                components/network/cjson_util.c \
                managed_components/espressif__cjson/cJSON/cJSON.c \
                -Itests/mock_include -Icomponents/audio/include \
                -lpthread -lm
            ;;
        *)
            echo "Unknown test: $t"
            exit 1
            ;;
    esac
done

echo
echo "All requested tests passed."
