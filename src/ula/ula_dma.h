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
#include "hardware/structs/sio.h"
#include "hardware/address_mapped.h"
#include "pinmap.h"

// gpio_put_masked_hi was added to the SDK specifically for RP2350 and may
// not be present in all SDK builds. Use hw_write_masked on gpio_hi_out directly.
static inline void ula_gpio_put_hi(uint32_t mask, uint32_t value) {
    hw_write_masked(&sio_hw->gpio_hi_out, value, mask);
}

// ---------------------------------------------------------------------------
// ZX Spectrum 48K timing constants
// ---------------------------------------------------------------------------

#define PIXELS_PER_LINE     456
#define TOTAL_LINES         312
#define ACTIVE_LINES        192
#define ACTIVE_PIXELS       256
#define WORDS_PER_LINE      ((PIXELS_PER_LINE + 31) / 32)   // = 15
#define MAX_DRAM_CMDS_PER_LINE  66

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
// VIDEO_GPIO_HI_MASK — all video output pins in RP2350 high GPIO bank
// All video output pins are GP33-GP44. Bit positions relative to GP32.
// ---------------------------------------------------------------------------
#define VIDEO_GPIO_HI_BASE  32u
#define VIDEO_GPIO_HI_MASK ( \
    (0xFu << (PIN_YN_BASE  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_UO_BASE  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_VO_BASE  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_R        - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_G        - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_B        - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_BRIGHT   - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_HSYNC_N  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_VSYNC_N  - VIDEO_GPIO_HI_BASE)) )

// ---------------------------------------------------------------------------
// ZX video address helpers (screen-relative coordinates)
// vc = screen line 0..191, hc = screen pixel column 0..255
// Returns offset into shadow_vram[].
// Physical DRAM: row = (addr >> 7) & 0x7F, col = addr & 0x7F
// ---------------------------------------------------------------------------
static inline uint16_t bitmap_addr(uint16_t vc, uint16_t hc) {
    return (uint16_t)(
        ((vc & 0xC0u) << 5) |
        ((vc & 0x07u) << 8) |
        ((vc & 0x38u) << 2) |
        ((hc >> 3) & 0x1Fu)
    );
}
static inline uint16_t attr_addr(uint16_t vc, uint16_t hc) {
    return (uint16_t)(0x1800u | ((vc >> 3) << 5) | ((hc >> 3) & 0x1Fu));
}

// ---------------------------------------------------------------------------
// GPIO HI word builders
// ---------------------------------------------------------------------------
#include "colour_lut.h"

// lut_idx = 4-bit ZX colour index {BRIGHT[3], G[2], R[1], B[0]}
static inline uint32_t colour_gpio_hi(uint8_t lut_idx, bool hsync_n, bool vsync_n) {
    return ((uint32_t)lut_yn(lut_idx)      << (PIN_YN_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)lut_uo(lut_idx)      << (PIN_UO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)lut_vo(lut_idx)      << (PIN_VO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 1) & 1) << (PIN_R        - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 2) & 1) << (PIN_G        - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 0) & 1) << (PIN_B        - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 3) & 1) << (PIN_BRIGHT   - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)hsync_n              << (PIN_HSYNC_N  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)vsync_n              << (PIN_VSYNC_N  - VIDEO_GPIO_HI_BASE));
}

static inline uint32_t sync_gpio_hi(bool hsync_n, bool vsync_n) {
    return ((uint32_t)YN_SYNC_TIP << (PIN_YN_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)UO_NEUTRAL  << (PIN_UO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)VO_NEUTRAL  << (PIN_VO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)hsync_n     << (PIN_HSYNC_N  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)vsync_n     << (PIN_VSYNC_N  - VIDEO_GPIO_HI_BASE));
}

// ---------------------------------------------------------------------------
// Scanline buffer (double-buffered ping-pong)
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t int_n[WORDS_PER_LINE];
    uint32_t clock[WORDS_PER_LINE];
    uint32_t pixel[WORDS_PER_LINE];
    uint32_t dram_cmds[MAX_DRAM_CMDS_PER_LINE];
    uint32_t dram_cmd_count;
    uint32_t colour_out[PIXELS_PER_LINE];
} ScanlineBuffer;

extern ScanlineBuffer scanline_buf[2];

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
extern volatile bool scanline_ready;
extern volatile int  active_buf;
extern volatile int  current_line;

extern int ch_sync;
extern int ch_pixel;
extern int ch_clock;
extern int ch_dram;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void ula_dma_init(void);
void ula_dma_start(int buf_idx);
void ula_scanline_prepare(int buf_idx, int line);
void ula_dma_irq_handler(void);

#endif // ULA_DMA_H
