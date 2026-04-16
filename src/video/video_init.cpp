// =============================================================================
// video_init.cpp — PIO0 SM0 tick generator + colour table precompute
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define SM_SYNC 0

// Precomputed GPIO HI word tables
uint32_t colour_table[16];   // indexed by {BRIGHT,G,R,B}
uint32_t border_table[8];    // indexed by border GRB (no BRIGHT)
uint32_t sync_word;          // sync tip level
uint32_t black_word;         // blanking pedestal

void video_init(void) {
    // Colour tables
    for (int i = 0; i < 16; i++)
        colour_table[i] = build_colour_word((uint8_t)i);

    for (int i = 0; i < 8; i++)
        border_table[i] = build_colour_word((uint8_t)i);  // no BRIGHT for border

    sync_word  = build_sync_word();
    black_word = build_black_word();

    // Initialise all video GPIO to black (blanking pedestal)
    ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, black_word);
}
