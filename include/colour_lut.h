#pragma once
#include <stdint.h>

// =============================================================================
// colour_lut.h — ZX Spectrum 16-colour YUV DAC lookup table
//
// Index: {BRIGHT[3], G[2], R[1], B[0]}  (ZX attribute GRB order)
//
// Packed uint32_t: bits[11:8]=yn, bits[6:4]=uo[2:0], bits[2:0]=vo[2:0]
//
// YN: 4-bit (0–15), UO/VO: 3-bit (0–7), quantized from 4-bit original.
// Quantization: round(x / 2) maps 4-bit (0–15) to 3-bit (0–7).
// Neutral (4) stays at 4 (midpoint of 0–7 range).
//
// =============================================================================
//
// --- 4-BIT ORIGINAL (commented out, retained for reference) ---
//
// UO (4-bit):  Scale: 8=neutral, 14=max positive, 2=max negative
// VO (4-bit):  Scale: 8=neutral, 14=max positive, 2=max negative
//
// static const uint32_t colour_lut_4bit[16] = {
//     LUTV(11, 8, 8),  // 0000  Black  (normal)
//     LUTV(10,14, 7),  // 0001  Blue   (normal)
//     LUTV( 8, 6,14),  // 0010  Red    (normal)
//     LUTV( 7,12,13),  // 0011  Magenta(normal)
//     LUTV( 6, 4, 3),  // 0100  Green  (normal)
//     LUTV( 5,10, 2),  // 0101  Cyan   (normal)
//     LUTV( 4, 2, 9),  // 0110  Yellow (normal)
//     LUTV( 3, 8, 8),  // 0111  White  (normal)
//     LUTV(11, 8, 8),  // 1000  Black  (bright — same as normal)
//     LUTV( 9,14, 7),  // 1001  Blue   (bright)
//     LUTV( 7, 6,14),  // 1010  Red    (bright)
//     LUTV( 6,12,13),  // 1011  Magenta(bright)
//     LUTV( 4, 4, 3),  // 1100  Green  (bright)
//     LUTV( 3,10, 2),  // 1101  Cyan   (bright)
//     LUTV( 1, 2, 9),  // 1110  Yellow (bright)
//     LUTV( 0, 8, 8),  // 1111  White  (bright)
// };
//
// =============================================================================

static const uint32_t colour_lut[16] = {
    // {I,G,R,B}   Colour       yn  uo  vo    (uo/vo 3-bit: neutral=4, quantized: round(x/2))
    (11u<<8)|(4u<<4)|(4u),  // 0000  Black  (normal)
    (10u<<8)|(7u<<4)|(4u),  // 0001  Blue   (normal)
    ( 8u<<8)|(3u<<4)|(7u),  // 0010  Red    (normal)
    ( 7u<<8)|(6u<<4)|(7u),  // 0011  Magenta(normal)
    ( 6u<<8)|(2u<<4)|(2u),  // 0100  Green  (normal)
    ( 5u<<8)|(5u<<4)|(1u),  // 0101  Cyan   (normal)
    ( 4u<<8)|(1u<<4)|(5u),  // 0110  Yellow (normal)
    ( 3u<<8)|(4u<<4)|(4u),  // 0111  White  (normal)
    (11u<<8)|(4u<<4)|(4u),  // 1000  Black  (bright — same as normal)
    ( 9u<<8)|(7u<<4)|(4u),  // 1001  Blue   (bright)
    ( 7u<<8)|(3u<<4)|(7u),  // 1010  Red    (bright)
    ( 6u<<8)|(6u<<4)|(7u),  // 1011  Magenta(bright)
    ( 4u<<8)|(2u<<4)|(2u),  // 1100  Green  (bright)
    ( 3u<<8)|(5u<<4)|(1u),  // 1101  Cyan   (bright)
    ( 1u<<8)|(1u<<4)|(5u),  // 1110  Yellow (bright)
    ( 0u<<8)|(4u<<4)|(4u),  // 1111  White  (bright)
};

static inline uint8_t lut_yn(uint8_t idx) { return (colour_lut[idx & 0xF] >> 8) & 0xFu; }
static inline uint8_t lut_uo(uint8_t idx) { return (colour_lut[idx & 0xF] >> 4) & 7u; }
static inline uint8_t lut_vo(uint8_t idx) { return (colour_lut[idx & 0xF]      ) & 7u; }

#define YN_SYNC_TIP   15u
#define YN_BLACK      11u
#define UO_NEUTRAL     4u
#define VO_NEUTRAL     4u
