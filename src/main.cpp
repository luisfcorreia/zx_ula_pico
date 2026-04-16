// =============================================================================
// main.cpp — ZX Spectrum RP2350B ULA bootstrap
//
// Startup sequence:
//   1. 252 MHz PLL
//   2. GPIO directions and safe initial levels
//   3. Initialise video (PIO0 SM0 tick, colour tables)
//   4. Initialise DRAM GPIO (Core 0 drives directly in video_run)
//   5. Initialise CPU clock PIO (PIO0 SM3)
//   6. Start all PIO0 SMs simultaneously (phase aligned)
//   7. Launch Core 1 (I/O, contention)
//   8. Core 0 enters video_run() — never returns
// =============================================================================

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include <initializer_list>

#include "pinmap.h"
#include "colour_lut.h"
#include "clock/clock.h"
#include "video/video.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "io/io.h"

int main(void) {
    // 1. 252 MHz system clock
    clock_init_252mhz();

    // 2. GPIO directions and safe initial levels

    // DRAM — set up by dram_init() below
    // Data bus — bidirectional, pull-up (high-Z)
    for (int i = PIN_D_BASE; i < PIN_D_BASE + PIN_D_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }
    // Z80 control inputs
    for (int p : {PIN_WR_N, PIN_RD_N, PIN_MREQ_N, PIN_IO_ULA_N, PIN_A14, PIN_A15}) {
        gpio_init(p); gpio_set_dir(p, GPIO_IN);
    }
    // Z80 control outputs
    gpio_init(PIN_INT_N);    gpio_set_dir(PIN_INT_N,    GPIO_OUT); gpio_put(PIN_INT_N,    1);
    gpio_init(PIN_CLOCK);    gpio_set_dir(PIN_CLOCK,    GPIO_OUT); gpio_put(PIN_CLOCK,    0);
    gpio_init(PIN_ROM_CS_N); gpio_set_dir(PIN_ROM_CS_N, GPIO_OUT); gpio_put(PIN_ROM_CS_N, 1);
    // Sound / keyboard
    gpio_init(PIN_SOUND); gpio_set_dir(PIN_SOUND, GPIO_IN);
    for (int i = PIN_T_BASE; i < PIN_T_BASE + PIN_T_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }
    // Video output (high bank GP33-GP44)
    for (int i = PIN_YN_BASE;   i < PIN_YN_BASE   + PIN_YN_COUNT;  i++) { gpio_init(i); gpio_set_dir(i, GPIO_OUT); }
    for (int i = PIN_UO_BASE;   i < PIN_UO_BASE   + PIN_UO_COUNT;  i++) { gpio_init(i); gpio_set_dir(i, GPIO_OUT); }
    for (int i = PIN_VO_BASE;   i < PIN_VO_BASE   + PIN_VO_COUNT;  i++) { gpio_init(i); gpio_set_dir(i, GPIO_OUT); }
    for (int p : {PIN_R, PIN_G, PIN_B, PIN_BRIGHT}) { gpio_init(p); gpio_set_dir(p, GPIO_OUT); gpio_put(p, 0); }
    gpio_init(PIN_HSYNC_N); gpio_set_dir(PIN_HSYNC_N, GPIO_OUT); gpio_put(PIN_HSYNC_N, 1);
    gpio_init(PIN_VSYNC_N); gpio_set_dir(PIN_VSYNC_N, GPIO_OUT); gpio_put(PIN_VSYNC_N, 1);

    // 3. Video init (PIO0 SM0 tick generator + colour tables)
    video_init();

    // 4. DRAM GPIO init (GP0-GP9 as SIO outputs)
    dram_init();

    // 5. CPU clock (PIO0 SM3, free-running 3.5 MHz)
    cpu_init();

    // 6. Start PIO0 SM0 (tick) and SM3 (cpu_clock) simultaneously
    pio_enable_sm_mask_in_sync(pio0, (1u << 0) | (1u << 3));

    // 7. Launch Core 1 (port 0xFE, contention)
    multicore_launch_core1(io_core1_entry);

    // 8. Core 0 pixel loop — never returns
    video_run();
}
