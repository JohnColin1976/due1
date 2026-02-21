# Patch Plan: Bootloader Instrumentation over USART0 (SAM3X8E)

## 1. Scope and baseline

This plan defines project changes required to implement firmware update over `USART0` without GPIO control, based on:
- `src/docs/bootloader_spec.md`
- `src/docs/protocol_v1_0.md`

Target behavior:
- Application (APP) receives `ENTER_BOOT`, sets GPBR flag, executes software reset.
- Bootloader (BL) starts first from flash base, checks GPBR/sync window, enters update mode, supports ERASE/WRITE/VERIFY/RUN.
- Host side on T113 (`/dev/ttyS1`) performs full update cycle.

Configuration baseline from spec:
- `FLASH_BASE = 0x00080000`
- `BL_SIZE = 0x8000` (32 KB)
- `APP_START = 0x00088000`
- `MAGIC_UPDATE = 0xB00710AD`
- `GPBR_INDEX = 0`
- `BAUD = 115200`
- `CHUNK_SIZE = 1024`

## 2. Current project impact (as-is)

Current state:
- Single firmware target in `CMakeLists.txt` (`due_cmsis`) linked at `0x00080000`.
- Single linker script `linker/sam3x8e_flash.ld`.
- Startup code sets VTOR to current vector table (`startup/startup_sam3x8e.c`).
- Existing app already uses `USART0` and protocol v1 frame parser (`src/main.c`).

Implication:
- Build and memory layout must be split into two images: BL and APP.
- APP vector table must move to `APP_START`.
- BL must own reset flow and jump logic to APP.

## 3. Patch sets

## Patch Set A: Memory layout and build split

Goal: introduce independent BL and APP artifacts with deterministic addresses.

Changes:
- Update `CMakeLists.txt`:
  - Keep APP target (rename from `due_cmsis` to `due_app` or add alias).
  - Add BL target `due_bootloader`.
  - Extract shared compile/include options into helper function/macros to avoid duplication.
  - Emit separate artifacts: `due_bootloader.elf/.bin/.hex`, `due_app.elf/.bin/.hex`.
- Add linker scripts:
  - `linker/sam3x8e_bootloader.ld`:
    - FLASH origin `0x00080000`, length `32K`.
  - `linker/sam3x8e_app.ld`:
    - FLASH origin `0x00088000`, length `512K - 32K`.
  - RAM unchanged (`0x20070000`, `96K`).
- Keep section symbols compatible with existing startup (`_sidata/_sdata/_edata/_sbss/_ebss/_estack`).

Acceptance for A:
- Both targets build independently.
- Map files confirm BL in `[0x00080000..0x00087FFF]`, APP starts at `0x00088000`.

## Patch Set B: Shared update config and protocol constants

Goal: single source of truth for update addresses, flags, timeouts, error codes.

Changes:
- Add `src/include/boot_cfg.h`:
  - flash boundaries, `BL_SIZE`, `APP_START`, `MAGIC_UPDATE`, `GPBR_INDEX`.
  - protocol command IDs (`CMD_INFO..CMD_RUN/CMD_ABORT`), ACK/NAK bytes.
  - limits/timeouts (`CHUNK_SIZE`, data timeout, sync wait).
- Add `src/include/boot_proto.h` for on-wire packet structs/helpers (LE encode/decode, CRC32 API).
- Add `src/boot_common/crc32.c` (+ header if needed) reused by BL and host-side tooling.

Acceptance for B:
- No duplicated hardcoded addresses in BL/APP code.
- Constants match `bootloader_spec.md`.

## Patch Set C: Bootloader runtime (new sources)

Goal: implement minimal BL flow and USART0 update protocol.

New files (suggested):
- `src/bootloader/bl_main.c`
- `src/bootloader/bl_uart0.c`
- `src/bootloader/bl_flash.c`
- `src/bootloader/bl_protocol.c`
- `src/bootloader/bl_jump.c`

Behavior to implement:
- Startup path:
  - Init clock/peripherals required for USART0 + EFC.
  - Check `RTC->RTC_GPBR[GPBR_INDEX]`.
  - If `MAGIC_UPDATE`: clear GPBR, enter update mode immediately.
  - Else wait `T_SYNC_WAIT_MS=150ms` for `0x55AA55AA`; if hit, enter update mode, else jump APP.
- Update mode protocol:
  - SYNC response (`BL>OK\n` or binary ACK; pick one and document).
  - `CMD_INFO`: return BL version, app range, page size, max chunk.
  - `CMD_ERASE`: range check (`>= APP_START`, within flash), erase pages via EEFC, return ACK/NAK.
  - `CMD_WRITE`: range+length check, CRC32 block check, program flash, ACK/NAK.
  - `CMD_VERIFY`: compute total CRC32 on flash range and compare.
  - `CMD_RUN`: ACK then jump APP.
  - Optional `CMD_ABORT`.
- Error handling:
  - Return `NAK + error_code`.
  - On verify failure remain in BL (no auto-run).
- Jump to APP:
  - `__disable_irq();`
  - set `SCB->VTOR = APP_START;`
  - load MSP from `*(uint32_t*)APP_START`;
  - branch to reset handler at `*(uint32_t*)(APP_START+4)`.

