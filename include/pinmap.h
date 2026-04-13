#pragma once
#include <stdint.h>

// =============================================================================
// pinmap.h — ZX Spectrum RP2350B ULA pin map and timing constants
// System clock: 252 MHz = 36 × 7 MHz (exact ratio, zero-jitter pixel clock)
// =============================================================================

// DRAM interface (GP0–GP9) — PIO1 SM0
#define PIN_RA_BASE     0
#define PIN_RA_COUNT    7
#define PIN_RAS_N       7
#define PIN_CAS_N       8
#define PIN_WE_N        9

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
#define PIN_CLOCK       25
#define PIN_ROM_CS_N    26

// Audio / keyboard (GP27–GP32)
#define PIN_SOUND       27
#define PIN_T_BASE      28
#define PIN_T_COUNT     5

// /Y DAC (GP33–GP36)
#define PIN_YN_BASE     33
#define PIN_YN_COUNT    4

// U Cb (GP37)
#define PIN_UO_BASE     37
#define PIN_UO_COUNT    1

// V Cr (GP38)
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

// Contention signal (GP45) — output driven by Core 1
// HIGH when CPU is accessing contended RAM (A15=0, A14=1, MREQ_N=0).
// Read by cpu_clock SM3 via JMP PIN to implement real-time contention.

// GP46–GP47 — reserved
#define PIN_SPARE_BASE  45
#define PIN_SPARE_COUNT 3

// Timing constants
#define SYS_CLK_MHZ             252
#define PIXEL_CLK_MHZ             7
#define PIO0_CLK_DIV             36   // 252 / 36 = 7 MHz
#define SYS_CYCLES_PER_PIXEL     36

// DRAM (4116-4) timing in 252 MHz system cycles (1 cycle = ~4 ns)
#define DRAM_RAS_CYCLES          38
#define DRAM_CAS_CYCLES          19
#define DRAM_RP_CYCLES           25

// Raster timing (pixel clocks at 7 MHz)
#define HC_MAX                  447
#define VC_MAX                  311
#define HBLANK_START            320
#define HBLANK_END              415
#define HSYNC_START             344
#define HSYNC_END               375
#define VBLANK_START            248
#define VBLANK_END              255
#define VSYNC_START             248
#define VSYNC_END               251
#define INT_HC_END               31

// 16-clock display group phases (hc & 0xF)
#define PHASE_TURNAROUND        0x0
#define PHASE_CPU_RAS           0x2
#define PHASE_CPU_CAS           0x4
#define PHASE_CPU_DATA          0x5
#define PHASE_CPU_REL           0x6
#define PHASE_CPU_PRE           0x7
#define PHASE_PIX_RAS           0x8
#define PHASE_PIX_COL           0x9
#define PHASE_PIX_CAS           0xA
#define PHASE_PIX_DATA          0xB
#define PHASE_PIX_PRE           0xC
#define PHASE_ATTR_RAS          0xD
#define PHASE_ATTR_COL          0xE
#define PHASE_ATTR_CAS          0xF
