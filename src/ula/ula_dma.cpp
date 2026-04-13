// =============================================================================
// ula_dma.cpp — DMA setup for ZX ULA PIO state machines
//
// CPU clock DMA uses a self-chaining pair (ch_clock_0 ↔ ch_clock_1):
//   ch_clock_0 always feeds buf[0].clock  (active when active_buf == 0)
//   ch_clock_1 always feeds buf[1].clock  (active when active_buf == 1)
//   Each channel is configured to chain to the other on completion.
//   Hardware chaining gives zero gap between scanlines so the SM never stalls.
//
// Bitstream word layout (MSB first, 15 words = 480 bits per scanline):
//   word N bit 31 = pixel N*32+0, bit 30 = pixel N*32+1, ...
//   Pixels 456-479 are padding (24 bits, word 14 bits 23-0):
//     clock[] padding = 0xAAAAAA  (alternating 1,0,... continuing the wave)
//     int_n[] padding = 0xFFFFFF  (all deasserted = 1)
//     pixel[] padding = 0x000000  (all paper = 0)  — from memset
// =============================================================================

#include "ula_dma.h"
#include "dram/dram.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include <string.h>

ScanlineBuffer scanline_buf[2];

int ch_sync;
int ch_pixel;
int ch_clock_0;
int ch_clock_1;
int ch_dram;

volatile bool scanline_ready = false;
volatile int  active_buf     = 0;
volatile int  current_line   = 0;

static bool int_n_active_at(int line, int pixel_col) {
    return (line == 0 && pixel_col < 32);
}

// ---------------------------------------------------------------------------
// ZX Spectrum 48K contention pattern
//
// During active display lines (0-191), the ULA fetches one bitmap byte and
// one attribute byte per 8-pixel group. The CPU clock is held HIGH during
// the fetch window to insert wait states.
//
// Simplified implementation: within each 8-pixel group in the active area
// (hc 0-127), positions 0-3 are contended (clock held HIGH), positions 4-7
// are uncontended (normal alternating square wave).
//
// Outside the active area: normal 3.5 MHz square wave (HIGH on even px).
// ---------------------------------------------------------------------------
static bool clock_high_at(int line, int pixel_col) {
    if (line < ACTIVE_LINES && pixel_col < 128) {
        int slot = pixel_col % 8;
        if (slot < 4) return true;   // contended — hold HIGH
    }
    return (pixel_col % 2) == 0;    // normal square wave
}

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;

#define ACTIVE_START    64
#define ACTIVE_END      (ACTIVE_START + ACTIVE_PIXELS)

// Padding bits for word 14 (pixels 456-479):
//   clock: alternating starting HIGH (px456 is even)
//     = 101010101010101010101010b = 0xAAAAAA
//   int_n: all deasserted = all 1s
//     = 0xFFFFFF
#define CLOCK_PADDING   0x00AAAAAu
#define INT_N_PADDING   0x00FFFFFFu

