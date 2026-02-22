#ifndef TFT_ILI9486_H
#define TFT_ILI9486_H

#include <stdint.h>
#include "tft_cfg.h"

#define TFT_ILI9486_WIDTH   TFT_CFG_PANEL_WIDTH
#define TFT_ILI9486_HEIGHT  TFT_CFG_PANEL_HEIGHT

void tft_init(void);
void tft_set_rotation(uint8_t r);
void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void tft_write_color565(uint16_t color, uint32_t count);
void tft_fill_screen(uint16_t color);
void tft_fill_rect_solid(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
uint16_t tft_get_width(void);
uint16_t tft_get_height(void);

#endif /* TFT_ILI9486_H */
