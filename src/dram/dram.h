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

// Direct DRAM command builders — used by ula_scanline_prepare().
// Return a packed 32-bit command word; caller writes it into the DMA buffer.
// See dram.pio for command word format.
uint32_t dram_cmd_cpu(bool write);
uint32_t dram_cmd_ula_read(uint8_t row, uint8_t col);
uint32_t dram_cmd_refresh(uint8_t row);

// Shared pixel pipeline state — written by IRQ, read by ula_scanline_prepare
extern volatile uint8_t dram_bitmap_reg;  // last pixel byte from DRAM
extern volatile uint8_t dram_attr_reg;    // last attribute byte from DRAM
extern volatile uint8_t dram_fetch_phase; // no idea of what this does
extern volatile bool     dram_bitmap_ready;
extern volatile bool     dram_attr_ready;

// Shadow copy of ZX Spectrum video RAM, updated by the D-bus capture IRQ.
// Indexed by the ZX video address (same word offsets as bitmap_addr /
// attr_addr in ula_dma.h):
//   [0x0000..0x17FF] — pixel bitmap (6144 bytes, scrambled ZX layout)
//   [0x1800..0x1AFF] — colour attributes (768 bytes)
// Single-writer (D-bus capture IRQ on Core 0), so no mutex needed.
#define SHADOW_VRAM_SIZE  0x1B00u   // 6912 bytes
extern volatile uint8_t shadow_vram[SHADOW_VRAM_SIZE];

// Called by ula_dma_irq_handler at the start of each scanline's DMA.
// Arms the D-bus capture state machine for the new scanline:
//   - Active lines (0..191): enables capture, resets col/phase counters
//   - Non-active lines: disables capture (only refresh commands run)
void dram_begin_line(int line);
