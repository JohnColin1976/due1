#include "tft_ili9486.h"

#include "init.h"
#include "tft_cfg.h"

static uint16_t g_tft_width = TFT_ILI9486_WIDTH;
static uint16_t g_tft_height = TFT_ILI9486_HEIGHT;
static uint8_t g_tft_spi_err_reported = 0u;

__attribute__((weak)) void tft_diag_puts(const char *s)
{
    (void)s;
}

static void tft_report_spi_error_if_any(void)
{
    if ((tft_spi_has_error() != 0u) && (g_tft_spi_err_reported == 0u)) {
        tft_diag_puts("TFT:SPI_ERR\r\n");
        g_tft_spi_err_reported = 1u;
    }
}

static void tft_delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- > 0u) {
        __asm__ volatile ("nop");
    }
}

static void tft_delay_ms(uint32_t ms)
{
    while (ms-- > 0u) {
        /* rough ~1ms @ 84MHz */
        tft_delay_cycles(21000u);
    }
}

static void write_cmd(uint8_t cmd)
{
    if (tft_spi_has_error() != 0u) {
        return;
    }
    tft_cs_low();
    tft_dc_cmd();
    tft_spi_tx8(cmd);
    tft_cs_high();
    tft_report_spi_error_if_any();
}

static void write_data(uint8_t data)
{
    if (tft_spi_has_error() != 0u) {
        return;
    }
    tft_dc_data();
    tft_spi_tx8(data);
    tft_report_spi_error_if_any();
}

static void write_cmd_data(uint8_t cmd, const uint8_t *data, uint32_t n)
{
    if (tft_spi_has_error() != 0u) {
        return;
    }
    tft_cs_low();
    tft_dc_cmd();
    tft_spi_tx8(cmd);
    for (uint32_t i = 0u; i < n; i++) {
        write_data(data[i]);
    }
    tft_cs_high();
    tft_report_spi_error_if_any();
}

void tft_set_rotation(uint8_t r)
{
    static const uint8_t madctl_lut[4] = {
        0x40u, /* MX */
        0x20u, /* MV */
        0x80u, /* MY */
        0xE0u  /* MX | MY | MV */
    };
    uint8_t madctl_color = (TFT_CFG_COLOR_ORDER_BGR != 0u) ? 0x08u : 0x00u;
    uint8_t madctl;

    switch (r & 0x03u) {
        case 0u:
            madctl = (uint8_t)(madctl_lut[0] | madctl_color);
            g_tft_width = TFT_ILI9486_WIDTH;
            g_tft_height = TFT_ILI9486_HEIGHT;
            break;
        case 1u:
            madctl = (uint8_t)(madctl_lut[1] | madctl_color);
#if (TFT_CFG_ROTATION_SWAP_WH != 0u)
            g_tft_width = TFT_ILI9486_HEIGHT;
            g_tft_height = TFT_ILI9486_WIDTH;
#else
            g_tft_width = TFT_ILI9486_WIDTH;
            g_tft_height = TFT_ILI9486_HEIGHT;
#endif
            break;
        case 2u:
            madctl = (uint8_t)(madctl_lut[2] | madctl_color);
            g_tft_width = TFT_ILI9486_WIDTH;
            g_tft_height = TFT_ILI9486_HEIGHT;
            break;
        default:
            madctl = (uint8_t)(madctl_lut[3] | madctl_color);
#if (TFT_CFG_ROTATION_SWAP_WH != 0u)
            g_tft_width = TFT_ILI9486_HEIGHT;
            g_tft_height = TFT_ILI9486_WIDTH;
#else
            g_tft_width = TFT_ILI9486_WIDTH;
            g_tft_height = TFT_ILI9486_HEIGHT;
#endif
            break;
    }

    write_cmd_data(0x36u, &madctl, 1u);
}

void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    if ((x0 > x1) || (y0 > y1)) {
        return;
    }
    if ((x0 >= g_tft_width) || (y0 >= g_tft_height)) {
        return;
    }
    if (x1 >= g_tft_width) {
        x1 = (uint16_t)(g_tft_width - 1u);
    }
    if (y1 >= g_tft_height) {
        y1 = (uint16_t)(g_tft_height - 1u);
    }

    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)(x0 & 0xFFu);
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)(x1 & 0xFFu);
    write_cmd_data(0x2Au, data, 4u);

    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)(y0 & 0xFFu);
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)(y1 & 0xFFu);
    write_cmd_data(0x2Bu, data, 4u);

#if (TFT_CFG_RAMWR_INLINE == 0u)
    write_cmd(0x2Cu);
#endif
}

void tft_write_color565(uint16_t color, uint32_t count)
{
#if (TFT_CFG_PIXEL_18BIT != 0u)
    uint8_t r8 = (uint8_t)((((color >> 11) & 0x1Fu) * 255u) / 31u);
    uint8_t g8 = (uint8_t)((((color >> 5)  & 0x3Fu) * 255u) / 63u);
    uint8_t b8 = (uint8_t)((( color        & 0x1Fu) * 255u) / 31u);
#else
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFu);
#endif

    if (tft_spi_has_error() != 0u) {
        return;
    }

    tft_cs_low();
#if (TFT_CFG_RAMWR_INLINE != 0u)
    tft_dc_cmd();
    tft_spi_tx8(0x2Cu);
    tft_dc_data();
