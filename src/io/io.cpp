// =============================================================================
// io.cpp — Core 1: port 0xFE decode, keyboard, sound, ROM_CS, /WE, contention
//
// Core 1 drives three combinatorial signals continuously:
//
//   PIN_ROM_CS_N = A14 | A15
//     ROM selected (active low) when both A14=0 and A15=0 (0x0000-0x3FFF).
//     Purely address-driven, no MREQ qualification needed for ROM CS.
//
//   PIN_WE_N = !(MREQ=1 & A15=0 & A14=1 & WR=1)
//     DRAM write enable: asserted when CPU writes to contended RAM (0x4000-0x7FFF).
//     Independent of ULA video fetch cycle.
//     Natural Core 1 loop latency provides ~30 ns propagation delay.
//
//   Contention (SM3 pause/resume):
//     When ULA is fetching AND CPU accesses 0x4000-0x7FFF,
//     hold CPU clock HIGH (wait state) by disabling PIO0 SM3.
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

[[noreturn]] void io_core1_entry(void) {
    bool clock_held = false;

    while (true) {
        bool mreq = !gpio_get(PIN_MREQ_N);
        bool a15  =  gpio_get(PIN_A15);
        bool a14  =  gpio_get(PIN_A14);
        bool wr   = !gpio_get(PIN_WR_N);

        // ---- ROM chip select: A14 OR A15 (deasserts ROM when either is high) ----
        gpio_put(PIN_ROM_CS_N, a14 | a15);

        // ---- DRAM /WE: assert when CPU writes to 0x4000-0x7FFF ----
        // Active low: assert when MREQ=1, A15=0, A14=1, WR=1
        // Natural loop latency provides ~30 ns propagation delay.
        bool cpu_write_contended = mreq & !a15 & a14 & wr;
        gpio_put(PIN_WE_N, !cpu_write_contended);

        // ---- CPU clock contention ----
        // Hold clock HIGH (wait state) when ULA is fetching AND
        // CPU is accessing contended RAM (0x4000-0x7FFF)
        bool contend = ula_fetch_active & mreq & !a15 & a14;
        if (contend) {
            if (!clock_held) {
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

        // ---- Port 0xFE I/O ----
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
