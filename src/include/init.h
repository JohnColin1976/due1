#ifndef INIT_H
#define INIT_H

#include <stdint.h>
#include "sam3xa.h"   // чтобы PIOB / Pio были видимы везде, где включили init.h

// Конфигурация под сигнальный PB27 - для вывода на светодиод
// событий, в том числе и за счет различной последовательности
// морганий
#define TEST_PIO      PIOB
#define TEST_PIN      27u
#define TEST_MASK     (1u << TEST_PIN)

// Конфиг пинов SYNC_OUT (пример: PB26) + флаг SYNC_PIO
#define SYNC_OUT_PIO      PIOB
#define SYNC_OUT_PIN      26u
#define SYNC_OUT_MASK     (1u << SYNC_OUT_PIN)

// Сдвиги фаз на 120° и 240° в 32-битной фазе (2^32 * 120/360, 2^32 * 240/360):
#define PHASE_120 0x55555555u  // +120°
#define PHASE_240 0xAAAAAAAau  // +240°

// TFT (3.5" RPi display on SAM3X8E)
#define TFT_DC_PIO      PIOC
#define TFT_DC_PIN      24u
#define TFT_DC_MASK     (1u << TFT_DC_PIN)

#define TFT_RST_PIO     PIOC
#define TFT_RST_PIN     25u
#define TFT_RST_MASK    (1u << TFT_RST_PIN)

// SPI0 pins for TFT
#define TFT_SPI_PIO     PIOA
#define TFT_SPI_MOSI_PIN 26u
#define TFT_SPI_SCK_PIN  27u
#define TFT_SPI_CS_PIN   28u
#define TFT_SPI_MISO_PIN 25u
#define TFT_SPI_MOSI_MASK (1u << TFT_SPI_MOSI_PIN)
#define TFT_SPI_SCK_MASK  (1u << TFT_SPI_SCK_PIN)
#define TFT_SPI_CS_MASK   (1u << TFT_SPI_CS_PIN)
#define TFT_SPI_MISO_MASK (1u << TFT_SPI_MISO_PIN)

/* Some Due shields route CS via D10 clone on PIOC29 as well. */
#define TFT_CS2_PIO      PIOC
#define TFT_CS2_PIN      29u
#define TFT_CS2_MASK     (1u << TFT_CS2_PIN)

void uart_init(void);
void uart1_init(void);
void uart_pa8_init(void);
void gpio_init_out(void);
void sync_out_init(void);
void dacc_init(void);
void tft_io_init(void);
void tft_hw_reset(void);
void tft_cs_low(void);
void tft_cs_high(void);
void tft_dc_cmd(void);
void tft_dc_data(void);
void tft_spi_tx8(uint8_t v);
void tft_spi_clear_error(void);
uint8_t tft_spi_has_error(void);

#endif /* INIT_H */
