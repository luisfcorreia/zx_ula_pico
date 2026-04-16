#pragma once
#include <stdint.h>

// =============================================================================
// pinmap.h — ZX Spectrum RP2350B ULA pin map and timing constants
// System clock: 252 MHz = 36 × 7 MHz
// =============================================================================

// DRAM interface (GP0–GP9) — Core 0 drives via gpio_put_masked(0x3FF, ...)
#define PIN_RA_BASE     0
#define PIN_RA_COUNT    7
#define PIN_RAS_N       7
#define PIN_CAS_N       8
#define PIN_WE_N        9

// DRAM GPIO mask: A[6:0] + /RAS + /CAS + /WE = bits 0-9
#define PIN_DRAM_MASK   0x3FFu
// Deasserted state: /RAS=1(bit7) /CAS=1(bit8) /WE=1(bit9) A=0
#define PIN_DRAM_IDLE   ((1u<<7)|(1u<<8)|(1u<<9))

// Data bus (GP10–GP17) — bidirectional
#define PIN_D_BASE      10
#define PIN_D_COUNT     8

// Z80 control inputs (GP18–GP23)
#define PIN_WR_N        18
#define PIN_RD_N        19
#define PIN_MREQ_N      20
#define PIN_IO_ULA_N    21
#define PIN_A14         22
#define PIN_A15         23

// Z80 control outputs (GP24–GP26)
#define PIN_INT_N       24
#define PIN_ROM_CS_N    25
#define PIN_CLOCK       26

// Audio / keyboard (GP27–GP32)
#define PIN_SOUND       27
#define PIN_T_BASE      28
#define PIN_T_COUNT     5

// /Y DAC (GP33–GP36) — 4-bit luma
#define PIN_YN_BASE     33
#define PIN_YN_COUNT    4

// U Cb (GP37) — 1-bit sign
#define PIN_UO_BASE     37
#define PIN_UO_COUNT    1

// V Cr (GP38) — 1-bit sign
#define PIN_VO_BASE     38
#define PIN_VO_COUNT    1

// RGBi outputs (GP39–GP42)
#define PIN_R           39
#define PIN_G           40
#define PIN_B           41
#define PIN_BRIGHT      42

// Sync outputs (GP43–GP44)
#define PIN_HSYNC_N     43
#define PIN_VSYNC_N     44

// Spare (GP45–GP47)
#define PIN_SPARE_BASE  45
#define PIN_SPARE_COUNT 3

// Timing constants
#define SYS_CLK_MHZ             252
#define PIXEL_CLK_MHZ             7
#define PIO0_CLK_DIV             36
#define SYS_CYCLES_PER_PIXEL     36

// Raster timing — from Verilog (448 clocks/line, 312 lines/frame)
#define HC_MAX          447
#define VC_MAX          311
#define HBLANK_START    320
#define HBLANK_END      415
#define HSYNC_START     344
#define HSYNC_END       375
#define VBLANK_START    248
#define VBLANK_END      255
#define VSYNC_START     248
#define VSYNC_END       251

// Total counts (for loops/buffers)
#define PIXELS_PER_LINE 448
#define TOTAL_LINES     312
#define ACTIVE_PIXELS   256
#define ACTIVE_LINES    192

// High GPIO bank base (GP32)
#define VIDEO_GPIO_HI_BASE  32u
#define VIDEO_GPIO_HI_MASK ( \
    (0xFu << (PIN_YN_BASE  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_UO_BASE  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_VO_BASE  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_R        - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_G        - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_B        - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_BRIGHT   - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_HSYNC_N  - VIDEO_GPIO_HI_BASE)) | \
    (1u   << (PIN_VSYNC_N  - VIDEO_GPIO_HI_BASE)) )
