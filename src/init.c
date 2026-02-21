#include "sam3xa.h"
#include "init.h"

// 0 = normal UART polarity, 1 = inverted data polarity (USART0 INVDATA)
#define UART_INVERT 0

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
