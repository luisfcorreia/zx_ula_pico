// =============================================================================
// main.cpp — ZX Spectrum RP2350B ULA bootstrap
// =============================================================================

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/resets.h"
#include <initializer_list>

#include "pinmap.h"
#include "clock/clock.h"
#include "video/video.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "io/io.h"

#if USE_TESTFRAME
#include "testframe/testframe.h"
#endif

int main(void) {
    clock_init_252mhz();

    reset_block_mask(RESETS_RESET_SPI0_BITS | RESETS_RESET_I2C0_BITS);

    // Data bus GP10-GP17: inputs, pull-up
    for (int i = PIN_D_BASE; i < PIN_D_BASE + PIN_D_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }
    // Z80 inputs
    for (int p : {PIN_WR_N, PIN_RD_N, PIN_MREQ_N, PIN_IO_ULA_N, PIN_A14, PIN_A15}) {
        gpio_init(p); gpio_set_dir(p, GPIO_IN);
    }
    // Z80 outputs (SIO)
    gpio_init(PIN_INT_N);    gpio_set_dir(PIN_INT_N,    GPIO_OUT); gpio_put(PIN_INT_N,    1);
    gpio_init(PIN_CLOCK);    gpio_set_dir(PIN_CLOCK,    GPIO_OUT); gpio_put(PIN_CLOCK,    0);
    gpio_init(PIN_ROM_CS_N); gpio_set_dir(PIN_ROM_CS_N, GPIO_OUT); gpio_put(PIN_ROM_CS_N, 1);

    // Sound / keyboard
    gpio_init(PIN_SOUND); gpio_set_dir(PIN_SOUND, GPIO_IN);
    for (int i = PIN_T_BASE; i < PIN_T_BASE + PIN_T_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }

#if USE_TESTFRAME
    testframe_init();
    pio_enable_sm_mask_in_sync(pio0, (1u << 0));
    pio_enable_sm_mask_in_sync(pio1, (1u << 0));
    testframe_run();
#else
    video_init();
    dram_init();
    cpu_init();
    pio_enable_sm_mask_in_sync(pio0, (1u << 0) | (1u << 1));
    pio_enable_sm_mask_in_sync(pio1, (1u << 0) | (1u << 1));
    multicore_launch_core1(io_core1_entry);
    video_run();
#endif
}
