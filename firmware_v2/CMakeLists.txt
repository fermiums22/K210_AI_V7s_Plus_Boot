cmake_minimum_required(VERSION 3.10)
cmake_policy(VERSION 3.10)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(DEFAULT_APP_ROOT "${ROOT}/../K210_AI_V7s_Plus" ABSOLUTE)
set(APP_V2_ROOT "${DEFAULT_APP_ROOT}" CACHE PATH "K210 v2 app repository")
set(TOOLCHAIN "C:/K210/toolchain/kendryte-toolchain/bin" CACHE PATH "RISC-V toolchain bin")
set(SDK_ROOT "${ROOT}")
include("${ROOT}/cmake/common.cmake")
project(k210_boot_v2 C CXX ASM)

include_directories(
  "${ROOT}/lib/arch/include" "${ROOT}/lib/utils/include" "${ROOT}/lib"
  "${ROOT}/third_party"
  "${APP_V2_ROOT}/protocol" "${APP_V2_ROOT}/firmware_v2/common")
add_subdirectory("${ROOT}/lib/hal" "${CMAKE_BINARY_DIR}/hal")

add_executable(k210_boot_v2
  startup.S main.c jump.S newlib_locks.c
  "${APP_V2_ROOT}/protocol/kupdate_v2.c"
  "${APP_V2_ROOT}/firmware_v2/common/sha256_stream.c"
  "${APP_V2_ROOT}/firmware_v2/common/k210_flash.c"
  "${APP_V2_ROOT}/firmware_v2/common/kboot_meta_v2.c")
set_source_files_properties(startup.S PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp -D__riscv64")
target_include_directories(k210_boot_v2 PRIVATE
  "${ROOT}/lib/arch/include" "${ROOT}/lib/hal/include" "${ROOT}/lib/utils/include")
target_link_libraries(k210_boot_v2
  -Wl,--start-group m atomic c hal -Wl,--end-group)
add_custom_command(TARGET k210_boot_v2 POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary k210_boot_v2 k210_boot_v2.bin
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
