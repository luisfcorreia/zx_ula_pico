// =============================================================================
// dram.cpp — DRAM controller initialisation and D-bus capture
//
// PIO1 SM0: dram_ctrl state machine (dram.pio)
//   Runs at 252 MHz (no divider) for 4 ns RAS/CAS timing resolution.
//   DMA feeds command words from the scanline buffer into SM0 TX FIFO.
//   SM0 drives RA[6:0], /RAS, /CAS, /WE.
//
// PIO1 SM1: D-bus capture trigger (unchanged from original)
//   Waits on IRQ4 (set by SM0 during /CAS hold), then fires PIO1 IRQ0
//   → Core 0 IRQ handler samples the D bus.
//
// PIO1 SM2: Spare / refresh (shares dram_ctrl program, refresh op=3)
//
// DMA CHANGE:
//   SM0 previously had autopull DISABLED — Core 0 called dram_cmd_*()
//   which pushed words directly with pio_sm_put(). Now autopull is ENABLED
//   (threshold=32) and DMA feeds the command buffer from the scanline struct.
//
//   dram_cmd_*() functions are now pure command-word BUILDERS — they return
//   a uint32_t for use by ula_scanline_prepare() when filling the buffer.
//   They no longer push to the FIFO.
//
// D-bus capture IRQ is unchanged — the DRAM data path (IRQ → bitmap/attr
// registers → incorporated into next scanline's colour buffer) is the same.
// =============================================================================

#include "dram.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include "dram.pio.h"

// Shared pixel pipeline state — written by IRQ, read by ula_scanline_prepare
volatile uint8_t dram_bitmap_reg   = 0;
volatile uint8_t dram_attr_reg     = 0;
volatile bool    dram_bitmap_ready = false;
volatile bool    dram_attr_ready   = false;
volatile uint8_t dram_fetch_phase  = 0;

#define SM_DRAM_CTRL    0
#define SM_DBUS_CAP     1
#define SM_REFRESH      2

// ---- D-bus capture IRQ (unchanged) --------------------------------------
static void __isr dram_capture_irq_handler_internal(void) {
    pio1->irq = (1u << 0);
    uint8_t data = (uint8_t)((gpio_get_all() >> PIN_D_BASE) & 0xFF);
    if (dram_fetch_phase == 0) {
        dram_bitmap_reg   = data;
        dram_bitmap_ready = true;
    } else {
        dram_attr_reg   = data;
        dram_attr_ready = true;
    }
}

void dram_capture_irq_handler(void) {
    dram_capture_irq_handler_internal();
}

// ---- Public init --------------------------------------------------------
void dram_init(void) {
    uint offset_ctrl = pio_add_program(pio1, &dram_ctrl_program);

    // SM0: dram_ctrl at 252 MHz
    pio_sm_config c0 = dram_ctrl_program_get_default_config(offset_ctrl);
    sm_config_set_set_pins(&c0, PIN_RAS_N, 3);
    sm_config_set_out_pins(&c0, PIN_RA_BASE, PIN_RA_COUNT);
    sm_config_set_clkdiv(&c0, 1.0f);

    // DMA CHANGE: autopull ENABLED (was false). DMA feeds command words;
    // SM stalls at first `out` when TX FIFO is empty (same behaviour as
    // the old `pull block` instruction that was removed from dram.pio).
    sm_config_set_out_shift(&c0, false, true, 32);

    pio_sm_init(pio1, SM_DRAM_CTRL, offset_ctrl, &c0);

    for (int i = PIN_RA_BASE; i < PIN_RA_BASE + PIN_RA_COUNT; i++)
        pio_gpio_init(pio1, i);
    pio_gpio_init(pio1, PIN_RAS_N);
    pio_gpio_init(pio1, PIN_CAS_N);
    pio_gpio_init(pio1, PIN_WE_N);
    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM_CTRL, PIN_RA_BASE, PIN_RA_COUNT, true);
    pio_sm_set_consecutive_pindirs(pio1, SM_DRAM_CTRL, PIN_RAS_N, 3, true);

    gpio_put(PIN_RAS_N, 1);
    gpio_put(PIN_CAS_N, 1);
    gpio_put(PIN_WE_N,  1);

    // SM1: D-bus capture (inline program — unchanged)
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

    // SM2: refresh (reuses dram_ctrl program, op=3 commands)
    pio_sm_config c2 = dram_ctrl_program_get_default_config(offset_ctrl);
    sm_config_set_set_pins(&c2, PIN_RAS_N, 3);
    sm_config_set_out_pins(&c2, PIN_RA_BASE, PIN_RA_COUNT);
    sm_config_set_clkdiv(&c2, 1.0f);
    sm_config_set_out_shift(&c2, false, true, 32);  // autopull enabled here too
    pio_sm_init(pio1, SM_REFRESH, offset_ctrl, &c2);

    pio_enable_sm_mask_in_sync(pio1,
        (1u << SM_DRAM_CTRL) | (1u << SM_DBUS_CAP) | (1u << SM_REFRESH));
}

// ---- Command word builders ----------------------------------------------
// These build a packed 32-bit command word for the dram_ctrl SM.
// Called by ula_scanline_prepare() when filling the DMA command buffer.
// They no longer push to any FIFO — that is done entirely by DMA.

uint32_t dram_cmd_cpu(bool write) {
    return (1u << 30) | ((uint32_t)write << 29);
}

uint32_t dram_cmd_ula_read(uint8_t row, uint8_t col) {
    return (2u << 30)                     |
           ((uint32_t)(col & 0x7Fu) << 22) |
           ((uint32_t)(row & 0x7Fu) << 15);
}

uint32_t dram_cmd_refresh(uint8_t row) {
    return (3u << 30) | ((uint32_t)(row & 0x7Fu) << 15);
}
