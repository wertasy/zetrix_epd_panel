#include "rtc_pcf8563.h"
#include "board.h"
#include <esp_log.h>

#define REG_CTRL1 0x00
#define REG_CTRL2 0x01
#define REG_SECONDS 0x02
#define REG_MINUTES 0x03
#define REG_HOURS 0x04
#define REG_DAYS 0x05
#define REG_WEEKDAYS 0x06
#define REG_MONTHS 0x07
#define REG_YEARS 0x08
#define REG_ALARM_MINUTE 0x09
#define REG_ALARM_HOUR 0x0A
#define REG_ALARM_DAY 0x0B
#define REG_ALARM_WEEKDAY 0x0C
#define REG_TIMER_CONTROL 0x0E
#define REG_TIMER_VALUE 0x0F

#define CTRL2_ALARM_FLAG (1 << 3)
#define CTRL2_TIMER_FLAG (1 << 2)
#define CTRL2_ALARM_INT_ENABLE (1 << 1)
#define CTRL2_TIMER_INT_ENABLE (1 << 0)
#define ALARM_DISABLE_BIT (1 << 7)
#define TIMER_ENABLE (1 << 7)
#define TIMER_FREQ_1HZ 0x02

#define CTRL2_WRITABLE_MASK 0x1F

static inline uint8_t to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static inline int from_bcd(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

void pcf8563_init(gpio_num_t int_gpio)
{
    if (int_gpio != GPIO_NUM_NC) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << int_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }
    pcf8563_clear_alarm_flag();
    /* Disable the alarm interrupt at boot. A stale alarm whose minute still
     * matches the current time would otherwise keep RTC_INT (GPIO5) low and
     * wake the ESP32 instantly on the next deep sleep (ANY_LOW ext1). The
     * interrupt is re-enabled only when enter_scheduled_sleep() arms a new
     * alarm. */
    pcf8563_enable_interrupt(false);
}

bool pcf8563_set_time(const struct tm *local_tm)
{
    if (!g_board.rtc_device)
        return false;
    esp_err_t err = ESP_OK;
    err |= board_i2c_write_reg(g_board.rtc_device, REG_SECONDS, to_bcd(local_tm->tm_sec) & 0x7F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_MINUTES, to_bcd(local_tm->tm_min) & 0x7F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_HOURS, to_bcd(local_tm->tm_hour) & 0x3F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_DAYS, to_bcd(local_tm->tm_mday) & 0x3F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_WEEKDAYS, to_bcd(local_tm->tm_wday) & 0x07);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_MONTHS, to_bcd(local_tm->tm_mon + 1) & 0x1F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_YEARS, to_bcd(local_tm->tm_year % 100));
    return err == ESP_OK;
}

bool pcf8563_get_time(struct tm *out_local_tm)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t buf[7] = {0};
    if (board_i2c_read_regs(g_board.rtc_device, REG_SECONDS, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    out_local_tm->tm_sec = from_bcd(buf[0] & 0x7F);
    out_local_tm->tm_min = from_bcd(buf[1] & 0x7F);
    out_local_tm->tm_hour = from_bcd(buf[2] & 0x3F);
    out_local_tm->tm_mday = from_bcd(buf[3] & 0x3F);
    out_local_tm->tm_wday = from_bcd(buf[4] & 0x07);
    out_local_tm->tm_mon = from_bcd(buf[5] & 0x1F) - 1;
    out_local_tm->tm_year = from_bcd(buf[6]) + 100;
    return true;
}

bool pcf8563_set_alarm(const struct tm *target_local_tm)
{
    if (!g_board.rtc_device)
        return false;
    esp_err_t err = ESP_OK;
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_MINUTE, to_bcd(target_local_tm->tm_min) & 0x7F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_HOUR, to_bcd(target_local_tm->tm_hour) & 0x3F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_DAY, to_bcd(target_local_tm->tm_mday) & 0x3F);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_WEEKDAY, ALARM_DISABLE_BIT);
    if (err != ESP_OK)
        return false;
    if (!pcf8563_clear_alarm_flag())
        return false;
    return pcf8563_enable_interrupt(true);
}

