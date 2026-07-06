#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FREERTOS = r'''# thin boot freertos subset: no network wrappers, no lwIP, no os_entry autostart
set(LIB_SRC
    ${CMAKE_CURRENT_LIST_DIR}/core_sync.c
    ${CMAKE_CURRENT_LIST_DIR}/croutine.c
    ${CMAKE_CURRENT_LIST_DIR}/event_groups.c
    ${CMAKE_CURRENT_LIST_DIR}/list.c
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

DRIVERS = r'''# thin boot drivers subset: only SD card driver source is kept for later update-mode work.
set(LIB_SRC
    ${CMAKE_CURRENT_LIST_DIR}/src/storage/sdcard.cpp
)
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
add_library(drivers STATIC ${LIB_SRC})
set_target_properties(drivers PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(drivers freertos)
target_include_directories(drivers PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
'''

BSP = r'''# thin boot bsp subset: no SDK os_entry device auto-install before boot decision
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

REGISTRY = r'''/* Thin boot registry: only drivers linked into boot. */
#include <kernel/driver.hpp>

using namespace sys;

extern driver &g_uart_driver_uart0;
extern driver &g_uart_driver_uart1;
extern driver &g_uart_driver_uart2;
extern driver &g_gpio_driver_gpio0;
extern driver &g_gpiohs_driver_gpio0;
extern driver &g_spi_driver_spi0;
extern driver &g_spi_driver_spi1;
extern driver &g_spi_driver_spi3;
extern driver &g_timer_driver_timer0;
extern driver &g_timer_driver_timer1;
extern driver &g_timer_driver_timer2;
extern driver &g_timer_driver_timer3;
extern driver &g_timer_driver_timer4;
extern driver &g_timer_driver_timer5;
extern driver &g_timer_driver_timer6;
extern driver &g_timer_driver_timer7;
extern driver &g_timer_driver_timer8;
extern driver &g_timer_driver_timer9;
extern driver &g_timer_driver_timer10;
extern driver &g_timer_driver_timer11;
extern driver &g_wdt_driver_wdt0;
extern driver &g_wdt_driver_wdt1;

extern driver &g_pic_driver_plic0;
extern driver &g_dmac_driver_dmac0;
extern driver &g_dma_driver_dma0;
extern driver &g_dma_driver_dma1;
extern driver &g_dma_driver_dma2;
extern driver &g_dma_driver_dma3;
extern driver &g_dma_driver_dma4;
extern driver &g_dma_driver_dma5;

driver_registry_t sys::g_system_drivers[] = {
    { "/dev/uart1", { std::in_place, &g_uart_driver_uart0 } },
    { "/dev/uart2", { std::in_place, &g_uart_driver_uart1 } },
    { "/dev/uart3", { std::in_place, &g_uart_driver_uart2 } },
    { "/dev/gpio0", { std::in_place, &g_gpiohs_driver_gpio0 } },
    { "/dev/gpio1", { std::in_place, &g_gpio_driver_gpio0 } },
    { "/dev/spi0", { std::in_place, &g_spi_driver_spi0 } },
    { "/dev/spi1", { std::in_place, &g_spi_driver_spi1 } },
    { "/dev/spi3", { std::in_place, &g_spi_driver_spi3 } },
    { "/dev/timer0", { std::in_place, &g_timer_driver_timer0 } },
    { "/dev/timer1", { std::in_place, &g_timer_driver_timer1 } },
    { "/dev/timer2", { std::in_place, &g_timer_driver_timer2 } },
    { "/dev/timer3", { std::in_place, &g_timer_driver_timer3 } },
    { "/dev/timer4", { std::in_place, &g_timer_driver_timer4 } },
    { "/dev/timer5", { std::in_place, &g_timer_driver_timer5 } },
    { "/dev/timer6", { std::in_place, &g_timer_driver_timer6 } },
    { "/dev/timer7", { std::in_place, &g_timer_driver_timer7 } },
    { "/dev/timer8", { std::in_place, &g_timer_driver_timer8 } },
    { "/dev/timer9", { std::in_place, &g_timer_driver_timer9 } },
    { "/dev/timer10", { std::in_place, &g_timer_driver_timer10 } },
    { "/dev/timer11", { std::in_place, &g_timer_driver_timer11 } },
    { "/dev/wdt0", { std::in_place, &g_wdt_driver_wdt0 } },
    { "/dev/wdt1", { std::in_place, &g_wdt_driver_wdt1 } },
    {}
};

driver_registry_t sys::g_hal_drivers[] = {
    { "/dev/pic0", { std::in_place, &g_pic_driver_plic0 } },
    { "/dev/dmac0", { std::in_place, &g_dmac_driver_dmac0 } },
    {}
};

driver_registry_t sys::g_dma_drivers[] = {
    { "/dev/dmac0/0", { std::in_place, &g_dma_driver_dma0 } },
    { "/dev/dmac0/1", { std::in_place, &g_dma_driver_dma1 } },
    { "/dev/dmac0/2", { std::in_place, &g_dma_driver_dma2 } },
    { "/dev/dmac0/3", { std::in_place, &g_dma_driver_dma3 } },
    { "/dev/dmac0/4", { std::in_place, &g_dma_driver_dma4 } },
    { "/dev/dmac0/5", { std::in_place, &g_dma_driver_dma5 } },
    {}
};
'''

ENTRY_USER = r'''/* Thin boot entry: call main() directly, do not auto-start FreeRTOS/os_entry.
 * This keeps the boot decision before UART/SD/FreeRTOS/device-layer runtime.
 */
#include <fpioa.h>
#include <stdlib.h>
#include <string.h>
#include <sysctl.h>
#include <uarths.h>
#include "pin_cfg_priv.h"

#define PLL1_OUTPUT_FREQ 160000000UL
#define PLL2_OUTPUT_FREQ 45158400UL

extern unsigned char __bss_start[];
extern unsigned char __bss_end[];
extern int main(void);
extern void __libc_init_array(void);
extern void __libc_fini_array(void);

static void setup_clocks(void)
{
    sysctl_pll_set_freq(SYSCTL_PLL1, PLL1_OUTPUT_FREQ);
    sysctl_pll_set_freq(SYSCTL_PLL2, PLL2_OUTPUT_FREQ);
}

static void init_bss(void)
{
    memset(__bss_start, 0, __bss_end - __bss_start);
}

void _init_bsp(int core_id, int number_of_cores)
{
    (void)number_of_cores;

    if (core_id != 0) {
        for (;;)
            __asm__ volatile("wfi");
    }

    init_bss();
    atexit(__libc_fini_array);
    __libc_init_array();
    fpioa_init();
    bsp_pin_setup();
    setup_clocks();
    uarths_init();

    (void)main();

    for (;;)
        __asm__ volatile("wfi");
}
'''


def patch_boot_freertos_config() -> None:
    p = ROOT / "lib" / "freertos" / "conf" / "FreeRTOSConfig.h"
    s = p.read_text(encoding="utf-8")
    s = s.replace("#define configTOTAL_HEAP_SIZE\t\t\t\t( ( size_t ) ( 1024 * 1024 ) )",
                  "#define configTOTAL_HEAP_SIZE\t\t\t\t( ( size_t ) ( 256 * 1024 ) )")
    s = s.replace("#define configMAIN_TASK_STACK_SIZE\t\t\t\t(4096 * 2)",
                  "#define configMAIN_TASK_STACK_SIZE\t\t\t\t(2048)")
    p.write_text(s, encoding="utf-8", newline="\n")


def patch_boot_linker_script() -> None:
    p = ROOT / "lds" / "kendryte.ld"
    s = p.read_text(encoding="utf-8")
    s = s.replace("ram (wxa!ri) : ORIGIN = 0x80000000, LENGTH = (6 * 1024 * 1024)",
                  "ram (wxa!ri) : ORIGIN = 0x80000000, LENGTH = (1 * 1024 * 1024)")
    s = s.replace("ram_nocache (wxa!ri) : ORIGIN = 0x40000000, LENGTH = (6 * 1024 * 1024)",
                  "ram_nocache (wxa!ri) : ORIGIN = 0x40000000, LENGTH = (1 * 1024 * 1024)")
    s = s.replace("PROVIDE( _heap_end = _ram_end );",
                  "PROVIDE( _heap_end = 0x800E0000 );\n"
                  "  PROVIDE( _boot_stack_core1 = 0x800F7000 );\n"
                  "  PROVIDE( _boot_stack_core0 = 0x800FF000 );\n"
                  "  ASSERT(_end < 0x800E0000, \"boot image/data overlaps heap/stack reserve below APP_LOAD_ADDR\");")
    p.write_text(s, encoding="utf-8", newline="\n")


def patch_boot_crt_stack() -> None:
    p = ROOT / "lib" / "bsp" / "crt.S"
    s = p.read_text(encoding="utf-8")
    old = """  la  tp, _end + 63\n  and tp, tp, -64\n  csrr a0, mhartid\n\n  sll a2, a0, STKSHIFT\n  add tp, tp, a2\n  add sp, a0, 1\n  sll sp, sp, STKSHIFT\n  add sp, sp, tp\n"""
    new = """  la  tp, _end + 63\n  and tp, tp, -64\n  csrr a0, mhartid\n\n  # Bootloader owns only RAM below APP_LOAD_ADDR (0x80100000).\n  # Keep startup/IRQ stack fixed in that boot-only window so loading the app\n  # to 0x80100000 never overwrites boot stack or TLS.\n  li sp, 0x800ff000\n  sll a2, a0, STKSHIFT\n  sub sp, sp, a2\n"""
    if old not in s:
        raise SystemExit("ERROR: crt.S stack block not found")
    p.write_text(s.replace(old, new), encoding="utf-8", newline="\n")


(ROOT / "lib" / "freertos" / "CMakeLists.txt").write_text(FREERTOS, encoding="utf-8", newline="\n")
(ROOT / "lib" / "drivers" / "CMakeLists.txt").write_text(DRIVERS, encoding="utf-8", newline="\n")
(ROOT / "lib" / "bsp" / "CMakeLists.txt").write_text(BSP, encoding="utf-8", newline="\n")
(ROOT / "lib" / "bsp" / "device" / "registry.cpp").write_text(REGISTRY, encoding="utf-8", newline="\n")
(ROOT / "lib" / "bsp" / "entry_user.c").write_text(ENTRY_USER, encoding="utf-8", newline="\n")
patch_boot_freertos_config()
patch_boot_linker_script()
patch_boot_crt_stack()
print("THIN_BOOT_CMAKE_OK no_lwip=1 no_esp_flasher=1 no_pwm_dvp_i2s=1 asm_language_c=1 thin_registry=1 direct_entry=1 no_os_entry_autostart=1 boot_ram_below_app=1 fixed_boot_stack=1 boot_heap_256k=1")
