#include "sam3xa.h"
#include "init.h"
#include "tft_cfg.h"

// 0 = normal UART polarity, 1 = inverted data polarity (USART0 INVDATA)
#define UART_INVERT 0
#define TFT_SPI_WAIT_TIMEOUT 1000000u

static uint8_t g_tft_cs_active = 0u;
static uint8_t g_tft_spi_error = 0u;

static void tft_delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- > 0u) {
        __asm__ volatile ("nop");
    }
}

static void tft_delay_ms(uint32_t ms)
{
    while (ms-- > 0u) {
        /* ~1ms @ 84MHz, rough delay for reset timing */
        tft_delay_cycles(21000u);
    }
}

static uint32_t tft_spi_calc_scbr(void)
{
    uint32_t target_hz = TFT_CFG_SPI_START_HZ;
    uint32_t mck_hz = TFT_CFG_MCK_HZ;
    uint32_t scbr;

    if (target_hz > TFT_CFG_SPI_MAX_HZ) {
        target_hz = TFT_CFG_SPI_MAX_HZ;
    }
    if (target_hz == 0u) {
        target_hz = 1u;
    }
    if (mck_hz == 0u) {
        mck_hz = 84000000u;
    }

    scbr = (mck_hz + target_hz - 1u) / target_hz; /* ceil(mck/target) */
    if (scbr < 1u) {
        scbr = 1u;
    }
    if (scbr > 255u) {
        scbr = 255u;
    }

    return scbr;
}

/* *****************************************************************
    INIT BLOCK
***************************************************************** */

void uart_init(void) {
    /* 0. Снять защиту записи */
    PMC->PMC_WPMR = 0x504D4300u;   /* "PMC" + WPEN=0 */
    PIOA->PIO_WPMR = 0x50494F00u;  /* "PIO" + WPEN=0 */

    /* 1. Включить тактирование PIOA и USART0 */
    PMC->PMC_PCER0 = (1u << ID_PIOA);
    PMC->PMC_PCER0 = (1u << ID_USART0);

    /* 2. Настроить PA10(RXD0) и PA11(TXD0) как периферию A */
    PIOA->PIO_PDR   = (1u << 10) | (1u << 11);
    PIOA->PIO_ABSR &= ~((1u << 10) | (1u << 11)); /* 0 = Peripheral A */
    PIOA->PIO_PUER  = (1u << 10); /* подтяжка RXD0 */

    /* 3. Полный сброс и отключение USART0 */
    USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX |
                    US_CR_RXDIS | US_CR_TXDIS |
                    US_CR_RSTSTA;
    USART0->US_IDR = 0xFFFFFFFFu;

    /* 4. Режим: async, MCK, 8N1, без паритета */
    uint32_t usart0_mr =
        US_MR_USART_MODE_NORMAL |
        US_MR_USCLKS_MCK |
        US_MR_CHRL_8_BIT |
        US_MR_PAR_NO |
        US_MR_NBSTOP_1_BIT;
#if UART_INVERT
    usart0_mr |= US_MR_INVDATA;
#endif
    USART0->US_MR = usart0_mr;

    /* 5. Baudrate = 84MHz / (16 * 115200) ~= 45 */
    USART0->US_BRGR = 45u;

    /* 6. Включить RX/TX */
    USART0->US_CR = US_CR_RXEN | US_CR_TXEN;
}

void uart1_init(void)
{
    /* 1) Включить тактирование USART1 */
    PMC->PMC_PCER0 = (1u << ID_USART1);

    /* 2) PA12(TXD1) + PA13(RXD1) -> Peripheral A */
    PIOA->PIO_PDR   = (1u << 12) | (1u << 13);   // отключить PIO, отдать периферии
    PIOA->PIO_ABSR &= ~((1u << 12) | (1u << 13));/* 0 = Peripheral A */

    /* (опционально) подтяжка на RX (PA12 = RXD1) */
    PIOA->PIO_PUER  = (1u << 12);

    /* 3) Сброс и отключение TX/RX */
    USART1->US_CR = US_CR_RSTRX | US_CR_RSTTX |
                    US_CR_RXDIS | US_CR_TXDIS;

    /* 4) Режим: async, MCK, 8N1 */
    USART1->US_MR =
        US_MR_USART_MODE_NORMAL |
        US_MR_USCLKS_MCK |
        US_MR_CHRL_8_BIT |
        US_MR_PAR_NO |
        US_MR_NBSTOP_1_BIT;

    /* 5) Baud = MCK/(16*CD). MCK=84MHz -> 115200 => CD ~ 45.57 -> 45 */
    USART1->US_BRGR = 45u;

    /* 6) Включить TX/RX */
    USART1->US_CR = US_CR_RXEN | US_CR_TXEN;
}

