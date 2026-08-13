#include "epd_driver.h"
#include <string.h>
#include <assert.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "sleep_manager.h"
#include "config.h"

/* EPD Controller Commands */
#define EPD_CMD_POWER_OFF          0x02
#define EPD_CMD_POWER_ON           0x04
#define EPD_CMD_DEEP_SLEEP         0x07
#define EPD_CMD_DATA_START         0x10
#define EPD_CMD_DISPLAY_REFRESH    0x12
#define EPD_CMD_LVD_VOLT_SELECT    0xE9
static const char *TAG = "epd_driver";

epd_driver_t g_display           = {0};
static bool          g_refresh_busy_seen = false;

/* EPD BUSY wait via binary semaphore + GPIO ISR.
 *
 * The BUSY pin (active-low: 0 = panel busy, 1 = idle) is configured as an
 * edge-triggered interrupt. read_busy() blocks on a binary semaphore instead
 * of 50 ms polling; the ISR gives it when the pin returns to idle. This lets
 * the CPU sleep during the multi-second physical refresh.
 *
 * A binary semaphore is used (not a task notification) because the EPD refresh
 * task's task-notification value is already consumed by refresh scheduling
 * (request_urgent_refresh → xTaskNotifyGive). A semaphore is an independent
 * kernel object, so the BUSY-wait signal cannot collide with the scheduling
 * signal — a task notification would share the same 32-bit value and corrupt
 * both uses.
 *
 * Note: gpio_install_isr_service() returns ESP_ERR_INVALID_STATE when another
 * driver (NFC, buttons) already installed the shared GPIO ISR service — that
 * is NOT an error: the service is shared, and gpio_isr_handler_add() below
 * registers our handler on it.
 */
static SemaphoreHandle_t s_busy_semaphore = NULL;
/* Set to false only if the GPIO ISR could not be registered (shared service
 * unavailable). In that case the semaphore is never given and read_busy()
 * falls back to polling so the display still works. */
static bool s_busy_irq_ok = true;

static void IRAM_ATTR busy_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t higher_prio_woken = pdFALSE;
    /* Give on every idle edge (BUSY high = panel finished). xSemaphoreGiveFromISR
     * on a binary semaphore is idempotent while full, so rapid 0↔1 toggling
     * cannot overflow it. read_busy() re-checks the pin level after taking, so
     * a spurious token cannot cause a premature return. */
    if (s_busy_semaphore) {
        xSemaphoreGiveFromISR(s_busy_semaphore, &higher_prio_woken);
    }
    if (higher_prio_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}
static void spi_gpio_init(void)
{
    gpio_config_t gpio_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << g_display.spi_data.rst) | (1ULL << g_display.spi_data.dc) |
                        (1ULL << g_display.spi_data.cs),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    /* BUSY: input with POSEDGE interrupt (0→1 = panel finished). The ISR
     * gives the semaphore on every idle edge. Create the semaphore before
     * registering the ISR. */
    if (!s_busy_semaphore) {
        s_busy_semaphore = xSemaphoreCreateBinary();
        assert(s_busy_semaphore != NULL);
    }
    gpio_config_t busy_conf = {
        .intr_type    = GPIO_INTR_POSEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << g_display.spi_data.busy),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&busy_conf));

    /* Shared GPIO ISR service: INVALID_STATE means already installed (NFC/
     * buttons) — reuse it. */
    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_ret));
    }
    esp_err_t add_ret = gpio_isr_handler_add((gpio_num_t)g_display.spi_data.busy, busy_isr_handler, NULL);
    if (add_ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(add_ret));
        s_busy_irq_ok = false;
    }
}

static void spi_port_init(void)
{
    if (g_display.spi && g_display.spi_bus_inited) {
        spi_bus_remove_device(g_display.spi);
        g_display.spi = NULL;
    }
    if (g_display.spi_bus_inited) {
        spi_bus_free((spi_host_device_t)g_display.spi_data.spi_host);
        g_display.spi_bus_inited = false;
    }

    spi_bus_config_t buscfg = {
        .miso_io_num     = -1,
        .mosi_io_num     = g_display.spi_data.mosi,
        .sclk_io_num     = g_display.spi_data.scl,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = g_display.spi_data.buffer_len,
    };

    spi_device_interface_config_t devcfg = {
        .spics_io_num   = -1,
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode           = 0,
        .queue_size     = 7,
    };

    ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)g_display.spi_data.spi_host, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device((spi_host_device_t)g_display.spi_data.spi_host, &devcfg, &g_display.spi));
    g_display.spi_bus_inited = true;
}

