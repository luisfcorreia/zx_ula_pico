// =============================================================================
// ula_dma.c — DMA setup for ZX ULA PIO state machines
// =============================================================================

#include "ula_dma.h"
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
static volatile int active_buf = 0;

// Current scanline being rendered
static volatile int current_line = 0;

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

// ---------------------------------------------------------------------------
// ula_scanline_prepare — build all three bitstreams + DRAM command list
// ---------------------------------------------------------------------------

void ula_scanline_prepare(int buf_idx, int line) {
    ScanlineBuffer *buf = &scanline_buf[buf_idx];
    memset(buf, 0, sizeof(*buf));

    // Build int_n, clock and pixel bitstreams pixel by pixel
    for (int px = 0; px < PIXELS_PER_LINE; px++) {
        int word  = px / 32;
        int bit   = 31 - (px % 32);   // MSB first → bit 31 = pixel 0

        // /INT bitstream
        if (!int_n_active_at(line, px))
            buf->int_n[word] |= (1u << bit);    // 1 = deasserted

        // Clock bitstream
        if (clock_high_at(line, px))
            buf->clock[word] |= (1u << bit);

        // Pixel bitstream (active display area only)
        if (line < ACTIVE_LINES && px >= 64 && px < 64 + ACTIVE_PIXELS) {
            int screen_col = px - 64;
            // Fetch pixel from ZX Spectrum video RAM (caller should pass pointer;
            // simplified here as a direct memory read from a fixed base address)
            // pixel_data would typically come from a pointer argument in real code.
            // Placeholder: leave bit 0 (all paper colour) for now.
            (void)screen_col;
        }
    }

    // Build DRAM command list for this scanline
    uint32_t *cmds = buf->dram_cmds;
    uint32_t  n    = 0;

    if (line < ACTIVE_LINES) {
        // 8 ULA reads: one per 8-pixel group in the active area
        // Column address = group index, row address = screen line
        for (int grp = 0; grp < 8; grp++) {
            uint8_t col = (uint8_t)grp;
            uint8_t row = (uint8_t)(line & 0x7F);
            // Command word: op=10 (ULA read), wr=0
            // [31:30]=op, [29]=wr, [28:22]=col, [21:15]=row, [14:0]=0
            cmds[n++] = (0b10u << 30) | (col << 22) | (row << 15);
        }
    }

    // RAS-only refresh every scanline (op=11)
    {
        uint8_t refresh_row = (uint8_t)(line & 0x7F);
        cmds[n++] = (0b11u << 30) | (refresh_row << 15);
    }

    // Pad remaining slots with idle command (op=00)
    while (n < MAX_DRAM_CMDS_PER_LINE)
        cmds[n++] = 0;

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

    // Start DMA on the buffer that was precomputed by Core 0
    ula_dma_start(active_buf);

    // Precompute the next scanline into the now-idle buffer.
    // This runs inside the IRQ — keep it fast. If preparation is too slow,
    // move this to Core 1 and use a semaphore to signal readiness.
    int next_line = (current_line + 1) % TOTAL_LINES;
    ula_scanline_prepare(active_buf ^ 1, next_line);
}
