// =============================================================================
// video_init.cpp — PIO0 SM0 tick + PIO1 SM0 video + DMA + colour tables
// =============================================================================

#include "video.h"
#include "pinmap.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#include "sync.pio.h"
#include "video.pio.h"

// Buffers
uint8_t  capture_buf[CAPTURE_BUFS][CAPTURE_BYTES];
uint32_t video_buf[VIDEO_BUFS][VIDEO_BUF_WORDS];

// Precomputed tables
uint32_t colour_table[16];
uint32_t border_table[8];
uint32_t sync_word;
uint32_t black_word;

// Template line: 448 words for border/blanking/sync (patched per-line with border color)
uint32_t template_line[PIXELS_PER_LINE];

int sync_tick_program_offset;
int video_program_offset;

static void dma_init(void) {
    // CH0: video buffer → PIO1 SM0 TX FIFO (DREQ-paced)
    dma_channel_config c0 = dma_channel_get_default_config(DMA_CH_VIDEO);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pio1, SM_VIDEO, true));
    dma_channel_configure(DMA_CH_VIDEO, &c0,
        &pio1->txf[SM_VIDEO],   // write addr
        NULL,                    // read addr (set per-line)
        PIXELS_PER_LINE,         // transfer count
        false);                  // don't start yet
}

void video_init(void) {
    // PIO0 SM0: 7 MHz tick source
    sync_tick_program_offset = pio_add_program(pio0, &sync_tick_program);
    pio_sm_config c = sync_tick_program_get_default_config(sync_tick_program_offset);
    sm_config_set_clkdiv(&c, (float)PIO0_CLK_DIV);
    sm_config_set_in_shift(&c, false, true, 1);
    pio_sm_init(pio0, SM_SYNC, sync_tick_program_offset, &c);

    // PIO1 SM0: 7 MHz video output to GPIO HI (GP33-GP47)
    video_program_offset = pio_add_program(pio1, &video_dma_program);
    pio_sm_config vc = video_dma_program_get_default_config(video_program_offset);
    sm_config_set_out_shift(&vc, true, true, 32);
    sm_config_set_out_pins(&vc, VIDEO_GPIO_HI_BASE, 16);
    sm_config_set_clkdiv_int_frac(&vc, PIO0_CLK_DIV, 0);
    pio_sm_init(pio1, SM_VIDEO, video_program_offset, &vc);

    for (int i = VIDEO_GPIO_HI_BASE; i < VIDEO_GPIO_HI_BASE + 16; i++) {
        pio_gpio_init(pio1, i);
    }
    pio_sm_set_consecutive_pindirs(pio1, SM_VIDEO, VIDEO_GPIO_HI_BASE, 16, true);

    // DMA init
    dma_init();

    // Colour tables
    for (int i = 0; i < 16; i++)
        colour_table[i] = build_colour_word((uint8_t)i);
    for (int i = 0; i < 8; i++)
        border_table[i] = build_colour_word((uint8_t)i);

    sync_word  = build_sync_word();
    black_word = build_black_word();

    // Build template line: border/blanking/sync pattern
    // hc 0-255: active display (placeholder, patched per-line)
    // hc 256-319: border
    // hc 320-343: blanking (black)
    // hc 344-375: hsync (sync)
    // hc 376-415: blanking (black)
    // hc 416-447: border
    for (int hc = 0; hc < PIXELS_PER_LINE; hc++) {
        uint32_t csync = 1u << 15;  // CSYNC active (high = no sync)
        if (hc >= 344 && hc <= 375) {
            // HSync: CSYNC low, sync tip
            csync = 0u << 15;
            template_line[hc] = sync_word;
        } else if (hc >= 320 && hc <= 415) {
            // Blanking: black pedestal
            template_line[hc] = black_word;
        } else if (hc >= 256 && hc <= 319) {
            // Right border
            template_line[hc] = border_table[4] | (1u << 15);  // blue border
        } else if (hc >= 416 && hc <= 447) {
            // Left border (next line start)
            template_line[hc] = border_table[4] | (1u << 15);
        } else {
            // Active display: placeholder (will be computed from capture buffer)
            template_line[hc] = black_word | (1u << 15);
        }
    }
}
