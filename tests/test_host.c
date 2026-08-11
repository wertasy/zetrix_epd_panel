#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../main/rawdraw.h"

// Copy display driver math for verification
static inline uint8_t pack_2bpp_row_to_1bpp_byte(const uint8_t *row_2bpp, int pixel_x)
{
    uint8_t out = 0x00;
    for (int bit = 0; bit < 8; ++bit) {
        const int     x      = pixel_x + bit;
        const uint8_t packed = row_2bpp[x >> 2];
        const uint8_t shift  = (uint8_t)(6 - ((x & 0x03) << 1));
        const uint8_t color  = (packed >> shift) & 0x03;
        if (color == 1) { // 1 = WHITE
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
        const int dst_bit0 = 2 * src_bit; // bytes2
        const int dst_bit1 = 2 * src_bit + 1; // bytes1

        result |= (uint16_t)(((bytes1 >> src_bit) & 1u) << dst_bit1);
        result |= (uint16_t)(((bytes2 >> src_bit) & 1u) << dst_bit0);
    }
    return result;
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

void test_pixel_packing(void)
{
    printf("Running test_pixel_packing...\n");
    int width      = 400;
    int height     = 300;
    int buffer_len = ((width * 2 + 7) / 8) * height; // 30,000 bytes

    uint8_t *fb = malloc(buffer_len);
    assert(fb != NULL);
    memset(fb, 0x00, buffer_len); // Start with solid black

    // Test 1: Set pixel 0 to WHITE (1)
    rawdraw_set_pixel(fb, width, height, 0, 0, RAWDRAW_COLOR_WHITE);
    // Pixel 0 is bits 7-6 of byte 0. Color is 0b01. Result byte should be 0b01000000 = 0x40.
    assert(fb[0] == 0x40);

    // Test 2: Set pixel 1 to YELLOW (2)
    rawdraw_set_pixel(fb, width, height, 1, 0, RAWDRAW_COLOR_YELLOW);
    // Pixel 1 is bits 5-4 of byte 0. Color is 0b10. Result byte should be 0x40 | (0b10 << 4) = 0x40 | 0x20 = 0x60.
    assert(fb[0] == 0x60);

    // Test 3: Set pixel 2 to RED (3)
    rawdraw_set_pixel(fb, width, height, 2, 0, RAWDRAW_COLOR_RED);
    // Pixel 2 is bits 3-2 of byte 0. Color is 0b11. Result byte should be 0x60 | (0b11 << 2) = 0x60 | 0x0C = 0x6C.
    assert(fb[0] == 0x6C);

    // Test 4: Set pixel 3 to BLACK (0)
    rawdraw_set_pixel(fb, width, height, 3, 0, RAWDRAW_COLOR_BLACK);
    // Pixel 3 is bits 1-0 of byte 0. Color is 0b00. Result byte should stay 0x6C.
    assert(fb[0] == 0x6C);

    // Test 5: Verify pixel reads/writes across rows
    rawdraw_set_pixel(fb, width, height, 4, 1, RAWDRAW_COLOR_WHITE); // pixel 4, row 1
    // Byte offset for row 1: 1 * 100 bytes = 100.
    // Pixel 4 is index 100 + (4 >> 2) = 101.
    // Pixel 4 is bits 7-6 of byte 101. Color is 0b01. Result byte should be 0x40.
    assert(fb[101] == 0x40);

    free(fb);
    printf("test_pixel_packing passed!\n");
}

void test_dither_pattern(void)
{
    printf("Running test_dither_pattern...\n");
    int width      = 400;
    int height     = 300;
    int buffer_len = ((width * 2 + 7) / 8) * height;

    uint8_t *fb = malloc(buffer_len);
    memset(fb, 0x55, buffer_len); // start with white

    // Draw dither pattern on 10x10 area
    rawdraw_draw_dither_rect(fb, width, height, 0, 0, 10, 10);

    // Verify checkerboard: (x + y) % 2 == 0 -> Black (0), otherwise White (1)
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            uint32_t index    = y * 100 + (x >> 2);
            uint8_t  shift    = (uint8_t)(6 - ((x & 0x03) << 1));
            uint8_t  color    = (fb[index] >> shift) & 0x03;
            uint8_t  expected = ((x + y) & 1) ? 1 : 0;
            assert(color == expected);
        }
    }

    free(fb);
    printf("test_dither_pattern passed!\n");
}

void test_frame_diff(void)
{
    printf("Running test_frame_diff...\n");
    int width      = 400;
    int height     = 300;
    int buffer_len = ((width * 2 + 7) / 8) * height;

    uint8_t *prev = malloc(buffer_len);
    uint8_t *curr = malloc(buffer_len);

    memset(prev, 0x55, buffer_len);
    memset(curr, 0x55, buffer_len);

    // Initial diff should be zero
    frame_diff_result_t res = analyze_frame_diff(prev, curr, width, height);
    assert(res.diff_bits == 0);
    assert(res.diff_ratio == 0.0f);

    // Change pixel (0, 0) in curr to black (0).
    // Bits change: 0b01 -> 0b00 (1 bit change: bit 6 goes from 1 to 0).
    curr[0] &= ~0x40;
    res = analyze_frame_diff(prev, curr, width, height);
    assert(res.diff_bits == 1);

    float expected_ratio = 1.0f / (buffer_len * 8.0f);
    assert(res.diff_ratio == expected_ratio);

    free(prev);
    free(curr);
    printf("test_frame_diff passed!\n");
}

void test_partial_encoding(void)
{
    printf("Running test_partial_encoding...\n");
    // Verify that pack_2bpp_row_to_1bpp_byte and bit_interleave function correctly
    uint8_t row_2bpp[100];
    memset(row_2bpp, 0x55, sizeof(row_2bpp)); // all white (1)

    // First byte (pixels 0-7) are all white.
    // pack_2bpp_row_to_1bpp_byte should pack them to 0xFF (since color is 1).
    uint8_t b1 = pack_2bpp_row_to_1bpp_byte(row_2bpp, 0);
    assert(b1 == 0xFF);

    // Change pixel 0 to black (0) and pixel 1 to yellow (2).
    // row_2bpp[0] byte: bits 7-6 = 00 (black), bits 5-4 = 10 (yellow) -> 0x20
    row_2bpp[0] = 0x20;
    b1          = pack_2bpp_row_to_1bpp_byte(row_2bpp, 0);
    // Pixels 4-7 are still white (1), pixels 2-3 are black (0).
    // Result binary: 0b00001111 = 0x0F.
    assert(b1 == 0x0F);

    // Test bit interleaving
    // b1 = 0xAA (0b10101010), b2 = 0x55 (0b01010101)
    uint16_t result = bit_interleave(0xAA, 0x55);
    // For each bit position:
    // dst_bit0 (even bits from b2) -> b2 is 01010101.
    // dst_bit1 (odd bits from b1) -> b1 is 10101010.
    // Let's check bit 0 (src_bit 7):
    // b1 bit 7 is 1, b2 bit 7 is 0.
    // dst_bit0 = 0 (even), dst_bit1 = 1 (odd) -> result bits 15-14 = 10.
    // Indeed:
    // result = 1001 1001 1001 1001 binary = 0x9999
    assert(result == 0x9999);

    printf("test_partial_encoding passed!\n");
}

int main(void)
{
    printf("Starting Host Verification Tests...\n");
    test_pixel_packing();
    test_dither_pattern();
    test_frame_diff();
    test_partial_encoding();
    printf("All verification tests passed successfully!\n");
    return 0;
}
