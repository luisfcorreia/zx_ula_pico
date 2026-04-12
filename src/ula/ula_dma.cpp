// =============================================================================
// ula_dma.c — DMA setup for ZX ULA PIO state machines
// =============================================================================

#include "ula_dma.h"
#include "dram/dram.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Ping-pong scanline buffers
// ---------------------------------------------------------------------------

ScanlineBuffer scanline_buf[2];

// DMA channel handles
int ch_sync;
int ch_pixel;
int ch_clock;
int ch_dram;

// Active buffer index (the one currently being played by DMA)
volatile bool scanline_ready = false;
volatile int  active_buf     = 0;
volatile int  current_line   = 0;

// ---------------------------------------------------------------------------
// ZX Spectrum 48K /INT timing
// Interrupt is asserted for 32 pixel clocks at the start of each frame
// (pixel clocks 0..31 of line 0). All other pixel clocks: deasserted.
// ---------------------------------------------------------------------------

static bool int_n_active_at(int line, int pixel_col) {
    return (line == 0 && pixel_col < 32);
}

// ---------------------------------------------------------------------------
// ZX Spectrum 48K contention pattern
// Contention applies during ULA screen area fetches on lines 0..191,
// pixel columns 0..127 (8 contended slots of 4 clocks each per line).
// The CPU clock is held HIGH during contended pixel clocks.
// During non-contended clocks the clock alternates: column 0→HIGH, 1→LOW...
// ---------------------------------------------------------------------------

static bool clock_high_at(int line, int pixel_col) {
    // Contended region: ULA owns bus, hold clock HIGH
    if (line < ACTIVE_LINES && pixel_col < 128) {
        // Contention pattern: slots 0,2,4,6 of each 8-clock group are contended
        int slot = (pixel_col % 8);
        if (slot < 4) return true;
    }
    // Normal: square wave — HIGH on even pixel clocks, LOW on odd
    return (pixel_col % 2) == 0;
}

// External state read by colour_out computation
extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;

// Active display horizontal extent (pixel clocks within total scanline)
#define ACTIVE_START    64              // first active pixel clock
#define ACTIVE_END      (ACTIVE_START + ACTIVE_PIXELS)  // 320

// ---------------------------------------------------------------------------
// ula_scanline_prepare — build bitstreams, DRAM commands, and colour_out[]
// ---------------------------------------------------------------------------

