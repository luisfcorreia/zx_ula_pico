// =============================================================================
// video.cpp — Core 0: capture loop + hblank compute + DMA swap
//
// Pipeline (one-line delay):
//   Line N active display  → capture data bus bytes + compute DRAM words on-the-fly
//   Hblank                 → compute video words for line N from capture buffer
//   Line N+1 active display → DMA streams video words to PIO1 SM0
//
// Direct register access (no SDK wrappers) in hot path for cycle budget.
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "dram/dram.h"
#include "cpu/cpu.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/pio.h"
#include "hardware/structs/dma.h"

extern uint32_t colour_table[16];
extern uint32_t border_table[8];
extern uint32_t template_line[PIXELS_PER_LINE];

extern volatile uint8_t shared_border_color;
volatile bool ula_fetch_window = false;

#define PIO_FSTAT_TXFULL_SM(n) (1u << (16 + (n)))
#define PIO_FSTAT_RXEMPTY_SM(n) (1u << (8 + (n)))

__attribute__((optimize("O3")))
[[noreturn]] void video_run(void) {
    uint32_t hc = 0, vc = 0;
    bool INT_r = true;
    bool Border_n = true;
    uint16_t pix_addr = 0, attr_addr_v = 0;
    uint8_t FlashCnt = 0;
    bool VSync_n = true, VSync_prev = true;
    int cap_buf_idx = 0, vid_buf_idx = 0;

    const uint32_t rom_cs_bit = (1u << PIN_ROM_CS_N);
    const uint32_t int_bit    = (1u << PIN_INT_N);

    for (int i = 0; i < CAPTURE_BYTES; i++)
        capture_buf[cap_buf_idx][i] = 0;

    while (true) {
        // 1. GATE on 7 MHz tick
        while (pio0_hw->fstat & PIO_FSTAT_RXEMPTY_SM(SM_SYNC));
        (void)pio0_hw->rxf[SM_SYNC];
        uint8_t BorderColor = shared_border_color;

        // 2. DRAM word (on-the-fly, matches Verilog)
        uint8_t phase = (uint8_t)(hc & 0xFu);
        bool ph3 = (phase >> 3) & 1u;
        bool ph2 = (phase >> 2) & 1u;
        bool ph1 = (phase >> 1) & 1u;
        bool ph0 =  phase       & 1u;

        uint32_t gpio_in = sio_hw->gpio_in;
        uint8_t  D       = (uint8_t)((gpio_in >> PIN_D_BASE) & 0xFFu);
        bool     mreq_n  = (gpio_in >> PIN_MREQ_N) & 1u;
        bool     wr_n    = (gpio_in >> PIN_WR_N)   & 1u;
        bool     a14     = (gpio_in >> PIN_A14)     & 1u;
        bool     a15     = (gpio_in >> PIN_A15)     & 1u;

        if (a15 | a14 | mreq_n)
            sio_hw->gpio_set = rom_cs_bit;
        else
            sio_hw->gpio_clr = rom_cs_bit;

        bool cpu_dram    = !mreq_n && !a15 && a14;
        bool cpu_cycle   = !ph3 && (ph2 || ph1 || ph0);
        bool cpu_ras_act = cpu_cycle && ph2 && !ph1;
        bool cpu_cas_act = cpu_cycle && ph2 &&  ph1 && !ph0;

        uint8_t  a_r  = 0u;
        bool    ras_r = true, cas_r = true, we_r = true;

        if (cpu_cycle) {
            if (cpu_ras_act) ras_r = !cpu_dram;
            if (cpu_cas_act) {
                cas_r = !cpu_dram;
                we_r  = !(cpu_dram && !wr_n);
            }
        } else if (Border_n) {
            ras_r = false;
            if (!ph2) {
                a_r = ph0 ? (uint8_t)(pix_addr & 0x7Fu) : (uint8_t)(pix_addr >> 7);
                if (ph1 && !ph0) cas_r = false;
            } else {
                a_r = ph0 ? (uint8_t)(attr_addr_v & 0x7Fu) : (uint8_t)(attr_addr_v >> 7);
                if (ph1 && ph0) cas_r = false;
            }
        } else if (ph3 && !ph2 && !ph1) {
            a_r   = (uint8_t)(hc & 0x7Fu);
            ras_r = (ph1 && !ph0);
        }

        uint32_t dram_word = ((uint32_t)(a_r & 0x7Fu))
                           | (ras_r ? (1u << (PIN_RAS_N - PIN_RA_BASE)) : 0u)
                           | (cas_r ? (1u << (PIN_CAS_N - PIN_RA_BASE)) : 0u)
                           | (we_r  ? (1u << (PIN_WE_N  - PIN_RA_BASE)) : 0u);

        while (pio1_hw->fstat & PIO_FSTAT_TXFULL_SM(SM_DRAM));
        pio1_hw->txf[SM_DRAM] = dram_word;

        // 3. CAPTURE data bus bytes
        bool active_display = Border_n && (hc < 256u);
        if (active_display) {
            uint8_t group = (uint8_t)(hc >> 3);
            if (ph0 && !ph1 && !ph2 && !ph3)
                capture_buf[cap_buf_idx][32 + group] = D;
            if (!ph0 && ph1 && ph2 && ph3)
                capture_buf[cap_buf_idx][group] = D;
        }

        // 4. CONTENTION window
        ula_fetch_window = Border_n && (hc & 8u) && !(hc & 4u);

        // 5. /INT
        if (INT_r)
            sio_hw->gpio_set = int_bit;
        else
            sio_hw->gpio_clr = int_bit;

        // 6. HBLANK: video compute + DMA swap
        if (hc >= 256u && hc < 288u) {
            uint32_t *vid_out = video_buf[vid_buf_idx];
            int g = (int)(hc - 256u);
            uint8_t bitmap = capture_buf[cap_buf_idx][g];
            uint8_t attr   = capture_buf[cap_buf_idx][32 + g];
            bool flash_pixel = ((attr >> 7) & 1u) && ((FlashCnt >> 4) & 1u);
            uint8_t ink   = attr & 7u;
            uint8_t paper = (attr >> 3) & 7u;
            uint8_t lut_base = ((attr >> 3) & 8u);
            for (int p = 0; p < 8; p++) {
                bool pixel = ((bitmap >> 7) & 1u) ^ flash_pixel;
                bitmap = (uint8_t)(bitmap << 1);
                uint8_t col = pixel ? ink : paper;
                uint8_t lut = col | lut_base;
                uint32_t rgb_bit = (((uint32_t)lut >> 1) & 1u) << 11
                                 | (((uint32_t)lut >> 2) & 1u) << 12
                                 | (((uint32_t)lut)      & 1u) << 13
                                 | (((uint32_t)lut >> 3) & 1u) << 14;
                vid_out[g * 8 + p] = colour_table[lut & 0xFu] | rgb_bit | (1u << 15);
            }
        }

        if (hc >= 288u && hc < 319u) {
            uint32_t *vid_out = video_buf[vid_buf_idx];
            int base = 256 + ((int)(hc - 288u) << 2);
            uint32_t border_word = border_table[BorderColor & 7u] | (1u << 15);
            for (int i = 0; i < 4; i++) {
                int idx = base + i;
                if (idx >= 256 && idx < 320)
                    vid_out[idx] = border_word;
                else if (idx >= 416)
                    vid_out[idx] = border_word;
                else
                    vid_out[idx] = template_line[idx];
            }
        }

        if (hc == 319u) {
            dma_hw->ch[DMA_CH_VIDEO].al3_read_addr_trig = (uint32_t)(uintptr_t)video_buf[vid_buf_idx];
            cap_buf_idx = 1 - cap_buf_idx;
            vid_buf_idx = 1 - vid_buf_idx;
            for (int i = 0; i < CAPTURE_BYTES; i++)
                capture_buf[cap_buf_idx][i] = 0;
        }

        // 7. NEXT STATE
        uint32_t n_hc = (hc == HC_MAX) ? 0u : hc + 1u;
        uint32_t n_vc = vc;
        if (hc == HC_MAX) n_vc = (vc == VC_MAX) ? 0u : vc + 1u;

        bool n_INT_r = INT_r;
        if (vc == 248u) {
            if (hc == 0u)  n_INT_r = false;
            if (hc == 32u) n_INT_r = true;
        }

        uint8_t n_FlashCnt = FlashCnt;
        if (VSync_prev && !VSync_n) n_FlashCnt++;
        bool n_VSync_prev = VSync_n;
        bool n_VSync_n    = (vc == 252u) ? true : (vc == 248u) ? false : VSync_n;

        bool n_Border_n = !(((vc & 0x80u) && (vc & 0x40u)) || (vc & 0x100u) || (hc & 0x100u));

        uint16_t n_pix_addr  = pix_addr;
        uint16_t n_attr_addr = attr_addr_v;
        if (Border_n && (phase == 0x7u || phase == 0xBu)) {
            uint8_t snap_c_hi  = (uint8_t)((hc >> 3) & 0x1Fu);
            uint8_t snap_v_hi  = (uint8_t)((vc >> 6) & 0x3u);
            uint8_t snap_v_mid = (uint8_t)((vc >> 3) & 0x7u);
            uint8_t snap_v_lo  = (uint8_t)(vc & 0x7u);
            n_pix_addr  = (uint16_t)(((uint16_t)snap_v_hi  << 11) |
                                     ((uint16_t)snap_v_lo  <<  8) |
                                     ((uint16_t)snap_v_mid <<  5) |
                                      (uint16_t)snap_c_hi);
            n_attr_addr = (uint16_t)(0x1800u |
                                     ((uint16_t)snap_v_hi  <<  8) |
                                     ((uint16_t)snap_v_mid <<  5) |
                                      (uint16_t)snap_c_hi);
        }

        // 8. UPDATE
        hc = n_hc; vc = n_vc;
        INT_r = n_INT_r;
        FlashCnt = n_FlashCnt; VSync_prev = n_VSync_prev; VSync_n = n_VSync_n;
        Border_n = n_Border_n;
        pix_addr = n_pix_addr; attr_addr_v = n_attr_addr;
    }
}
