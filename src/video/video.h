#pragma once
#include <stdint.h>
#include "hardware/structs/sio.h"
#include "hardware/address_mapped.h"
#include "pinmap.h"
#include "colour_lut.h"

void video_init(void);
[[noreturn]] void video_run(void);

// Set by Core 0 each pixel clock: true during ULA fetch window (phases 8-11,
// active display area). Read by Core 1 to gate CPU contention.
extern volatile bool ula_fetch_window;

// Build a GPIO HI word: yn=4-bit, uo/vo=3-bit.
static inline uint32_t build_video_word(uint8_t yn, uint8_t uo, uint8_t vo) {
    return ((uint32_t)(yn & 0xFu) << (PIN_YN_BASE - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)(uo & 7u)  << (PIN_UO_BASE - VIDEO_GPIO_HI_BASE)) |
           ((uint32_t)(vo & 7u)  << (PIN_VO_BASE - VIDEO_GPIO_HI_BASE));
}

static inline uint32_t build_colour_word(uint8_t lut_idx) {
    return build_video_word(lut_yn(lut_idx), lut_uo(lut_idx), lut_vo(lut_idx));
}

static inline uint32_t build_sync_word(void) {
    return build_video_word(YN_SYNC_TIP, UO_NEUTRAL, VO_NEUTRAL);
}

static inline uint32_t build_black_word(void) {
    return build_video_word(YN_BLACK, UO_NEUTRAL, VO_NEUTRAL);
}

static inline void ula_gpio_put_hi(uint32_t mask, uint32_t value) {
    hw_write_masked(&sio_hw->gpio_hi_out, value, mask);
}
