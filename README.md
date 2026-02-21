Для проекта необходимо чтобы были установлены нижеуказанные продукты по
соответствующим путям

C:\ArmGNU
C:\CMake
C:\Ninja
C:\JLink

и была правильно настроена переменная PATH

Это шаблон проекта.
Каталоги cmsis, startup, svd, cmake, linker - одинаковые для всех проектов
Файл .gitignore - одинаковый для всех проектов

В файле .\tools\jlink_flash_sam3x8e.jlink изменить путь для билдов

 В файлах lanch.json, settings.json и task.json в выражение "${workspaceFolder:empty_project}" необходимо
 будет заменить empty_project на название проекта

 Весь изменяемый код находится в каталогах params и src

---

Новые target-ы сборки:
- `due_bootloader` (`build/due_bootloader.elf`)
- `due_app` (`build/due_app.elf`)

Новые задачи прошивки:
- `Flash Bootloader (J-Link, SAM3X8E, Due2)`
- `Flash APP (J-Link, SAM3X8E, Due2)`
- `Flash Full (BL+APP, J-Link, SAM3X8E, Due2)`

Обновление через UART (`/dev/ttyS4`) с хоста:
```bash
python3 tools/uart_bl_update.py --port /dev/ttyS4 --baud 115200 --firmware build/due_app.bin
```

Обновление через UART без Python-зависимостей (C utility):
```bash
cd tools
make
./uart_bl_update --port /dev/ttyS4 --baud 115200 --firmware ../build/due_app.bin
```

Проверенный рабочий сценарий (T113 + FTDI):
1. Прошить `due_bootloader.elf` и `due_app.elf` (через J-Link task `Flash Full`).
2. На T113 запустить:
```bash
./uart_bl_update --port /dev/ttyS4 --baud 115200 --firmware due_app.bin
```
3. Ожидаемый вывод updater:
- `SYNC: OK`
- `INFO: ...`
- `ERASE: OK`
- `WRITE: ...`
- `VERIFY: OK`
- `RUN: OK`
4. В FTDI терминале (PA8/PA9, 115200 8N1) после `RUN`:
- `APP start`
- `APP alive ms=...` (примерно 1 раз/сек).

Примечание:
- Убедитесь, что на T113 используется актуальный `due_app.bin` (размер должен совпадать с файлом из `build/`), иначе после `RUN` можно видеть старое поведение APP.
 

