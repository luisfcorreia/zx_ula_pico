// =============================================================================
// video.cpp — Core 0 pixel loop: faithful C simulation of ula_zx48k.v
//
// Every iteration = one negedge clk7 event (7 MHz).
// Pattern per iteration:
//   1. Gate on PIO0 SM0 RX FIFO tick
//   2. Output current registered state to GPIO
//   3. Read inputs (D bus, bus control signals)
//   4. Compute all next-state values (Verilog <= assignments)
//   5. Update all state variables
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define SM_SYNC 0

extern uint32_t colour_table[16];
extern uint32_t border_table[8];
extern uint32_t sync_word;
extern uint32_t black_word;

extern volatile uint8_t shared_border_color;
volatile bool ula_fetch_window = false;

[[noreturn]] void video_run(void) {

    // ---- Registered state (mirrors every Verilog reg) ----
    uint32_t hc = 0, vc = 0;

    bool HBlank_n = true,  HSync_n = true;
    bool VBlank_n = true,  VSync_n = true;
    bool INT_r    = true;

    bool Border_n    = true;
    bool VidEN_n     = true;
    bool DataLatch_n = true;
    bool AttrLatch_n = true;
    bool SLoad_r     = false;
    bool AOLatch_n   = true;

    uint8_t  snap_c_hi  = 0, snap_v_hi  = 0;
    uint8_t  snap_v_mid = 0, snap_v_lo  = 0;
    uint16_t pix_addr   = 0, attr_addr_v = 0;
    uint8_t  a_r        = 0;
    bool     ras_r = true, cas_r = true, we_r = true;

    uint8_t BitmapReg = 0, SRegister  = 0;
    uint8_t AttrReg   = 0, AttrOut    = 0;
    uint8_t FlashCnt  = 0;
    bool    VSync_prev = true;

    while (true) {

        // ======================================================
        // 1. GATE on 7 MHz tick
        // ======================================================
        (void)pio_sm_get_blocking(pio0, SM_SYNC);
        uint8_t BorderColor = shared_border_color;

        // ======================================================
        // 2. OUTPUT current registered state
        // ======================================================

        // --- Video colour ---
        bool active_video = HBlank_n && VBlank_n;
        bool sync_active  = !HSync_n || !VSync_n;

        bool flash_pixel = ((AttrOut >> 7) & 1u) && ((FlashCnt >> 4) & 1u);
        bool Pixel = ((SRegister >> 7) & 1u) ^ flash_pixel;

        // CSYNC: 0 during hsync or vsync, 1 otherwise
        uint32_t csync_bit = (uint32_t)(!(HSync_n & VSync_n)) << 15;

        uint32_t vid_word;
        if (sync_active) {
            vid_word = sync_word | csync_bit;  // sync_word already has CSYNC=0
        } else if (!active_video) {
            vid_word = black_word | csync_bit;  // black_word already has CSYNC=1
        } else if (!VidEN_n) {
            uint8_t ink   = AttrOut & 7u;
            uint8_t paper = (AttrOut >> 3) & 7u;
            uint8_t col   = Pixel ? ink : paper;
            uint8_t lut   = col | ((AttrOut >> 3) & 8u);  // {BRIGHT,G,R,B}
            uint32_t rgb_bit = (((uint32_t)AttrOut >> 1) & 0x7u)
                              | (((uint32_t)AttrOut >> 3) & 0x8u);
            vid_word = colour_table[lut & 0xFu] | rgb_bit | csync_bit;
        } else {
            vid_word = border_table[BorderColor & 7u] | csync_bit;
        }
        ula_gpio_put_hi(VIDEO_GPIO_HI_MASK, vid_word);

        // --- DRAM control (GP0-GP9) ---
        {
            uint32_t dw = ((uint32_t)(a_r & 0x7Fu))
                        | (ras_r ? (1u << (PIN_RAS_N - PIN_RA_BASE)) : 0u)
                        | (cas_r ? (1u << (PIN_CAS_N - PIN_RA_BASE)) : 0u)
                        | (we_r  ? (1u << (PIN_WE_N  - PIN_RA_BASE)) : 0u);
            gpio_put_masked(PIN_DRAM_MASK, dw);
        }

        // --- /INT ---
        gpio_put(PIN_INT_N, INT_r ? 1 : 0);

        // --- Contention window flag for Core 1 ---
        // display_phase = hc[3] & ~hc[2] = phases 8-11
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

        // ROM_CS: combinatorial every pixel clock
        // Verilog: assign rom_cs_n = a15 | a14 | mreq_n
        gpio_put(PIN_ROM_CS_N, (int)(a15 | a14 | mreq_n));

        // ======================================================
        // 4. COMPUTE next state
        // ======================================================
        uint8_t phase = (uint8_t)(hc & 0xFu);
        bool ph3 = (phase >> 3) & 1u;
        bool ph2 = (phase >> 2) & 1u;
        bool ph1 = (phase >> 1) & 1u;
        bool ph0 =  phase       & 1u;

        // Counters
        uint32_t n_hc = (hc == HC_MAX) ? 0u : hc + 1u;
        uint32_t n_vc = vc;
        if (hc == HC_MAX) n_vc = (vc == VC_MAX) ? 0u : vc + 1u;

        // Sync / blanking
        bool n_HBlank_n = (hc == 416u) ? true  : (hc == 320u) ? false : HBlank_n;
        bool n_HSync_n  = (hc == 376u) ? true  : (hc == 344u) ? false : HSync_n;
        bool n_VBlank_n = (vc == 256u) ? true  : (vc == 248u) ? false : VBlank_n;
        bool n_VSync_n  = (vc == 252u) ? true  : (vc == 248u) ? false : VSync_n;

        bool n_INT_r = INT_r;
        if (vc == 248u) {
            if (hc == 0u)  n_INT_r = false;
            if (hc == 32u) n_INT_r = true;
        }

        uint8_t n_FlashCnt = FlashCnt;
        if (VSync_prev && !VSync_n) n_FlashCnt++;
        bool n_VSync_prev = VSync_n;

        // Display window
        bool n_Border_n = !(((vc & 0x80u) && (vc & 0x40u)) || (vc & 0x100u) || (hc & 0x100u));

        bool n_VidEN_n = VidEN_n;
        if (hc & 8u) n_VidEN_n = !Border_n;

        bool n_DataLatch_n = !((phase == 0xBu) && Border_n);
        bool n_AttrLatch_n = !((phase == 0x0u) && Border_n);
        bool n_SLoad       =   (phase == 0x4u) && !VidEN_n;
        bool n_AOLatch_n   =  !(phase == 0x5u);

        // DRAM address snap at phases 7 and 11
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
            n_pix_addr   = (uint16_t)(((uint16_t)n_snap_v_hi  << 11) |
                                      ((uint16_t)n_snap_v_lo  <<  8) |
                                      ((uint16_t)n_snap_v_mid <<  5) |
                                       (uint16_t)n_snap_c_hi);
            n_attr_addr  = (uint16_t)(0x1800u |
                                      ((uint16_t)n_snap_v_hi  <<  8) |
                                      ((uint16_t)n_snap_v_mid <<  5) |
                                       (uint16_t)n_snap_c_hi);
        }

        // DRAM state machine
        bool cpu_dram    = !mreq_n && !a15 && a14;
        bool cpu_cycle   = !ph3 && (ph2 || ph1 || ph0);
        bool cpu_ras_act = cpu_cycle && ph2 && !ph1;
        bool cpu_cas_act = cpu_cycle && ph2 &&  ph1 && !ph0;

        uint8_t n_a_r  = 0u;
        bool    n_ras_r = true, n_cas_r = true, n_we_r = true;

        if (cpu_cycle) {
            if (cpu_ras_act) n_ras_r = !cpu_dram;
            if (cpu_cas_act) {
                n_cas_r = !cpu_dram;
                n_we_r  = !(cpu_dram && !wr_n);
            }
        } else if (Border_n) {
            n_ras_r = false;
            if (!ph2) {
                n_a_r = ph0 ? (uint8_t)(pix_addr & 0x7Fu) : (uint8_t)(pix_addr >> 7);
                if (ph1 && !ph0) n_cas_r = false;
            } else {
                n_a_r = ph0 ? (uint8_t)(attr_addr_v & 0x7Fu) : (uint8_t)(attr_addr_v >> 7);
                if (ph1 && ph0) n_cas_r = false;
            }
        } else if (ph3 && !ph2 && !ph1) {
            n_a_r   = (uint8_t)(hc & 0x7Fu);
            n_ras_r = false;
        }

        // Pixel pipeline
        uint8_t n_BitmapReg = DataLatch_n ? BitmapReg : D;
        uint8_t n_AttrReg   = AttrLatch_n ? AttrReg   : D;
        uint8_t n_SRegister = SLoad_r ? BitmapReg : (uint8_t)((SRegister << 1) & 0xFFu);
        uint8_t n_AttrOut   = AOLatch_n ? AttrOut
                            : (!VidEN_n ? AttrReg
                                        : (uint8_t)(BorderColor | (BorderColor << 3)));

        // ======================================================
        // 5. UPDATE state
        // ======================================================
        hc = n_hc;           vc = n_vc;
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