Acceptance for C:
- BL responds on `USART0` within required timing.
- BL never writes into BL flash region.

## Patch Set D: APP changes for ENTER_BOOT and compatibility

Goal: APP can reliably transfer control to BL without GPIO.

Changes in existing APP files:
- `src/main.c`:
  - Add command handling path for `ENTER_BOOT` trigger in current command parser.
  - Recommended mapping in protocol v1 `COMMAND`:
    - add command ID `ENTER_BOOT` (new ID, e.g. 8) or reserve vendor extension range.
  - On trigger:
    - optional ACK/diagnostic response to host,
    - `RTC->RTC_GPBR[GPBR_INDEX] = MAGIC_UPDATE`,
    - short barrier/delay if needed,
    - `NVIC_SystemReset()`.
- `src/init.c`:
  - Ensure `USART0` init remains BL-compatible (8N1, same baud).
  - Avoid peripheral state that can block BL after reset.
- `src/docs/protocol_v1_0.md`:
  - Add/update `COMMAND` section for `ENTER_BOOT` semantics and expected ACK behavior.

Acceptance for D:
- With APP running, host command causes reboot into BL and SYNC response.

## Patch Set E: Startup and VTOR correctness

Goal: safe operation when APP vector table is no longer at flash base.

Changes:
- Reuse existing `startup/startup_sam3x8e.c` for both targets; ensure:
  - For BL build, VTOR points to BL vector table (base flash).
  - For APP build, VTOR points to APP vector table at `APP_START`.
- If needed, add compile-time define (`APP_VECTOR_BASE`) and set VTOR explicitly.

Acceptance for E:
- APP interrupts/exceptions work correctly after standalone APP boot via BL jump.

## Patch Set F: Host-side updater tooling (T113 flow)

Goal: provide practical instrumentation to execute SRS update algorithm over `/dev/ttyS1`.

Changes (new tool):
- Add `tools/uart_bl_update.py` (or C utility if required by project policy) implementing:
  - serial open/config (`raw`, `115200`, `8N1`, no flow control),
  - `ENTER_BOOT` trigger (via existing APP protocol command),
  - SYNC loop up to `T_BOOT_WAIT=5s`,
  - INFO/ERASE/WRITE(with retries)/VERIFY/RUN state machine,
  - block retries (`N_RETRY=3`),
  - structured logs with BL info, progress, error codes.
- Add CLI args:
  - `--port`, `--baud`, `--firmware`, `--chunk`, `--no-run`, `--start-addr`.
- Add safety checks:
  - file size bounds against app region,
  - optional dry-run/protocol-only check.

Acceptance for F:
- Tool can complete update cycle on target and produce actionable logs on failures.

## Patch Set G: Documentation and flashing workflow

Goal: ensure reproducible integration for developers.

Changes:
- Update `README.md` with:
  - build commands for BL and APP,
  - flash order (BL first, APP second),
  - updater tool usage examples.
- Add `src/docs/bootloader_protocol.md` (or section in existing spec) with final wire format and examples.
- Add `tools/jlink_flash_sam3x8e.jlink` variants or template notes for separate BL/APP flashing.

Acceptance for G:
- New contributor can build, flash, and run update flow using docs only.

## 4. Recommended implementation order

1. Patch Set A (build/link split).
2. Patch Set B (shared constants).
3. Patch Set C (BL core without full protocol first: SYNC + RUN jump test).
4. Patch Set D (APP ENTER_BOOT command).
5. Patch Set C completion (ERASE/WRITE/VERIFY + errors).
6. Patch Set E (VTOR/interrupt validation).
7. Patch Set F (host tool).
8. Patch Set G (docs/final polish).

## 5. Verification matrix

- Build-time:
  - BL and APP compile with no new warnings.
  - Size checks: BL <= `BL_SIZE`.
- Static/runtime safety:
  - Attempted writes below `APP_START` return NAK `invalid address/range`.
  - CRC mismatch path returns NAK `CRC mismatch`.
- End-to-end:
  - APP running -> ENTER_BOOT -> BL SYNC/INFO.
  - ERASE/WRITE/VERIFY success -> RUN -> APP boots.
  - Forced corruption -> VERIFY fails and BL remains active.
- Regression:
  - Existing protocol parser behavior in APP remains valid for non-update traffic.

## 6. Risks and mitigations

- Risk: linker overlap or wrong VTOR causes hard faults.
  - Mitigation: enforce map-file CI check for ranges; explicit jump sequence and VTOR set.
- Risk: flash timing/EEFC errors at high baud.
  - Mitigation: start with `115200`, make `921600` optional compile-time flag.
- Risk: protocol ambiguity between textual and binary handshake.
  - Mitigation: freeze one handshake variant in `bootloader_protocol.md` and updater implementation.
- Risk: partial update leaves device unusable.
  - Mitigation: BL immutable region protection; no auto-run on verify failure.

## 7. Deliverables

- New BL target + linker.
- Updated APP with ENTER_BOOT command.
- BL protocol implementation on USART0.
- T113 updater utility.
- Updated project docs for build/flash/update.
