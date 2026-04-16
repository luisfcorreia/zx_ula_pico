#pragma once
#include <stdint.h>

// =============================================================================
// colour_lut.h — ZX Spectrum 16-colour YUV DAC lookup table
//
// Index: {BRIGHT[3], G[2], R[1], B[0]}  (ZX attribute GRB order)
//
// Packed uint32_t: bits[11:8]=yn, bits[7:4]=uo, bits[3:0]=vo
//
// YN values from Verilog Section 7 (exact):
//   sync tip=15, black pedestal=11, bright white=0
//
// UO (Cb) and VO (Cr) values from Verilog Section 7 BT.601 derivation:
//   Scale: 8=neutral, 14=max positive, 2=max negative
//   BRIGHT does not affect U or V.
//
// U table (indexed by {G,R,B}):
//   000=8  001=14  010=6   011=12  100=4   101=10  110=2   111=8
// V table (indexed by {G,R,B}):
//   000=8  001=7   010=14  011=13  100=3   101=2   110=9   111=8
// =============================================================================

// Pack: bits[11:8]=yn, bits[7:4]=uo, bits[3:0]=vo
#define LUTV(yn, uo, vo)  ((uint32_t)(((yn)<<8)|((uo)<<4)|(vo)))

static const uint32_t colour_lut[16] = {
    // {I,G,R,B}   Colour       yn  uo  vo
    LUTV(11, 8, 8),  // 0000  Black  (normal)
    LUTV(10,14, 7),  // 0001  Blue   (normal)
    LUTV( 8, 6,14),  // 0010  Red    (normal)
    LUTV( 7,12,13),  // 0011  Magenta(normal)
    LUTV( 6, 4, 3),  // 0100  Green  (normal)
    LUTV( 5,10, 2),  // 0101  Cyan   (normal)
    LUTV( 4, 2, 9),  // 0110  Yellow (normal)
    LUTV( 3, 8, 8),  // 0111  White  (normal)
    LUTV(11, 8, 8),  // 1000  Black  (bright — same as normal)
    LUTV( 9,14, 7),  // 1001  Blue   (bright)
    LUTV( 7, 6,14),  // 1010  Red    (bright)
    LUTV( 6,12,13),  // 1011  Magenta(bright)
    LUTV( 4, 4, 3),  // 1100  Green  (bright)
    LUTV( 3,10, 2),  // 1101  Cyan   (bright)
    LUTV( 1, 2, 9),  // 1110  Yellow (bright)
    LUTV( 0, 8, 8),  // 1111  White  (bright)
};

static inline uint8_t lut_yn(uint8_t idx) { return (colour_lut[idx & 0xF] >> 8) & 0xFu; }
static inline uint8_t lut_uo(uint8_t idx) { return (colour_lut[idx & 0xF] >> 4) & 0xFu; }
static inline uint8_t lut_vo(uint8_t idx) { return  colour_lut[idx & 0xF]       & 0xFu; }

#define YN_SYNC_TIP   15u
#define YN_BLACK      11u
#define UO_NEUTRAL     8u
#define VO_NEUTRAL     8u
