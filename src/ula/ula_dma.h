#pragma once
// =============================================================================
// ula_dma.h — DMA setup for ZX ULA PIO state machines
//
// Four DMA channels, one per SM TX FIFO:
//   CH_DMA_SYNC     → PIO0 SM0 (sync_gen)    — /INT bitstream
//   CH_DMA_PIXEL    → PIO0 SM1 (pixel_shift)  — pixel bitstream
//   CH_DMA_CLOCK    → PIO0 SM3 (cpu_clock)    — clock/contention bitstream
//   CH_DMA_DRAM     → PIO1 SM0 (dram_ctrl)    — command word stream
//
// Each channel is paced by its SM's TX DREQ so it only advances when the
// SM has consumed the previous word — no busy-waiting, no overflow.
//
// Scanline buffers are double-buffered: while DMA plays buffer A into the
// SMs, Core 0 fills buffer B for the next scanline. When DMA raises its
// completion IRQ, Core 0 swaps the pointers and restarts.
// =============================================================================

#ifndef ULA_DMA_H
#define ULA_DMA_H

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

// ---------------------------------------------------------------------------
// ZX Spectrum 48K timing constants
// ---------------------------------------------------------------------------

#define PIXELS_PER_LINE     456     // total pixel clocks per scanline (border + active + blanking)
#define TOTAL_LINES         312     // total scanlines per frame (PAL)
#define ACTIVE_LINES        192     // visible screen lines
#define ACTIVE_PIXELS       256     // visible pixels per line

// Words needed per scanline (ceil(PIXELS_PER_LINE / 32))
#define WORDS_PER_LINE      ((PIXELS_PER_LINE + 31) / 32)   // = 15

// Max DRAM command words per scanline:
//   8 ULA reads (one per 8-pixel group in active area) + 1 CPU + 1 refresh
#define MAX_DRAM_CMDS_PER_LINE  16

// ---------------------------------------------------------------------------
// Pin assignments (must match PIO program sm_config_set_out_pins calls)
// ---------------------------------------------------------------------------

#define PIN_INT_N       24
#define PIN_CLOCK       25
#define PIN_PIXEL_R     26      // pixel SM drives R; G/B set by gpio_put_masked

// ---------------------------------------------------------------------------
// PIO / SM assignments
// ---------------------------------------------------------------------------

#define PIO_VIDEO       pio0
#define SM_SYNC         0
#define SM_PIXEL        1
#define SM_CLOCK        3

#define PIO_DRAM        pio1
#define SM_DRAM         0

// ---------------------------------------------------------------------------
// Scanline buffer layout (double-buffered, ping-pong between A and B)
// ---------------------------------------------------------------------------

typedef struct {
    // /INT bitstream: 1 bit per pixel clock, MSB first
    // bit=1 → /INT high (deasserted), bit=0 → /INT low (asserted)
    uint32_t int_n[WORDS_PER_LINE];

    // CPU clock + contention bitstream: 1 bit per pixel clock, MSB first
    // Normal: alternating 1,0,1,0 (3.5 MHz square wave)
    // Contended pixels: consecutive 1s (clock held HIGH)
    uint32_t clock[WORDS_PER_LINE];

    // Pixel bitstream: 1 bit per pixel clock, MSB first
    // bit=1 → ink colour, bit=0 → paper colour (Core 0 applies colour via GPIO)
    uint32_t pixel[WORDS_PER_LINE];

    // DRAM command words (variable count per line, padded with idle op=0)
    uint32_t dram_cmds[MAX_DRAM_CMDS_PER_LINE];
    uint32_t dram_cmd_count;
} ScanlineBuffer;

// Two scanline buffers for ping-pong DMA
extern ScanlineBuffer scanline_buf[2];

// ---------------------------------------------------------------------------
// DMA channel handles (assigned by ula_dma_init)
// ---------------------------------------------------------------------------

extern int ch_sync;
extern int ch_pixel;
extern int ch_clock;
extern int ch_dram;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Initialise all four PIO state machines and their DMA channels.
// Call once at startup after PIO programs have been loaded.
void ula_dma_init(void);

// Start DMA playback of scanline buffer |buf_idx| (0 or 1) on all four channels.
// Typically called from the DMA completion IRQ handler after the next buffer
// has been precomputed.
void ula_dma_start(int buf_idx);

// Precompute the int_n, clock and pixel bitstreams for |line| (0..TOTAL_LINES-1)
// into scanline_buf[buf_idx]. Also fills dram_cmds.
// Call this on Core 0 while DMA is playing the other buffer.
void ula_scanline_prepare(int buf_idx, int line);

// DMA completion IRQ handler — swap buffers, kick off next scanline DMA,
// then precompute the following scanline. Install with:
//   irq_set_exclusive_handler(DMA_IRQ_0, ula_dma_irq_handler);
//   irq_set_enabled(DMA_IRQ_0, true);
void ula_dma_irq_handler(void);

#endif // ULA_DMA_H
