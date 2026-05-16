#pragma once
#include <stdint.h>
#include "pinmap.h"

void testframe_init(void);
[[noreturn]] void testframe_run(void);

#define TESTFRAME_VIDEO_BUF_WORDS PIXELS_PER_LINE
#define TESTFRAME_VIDEO_BUFS      2

extern uint32_t tf_video_buf[TESTFRAME_VIDEO_BUFS][TESTFRAME_VIDEO_BUF_WORDS];
extern uint32_t tf_colour_table[16];
extern uint32_t tf_template_line[PIXELS_PER_LINE];