void ula_scanline_prepare(int buf_idx, int line) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];
    memset(buf, 0, sizeof(*buf));

    // ------------------------------------------------------------------
    // Determine per-scanline vertical sync state (constant across the line)
    // ------------------------------------------------------------------
    bool in_vblank = (line >= VBLANK_START && line <= VBLANK_END);
    bool in_vsync  = (line >= VSYNC_START  && line <= VSYNC_END);

    // Local snapshot of shared state (avoid re-reading volatile each pixel)
    uint8_t bc         = border_colour;
    uint8_t flash      = flash_cnt;

    // ------------------------------------------------------------------
    // Per-pixel loop: fill int_n[], clock[], pixel[], and colour_out[]
    // ------------------------------------------------------------------
    for (int px = 0; px < PIXELS_PER_LINE; px++) {
        int word = px / 32;
        int bit  = 31 - (px % 32);     // MSB first → bit 31 = pixel 0

        // -- /INT bitstream -----------------------------------------------
        if (!int_n_active_at(line, px))
            buf->int_n[word] |= (1u << bit);    // 1 = deasserted

        // -- Clock bitstream ----------------------------------------------
        if (clock_high_at(line, px))
            buf->clock[word] |= (1u << bit);

        // -- Sync and blanking flags for this pixel ------------------------
        bool in_hblank = (px >= HBLANK_START && px <= HBLANK_END);
        bool in_hsync  = (px >= HSYNC_START  && px <= HSYNC_END);
        bool hsync_n   = !in_hsync;
        bool vsync_n   = !in_vsync;

        // -- colour_out[] -------------------------------------------------
        uint32_t gpio_word;

        if (px >= PIXELS_PER_LINE - 1 || px > HC_MAX) {
            // Padding pixels beyond HC_MAX — output black, syncs deasserted
            gpio_word = colour_gpio_hi(0, true, true);

        } else if (in_hsync || in_vsync) {
            // Sync tip (composite: bring signal to lowest level)
            gpio_word = sync_gpio_hi(hsync_n, vsync_n);

        } else if (in_hblank || in_vblank) {
            // Non-sync blanking — black pedestal, syncs deasserted
            gpio_word = colour_gpio_hi(0, true, true);

        } else if (line < ACTIVE_LINES && px >= ACTIVE_START && px < ACTIVE_END) {
            // Active display area — look up from shadow video RAM
            int screen_col = px - ACTIVE_START;     // 0..255

            uint16_t bmp_off = bitmap_addr((uint16_t)line, (uint16_t)screen_col);
            uint16_t atr_off = attr_addr  ((uint16_t)line, (uint16_t)screen_col);

            uint8_t bitmap_byte = shadow_vram[bmp_off];
            uint8_t attr_byte   = shadow_vram[atr_off];

            uint8_t bright = (attr_byte >> 6) & 1u;
            uint8_t paper  = (uint8_t)((bright << 3) | ((attr_byte >> 3) & 7u));
            uint8_t ink    = (uint8_t)((bright << 3) | (attr_byte & 7u));

            // Flash: swap ink/paper every 16 frames when FLASH attribute set
            if ((attr_byte & 0x80u) && (flash & 0x10u)) {
                uint8_t tmp = ink; ink = paper; paper = tmp;
            }

            // Pixel bit: MSB of the byte group is the leftmost pixel
            int bit_pos = 7 - (screen_col & 7);
            bool pixel_on = (bitmap_byte >> bit_pos) & 1u;

            gpio_word = colour_gpio_hi(pixel_on ? ink : paper, true, true);

        } else {
            // Border — use border colour, pass through sync signals
            gpio_word = colour_gpio_hi(bc, hsync_n, vsync_n);
        }

        buf->colour_out[px] = gpio_word;

        // -- Pixel bitstream (for PIO SM1 — drives R pin from shadow data) --
        if (line < ACTIVE_LINES && px >= ACTIVE_START && px < ACTIVE_END) {
            int screen_col = px - ACTIVE_START;
            uint16_t bmp_off   = bitmap_addr((uint16_t)line, (uint16_t)screen_col);
            uint8_t  bmp_byte  = shadow_vram[bmp_off];
            int      bit_pos   = 7 - (screen_col & 7);
            if ((bmp_byte >> bit_pos) & 1u)
                buf->pixel[word] |= (1u << bit);
        }
    }

    // ------------------------------------------------------------------
    // DRAM command list
    //
    // For active lines: 32 interleaved (bitmap, attr) pairs = 64 ULA reads.
    // Physical DRAM addresses derived from ZX video address formulas.
    // The D-bus capture IRQ expects this exact (bitmap, attr) interleaving
    // so it can index shadow_vram correctly via cap_phase.
    //
    // For all lines: one RAS-only refresh using the scanline number as the
    // refresh row address (covers all 128 rows over two frames @ 50 Hz).
    // ------------------------------------------------------------------
    uint32_t *cmds = buf->dram_cmds;
    uint32_t  n    = 0;

    if (line < ACTIVE_LINES) {
        for (int col = 0; col < 32; col++) {
            uint16_t bmp = bitmap_addr((uint16_t)line, (uint16_t)(col << 3));
            uint16_t atr = attr_addr  ((uint16_t)line, (uint16_t)(col << 3));

            uint8_t bmp_row = (uint8_t)((bmp >> 7) & 0x7Fu);
            uint8_t bmp_col = (uint8_t)(bmp & 0x7Fu);
            uint8_t atr_row = (uint8_t)((atr >> 7) & 0x7Fu);
            uint8_t atr_col = (uint8_t)(atr & 0x7Fu);

            cmds[n++] = dram_cmd_ula_read(bmp_row, bmp_col);   // bitmap
            cmds[n++] = dram_cmd_ula_read(atr_row, atr_col);   // attr
        }
    }

    // RAS-only refresh (op=11)
    cmds[n++] = dram_cmd_refresh((uint8_t)(line & 0x7Fu));

    // Pad with idle (op=00) — DMA always sends MAX_DRAM_CMDS_PER_LINE words
    while (n < MAX_DRAM_CMDS_PER_LINE)
        cmds[n++] = 0u;

    buf->dram_cmd_count = n;
}

// ---------------------------------------------------------------------------
// ula_dma_init — configure DMA channels for all four SMs
// ---------------------------------------------------------------------------

