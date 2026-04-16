// =============================================================================
// video_init.cpp — PIO0 SM0 tick generator + colour table precompute
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "colour_lut.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define SM_SYNC 0

// Precomputed GPIO HI word tables (computed once at startup)
// colour_table[16]: indexed by {BRIGHT[3], G[2], R[1], B[0]}
// border_table[8]:  indexed by border GRB colour (no BRIGHT)
uint32_t colour_table[16];
uint32_t border_table[8];
uint32_t sync_tip_words[4];   // [0]=HSync=1 VSync=1, [1]=HSync=0, [2]=VSync=0, [3]=both
uint32_t black_word;

static uint32_t build_gpio_hi(uint8_t lut_idx, bool hsync_n, bool vsync_n) {
    return ((uint32_t)lut_yn(lut_idx)      << (PIN_YN_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)lut_uo(lut_idx)      << (PIN_UO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)lut_vo(lut_idx)      << (PIN_VO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 1) & 1) << (PIN_R        - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 2) & 1) << (PIN_G        - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 0) & 1) << (PIN_B        - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)((lut_idx >> 3) & 1) << (PIN_BRIGHT   - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)hsync_n              << (PIN_HSYNC_N  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)vsync_n              << (PIN_VSYNC_N  - VIDEO_GPIO_HI_BASE));
}

static uint32_t build_sync_hi(bool hsync_n, bool vsync_n) {
    return ((uint32_t)YN_SYNC_TIP << (PIN_YN_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)UO_NEUTRAL  << (PIN_UO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)VO_NEUTRAL  << (PIN_VO_BASE  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)hsync_n     << (PIN_HSYNC_N  - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)vsync_n     << (PIN_VSYNC_N  - VIDEO_GPIO_HI_BASE));
}

void video_init(void) {
    // ---- Precompute colour tables ----------------------------------------
    for (int i = 0; i < 16; i++)
        colour_table[i] = build_gpio_hi((uint8_t)i, true, true);

    for (int i = 0; i < 8; i++)
        border_table[i] = build_gpio_hi((uint8_t)i, true, true);

    // Sync tip words: index = (HSync_deasserted ? 0 : 1) | (VSync_deasserted ? 0 : 2)
    // bit0=0: HSync asserted, bit1=0: VSync asserted
    sync_tip_words[0] = build_sync_hi(true,  true);   // no sync (shouldn't occur)
    sync_tip_words[1] = build_sync_hi(false, true);   // HSync only
    sync_tip_words[2] = build_sync_hi(true,  false);  // VSync only
    sync_tip_words[3] = build_sync_hi(false, false);  // both

    black_word = build_gpio_hi(0, true, true);  // YN=11, no syncs

}
