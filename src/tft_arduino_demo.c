#include "sam3xa.h"

#include <stdint.h>

// Taken from known-good Arduino sketch behavior.
#define TFT_W 480u
#define TFT_H 320u

#define SPI_SCBR_DIV 32u
#define MADCTL_VALUE 0x28u
#define COLMOD_VALUE 0x55u

#define DC_BIT   (1u << 24)
#define RST_BIT  (1u << 25)
#define CS_A_BIT (1u << 28)
#define CS_C_BIT (1u << 29)

#define SPI_MISO (1u << 25)
#define SPI_MOSI (1u << 26)
#define SPI_SCK  (1u << 27)

static inline void delay_cycles(volatile uint32_t c)
{
    while (c-- > 0u) {
        __asm__ volatile ("nop");
    }
}

static void delay_ms(uint32_t ms)
{
    while (ms-- > 0u) {
        delay_cycles(84000u / 4u);
    }
}

static inline void dc_low(void)  { PIOC->PIO_CODR = DC_BIT; }
static inline void dc_high(void) { PIOC->PIO_SODR = DC_BIT; }
static inline void rst_low(void) { PIOC->PIO_CODR = RST_BIT; }
static inline void rst_high(void){ PIOC->PIO_SODR = RST_BIT; }
static inline void cs_low(void)  { PIOA->PIO_CODR = CS_A_BIT; PIOC->PIO_CODR = CS_C_BIT; }
static inline void cs_high(void) { PIOA->PIO_SODR = CS_A_BIT; PIOC->PIO_SODR = CS_C_BIT; }

static inline void spi0_write8(uint8_t v)
{
    while ((SPI0->SPI_SR & SPI_SR_TDRE) == 0u) {}
    SPI0->SPI_TDR = (uint32_t)v;
    while ((SPI0->SPI_SR & SPI_SR_RDRF) == 0u) {}
    (void)SPI0->SPI_RDR;
}

static inline void spi0_write16(uint16_t v)
{
    while ((SPI0->SPI_SR & SPI_SR_TDRE) == 0u) {}
    SPI0->SPI_TDR = (uint32_t)v;
    while ((SPI0->SPI_SR & SPI_SR_RDRF) == 0u) {}
    (void)SPI0->SPI_RDR;
}

static inline void tft_begin(void) { cs_low(); }
static inline void tft_end(void) { cs_high(); }
static inline void tft_cmd(uint8_t c) { dc_low(); spi0_write8(c); }
static inline void tft_data8(uint8_t d) { dc_high(); spi0_write8(d); }

static void spi0_init_16bit(void)
{
    PMC->PMC_WPMR = 0x504D4300u;
    PMC->PMC_PCER0 = (1u << ID_PIOA) | (1u << ID_PIOC) | (1u << ID_SPI0);

    PIOC->PIO_PER = DC_BIT | RST_BIT | CS_C_BIT;
    PIOC->PIO_OER = DC_BIT | RST_BIT | CS_C_BIT;
    PIOC->PIO_SODR = DC_BIT | RST_BIT | CS_C_BIT;

    PIOA->PIO_PER = CS_A_BIT;
    PIOA->PIO_OER = CS_A_BIT;
    PIOA->PIO_SODR = CS_A_BIT;

    PIOA->PIO_PDR = SPI_MISO | SPI_MOSI | SPI_SCK;
    PIOA->PIO_ABSR &= ~(SPI_MISO | SPI_MOSI | SPI_SCK);

    SPI0->SPI_CR = SPI_CR_SWRST;
    SPI0->SPI_CR = SPI_CR_SWRST;
    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS;
    SPI0->SPI_CSR[0] = SPI_CSR_BITS_16_BIT | SPI_CSR_NCPHA | SPI_CSR_SCBR(SPI_SCBR_DIV);
    SPI0->SPI_CR = SPI_CR_SPIEN;

    cs_high();
    dc_high();
    rst_high();
}

static void ili9486_init_like_lib(void)
{
    rst_high(); delay_ms(5u);
    rst_low();  delay_ms(20u);
    rst_high(); delay_ms(150u);

    tft_begin(); tft_cmd(0x01u); tft_end(); delay_ms(150u);
    tft_begin(); tft_cmd(0x11u); tft_end(); delay_ms(180u);

    tft_begin(); tft_cmd(0x36u); tft_data8(MADCTL_VALUE); tft_end();
    tft_begin(); tft_cmd(0x3Au); tft_data8(COLMOD_VALUE); tft_end();
    tft_begin(); tft_cmd(0x13u); tft_end(); delay_ms(10u);
    tft_begin(); tft_cmd(0x29u); tft_end(); delay_ms(50u);
}