// ---------------------------------------------------------------------------
// ula_scanline_prepare
// ---------------------------------------------------------------------------
void ula_scanline_prepare(int buf_idx, int line) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];
    memset(buf, 0, sizeof(*buf));

    bool in_vblank = (line >= VBLANK_START && line <= VBLANK_END);
    bool in_vsync  = (line >= VSYNC_START  && line <= VSYNC_END);

    uint8_t bc    = border_colour;
    uint8_t flash = flash_cnt;

    // Snapshot volatile VRAM once per line (64 reads) rather than 512 times
    // in the pixel loop. Lets the compiler keep values in registers.
    uint8_t bmp_cache[32];
    uint8_t atr_cache[32];
    if (line < ACTIVE_LINES) {
        for (int g = 0; g < 32; g++) {
            bmp_cache[g] = shadow_vram[bitmap_addr((uint16_t)line, (uint16_t)(g << 3))];
            atr_cache[g] = shadow_vram[attr_addr  ((uint16_t)line, (uint16_t)(g << 3))];
        }
    }

    for (int px = 0; px < PIXELS_PER_LINE; px++) {
        int word = px / 32;
        int bit  = 31 - (px % 32);

        // /INT bitstream (1 = deasserted)
        if (!int_n_active_at(line, px))
            buf->int_n[word] |= (1u << bit);

        // Clock bitstream (1 = HIGH)
        if (clock_high_at(line, px))
            buf->clock[word] |= (1u << bit);

        bool in_hblank = (px >= HBLANK_START && px <= HBLANK_END);
        bool in_hsync  = (px >= HSYNC_START  && px <= HSYNC_END);
        bool hsync_n   = !in_hsync;
        bool vsync_n   = !in_vsync;

        uint32_t gpio_word;

        if (px > HC_MAX) {
            gpio_word = colour_gpio_hi(0, true, true);

        } else if (in_hsync || in_vsync) {
            gpio_word = sync_gpio_hi(hsync_n, vsync_n);

        } else if (in_hblank || in_vblank) {
            gpio_word = colour_gpio_hi(0, true, true);

        } else if (line < ACTIVE_LINES && px >= ACTIVE_START && px < ACTIVE_END) {
            int screen_col  = px - ACTIVE_START;
            int grp         = screen_col >> 3;
            int bit_pos     = 7 - (screen_col & 7);

            uint8_t bitmap_byte = bmp_cache[grp];
            uint8_t attr_byte   = atr_cache[grp];

            uint8_t bright = (attr_byte >> 6) & 1u;
            uint8_t paper  = (uint8_t)((bright << 3) | ((attr_byte >> 3) & 7u));
            uint8_t ink    = (uint8_t)((bright << 3) | (attr_byte & 7u));

            if ((attr_byte & 0x80u) && (flash & 0x10u)) {
                uint8_t tmp = ink; ink = paper; paper = tmp;
            }

            bool pixel_on = (bitmap_byte >> bit_pos) & 1u;
            gpio_word = colour_gpio_hi(pixel_on ? ink : paper, true, true);

            if (pixel_on)
                buf->pixel[word] |= (1u << bit);

        } else {
            gpio_word = colour_gpio_hi(bc, hsync_n, vsync_n);
        }

        buf->colour_out[px] = gpio_word;
    }

    // Fix padding bits in word 14 (pixels 456-479)
    // clock: continue alternating wave (pixel 456 is even → HIGH first)
    // int_n: deasserted (1) for all padding positions
    buf->clock[WORDS_PER_LINE - 1] |= CLOCK_PADDING;
    buf->int_n[WORDS_PER_LINE - 1] |= INT_N_PADDING;

    // DRAM command list
    uint32_t *cmds = buf->dram_cmds;
    uint32_t  n    = 0;

    if (line < ACTIVE_LINES) {
        for (int col = 0; col < 32; col++) {
            uint16_t bmp = bitmap_addr((uint16_t)line, (uint16_t)(col << 3));
            uint16_t atr = attr_addr  ((uint16_t)line, (uint16_t)(col << 3));
            cmds[n++] = dram_cmd_ula_read((uint8_t)((bmp >> 7) & 0x7Fu), (uint8_t)(bmp & 0x7Fu));
            cmds[n++] = dram_cmd_ula_read((uint8_t)((atr >> 7) & 0x7Fu), (uint8_t)(atr & 0x7Fu));
        }
    }

    cmds[n++] = dram_cmd_refresh((uint8_t)(line & 0x7Fu));

    while (n < MAX_DRAM_CMDS_PER_LINE)
        cmds[n++] = 0u;

    buf->dram_cmd_count = n;
}

