#ifndef MAIN_RTC_PCF8563_H_
#define MAIN_RTC_PCF8563_H_

#include <driver/gpio.h>
#include <time.h>
#include <stdbool.h>

void pcf8563_init(gpio_num_t int_gpio);
bool pcf8563_set_time(const struct tm *local_tm);
bool pcf8563_get_time(struct tm *out_local_tm);
bool pcf8563_get_raw(uint8_t regs[7]);
bool pcf8563_set_alarm(const struct tm *target_local_tm);
bool pcf8563_disable_alarm(void);
bool pcf8563_clear_alarm_flag(void);
bool pcf8563_enable_interrupt(bool enable);
bool pcf8563_is_alarm_fired(void);
bool pcf8563_start_countdown_timer(uint8_t seconds);
bool pcf8563_stop_countdown_timer(void);
bool pcf8563_clear_timer_flag(void);
bool pcf8563_is_timer_fired(void);

#endif // MAIN_RTC_PCF8563_H_
