// =============================================================================
// testframe.cpp — Static test pattern video generator
//
// Same raster timing as video_run(), but reads from static bitmap/attr
// instead of capturing from the data bus. No DRAM, no contention, no Core 1.
// =============================================================================

#include "testframe.h"
#include "testframe_data.h"
#include "pinmap.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/pio.h"
#include "hardware/structs/dma.h"

#define PIO_FSTAT_RXEMPTY_SM(n) (1u << (8 + (n)))

[[noreturn]] void testframe_run(void) {
    uint32_t hc = 0, vc = 0;
    bool INT_r = true;
    uint8_t FlashCnt = 0;
    bool VSync_n = true, VSync_prev = true;
    int vid_buf_idx = 0;

    const uint32_t int_bit = (1u << PIN_INT_N);
    const uint32_t border_word = tf_template_line[256];

    for (int i = 0; i < TESTFRAME_VIDEO_BUF_WORDS; i++)
        tf_video_buf[vid_buf_idx][i] = 0;

    while (true) {
        // 1. GATE on 7 MHz tick
        while (pio0_hw->fstat & PIO_FSTAT_RXEMPTY_SM(SM_SYNC));
        (void)pio0_hw->rxf[SM_SYNC];

        // 2. HBLANK: compute video words for current line
        if (hc >= 256u && hc < 288u && vc < 192u) {
            uint32_t *vid_out = tf_video_buf[vid_buf_idx];
            int g = (int)(hc - 256u);
            uint16_t bidx = (uint16_t)(vc * 32 + g);
            uint16_t aidx = (uint16_t)((vc / 8) * 32 + g);
            uint8_t bitmap = TESTFRAME_BITMAP_BYTE;
            uint8_t attr   = testframe_attr[aidx];
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
                vid_out[g * 8 + p] = tf_colour_table[lut & 0xFu] | rgb_bit | (1u << 15);
            }
        }

        if (hc >= 288u && hc < 320u) {
            uint32_t *vid_out = tf_video_buf[vid_buf_idx];
            int base = 256 + ((int)(hc - 288u) << 2);
            for (int i = 0; i < 4; i++) {
                int idx = base + i;
                if (idx >= 256 && idx < 320)
                    vid_out[idx] = border_word;
                else if (idx >= 416)
                    vid_out[idx] = border_word;
                else
                    vid_out[idx] = tf_template_line[idx];
            }
        }

        // 3. DMA trigger + swap
        if (hc == 319u) {
            dma_hw->ch[DMA_CH_VIDEO].al3_read_addr_trig = (uint32_t)(uintptr_t)tf_video_buf[vid_buf_idx];
            vid_buf_idx = 1 - vid_buf_idx;
        }

        // 4. /INT
        if (INT_r)
            sio_hw->gpio_set = int_bit;
        else
            sio_hw->gpio_clr = int_bit;

        // 5. NEXT STATE
        uint32_t n_hc = (hc == HC_MAX) ? 0u : hc + 1u;
        uint32_t n_vc = vc;
        if (hc == HC_MAX) n_vc = (vc == VC_MAX) ? 0u : vc + 1u;

        bool n_INT_r = INT_r;
        if (vc == 248u) {
            if (hc == 0u)  n_INT_r = false;
            if (hc == 32u) n_INT_r = true;
        }

        uint8_t n_FlashCnt = FlashCnt;
        bool n_VSync_prev = VSync_n;
        bool n_VSync_n    = (vc == 252u) ? true : (vc == 248u) ? false : VSync_n;
        if (n_VSync_prev && !n_VSync_n) n_FlashCnt++;

        hc = n_hc; vc = n_vc;
        INT_r = n_INT_r;
        FlashCnt = n_FlashCnt;
        VSync_prev = n_VSync_prev;
        VSync_n = n_VSync_n;
    }
}