void ula_dma_init(void) {
    // Claim four DMA channels
    ch_sync  = dma_claim_unused_channel(true);
    ch_pixel = dma_claim_unused_channel(true);
    ch_clock = dma_claim_unused_channel(true);
    ch_dram  = dma_claim_unused_channel(true);

    // --- CH_SYNC: PIO0 SM0 TX FIFO (/INT bitstream) -------------------------
    {
        dma_channel_config c = dma_channel_get_default_config(ch_sync);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        // Pace to SM TX DREQ so we never overfill the FIFO
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_SYNC, true));

        dma_channel_configure(
            ch_sync,
            &c,
            &PIO_VIDEO->txf[SM_SYNC],          // write to SM TX FIFO
            scanline_buf[0].int_n,              // read from buffer A initially
            WORDS_PER_LINE,
            false                               // don't start yet
        );

        // Raise DMA_IRQ_0 when this channel completes
        dma_channel_set_irq0_enabled(ch_sync, true);
    }

    // --- CH_PIXEL: PIO0 SM1 TX FIFO (pixel bitstream) -----------------------
    {
        dma_channel_config c = dma_channel_get_default_config(ch_pixel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_PIXEL, true));

        dma_channel_configure(
            ch_pixel,
            &c,
            &PIO_VIDEO->txf[SM_PIXEL],
            scanline_buf[0].pixel,
            WORDS_PER_LINE,
            false
        );
    }

    // --- CH_CLOCK: PIO0 SM3 TX FIFO (clock/contention bitstream) ------------
    {
        dma_channel_config c = dma_channel_get_default_config(ch_clock);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_VIDEO, SM_CLOCK, true));

        dma_channel_configure(
            ch_clock,
            &c,
            &PIO_VIDEO->txf[SM_CLOCK],
            scanline_buf[0].clock,
            WORDS_PER_LINE,
            false
        );
    }

    // --- CH_DRAM: PIO1 SM0 TX FIFO (DRAM command words) --------------------
    {
        dma_channel_config c = dma_channel_get_default_config(ch_dram);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(PIO_DRAM, SM_DRAM, true));

        dma_channel_configure(
            ch_dram,
            &c,
            &PIO_DRAM->txf[SM_DRAM],
            scanline_buf[0].dram_cmds,
            MAX_DRAM_CMDS_PER_LINE,
            false
        );
    }

    // Precompute first two scanlines before starting
    ula_scanline_prepare(0, 0);
    ula_scanline_prepare(1, 1);

    // Install and enable the DMA completion IRQ
    irq_set_exclusive_handler(DMA_IRQ_0, ula_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Kick off scanline 0
    active_buf   = 0;
    current_line = 0;
    ula_dma_start(0);
}

// ---------------------------------------------------------------------------
// ula_dma_start — (re)point all channels at buf_idx and trigger
// ---------------------------------------------------------------------------

void ula_dma_start(int buf_idx) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];

    // Repoint read addresses
    dma_channel_set_read_addr(ch_sync,  buf->int_n,     false);
    dma_channel_set_read_addr(ch_pixel, buf->pixel,     false);
    dma_channel_set_read_addr(ch_clock, buf->clock,     false);
    dma_channel_set_read_addr(ch_dram,  buf->dram_cmds, false);

    // Reset transfer counts
    dma_channel_set_trans_count(ch_sync,  WORDS_PER_LINE,          false);
    dma_channel_set_trans_count(ch_pixel, WORDS_PER_LINE,          false);
    dma_channel_set_trans_count(ch_clock, WORDS_PER_LINE,          false);
    dma_channel_set_trans_count(ch_dram,  MAX_DRAM_CMDS_PER_LINE,  false);

    // Start all four simultaneously via the multi-channel trigger
    dma_start_channel_mask(
        (1u << ch_sync) | (1u << ch_pixel) | (1u << ch_clock) | (1u << ch_dram)
    );
}

// ---------------------------------------------------------------------------
// ula_dma_irq_handler — fires when ch_sync completes (end of scanline)
// ---------------------------------------------------------------------------

void ula_dma_irq_handler(void) {
    // Clear the interrupt flag for ch_sync
    dma_hw->ints0 = (1u << ch_sync);

    // Swap buffers
    active_buf ^= 1;
    current_line++;
    if (current_line >= TOTAL_LINES)
        current_line = 0;

    // Arm shadow VRAM capture for the new scanline's DRAM fetches
    dram_begin_line(current_line);

    // Start DMA on the buffer that was precomputed by Core 0
    ula_dma_start(active_buf);

    // Signal Core 0 that the new scanline has started
    scanline_ready = true;

    // Precompute the next scanline into the now-idle buffer.
    // This runs inside the IRQ — keep it fast. If preparation is too slow,
    // move this to Core 1 and use a semaphore to signal readiness.
    int next_line = (current_line + 1) % TOTAL_LINES;
    ula_scanline_prepare(active_buf ^ 1, next_line);
}
