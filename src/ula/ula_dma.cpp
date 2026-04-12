// =============================================================================
// ula_dma.cpp — DMA setup for ZX ULA PIO state machines
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
int ch_clock;
int ch_dram;

volatile bool scanline_ready = false;
volatile int  active_buf     = 0;
volatile int  current_line   = 0;

static bool int_n_active_at(int line, int pixel_col) {
    return (line == 0 && pixel_col < 32);
}

static bool clock_high_at(int line, int pixel_col) {
    if (line < ACTIVE_LINES && pixel_col < 128) {
        if ((pixel_col % 8) < 4) return true;
    }
    return (pixel_col % 2) == 0;
}

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;

#define ACTIVE_START    64
#define ACTIVE_END      (ACTIVE_START + ACTIVE_PIXELS)

// ---------------------------------------------------------------------------
// ula_scanline_prepare
//
// Performance: shadow_vram is volatile. Reading it 2x256 times inside the
// pixel loop serialises every access and makes this function too slow to
// complete within one scanline period (~68 us at 252 MHz), which causes the
// cpu_clock SM to stall between scanlines producing ~80 kHz instead of 3.5 MHz.
//
// Fix: snapshot the 32 unique (bitmap, attr) pairs for this line into plain
// local arrays once. Inner loop reads from stack which the compiler keeps
// in registers. Volatile access count drops from 512 to 64 per active line.
// ---------------------------------------------------------------------------
void ula_scanline_prepare(int buf_idx, int line) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];
    memset(buf, 0, sizeof(*buf));

    bool in_vblank = (line >= VBLANK_START && line <= VBLANK_END);
    bool in_vsync  = (line >= VSYNC_START  && line <= VSYNC_END);

    uint8_t bc    = border_colour;
    uint8_t flash = flash_cnt;

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

        if (!int_n_active_at(line, px))
            buf->int_n[word] |= (1u << bit);

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
    ch_sync  = dma_claim_unused_channel(true);
    ch_pixel = dma_claim_unused_channel(true);
    ch_clock = dma_claim_unused_channel(true);
    ch_dram  = dma_claim_unused_channel(true);

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
    {
        dma_channel_config c = dma_channel_get_default_config(ch_pixel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_PIXEL, true));
        dma_channel_configure(ch_pixel, &c, &PIO_VIDEO->txf[SM_PIXEL],
                              scanline_buf[0].pixel, WORDS_PER_LINE, false);
    }
    {
        dma_channel_config c = dma_channel_get_default_config(ch_clock);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_CLOCK, true));
        dma_channel_configure(ch_clock, &c, &PIO_VIDEO->txf[SM_CLOCK],
                              scanline_buf[0].clock, WORDS_PER_LINE, false);
    }
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
    ula_dma_start(0);
}

// ---------------------------------------------------------------------------
// ula_dma_start
// ---------------------------------------------------------------------------
void ula_dma_start(int buf_idx) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];

    dma_channel_set_read_addr(ch_sync,  buf->int_n,     false);
    dma_channel_set_read_addr(ch_pixel, buf->pixel,     false);
    dma_channel_set_read_addr(ch_clock, buf->clock,     false);
    dma_channel_set_read_addr(ch_dram,  buf->dram_cmds, false);

    dma_channel_set_trans_count(ch_sync,  WORDS_PER_LINE,         false);
    dma_channel_set_trans_count(ch_pixel, WORDS_PER_LINE,         false);
    dma_channel_set_trans_count(ch_clock, WORDS_PER_LINE,         false);
    dma_channel_set_trans_count(ch_dram,  MAX_DRAM_CMDS_PER_LINE, false);

    dma_start_channel_mask(
        (1u << ch_sync) | (1u << ch_pixel) | (1u << ch_clock) | (1u << ch_dram));
}

// ---------------------------------------------------------------------------
// ula_dma_irq_handler — kept minimal so the SM never stalls
//
// Does NOT call ula_scanline_prepare. Core 0 owns that work.
// ---------------------------------------------------------------------------
void ula_dma_irq_handler(void) {
    dma_hw->ints0 = (1u << ch_sync);

    if (++current_line >= TOTAL_LINES)
        current_line = 0;

    active_buf ^= 1;

    dram_begin_line(current_line);

    // Restart DMA immediately on the buffer Core 0 already prepared
    ula_dma_start(active_buf);

    // Signal Core 0
    scanline_ready = true;
}