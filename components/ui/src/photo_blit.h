#ifndef COMPONENTS_UI_SRC_PHOTO_BLIT_H_
#define COMPONENTS_UI_SRC_PHOTO_BLIT_H_

#include <stdint.h>
#include <stdbool.h>
#include "rawdraw.h"

int             photo_bytes_per_row_1bpp(int width);
int             photo_bytes_per_row_2bpp(int width);
bool            photo_is_bwry_2bpp(int width, int height, uint32_t size);
bool            photo_is_mono_1bpp(int width, int height, uint32_t size);
rawdraw_color_t photo_read_pixel(const uint8_t *data, uint32_t size, int bpr, bool bwry2bpp, int x, int y);

#endif // COMPONENTS_UI_SRC_PHOTO_BLIT_H_
