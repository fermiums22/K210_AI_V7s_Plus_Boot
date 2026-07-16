# K210 bootloader

> [!IMPORTANT]
> Разработка остановлена 16 июля 2026 года вместе с `K210_AI_V7s_Plus`.
> Новый проект — `ESP32S3_V7s_Plus` на ESP32-S3 с собственной сборкой на
> ESP-IDF/CMake. Отдельный загрузчик K210 больше не нужен: прежняя связка
> K210 + ESP8285 оказалась слишком сложной для надёжных Wi-Fi/OTA и мешала
> перейти к верхнему уровню робота и интеграции с Home Assistant.

Минимальный загрузчик и updater для `K210_AI_V7s_Plus`.

## Структура

- `firmware_v2` — единственная реализация загрузчика.
- `tools/boot_monitor.py` — UART monitor.
- `tools/boot_cmd_smoke.py` — smoke test команд загрузчика.

SDK, HAL и протокол берутся из соседнего репозитория `K210_AI_V7s_Plus`.

## Команды

```bat
build_boot.bat
flash_boot.bat COM8
monitor_boot.bat COM8
```

Сборка использует Ninja и один каталог `build`.
