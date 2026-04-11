// =============================================================================
// cpu.cpp — CPU clock PIO initialisation
//
// Loads cpu_clock.pio into PIO0 SM3 and configures the CLOCK output pin
// (GP25). The SM runs at 7 MHz (PIO0_CLK_DIV = 36) in lockstep with the
// sync and pixel SMs.
//
// DMA CHANGE (vs original):
//   Previously CLOCK was driven via side-set, and Core 0 pushed one
//   contention bit per pixel clock into the TX FIFO each loop iteration.
//
//   Now:
//   - CLOCK is an OUT pin (side-set removed from cpu_clock.pio).
//   - Core 0 precomputes the full clock+contention waveform for a scanline
//     into a uint32_t bitstream (alternating 1/0 for normal operation,
//     consecutive 1s for contended pixels).
//   - DMA feeds the bitstream into TX FIFO, paced by SM TX DREQ.
//   - Autopull threshold = 32 (one DMA word covers 32 pixel clocks).
//   - No FIFO pre-fill needed — DMA starts simultaneously with pio_enable.
// =============================================================================

#include "cpu.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "cpu_clock.pio.h"

#define SM_CPUCLK   3

void cpu_init(void) {
    uint offset = pio_add_program(pio0, &cpu_clock_program);

    pio_sm_config c = cpu_clock_program_get_default_config(offset);

    // CLOCK is now an OUT pin, not side-set.
    // The PIO program is `out pins, 1 / .wrap` — one clock bit per cycle.
    sm_config_set_out_pins(&c, PIN_CLOCK, 1);

    // Shift left, autopull enabled, threshold = 32 bits.
    // DMA feeds precomputed clock bitstream; SM shifts 1 bit per pixel clock.
    sm_config_set_out_shift(&c, false, true, 32);

    // Join FIFOs into TX for deeper DMA buffering.
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // 7 MHz — matches sync and pixel SMs exactly.
    sm_config_set_clkdiv_int_frac(&c, PIO0_CLK_DIV, 0);

    pio_sm_init(pio0, SM_CPUCLK, offset, &c);

    pio_gpio_init(pio0, PIN_CLOCK);
    pio_sm_set_consecutive_pindirs(pio0, SM_CPUCLK, PIN_CLOCK, 1, true);

    // SM3 is started by main.cpp together with SM0/SM1 via
    // pio_enable_sm_mask_in_sync(). Do NOT call pio_sm_set_enabled() here.
}
