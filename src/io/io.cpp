// =============================================================================
// io.cpp — Core 1: port 0xFE, contention, border colour, sound, keyboard
//
// Contention (from Verilog Section 8):
//   wire mem_contend = ~mreq_n & ~a15 & a14;
//   wire io_contend  = ~io_ula_n;
//   wire display_phase = hc[3] & ~hc[2];    // phases 8-11
//   wire contend = (mem_contend | io_contend) & display_phase & Border_n;
//
//   If ~contend: CPUClk toggles normally
//   If  contend: CPUClk held HIGH (wait state)
//
// Core 1 reads the ula_fetch_window flag (set by Core 0 in video_run) which
// combines display_phase & Border_n. Core 1 adds the bus signal conditions.
//
// Port 0xFE write updates BorderColor in video_run via shared volatile.
// =============================================================================

#include "io.h"
#include "video/video.h"
#include "cpu/cpu.h"
#include "pinmap.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

// Shared with video_run()
volatile uint8_t shared_border_color = 1;  // GRB blue
volatile uint8_t shared_sound_out    = 0;

static inline uint8_t read_dbus(void) {
    return (uint8_t)((gpio_get_all() >> PIN_D_BASE) & 0xFFu);
}

static inline void drive_dbus(uint8_t val) {
    uint32_t mask = 0xFFu << PIN_D_BASE;
    gpio_set_dir_masked(mask, mask);
    gpio_put_masked(mask, (uint32_t)val << PIN_D_BASE);
}

static inline void release_dbus(void) {
    gpio_set_dir_masked(0xFFu << PIN_D_BASE, 0u);
}

static uint8_t port_fe_read(void) {
    uint8_t ear = gpio_get(PIN_SOUND) ? 1u : 0u;
    uint8_t t   = (uint8_t)((gpio_get_all() >> PIN_T_BASE) & 0x1Fu);
    return 0xA0u | (ear << 6) | t;
}

static void port_fe_write(uint8_t d) {
    shared_sound_out    = ((d >> 4) & 1u) | ((d >> 3) & 1u);
    shared_border_color = d & 0x07u;
    if (shared_sound_out) {
        gpio_set_dir(PIN_SOUND, GPIO_OUT);
        gpio_put(PIN_SOUND, 1);
    } else {
        gpio_set_dir(PIN_SOUND, GPIO_IN);
    }
}

[[noreturn]] void io_core1_entry(void) {
    bool clock_held = false;

    while (true) {
        uint32_t g = gpio_get_all();
        bool mreq_n  = (g >> PIN_MREQ_N)   & 1u;
        bool a14     = (g >> PIN_A14)       & 1u;
        bool a15     = (g >> PIN_A15)       & 1u;
        bool io_ula_n = (g >> PIN_IO_ULA_N) & 1u;

        // Contention: (mem_contend | io_contend) & display_phase & Border_n
        // ula_fetch_window = display_phase & Border_n (set by Core 0)
        bool mem_contend = !mreq_n && !a15 && a14;
        bool io_contend  = !io_ula_n;
        bool contend     = ula_fetch_window && (mem_contend || io_contend);

        if (contend) {
            if (!clock_held) {
                // Wait until clock is HIGH then freeze SM3 (hold HIGH = wait state)
                while (!gpio_get(PIN_CLOCK)) tight_loop_contents();
                pio_sm_set_enabled(pio0, SM_CPUCLK, false);
                clock_held = true;
            }
        } else {
            if (clock_held) {
                pio_sm_set_enabled(pio0, SM_CPUCLK, true);
                clock_held = false;
            }
        }

        // Port 0xFE I/O
        if (!io_ula_n) {
            if (!((g >> PIN_RD_N) & 1u)) {
                drive_dbus(port_fe_read());
                while (!gpio_get(PIN_RD_N)) tight_loop_contents();
                release_dbus();
            } else if (!((g >> PIN_WR_N) & 1u)) {
                port_fe_write(read_dbus());
                while (!gpio_get(PIN_WR_N)) tight_loop_contents();
            }
        }
    }
}
