# Bootloader Runbook (Due2, T113 `/dev/ttyS4`)

## 1. Build

```bash
cmake --build build --target due_bootloader due_app
```

Artifacts:
- `build/due_bootloader.elf`
- `build/due_app.elf`
- `build/due_app.bin`

## 2. Flash via J-Link (PC)

- Use VS Code task:
  - `Flash Full (BL+APP, J-Link, SAM3X8E, Due2)`

Or flash separately:
- `Flash Bootloader (J-Link, SAM3X8E, Due2)`
- `Flash APP (J-Link, SAM3X8E, Due2)`

## 3. Build host updater on T113

```bash
cd tools
make
```

## 4. Run firmware update from T113

```bash
./uart_bl_update --port /dev/ttyS4 --baud 115200 --firmware due_app.bin
```

Expected updater output:
- `SYNC: OK`
- `INFO: ... app_start=0x00088000 ...`
- `ERASE: OK`
- `WRITE: ...`
- `VERIFY: OK`
- `RUN: OK`

## 5. FTDI debug UART check (PA8/PA9, 115200 8N1)

After `RUN: OK`, APP should print:
- `APP start`
- `APP alive ms=<ticks>` every ~1 second.

## 6. Troubleshooting

- If `RUN: OK`, but no expected APP logs:
  - verify `due_app.bin` on T113 is the latest file from `build/`.
  - check file size consistency (`ls -l due_app.bin`).
- If `SYNC` fails:
  - verify correct port `/dev/ttyS4`.
  - ensure port is not occupied by getty/console service.
- If flashing via J-Link fails to attach:
  - lower SWD speed.
  - try connect under reset.