static void spi_send_byte(uint8_t data)
{
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &data,
    };
    esp_err_t ret = spi_device_polling_transmit(g_display.spi, &t);
    assert(ret == ESP_OK);
}

static void write_bytes(const uint8_t *buf, int len)
{
    gpio_set_level(g_display.spi_data.dc, 1);
    gpio_set_level(g_display.spi_data.cs, 0);
    spi_transaction_t t = {
        .length    = 8 * len,
        .tx_buffer = buf,
    };
    esp_err_t ret = spi_device_polling_transmit(g_display.spi, &t);
    assert(ret == ESP_OK);
    gpio_set_level(g_display.spi_data.cs, 1);
}

static void epd_send_command(uint8_t command)
{
    gpio_set_level(g_display.spi_data.dc, 0);
    gpio_set_level(g_display.spi_data.cs, 0);
    spi_send_byte(command);
    gpio_set_level(g_display.spi_data.cs, 1);
}

static void epd_send_data(uint8_t data)
{
    gpio_set_level(g_display.spi_data.dc, 1);
    gpio_set_level(g_display.spi_data.cs, 0);
    spi_send_byte(data);
    gpio_set_level(g_display.spi_data.cs, 1);
}

static void epd_power_on(void)
{
    gpio_hold_dis((gpio_num_t)g_display.spi_data.power);
    gpio_set_level((gpio_num_t)g_display.spi_data.power, 1);
    gpio_hold_en((gpio_num_t)g_display.spi_data.power);
}

