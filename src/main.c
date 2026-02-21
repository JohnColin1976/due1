#include "sam3xa.h"
#include "init.h"
#include "boot_cfg.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// protocol_v1_0 constants
static const uint16_t ECU_MAGIC = 0xEC10u;
static const uint8_t ECU_VERSION = 1u;

static const uint8_t ECU_MSG_HELLO = 0x01u;
static const uint8_t ECU_MSG_TELEMETRY = 0x02u;
static const uint8_t ECU_MSG_COMMAND = 0x03u;
static const uint8_t ECU_MSG_ACK = 0x04u;
static const uint8_t ECU_MSG_TIME_SYNC = 0x05u;
static const uint8_t ECU_MSG_EVENT = 0x06u;
static const uint8_t ECU_MSG_CONFIG = 0x07u;
static const uint8_t ECU_MSG_HEARTBEAT = 0x08u;

static const uint16_t ECU_MAX_PAYLOAD = 1024u;

// SLIP (RFC1055)
static const uint8_t SLIP_END = 0xC0u;
static const uint8_t SLIP_ESC = 0xDBu;
static const uint8_t SLIP_ESC_END = 0xDCu;
static const uint8_t SLIP_ESC_ESC = 0xDDu;

static uint8_t g_rx_frame[2048];
static size_t g_rx_len = 0u;
static bool g_rx_esc = false;
static bool g_rx_in_frame = false;
volatile uint32_t g_ms_tick = 0u;

static void app_dbg_heartbeat_poll(void);

void SysTick_Handler(void)
{
    g_ms_tick++;
}

static inline int usart0_getc(uint8_t *out)
{
    if ((USART0->US_CSR & US_CSR_RXRDY) == 0u) {
        return 0;
    }
    *out = (uint8_t)(USART0->US_RHR & 0xFFu);
    return 1;
}

static inline void uart_dbg_putc(uint8_t c)
{
    while ((UART->UART_SR & UART_SR_TXRDY) == 0u) {}
    UART->UART_THR = (uint32_t)c;
}

static void uart_dbg_puts(const char *s)
{
    while (*s != '\0') {
        uart_dbg_putc((uint8_t)*s++);
    }
}

static void uart_dbg_put_u32(uint32_t v)
{
    char buf[10];
    uint32_t n = 0u;

    if (v == 0u) {
        uart_dbg_putc((uint8_t)'0');
        return;
    }

    while (v != 0u) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0u) {
        uart_dbg_putc((uint8_t)buf[--n]);
    }
}

static void uart_dbg_put_hex4(uint8_t v)
{
    v &= 0x0Fu;
    uart_dbg_putc((uint8_t)(v < 10u ? ('0' + v) : ('A' + (v - 10u))));
}

static void uart_dbg_put_hex8(uint8_t v)
{
    uart_dbg_put_hex4((uint8_t)(v >> 4));
    uart_dbg_put_hex4(v);
}

static void uart_dbg_put_hex16(uint16_t v)
{
    uart_dbg_put_hex8((uint8_t)(v >> 8));
    uart_dbg_put_hex8((uint8_t)(v & 0xFFu));
}

static void uart_dbg_put_hex32(uint32_t v)
{
    uart_dbg_put_hex16((uint16_t)(v >> 16));
    uart_dbg_put_hex16((uint16_t)(v & 0xFFFFu));
}

static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd_u64_le(const uint8_t *p)
{
    return (uint64_t)rd_u32_le(p) | ((uint64_t)rd_u32_le(p + 4) << 32);
}

