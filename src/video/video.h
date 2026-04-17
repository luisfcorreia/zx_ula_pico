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

// Build a GPIO HI word: yn=4-bit, uo/vo=3-bit, r/g/b/i 1-bit, csync 1-bit.
// gpio_hi_out layout: bit1=Y, bit5=U, bit8=V, bit11=R, bit12=G, bit13=B, bit14=I, bit15=CSYNC
static inline uint32_t build_video_word(uint8_t yn, uint8_t uo, uint8_t vo,
                                        uint8_t r, uint8_t g, uint8_t b,
                                        uint8_t i, uint8_t csync) {
    return ((uint32_t)(yn & 0xFu)   <<  1) |
           ((uint32_t)(uo & 7u)     <<  5) |
           ((uint32_t)(vo & 7u)     <<  8) |
           ((uint32_t)(r & 1u)      << 11) |
           ((uint32_t)(g & 1u)      << 12) |
           ((uint32_t)(b & 1u)      << 13) |
           ((uint32_t)(i & 1u)      << 14) |
           ((uint32_t)(csync & 1u)  << 15);
}

static inline uint32_t build_colour_word(uint8_t lut_idx) {
    return build_video_word(lut_yn(lut_idx), lut_uo(lut_idx), lut_vo(lut_idx),
                          0, 0, 0, 0, 1u);
}

static inline uint32_t build_sync_word(void) {
    return build_video_word(YN_SYNC_TIP, UO_NEUTRAL, VO_NEUTRAL, 0, 0, 0, 0, 0u);
}

static inline uint32_t build_black_word(void) {
    return build_video_word(YN_BLACK, UO_NEUTRAL, VO_NEUTRAL, 0, 0, 0, 0, 1u);
}

static inline void ula_gpio_put_hi(uint32_t mask, uint32_t value) {
    hw_write_masked(&sio_hw->gpio_hi_out, value, mask);
}
