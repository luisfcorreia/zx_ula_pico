cmake_minimum_required(VERSION 3.13)

#include(pico_sdk_import.cmake)
include(pico-sdk/pico_sdk_init.cmake)

project(zx_pico_ula C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

pico_generate_pio_header(zx_pico_ula ${CMAKE_CURRENT_SOURCE_DIR}/src/video/sync.pio)
pico_generate_pio_header(zx_pico_ula ${CMAKE_CURRENT_SOURCE_DIR}/src/video/pixel.pio)
pico_generate_pio_header(zx_pico_ula ${CMAKE_CURRENT_SOURCE_DIR}/src/dram/dram.pio)
pico_generate_pio_header(zx_pico_ula ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/cpu_clock.pio)

add_executable(zx_pico_ula
    src/main.cpp
    src/clock/clock.cpp
    src/video/video.cpp
    src/video/video_init.cpp
    src/dram/dram.cpp
    src/cpu/cpu.cpp
    src/io/io.cpp
)

target_include_directories(zx_pico_ula PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(zx_pico_ula
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_gpio
    hardware_pwm
)

# No USB/UART stdio — all GPIO needed for the ULA interface
pico_enable_stdio_usb(zx_pico_ula 0)
pico_enable_stdio_uart(zx_pico_ula 0)

pico_add_extra_outputs(zx_pico_ula)
