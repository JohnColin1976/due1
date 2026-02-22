# Patch Plan: 3.5inch RPi Display on Arduino Due (CMSIS, project `due1`)

## 1. Цель

Добавить в `due1` поддержку TFT-дисплея 3.5inch RPi Display, подключенного к SAM3X8E (Arduino Due), без Arduino HAL, только через CMSIS/регистры SAM3X:

- `SCK  -> PA27` (SPI0 SPCK)
- `MOSI -> PA26` (SPI0 MOSI)
- `CS   -> PA28` (SPI0 NPCS0)
- `DC   -> PC24` (GPIO output)
- `RST  -> PC25` (GPIO output)
- `GND`, `5V`

## 2. Основание (что берем из GxTFT)

Источник поведения: архив `GxTFT.zip`.

Ключевые файлы библиотеки:

- `src/myTFTs/my_3.5_RPi_480x320_DUE.h`
  - профиль для Due + 3.5" RPi, контроллер `ILI9486_WSPI`.
- `src/GxCTRL/GxCTRL_ILI9486_WSPI/GxCTRL_ILI9486_WSPI.cpp`
  - инициализационная последовательность, `MADCTL`, окно, `RAMWR`.
- `src/GxIO/GxIO_SPI/GxIO_SPI.cpp`
  - поведение линий `CS/DC/RST`, режим SPI Mode0, MSB first, целевая частота 20 MHz.

Важно: в GxTFT для этого профиля используется именно `ILI9486_WSPI`.

## 3. Принятые решения для CMSIS-реализации

1. Делать минимальный порт под конкретный дисплей (не универсальный графический фреймворк).
2. Разделить код на слои:
   - low-level: SPI + GPIO (`CS/DC/RST`, передача байт);
   - controller-level: команды ILI9486 (`init`, `set_window`, `set_rotation`, `write_pixels`);
   - app-level: демо/индикация в `main.c`.
3. Сразу поддержать только запись в дисплей (без `RAMRD`, без touch).
4. Частоту SPI стартовать безопасно (например 8 MHz), затем поднять до 20 MHz после подтверждения стабильности.

## 4. Patch Set A: pinmux и низкоуровневый SPI0

## Цель

Стабильный обмен по SPI0 на пинах PA26/PA27/PA28 + GPIO для PC24/PC25.

## Изменения

1. `src/include/init.h`
   - добавить объявления:
     - `void tft_io_init(void);`
     - `void tft_hw_reset(void);`
   - добавить pin macros для `DC`/`RST`.

2. `src/init.c`
   - включить тактирование `PIOA`, `PIOC`, `SPI0` (`PMC_PCER0`).
   - настроить `PA26/PA27/PA28` в Peripheral A (`PIO_PDR`, `PIO_ABSR`).
   - настроить `PC24` и `PC25` как GPIO output, default high.
   - инициализировать `SPI0`:
     - `SPI_CR = SPI_CR_SWRST`, затем `SPI_CR_SPIEN`;
     - `SPI_MR`: `MSTR=1`, `MODFDIS=1`, `PCS` для `NPCS0`;
     - `SPI_CSR[0]`: `NCPHA`/`CPOL` под Mode0, 8-bit transfer, `SCBR` для выбранной частоты.
   - добавить helper-и:
     - `tft_cs_low/high` (если CS управляем GPIO; если аппаратный NPCS0 — описать единый вариант);
     - `tft_dc_cmd/data`;
     - `tft_spi_tx8(uint8_t)`.

## Критерий готовности

- На логическом анализаторе: корректный SCK/MOSI, стабильный `CS`, переключение `DC` для command/data.
- Нет блокировок по `RDRF/TDRE`.

## 5. Patch Set B: модуль `tft_ili9486_wspi` (порт из GxTFT)

## Цель

Перенести командный протокол из `GxCTRL_ILI9486_WSPI.cpp` на CMSIS.

## Новые файлы

1. `src/include/tft_ili9486.h`
   - API:
     - `void tft_init(void);`
     - `void tft_set_rotation(uint8_t r);`
     - `void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);`
     - `void tft_write_color565(uint16_t color, uint32_t count);`
     - `void tft_fill_screen(uint16_t color);`

