// =============================================================================
// dram.cpp — DRAM controller initialisation and D-bus capture
//
// PIO1 SM0: dram_ctrl state machine (dram.pio)
//   Runs at 252 MHz (no clock divider) for 4 ns RAS/CAS timing resolution.
//   Core 0 pushes DRAM command words; SM0 drives RA[6:0], /RAS, /CAS, /WE.
//
// PIO1 SM1: D-bus capture trigger
//   A minimal 2-instruction SM that fires PIO1 IRQ 0 when the DRAM /CAS
//   hold window is open (indicated by SM0 asserting a shared IRQ flag).
//   The IRQ handler on Core 0 then samples GPIO[PIN_D_BASE..+7].
//
// PIO1 SM2: RAS-only refresh (managed separately from SM0 during blanking)
//   Core 0 pushes refresh row addresses during blanking phases.
//
// IRQ handler (Core 0):
//   Fires when SM1 signals that D-bus data is valid.
//   Stores byte into dram_bitmap_reg or dram_attr_reg based on
//   the fetch_phase flag set by Core 0 before issuing the DRAM command.
// =============================================================================

#include "dram.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

// Generated headers from pioasm (via CMakeLists pico_generate_pio_header)
#include "dram.pio.h"

// Shared state — written by IRQ, read by Core 0 raster loop
volatile uint8_t dram_bitmap_reg  = 0;
volatile uint8_t dram_attr_reg    = 0;
volatile bool    dram_bitmap_ready = false;
volatile bool    dram_attr_ready   = false;

// Set by Core 0 before pushing a ULA read command, so the IRQ handler
// knows whether to store into bitmap or attribute register.
// 0 = pixel fetch,  1 = attribute fetch
volatile uint8_t dram_fetch_phase = 0;

// PIO1 SM indices
#define SM_DRAM_CTRL    0
#define SM_DBUS_CAP     1
#define SM_REFRESH      2

// ---- D-bus capture IRQ --------------------------------------------------
// Called on Core 0 when PIO1 IRQ 0 fires (SM0 signals /CAS hold open).
static void __isr dram_capture_irq_handler_internal(void) {
    // Clear the IRQ flag in PIO1
    pio1->irq = (1u << 0);

    // Sample data bus (D[7:0] = GP10..GP17)
    uint8_t data = (uint8_t)((gpio_get_all() >> PIN_D_BASE) & 0xFF);

    if (dram_fetch_phase == 0) {
        dram_bitmap_reg   = data;
        dram_bitmap_ready = true;
    } else {
        dram_attr_reg   = data;
        dram_attr_ready = true;
    }
}