// ---------------------------------------------------------------------------
// ula_dma_init
// ---------------------------------------------------------------------------
void ula_dma_init(void) {
    ch_sync    = dma_claim_unused_channel(true);
    ch_pixel   = dma_claim_unused_channel(true);
    ch_clock_0 = dma_claim_unused_channel(true);
    ch_clock_1 = dma_claim_unused_channel(true);
    ch_dram    = dma_claim_unused_channel(true);

    // CH_SYNC: /INT bitstream → SM0 TX FIFO, triggers IRQ on completion
    {
        dma_channel_config c = dma_channel_get_default_config(ch_sync);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_SYNC, true));
        dma_channel_configure(ch_sync, &c, &PIO_VIDEO->txf[SM_SYNC],
                              scanline_buf[0].int_n, WORDS_PER_LINE, false);
        dma_channel_set_irq0_enabled(ch_sync, true);
    }

    // CH_PIXEL: pixel bitstream → SM1 TX FIFO, restarted by IRQ
    {
        dma_channel_config c = dma_channel_get_default_config(ch_pixel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_PIXEL, true));
        dma_channel_configure(ch_pixel, &c, &PIO_VIDEO->txf[SM_PIXEL],
                              scanline_buf[0].pixel, WORDS_PER_LINE, false);
    }

    // CH_CLOCK_0 and CH_CLOCK_1: self-chaining pair → SM3 TX FIFO
    //
    // ch_clock_0 always feeds buf[0].clock, chains to ch_clock_1.
    // ch_clock_1 always feeds buf[1].clock, chains back to ch_clock_0.
    //
    // Hardware chaining = zero-cycle gap between scanlines. The SM never
    // stalls, giving a gapless 3.5 MHz (contention-modulated) clock.
    //
    // Since active_buf alternates 0,1,0,1..., buf[0] is always prepared
    // (by Core 0) before ch_clock_0 replays it, and likewise for buf[1].
    {
        dma_channel_config c = dma_channel_get_default_config(ch_clock_0);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_CLOCK, true));
        channel_config_set_chain_to(&c, ch_clock_1);
        dma_channel_configure(ch_clock_0, &c, &PIO_VIDEO->txf[SM_CLOCK],
                              scanline_buf[0].clock, WORDS_PER_LINE, false);
    }
    {
        dma_channel_config c = dma_channel_get_default_config(ch_clock_1);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_CLOCK, true));
        channel_config_set_chain_to(&c, ch_clock_0);
        dma_channel_configure(ch_clock_1, &c, &PIO_VIDEO->txf[SM_CLOCK],
                              scanline_buf[1].clock, WORDS_PER_LINE, false);
    }

    // CH_DRAM: command words → PIO1 SM0 TX FIFO, restarted by IRQ
    {
        dma_channel_config c = dma_channel_get_default_config(ch_dram);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_DRAM, SM_DRAM, true));
        dma_channel_configure(ch_dram, &c, &PIO_DRAM->txf[SM_DRAM],
                              scanline_buf[0].dram_cmds, MAX_DRAM_CMDS_PER_LINE, false);
    }

    ula_scanline_prepare(0, 0);
    ula_scanline_prepare(1, 1);

    irq_set_exclusive_handler(DMA_IRQ_0, ula_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    active_buf   = 0;
    current_line = 0;

    // Start ch_clock_0 once — the self-chain keeps it running forever.
    // ch_clock_1 is pre-armed; it will fire automatically when ch_clock_0
    // finishes its first scanline (~68.6 µs).
    dma_channel_set_read_addr(ch_clock_0, scanline_buf[0].clock, false);
    dma_channel_set_trans_count(ch_clock_0, WORDS_PER_LINE, true); // trigger=true

    // Start the remaining channels (sync/pixel/dram) for scanline 0
    ula_dma_start(0);
}

// ---------------------------------------------------------------------------
// ula_dma_start — restart sync, pixel and dram channels for buf_idx
// Clock channels are NOT touched — the self-chain handles them.
// ---------------------------------------------------------------------------
void ula_dma_start(int buf_idx) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];

    dma_channel_set_read_addr(ch_sync,  buf->int_n,     false);
    dma_channel_set_read_addr(ch_pixel, buf->pixel,     false);
    dma_channel_set_read_addr(ch_dram,  buf->dram_cmds, false);

    dma_channel_set_trans_count(ch_sync,  WORDS_PER_LINE,         false);
    dma_channel_set_trans_count(ch_pixel, WORDS_PER_LINE,         false);
    dma_channel_set_trans_count(ch_dram,  MAX_DRAM_CMDS_PER_LINE, false);

    dma_start_channel_mask((1u << ch_sync) | (1u << ch_pixel) | (1u << ch_dram));
}

// ---------------------------------------------------------------------------
// ula_dma_irq_handler — fires when ch_sync completes each scanline
// Clock channels are self-managing; no action needed for them here.
// ---------------------------------------------------------------------------
void ula_dma_irq_handler(void) {
    dma_hw->ints0 = (1u << ch_sync);

    if (++current_line >= TOTAL_LINES)
        current_line = 0;

    active_buf ^= 1;

    dram_begin_line(current_line);
    ula_dma_start(active_buf);
    scanline_ready = true;
}
