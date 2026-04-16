// =============================================================================
// video.cpp — Core 0 pixel loop: faithful C simulation of ula_zx48k.v
//
// Every pass through the while loop = one negedge clk7 event (7 MHz).
// The pattern:
//   1. Gate on PIO0 SM0 RX FIFO tick (7 MHz synchronisation)
//   2. OUTPUT current registered state to GPIO (colour, DRAM, /INT)
//   3. READ inputs (D bus, bus control signals)
//   4. COMPUTE all next-state values (Verilog <= assignments)
//   5. UPDATE all C variables to next-state values
//
// This gives cycle-accurate registered behaviour matching the Verilog.
// All signal names match the Verilog module identically.
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "colour_lut.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define SM_SYNC 0

// Declared in video_init.cpp
extern uint32_t colour_table[16];
extern uint32_t border_table[8];
extern uint32_t sync_tip_words[4];
extern uint32_t black_word;

// Shared with Core 1 (io.cpp) for contention gating
volatile bool ula_fetch_window = false;
extern volatile uint8_t shared_border_color;

[[noreturn]] void video_run(void) {

    // =========================================================
    // REGISTERED STATE — all variables here are Verilog registers.
    // Their value is what currently appears on the output pins.
    // =========================================================

    // Section 2: Raster counters
    uint32_t hc = 0, vc = 0;

    // Section 3: Sync / blanking
    bool HBlank_n = true,  HSync_n = true;
    bool VBlank_n = true,  VSync_n = true;
    bool INT_r    = true;

    // Section 4: Display window
    bool Border_n    = true;   // !(vc[7]&vc[6] | vc[8] | hc[8]) at vc=hc=0 → true
    bool VidEN_n     = true;
    bool DataLatch_n = true;
    bool AttrLatch_n = true;
    bool SLoad_r     = false;
    bool AOLatch_n   = true;

    // Section 5: DRAM
    uint8_t  snap_c_hi  = 0, snap_v_hi  = 0;
    uint8_t  snap_v_mid = 0, snap_v_lo  = 0;
    uint16_t pix_addr   = 0, attr_addr_v = 0;
    uint8_t  a_r        = 0;
    bool     ras_r      = true, cas_r = true, we_r = true;

    // Section 6: Pixel pipeline
    uint8_t BitmapReg = 0, SRegister  = 0;
    uint8_t AttrReg   = 0, AttrOut    = 0;
    uint8_t FlashCnt  = 0;
    bool    VSync_prev = true;

    while (true) {

        // ======================================================
        // 1. GATE: wait for 7 MHz tick from PIO0 SM0
        uint8_t BorderColor = shared_border_color;  // read Core 1 border write
        // ======================================================
        (void)pio_sm_get_blocking(pio0, SM_SYNC);

        // ======================================================
        // 2. OUTPUT current registered state
        // ======================================================

        // 2a. Video colour output
        // Combinatorial in Verilog: Pixel, col_*, yn/uo/vo/r/g/b/bright
        bool active_video = HBlank_n && VBlank_n;
        bool sync_active  = !HSync_n || !VSync_n;

        bool flash_pixel = ((AttrOut >> 7) & 1) && ((FlashCnt >> 4) & 1);
        bool Pixel = ((SRegister >> 7) & 1) ^ flash_pixel;

        uint32_t gpio_hi;
        if (sync_active) {
            // Sync tip — index = (HSync_n=0→bit0) | (VSync_n=0→bit1)
            gpio_hi = sync_tip_words[(!HSync_n ? 1 : 0) | (!VSync_n ? 2 : 0)];
        } else if (!active_video) {
            gpio_hi = black_word;  // blanking pedestal, YN=11
        } else if (!VidEN_n) {
            // Active pixel area
            uint8_t ink   = AttrOut & 7;
            uint8_t paper = (AttrOut >> 3) & 7;
            uint8_t col   = Pixel ? ink : paper;
            uint8_t lut   = col | ((AttrOut >> 3) & 8);  // {BRIGHT,G,R,B}
            gpio_hi = colour_table[lut];
        } else {
            // Border
            gpio_hi = border_table[BorderColor & 7];
        }
        ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, gpio_hi);

        // 2b. DRAM control — Core 0 owns GP0-GP9
        {
            uint32_t dw = ((uint32_t)(a_r & 0x7Fu))
                        | (ras_r ? (1u << (PIN_RAS_N - PIN_RA_BASE)) : 0u)
                        | (cas_r ? (1u << (PIN_CAS_N - PIN_RA_BASE)) : 0u)
                        | (we_r  ? (1u << (PIN_WE_N  - PIN_RA_BASE)) : 0u);
            gpio_put_masked(PIN_DRAM_MASK, dw);
        }

        // 2c. /INT
        gpio_put(PIN_INT_N, INT_r ? 1 : 0);

        // 2d. Update contention window flag for Core 1
        // display_phase = hc[3] & ~hc[2] = phases 8,9,10,11
        ula_fetch_window = Border_n && (hc & 8u) && !(hc & 4u);

        // ======================================================
        // 3. READ INPUTS
        // ======================================================
        uint32_t gpio_in = gpio_get_all();
        uint8_t  D       = (uint8_t)((gpio_in >> PIN_D_BASE) & 0xFFu);
        bool     mreq_n  = (gpio_in >> PIN_MREQ_N) & 1u;
        bool     wr_n    = (gpio_in >> PIN_WR_N)   & 1u;
        bool     a14     = (gpio_in >> PIN_A14)     & 1u;
        bool     a15     = (gpio_in >> PIN_A15)     & 1u;

        // ROM_CS: combinatorial, driven directly every pixel clock
        // Verilog: assign rom_cs_n = a15 | a14 | mreq_n
        gpio_put(PIN_ROM_CS_N, (int)(a15 | a14 | mreq_n));

        // ======================================================
        // 4. COMPUTE next-state (Verilog <= assignments)
        // ======================================================

        uint8_t  phase = (uint8_t)(hc & 0xFu);
        bool ph3 = (phase >> 3) & 1u;
        bool ph2 = (phase >> 2) & 1u;
        bool ph1 = (phase >> 1) & 1u;
        bool ph0 =  phase       & 1u;

        // --- Section 2: counters ---
        uint32_t n_hc = (hc == HC_MAX) ? 0u : hc + 1u;
        uint32_t n_vc = vc;
        if (hc == HC_MAX) n_vc = (vc == VC_MAX) ? 0u : vc + 1u;

        // --- Section 3: sync/blanking/INT ---
        // HBlank_n: cyclestart(hc,320)→0, cycleend(hc,415)→1
        bool n_HBlank_n = (hc == 416u) ? true : (hc == 320u) ? false : HBlank_n;
        bool n_HSync_n  = (hc == 376u) ? true : (hc == 344u) ? false : HSync_n;

        // VBlank/VSync: check vc every clock (vc is stable per line)
        bool n_VBlank_n = (vc == 256u) ? true : (vc == 248u) ? false : VBlank_n;
        bool n_VSync_n  = (vc == 252u) ? true : (vc == 248u) ? false : VSync_n;

        // /INT: cyclestart(vc,248)&&cyclestart(hc,0)→0, cycleend(hc,31)→1
        bool n_INT_r = INT_r;
        if (vc == 248u) {
            if (hc == 0u)  n_INT_r = false;
            if (hc == 32u) n_INT_r = true;
        }

        // FlashCnt: VSync falling edge
        uint8_t n_FlashCnt = FlashCnt;
        if (VSync_prev && !VSync_n) n_FlashCnt = FlashCnt + 1u;
        bool n_VSync_prev = VSync_n;

        // --- Section 4: display window ---
        // Border_n: uses CURRENT hc/vc
        bool n_Border_n = !(((vc & 0x80u) && (vc & 0x40u)) || (vc & 0x100u) || (hc & 0x100u));

        // VidEN_n: only updates when current hc[3]=1
        bool n_VidEN_n = VidEN_n;
        if (hc & 8u) n_VidEN_n = !Border_n;

        // Control latches (registered signals gating pipeline)
        bool n_DataLatch_n = !((phase == 0xBu) && Border_n);
        bool n_AttrLatch_n = !((phase == 0x0u) && Border_n);
        bool n_SLoad       =   (phase == 0x4u) && !VidEN_n;
        bool n_AOLatch_n   =  !(phase == 0x5u);

        // --- Section 5: DRAM ---
        // Address snap at phases 7 and 11
        uint8_t  n_snap_c_hi  = snap_c_hi;
        uint8_t  n_snap_v_hi  = snap_v_hi;
        uint8_t  n_snap_v_mid = snap_v_mid;
        uint8_t  n_snap_v_lo  = snap_v_lo;
        uint16_t n_pix_addr   = pix_addr;
        uint16_t n_attr_addr  = attr_addr_v;

        if (Border_n && (phase == 0x7u || phase == 0xBu)) {
            n_snap_c_hi  = (uint8_t)((hc >> 3) & 0x1Fu);
            n_snap_v_hi  = (uint8_t)((vc >> 6) & 0x3u);
            n_snap_v_mid = (uint8_t)((vc >> 3) & 0x7u);
            n_snap_v_lo  = (uint8_t)(vc & 0x7u);
            // pix_addr  = {0, v_hi[1:0], v_lo[2:0], v_mid[2:0], c_hi[4:0]}
            n_pix_addr   = (uint16_t)(((uint16_t)n_snap_v_hi  << 11) |
                                      ((uint16_t)n_snap_v_lo  <<  8) |
                                      ((uint16_t)n_snap_v_mid <<  5) |
                                       (uint16_t)n_snap_c_hi);
            // attr_addr = {4'b0110, v_hi[1:0], v_mid[2:0], c_hi[4:0]}
            n_attr_addr  = (uint16_t)(0x1800u |
                                      ((uint16_t)n_snap_v_hi  <<  8) |
                                      ((uint16_t)n_snap_v_mid <<  5) |
                                       (uint16_t)n_snap_c_hi);
        }

        // DRAM state machine (matches Verilog always @(negedge clk7))
        bool cpu_dram    = !mreq_n && !a15 && a14;
        bool cpu_cycle   = !ph3 && (ph2 || ph1 || ph0);
        bool cpu_ras_act = cpu_cycle && ph2 && !ph1;
        bool cpu_cas_act = cpu_cycle && ph2 &&  ph1 && !ph0;

        uint8_t n_a_r  = 0u;
        bool    n_ras_r = true, n_cas_r = true, n_we_r = true;

        if (cpu_cycle) {
            if (cpu_ras_act) {
                n_ras_r = !cpu_dram;
            }
            if (cpu_cas_act) {
                n_cas_r = !cpu_dram;
                n_we_r  = !(cpu_dram && !wr_n);
            }
        } else if (Border_n) {
            n_ras_r = false;  // /RAS asserted throughout ULA fetch
            if (!ph2) {
                // phases 0,8,9,10,11: pixel fetch
                n_a_r   = ph0 ? (uint8_t)(pix_addr & 0x7Fu)
                               : (uint8_t)(pix_addr >> 7);
                if (ph1 && !ph0) n_cas_r = false;  // phase 10
            } else {
                // phases 12,13,14,15: attribute fetch
                n_a_r   = ph0 ? (uint8_t)(attr_addr_v & 0x7Fu)
                               : (uint8_t)(attr_addr_v >> 7);
                if (ph1 && ph0) n_cas_r = false;   // phase 15
            }
        } else if (ph3 && !ph2 && !ph1) {
            // RAS-only refresh during blanking (phases 8,9)
            n_a_r   = (uint8_t)(hc & 0x7Fu);
            n_ras_r = false;
        }

        // --- Section 6: pixel pipeline ---
        uint8_t n_BitmapReg = DataLatch_n ? BitmapReg : D;
        uint8_t n_AttrReg   = AttrLatch_n ? AttrReg   : D;
        uint8_t n_SRegister = SLoad_r ? BitmapReg : (uint8_t)((SRegister << 1) & 0xFFu);
        uint8_t n_AttrOut   = AOLatch_n ? AttrOut
                            : (!VidEN_n ? AttrReg
                                        : (uint8_t)((BorderColor) | (BorderColor << 3)));

        // Port 0xFE write: BorderColor update (Core 1 handles this via the
        // io_core1_entry write path — BorderColor is shared below via extern)

        // ======================================================
        // 5. UPDATE all state
        // ======================================================
        hc = n_hc;          vc = n_vc;
        HBlank_n = n_HBlank_n;  HSync_n = n_HSync_n;
        VBlank_n = n_VBlank_n;  VSync_n = n_VSync_n;
        INT_r    = n_INT_r;
        FlashCnt = n_FlashCnt;  VSync_prev = n_VSync_prev;
        Border_n = n_Border_n;  VidEN_n    = n_VidEN_n;
        DataLatch_n = n_DataLatch_n;  AttrLatch_n = n_AttrLatch_n;
        SLoad_r  = n_SLoad;     AOLatch_n  = n_AOLatch_n;
        snap_c_hi  = n_snap_c_hi;   snap_v_hi  = n_snap_v_hi;
        snap_v_mid = n_snap_v_mid;  snap_v_lo  = n_snap_v_lo;
        pix_addr   = n_pix_addr;    attr_addr_v = n_attr_addr;
        a_r   = n_a_r;
        ras_r = n_ras_r;  cas_r = n_cas_r;  we_r = n_we_r;
        BitmapReg = n_BitmapReg;  AttrReg   = n_AttrReg;
        SRegister = n_SRegister;  AttrOut   = n_AttrOut;
    }
}