static inline void window_ramwr_begin(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    tft_begin();
    tft_cmd(0x2Au);
    dc_high();
    spi0_write8((uint8_t)(x0 >> 8)); spi0_write8((uint8_t)x0);
    spi0_write8((uint8_t)(x1 >> 8)); spi0_write8((uint8_t)x1);

    dc_low(); spi0_write8(0x2Bu);
    dc_high();
    spi0_write8((uint8_t)(y0 >> 8)); spi0_write8((uint8_t)y0);
    spi0_write8((uint8_t)(y1 >> 8)); spi0_write8((uint8_t)y1);

    dc_low(); spi0_write8(0x2Cu);
    dc_high();
}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c565)
{
    uint16_t ww;
    uint32_t px;

    if ((x >= TFT_W) || (y >= TFT_H) || (w == 0u) || (h == 0u)) {
        return;
    }
    if ((uint32_t)x + (uint32_t)w > TFT_W) {
        w = (uint16_t)(TFT_W - x);
    }
    if ((uint32_t)y + (uint32_t)h > TFT_H) {
        h = (uint16_t)(TFT_H - y);
    }

    window_ramwr_begin(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    ww = swap16(c565);
    px = (uint32_t)w * (uint32_t)h;
    while (px-- > 0u) {
        spi0_write16(ww);
    }
    tft_end();
}

static void fill_screen(uint16_t c565)
{
    fill_rect(0u, 0u, TFT_W, TFT_H, c565);
}

static const uint8_t font5x7_digits[11][5] = {
    {0x3Eu,0x51u,0x49u,0x45u,0x3Eu}, {0x00u,0x42u,0x7Fu,0x40u,0x00u},
    {0x42u,0x61u,0x51u,0x49u,0x46u}, {0x21u,0x41u,0x45u,0x4Bu,0x31u},
    {0x18u,0x14u,0x12u,0x7Fu,0x10u}, {0x27u,0x45u,0x45u,0x45u,0x39u},
    {0x3Cu,0x4Au,0x49u,0x49u,0x30u}, {0x01u,0x71u,0x09u,0x05u,0x03u},
    {0x36u,0x49u,0x49u,0x49u,0x36u}, {0x06u,0x49u,0x49u,0x29u,0x1Eu},
    {0x00u,0x60u,0x60u,0x00u,0x00u}
};

static void draw_char_big(uint16_t x, uint16_t y, char c, uint8_t s, uint16_t fg, uint16_t bg)
{
    const uint8_t *cols = 0;
    if ((c >= '0') && (c <= '9')) {
        cols = font5x7_digits[(uint8_t)(c - '0')];
    } else if (c == '.') {
        cols = font5x7_digits[10];
    } else {
        return;
    }

    fill_rect(x, y, (uint16_t)(5u * s), (uint16_t)(7u * s), bg);
    for (uint8_t cx = 0u; cx < 5u; cx++) {
        uint8_t bits = cols[cx];
        for (uint8_t cy = 0u; cy < 7u; cy++) {
            if ((bits & (1u << cy)) != 0u) {
                fill_rect((uint16_t)(x + cx * s), (uint16_t)(y + cy * s), s, s, fg);
            }
        }
    }
}

static uint16_t text_width_px(const char *p, uint8_t s)
{
    uint16_t w = 0u;
    while (*p != '\0') {
        w = (uint16_t)(w + 6u * s);
        p++;
    }
    if (w != 0u) {
        w = (uint16_t)(w - s);
    }
    return w;
}

static void draw_text_big_center(const char *p, uint8_t s, uint16_t fg, uint16_t bg)
{
    uint16_t tw = text_width_px(p, s);
    uint16_t th = (uint16_t)(7u * s);
    uint16_t x = (uint16_t)((TFT_W - tw) / 2u);
    uint16_t y = (uint16_t)((TFT_H - th) / 2u);

    while (*p != '\0') {
        draw_char_big(x, y, *p, s, fg, bg);
        x = (uint16_t)(x + 6u * s);
        p++;
    }
}

void tft_arduino_demo_run(void)
{
    spi0_init_16bit();
    ili9486_init_like_lib();

    for (uint16_t y = 0u; y < TFT_H; y++) {
        fill_rect(0u, y, TFT_W, 1u, rgb565(0u, 0u, 0u));
    }
    delay_ms(300u);

    fill_screen(rgb565(0u, 0u, 0u));
    draw_text_big_center("10.5", 10u, rgb565(255u, 255u, 255u), rgb565(0u, 0u, 0u));
}
