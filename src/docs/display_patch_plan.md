# Patch Plan: 3.5inch RPi Display on Arduino Due (CMSIS, project `due1`)

Статус на текущий момент:
- Рабочий вариант на стенде подтвержден: заливка сверху-вниз и вывод `10.5` по центру экрана выполняются корректно.
- Для стабильности активирован Arduino-совместимый путь `src/tft_arduino_demo.c` (по референсу `ArduinoDisplay.zip`).
- Экспериментальные ветки TFT (`tft_ili9486.c`/`tft_gfx.c`) сохранены в репозитории как референс, но исключены из активной сборки `due_app`.

## 0. Stable Display Profile (зафиксировано)

Цель: повторить рабочее поведение из `ArduinoDisplay.zip` без регрессов геометрии/цвета.

Принятый рабочий профиль:
- Дисплей: 3.5" RPi display (маркировка контроллера на модуле: `ILI9486`).
- SPI0: 16-bit frames, `SPI_CSR_BITS_16_BIT`, Mode0 (`NCPHA=1`), `SCBR=32`.
- CS управление: GPIO одновременно на `PA28` и `PC29`.
- DC: `PC24`, RST: `PC25`.
- `MADCTL = 0x28`, `COLMOD = 0x55`.
- Demo-сценарий:
  1. поэкранная заливка сверху-вниз;
  2. очистка в черный;
  3. вывод `10.5` по центру.

Файлы рабочего пути:
- `src/tft_arduino_demo.c`
- `src/include/tft_arduino_demo.h`
- `src/main.c` (`ENABLE_TFT_DEMO` -> `tft_arduino_demo_run()`).

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

### Цель

Стабильный обмен по SPI0 на пинах PA26/PA27/PA28 + GPIO для PC24/PC25.

### Изменения

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

### Реализационный чеклист

- [x] Добавлены объявления `tft_io_init()` и `tft_hw_reset()` в `src/include/init.h`.
- [x] Добавлены pin macros для `DC` и `RST` в `src/include/init.h`.
- [x] В `src/init.c` включено тактирование `PIOA`, `PIOC`, `SPI0`.
- [x] В `src/init.c` настроены `PA26/PA27/PA28` как Peripheral A.
- [x] В `src/init.c` настроены `PC24/PC25` как GPIO output со стартовым `high`.
- [x] В `src/init.c` реализована инициализация `SPI0` (Mode0, 8-bit, SCBR).
- [x] Реализованы helper-функции `tft_cs_*`, `tft_dc_*`, `tft_spi_tx8()`.

### Definition of Done и проверка

- На логическом анализаторе: корректный SCK/MOSI, стабильный `CS`, переключение `DC` для command/data.
- Нет блокировок по `RDRF/TDRE`.
- UART-лог содержит маркер старта инициализации TFT IO (`TFT:IO_INIT`).

Команды проверки:

```bash
cmake --build build --target due_app
```

```bash
# hardware check: LA/oscilloscope на SCK/MOSI/CS/DC
```

## 5. Patch Set B: модуль `tft_ili9486_wspi` (порт из GxTFT)

### Цель

Перенести командный протокол из `GxCTRL_ILI9486_WSPI.cpp` на CMSIS.

### Новые файлы

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

### Реализационный чеклист

- [x] Добавлен заголовок `src/include/tft_ili9486.h` с публичным API.
- [x] Добавлен `src/tft_ili9486.c` с `write_cmd`, `write_data`, `write_cmd_data`.
- [x] Последовательность `tft_init()` соответствует `ILI9486_WSPI` из GxTFT.
- [x] Реализованы `tft_set_window()` (`0x2A/0x2B/0x2C`).
- [x] Реализованы режимы `tft_set_rotation()`.

### Definition of Done и проверка

- После `tft_init()` дисплей выходит из sleep.
- `tft_fill_screen()` рисует сплошной цвет без артефактов.
- UART-лог содержит маркер завершения init (`TFT:INIT_OK`).

