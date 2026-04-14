// =============================================================================
// dram.cpp — DRAM controller initialisation and D-bus capture
//
// /WE (GP9) is NOT owned by PIO. Core 1 (io.cpp) drives it directly via
// gpio_put() based on bus signals: !MREQ_N & !A15 & A14 & !WR_N.
// This gives the ~30 ns propagation delay described in the ZX Spectrum ULA
// documentation naturally from Core 1's polling loop latency.
//
// PIO SET pins count=2: PIO controls only /RAS (GP7) and /CAS (GP8).
// =============================================================================

#include "dram.h"
#include "pinmap.h"
#include "ula/ula_dma.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include "dram.pio.h"

volatile uint8_t dram_bitmap_reg   = 0;
volatile uint8_t dram_attr_reg     = 0;
volatile bool    dram_bitmap_ready = false;
volatile bool    dram_attr_ready   = false;
volatile uint8_t dram_fetch_phase  = 0;

volatile uint8_t shadow_vram[SHADOW_VRAM_SIZE];

static volatile bool    cap_active = false;
static volatile int     cap_line   = 0;
static volatile uint8_t cap_col    = 0;
static volatile uint8_t cap_phase  = 0;

static void __isr dram_capture_irq_handler_internal(void) {
    pio1->irq = (1u << 0);

    if (!cap_active) return;

    uint8_t data = (uint8_t)((gpio_get_all() >> PIN_D_BASE) & 0xFFu);

    if (cap_phase == 0) {
        uint16_t off = (uint16_t)(
            ((cap_line & 0xC0u) << 5) |
            ((cap_line & 0x07u) << 8) |
            ((cap_line & 0x38u) << 2) |
            (cap_col & 0x1Fu)
        );
        shadow_vram[off] = data;
        cap_phase = 1;
    } else {
        uint16_t off = (uint16_t)(0x1800u | ((cap_line >> 3) << 5) | (cap_col & 0x1Fu));
        shadow_vram[off] = data;
        cap_phase = 0;
        if (++cap_col >= 32u) cap_col = 0;
    }
}

void dram_capture_irq_handler(void) {
    dram_capture_irq_handler_internal();
}

void dram_begin_line(int line) {
    if (line >= 0 && line < ACTIVE_LINES) {
        cap_line   = line;
        cap_col    = 0;
        cap_phase  = 0;
        cap_active = true;
    } else {
        cap_active = false;
    }
}

#define SM_DRAM_CTRL    0
#define SM_DBUS_CAP     1
#define SM_REFRESH      2

void dram_init(void) {
    uint offset_ctrl = pio_add_program(pio1, &dram_ctrl_program);

    // SM0: dram_ctrl at 252 MHz
    pio_sm_config c0 = dram_ctrl_program_get_default_config(offset_ctrl);

    // SET pins: /RAS (GP7) and /CAS (GP8) only — count=2
    // /WE (GP9) is left for Core 1 to drive via gpio_put()
    sm_config_set_set_pins(&c0, PIN_RAS_N, 2);
    sm_config_set_out_pins(&c0, PIN_RA_BASE, PIN_RA_COUNT);
    sm_config_set_clkdiv(&c0, 1.0f);
    sm_config_set_out_shift(&c0, false, true, 32);  // left-shift, autopull at 32 bits

    pio_sm_init(pio1, SM_DRAM_CTRL, offset_ctrl, &c0);

    for (int i = PIN_RA_BASE; i < PIN_RA_BASE + PIN_RA_COUNT; i++)
        pio_gpio_init(pio1, i);
    pio_gpio_init(pio1, PIN_RAS_N);
    pio_gpio_init(pio1, PIN_CAS_N);
    // NOTE: PIN_WE_N (GP9) NOT given to PIO — Core 1 drives it
    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM_CTRL, PIN_RA_BASE, PIN_RA_COUNT, true);
    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM_CTRL, PIN_RAS_N, 2, true);  // /RAS + /CAS only

    gpio_put(PIN_RAS_N, 1);
    gpio_put(PIN_CAS_N, 1);
    gpio_put(PIN_WE_N,  1);   // deasserted; Core 1 will drive this

    // SM1: D-bus capture trigger
    static const uint16_t cap_prog[] = {
        0xC024,  // wait 1 irq 4
        0xC000,  // irq 0
        0x0000,  // jmp 0
    };
    struct pio_program cap_def = { .instructions = cap_prog, .length = 3, .origin = -1 };
    uint offset_cap = pio_add_program(pio1, &cap_def);
    pio_sm_config c1 = pio_get_default_sm_config();
    sm_config_set_wrap(&c1, offset_cap, offset_cap + 2);
    sm_config_set_clkdiv(&c1, 1.0f);
    pio_sm_init(pio1, SM_DBUS_CAP, offset_cap, &c1);

    irq_set_exclusive_handler(PIO1_IRQ_0, dram_capture_irq_handler_internal);
    irq_set_enabled(PIO1_IRQ_0, true);
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);

    // SM2: refresh (reuses dram_ctrl program)
    pio_sm_config c2 = dram_ctrl_program_get_default_config(offset_ctrl);
    sm_config_set_set_pins(&c2, PIN_RAS_N, 2);
    sm_config_set_out_pins(&c2, PIN_RA_BASE, PIN_RA_COUNT);
    sm_config_set_clkdiv(&c2, 1.0f);
    sm_config_set_out_shift(&c2, false, true, 32);
    pio_sm_init(pio1, SM_REFRESH, offset_ctrl, &c2);

    pio_enable_sm_mask_in_sync(pio1,
        (1u << SM_DRAM_CTRL) | (1u << SM_DBUS_CAP) | (1u << SM_REFRESH));
}

// Command word builders — /WE is controlled by Core 1 GPIO, not by PIO,
// so the wr bit in the command word is no longer used by dram.pio.
// It is kept in the API for compatibility; dram.pio discards it.

uint32_t dram_cmd_cpu(bool write) {
    (void)write;  // /WE driven by Core 1 independently
    return (1u << 30);
}

uint32_t dram_cmd_ula_read(uint8_t row, uint8_t col) {
    return (2u << 30)                      |
           ((uint32_t)(col & 0x7Fu) << 22) |
           ((uint32_t)(row & 0x7Fu) << 15);
}

uint32_t dram_cmd_refresh(uint8_t row) {
    return (3u << 30) | ((uint32_t)(row & 0x7Fu) << 15);
}
