// =============================================================================
// video_init.cpp — PIO0 SM0/SM1/SM2/SM3 setup for video output
//
// SM0: sync.pio   — HSync/VSync//INT generator at 7 MHz
// SM1: pixel.pio  — pixel shift register + RGBi at 7 MHz
// SM2: YUV DAC    — no PIO program; Core 0 drives via gpio_put_masked()
// SM3: cpu_clock  — configured in cpu.cpp
//
// All SMs are configured here but NOT started. main.cpp starts all PIO0
// SMs together with pio_enable_sm_mask_in_sync(pio0, 0xF) so they are
// phase-aligned from the first cycle.
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "sync.pio.h"
#include "pixel.pio.h"

#define SM_SYNC   0
#define SM_PIXEL  1

void video_init(void) {
    // ---- SM0: sync generator --------------------------------------------
    uint offset_sync = pio_add_program(pio0, &sync_gen_program);
    pio_sm_config cs = sync_gen_program_get_default_config(offset_sync);

    sm_config_set_sideset_pins(&cs, PIN_INT_N);       // side-set → /INT
    sm_config_set_out_pins(&cs, PIN_INT_N, 1);
    sm_config_set_clkdiv_int_frac(&cs, PIO0_CLK_DIV, 0);
    sm_config_set_fifo_join(&cs, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio0, SM_SYNC, offset_sync, &cs);
    pio_gpio_init(pio0, PIN_INT_N);
    pio_sm_set_consecutive_pindirs(pio0, SM_SYNC, PIN_INT_N, 1, true);
    gpio_put(PIN_INT_N, 1);  // deasserted (active-low)

    // ---- SM1: pixel shift register --------------------------------------
    uint offset_pixel = pio_add_program(pio0, &pixel_shift_program);
    pio_sm_config cp = pixel_shift_program_get_default_config(offset_pixel);

    sm_config_set_out_pins(&cp, PIN_R, 3);             // R, G, B
    sm_config_set_clkdiv_int_frac(&cp, PIO0_CLK_DIV, 0);
    sm_config_set_out_shift(&cp, false, true, 8);      // shift left, autopull 8
    sm_config_set_fifo_join(&cp, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio0, SM_PIXEL, offset_pixel, &cp);
    for (int p = PIN_R; p <= PIN_B; p++) pio_gpio_init(pio0, p);
    pio_sm_set_consecutive_pindirs(pio0, SM_PIXEL, PIN_R, 3, true);

    // ---- YUV DAC: plain GPIO, initialise to black/neutral ---------------
    gpio_put_masked(
        (0xFu << PIN_YN_BASE) | (0xFu << PIN_UO_BASE) | (0xFu << PIN_VO_BASE),
        (11u  << PIN_YN_BASE) | ( 8u  << PIN_UO_BASE) | ( 8u  << PIN_VO_BASE)
    );
    // SMs left stopped — main.cpp enables all together
}

// video_init addendum — configure new pins added in pin reallocation:
// BRIGHT (GP42), HSYNC_N (GP43), VSYNC_N (GP44), U 1-bit (GP37), V 1-bit (GP38)
// Called at end of video_init() — safe to append here as these are plain GPIO.
// In the final build, merge these gpio_init() calls into the main video_init() body.
static void __attribute__((constructor)) video_extra_pins_init(void) {
    // U and V: now 1-bit each
    gpio_init(PIN_UO); gpio_set_dir(PIN_UO, GPIO_OUT); gpio_put(PIN_UO, UO_NEUTRAL);
    gpio_init(PIN_VO); gpio_set_dir(PIN_VO, GPIO_OUT); gpio_put(PIN_VO, VO_NEUTRAL);
    // BRIGHT
    gpio_init(PIN_BRIGHT); gpio_set_dir(PIN_BRIGHT, GPIO_OUT); gpio_put(PIN_BRIGHT, 0);
    // Separate H and V sync outputs (active-low, start deasserted = 1)
    gpio_init(PIN_HSYNC_N); gpio_set_dir(PIN_HSYNC_N, GPIO_OUT); gpio_put(PIN_HSYNC_N, 1);
    gpio_init(PIN_VSYNC_N); gpio_set_dir(PIN_VSYNC_N, GPIO_OUT); gpio_put(PIN_VSYNC_N, 1);
}
