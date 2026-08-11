#include <stdio.h>
#include <assert.h>
#include "photo_blit.h"

void test_bytes_per_row(void)
{
    // 1bpp: bytes = max(1, (width + 7) / 8)
    assert(photo_bytes_per_row_1bpp(0) == 1);
    assert(photo_bytes_per_row_1bpp(1) == 1);
    assert(photo_bytes_per_row_1bpp(8) == 1);
    assert(photo_bytes_per_row_1bpp(9) == 2);
    assert(photo_bytes_per_row_1bpp(16) == 2);
    assert(photo_bytes_per_row_1bpp(17) == 3);

    // 2bpp: bytes = max(1, (width + 3) / 4)
    assert(photo_bytes_per_row_2bpp(0) == 1);
    assert(photo_bytes_per_row_2bpp(1) == 1);
    assert(photo_bytes_per_row_2bpp(4) == 1);
    assert(photo_bytes_per_row_2bpp(5) == 2);
    assert(photo_bytes_per_row_2bpp(8) == 2);
    assert(photo_bytes_per_row_2bpp(9) == 3);
}

void test_image_type_checks(void)
{
    // Width and height <= 0 are invalid
    assert(!photo_is_bwry_2bpp(0, 10, 100));
    assert(!photo_is_bwry_2bpp(10, 0, 100));
    assert(!photo_is_mono_1bpp(0, 10, 100));
    assert(!photo_is_mono_1bpp(10, 0, 100));

    // Valid size check:
    // For 10x10 image:
    // 1bpp: bytes_per_row = max(1, (10 + 7)/8) = 2. Total size = 2 * 10 = 20.
    assert(photo_is_mono_1bpp(10, 10, 20));
    assert(photo_is_mono_1bpp(10, 10, 25));
    assert(!photo_is_mono_1bpp(10, 10, 19));

    // 2bpp: bytes_per_row = max(1, (10 + 3)/4) = 3. Total size = 3 * 10 = 30.
    assert(photo_is_bwry_2bpp(10, 10, 30));
    assert(photo_is_bwry_2bpp(10, 10, 45));
    assert(!photo_is_bwry_2bpp(10, 10, 29));
}

void test_read_pixel(void)
{
    // 1bpp image: 8x8. bytes_per_row = 1.
    // Let's populate a buffer with a pattern: 0xAA (10101010)
    // Bits: 1, 0, 1, 0, 1, 0, 1, 0
    // So x=0 should be WHITE, x=1 BLACK, x=2 WHITE, x=3 BLACK...
    const int bpr_1bpp     = photo_bytes_per_row_1bpp(8); /* == 1 */
    uint8_t   data_1bpp[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, 0, 0) == RAWDRAW_COLOR_WHITE);
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, 1, 0) == RAWDRAW_COLOR_BLACK);
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, 0, 1) == RAWDRAW_COLOR_BLACK); // y=1 has 0x55 (01010101)
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, 1, 1) == RAWDRAW_COLOR_WHITE);

    // Out of bounds / invalid inputs
    assert(photo_read_pixel(NULL, 8, bpr_1bpp, false, 0, 0) == RAWDRAW_COLOR_BLACK);
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, -1, 0) == RAWDRAW_COLOR_BLACK);
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, 8, 0) == RAWDRAW_COLOR_BLACK);
    assert(photo_read_pixel(data_1bpp, 8, bpr_1bpp, false, 0, 8) == RAWDRAW_COLOR_BLACK);

    // 2bpp image: 4x4. bytes_per_row = 1.
    // 2bpp colors: 0=BLACK, 1=WHITE, 2=YELLOW, 3=RED
    // Let's pack pixels: x=0: 0 (00), x=1: 1 (01), x=2: 2 (10), x=3: 3 (11)
    // Byte: 00 01 10 11 = 0x1B
    const int bpr_2bpp     = photo_bytes_per_row_2bpp(4); /* == 1 */
    uint8_t   data_2bpp[4] = {0x1B, 0xE4, 0x1B, 0xE4}; // 0xE4 = 11 10 01 00
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 0, 0) == RAWDRAW_COLOR_BLACK);
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 1, 0) == RAWDRAW_COLOR_WHITE);
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 2, 0) == RAWDRAW_COLOR_YELLOW);
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 3, 0) == RAWDRAW_COLOR_RED);

    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 0, 1) == RAWDRAW_COLOR_RED);
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 1, 1) == RAWDRAW_COLOR_YELLOW);
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 2, 1) == RAWDRAW_COLOR_WHITE);
    assert(photo_read_pixel(data_2bpp, 4, bpr_2bpp, true, 3, 1) == RAWDRAW_COLOR_BLACK);
}

int main(void)
{
    test_bytes_per_row();
    test_image_type_checks();
    test_read_pixel();
    printf("photo_blit tests passed successfully!\n");
    return 0;
}
