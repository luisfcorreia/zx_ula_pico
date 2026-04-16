#pragma once
#include <stdint.h>

// Initialise PIO0 SM0 (tick generator) and precompute colour tables.
void video_init(void);

// Core 0 main loop — never returns.
// Replicates the ZX Spectrum ULA Verilog always @(negedge clk7) logic:
//   - Maintains hc/vc raster counters
//   - Drives DRAM /RAS, /CAS, /WE, A[6:0] per-phase
//   - Runs pixel pipeline: BitmapReg → SRegister (shift) → colour output
//   - Outputs yn/uo/vo/r/g/b/bright/hsync/vsync via GPIO HI bank
//   - Asserts /INT at the correct frame position
[[noreturn]] void video_run(void);

// Shared flag: set by Core 0 each pixel clock.
// True when ULA is in an active display fetch window (phases 8-11, Border_n active).
// Read by Core 1 to gate CPU clock contention.
extern volatile bool ula_fetch_window;

// gpio helper for high GPIO bank (GP32+)
#include "hardware/structs/sio.h"
#include "hardware/address_mapped.h"
static inline void ula_gpio_put_hi(uint32_t mask, uint32_t value) {
    hw_write_masked(&sio_hw->gpio_hi_out, value, mask);
}