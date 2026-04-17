// =============================================================================
// main.cpp — ZX Spectrum RP2350B ULA bootstrap
// =============================================================================

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include <initializer_list>

#include "pinmap.h"
#include "clock/clock.h"
#include "video/video.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "io/io.h"

int main(void) {
    // 1. 252 MHz
    clock_init_252mhz();

    // 2. GPIO setup
    // Data bus GP10-GP17: inputs, pull-up
    for (int i = PIN_D_BASE; i < PIN_D_BASE + PIN_D_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }
    // Z80 inputs
    for (int p : {PIN_WR_N, PIN_RD_N, PIN_MREQ_N, PIN_IO_ULA_N, PIN_A14, PIN_A15}) {
        gpio_init(p); gpio_set_dir(p, GPIO_IN);
    }
    // Z80 outputs
    gpio_init(PIN_INT_N);    gpio_set_dir(PIN_INT_N,    GPIO_OUT); gpio_put(PIN_INT_N,    1);
    gpio_init(PIN_CLOCK);    gpio_set_dir(PIN_CLOCK,    GPIO_OUT); gpio_put(PIN_CLOCK,    0);
    gpio_init(PIN_ROM_CS_N); gpio_set_dir(PIN_ROM_CS_N, GPIO_OUT); gpio_put(PIN_ROM_CS_N, 1);

    // Sound / keyboard
    gpio_init(PIN_SOUND); gpio_set_dir(PIN_SOUND, GPIO_IN);
    for (int i = PIN_T_BASE; i < PIN_T_BASE + PIN_T_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }

    // Video output GP33-GP44: YN[3:0] + UO[3:0] + VO[3:0]
    for (int i = PIN_YN_BASE; i < PIN_YN_BASE + PIN_YN_COUNT; i++) { gpio_init(i); gpio_set_dir(i, GPIO_OUT); }
    for (int i = PIN_UO_BASE; i < PIN_UO_BASE + PIN_UO_COUNT; i++) { gpio_init(i); gpio_set_dir(i, GPIO_OUT); }
    for (int i = PIN_VO_BASE; i < PIN_VO_BASE + PIN_VO_COUNT; i++) { gpio_init(i); gpio_set_dir(i, GPIO_OUT); }
    // RGBi bonus TTL outputs
    gpio_init(PIN_RGB_R); gpio_init(PIN_RGB_G); gpio_init(PIN_RGB_B);
    gpio_init(PIN_RGB_I); gpio_init(PIN_RGB_CSYNC);
    gpio_set_dir(PIN_RGB_R, GPIO_OUT); gpio_set_dir(PIN_RGB_G, GPIO_OUT);
    gpio_set_dir(PIN_RGB_B, GPIO_OUT); gpio_set_dir(PIN_RGB_I, GPIO_OUT);
    gpio_set_dir(PIN_RGB_CSYNC, GPIO_OUT);

    // 3-5. Subsystem init
    video_init();   // PIO0 SM0 tick + colour tables
    dram_init();    // GP0-GP9 as SIO outputs
    cpu_init();     // PIO0 SM1 cpu_clock

    // 6. Start PIO0 SM0 (tick) and SM1 (clock) simultaneously
    pio_enable_sm_mask_in_sync(pio0, (1u << 0) | (1u << 1));

    // 7. Launch Core 1
    multicore_launch_core1(io_core1_entry);

    // 8. Run — never returns
    video_run();
}
