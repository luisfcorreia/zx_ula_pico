// =============================================================================
// video_init.cpp — PIO0 SM0/SM1 setup for video output
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "ula/ula_dma.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "sync.pio.h"
#include "pixel.pio.h"

void video_init(void) {
    // SM0: sync generator (/INT bitstream, DMA-fed)
    uint offset_sync = pio_add_program(pio0, &sync_gen_program);
    pio_sm_config cs = sync_gen_program_get_default_config(offset_sync);
    sm_config_set_out_pins(&cs, PIN_INT_N, 1);
    sm_config_set_out_shift(&cs, false, true, 32);
    sm_config_set_fifo_join(&cs, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&cs, PIO0_CLK_DIV, 0);
    pio_sm_init(pio0, SM_SYNC, offset_sync, &cs);
    pio_gpio_init(pio0, PIN_INT_N);
    pio_sm_set_consecutive_pindirs(pio0, SM_SYNC, PIN_INT_N, 1, true);
    gpio_put(PIN_INT_N, 1);

    // SM1: pixel shift register (DMA-fed, no physical pin — Core 0 owns all
    // video GPIO via ula_gpio_put_hi; SM1 runs to prevent FIFO stall only)
    uint offset_pixel = pio_add_program(pio0, &pixel_shift_program);
    pio_sm_config cp = pixel_shift_program_get_default_config(offset_pixel);
    sm_config_set_out_pins(&cp, PIN_R, 1);
    sm_config_set_out_shift(&cp, false, true, 32);
    sm_config_set_fifo_join(&cp, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&cp, PIO0_CLK_DIV, 0);
    pio_sm_init(pio0, SM_PIXEL, offset_pixel, &cp);
    // pio_gpio_init NOT called for PIN_R — Core 0 owns that pin.

    // Initialise all video output pins to black, syncs deasserted
    ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, colour_gpio_hi(0, true, true));

    // SMs left stopped — main.cpp starts all PIO0 SMs together.
}