#endif

    while (count-- > 0u) {
#if (TFT_CFG_PIXEL_18BIT != 0u)
        write_data((uint8_t)(r8 & 0xFCu));
        write_data((uint8_t)(g8 & 0xFCu));
        write_data((uint8_t)(b8 & 0xFCu));
#else
        write_data(hi);
        write_data(lo);
#endif
    }
    tft_cs_high();
    tft_report_spi_error_if_any();
}

void tft_fill_screen(uint16_t color)
{
    tft_fill_rect_solid(0u, 0u, g_tft_width, g_tft_height, color);
}

void tft_fill_rect_solid(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t x1;
    uint16_t y1;
    uint32_t count;
#if (TFT_CFG_PIXEL_18BIT != 0u)
    uint8_t r8 = (uint8_t)((((color >> 11) & 0x1Fu) * 255u) / 31u);
    uint8_t g8 = (uint8_t)((((color >> 5)  & 0x3Fu) * 255u) / 63u);
    uint8_t b8 = (uint8_t)((( color        & 0x1Fu) * 255u) / 31u);
#else
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFu);
#endif

    if ((w == 0u) || (h == 0u)) {
        return;
    }
    if ((x >= g_tft_width) || (y >= g_tft_height)) {
        return;
    }
    if ((uint32_t)x + (uint32_t)w > g_tft_width) {
        w = (uint16_t)(g_tft_width - x);
    }
    if ((uint32_t)y + (uint32_t)h > g_tft_height) {
        h = (uint16_t)(g_tft_height - y);
    }
    if ((w == 0u) || (h == 0u)) {
        return;
    }
    if (tft_spi_has_error() != 0u) {
        return;
    }

    x1 = (uint16_t)(x + w - 1u);
    y1 = (uint16_t)(y + h - 1u);

    tft_cs_low();

    tft_dc_cmd();
    tft_spi_tx8(0x2Au);
    tft_dc_data();
    tft_spi_tx8((uint8_t)(x >> 8));
    tft_spi_tx8((uint8_t)(x & 0xFFu));
    tft_spi_tx8((uint8_t)(x1 >> 8));
    tft_spi_tx8((uint8_t)(x1 & 0xFFu));

    tft_dc_cmd();
    tft_spi_tx8(0x2Bu);
    tft_dc_data();
    tft_spi_tx8((uint8_t)(y >> 8));
    tft_spi_tx8((uint8_t)(y & 0xFFu));
    tft_spi_tx8((uint8_t)(y1 >> 8));
    tft_spi_tx8((uint8_t)(y1 & 0xFFu));

    tft_dc_cmd();
    tft_spi_tx8(0x2Cu);
    tft_dc_data();

    count = (uint32_t)w * (uint32_t)h;
    while (count-- > 0u) {
#if (TFT_CFG_PIXEL_18BIT != 0u)
        tft_spi_tx8((uint8_t)(r8 & 0xFCu));
        tft_spi_tx8((uint8_t)(g8 & 0xFCu));
        tft_spi_tx8((uint8_t)(b8 & 0xFCu));
#else
        tft_spi_tx8(hi);
        tft_spi_tx8(lo);
#endif
    }

    tft_cs_high();
    tft_report_spi_error_if_any();
}

uint16_t tft_get_width(void)
{
    return g_tft_width;
}

uint16_t tft_get_height(void)
{
    return g_tft_height;
}

void tft_init(void)
{
    static const uint8_t gamma_p[15] = {
        0x0Fu, 0x1Fu, 0x1Cu, 0x0Cu, 0x0Fu, 0x08u, 0x48u, 0x98u,
        0x37u, 0x0Au, 0x13u, 0x04u, 0x11u, 0x0Du, 0x00u
    };
    static const uint8_t gamma_n[15] = {
        0x0Fu, 0x32u, 0x2Eu, 0x0Bu, 0x0Du, 0x05u, 0x47u, 0x75u,
        0x37u, 0x06u, 0x10u, 0x03u, 0x24u, 0x20u, 0x00u
    };
    static const uint8_t gamma_d[15] = {
        0x0Fu, 0x32u, 0x2Eu, 0x0Bu, 0x0Du, 0x05u, 0x47u, 0x75u,
        0x37u, 0x06u, 0x10u, 0x03u, 0x24u, 0x20u, 0x00u
    };
#if (TFT_CFG_PIXEL_18BIT != 0u)
    static const uint8_t if_pixel_fmt = 0x66u;
#else
    static const uint8_t if_pixel_fmt = 0x55u;
#endif

    tft_spi_clear_error();
    g_tft_spi_err_reported = 0u;

    tft_io_init();
    tft_hw_reset();
    tft_diag_puts("TFT:RESET_OK\r\n");

    write_cmd_data(0x3Au, &if_pixel_fmt, 1u);
    tft_set_rotation(TFT_CFG_DEFAULT_ROTATION);
    write_cmd_data(0xE0u, gamma_p, 15u);
    write_cmd_data(0xE1u, gamma_n, 15u);
    write_cmd_data(0xE2u, gamma_d, 15u);

    write_cmd(0x11u); /* SLPOUT */
    tft_delay_ms(150u);
    write_cmd(0x29u); /* DISPON */
    tft_report_spi_error_if_any();
}
