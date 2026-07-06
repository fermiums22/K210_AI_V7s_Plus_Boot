#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FREERTOS = r'''# thin boot freertos subset: no network wrappers, no lwIP
set(LIB_SRC
    ${CMAKE_CURRENT_LIST_DIR}/core_sync.c
    ${CMAKE_CURRENT_LIST_DIR}/croutine.c
    ${CMAKE_CURRENT_LIST_DIR}/event_groups.c
    ${CMAKE_CURRENT_LIST_DIR}/list.c
    ${CMAKE_CURRENT_LIST_DIR}/os_entry.c
    ${CMAKE_CURRENT_LIST_DIR}/pthread.c
    ${CMAKE_CURRENT_LIST_DIR}/queue.c
    ${CMAKE_CURRENT_LIST_DIR}/stream_buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/tasks.c
    ${CMAKE_CURRENT_LIST_DIR}/timers.c
    ${CMAKE_CURRENT_LIST_DIR}/portable/heap_4.c
    ${CMAKE_CURRENT_LIST_DIR}/portable/port.c
    ${CMAKE_CURRENT_LIST_DIR}/portable/portasm.S
    ${CMAKE_CURRENT_LIST_DIR}/kernel/devices.cpp
    ${CMAKE_CURRENT_LIST_DIR}/kernel/driver_impl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/kernel/storage/filesystem.cpp
)
set(LIB_INC ${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/conf ${CMAKE_CURRENT_LIST_DIR}/portable)
set(ASSEMBLY_FILES ${CMAKE_CURRENT_LIST_DIR}/portable/portasm.S)
set_property(SOURCE ${ASSEMBLY_FILES} PROPERTY LANGUAGE C)
set_source_files_properties(${ASSEMBLY_FILES} PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp -D __riscv64")
add_library(freertos STATIC ${LIB_SRC})
set_target_properties(freertos PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(freertos PRIVATE hal PRIVATE fatfs)
target_include_directories(freertos PUBLIC ${LIB_INC})
'''

DRIVERS = r'''# thin boot drivers subset: only SD card driver
set(LIB_SRC
    ${CMAKE_CURRENT_LIST_DIR}/src/storage/sdcard.cpp
)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
add_library(drivers STATIC ${LIB_SRC})
set_target_properties(drivers PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(drivers freertos)
target_include_directories(drivers PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
'''

BSP = r'''# thin boot bsp subset: boot, UART log, SD/SPI/FPIOA/GPIOHS/timer/wdt basics
set(LIB_SRC
    ${CMAKE_CURRENT_LIST_DIR}/config/pin_cfg.c
    ${CMAKE_CURRENT_LIST_DIR}/device/dmac.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/gpio.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/gpiohs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/plic.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/registry.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/spi.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/timer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/uart.cpp
    ${CMAKE_CURRENT_LIST_DIR}/device/wdt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dump.c
    ${CMAKE_CURRENT_LIST_DIR}/entry_user.c
    ${CMAKE_CURRENT_LIST_DIR}/except.c
    ${CMAKE_CURRENT_LIST_DIR}/interrupt.c
    ${CMAKE_CURRENT_LIST_DIR}/printf.c
    ${CMAKE_CURRENT_LIST_DIR}/sleep.c
    ${CMAKE_CURRENT_LIST_DIR}/syscalls.c
    ${CMAKE_CURRENT_LIST_DIR}/syscalls/syscalls.cpp
    ${CMAKE_CURRENT_LIST_DIR}/crt.S
)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
set(ASSEMBLY_FILES ${CMAKE_CURRENT_LIST_DIR}/crt.S)
set_property(SOURCE ${ASSEMBLY_FILES} PROPERTY LANGUAGE C)
set_source_files_properties(${ASSEMBLY_FILES} PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp -D __riscv64")
add_library(bsp STATIC ${LIB_SRC})
set_target_properties(bsp PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(bsp PRIVATE hal freertos)
target_include_directories(bsp PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
'''

(ROOT / "lib" / "freertos" / "CMakeLists.txt").write_text(FREERTOS, encoding="utf-8", newline="\n")
(ROOT / "lib" / "drivers" / "CMakeLists.txt").write_text(DRIVERS, encoding="utf-8", newline="\n")
(ROOT / "lib" / "bsp" / "CMakeLists.txt").write_text(BSP, encoding="utf-8", newline="\n")
print("THIN_BOOT_CMAKE_OK no_lwip=1 no_esp_flasher=1 no_pwm_dvp_i2s=1 asm_language_c=1")
