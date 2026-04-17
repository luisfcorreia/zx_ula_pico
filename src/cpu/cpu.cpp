// =============================================================================
// cpu.cpp — CPU clock PIO initialisation
//
// Pure free-running 3.5 MHz square wave on PIN_CLOCK (GP25).
// PIO0 SM1, clkdiv=36 → 7 MHz; two SET instructions = 3.5 MHz output.
// No FIFO, no autopull, no DMA — clock starts immediately with the SM.
//
// Contention is applied externally by Core 1 (io.cpp):
//   When CPU accesses 0x4000-0x7FFF during a ULA fetch window, Core 1
//   polls until PIN_CLOCK=HIGH then disables SM3 (clock held HIGH = wait).
//   Re-enables SM3 when contention condition clears.
// =============================================================================

#include "cpu.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "cpu_clock.pio.h"

int cpu_clock_program_offset;

void cpu_init(void) {
    cpu_clock_program_offset = pio_add_program(pio0, &cpu_clock_program);

    pio_sm_config c = cpu_clock_program_get_default_config(cpu_clock_program_offset);

    // Clock output via SET pins
    sm_config_set_set_pins(&c, PIN_CLOCK, 1);

    // No OUT pins, no FIFO, no autopull
    sm_config_set_clkdiv_int_frac(&c, PIO0_CLK_DIV, 0);

    pio_sm_init(pio0, SM_CPUCLK, cpu_clock_program_offset, &c);

    pio_gpio_init(pio0, PIN_CLOCK);
    pio_sm_set_consecutive_pindirs(pio0, SM_CPUCLK, PIN_CLOCK, 1, true);

    // SM1 started by main.cpp via pio_enable_sm_mask_in_sync().
}
