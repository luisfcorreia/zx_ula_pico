#pragma once
#include <stdint.h>

// Load dram.pio into PIO1, configure SM0 (DRAM sequencer), SM1 (D-bus
// capture IRQ), SM2 (refresh). Configure RA[6:0], /RAS, /CAS, /WE pins.
// Must be called before video_raster_loop() begins issuing DRAM commands.
void dram_init(void);

// IRQ handler — called by PIO1 SM1 when a ULA read cycle completes and
// the D bus has valid data. Stores byte into BitmapReg or AttrReg
// depending on which fetch phase triggered the IRQ.
// Registered automatically by dram_init().
void dram_capture_irq_handler(void);

// Direct DRAM command builders — used by video.cpp raster loop.
// These push a packed 32-bit command word into PIO1 SM0 TX FIFO.
// See dram.pio for command word format.
void dram_cmd_cpu(bool write);
void dram_cmd_ula_read(uint8_t row, uint8_t col);
void dram_cmd_refresh(uint8_t row);

// Shared pixel pipeline state — written by IRQ handler, read by Core 0.
// Single-writer (IRQ on Core 0), so no mutex needed.
extern volatile uint8_t dram_bitmap_reg;  // last pixel byte from DRAM
extern volatile uint8_t dram_attr_reg;    // last attribute byte from DRAM
extern volatile uint8_t dram_fetch_phase; // no idea of what this does
extern volatile bool     dram_bitmap_ready;
extern volatile bool     dram_attr_ready;
