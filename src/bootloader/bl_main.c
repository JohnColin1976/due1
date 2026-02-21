#include "sam3xa.h"
#include "init.h"
#include "boot_cfg.h"
#include <stddef.h>
#include <stdint.h>

static inline int usart0_getc(uint8_t *out)
{
    if ((USART0->US_CSR & US_CSR_RXRDY) == 0u) {
        return 0;
    }
    *out = (uint8_t)(USART0->US_RHR & 0xFFu);
    return 1;
}

static inline void usart0_putc(uint8_t c)
{
    while ((USART0->US_CSR & US_CSR_TXRDY) == 0u) {}
    USART0->US_THR = (uint32_t)c;
}

static inline void usart0_wait_tx_empty(void)
{
    while ((USART0->US_CSR & US_CSR_TXEMPTY) == 0u) {}
}

static void usart0_puts(const char *s)
{
    while (*s != '\0') {
        usart0_putc((uint8_t)*s++);
    }
}

static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void wr_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (uint32_t b = 0; b < 8u; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t timeout_loops(uint32_t timeout_ms)
{
    uint32_t cpu = (SystemCoreClock != 0u) ? SystemCoreClock : 84000000u;
    uint32_t loops = (cpu / 6000u) * timeout_ms;
    return (loops == 0u) ? 1u : loops;
}

static int usart0_read_byte_timeout(uint8_t *out, uint32_t timeout_ms)
{
    uint32_t loops = timeout_loops(timeout_ms);
    while (loops-- > 0u) {
        if (usart0_getc(out) != 0) {
            return 1;
        }
    }
    return 0;
}

static int usart0_read_exact(uint8_t *dst, size_t len, uint32_t timeout_ms)
{
    for (size_t i = 0; i < len; i++) {
        if (usart0_read_byte_timeout(&dst[i], timeout_ms) == 0) {
            return 0;
        }
    }
    return 1;
}

static int app_vector_is_valid(void)
{
    uint32_t sp = *(volatile const uint32_t *)(APP_START_ADDR + 0u);
    uint32_t rv = *(volatile const uint32_t *)(APP_START_ADDR + 4u);

    if (sp < 0x20070000u || sp > 0x20088000u) {
        return 0;
    }
    if (rv < APP_START_ADDR || rv > FLASH_END_ADDR) {
        return 0;
    }
    if ((rv & 1u) == 0u) {
        return 0;
    }
    return 1;
}

static void jump_to_app(void)
{
    uint32_t app_sp = *(volatile const uint32_t *)(APP_START_ADDR + 0u);
    uint32_t app_reset = *(volatile const uint32_t *)(APP_START_ADDR + 4u);
    void (*app_entry)(void) = (void (*)(void))app_reset;

    __disable_irq();
    SysTick->CTRL = 0u;
    SCB->VTOR = APP_START_ADDR;
    __set_MSP(app_sp);
    app_entry();

    while (1) {}
}

static int wait_sync_window(void)
{
    static const uint8_t sync[4] = {0x55u, 0xAAu, 0x55u, 0xAAu};
    uint32_t idx = 0u;
    uint32_t guard = 84u * 1000u * BL_SYNC_WAIT_MS;

    while (guard-- > 0u) {
        uint8_t b = 0u;
        if (usart0_getc(&b) == 0) {
            continue;
        }

        if (b == sync[idx]) {
            idx++;
            if (idx == 4u) {
                return 1;
            }
        } else {
            idx = (b == sync[0]) ? 1u : 0u;
        }
    }

    return 0;
}

#define FLASH_PAGE_SIZE        (256u)
#define FLASH_PAGES_TOTAL      (FLASH_SIZE_BYTES / FLASH_PAGE_SIZE)
#define FLASH_PAGES_PER_BANK   (1024u)

typedef struct {
    uint8_t cmd;
    uint16_t payload_len;
    uint16_t seq;
} bl_cmd_hdr_t;

static uint8_t g_cmd_payload[BL_FRAME_MAX_PAYLOAD];
static uint8_t g_crc_buf[10 + BL_FRAME_MAX_PAYLOAD];
static uint8_t g_page_buf[FLASH_PAGE_SIZE];

static int flash_page_arg(uint32_t page, Efc **efc, uint32_t *farg)
{
    if (page >= FLASH_PAGES_TOTAL) {
        return 0;
    }
    if (page < FLASH_PAGES_PER_BANK) {
        *efc = EFC0;
        *farg = page;
    } else {
        *efc = EFC1;
        *farg = page - FLASH_PAGES_PER_BANK;
    }
    return 1;
}

__attribute__((section(".ramfunc"), noinline))
static int efc_wait_ready(Efc *efc, uint32_t timeout_ms)
{
    uint32_t loops = timeout_loops(timeout_ms);
    while (loops-- > 0u) {
        if ((efc->EEFC_FSR & EEFC_FSR_FRDY) != 0u) {
            return 1;
        }
    }
    return 0;
}

__attribute__((section(".ramfunc"), noinline))
static int efc_do_command(Efc *efc, uint32_t farg, uint32_t fcmd, uint32_t timeout_ms)
{
    if (efc_wait_ready(efc, timeout_ms) == 0) {
        return 0;
    }

    /* Disable sequential code optimization during flash command execution. */
    uint32_t fmr0 = EFC0->EEFC_FMR;
    uint32_t fmr1 = EFC1->EEFC_FMR;
    EFC0->EEFC_FMR = fmr0 | EEFC_FMR_SCOD;
    EFC1->EEFC_FMR = fmr1 | EEFC_FMR_SCOD;

    __disable_irq();
    efc->EEFC_FCR = EEFC_FCR_FKEY_PASSWD | EEFC_FCR_FARG(farg) | fcmd;
    uint32_t fsr = efc->EEFC_FSR;
    __enable_irq();

    if (efc_wait_ready(efc, timeout_ms) == 0) {
        EFC0->EEFC_FMR = fmr0;
        EFC1->EEFC_FMR = fmr1;
        return 0;
    }

    EFC0->EEFC_FMR = fmr0;
    EFC1->EEFC_FMR = fmr1;

    if ((fsr & (EEFC_FSR_FCMDE | EEFC_FSR_FLOCKE)) != 0u) {
        return 0;
    }

    return 1;
}

__attribute__((section(".ramfunc"), noinline))
static int flash_program_page(uint32_t page, const uint8_t *page_data, uint32_t cmd)
{
    Efc *efc = 0;
    uint32_t farg = 0u;
    if (flash_page_arg(page, &efc, &farg) == 0) {
        return 0;
    }

    uint32_t addr = FLASH_BASE_ADDR + (page * FLASH_PAGE_SIZE);
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    for (uint32_t i = 0u; i < (FLASH_PAGE_SIZE / 4u); i++) {
        uint32_t w = rd_u32_le(page_data + (i * 4u));
        dst[i] = w;
    }

    return efc_do_command(efc, farg, cmd, 100u);
}

static int flash_erase_range(uint32_t addr, uint32_t len)
{
    (void)addr;
    (void)len;
    return 1;
}

static int flash_write_range(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t cur = 0u;
    while (cur < len) {
        uint32_t abs_addr = addr + cur;
        uint32_t page = (abs_addr - FLASH_BASE_ADDR) / FLASH_PAGE_SIZE;
        uint32_t page_addr = FLASH_BASE_ADDR + page * FLASH_PAGE_SIZE;
        uint32_t page_off = abs_addr - page_addr;
        uint32_t copy_len = FLASH_PAGE_SIZE - page_off;
        if (copy_len > (len - cur)) {
            copy_len = len - cur;
        }

        const uint8_t *flash_src = (const uint8_t *)page_addr;
        for (uint32_t i = 0u; i < FLASH_PAGE_SIZE; i++) {
            g_page_buf[i] = flash_src[i];
        }
        for (uint32_t i = 0u; i < copy_len; i++) {
            g_page_buf[page_off + i] = data[cur + i];
        }

        if (flash_program_page(page, g_page_buf, EEFC_FCR_FCMD_EWP) == 0) {
            return 0;
        }

        cur += copy_len;
    }

    return 1;
}

static int range_is_valid(uint32_t addr, uint32_t len)
{
    if (len == 0u) {
        return 0;
    }
    if (addr < APP_START_ADDR) {
        return 0;
    }
    uint64_t end = (uint64_t)addr + (uint64_t)len - 1u;
    if (end > FLASH_END_ADDR) {
        return 0;
    }
    return 1;
}

static void send_resp(uint8_t code, uint8_t status, uint16_t seq, uint32_t detail,
                      const uint8_t *payload, uint16_t payload_len)
{
    uint8_t hdr[10];
    hdr[0] = code;
    hdr[1] = status;
    wr_u16_le(hdr + 2, payload_len);
    wr_u16_le(hdr + 4, seq);
    wr_u32_le(hdr + 6, detail);

    for (size_t i = 0; i < sizeof(hdr); i++) {
        g_crc_buf[i] = hdr[i];
    }
    for (uint16_t i = 0u; i < payload_len; i++) {
        g_crc_buf[sizeof(hdr) + i] = payload[i];
    }
    uint32_t crc = crc32_calc(g_crc_buf, sizeof(hdr) + payload_len);

    for (size_t i = 0; i < sizeof(hdr); i++) {
        usart0_putc(hdr[i]);
    }
    uint8_t crc_b[4];
    wr_u32_le(crc_b, crc);
    for (size_t i = 0; i < sizeof(crc_b); i++) {
        usart0_putc(crc_b[i]);
    }
    for (uint16_t i = 0; i < payload_len; i++) {
        usart0_putc(payload[i]);
    }
}

static void send_ack(uint16_t seq, const uint8_t *payload, uint16_t payload_len)
{
    send_resp(BL_ACK, 0u, seq, 0u, payload, payload_len);
}

static void send_nak(uint16_t seq, uint8_t err, uint32_t detail)
{
    send_resp(BL_NAK, err, seq, detail, 0, 0u);
}

static int recv_frame_start(uint8_t *first2)
{
    static const uint8_t sync[4] = {0x55u, 0xAAu, 0x55u, 0xAAu};
    uint32_t sync_idx = 0u;
    uint8_t b = 0u;

    while (1) {
        if (usart0_getc(&b) == 0) {
            continue;
        }

        if (b == sync[sync_idx]) {
            sync_idx++;
            if (sync_idx == 4u) {
                usart0_puts("BL>OK\n");
                sync_idx = 0u;
            }
        } else {
            sync_idx = (b == sync[0]) ? 1u : 0u;
        }

        first2[0] = first2[1];
        first2[1] = b;
        if (first2[0] == (uint8_t)(BL_FRAME_MAGIC & 0xFFu) &&
            first2[1] == (uint8_t)(BL_FRAME_MAGIC >> 8)) {
            return 1;
        }
    }
}

static int recv_command(bl_cmd_hdr_t *out_hdr)
{
    uint8_t first2[2] = {0u, 0u};
    uint8_t hdr[8];
    uint8_t crc_buf[4];

    if (recv_frame_start(first2) == 0) {
        return 0;
    }

    hdr[0] = first2[0];
    hdr[1] = first2[1];
    if (usart0_read_exact(hdr + 2, 6u, BL_DATA_TIMEOUT_MS) == 0) {
        send_nak(0u, BL_ERR_TIMEOUT, 1u);
        return 0;
    }

    uint8_t version = hdr[2];
    uint8_t cmd = hdr[3];
    uint16_t len = rd_u16_le(hdr + 4);
    uint16_t seq = rd_u16_le(hdr + 6);

    if (version != BL_FRAME_VERSION) {
        send_nak(seq, BL_ERR_INVALID_COMMAND, 2u);
        return 0;
    }
    if (len > BL_FRAME_MAX_PAYLOAD) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, len);
        return 0;
    }

    if (len > 0u) {
        if (usart0_read_exact(g_cmd_payload, len, BL_DATA_TIMEOUT_MS) == 0) {
            send_nak(seq, BL_ERR_TIMEOUT, 2u);
            return 0;
        }
    }

    if (usart0_read_exact(crc_buf, 4u, BL_DATA_TIMEOUT_MS) == 0) {
        send_nak(seq, BL_ERR_TIMEOUT, 3u);
        return 0;
    }

    for (uint16_t i = 0u; i < 8u; i++) {
        g_crc_buf[i] = hdr[i];
    }
    for (uint16_t i = 0u; i < len; i++) {
        g_crc_buf[8u + i] = g_cmd_payload[i];
    }

    uint32_t rx_crc = rd_u32_le(crc_buf);
    uint32_t calc_crc = crc32_calc(g_crc_buf, (size_t)8u + len);
    if (rx_crc != calc_crc) {
        send_nak(seq, BL_ERR_CRC_MISMATCH, rx_crc ^ calc_crc);
        return 0;
    }

    out_hdr->cmd = cmd;
    out_hdr->payload_len = len;
    out_hdr->seq = seq;
    return 1;
}

