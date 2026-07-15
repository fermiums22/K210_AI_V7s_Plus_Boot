# K210 bootloader

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
