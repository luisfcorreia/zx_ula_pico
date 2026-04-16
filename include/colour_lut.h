#pragma once
#include <stdint.h>

// =============================================================================
// colour_lut.h — ZX Spectrum 16-colour YUV DAC lookup table
//
// Index: {BRIGHT[3], G[2], R[1], B[0]}  (ZX attribute GRB order)
//
// Packed uint16_t: bits[11:8]=yn(4-bit), bits[3:2]=uo(2 MSBs of 4-bit), bits[1:0]=vo(2 MSBs)
// Since hardware only has 1 pin each for U/V, we use the sign bit (bit3 of the 4-bit value).
// uo_sign = 1 when uo_val >= 8 (positive/neutral Cb), 0 when negative
// vo_sign = 1 when vo_val >= 8 (positive/neutral Cr), 0 when negative
//
// Packing: bits[11:8]=yn, bit[1]=uo_sign, bit[0]=vo_sign
//
// /Y values from Verilog (exact match):
//   sync tip=15, black=11, bright white=0
// U/V sign from Verilog 4-bit DAC values (BT.601 derived):
//   uo >= 8 → sign=1 (positive Cb), uo < 8 → sign=0
//   vo >= 8 → sign=1 (positive Cr), vo < 8 → sign=0
// =============================================================================

static const uint16_t colour_lut[16] = {
    //  {I,G,R,B}  Colour          yn   uo  vo
    0x0B03,  // 0000  Black/N        11    1   1  (neutral: uo=8, vo=8)
    0x0A02,  // 0001  Blue/N         10    1   0  (uo=14 pos, vo=7 neg)
    0x0801,  // 0010  Red/N           8    0   1  (uo=6 neg, vo=14 pos)
    0x0703,  // 0011  Magenta/N       7    1   1  (uo=12 pos, vo=13 pos)
    0x0600,  // 0100  Green/N         6    0   0  (uo=4 neg, vo=3 neg)
    0x0502,  // 0101  Cyan/N          5    1   0  (uo=10 pos, vo=2 neg)
    0x0401,  // 0110  Yellow/N        4    0   1  (uo=2 neg, vo=9 pos)
    0x0303,  // 0111  White/N         3    1   1  (neutral: uo=8, vo=8)
    0x0B03,  // 1000  Black/B        11    1   1
    0x0902,  // 1001  Blue/B          9    1   0
    0x0701,  // 1010  Red/B           7    0   1
    0x0603,  // 1011  Magenta/B       6    1   1
    0x0400,  // 1100  Green/B         4    0   0
    0x0302,  // 1101  Cyan/B          3    1   0
    0x0101,  // 1110  Yellow/B        1    0   1
    0x0003,  // 1111  White/B         0    1   1
};

static inline uint8_t lut_yn(uint8_t idx) { return (colour_lut[idx & 0xF] >> 8) & 0xF; }
static inline uint8_t lut_uo(uint8_t idx) { return (colour_lut[idx & 0xF] >> 1) & 0x1; }
static inline uint8_t lut_vo(uint8_t idx) { return  colour_lut[idx & 0xF]       & 0x1; }

#define YN_SYNC_TIP   15
#define YN_BLACK      11
#define UO_NEUTRAL     1
#define VO_NEUTRAL     1
