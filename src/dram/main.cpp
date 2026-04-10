// =============================================================================
// main.cpp — ZX Spectrum RP2350B ULA bootstrap
//
// Startup sequence:
//   1. 252 MHz PLL
//   2. GPIO directions and safe initial levels
//   3. Load PIO programs (video, cpu_clock, dram) — not yet running
//   4. Start all PIO0 SMs on the same cycle (phase alignment)
//   5. Launch Core 1 (I/O handler)
//   6. Core 0 enters video_raster_loop() — never returns
// =============================================================================

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "pinmap.h"
#include "colour_lut.h"
#include "clock/clock.h"
#include "video/video.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "io/io.h"

// Shared state — single-writer convention (see individual module comments)
volatile uint8_t border_colour  = 0b100;  // GRB blue (cold-boot default)
volatile uint8_t flash_cnt      = 0;
volatile uint8_t sound_out      = 0;

int main(void) {
    // 1. 252 MHz system clock
    clock_init_252mhz();

    // 2. GPIO — set directions and safe initial levels before PIO takes over

    // DRAM address + control — outputs, deasserted
    for (int i = PIN_RA_BASE; i < PIN_RA_BASE + PIN_RA_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_OUT); gpio_put(i, 0);
    }
    gpio_init(PIN_RAS_N); gpio_set_dir(PIN_RAS_N, GPIO_OUT); gpio_put(PIN_RAS_N, 1);
    gpio_init(PIN_CAS_N); gpio_set_dir(PIN_CAS_N, GPIO_OUT); gpio_put(PIN_CAS_N, 1);
    gpio_init(PIN_WE_N);  gpio_set_dir(PIN_WE_N,  GPIO_OUT); gpio_put(PIN_WE_N,  1);

    // Data bus — inputs with pull-up (high-Z between cycles)
    for (int i = PIN_D_BASE; i < PIN_D_BASE + PIN_D_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }

    // Z80 control — inputs
    for (int p : {PIN_WR_N, PIN_RD_N, PIN_MREQ_N, PIN_IO_ULA_N, PIN_A14, PIN_A15}) {
        gpio_init(p); gpio_set_dir(p, GPIO_IN);
    }

    // Z80 control — outputs
    gpio_init(PIN_INT_N);    gpio_set_dir(PIN_INT_N,    GPIO_OUT); gpio_put(PIN_INT_N,    1);
    gpio_init(PIN_CLOCK);    gpio_set_dir(PIN_CLOCK,    GPIO_OUT); gpio_put(PIN_CLOCK,    0);
    gpio_init(PIN_ROM_CS_N); gpio_set_dir(PIN_ROM_CS_N, GPIO_OUT); gpio_put(PIN_ROM_CS_N, 1);

    // Sound / keyboard
    gpio_init(PIN_SOUND); gpio_set_dir(PIN_SOUND, GPIO_IN);
    for (int i = PIN_T_BASE; i < PIN_T_BASE + PIN_T_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }

    // YUV DAC pins — outputs
    for (int i = PIN_YN_BASE; i < PIN_YN_BASE + PIN_YN_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_OUT);
    }
    for (int i = PIN_UO_BASE; i < PIN_UO_BASE + PIN_UO_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_OUT);
    }
    for (int i = PIN_VO_BASE; i < PIN_VO_BASE + PIN_VO_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_OUT);
    }

    // RGBi bonus outputs
    for (int p : {PIN_R, PIN_G, PIN_B}) {
        gpio_init(p); gpio_set_dir(p, GPIO_OUT); gpio_put(p, 0);
    }

    // 3. Load PIO programs — all SMs configured but not yet running
    video_init();   // PIO0 SM0 (sync), SM1 (pixel); GPIO YUV set to black/neutral
    cpu_init();     // PIO0 SM3 (cpu_clock); FIFO pre-filled, SM stopped
    dram_init();    // PIO1 SM0/SM1/SM2 loaded, configured, AND started
                    // (PIO1 can start independently — it waits for FIFO commands)

    // 4. Start all PIO0 SMs on the same system clock edge
    //    SM0 = sync, SM1 = pixel, SM3 = cpu_clock  (SM2 = plain GPIO, no SM)
    pio_enable_sm_mask_in_sync(pio0, (1u << 0) | (1u << 1) | (1u << 3));

    // 5. Launch Core 1
    multicore_launch_core1(io_core1_entry);

    // 6. Core 0 raster loop — never returns
    video_raster_loop();
}
