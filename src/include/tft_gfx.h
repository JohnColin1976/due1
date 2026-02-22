#ifndef TFT_GFX_H
#define TFT_GFX_H

#include <stdint.h>

void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#endif /* TFT_GFX_H */