// ---- Public init --------------------------------------------------------
void dram_init(void) {
    // Load dram_ctrl program into PIO1
    uint offset_ctrl = pio_add_program(pio1, &dram_ctrl_program);

    // Configure SM0: dram_ctrl at 252 MHz (div=1)
    pio_sm_config c0 = dram_ctrl_program_get_default_config(offset_ctrl);

    // SET pins: /RAS(GP7), /CAS(GP8), /WE(GP9) — 3 consecutive
    sm_config_set_set_pins(&c0, PIN_RAS_N, 3);

    // OUT pins: RA[6:0] (GP0–GP6) — 7 consecutive
    sm_config_set_out_pins(&c0, PIN_RA_BASE, PIN_RA_COUNT);

    // No clock divider — run at full 252 MHz for 4 ns resolution
    sm_config_set_clkdiv(&c0, 1.0f);

    // Autopull disabled — Core 0 pushes commands explicitly
    sm_config_set_out_shift(&c0, false, false, 32);

    pio_sm_init(pio1, SM_DRAM_CTRL, offset_ctrl, &c0);

    // Configure GPIO directions for DRAM control pins
    pio_gpio_init(pio1, PIN_RAS_N);
    pio_gpio_init(pio1, PIN_CAS_N);
    pio_gpio_init(pio1, PIN_WE_N);
    for (int i = PIN_RA_BASE; i < PIN_RA_BASE + PIN_RA_COUNT; i++)
        pio_gpio_init(pio1, i);

    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM_CTRL, PIN_RA_BASE, PIN_RA_COUNT, true);
    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM_CTRL, PIN_RAS_N, 3, true);

    // Initial safe state: all DRAM control signals deasserted
    gpio_put(PIN_RAS_N, 1);
    gpio_put(PIN_CAS_N, 1);
    gpio_put(PIN_WE_N,  1);

    // ---- SM1: D-bus capture (minimal SM, fires IRQ when SM0 signals) ----
    // We use a trivial PIO program: wait for IRQ 4 (set by SM0 during CAS hold),
    // then raise IRQ 0 (triggers Core 0 CPU IRQ handler).
    // Program is too small to need a separate .pio file — load inline.
    //
    //   wait 1 irq 4    ; wait for SM0 to signal /CAS is held
    //   irq 0           ; fire CPU IRQ
    //   jmp 0           ; loop
    static const uint16_t cap_prog[] = {
        0xC024,  // wait 1 irq 4
        0xC000,  // irq 0  (fires PIO1 IRQ 0 → CPU IRQ handler)
        0x0000,  // jmp 0
    };
    struct pio_program cap_def = { .instructions = cap_prog, .length = 3, .origin = -1 };
    uint offset_cap = pio_add_program(pio1, &cap_def);

    pio_sm_config c1 = pio_get_default_sm_config();
    sm_config_set_wrap(&c1, offset_cap, offset_cap + 2);
    sm_config_set_clkdiv(&c1, 1.0f);
    pio_sm_init(pio1, SM_DBUS_CAP, offset_cap, &c1);

    // Register IRQ handler on Core 0 for PIO1 IRQ 0
    // PIO1 IRQ 0 maps to system IRQ PIO1_IRQ_0
    irq_set_exclusive_handler(PIO1_IRQ_0, dram_capture_irq_handler_internal);
    irq_set_enabled(PIO1_IRQ_0, true);
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);

    // ---- SM2: refresh (same program as SM0, separate instance) ----------
    // Reuse dram_ctrl program — refresh commands use op=3
    pio_sm_config c2 = dram_ctrl_program_get_default_config(offset_ctrl);
    sm_config_set_set_pins(&c2, PIN_RAS_N, 3);
    sm_config_set_out_pins(&c2, PIN_RA_BASE, PIN_RA_COUNT);
    sm_config_set_clkdiv(&c2, 1.0f);
    sm_config_set_out_shift(&c2, false, false, 32);
    pio_sm_init(pio1, SM_REFRESH, offset_ctrl, &c2);

    // Start all three SMs simultaneously to keep them phase-aligned
    pio_enable_sm_mask_in_sync(pio1,
        (1u << SM_DRAM_CTRL) | (1u << SM_DBUS_CAP) | (1u << SM_REFRESH));
}

// ---- Command builders ---------------------------------------------------
// These are called by video.cpp's raster loop. They push a packed 32-bit
// command word into PIO1 SM0 (or SM2 for refresh) TX FIFO.
// Non-blocking: if FIFO is full the command is dropped — at 7 MHz rates
// the FIFO (4 entries) will never fill between pushes.

void dram_cmd_cpu(bool write) {
    uint32_t cmd = (1u << 30) | ((uint32_t)write << 29);
    pio_sm_put(pio1, SM_DRAM_CTRL, cmd);
}

void dram_cmd_ula_read(uint8_t row, uint8_t col) {
    uint32_t cmd = (2u << 30)                    |
                   ((uint32_t)(col & 0x7F) << 22) |
                   ((uint32_t)(row & 0x7F) << 15);
    pio_sm_put(pio1, SM_DRAM_CTRL, cmd);
}

void dram_cmd_refresh(uint8_t row) {
    uint32_t cmd = (3u << 30) | ((uint32_t)(row & 0x7F) << 15);
    pio_sm_put(pio1, SM_REFRESH, cmd);  // refresh uses SM2
}

// Public alias for IRQ handler (declared in dram.h)
void dram_capture_irq_handler(void) {
    dram_capture_irq_handler_internal();
}
