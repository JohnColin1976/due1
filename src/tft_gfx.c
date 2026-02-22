#include "tft_gfx.h"

#include "tft_ili9486.h"

void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    tft_fill_rect_solid(x, y, 1u, 1u, color);
}

void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    tft_fill_rect_solid(x, y, w, h, color);
}
