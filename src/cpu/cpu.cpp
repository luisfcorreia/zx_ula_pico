// =============================================================================
// cpu.cpp — CPU clock PIO initialisation
//
// PIO0 SM3, clkdiv=36 → 7 MHz. DMA feeds precomputed bitstream.
// Normal: alternating 1/0 = 3.5 MHz square wave.
// Contended pixels: consecutive 1s = clock held HIGH.
//
// A self-chaining DMA pair (ch_clock_0 ↔ ch_clock_1) drives the FIFO
// continuously with zero inter-scanline gap, so the SM never stalls.
// =============================================================================

#include "cpu.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "cpu_clock.pio.h"

#define SM_CPUCLK   3

void cpu_init(void) {
    uint offset = pio_add_program(pio0, &cpu_clock_program);

    pio_sm_config c = cpu_clock_program_get_default_config(offset);

    sm_config_set_out_pins(&c, PIN_CLOCK, 1);
    sm_config_set_out_shift(&c, false, true, 32);  // shift left, autopull 32
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&c, PIO0_CLK_DIV, 0);

    pio_sm_init(pio0, SM_CPUCLK, offset, &c);
    pio_gpio_init(pio0, PIN_CLOCK);
    pio_sm_set_consecutive_pindirs(pio0, SM_CPUCLK, PIN_CLOCK, 1, true);

    // SM started by main.cpp via pio_enable_sm_mask_in_sync().
}
