// =============================================================================
// io.cpp — Core 1: port 0xFE decode, keyboard, sound, flash counter
// =============================================================================

#include "io.h"
#include "pinmap.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;
extern volatile uint8_t sound_out;

static bool vsync_prev = true;

// VSync detection: both YN MSBs high = sync tip (all 4 bits = 15)
static inline bool vsync_active(void) {
    return gpio_get(PIN_YN_BASE + 3) & gpio_get(PIN_YN_BASE + 2);
}

static inline uint8_t read_dbus(void) {
    return (uint8_t)((gpio_get_all() >> PIN_D_BASE) & 0xFF);
}

static inline void drive_dbus(uint8_t val) {
    uint32_t mask = 0xFFu << PIN_D_BASE;
    gpio_set_dir_masked(mask, mask);
    gpio_put_masked(mask, (uint32_t)val << PIN_D_BASE);
}

static inline void release_dbus(void) {
    gpio_set_dir_masked(0xFFu << PIN_D_BASE, 0);
}

// Port 0xFE read: {1, EAR, 1, T4, T3, T2, T1, T0}
static uint8_t port_fe_read(void) {
    uint8_t ear = gpio_get(PIN_SOUND) ? 1 : 0;
    uint8_t t   = (uint8_t)((gpio_get_all() >> PIN_T_BASE) & 0x1F);
    return 0xA0u | (ear << 6) | t;  // bits 7 and 5 always 1
}

// Port 0xFE write: d[4]=SPK, d[3]=MIC, d[2:0]=border {G,R,B}
static void port_fe_write(uint8_t d) {
    sound_out     = ((d >> 4) & 1) | ((d >> 3) & 1);
    border_colour = d & 0x07;
    if (sound_out) {
        gpio_set_dir(PIN_SOUND, GPIO_OUT);
        gpio_put(PIN_SOUND, 1);
    } else {
        gpio_set_dir(PIN_SOUND, GPIO_IN);
    }
}

[[noreturn]] void io_core1_entry(void) {
    while (true) {
        if (!gpio_get(PIN_IO_ULA_N)) {
            if (!gpio_get(PIN_RD_N)) {
                drive_dbus(port_fe_read());
                while (!gpio_get(PIN_RD_N)) tight_loop_contents();
                release_dbus();
            } else if (!gpio_get(PIN_WR_N)) {
                port_fe_write(read_dbus());
                while (!gpio_get(PIN_WR_N)) tight_loop_contents();
            }
        }
        // Flash counter: increment on VSync falling edge
        bool vs = vsync_active();
        if (vsync_prev && !vs) flash_cnt++;
        vsync_prev = vs;
    }
}
