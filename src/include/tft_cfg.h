#ifndef TFT_CFG_H
#define TFT_CFG_H

#include <stdint.h>

/* SPI clock plan: start safe, keep max for future speed-up policy */
#define TFT_CFG_SPI_START_HZ      8000000u
#define TFT_CFG_SPI_MAX_HZ       20000000u
#define TFT_CFG_SPI_FALLBACK_HZ  12000000u

/* Working reference profile (display-main.zip): 480x320 */
#define TFT_CFG_PANEL_WIDTH      480u
#define TFT_CFG_PANEL_HEIGHT     320u

/* 0..3 rotation mapping used by tft_set_rotation() */
#define TFT_CFG_DEFAULT_ROTATION 1u

/* 1 = BGR, 0 = RGB (MADCTL BGR bit) */
#define TFT_CFG_COLOR_ORDER_BGR  1u

/*
 * 1 = swap logical width/height for rotations 1 and 3.
 * Keep 0 for panels/clones where MADCTL does not fully remap coordinates.
 */
#define TFT_CFG_ROTATION_SWAP_WH 0u

/*
 * 0 = COLMOD 0x55 (RGB565), 1 = COLMOD 0x66 (RGB666/18-bit).
 * Many 3.5" RPi clones with ILI9488-class behavior need 18-bit mode,
 * otherwise color may look like grayscale.
 */
#define TFT_CFG_PIXEL_18BIT      1u

/*
 * 1 = send RAMWR (0x2C) in same CS transaction as pixel stream.
 * Some clone controllers require this for correct per-window writes.
 */
#define TFT_CFG_RAMWR_INLINE     0u

/* System master clock used for SCBR calculation (SAM3X8E default 84 MHz) */
#define TFT_CFG_MCK_HZ          84000000u

#endif /* TFT_CFG_H */
