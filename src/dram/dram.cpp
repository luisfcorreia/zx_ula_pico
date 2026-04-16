// =============================================================================
// dram.cpp — DRAM GPIO initialisation
//
// Core 0 drives all DRAM signals directly via SIO GPIO in video_run().
// There is no PIO DRAM controller — the pixel loop handles RAS/CAS/WE/A
// phase-by-phase, faithfully replicating the Verilog DRAM state machine.
// =============================================================================

#include "dram.h"
#include "pinmap.h"
#include "hardware/gpio.h"

void dram_init(void) {
    // A[6:0]: outputs, initially 0
    for (int i = PIN_RA_BASE; i < PIN_RA_BASE + PIN_RA_COUNT; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }
    // /RAS, /CAS, /WE: outputs, initially deasserted (high)
    gpio_init(PIN_RAS_N); gpio_set_dir(PIN_RAS_N, GPIO_OUT); gpio_put(PIN_RAS_N, 1);
    gpio_init(PIN_CAS_N); gpio_set_dir(PIN_CAS_N, GPIO_OUT); gpio_put(PIN_CAS_N, 1);
    gpio_init(PIN_WE_N);  gpio_set_dir(PIN_WE_N,  GPIO_OUT); gpio_put(PIN_WE_N,  1);
}