static int handle_cmd_info(uint16_t seq, uint16_t payload_len)
{
    uint8_t out[16];
    if (payload_len != 0u) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, payload_len);
        return 0;
    }
    wr_u32_le(out + 0, 0x00010000u);
    wr_u32_le(out + 4, APP_START_ADDR);
    wr_u32_le(out + 8, FLASH_END_ADDR);
    wr_u16_le(out + 12, FLASH_PAGE_SIZE);
    wr_u16_le(out + 14, BL_CHUNK_SIZE);
    send_ack(seq, out, sizeof(out));
    return 0;
}

static int handle_cmd_erase(uint16_t seq, uint16_t payload_len)
{
    if (payload_len != 8u) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, payload_len);
        return 0;
    }
    uint32_t addr = rd_u32_le(g_cmd_payload + 0);
    uint32_t len = rd_u32_le(g_cmd_payload + 4);
    if (range_is_valid(addr, len) == 0) {
        send_nak(seq, BL_ERR_INVALID_RANGE, addr);
        return 0;
    }
    if (flash_erase_range(addr, len) == 0) {
        send_nak(seq, BL_ERR_FLASH_ERASE, addr);
        return 0;
    }
    send_ack(seq, 0, 0u);
    return 0;
}

static int handle_cmd_write(uint16_t seq, uint16_t payload_len)
{
    if (payload_len < 10u) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, payload_len);
        return 0;
    }
    uint32_t addr = rd_u32_le(g_cmd_payload + 0);
    uint16_t len = rd_u16_le(g_cmd_payload + 4);
    uint32_t crc = rd_u32_le(g_cmd_payload + 6);
    if ((uint32_t)len + 10u != payload_len) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, payload_len);
        return 0;
    }
    if (len > BL_CHUNK_SIZE) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, len);
        return 0;
    }
    if (range_is_valid(addr, len) == 0) {
        send_nak(seq, BL_ERR_INVALID_RANGE, addr);
        return 0;
    }
    if (crc32_calc(g_cmd_payload + 10, len) != crc) {
        send_nak(seq, BL_ERR_CRC_MISMATCH, addr);
        return 0;
    }
    if (flash_write_range(addr, g_cmd_payload + 10, len) == 0) {
        send_nak(seq, BL_ERR_FLASH_PROGRAM, addr);
        return 0;
    }
    send_ack(seq, 0, 0u);
    return 0;
}

