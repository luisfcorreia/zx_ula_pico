// =============================================================================
// dram.cpp — DRAM control via PIO1 SM1
//
// PIO1 SM1 outputs 10-bit DRAM words to GP0-GP9 at 7 MHz.
// Core 0 pushes computed DRAM words to the TX FIFO each pixel tick.
// =============================================================================

#include "dram.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "dram.pio.h"

void dram_init(void) {
    int offset = pio_add_program(pio1, &dram_ctrl_program);

    pio_sm_config c = dram_ctrl_program_get_default_config(offset);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_out_pins(&c, PIN_RA_BASE, PIN_RA_COUNT + 3);
    sm_config_set_clkdiv_int_frac(&c, PIO0_CLK_DIV, 0);

    pio_sm_init(pio1, SM_DRAM, offset, &c);

    for (int i = PIN_RA_BASE; i < PIN_RA_BASE + PIN_RA_COUNT + 3; i++) {
        pio_gpio_init(pio1, i);
    }
    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM, PIN_RA_BASE, PIN_RA_COUNT + 3, true);

    pio_sm_put(pio1, SM_DRAM, PIN_DRAM_IDLE);
}
