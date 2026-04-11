// =============================================================================
// main.cpp — ZX Spectrum RP2350B ULA bootstrap
//
// Startup sequence:
//   1. 252 MHz PLL
//   2. GPIO directions and safe initial levels
//   3. Load PIO programs (video, cpu_clock, dram) — not yet running
//   4. Start all PIO0 SMs on the same cycle (phase alignment)
//   5. Launch Core 1 (I/O handler)
//   6. Initialise DMA channels (starts first scanline automatically)
//   7. Core 0 enters scanline preparation loop — never returns
//
// DMA ARCHITECTURE CHANGE (vs original pio_sm_put/get loop):
//   The original video_raster_loop() gated Core 0 per-pixel-clock via
//   pio_sm_get_blocking() and pushed /INT, contention and DRAM command words
//   into PIO FIFOs manually every 36 system cycles.
//
//   Now:
//     - sync_gen SM (SM0):  DMA feeds precomputed /INT bitstream
//     - pixel_shift SM (SM1): DMA feeds precomputed pixel bitstream
//     - cpu_clock SM (SM3): DMA feeds precomputed clock/contention bitstream
//     - dram_ctrl SM (PIO1 SM0): DMA feeds precomputed command words
//
//   Core 0 is freed from per-pixel FIFO work. Its main loop:
//     a) Waits for DMA completion IRQ (end of previous scanline)
//     b) Outputs precomputed colour values to GPIO for the current scanline
//        (~36 system cycles per pixel × 456 pixels ≈ one scanline period)
//     c) Prepares DMA buffers for the next-next scanline
// =============================================================================

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include <initializer_list>

#include "pinmap.h"
#include "colour_lut.h"
#include "clock/clock.h"
#include "video/video.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "io/io.h"
#include "ula/ula_dma.h"

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
    // NOTE: PIN_INT_N and PIN_CLOCK are now OUT pins driven by PIO (SM0 and SM3).
    // gpio_init here sets safe initial levels; pio_gpio_init() in video_init /
    // cpu_init transfers ownership to PIO before the SMs start.
    gpio_init(PIN_INT_N);    gpio_set_dir(PIN_INT_N,    GPIO_OUT); gpio_put(PIN_INT_N,    1);
    gpio_init(PIN_CLOCK);    gpio_set_dir(PIN_CLOCK,    GPIO_OUT); gpio_put(PIN_CLOCK,    0);
    gpio_init(PIN_ROM_CS_N); gpio_set_dir(PIN_ROM_CS_N, GPIO_OUT); gpio_put(PIN_ROM_CS_N, 1);

    // Sound / keyboard
    gpio_init(PIN_SOUND); gpio_set_dir(PIN_SOUND, GPIO_IN);
    for (int i = PIN_T_BASE; i < PIN_T_BASE + PIN_T_COUNT; i++) {
        gpio_init(i); gpio_set_dir(i, GPIO_IN); gpio_pull_up(i);
    }

    // YUV DAC pins — outputs (Core 0 drives via gpio_hi_put_masked each scanline)
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
    for (int p : {PIN_R, PIN_G, PIN_B, PIN_BRIGHT}) {
        gpio_init(p); gpio_set_dir(p, GPIO_OUT); gpio_put(p, 0);
    }

    // Sync outputs (active-low, start deasserted)
    gpio_init(PIN_HSYNC_N); gpio_set_dir(PIN_HSYNC_N, GPIO_OUT); gpio_put(PIN_HSYNC_N, 1);
    gpio_init(PIN_VSYNC_N); gpio_set_dir(PIN_VSYNC_N, GPIO_OUT); gpio_put(PIN_VSYNC_N, 1);

    // 3. Load PIO programs — all SMs configured but not yet running
    video_init();   // PIO0 SM0 (sync), SM1 (pixel); GPIO YUV set to black/neutral
    cpu_init();     // PIO0 SM3 (cpu_clock); configured, SM stopped
    dram_init();    // PIO1 SM0/SM1/SM2 loaded, configured, AND started

    // 4. Start all PIO0 SMs on the same system clock edge
    //    SM0 = sync, SM1 = pixel, SM3 = cpu_clock  (SM2 spare)
    pio_enable_sm_mask_in_sync(pio0, (1u << 0) | (1u << 1) | (1u << 3));

    // 5. Launch Core 1 (I/O handler — unchanged)
    multicore_launch_core1(io_core1_entry);

    // 6. Initialise DMA channels and start first scanline
    //    Precomputes scanline buffers 0 and 1, installs DMA_IRQ_0 handler,
    //    then kicks off DMA for scanline 0.
    ula_dma_init();

    // 7. Core 0 scanline loop — never returns
    //
    //    Each iteration handles one scanline:
    //      a) Wait for the DMA IRQ to signal that the current scanline's DMA
    //         has started (set by ula_dma_irq_handler).
    //      b) Write precomputed colour values to GPIO hi-word for each pixel
    //         clock. Each gpio_hi_put_masked takes ~36 system cycles, matching
    //         the 7 MHz pixel clock rate (252/7 = 36 cycles/pixel).
    //      c) Prepare DMA buffers for the next-next scanline (the IRQ already
    //         prepared next scanline's buffer before signalling us).
    //
    //    Core 0 stays busy for almost exactly one scanline period in step (b),
    //    so (c) runs during the early portion of the following scanline while
    //    DMA and PIO handle timing signals autonomously.
    video_scanline_loop();  // defined in video.cpp — never returns
}