Команды проверки:

```bash
cmake --build build --target due_app
```

## 6. Patch Set C: простая графическая обвязка и smoke-test

### Цель

Получить минимально полезный API для проверки на стенде.

### Изменения

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

### Реализационный чеклист

- [x] Добавлен/обновлен API базовой графики (`tft_draw_pixel`, `tft_fill_rect`, `tft_fill_screen`).
- [x] Реализован `src/tft_gfx.c` через `set_window + write_color565`.
- [x] В `src/main.c` добавлен demo-путь под `ENABLE_TFT_DEMO`.
- [x] Для demo добавлены шаги с понятными UART-маркерами.

### Definition of Done и проверка

- На экране воспроизводимый паттерн после старта.
- По UART-логам понятно, на каком шаге инициализация.
- После полной заливки есть маркер `TFT:FILL_OK`.

## 7. Patch Set D: интеграция сборки

### Изменения

`CMakeLists.txt`:

- добавить в `APP_SOURCES`:
  - `src/tft_ili9486.c`
  - `src/tft_gfx.c` (если используется)
- добавить compile definition `ENABLE_TFT`/`ENABLE_TFT_DEMO` для `due_app`.

### Реализационный чеклист

- [x] `src/tft_ili9486.c` добавлен в `APP_SOURCES`.
- [x] `src/tft_gfx.c` добавлен в `APP_SOURCES` (если вынесен отдельно).
- [x] Добавлены compile definition `ENABLE_TFT`/`ENABLE_TFT_DEMO`.
- [x] Сборка `due_app` проходит без регрессий в bootloader target.

### Definition of Done и проверка

- `cmake --build build --target due_app` проходит без warning-ошибок.
- Размер бинарника контролируем (оценить прирост).

Команды проверки:

```bash
cmake -S . -B build
cmake --build build --target due_app
cmake --build build --target due_bootloader
```

## 8. Patch Set E: валидация и диагностика

### Функциональные тесты

1. Hardware reset:
   - `RST` low 20 ms -> high 20+ ms.
2. Init:
   - команды уходят в верной последовательности.
3. Rotation:
   - 4 ориентации через `MADCTL`.
4. Fill performance:
   - замер времени полной заливки (320x480).

### Диагностика

- Ввести `uart_dbg_puts()` маркеры:
  - `TFT:IO_INIT`
  - `TFT:RESET_OK`
  - `TFT:INIT_OK`
  - `TFT:FILL_OK`
- На ошибках таймаута SPI печатать `TFT:SPI_ERR`.

### Реализационный чеклист

- [x] Добавлены диагностические маркеры в ключевые этапы.
- [x] Добавлен error path с маркером `TFT:SPI_ERR`.
- [x] Подготовлен минимальный протокол ручной проверки (что проверяем в логах/на экране).

### Definition of Done и проверка

- По UART-логу можно однозначно локализовать стадию сбоя.
- Rotation меняет ориентацию корректно (без смещения окна).
- Замер времени полной заливки зафиксирован и сохранен в заметках интеграции.

## 9. Patch Set F: параметры и ограничения

1. Вынести в конфиг (`src/include/tft_cfg.h`):
   - целевую SPI частоту (start/max);
   - swap RGB/BGR;
   - дефолтную ориентацию.
2. На первом этапе не реализовывать:
   - чтение ID/GRAM;
   - тач-контроллер;
   - DMA.

### Реализационный чеклист

- [x] Добавлен `src/include/tft_cfg.h` с SPI частотами, rotation, RGB/BGR.
- [x] Параметры используются в `tft_ili9486.c` (а не захардкожены в нескольких местах).
- [x] Ограничения явно зафиксированы в документе и комментариях к API.

### Definition of Done и проверка

- Смена ключевых параметров через `tft_cfg.h` не требует правок в логике драйвера.
- Поведение по умолчанию соответствует целевой плате и разводке.

