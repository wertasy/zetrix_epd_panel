#ifndef MAIN_CUSTOM_LCD_DISPLAY_H_
#define MAIN_CUSTOM_LCD_DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define EXAMPLE_LCD_WIDTH 400
#define EXAMPLE_LCD_HEIGHT 300

typedef struct {
    int x;
    int y;
    int w;
    int h;
} epd_rect_t;

typedef enum {
    EPD_COLOR_BLACK  = 0,
    EPD_COLOR_WHITE  = 1,
    EPD_COLOR_YELLOW = 2,
    EPD_COLOR_RED    = 3,
} epd_color_t;

typedef struct {
    uint8_t cs;
    uint8_t dc;
    uint8_t rst;
    uint8_t busy;
    uint8_t mosi;
    uint8_t scl;
    uint8_t power;
    int     spi_host;
    int     buffer_len;
    int     panel_type;
} custom_lcd_spi_t;

typedef enum {
    EPD_PANEL_1BPP           = 0,
    EPD_PANEL_4COLOR_SSD2683 = 1,
} epd_panel_type_t;

typedef struct {
    custom_lcd_spi_t    lcd_spi_data;
    int                 width;
    int                 height;
    epd_panel_type_t    panel_type;
    spi_device_handle_t spi;
    bool                spi_bus_inited;
    uint8_t            *buffer; // 2bpp current framebuffer
    uint8_t            *prev_buffer; // 2bpp previous framebuffer
    uint8_t            *tx_buf; // 2bpp snapshot buffer for async send

    SemaphoreHandle_t dirty_mutex;
    TaskHandle_t      refresh_task;
    epd_rect_t        dirty;
    volatile bool     pending;
    volatile bool     urgent_refresh;
    volatile bool     force_full_refresh;
    volatile bool     refresh_in_progress;

    TickType_t last_sample_tick;
    int        sample_interval_ms;
    uint32_t   next_kick_ms;

    bool prev_buffer_synced;
    void (*on_refresh_idle)(void *user_data);
    void *on_refresh_idle_user_data;
} custom_lcd_display_t;

extern custom_lcd_display_t g_display;

void custom_lcd_display_init(const custom_lcd_spi_t *spi_data);
void custom_lcd_display_deinit(void);

void epd_init(void);
void epd_clear(void);
void epd_display_full(void);
void epd_display_partial(void);
void epd_draw_color_pixel(uint16_t x, uint16_t y, uint8_t color);

void request_urgent_refresh(void);
void request_urgent_full_refresh(void);
bool is_refresh_pending(void);
void set_on_refresh_idle(void (*cb)(void *), void *user_data);
void set_next_kick_ms(uint32_t kick_ms);

uint8_t          *get_framebuffer(void);
SemaphoreHandle_t get_display_mutex(void);

#endif // MAIN_CUSTOM_LCD_DISPLAY_H_