bool pcf8563_disable_alarm(void)
{
    if (!g_board.rtc_device)
        return false;
    esp_err_t err = ESP_OK;
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_MINUTE, ALARM_DISABLE_BIT);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_HOUR, ALARM_DISABLE_BIT);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_DAY, ALARM_DISABLE_BIT);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_ALARM_WEEKDAY, ALARM_DISABLE_BIT);
    if (err != ESP_OK)
        return false;
    return pcf8563_enable_interrupt(false);
}

bool pcf8563_clear_alarm_flag(void)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t ctrl2 = 0;
    /* A failed read would write garbage back into CTRL2 — bail out first. */
    if (board_i2c_read_regs(g_board.rtc_device, REG_CTRL2, &ctrl2, 1) != ESP_OK) {
        return false;
    }
    ctrl2 = (ctrl2 & CTRL2_WRITABLE_MASK) & ~CTRL2_ALARM_FLAG;
    return board_i2c_write_reg(g_board.rtc_device, REG_CTRL2, ctrl2) == ESP_OK;
}

bool pcf8563_enable_interrupt(bool enable)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t ctrl2 = 0;
    /* A failed read would write garbage back into CTRL2 — bail out first. */
    if (board_i2c_read_regs(g_board.rtc_device, REG_CTRL2, &ctrl2, 1) != ESP_OK) {
        return false;
    }
    ctrl2 &= CTRL2_WRITABLE_MASK;
    if (enable) {
        ctrl2 |= CTRL2_ALARM_INT_ENABLE;
    } else {
        ctrl2 &= ~CTRL2_ALARM_INT_ENABLE;
    }
    return board_i2c_write_reg(g_board.rtc_device, REG_CTRL2, ctrl2) == ESP_OK;
}

bool pcf8563_is_alarm_fired(void)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t ctrl2 = board_i2c_read_reg(g_board.rtc_device, REG_CTRL2);
    return (ctrl2 & CTRL2_ALARM_FLAG) != 0;
}

bool pcf8563_start_countdown_timer(uint8_t seconds)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t timer_value = seconds == 0 ? 1 : seconds;
    if (!pcf8563_stop_countdown_timer())
        return false;
    if (!pcf8563_clear_timer_flag())
        return false;
    esp_err_t err = ESP_OK;
    err |= board_i2c_write_reg(g_board.rtc_device, REG_TIMER_VALUE, timer_value);
    err |= board_i2c_write_reg(g_board.rtc_device, REG_TIMER_CONTROL, (uint8_t)(TIMER_ENABLE | TIMER_FREQ_1HZ));

    uint8_t ctrl2 = 0;
    if (board_i2c_read_regs(g_board.rtc_device, REG_CTRL2, &ctrl2, 1) != ESP_OK) {
        return false;
    }
    ctrl2 = (ctrl2 & CTRL2_WRITABLE_MASK) | CTRL2_TIMER_INT_ENABLE;
    err |= board_i2c_write_reg(g_board.rtc_device, REG_CTRL2, ctrl2);
    return err == ESP_OK;
}

bool pcf8563_stop_countdown_timer(void)
{
    if (!g_board.rtc_device)
        return false;
    esp_err_t err = board_i2c_write_reg(g_board.rtc_device, REG_TIMER_CONTROL, 0x00);
    uint8_t ctrl2 = 0;
    if (board_i2c_read_regs(g_board.rtc_device, REG_CTRL2, &ctrl2, 1) != ESP_OK) {
        return false;
    }
    ctrl2 = (ctrl2 & CTRL2_WRITABLE_MASK) & ~CTRL2_TIMER_INT_ENABLE;
    err |= board_i2c_write_reg(g_board.rtc_device, REG_CTRL2, ctrl2);
    return err == ESP_OK;
}

bool pcf8563_clear_timer_flag(void)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t ctrl2 = 0;
    if (board_i2c_read_regs(g_board.rtc_device, REG_CTRL2, &ctrl2, 1) != ESP_OK) {
        return false;
    }
    ctrl2 = (ctrl2 & CTRL2_WRITABLE_MASK) & ~CTRL2_TIMER_FLAG;
    return board_i2c_write_reg(g_board.rtc_device, REG_CTRL2, ctrl2) == ESP_OK;
}

bool pcf8563_is_timer_fired(void)
{
    if (!g_board.rtc_device)
        return false;
    uint8_t ctrl2 = board_i2c_read_reg(g_board.rtc_device, REG_CTRL2);
    return (ctrl2 & CTRL2_TIMER_FLAG) != 0;
}
