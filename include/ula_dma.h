#pragma once
// =============================================================================
// ula_dma.h — DMA setup for ZX ULA PIO state machines
// =============================================================================

#ifndef ULA_DMA_H
#define ULA_DMA_H

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pinmap.h"

#define PIXELS_PER_LINE         456
#define TOTAL_LINES             312
#define ACTIVE_LINES            192
#define ACTIVE_PIXELS           256
#define WORDS_PER_LINE          ((PIXELS_PER_LINE + 31) / 32)   // 15
#define MAX_DRAM_CMDS_PER_LINE  16

#define VIDEO_GPIO_MASK ( \
    (0xFu << PIN_YN_BASE) | (1u << PIN_UO) | (1u << PIN_VO) | \
    (1u << PIN_R) | (1u << PIN_G) | (1u << PIN_B) | (1u << PIN_BRIGHT) | \
    (1u << PIN_HSYNC_N) | (1u << PIN_VSYNC_N) )

#define PIO_VIDEO   pio0
#define SM_SYNC     0
#define SM_PIXEL    1
#define SM_CLOCK    3
#define PIO_DRAM    pio1
#define SM_DRAM     0

typedef struct {
    uint32_t int_n[WORDS_PER_LINE];
    uint32_t clock[WORDS_PER_LINE];
    uint32_t pixel[WORDS_PER_LINE];
    uint32_t dram_cmds[MAX_DRAM_CMDS_PER_LINE];
    // Precomputed full GPIO value per pixel clock.
    // Core 0 writes colour_out[px] via gpio_put_masked(VIDEO_GPIO_MASK, ...)
    // in a tight loop during the scanline's playback window.
    uint32_t colour_out[PIXELS_PER_LINE];
} ScanlineBuffer;

extern ScanlineBuffer scanline_buf[2];
extern int ch_sync, ch_pixel, ch_clock, ch_dram;

// Set by IRQ handler when DMA for the new scanline has started.
// Cleared by Core 0 at the top of each scanline iteration.
extern volatile bool scanline_ready;
extern volatile int  active_buf;
extern volatile int  current_line;

void ula_dma_init(void);
void ula_dma_start(int buf_idx);
void ula_scanline_prepare(int buf_idx, int line);
void ula_dma_irq_handler(void);

#endif
