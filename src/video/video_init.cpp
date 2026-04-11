// =============================================================================
// video_init.cpp — PIO0 SM0/SM1/SM3 setup for video output
//
// SM0: sync.pio   — /INT bitstream output at 7 MHz (DMA-fed, OUT pin)
// SM1: pixel.pio  — pixel bitstream output at 7 MHz (DMA-fed, OUT pin)
// SM2: (spare)
// SM3: cpu_clock  — configured in cpu.cpp
//
// DMA CHANGE: sync SM0 previously used side-set for /INT and pushed RX FIFO
// tick words that Core 0 blocked on. Now:
//   - /INT is an OUT pin (not side-set). PIN_INT_N driven by `out pins, 1`.
//   - No RX FIFO output. Frame synchronisation is via DMA completion IRQ.
//   - Autopull threshold = 32. DMA feeds precomputed int_n bitstream.
//
// All SMs are configured here but NOT started. main.cpp starts all PIO0
// SMs together with pio_enable_sm_mask_in_sync() so they are phase-aligned.
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
    // ---- SM0: sync generator (/INT bitstream, DMA-fed) ------------------
    uint offset_sync = pio_add_program(pio0, &sync_gen_program);
    pio_sm_config cs = sync_gen_program_get_default_config(offset_sync);

    // /INT is now an OUT pin — NOT side-set (side-set removed from sync.pio).
    // DMA feeds precomputed int_n bits; SM shifts one bit per pixel clock.
    sm_config_set_out_pins(&cs, PIN_INT_N, 1);

    // Shift left, autopull enabled, threshold = 32 bits.
    // One 32-bit word from DMA covers 32 consecutive pixel clocks.
    sm_config_set_out_shift(&cs, false, true, 32);

    // Join both FIFOs into TX for deeper DMA buffering (8 words = 256 pixels).
    sm_config_set_fifo_join(&cs, PIO_FIFO_JOIN_TX);

    sm_config_set_clkdiv_int_frac(&cs, PIO0_CLK_DIV, 0);

    pio_sm_init(pio0, SM_SYNC, offset_sync, &cs);
    pio_gpio_init(pio0, PIN_INT_N);
    pio_sm_set_consecutive_pindirs(pio0, SM_SYNC, PIN_INT_N, 1, true);
    gpio_put(PIN_INT_N, 1);  // deasserted (active-low) until DMA starts

    // ---- SM1: pixel shift register (pixel bitstream, DMA-fed) ----------
    uint offset_pixel = pio_add_program(pio0, &pixel_shift_program);
    pio_sm_config cp = pixel_shift_program_get_default_config(offset_pixel);

    sm_config_set_out_pins(&cp, PIN_R, 1);           // drives R bit only
    sm_config_set_out_shift(&cp, false, true, 32);    // shift left, autopull 32
    sm_config_set_fifo_join(&cp, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&cp, PIO0_CLK_DIV, 0);

    pio_sm_init(pio0, SM_PIXEL, offset_pixel, &cp);
    pio_gpio_init(pio0, PIN_R);
    pio_sm_set_consecutive_pindirs(pio0, SM_PIXEL, PIN_R, 1, true);

    // ---- Video output GPIO: initialise to black/neutral -----------------
    // All video output pins are GP33–GP44, so use gpio_hi registers.
    // gpio_put_masked handles this transparently via the SDK; safe here
    // because PIO has not started yet.
    gpio_put_masked(
        (0xFu << PIN_YN_BASE) | (1u << PIN_UO) | (1u << PIN_VO) |
        (1u << PIN_R) | (1u << PIN_G) | (1u << PIN_B) | (1u << PIN_BRIGHT) |
        (1u << PIN_HSYNC_N) | (1u << PIN_VSYNC_N),
        (11u << PIN_YN_BASE) |                          // YN = black
        (1u << PIN_UO) | (1u << PIN_VO) |              // U/V neutral
        (1u << PIN_HSYNC_N) | (1u << PIN_VSYNC_N)      // sync deasserted
    );

    // SMs left stopped — main.cpp enables all PIO0 SMs together.
}