static int handle_cmd_verify(uint16_t seq, uint16_t payload_len)
{
    if (payload_len != 12u) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, payload_len);
        return 0;
    }
    uint32_t addr = rd_u32_le(g_cmd_payload + 0);
    uint32_t len = rd_u32_le(g_cmd_payload + 4);
    uint32_t expected = rd_u32_le(g_cmd_payload + 8);
    if (range_is_valid(addr, len) == 0) {
        send_nak(seq, BL_ERR_INVALID_RANGE, addr);
        return 0;
    }
    uint32_t calc = crc32_calc((const uint8_t *)addr, len);
    if (calc != expected) {
        send_nak(seq, BL_ERR_VERIFY_FAILED, calc);
        return 0;
    }
    send_ack(seq, 0, 0u);
    return 0;
}

static int handle_cmd_run(uint16_t seq, uint16_t payload_len)
{
    if (payload_len != 0u) {
        send_nak(seq, BL_ERR_INVALID_LENGTH, payload_len);
        return 0;
    }
    if (app_vector_is_valid() == 0) {
        send_nak(seq, BL_ERR_INTERNAL_STATE, 0u);
        return 0;
    }
    send_ack(seq, 0, 0u);
    usart0_wait_tx_empty();
    for (volatile uint32_t i = 0; i < 200000u; i++) {
        __NOP();
    }
    NVIC_SystemReset();
    return 1;
}