static void epd_power_off(void)
{
    gpio_hold_dis((gpio_num_t)g_display.spi_data.power);
    gpio_set_level((gpio_num_t)g_display.spi_data.power, 0);
    gpio_hold_en((gpio_num_t)g_display.spi_data.power);
}
static void read_busy(void)
{
    int busy = g_display.spi_data.busy;

    /* Already idle — nothing to wait for. */
    if (gpio_get_level((gpio_num_t)busy) == 1) {
        return;
    }

    /* If the GPIO ISR could not be registered (shared service unavailable),
     * the semaphore is never given — fall back to polling so the display
     * still works. This is a rare init-time failure path, not the normal
     * busy wait. */
    if (!s_busy_irq_ok) {
        const TickType_t start    = xTaskGetTickCount();
        const TickType_t timeout  = pdMS_TO_TICKS(120000);
        TickType_t       last_log = start;
        while (gpio_get_level((gpio_num_t)busy) == 0) {
            const TickType_t now = xTaskGetTickCount();
            if ((now - last_log) >= pdMS_TO_TICKS(5000)) {
                ESP_LOGW(TAG, "EPD busy wait: %u ms", (unsigned)((now - start) * portTICK_PERIOD_MS));
                last_log = now;
            }
            if ((now - start) >= timeout) {
                ESP_LOGE(TAG, "EPD busy timeout after %u ms, continuing",
                         (unsigned)((now - start) * portTICK_PERIOD_MS));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return;
    }

    /* Block on the binary semaphore. Drain any token that raced in before we
     * blocked (semantics: idle edge already happened → take succeeds), then
     * wait with a 5 s bounded timeout for progress logging and the 120 s
     * watchdog. After taking, re-check the level: a stale token (e.g. from an
     * earlier refresh) cannot cause a premature return while BUSY is low. */
    xSemaphoreTake(s_busy_semaphore, 0);

    const TickType_t start    = xTaskGetTickCount();
    const TickType_t timeout  = pdMS_TO_TICKS(120000);
    TickType_t       last_log = start;

    while (gpio_get_level((gpio_num_t)busy) == 0) {
        if (xSemaphoreTake(s_busy_semaphore, pdMS_TO_TICKS(5000)) == pdTRUE) {
            break;
        }
        const TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(5000)) {
            ESP_LOGW(TAG, "EPD busy wait: %u ms", (unsigned)((now - start) * portTICK_PERIOD_MS));
            last_log = now;
        }
        if ((now - start) >= timeout) {
            ESP_LOGE(TAG, "EPD busy timeout after %u ms, continuing", (unsigned)((now - start) * portTICK_PERIOD_MS));
            break;
        }
    }
}

static void epd_start_refresh(void)
{
    epd_send_command(EPD_CMD_POWER_ON);
    read_busy();
    vTaskDelay(pdMS_TO_TICKS(10));

    epd_send_command(EPD_CMD_DISPLAY_REFRESH);
    epd_send_data(0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void epd_complete_refresh(void)
{
    read_busy();

    epd_send_command(EPD_CMD_POWER_OFF);
    epd_send_data(0x00);
    read_busy();

    vTaskDelay(pdMS_TO_TICKS(20));
    epd_send_command(EPD_CMD_DEEP_SLEEP);
    epd_send_data(0xA5);

    epd_power_off();
}

void epd_init(void)
{
    epd_power_on();
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(g_display.spi_data.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(g_display.spi_data.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(g_display.spi_data.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    read_busy();

    epd_send_command(EPD_CMD_LVD_VOLT_SELECT);
    epd_send_data(0x01);
}

void epd_clear(void)
{
    memset(g_display.buffer, 0x55, g_display.spi_data.buffer_len);
}

static inline uint8_t pack_2bpp_row_to_1bpp_byte(const uint8_t *row_2bpp, int pixel_x)
{
    uint8_t out = 0x00;
    for (int bit = 0; bit < 8; ++bit) {
        const int     x      = pixel_x + bit;
        const uint8_t packed = row_2bpp[x >> 2];
        const uint8_t shift  = (uint8_t)(6 - ((x & 0x03) << 1));
        const uint8_t color  = (packed >> shift) & 0x03;
        if (color == 1) {
            out |= (uint8_t)(1U << (7 - bit));
        }
    }
    return out;
}

static inline uint16_t bit_interleave(uint8_t bytes1, uint8_t bytes2)
{
    uint16_t result = 0;
    for (int i = 0; i < 8; i++) {
        const int src_bit  = 7 - i;
        const int dst_bit0 = 2 * src_bit;
        const int dst_bit1 = 2 * src_bit + 1;

        result |= (uint16_t)(((bytes1 >> src_bit) & 1u) << dst_bit1);
        result |= (uint16_t)(((bytes2 >> src_bit) & 1u) << dst_bit0);
    }
    return result;
}

void epd_display_full(void)
{
    ESP_LOGI(TAG, "EPD refresh START: FULL");
    const int bytes_per_row_2bpp = (g_display.width * 2 + 7) >> 3;
    epd_send_command(EPD_CMD_DATA_START);
    read_busy();

    for (int y = 0; y < g_display.height; y++) {
        const uint8_t *src = g_display.tx_buf + y * bytes_per_row_2bpp;
        write_bytes(src, bytes_per_row_2bpp);
        if ((y % 16) == 15) {
            vTaskDelay(1);
        }
    }
    /* epd_turn_on_display() call removed for early release */
}

void epd_display_partial(void)
{
    ESP_LOGI(TAG, "EPD refresh START: PARTIAL");
    const int bytes_per_row_1bpp = (g_display.width + 7) >> 3;
    const int bytes_per_row_2bpp = (g_display.width * 2 + 7) >> 3;
    const int bytes_per_row_out  = bytes_per_row_1bpp * 2;

    static uint8_t line_buffer[100] __attribute__((aligned(4)));

    epd_send_command(EPD_CMD_DATA_START);
    read_busy();

    for (int i = 0; i < g_display.height; i++) {
        const uint8_t *prev_row = g_display.prev_buffer + i * bytes_per_row_2bpp;
        const uint8_t *tx_row   = g_display.tx_buf + i * bytes_per_row_2bpp;

        for (int j = 0; j < bytes_per_row_1bpp; j++) {
            uint8_t b1 = pack_2bpp_row_to_1bpp_byte(prev_row, j * 8);
            uint8_t b2 = pack_2bpp_row_to_1bpp_byte(tx_row, j * 8);

            uint16_t result        = bit_interleave(b1, b2);
            line_buffer[2 * j + 0] = (uint8_t)(result >> 8);
            line_buffer[2 * j + 1] = (uint8_t)(result & 0xFF);
        }

        write_bytes(line_buffer, bytes_per_row_out);
        if ((i % 16) == 15) {
            vTaskDelay(1);
        }
    }

    /* epd_turn_on_display() call removed for early release */
}

typedef struct {
    size_t diff_bits;
    float  diff_ratio;
} frame_diff_result_t;

static frame_diff_result_t analyze_frame_diff(const uint8_t *prev_buffer, const uint8_t *tx_buf, int width, int height)
{
    frame_diff_result_t result = {0};
    if (!prev_buffer || !tx_buf || width <= 0 || height <= 0) {
        return result;
    }

    const int    bytes_per_row = (width * 2 + 7) >> 3;
    const size_t total_bytes   = bytes_per_row * height;
    const size_t total_bits    = total_bytes * 8;

    for (int y = 0; y < height; ++y) {
        const uint8_t *prow = prev_buffer + y * bytes_per_row;
        const uint8_t *crow = tx_buf + y * bytes_per_row;
        for (int xb = 0; xb < bytes_per_row; ++xb) {
            uint8_t x = prow[xb] ^ crow[xb];
            if (x != 0) {
                result.diff_bits += __builtin_popcount(x);
            }
        }
    }

    result.diff_ratio = (total_bits > 0) ? (float)result.diff_bits / (float)total_bits : 0.0f;
    return result;
}

/* Union two dirty rects in place (smallest rect enclosing both).
 * r is the accumulated region; d is a newly reported region. */
static void dirty_rect_union(epd_rect_t *r, const epd_rect_t *d)
{
    if (d->w <= 0 || d->h <= 0)
        return;
    if (r->w <= 0 || r->h <= 0) {
        *r = *d;
        return;
    }
    const int x1 = r->x < d->x ? r->x : d->x;
    const int y1 = r->y < d->y ? r->y : d->y;
    const int x2 = (r->x + r->w) > (d->x + d->w) ? (r->x + r->w) : (d->x + d->w);
    const int y2 = (r->y + r->h) > (d->y + d->h) ? (r->y + r->h) : (d->y + d->h);
    r->x         = x1;
    r->y         = y1;
    r->w         = x2 - x1;
    r->h         = y2 - y1;
}
static void update_display_busy_locked(void)
{
    const bool busy =
        g_display.pending || g_display.urgent_refresh || g_display.force_full_refresh || g_display.refresh_in_progress;
    sm_set_busy(SLEEP_BUSY_SRC_DISPLAY, busy);
}

static bool check_refresh_idle_locked(void)
{
    const bool busy =
        g_display.pending || g_display.urgent_refresh || g_display.force_full_refresh || g_display.refresh_in_progress;
    if (busy) {
        g_refresh_busy_seen = true;
        return false;
    }
    if (!g_refresh_busy_seen) {
        return false;
    }
    g_refresh_busy_seen = false;
    return true;
}

static void refresh_task_loop(void *arg)
{
    int partial_since_full = 0;
    /* First physical refresh after boot gets a long merge window: requests
     * arriving close together (e.g. the initial frame at ~1.1 s and the wifi
     * status-bar change at ~2 s) are coalesced into ONE refresh that snapshots
     * the latest framebuffer, instead of flashing the page twice. */
    static bool s_first_refresh_done = false;

    const TickType_t debounce_ticks        = pdMS_TO_TICKS(3000);
    const TickType_t urgent_debounce_ticks = pdMS_TO_TICKS(30);
    const TickType_t first_merge_ticks     = pdMS_TO_TICKS(g_display.boot_merge_ms);
    const float      force_full_diff_ratio = 0.30f;

    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));

        TickType_t now        = xTaskGetTickCount();
        bool       urgent     = false;
        bool       force_full = false;
        epd_rect_t r          = {0, 0, 0, 0};

        xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
        if (g_display.urgent_refresh) {
            urgent                   = true;
            g_display.urgent_refresh = false;
        }
        if (g_display.force_full_refresh) {
            force_full                   = true;
            g_display.force_full_refresh = false;
        }
        if (g_display.pending && g_display.dirty.w > 0 && g_display.dirty.h > 0) {
            r                 = g_display.dirty;
            g_display.dirty.x = 0;
            g_display.dirty.y = 0;
            g_display.dirty.w = 0;
            g_display.dirty.h = 0;
            g_display.pending = false;
        }
        if (urgent || r.w > 0) {
            g_display.refresh_in_progress = true;
            g_refresh_busy_seen = true;
        }
        update_display_busy_locked();
        xSemaphoreGive(g_display.dirty_mutex);

        TickType_t min_ticks = pdMS_TO_TICKS(g_display.sample_interval_ms);
        if (!urgent) {
            TickType_t elapsed = (g_display.last_sample_tick == 0) ? min_ticks : (now - g_display.last_sample_tick);
            if (elapsed < min_ticks) {
                TickType_t wait_ticks = min_ticks - elapsed;
                vTaskDelay(wait_ticks > 0 ? wait_ticks : 1);
                continue;
            }
        }

        TickType_t debounce = (urgent || force_full) ? urgent_debounce_ticks : debounce_ticks;
        if (!s_first_refresh_done) {
            debounce = first_merge_ticks;
        }
        TickType_t t0       = xTaskGetTickCount();
        while (debounce > 0 && (xTaskGetTickCount() - t0) < debounce) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
            xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
            if (g_display.pending && g_display.dirty.w > 0 && g_display.dirty.h > 0) {
                dirty_rect_union(&r, &g_display.dirty);
                g_display.dirty.x = 0;
                g_display.dirty.y = 0;
                g_display.dirty.w = 0;
                g_display.dirty.h = 0;
                g_display.pending = false;
            }
            xSemaphoreGive(g_display.dirty_mutex);
        }

        xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
        memcpy(g_display.tx_buf, g_display.buffer, g_display.spi_data.buffer_len);
        xSemaphoreGive(g_display.dirty_mutex);
        g_display.last_sample_tick = xTaskGetTickCount();

        frame_diff_result_t result =
            analyze_frame_diff(g_display.prev_buffer, g_display.tx_buf, g_display.width, g_display.height);

        if (result.diff_bits == 0 && !force_full) {
            xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
            g_display.urgent_refresh      = false;
            g_display.refresh_in_progress = false;
            update_display_busy_locked();
            bool fire_idle = check_refresh_idle_locked();
            xSemaphoreGive(g_display.dirty_mutex);
            if (fire_idle && g_display.on_refresh_idle) {
                g_display.on_refresh_idle(g_display.on_refresh_idle_user_data);
            }
            vTaskDelay(1);
            continue;
        }

        bool should_full = force_full || !g_display.prev_buffer_synced;
        if (!should_full && g_display.panel_type == EPD_PANEL_4COLOR_SSD2683) {
            should_full = true;
        }
        if (!should_full && result.diff_ratio >= force_full_diff_ratio) {
            should_full = true;
        }
        if (!should_full && partial_since_full >= 10) {
            should_full = true;
        }

        if (should_full) {
            epd_init();
            epd_display_full();
            memcpy(g_display.prev_buffer, g_display.tx_buf, g_display.spi_data.buffer_len);
            g_display.prev_buffer_synced = true;
            partial_since_full           = 0;
        } else {
            epd_init();
            epd_display_partial();
            memcpy(g_display.prev_buffer, g_display.tx_buf, g_display.spi_data.buffer_len);
            g_display.prev_buffer_synced = true;
            partial_since_full++;
        }

        epd_start_refresh();
        epd_complete_refresh();
        s_first_refresh_done = true;

        xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
        g_display.refresh_in_progress = false;
        update_display_busy_locked();
        bool fire_idle = check_refresh_idle_locked();
        xSemaphoreGive(g_display.dirty_mutex);
        if (fire_idle && g_display.on_refresh_idle) {
            g_display.on_refresh_idle(g_display.on_refresh_idle_user_data);
        }
    }
}

/* Allocate a framebuffer-sized buffer, preferring PSRAM (MALLOC_CAP_SPIRAM)
 * to keep SRAM free; falls back to internal heap. Fills with 0x55 = white. */
static uint8_t *alloc_fb_buffer(size_t len)
{
    uint8_t *buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = (uint8_t *)malloc(len);
    }
    assert(buf);
    memset(buf, 0x55, len);
    return buf;
}

void epd_driver_init(const epd_spi_t *spi_data)
{
    g_display.spi_data              = *spi_data;
    g_display.width                     = EXAMPLE_LCD_WIDTH;
    g_display.height                    = EXAMPLE_LCD_HEIGHT;
    g_display.panel_type                = spi_data->panel_type;
    g_display.spi                       = NULL;
    g_display.spi_bus_inited            = false;
    g_display.pending                   = false;
    g_display.urgent_refresh            = false;
    g_display.force_full_refresh        = false;
    g_display.refresh_in_progress       = false;
    g_display.last_sample_tick          = 0;
    g_display.sample_interval_ms        = (spi_data->panel_type == EPD_PANEL_4COLOR_SSD2683) ? 800 : 300;
    g_display.next_kick_ms              = 0;
    g_display.boot_merge_ms              = 2000;
    g_display.on_refresh_idle           = NULL;
    g_display.on_refresh_idle_user_data = NULL;
    g_display.prev_buffer_synced        = false;
    g_display.dirty.x                   = 0;
    g_display.dirty.y                   = 0;
    g_display.dirty.w                   = 0;
    g_display.dirty.h                   = 0;

    g_display.buffer      = alloc_fb_buffer(spi_data->buffer_len);
    g_display.prev_buffer = alloc_fb_buffer(spi_data->buffer_len);
    g_display.tx_buf      = alloc_fb_buffer(spi_data->buffer_len);

    spi_gpio_init();
    spi_port_init();

    ESP_LOGI(TAG, "Initializing SSD2683 EPD");
    epd_init();
    epd_clear();
    /* All three buffers are already 0x55 (white); prev/tx start synced. */
    g_display.prev_buffer_synced = true;
    epd_display_full();

    g_display.dirty_mutex = xSemaphoreCreateMutex();
    assert(g_display.dirty_mutex);

    xTaskCreatePinnedToCore(refresh_task_loop, "epd_refresh", 4096, NULL, 3, &g_display.refresh_task, 1);
}

void epd_driver_deinit(void)
{
    if (g_display.refresh_task) {
        vTaskDelete(g_display.refresh_task);
        g_display.refresh_task = NULL;
    }
    /* Stop the BUSY ISR before deleting the semaphore it gives, so a
     * late edge cannot xSemaphoreGiveFromISR a freed handle. */
    if (s_busy_irq_ok) {
        gpio_isr_handler_remove((gpio_num_t)g_display.spi_data.busy);
    }
    if (s_busy_semaphore) {
        vSemaphoreDelete(s_busy_semaphore);
        s_busy_semaphore = NULL;
    }
    if (g_display.dirty_mutex) {
        vSemaphoreDelete(g_display.dirty_mutex);
        g_display.dirty_mutex = NULL;
    }
    free(g_display.buffer);
    free(g_display.prev_buffer);
    free(g_display.tx_buf);
    g_display.buffer      = NULL;
    g_display.prev_buffer = NULL;
    g_display.tx_buf      = NULL;
}

/* Shared impl for both urgent-refresh entry points. */
static void request_refresh(bool force_full)
{
    xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
    g_display.urgent_refresh = true;
    if (force_full) {
        g_display.force_full_refresh = true;
    }
    g_display.refresh_in_progress = true;
    update_display_busy_locked();
    g_refresh_busy_seen = true;
    xSemaphoreGive(g_display.dirty_mutex);
    sm_kick(g_display.next_kick_ms > 0 ? g_display.next_kick_ms : g_display.sample_interval_ms, "display_urgent");
    g_display.next_kick_ms = 0;
    if (g_display.refresh_task) {
        xTaskNotifyGive(g_display.refresh_task);
    }
}

void request_urgent_refresh(void)
{
    request_refresh(false);
}

void request_urgent_full_refresh(void)
{
    request_refresh(true);
}

bool is_refresh_pending(void)
{
    xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
    const bool busy =
        g_display.pending || g_display.urgent_refresh || g_display.force_full_refresh || g_display.refresh_in_progress;
    xSemaphoreGive(g_display.dirty_mutex);
    return busy;
}

void set_on_refresh_idle(void (*cb)(void *), void *user_data)
{
    xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
    g_display.on_refresh_idle           = cb;
    g_display.on_refresh_idle_user_data = user_data;
    xSemaphoreGive(g_display.dirty_mutex);
}

void set_next_kick_ms(uint32_t kick_ms)
{
    xSemaphoreTake(g_display.dirty_mutex, portMAX_DELAY);
    g_display.next_kick_ms = kick_ms;
    xSemaphoreGive(g_display.dirty_mutex);
}

void epd_driver_set_boot_merge_ms(uint32_t ms)
{
    g_display.boot_merge_ms = ms;
}

uint8_t *get_framebuffer(void)
{
    return g_display.buffer;
}

SemaphoreHandle_t get_display_mutex(void)
{
    return g_display.dirty_mutex;
}
