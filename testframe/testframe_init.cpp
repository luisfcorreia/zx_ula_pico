// =============================================================================
// testframe_init.cpp — PIO0 SM0 tick + PIO1 SM0 video + DMA + tables
// =============================================================================

#include "testframe.h"
#include "video/video.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#include "sync.pio.h"
#include "video.pio.h"

uint32_t tf_video_buf[TESTFRAME_VIDEO_BUFS][TESTFRAME_VIDEO_BUF_WORDS];
uint32_t tf_colour_table[16];
uint32_t tf_template_line[PIXELS_PER_LINE];

static void dma_init(void) {
    dma_channel_config c0 = dma_channel_get_default_config(DMA_CH_VIDEO);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pio1, SM_VIDEO, true));
    dma_channel_configure(DMA_CH_VIDEO, &c0,
        &pio1->txf[SM_VIDEO],
        NULL,
        PIXELS_PER_LINE,
        false);
}

void testframe_init(void) {
    // PIO0 SM0: 7 MHz tick
    int sync_off = pio_add_program(pio0, &sync_tick_program);
    pio_sm_config c = sync_tick_program_get_default_config(sync_off);
    sm_config_set_clkdiv(&c, (float)PIO0_CLK_DIV);
    sm_config_set_in_shift(&c, false, true, 1);
    pio_sm_init(pio0, SM_SYNC, sync_off, &c);

    // PIO1 SM0: 7 MHz video output to GPIO HI
    int vid_off = pio_add_program(pio1, &video_dma_program);
    pio_sm_config vc = video_dma_program_get_default_config(vid_off);
    sm_config_set_out_shift(&vc, true, true, 32);
    sm_config_set_out_pins(&vc, VIDEO_GPIO_HI_BASE, 16);
    sm_config_set_clkdiv_int_frac(&vc, PIO0_CLK_DIV, 0);
    pio_sm_init(pio1, SM_VIDEO, vid_off, &vc);

    for (int i = VIDEO_GPIO_HI_BASE; i < VIDEO_GPIO_HI_BASE + 16; i++) {
        pio_gpio_init(pio1, i);
    }
    pio_sm_set_consecutive_pindirs(pio1, SM_VIDEO, VIDEO_GPIO_HI_BASE, 16, true);

    dma_init();

    // Colour tables
    for (int i = 0; i < 16; i++)
        tf_colour_table[i] = build_colour_word((uint8_t)i);

    // Template line: border/blanking/sync (border = black)
    uint32_t sync_w  = build_sync_word();
    uint32_t black_w = build_black_word();

    for (int hc = 0; hc < PIXELS_PER_LINE; hc++) {
        if (hc >= 344 && hc <= 375) {
            tf_template_line[hc] = sync_w;
        } else if (hc >= 320 && hc <= 415) {
            tf_template_line[hc] = black_w;
        } else if (hc >= 256 && hc <= 319) {
            tf_template_line[hc] = black_w | (1u << 15);
        } else if (hc >= 416 && hc <= 447) {
            tf_template_line[hc] = black_w | (1u << 15);
        } else {
            tf_template_line[hc] = black_w | (1u << 15);
        }
    }
}