static void bootloader_protocol_loop(void)
{
    while (1) {
        bl_cmd_hdr_t hdr;
        if (recv_command(&hdr) == 0) {
            continue;
        }

        switch (hdr.cmd) {
            case BL_CMD_INFO:
                (void)handle_cmd_info(hdr.seq, hdr.payload_len);
                break;
            case BL_CMD_ERASE:
                (void)handle_cmd_erase(hdr.seq, hdr.payload_len);
                break;
            case BL_CMD_WRITE:
                (void)handle_cmd_write(hdr.seq, hdr.payload_len);
                break;
            case BL_CMD_VERIFY:
                (void)handle_cmd_verify(hdr.seq, hdr.payload_len);
                break;
            case BL_CMD_RUN:
                (void)handle_cmd_run(hdr.seq, hdr.payload_len);
                break;
            case BL_CMD_ABORT:
                send_ack(hdr.seq, 0, 0u);
                break;
            default:
                send_nak(hdr.seq, BL_ERR_INVALID_COMMAND, hdr.cmd);
                break;
        }
    }
}

int main(void)
{
    uart_init();

    if (REG_GPBR_GPBR == MAGIC_UPDATE) {
        REG_GPBR_GPBR = 0u;
        bootloader_protocol_loop();
    }

    if (wait_sync_window() != 0) {
        bootloader_protocol_loop();
    }

    if (app_vector_is_valid() != 0) {
        jump_to_app();
    }

    bootloader_protocol_loop();
}