2. `src/tft_ili9486.c`
   - статические функции:
     - `write_cmd(uint8_t)`
     - `write_data(uint8_t)`
     - `write_cmd_data(cmd, data[], n)`
   - последовательность `tft_init()` повторяет GxTFT (`ILI9486_WSPI`):
     - `0x3A <- 0x55`
     - `0x36 <- 0x48`
     - gamma (`0xE0`, `0xE1`, `0xE2`) с теми же байтами
     - `0x11` + задержка 150 ms
     - `0x29`
   - `set_window` через `CASET(0x2A)`, `PASET(0x2B)`, `RAMWR(0x2C)`.
   - `set_rotation` по тем же комбинациям `MADCTL` что в GxTFT.

## Критерий готовности

- После `tft_init()` дисплей выходит из sleep.
- `tft_fill_screen()` рисует сплошной цвет без артефактов.

## 6. Patch Set C: простая графическая обвязка и smoke-test

## Цель

Получить минимально полезный API для проверки на стенде.

## Изменения

1. `src/include/tft_gfx.h` (или в `tft_ili9486.h`)
   - `tft_draw_pixel`
   - `tft_fill_rect`
   - `tft_fill_screen`

2. `src/tft_gfx.c`
   - реализация через `set_window + write_color565`.

3. `src/main.c`
   - под compile-time флагом (например `ENABLE_TFT_DEMO`) добавить последовательность:
     - `tft_init();`
     - заливка черным;
     - вывод 3-4 цветных экранов (red/green/blue/white) с паузами;
     - тестовые полосы/прямоугольники.

## Критерий готовности

- На экране воспроизводимый паттерн после старта.
- По UART-логам понятно, на каком шаге инициализация.

## 7. Patch Set D: интеграция сборки

## Изменения

`CMakeLists.txt`:

- добавить в `APP_SOURCES`:
  - `src/tft_ili9486.c`
  - `src/tft_gfx.c` (если используется)
- добавить compile definition `ENABLE_TFT`/`ENABLE_TFT_DEMO` для `due_app`.

## Критерий готовности

- `cmake --build build --target due_app` проходит без warning-ошибок.
- Размер бинарника контролируем (оценить прирост).

## 8. Patch Set E: валидация и диагностика

## Функциональные тесты

1. Hardware reset:
   - `RST` low 20 ms -> high 20+ ms.
2. Init:
   - команды уходят в верной последовательности.
3. Rotation:
   - 4 ориентации через `MADCTL`.
4. Fill performance:
   - замер времени полной заливки (320x480).

## Диагностика

- Ввести `uart_dbg_puts()` маркеры:
  - `TFT:IO_INIT`
  - `TFT:RESET_OK`
  - `TFT:INIT_OK`
  - `TFT:FILL_OK`
- На ошибках таймаута SPI печатать `TFT:SPI_ERR`.

## 9. Patch Set F: параметры и ограничения

1. Вынести в конфиг (`src/include/tft_cfg.h`):
   - целевую SPI частоту (start/max);
   - swap RGB/BGR;
   - дефолтную ориентацию.
2. На первом этапе не реализовывать:
   - чтение ID/GRAM;
   - тач-контроллер;
   - DMA.

## 10. Риски и меры

1. Риск: не тот фактический контроллер (клоны 3.5" бывают ILI9486/ILI9488/HX8357).
   - Мера: оставить альтернативный init table (вторая конфигурация) за compile-time флагом.
2. Риск: 20 MHz нестабильно на конкретном шлейфе.
   - Мера: fallback на 8/12 MHz.
3. Риск: конфликт с текущими UART-инициализациями/пинами.
   - Мера: отдельный и явный `tft_io_init()`, без побочных изменений в уже рабочих инициализациях USART.

## 11. Порядок внедрения (рекомендуемый)

1. Patch Set A (pinmux + SPI) + осциллограф/LA проверка.
2. Patch Set B (init + fill screen).
3. Patch Set C (демо в `main.c`).
4. Patch Set D (чистая сборка, флаги).
5. Patch Set E/F (диагностика и параметризация).

## 12. Минимальный целевой DoD

- Проект `due1` собирается с `ENABLE_TFT_DEMO`.
- На железе дисплей стабильно инициализируется и показывает тестовые заливки.
- Используется CMSIS-реализация, поведение init/rotation/window соответствует `GxTFT` профилю `my_3.5_RPi_480x320_DUE.h` + `GxCTRL_ILI9486_WSPI`.