void uart_pa8_init(void)
{
    /* UART (ID_UART=8) on PA8(URXD) / PA9(UTXD), Peripheral A */
    PMC->PMC_PCER0 = (1u << ID_UART);

    PIOA->PIO_PDR   = (1u << 8) | (1u << 9);
    PIOA->PIO_ABSR &= ~((1u << 8) | (1u << 9));
    PIOA->PIO_PUER  = (1u << 8);

    UART->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX |
                    UART_CR_RXDIS | UART_CR_TXDIS |
                    UART_CR_RSTSTA;
    UART->UART_IDR = 0xFFFFFFFFu;

    UART->UART_MR = UART_MR_PAR_NO | UART_MR_CHMODE_NORMAL;
    UART->UART_BRGR = 45u; /* 84MHz / (16 * 115200) */
    UART->UART_CR = UART_CR_RXEN | UART_CR_TXEN;
}

// Инициализация PB27
void gpio_init_out(void) {
  TEST_PIO->PIO_PER  = TEST_MASK;
  TEST_PIO->PIO_OER  = TEST_MASK;
  TEST_PIO->PIO_CODR = TEST_MASK;
}

// Инициализация PB26 для вывода сигнала синхронизации
void sync_out_init(void) {
  PMC->PMC_PCER0 = (1u << ID_PIOB);
  SYNC_OUT_PIO->PIO_PER = SYNC_OUT_MASK;
  SYNC_OUT_PIO->PIO_OER = SYNC_OUT_MASK;
  SYNC_OUT_PIO->PIO_CODR = SYNC_OUT_MASK; // low
}

void dacc_init(void) {
  // Включить тактирование DACC (ID_DACC в PMC_PCER1, т.к. >31)
  PMC->PMC_PCER1 = (1u << (ID_DACC - 32));

  // (Опционально) снять защиту записи DACC, если включена
  // В SAM3X у DACC есть WPMR. Для начала можно просто отключить WP:
  DACC->DACC_WPMR = 0x44414300; // "DAC", WPEN=0

  // Сброс
  DACC->DACC_CR = DACC_CR_SWRST;

  // Режим:
  // - TRGEN_DIS: обновляем вручную
  // - WORD_HALF: 12-bit
  // - TAG_EN: удобно писать в один регистр и выбирать канал
  DACC->DACC_MR =
      DACC_MR_TRGEN_DIS |
      DACC_MR_WORD_HALF |
      DACC_MR_TAG_EN |
      DACC_MR_STARTUP_8;

  // Разрешить каналы
  DACC->DACC_CHER = DACC_CHER_CH0 | DACC_CHER_CH1;
}