static float rd_f32_le(const uint8_t *p)
{
    union {
        uint32_t u;
        float f;
    } u;
    u.u = rd_u32_le(p);
    return u.f;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8u; b++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void print_error(void)
{
    uart_dbg_puts("ERROR\r\n");
}

static void print_msg_name(uint8_t msg_type)
{
    switch (msg_type) {
        case ECU_MSG_HELLO: uart_dbg_puts("HELLO"); break;
        case ECU_MSG_TELEMETRY: uart_dbg_puts("TELEMETRY"); break;
        case ECU_MSG_COMMAND: uart_dbg_puts("COMMAND"); break;
        case ECU_MSG_ACK: uart_dbg_puts("ACK"); break;
        case ECU_MSG_TIME_SYNC: uart_dbg_puts("TIME_SYNC"); break;
        case ECU_MSG_EVENT: uart_dbg_puts("EVENT"); break;
        case ECU_MSG_CONFIG: uart_dbg_puts("CONFIG"); break;
        case ECU_MSG_HEARTBEAT: uart_dbg_puts("HEARTBEAT"); break;
        default: uart_dbg_puts("UNKNOWN"); break;
    }
}

static void print_float_2(float f)
{
    if (f < 0.0f) {
        uart_dbg_putc((uint8_t)'-');
        f = -f;
    }

    uint32_t whole = (uint32_t)f;
    float frac_f = (f - (float)whole) * 100.0f;
    uint32_t frac = (uint32_t)(frac_f + 0.5f);
    if (frac >= 100u) {
        whole++;
        frac = 0u;
    }

    uart_dbg_put_u32(whole);
    uart_dbg_putc((uint8_t)'.');
    uart_dbg_putc((uint8_t)('0' + (frac / 10u)));
    uart_dbg_putc((uint8_t)('0' + (frac % 10u)));
}

static void handle_ecu_frame(const uint8_t *buf, size_t len)
{
    if (len < 18u) {
        print_error();
        return;
    }

    uint16_t magic = rd_u16_le(buf + 0);
    uint8_t version = buf[2];
    uint8_t msg_type = buf[3];
    uint8_t src = buf[4];
    uint8_t dst = buf[5];
    uint16_t seq = rd_u16_le(buf + 6);
    uint16_t flags = rd_u16_le(buf + 8);
    uint16_t payload_len = rd_u16_le(buf + 10);

    if (magic != ECU_MAGIC || version != ECU_VERSION) {
        print_error();
        return;
    }
    if (payload_len > ECU_MAX_PAYLOAD) {
        print_error();
        return;
    }
    if (len != (size_t)(16u + payload_len + 2u)) {
        print_error();
        return;
    }

    uint16_t rx_crc = rd_u16_le(buf + 16u + payload_len);
    uint16_t calc_crc = crc16_ccitt(buf, 16u + payload_len);
    if (rx_crc != calc_crc) {
        print_error();
        return;
    }

    const uint8_t *pl = buf + 16u;

    uart_dbg_puts("OK type=");
    print_msg_name(msg_type);
    uart_dbg_puts(" src=");
    uart_dbg_put_u32(src);
    uart_dbg_puts(" dst=");
    uart_dbg_put_u32(dst);
    uart_dbg_puts(" seq=");
    uart_dbg_put_u32(seq);
    uart_dbg_puts(" flags=0x");
    uart_dbg_put_hex16(flags);
    uart_dbg_puts(" len=");
    uart_dbg_put_u32(payload_len);

    switch (msg_type) {
        case ECU_MSG_HELLO:
            if (payload_len != 13u) {
                print_error();
                return;
            }
            uart_dbg_puts(" node=");
            uart_dbg_put_u32(pl[0]);
            uart_dbg_puts(" fw=0x");
            uart_dbg_put_hex32(rd_u32_le(pl + 1));
            uart_dbg_puts(" build=0x");
            uart_dbg_put_hex32(rd_u32_le(pl + 5));
            uart_dbg_puts(" cap=0x");
            uart_dbg_put_hex32(rd_u32_le(pl + 9));
            break;

        case ECU_MSG_TELEMETRY:
            if (payload_len != 24u) {
                print_error();
                return;
            }
            uart_dbg_puts(" up=");
            uart_dbg_put_u32(rd_u32_le(pl + 0));
            uart_dbg_puts(" st=0x");
            uart_dbg_put_hex16(rd_u16_le(pl + 4));
            uart_dbg_puts(" err=");
            uart_dbg_put_u32(rd_u16_le(pl + 6));
            uart_dbg_puts(" V=");
            print_float_2(rd_f32_le(pl + 8));
            uart_dbg_puts(" I=");
            print_float_2(rd_f32_le(pl + 12));
            uart_dbg_puts(" T=");
            print_float_2(rd_f32_le(pl + 16));
            uart_dbg_puts(" RPM=");
            print_float_2(rd_f32_le(pl + 20));
            break;

        case ECU_MSG_COMMAND:
            if (payload_len < 4u) {
                print_error();
                return;
            } else {
                uint16_t cmd_id = rd_u16_le(pl + 0);
                uint16_t param_len = rd_u16_le(pl + 2);
                if (param_len != (uint16_t)(payload_len - 4u)) {
                    print_error();
                    return;
                }
                uart_dbg_puts(" cmd=");
                uart_dbg_put_u32(cmd_id);
                uart_dbg_puts(" param_len=");
                uart_dbg_put_u32(param_len);

                if (cmd_id == ECU_CMD_ENTER_BOOT && param_len == 0u) {
                    uart_dbg_puts(" action=ENTER_BOOT");
                    REG_GPBR_GPBR = MAGIC_UPDATE;
                    uart_dbg_puts(" reboot=1\r\n");
                    uart_dbg_puts("APP enter bootloader\r\n");
                    NVIC_SystemReset();
                }
            }
            break;

        case ECU_MSG_ACK:
            if (payload_len != 4u) {
                print_error();
                return;
            }
            uart_dbg_puts(" ack_seq=");
            uart_dbg_put_u32(rd_u16_le(pl + 0));
            uart_dbg_puts(" status=");
            uart_dbg_put_u32(rd_u16_le(pl + 2));
            break;

        case ECU_MSG_TIME_SYNC:
            if (payload_len != 8u) {
                print_error();
                return;
            } else {
                uint64_t ts = rd_u64_le(pl);
                uart_dbg_puts(" unix_ms=0x");
                uart_dbg_put_hex32((uint32_t)(ts >> 32));
                uart_dbg_put_hex32((uint32_t)(ts & 0xFFFFFFFFu));
            }
            break;

        case ECU_MSG_EVENT:
            if (payload_len < 4u) {
                print_error();
                return;
            } else {
                uint16_t event_code = rd_u16_le(pl + 0);
                uint16_t data_len = rd_u16_le(pl + 2);
                if (data_len != (uint16_t)(payload_len - 4u)) {
                    print_error();
                    return;
                }
                uart_dbg_puts(" event=");
                uart_dbg_put_u32(event_code);
                uart_dbg_puts(" data_len=");
                uart_dbg_put_u32(data_len);
            }
            break;

        case ECU_MSG_HEARTBEAT:
            if (payload_len != 0u) {
                print_error();
                return;
            }
            break;

        default:
            // Unknown msg_type is still a valid frame at transport level.
            break;
    }

    uart_dbg_puts("\r\n");
}

static void slip_rx_reset(void)
{
    g_rx_len = 0u;
    g_rx_esc = false;
    g_rx_in_frame = false;
}

static void slip_rx_byte(uint8_t b)
{
    if (b == SLIP_END) {
        if (g_rx_in_frame && g_rx_len > 0u) {
            handle_ecu_frame(g_rx_frame, g_rx_len);
        }
        g_rx_in_frame = true;
        g_rx_len = 0u;
        g_rx_esc = false;
        return;
    }

    if (!g_rx_in_frame) {
        return;
    }

    if (g_rx_esc) {
        g_rx_esc = false;
        if (b == SLIP_ESC_END) {
            b = SLIP_END;
        } else if (b == SLIP_ESC_ESC) {
            b = SLIP_ESC;
        } else {
            print_error();
            slip_rx_reset();
            return;
        }
    } else if (b == SLIP_ESC) {
        g_rx_esc = true;
        return;
    }

    if (g_rx_len >= sizeof(g_rx_frame)) {
        print_error();
        slip_rx_reset();
        return;
    }
    g_rx_frame[g_rx_len++] = b;
}

static void app_dbg_heartbeat_poll(void)
{
    static uint32_t next_hb_ms = 1000u;
    uint32_t now = g_ms_tick;
    if ((int32_t)(now - next_hb_ms) >= 0) {
        uart_dbg_puts("APP alive ms=");
        uart_dbg_put_u32(now);
        uart_dbg_puts("\r\n");
        next_hb_ms += 1000u;
    }
}

int main(void)
{
    uart_init();      // USART0 on PA10/PA11 (RX input)
    uart_pa8_init();  // UART on PA8/PA9 (debug output on PA9)
    uart_dbg_puts("APP start\r\n");
    __enable_irq();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        uart_dbg_puts("APP systick init error\r\n");
    }

    while (1) {
        uint8_t b = 0u;
        while (usart0_getc(&b) != 0) {
            slip_rx_byte(b);
            app_dbg_heartbeat_poll();
        }
        app_dbg_heartbeat_poll();
    }
}
