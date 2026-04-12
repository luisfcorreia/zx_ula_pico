// =============================================================================
// video.cpp — Core 0 scanline colour output loop
// =============================================================================

#include "video.h"
#include "ula/ula_dma.h"
#include "dram/dram.h"
#include "pinmap.h"
#include "colour_lut.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;

static uint32_t flash_scanline_cnt = 0;

// ---------------------------------------------------------------------------
// video_scanline_loop — Core 0 main loop, never returns
//
// After each IRQ:
//   active_buf  = buffer DMA is NOW playing (line N, just started)
//   active_buf^1 = buffer DMA just FINISHED (line N-1, free for reuse)
//
// Core 0 must finish all work before the next IRQ fires (~68 us).
// ---------------------------------------------------------------------------
[[noreturn]] void video_scanline_loop(void) {
    while (true) {
        while (!scanline_ready) tight_loop_contents();
        scanline_ready = false;

        // active_buf points to the buffer currently playing (line N).
        // The just-finished buffer is active_buf ^ 1 (line N-1).
        int idle_buf  = active_buf ^ 1;
        int done_line = current_line - 1;
        if (done_line < 0) done_line += TOTAL_LINES;

        // Output colours for the just-completed scanline (line N-1).
        // This runs while DMA is playing line N's bitstreams.
        const uint32_t *colour = scanline_buf[idle_buf].colour_out;
        for (int px = 0; px < PIXELS_PER_LINE; px++) {
            ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, colour[px]);
        }

        gpio_put(PIN_ROM_CS_N,
            gpio_get(PIN_A15) | gpio_get(PIN_A14) | gpio_get(PIN_MREQ_N));

        if (++flash_scanline_cnt >= TOTAL_LINES) {
            flash_scanline_cnt = 0;
            flash_cnt++;
        }

        // Prepare the idle buffer for the scanline AFTER the one currently
        // playing (line N+1). It will be played by DMA two IRQs from now.
        int next_line = (current_line + 1) % TOTAL_LINES;
        ula_scanline_prepare(idle_buf, next_line);
    }
}