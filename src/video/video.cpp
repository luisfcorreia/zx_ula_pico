// =============================================================================
// video.cpp — Core 0 raster loop
//
// PIXEL CLOCK SYNCHRONISATION
// ─────────────────────────────
// Core 0 calls pio_sm_get_blocking(pio0, 0) at the top of each iteration.
// This blocks until PIO0 SM0 (sync_gen) pushes a tick word into its RX FIFO,
// which happens exactly once per pixel clock (142 ns at 7 MHz).
// Core 0 is therefore naturally gated to the pixel rate — no busy-wait,
// no timer, no ISR overhead. The SM's FIFO (8 entries with join) absorbs
// any jitter from Core 0 running slightly fast or slow on a given iteration.
//
// PIXEL CLOCK BUDGET: 36 system cycles @ 252 MHz per iteration.
// The loop body has been structured to stay within this budget in the
// common (active display) case. Blanking periods have more headroom.
//
// VidEN_n DELAY
// ─────────────
// The real ULA delays Border_n by 8 pixel clocks to align the shift register
// output with the pre-fetched DRAM data. This is implemented here as an
// 8-element shift register (viden_sr): Border_n is shifted in at bit 7 each
// pixel clock, and bit 0 is the delayed VidEN_n (active-low).
// The shift register is updated unconditionally every pixel clock.
//
// ATTR /CAS BOUNDARY
// ──────────────────
// The attribute /CAS asserted at phase 0xF spans into phase 0x0 of the
// next 16-clock group. The D bus is valid at phase 0x0 and AttrLatch fires
// there. This is handled by checking phase==TURNAROUND and Border_n (which
// was still set during the previous group's active display).
// A one-bit flag (attr_cas_pending) bridges the group boundary.
//
// DRAM DATA CAPTURE
// ─────────────────
// PIO1 SM0 fires IRQ4 during /CAS hold. PIO1 SM1 waits on IRQ4, then
// fires PIO1 IRQ0 → CPU IRQ (Core 0). The IRQ handler samples the D bus
// and stores into dram_bitmap_reg or dram_attr_reg (see dram.cpp).
// video.cpp reads those volatile registers at the appropriate phase.
// =============================================================================

#include "video.h"
#include "dram/dram.h"
#include "pinmap.h"
#include "colour_lut.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

extern volatile uint8_t border_colour;
extern volatile uint8_t flash_cnt;

// PIO0 SM indices
#define SM_SYNC    0
#define SM_PIXEL   1
#define SM_CPUCLK  3

// ---- Fast video output write --------------------------------------------
// Single gpio_put_masked() covers all video outputs in ~3 system cycles.
// yn: 4-bit /Y luma+sync DAC    uo: 1-bit Cb sign    vo: 1-bit Cr sign
// r,g,b,bright: RGBi TTL        hsync_n,vsync_n: active-low sync outputs
static inline void __attribute__((always_inline))
write_video_outputs(uint8_t yn, uint8_t uo, uint8_t vo,
                    bool r, bool g, bool b, bool bright,
                    bool hsync_n, bool vsync_n)
{
    const uint32_t mask =
        (0xFu << PIN_YN_BASE) |
        (1u   << PIN_UO)      |
        (1u   << PIN_VO)      |
        (1u   << PIN_R)       |
        (1u   << PIN_G)       |
        (1u   << PIN_B)       |
        (1u   << PIN_BRIGHT)  |
        (1u   << PIN_HSYNC_N) |
        (1u   << PIN_VSYNC_N);

    const uint32_t bits =
        ((uint32_t)(yn & 0xF) << PIN_YN_BASE) |
        ((uint32_t)(uo & 1u)  << PIN_UO)      |
        ((uint32_t)(vo & 1u)  << PIN_VO)      |
        ((uint32_t)r          << PIN_R)        |
        ((uint32_t)g          << PIN_G)        |
        ((uint32_t)b          << PIN_B)        |
        ((uint32_t)bright     << PIN_BRIGHT)   |
        ((uint32_t)hsync_n    << PIN_HSYNC_N)  |
        ((uint32_t)vsync_n    << PIN_VSYNC_N);

    gpio_put_masked(mask, bits);
}

// ---- Display address computation ----------------------------------------
static inline uint16_t bitmap_addr(uint16_t vc, uint16_t hc) {
    // { 0, vc[7:6], vc[2:0], vc[5:3], hc[7:3] }
    return (uint16_t)(
        ((vc & 0xC0u) << 5) |
        ((vc & 0x07u) << 8) |
        ((vc & 0x38u) << 2) |
        ((hc >> 3) & 0x1Fu)
    );
}

static inline uint16_t attr_addr(uint16_t vc, uint16_t hc) {
    // 0x1800 + (vc>>3)*32 + (hc>>3)
    return (uint16_t)(0x1800u | ((vc >> 3) << 5) | ((hc >> 3) & 0x1Fu));
}

