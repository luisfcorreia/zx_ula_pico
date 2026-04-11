// =============================================================================
// video.cpp — Core 0 scanline colour output loop
//
// DMA REDESIGN — what changed vs original video_raster_loop():
//
//   REMOVED:
//     - pio_sm_get_blocking(pio0, SM_SYNC)  — pixel clock gate (was blocking)
//     - pio_sm_put(pio0, SM_SYNC, ...)      — /INT word push to SM0 FIFO
//     - pio_sm_put(pio0, SM_CPUCLK, ...)    — contention bit push to SM3 FIFO
//     - dram_cmd_*() calls inside the loop  — DRAM command dispatch
//     - gpio_put(PIN_INT_N, ...)            — /INT driven by PIO SM0 now
//
//   RETAINED:
//     - gpio_put_masked() colour output     — still done per pixel by Core 0
//     - gpio_put(PIN_ROM_CS_N, ...)         — still done per scanline
//     - flash_cnt increment                 — moved to per-scanline boundary
//
//   MOVED TO ula_scanline_prepare() in ula_dma.c:
//     - /INT bitstream computation          — packed into int_n[] DMA buffer
//     - clock/contention bitstream          — packed into clock[] DMA buffer
//     - DRAM command list                   — packed into dram_cmds[] buffer
//     - Pixel bitstream                     — packed into pixel[] buffer
//     - Colour GPIO values                  — packed into colour_out[] buffer
//
//   SYNC MECHANISM:
//     Old: Core 0 blocked on pio_sm_get_blocking() once per pixel clock.
//     New: Core 0 polls `scanline_ready` flag set by DMA completion IRQ.
//          One flag per scanline (456 pixel clocks). Core 0 outputs 456
//          colour values in a tight loop (~36 sys cycles each = ~1 scanline).
//
//   TIMING NOTE:
//     gpio_put_masked() at 252 MHz takes ~6 cycles (inline). A 456-iteration
//     loop with ~30 cycles of loop overhead per pixel totals ~36 cycles/pixel,
//     which matches 252 MHz / 7 MHz = 36 sys cycles per pixel clock exactly.
//     If the loop runs slightly fast, Core 0 starts the buffer prep early
//     (no harm). If slightly slow, scanline_ready for the next scanline will
//     already be set when Core 0 re-checks (no frames are lost — DMA and PIO
//     continue autonomously without Core 0 involvement).
// =============================================================================

#include "video.h"
#include "ula_dma.h"
#include "dram/dram.h"
#include "pinmap.h"
#include "colour_lut.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;

// Flash counter: increment once per frame (TOTAL_LINES scanlines)
static uint32_t flash_scanline_cnt = 0;

// ---- Display address computation (used by ula_scanline_prepare linkage) ---
uint16_t bitmap_addr(uint16_t vc, uint16_t hc) {
    return (uint16_t)(
        ((vc & 0xC0u) << 5) |
        ((vc & 0x07u) << 8) |
        ((vc & 0x38u) << 2) |
        ((hc >> 3) & 0x1Fu)
    );
}

uint16_t attr_addr(uint16_t vc, uint16_t hc) {
    return (uint16_t)(0x1800u | ((vc >> 3) << 5) | ((hc >> 3) & 0x1Fu));
}

// ---- video_scanline_loop -----------------------------------------------
// Core 0 main loop. Never returns.
// Each iteration handles one scanline:
//   1. Wait for IRQ to signal scanline N has started on DMA/PIO.
//   2. Output precomputed colour values for scanline N.
//   3. Prepare DMA buffers for scanline N+2 (N+1 prepared by IRQ handler).
[[noreturn]] void video_scanline_loop(void) {
    while (true) {
        // ------------------------------------------------------------------
        // 1. Wait for DMA IRQ to signal new scanline has started.
        //    The IRQ handler sets scanline_ready after calling ula_dma_start().
        // ------------------------------------------------------------------
        while (!scanline_ready) tight_loop_contents();
        scanline_ready = false;

        int line = current_line;
        const ScanlineBuffer *buf = &scanline_buf[active_buf];

        // ------------------------------------------------------------------
        // 2. Output precomputed colour values for each pixel of this scanline.
        //    ~36 system cycles per iteration fills one pixel clock period.
        //    All video output pins (YN, U, V, R, G, B, BRIGHT, HSYNC, VSYNC)
        //    are updated atomically via a single gpio_put_masked() call.
        // ------------------------------------------------------------------
        for (int px = 0; px < PIXELS_PER_LINE; px++) {
            gpio_put_masked(VIDEO_GPIO_MASK, buf->colour_out[px]);
        }

        // /ROM_CS: combinatorial — re-evaluate once per scanline is sufficient
        // (the Z80 MREQ/A15/A14 signals change slowly relative to scanline rate)
        gpio_put(PIN_ROM_CS_N,
            gpio_get(PIN_A15) | gpio_get(PIN_A14) | gpio_get(PIN_MREQ_N));

        // Flash counter: advance once per complete frame
        if (++flash_scanline_cnt >= TOTAL_LINES) {
            flash_scanline_cnt = 0;
            flash_cnt++;
        }

        // ------------------------------------------------------------------
        // 3. Prepare DMA buffers for scanline N+2.
        //    (Scanline N+1 was already prepared by the IRQ handler itself.)
        //    Writes into the buffer not currently being played by DMA.
        // ------------------------------------------------------------------
        int next2 = (line + 2) % TOTAL_LINES;
        ula_scanline_prepare(active_buf ^ 1, next2);
    }
}
