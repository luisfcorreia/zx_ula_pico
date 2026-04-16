cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD weact_studio_rp2350b_core)

include(pico-sdk/pico_sdk_init.cmake)

project(zx_pico_ula C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(zx_ula_pico
    src/main.cpp
    src/clock/clock.cpp
    src/video/video.cpp
    src/video/video_init.cpp
    src/dram/dram.cpp
    src/cpu/cpu.cpp
    src/io/io.cpp
)

pico_generate_pio_header(zx_ula_pico ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/sync.pio OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
pico_generate_pio_header(zx_ula_pico ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/cpu_clock.pio OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
pico_generate_pio_header(zx_ula_pico ${CMAKE_CURRENT_SOURCE_DIR}/src/dram/dram.pio OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

target_include_directories(zx_ula_pico PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(zx_ula_pico
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_clocks
    hardware_gpio
)

# Custom 252 MHz PLL: VCO=1512 MHz, POSTDIV1=6, POSTDIV2=1
target_compile_definitions(zx_ula_pico PRIVATE
    PLL_SYS_VCO_FREQ_HZ=1512000000
    PLL_SYS_POSTDIV1=6
    PLL_SYS_POSTDIV2=1
)

# Disable USB and UART
pico_enable_stdio_usb(zx_ula_pico 0)
pico_enable_stdio_uart(zx_ula_pico 0)

pico_add_extra_outputs(zx_ula_pico)
