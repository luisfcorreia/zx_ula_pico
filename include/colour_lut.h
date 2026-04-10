#pragma once
#include <stdint.h>

// =============================================================================
// colour_lut.h — ZX Spectrum 16-colour YUV DAC lookup table
//
// Index: {BRIGHT, G, R, B}  (4-bit, ZX Spectrum GRB attribute order)
//
// Packed uint16_t: bits[11:8]=yn(4-bit), bit[1]=uo(1-bit), bit[0]=vo(1-bit)
//
// /Y  (yn): 4-bit, 3.3V R-2R ladder.  11=black pedestal, 15=sync tip, 0=peak white.
// U   (uo): 1-bit sign.  1=positive or neutral Cb, 0=negative Cb.
// V   (vo): 1-bit sign.  1=positive or neutral Cr, 0=negative Cr.
//
// U and V are 1-bit placeholders pending oscilloscope calibration on a real
// 6C001E-7. The GP45–GP47 spare pins are reserved for expanding U and V to
// 4-bit once measured voltage levels are known.
//
// TODO: replace yn values with measurements from working 6C001E-7 ULA.
// Current yn values derived from BT.601 luma coefficients.
// =============================================================================

// Packed: bits[11:8]=yn, bit[1]=uo, bit[0]=vo
static const uint16_t colour_lut[16] = {
    //  {I,G,R,B}   Colour           yn  uo vo
    0x0B00,  // 0000  Black  (normal)  11   0  0  — neutral → uo=1,vo=1 but both 0 at neutral sign threshold
    0x0A03,  // 0001  Blue   (normal)  10   1  0  — Cb+ (blue), Cr- (blue)
    0x0800,  // 0010  Red    (normal)   8   0  1  — Cb- (red),  Cr+ (red)
    0x0803,  // 0011  Magenta(normal)   8   1  1  — Cb+ Cr+
    0x0600,  // 0100  Green  (normal)   6   0  0  — Cb- Cr-
    0x0602,  // 0101  Cyan   (normal)   5   1  0  — Cb+ Cr-
    0x0401,  // 0110  Yellow (normal)   4   0  1  — Cb- Cr+
    0x0303,  // 0111  White  (normal)   3   1  1  — neutral → both 1
    0x0B03,  // 1000  Black  (bright)  11   1  1  — neutral
    0x0903,  // 1001  Blue   (bright)   9   1  0
    0x0700,  // 1010  Red    (bright)   7   0  1
    0x0603,  // 1011  Magenta(bright)   6   1  1
    0x0400,  // 1100  Green  (bright)   4   0  0
    0x0302,  // 1101  Cyan   (bright)   3   1  0
    0x0101,  // 1110  Yellow (bright)   1   0  1
    0x0003,  // 1111  White  (bright)   0   1  1  — neutral
};

static inline uint8_t lut_yn(uint8_t idx) { return (colour_lut[idx & 0xF] >> 8) & 0xF; }
static inline uint8_t lut_uo(uint8_t idx) { return (colour_lut[idx & 0xF] >> 1) & 0x1; }
static inline uint8_t lut_vo(uint8_t idx) { return  colour_lut[idx & 0xF]       & 0x1; }

#define YN_SYNC_TIP   15
#define YN_BLACK      11
#define UO_NEUTRAL     1   // 1-bit neutral = 1 (positive/zero bias)
#define VO_NEUTRAL     1
