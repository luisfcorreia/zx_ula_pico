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

        int line = current_line;
        const ScanlineBuffer *buf = &scanline_buf[active_buf];

        for (int px = 0; px < PIXELS_PER_LINE; px++) {
            ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, buf->colour_out[px]);
        }

        gpio_put(PIN_ROM_CS_N,
            gpio_get(PIN_A15) | gpio_get(PIN_A14) | gpio_get(PIN_MREQ_N));

        if (++flash_scanline_cnt >= TOTAL_LINES) {
            flash_scanline_cnt = 0;
            flash_cnt++;
        }

        int next2 = (line + 2) % TOTAL_LINES;
        ula_scanline_prepare(active_buf ^ 1, next2);
    }
}