## 10. Риски и меры

| Риск | Как диагностировать | Fallback / мера |
|---|---|---|
| Фактический контроллер не `ILI9486` (клоны `ILI9488`/`HX8357`) | Есть SPI-обмен, но экран белый/инверсия/искаженные цвета после `INIT_OK` | Альтернативный init table под compile-time флагом |
| 20 MHz SPI нестабилен на конкретном шлейфе | Артефакты при заливке, редкие битые строки, нестабильный `FILL_OK` | Понизить SPI до 12/8 MHz через `tft_cfg.h` |
| Конфликт с текущими UART-инициализациями/пинами | Регресс логов UART после интеграции TFT | Изолировать `tft_io_init()`, не трогать существующий путь USART init |
| Некорректный `MADCTL` для выбранной ориентации | Смещение осей, неверный mirroring, перепутаны X/Y | Проверить 4 режима rotation, поправить таблицу значений `MADCTL` |

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

## 13. Рабочий чеклист внедрения (с отметкой статуса)

- [x] A: pinmux + SPI low-level готов и подтвержден на стенде.
- [x] B: базовый ILI9486/совместимый путь инициализации стабилен на железе.
- [x] C: smoke-demo в `main.c` воспроизводимо отрабатывает (заливка + `10.5`).
- [x] D: интеграция в `CMakeLists.txt` завершена, `due_app` и `due_bootloader` собираются.
- [x] E: UART-диагностика покрывает ключевые этапы и ошибки SPI.
- [x] F: параметры и рабочий профиль зафиксированы.

Осталось закрыть на стенде:
- Нет блокирующих задач для текущего DoD. Дальше — опциональная оптимизация/рефакторинг.

## 14. Hardware Runbook для закрытия A/C

### Предусловия

- Прошивка собрана и зашита из `due_app` с включенным demo:
  - `cmake -S . -B build -DDUE_ENABLE_TFT_DEMO=ON`
  - `cmake --build build --target due_app`
- Подключение TFT соответствует распиновке в разделе 1.
- Подключен debug UART (PA8/PA9, `115200 8N1`) для чтения логов.
- Подключен LA/осциллограф на линии `SCK`, `MOSI`, `CS`, `DC`.

### Шаги проверки Patch Set A (сигналы)

1. Подать питание/сбросить плату и начать захват сигналов.
2. Убедиться, что в UART идут маркеры:
   - `TFT:IO_INIT`
   - `TFT:RESET_OK`
3. На LA/осциллографе подтвердить:
   - `SCK` активен при передаче, частота около стартовой (`~8 MHz`);
   - `MOSI` содержит данные в момент тактов;
   - `CS` активен во время команд/данных и отпускается после транзакций;
   - `DC` переключается: low для команд, high для данных.

Критерий PASS для A:
- Все 4 линии ведут себя ожидаемо, нет “залипания” `CS`/`DC`.
- Нет маркера `TFT:SPI_ERR` в UART.

### Шаги проверки Patch Set C (визуальный smoke-demo)

1. После старта наблюдать последовательность экранов:
   - черный -> красный -> зеленый -> синий -> белый;
   - затем черный фон с набором цветных прямоугольников.
2. Проверить UART:
   - есть `TFT:INIT_OK`;
   - есть `TFT:FILL_OK`;
   - отсутствует `TFT:SPI_ERR`.

Критерий PASS для C:
- Цветовые заливки и прямоугольники воспроизводятся стабильно после каждого перезапуска.

### Если FAIL

- Есть `TFT:SPI_ERR`: снизить `TFT_CFG_SPI_START_HZ` в `src/include/tft_cfg.h` до `12000000` или `8000000`.
- Есть искажение цветов: переключить `TFT_CFG_COLOR_ORDER_BGR` (`1` <-> `0`).
- Неверная ориентация: изменить `TFT_CFG_DEFAULT_ROTATION` (`0..3`).
