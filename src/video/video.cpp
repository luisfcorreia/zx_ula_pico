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

[[noreturn]] void video_scanline_loop(void) {
    while (true) {
        while (!scanline_ready) tight_loop_contents();
        scanline_ready = false;

        int idle_buf = active_buf ^ 1;

        // Output colours for the just-completed scanline
        const uint32_t *colour = scanline_buf[idle_buf].colour_out;
        for (int px = 0; px < PIXELS_PER_LINE; px++) {
            ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, colour[px]);
        }

        // Flash counter: once per frame
        if (++flash_scanline_cnt >= TOTAL_LINES) {
            flash_scanline_cnt = 0;
            flash_cnt++;
        }

        // Prepare idle buffer for next scanline
        int next_line = (current_line + 1) % TOTAL_LINES;
        ula_scanline_prepare(idle_buf, next_line);
    }
}