void tft_io_init(void)
{
    uint32_t scbr = tft_spi_calc_scbr();

    /* Clocks for PIOA/PIOC/SPI0 */
    PMC->PMC_PCER0 = (1u << ID_PIOA);
    PMC->PMC_PCER0 = (1u << ID_PIOC);
    PMC->PMC_PCER0 = (1u << ID_SPI0);

    /* PA26(MOSI), PA27(SPCK), PA25(MISO) -> Peripheral A */
    TFT_SPI_PIO->PIO_PDR = TFT_SPI_MOSI_MASK | TFT_SPI_SCK_MASK | TFT_SPI_MISO_MASK;
    TFT_SPI_PIO->PIO_ABSR &= ~(TFT_SPI_MOSI_MASK | TFT_SPI_SCK_MASK | TFT_SPI_MISO_MASK);

    /* CS on PA28 and mirrored CS on PC29 as GPIO outputs, default high */
    TFT_SPI_PIO->PIO_PER = TFT_SPI_CS_MASK;
    TFT_SPI_PIO->PIO_OER = TFT_SPI_CS_MASK;
    TFT_SPI_PIO->PIO_SODR = TFT_SPI_CS_MASK;

    TFT_CS2_PIO->PIO_PER = TFT_CS2_MASK;
    TFT_CS2_PIO->PIO_OER = TFT_CS2_MASK;
    TFT_CS2_PIO->PIO_SODR = TFT_CS2_MASK;

    /* PC24(DC), PC25(RST) -> GPIO output, default high */
    TFT_DC_PIO->PIO_PER = TFT_DC_MASK;
    TFT_DC_PIO->PIO_OER = TFT_DC_MASK;
    TFT_DC_PIO->PIO_SODR = TFT_DC_MASK;

    TFT_RST_PIO->PIO_PER = TFT_RST_MASK;
    TFT_RST_PIO->PIO_OER = TFT_RST_MASK;
    TFT_RST_PIO->PIO_SODR = TFT_RST_MASK;

    /* SPI0 reset and master init, Mode 0, 8-bit, CS handled by GPIO */
    SPI0->SPI_CR = SPI_CR_SWRST;
    SPI0->SPI_CR = SPI_CR_SWRST;

    SPI0->SPI_MR = SPI_MR_MSTR | SPI_MR_MODFDIS;
    SPI0->SPI_CSR[0] = SPI_CSR_NCPHA | SPI_CSR_BITS_8_BIT | SPI_CSR_SCBR(scbr);

    SPI0->SPI_CR = SPI_CR_SPIEN;
    g_tft_cs_active = 0u;
    g_tft_spi_error = 0u;
}

void tft_hw_reset(void)
{
    TFT_RST_PIO->PIO_CODR = TFT_RST_MASK;
    tft_delay_ms(20u);
    TFT_RST_PIO->PIO_SODR = TFT_RST_MASK;
    tft_delay_ms(20u);
}

void tft_cs_low(void)
{
    TFT_SPI_PIO->PIO_CODR = TFT_SPI_CS_MASK;
    TFT_CS2_PIO->PIO_CODR = TFT_CS2_MASK;
    g_tft_cs_active = 1u;
}

void tft_cs_high(void)
{
    uint32_t timeout = TFT_SPI_WAIT_TIMEOUT;

    while ((SPI0->SPI_SR & SPI_SR_TXEMPTY) == 0u) {
        if (timeout-- == 0u) {
            g_tft_spi_error = 1u;
            break;
        }
    }
    TFT_SPI_PIO->PIO_SODR = TFT_SPI_CS_MASK;
    TFT_CS2_PIO->PIO_SODR = TFT_CS2_MASK;
    g_tft_cs_active = 0u;
}

void tft_dc_cmd(void)
{
    TFT_DC_PIO->PIO_CODR = TFT_DC_MASK;
}

void tft_dc_data(void)
{
    TFT_DC_PIO->PIO_SODR = TFT_DC_MASK;
}

void tft_spi_tx8(uint8_t v)
{
    uint32_t timeout = TFT_SPI_WAIT_TIMEOUT;

    while ((SPI0->SPI_SR & SPI_SR_TDRE) == 0u) {
        if (timeout-- == 0u) {
            g_tft_spi_error = 1u;
            return;
        }
    }
    SPI0->SPI_TDR = SPI_TDR_TD(v);
    timeout = TFT_SPI_WAIT_TIMEOUT;
    while ((SPI0->SPI_SR & SPI_SR_RDRF) == 0u) {
        if (timeout-- == 0u) {
            g_tft_spi_error = 1u;
            return;
        }
    }
    (void)SPI0->SPI_RDR;

    (void)g_tft_cs_active;
}

void tft_spi_clear_error(void)
{
    g_tft_spi_error = 0u;
}

uint8_t tft_spi_has_error(void)
{
    return g_tft_spi_error;
}
