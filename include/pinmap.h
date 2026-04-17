#pragma once
#include <stdint.h>

// =============================================================================
// pinmap.h — ZX Spectrum RP2350B ULA pin map and timing constants
// System clock: 252 MHz = 36 × 7 MHz
// =============================================================================

// DRAM interface (GP0–GP9)
#define PIN_RA_BASE     0
#define PIN_RA_COUNT    7
#define PIN_RAS_N       7
#define PIN_CAS_N       8
#define PIN_WE_N        9

#define PIN_DRAM_MASK   0x3FFu
#define PIN_DRAM_IDLE   ((1u<<7)|(1u<<8)|(1u<<9))

// Data bus (GP10–GP17)
#define PIN_D_BASE      10
#define PIN_D_COUNT     8

// Z80 control inputs (GP18–GP23)
#define PIN_WR_N        18
#define PIN_RD_N        19
#define PIN_MREQ_N      20
#define PIN_IO_ULA_N    21
#define PIN_A14         22
#define PIN_A15         23

// Z80 control outputs
#define PIN_INT_N       24
#define PIN_ROM_CS_N    25   // swapped
#define PIN_CLOCK       26   // swapped

// Audio / keyboard (GP27–GP32)
#define PIN_SOUND       27
#define PIN_T_BASE      28
#define PIN_T_COUNT     5

// Video outputs — YUV composite via R-2R ladders, plus RGBi TTL bonus
//
// /Y luma+sync DAC  — GP33–GP36 (4-bit)
// U  Cb DAC         — GP37–GP39 (3-bit)
// V  Cr DAC         — GP40–GP42 (3-bit)
//
// RGBi bonus TTL outputs — GP43 (R), GP44 (B), GP45 (G), GP46 (I), GP47 (CSYNC)
//                          (active during active display area only)

#define PIN_YN_BASE     33
#define PIN_YN_COUNT    4

#define PIN_UO_BASE     37
#define PIN_UO_COUNT    3

#define PIN_VO_BASE     40
#define PIN_VO_COUNT    3

#define PIN_RGB_R       43
#define PIN_RGB_G       45
#define PIN_RGB_B       44
#define PIN_RGB_I       46
#define PIN_RGB_CSYNC   47

// Timing constants
#define SYS_FREQ_MHZ             252
#define PIXEL_CLK_MHZ             7
#define PIO0_CLK_DIV             36
#define SYS_CYCLES_PER_PIXEL     36

// Raster timing (from Verilog, 448 clocks/line)
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

#define PIXELS_PER_LINE 448
#define TOTAL_LINES     312
#define ACTIVE_PIXELS   256
#define ACTIVE_LINES    192

// High GPIO bank (GP32 = bit 0)
// GP33=bit1 .. GP36=bit4  (YN[3:0])
// GP37=bit5 .. GP39=bit7  (UO[2:0])
// GP40=bit8 .. GP42=bit10 (VO[2:0])
#define VIDEO_GPIO_HI_BASE  32u
#define VIDEO_GPIO_HI_MASK  ((0xFu << (PIN_YN_BASE - VIDEO_GPIO_HI_BASE)) | \
                             (0x7u << (PIN_UO_BASE - VIDEO_GPIO_HI_BASE)) | \
                             (0x7u << (PIN_VO_BASE - VIDEO_GPIO_HI_BASE)))
