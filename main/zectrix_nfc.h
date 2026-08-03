#ifndef MAIN_ZECTRIX_NFC_H_
#define MAIN_ZECTRIX_NFC_H_

#include <driver/gpio.h>
#include <esp_err.h>
#include <stdbool.h>

#define NFC_BLOCK_SIZE 16
#define NFC_USER_DATA_START_BLOCK 0x01
#define NFC_USER_DATA_END_BLOCK   0x37
#define NFC_USER_DATA_CAPACITY    ((NFC_USER_DATA_END_BLOCK - NFC_USER_DATA_START_BLOCK + 1) * NFC_BLOCK_SIZE)

typedef void (*nfc_field_callback_t)(bool field_present, void* user_data);

bool nfc_init(gpio_num_t power_gpio, gpio_num_t fd_gpio, int fd_active_level);
bool nfc_power_on(void);
void nfc_power_off(void);
bool nfc_is_powered(void);
bool nfc_has_field(void);
void nfc_set_field_callback(nfc_field_callback_t callback, void* user_data);

esp_err_t nfc_read_block(uint8_t block_addr, uint8_t out[NFC_BLOCK_SIZE]);
esp_err_t nfc_write_block(uint8_t block_addr, const uint8_t data[NFC_BLOCK_SIZE]);
esp_err_t nfc_read_user_data(uint16_t offset, uint8_t* out, size_t len);
esp_err_t nfc_write_user_data(uint16_t offset, const uint8_t* data, size_t len);
esp_err_t nfc_read_uid(uint8_t uid[7]);
esp_err_t nfc_write_uri_ndef(const char* uri);
esp_err_t nfc_read_ndef(uint8_t* out_message, size_t max_len, size_t* out_len);

#endif // MAIN_ZECTRIX_NFC_H_
