# Bootloader USART0 Protocol (Current Implementation)

## 1. Transport

- UART: `115200`, `8N1`, no flow control
- Endian: little-endian
- SYNC request from host: `55 AA 55 AA`
- SYNC response from BL: ASCII `BL>OK\n`

## 2. Command Frame (Host -> BL)

Header (8 bytes):
- `magic` `u16` = `0xB10C`
- `version` `u8` = `1`
- `cmd` `u8`
- `payload_len` `u16` (`<=2048`)
- `seq` `u16`

Then:
- `payload[payload_len]`
- `crc32` `u32` over `header(8) + payload`

## 3. Response Frame (BL -> Host)

Header (10 bytes):
- `code` `u8` (`0x79` ACK, `0x1F` NAK)
- `status` `u8` (`0` for ACK, error code for NAK)
- `payload_len` `u16`
- `seq` `u16` (echo)
- `detail` `u32` (optional detail/debug value)

Then:
- `crc32` `u32` over `header(10) + payload`
- `payload[payload_len]`

## 4. Commands

- `0x01 CMD_INFO`, payload empty
- `0x02 CMD_ERASE`, payload: `u32 addr_start, u32 length`
- `0x03 CMD_WRITE`, payload: `u32 addr, u16 length, u32 crc32_block, u8 data[length]`
- `0x04 CMD_VERIFY`, payload: `u32 addr_start, u32 length, u32 crc32_total`
- `0x05 CMD_RUN`, payload empty
- `0x06 CMD_ABORT`, payload empty

## 5. INFO payload

ACK payload (`16` bytes):
- `u32 bl_version` (`0x00010000`)
- `u32 app_start`
- `u32 flash_end`
- `u16 page_size`
- `u16 max_chunk`

## 6. Error Codes (NAK status)

- `0x01` invalid command
- `0x02` invalid address/range
- `0x03` invalid length
- `0x04` CRC mismatch
- `0x05` flash program error
- `0x06` flash erase error
- `0x07` timeout waiting data
- `0x08` verify failed
- `0x09` internal state error

## Notes

- Current implementation performs effective page erase during `CMD_WRITE` via `EWP` (erase+write page).
- `CMD_ERASE` is currently a validation/synchronization stage (range check + ACK).
- Current frame payload limit is `2048` bytes. Recommended write `chunk <= 1024` (because `CMD_WRITE` adds 10-byte metadata).
- Current `CMD_RUN` behavior: BL sends ACK, completes UART TX, then triggers software reset (`NVIC_SystemReset`) to start APP in clean reset context.

## Known-good E2E Result

Validated flow on T113 `/dev/ttyS4`:
- `SYNC -> INFO -> ERASE -> WRITE -> VERIFY -> RUN`
- updater output ends with `RUN: OK`
- APP then outputs to FTDI debug UART:
  - `APP start`
  - periodic `APP alive ms=<ticks>`.
