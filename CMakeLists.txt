cmake_minimum_required(VERSION 3.0)
cmake_policy(VERSION 3.0)

set(TOOLCHAIN "C:/K210/toolchain/kendryte-toolchain/bin" CACHE PATH "RISC-V gcc bin dir")
set(SDK_ROOT ${CMAKE_SOURCE_DIR})
include("${CMAKE_SOURCE_DIR}/cmake/common.cmake")

project(k210_boot C CXX ASM)

include_directories(
    "${CMAKE_SOURCE_DIR}/lib/arch/include"
    "${CMAKE_SOURCE_DIR}/lib/utils/include"
    "${CMAKE_SOURCE_DIR}/lib"
    "${CMAKE_SOURCE_DIR}/third_party"
)

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/fatfs" "${CMAKE_BINARY_DIR}/fatfs")

add_subdirectory("${CMAKE_SOURCE_DIR}/lib/hal"      "${CMAKE_BINARY_DIR}/hal")
add_subdirectory("${CMAKE_SOURCE_DIR}/lib/freertos" "${CMAKE_BINARY_DIR}/freertos")
add_subdirectory("${CMAKE_SOURCE_DIR}/lib/bsp"      "${CMAKE_BINARY_DIR}/bsp")
add_subdirectory("${CMAKE_SOURCE_DIR}/lib/drivers"  "${CMAKE_BINARY_DIR}/drivers")

file(GLOB APP_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.S"
)

add_executable(k210_boot ${APP_SOURCES})

if(BOOT_DIAG_BEFORE_JUMP)
    target_compile_definitions(k210_boot PRIVATE BOOT_DIAG_BEFORE_JUMP=1)
endif()

if(BOOT_ENABLE_SLOT_PROBE)
    target_compile_definitions(k210_boot PRIVATE BOOT_ENABLE_SLOT_PROBE=1)
endif()

target_include_directories(k210_boot PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/arch/include"
    "${CMAKE_SOURCE_DIR}/lib/bsp/include"
    "${CMAKE_SOURCE_DIR}/lib/hal/include"
    "${CMAKE_SOURCE_DIR}/lib/drivers/include"
    "${CMAKE_SOURCE_DIR}/lib/utils/include"
    "${CMAKE_SOURCE_DIR}/lib/freertos/include"
    "${CMAKE_SOURCE_DIR}/lib/freertos/conf"
    "${CMAKE_SOURCE_DIR}/lib/freertos/portable"
    "${CMAKE_SOURCE_DIR}/third_party/fatfs/source"
)

target_link_libraries(k210_boot
    -Wl,--start-group
    m freertos atomic bsp c stdc++ drivers hal fatfs
    -Wl,--end-group
)

add_custom_command(TARGET k210_boot POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary k210_boot k210_boot.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "-> k210_boot.bin ready for kflash"
)
