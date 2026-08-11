#include "rawdraw_util.h"
#include "photo_blit.h"

int photo_bytes_per_row_1bpp(int width)
{
    return RD_MAX(1, (width + 7) / 8);
}

int photo_bytes_per_row_2bpp(int width)
{
    return RD_MAX(1, (width + 3) / 4);
}

bool photo_is_bwry_2bpp(int width, int height, uint32_t size)
{
    return width > 0 && height > 0 && size >= (uint32_t)(photo_bytes_per_row_2bpp(width) * height);
}

bool photo_is_mono_1bpp(int width, int height, uint32_t size)
{
    return width > 0 && height > 0 && size >= (uint32_t)(photo_bytes_per_row_1bpp(width) * height);
}

rawdraw_color_t photo_read_pixel(const uint8_t *data, uint32_t size, int bpr, bool bwry2bpp, int x, int y)
{
    if (!data || bpr <= 0 || x < 0 || y < 0)
        return RAWDRAW_COLOR_BLACK;
    if (bwry2bpp) {
        const int offset = y * bpr + (x >> 2);
        if (offset < 0 || offset >= (int)size)
            return RAWDRAW_COLOR_BLACK;
        const int     shift = 6 - ((x & 0x03) * 2);
        const uint8_t color = (data[offset] >> shift) & 0x03;
        return (rawdraw_color_t)color;
    }

    const int offset = y * bpr + (x >> 3);
    if (offset < 0 || offset >= (int)size)
        return RAWDRAW_COLOR_BLACK;
    const int bit = 7 - (x & 0x07);
    return ((data[offset] >> bit) & 0x01) != 0 ? RAWDRAW_COLOR_WHITE : RAWDRAW_COLOR_BLACK;
}