// ---- Raster loop --------------------------------------------------------
[[noreturn]] void video_raster_loop(void) {
    uint16_t hc = 0;
    uint16_t vc = 0;

    // Pixel pipeline state
    uint8_t BitmapReg     = 0;  // raw byte captured from DRAM by IRQ handler
    uint8_t SRegister     = 0;  // 8-bit pixel shift register
    uint8_t AttrReg       = 0;  // raw attribute byte from DRAM
    uint8_t AttrOut       = 0;  // latched visible attribute (or synthetic border)

    // VidEN_n: 8-bit shift register implementing the 8-clock pipeline delay.
    // Bit 7 = Border_n this clock; bit 0 = Border_n 8 clocks ago = VidEN_n source.
    // VidEN_n (active-low) = !(viden_sr & 1)  i.e. pipeline is active when bit0=1.
    uint8_t viden_sr      = 0;

    // Raster counter snapshots for stable DRAM addressing
    uint16_t snap_hc = 0;
    uint16_t snap_vc = 0;

    // Flag: attr /CAS was asserted at phase 0xF; D bus valid at next phase 0x0
    bool attr_cas_pending = false;

    while (true) {
        // ==================================================================
        // PIXEL CLOCK SYNC — blocks until SM0 pushes a tick
        // This is the only blocking call; everything else must complete before
        // the next tick arrives (i.e. within ~36 system cycles).
        // ==================================================================
        (void)pio_sm_get_blocking(pio0, SM_SYNC);

        // ==================================================================
        // RASTER POSITION
        // ==================================================================
        const uint8_t  phase    = (uint8_t)(hc & 0xFu);
        const bool     border_n = !((vc >= 192u && vc < 256u) || vc >= 256u || hc >= 256u);
        const bool     hblank_n = !(hc >= HBLANK_START && hc <= HBLANK_END);
        const bool     vblank_n = !(vc >= VBLANK_START && vc <= VBLANK_END);
        const bool     hsync_n  = !(hc >= HSYNC_START  && hc <= HSYNC_END);
        const bool     vsync_n  = !(vc >= VSYNC_START  && vc <= VSYNC_END);
        const bool     csync_n  = hsync_n & vsync_n;
        const bool     active   = hblank_n & vblank_n;

        // VidEN_n: shift Border_n through 8-bit shift register
        viden_sr = (viden_sr >> 1) | (border_n ? 0x80u : 0x00u);
        const bool viden = (viden_sr & 1u);  // true = display pipeline active

        // /INT: vc=248, hc 0..31
        const bool int_n = !(vc == 248u && hc <= INT_HC_END);

        // ==================================================================
        // COUNTER SNAPSHOTS for stable DRAM addressing
        // Taken at phases 0x7 (before pixel fetch) and 0xB (before attr fetch)
        // ==================================================================
        if (border_n && (phase == PHASE_CPU_PRE || phase == PHASE_PIX_DATA)) {
            snap_hc = hc;
            snap_vc = vc;
        }

        // ==================================================================
        // ATTR /CAS BOUNDARY — capture attr data at phase 0x0
        // The attr /CAS asserted at phase 0xF spans into the next group's 0x0.
        // attr_cas_pending is set at 0xF and cleared after capture at 0x0.
        // ==================================================================
        if (attr_cas_pending && phase == PHASE_TURNAROUND) {
            // D bus has attribute byte (DRAM /CAS still held from phase 0xF)
            // IRQ handler will have fired — read the volatile result
            AttrReg = dram_attr_reg;
            attr_cas_pending = false;
        }

        // ==================================================================
        // DRAM COMMAND DISPATCH
        // ==================================================================
        if (border_n) {
            switch (phase) {
                case PHASE_CPU_RAS:
                    // CPU display-RAM access cycle: let PIO1 generate RAS/CAS
                    dram_fetch_phase = 0;  // not a display fetch
                    dram_cmd_cpu(!gpio_get(PIN_WR_N) & !gpio_get(PIN_MREQ_N)
                                 & !gpio_get(PIN_A15) & gpio_get(PIN_A14));
                    break;

                case PHASE_PIX_RAS: {
                    // ULA pixel fetch: drive RA with pixel address row
                    uint16_t addr = bitmap_addr(snap_vc, snap_hc);
                    dram_fetch_phase = 0;  // 0 = store into BitmapReg
                    dram_cmd_ula_read((uint8_t)(addr >> 7), (uint8_t)(addr & 0x7Fu));
                    break;
                }

                case PHASE_ATTR_RAS: {
                    // ULA attribute fetch: drive RA with attr address row
                    uint16_t addr = attr_addr(snap_vc, snap_hc);
                    dram_fetch_phase = 1;  // 1 = store into AttrReg
                    dram_cmd_ula_read((uint8_t)(addr >> 7), (uint8_t)(addr & 0x7Fu));
                    attr_cas_pending = true;  // /CAS will span into phase 0x0
                    break;
                }

                default: break;
            }
        } else if (phase == PHASE_PIX_RAS) {
            // Blanking region: RAS-only refresh using hc[6:0] as row address
            dram_cmd_refresh((uint8_t)(hc & 0x7Fu));
        }

        // ==================================================================
        // PIXEL PIPELINE UPDATE
        // ==================================================================

        // DataLatch: at phase 0xB the IRQ handler has captured BitmapReg
        if (border_n && phase == PHASE_PIX_DATA) {
            BitmapReg = dram_bitmap_reg;
        }

        // SLoad: parallel-load shift register at phase 0x4 when pipeline active
        if (viden && phase == PHASE_CPU_CAS) {
            SRegister = BitmapReg;
        } else {
            SRegister <<= 1;  // shift left; MSB exits as current pixel
        }

        // AOLatch: latch visible attribute at phase 0x5
        if (phase == PHASE_CPU_DATA) {
            if (viden) {
                AttrOut = AttrReg;
            } else {
                // Border: synthesise attribute so video logic needs no special case
                // {FLASH=0, BRIGHT=0, PAPER=BorderColor, INK=BorderColor}
                AttrOut = (uint8_t)((border_colour << 3) | border_colour);
            }
        }

        // ==================================================================
        // COLOUR SELECTION AND VIDEO OUTPUT
        // ==================================================================
        bool pixel = (SRegister & 0x80u) != 0;
        if ((AttrOut & 0x80u) && (flash_cnt & 0x10u)) pixel = !pixel;

        // Extract {BRIGHT,G,R,B} colour index — ZX Spectrum GRB attribute order:
        //   AttrOut[6]=BRIGHT, [5]=Gpaper, [4]=Rpaper, [3]=Bpaper
        //                      [2]=Gink,   [1]=Rink,   [0]=Bink
        const uint8_t col_idx = pixel
            ? (uint8_t)(((AttrOut & 0x40u) >> 3) | (AttrOut & 0x07u))   // ink
            : (uint8_t)(((AttrOut & 0x40u) >> 3) | ((AttrOut >> 3) & 0x07u)); // paper

        if (!csync_n) {
            // Sync tip: /Y = 15, U/V = neutral, RGB = 0
            write_video_outputs(YN_SYNC_TIP, UO_NEUTRAL, VO_NEUTRAL, false, false, false, false, hsync_n, vsync_n);
        } else if (!active) {
            // Blanking pedestal
            write_video_outputs(YN_BLACK, UO_NEUTRAL, VO_NEUTRAL, false, false, false, false, hsync_n, vsync_n);
        } else {
            // Active picture
            const uint8_t yn = lut_yn(col_idx);
            const uint8_t uo = lut_uo(col_idx);
            const uint8_t vo = lut_vo(col_idx);
            // RGBi from colour index (GRB order: bit2=G, bit1=R, bit0=B)
            const bool r = (col_idx & 0x2u) != 0;
            const bool g = (col_idx & 0x4u) != 0;
            const bool b = (col_idx & 0x1u) != 0;
            const bool brt = (col_idx & 0x8u) != 0;  // BRIGHT bit
            write_video_outputs(yn, uo, vo, r, g, b, brt, hsync_n, vsync_n);
        }

        // ==================================================================
        // CONTROL OUTPUTS
        // ==================================================================

        // /INT and /ROM_CS via direct GPIO (fast single-pin writes)
        gpio_put(PIN_INT_N,    int_n ? 1 : 0);
        gpio_put(PIN_ROM_CS_N, gpio_get(PIN_A15) | gpio_get(PIN_A14) | gpio_get(PIN_MREQ_N));

        // Push /INT command to SM0 TX FIFO (SM0 reads it next pixel clock)
        pio_sm_put(pio0, SM_SYNC, int_n ? (1u << 31) : 0u);

        // CPU clock contention:
        //   contend = (mem OR io) AND display_phase AND border_n
        //   display_phase = phases 8–11 of each group = hc[3] & ~hc[2]
        const bool mem_contend  = !gpio_get(PIN_MREQ_N) & !gpio_get(PIN_A15) & gpio_get(PIN_A14);
        const bool io_contend   = !gpio_get(PIN_IO_ULA_N);
        const bool disp_phase   = (hc & 0x8u) && !(hc & 0x4u);
        const bool contend      = (mem_contend | io_contend) & disp_phase & border_n;
        // Push to SM3 (cpu_clock): 0=free, 1=hold high
        pio_sm_put(pio0, SM_CPUCLK, contend ? 1u : 0u);

        // ==================================================================
        // ADVANCE RASTER COUNTERS
        // ==================================================================
        if (++hc > HC_MAX) {
            hc = 0;
            if (++vc > VC_MAX) vc = 0;
        }
    }
}
