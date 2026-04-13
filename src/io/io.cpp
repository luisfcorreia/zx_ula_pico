// =============================================================================
// io.cpp — Core 1: port 0xFE decode, keyboard, sound, CPU clock contention
//
// Contention logic:
//   The ZX Spectrum ULA holds the CPU clock HIGH (inserting wait states) when
//   ALL of the following are true simultaneously:
//     1. ULA is fetching VRAM to draw the current scanline (ula_fetch_active)
//     2. CPU has MREQ_N asserted (accessing memory)
//     3. A15=0, A14=1 (address is in 0x4000-0x7FFF — contended RAM)
//
//   Implementation:
//     Core 1 polls the above condition every iteration.
//     When contention is needed:
//       - Spin until PIN_CLOCK reads HIGH (so we pause during the HIGH phase)
//       - Disable SM3 (pio_sm_set_enabled false) — clock frozen HIGH = wait state
//     When contention condition clears:
//       - Re-enable SM3 — clock resumes from where it stopped
// =============================================================================

#include "io.h"
#include "cpu/cpu.h"
#include "ula/ula_dma.h"
#include "pinmap.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;
extern volatile uint8_t sound_out;

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

static uint8_t port_fe_read(void) {
    uint8_t ear = gpio_get(PIN_SOUND) ? 1 : 0;
    uint8_t t   = (uint8_t)((gpio_get_all() >> PIN_T_BASE) & 0x1F);
    return 0xA0u | (ear << 6) | t;
}

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

// Returns true if CPU is accessing contended RAM right now
static inline bool cpu_contending(void) {
    return ula_fetch_active
        && !gpio_get(PIN_MREQ_N)
        && !gpio_get(PIN_A15)
        &&  gpio_get(PIN_A14);
}

[[noreturn]] void io_core1_entry(void) {
    bool clock_held = false;

    while (true) {
        // ----------------------------------------------------------------
        // Contention: hold CPU clock HIGH when CPU and ULA both want VRAM
        // ----------------------------------------------------------------
        if (cpu_contending()) {
            if (!clock_held) {
                // Wait until clock is HIGH so we pause during the HIGH phase,
                // giving the CPU a stretched HIGH = valid wait state.
                // Core 1 runs at ~250 MHz; the 7 MHz clock gives ~35 cycles
                // per half-period so we will catch the HIGH in time.
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

        // ----------------------------------------------------------------
        // Port 0xFE I/O
        // ----------------------------------------------------------------
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
    }
}
