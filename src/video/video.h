#pragma once

// Load sync.pio and pixel.pio into PIO0, configure all video output pins,
// and start PIO0 SM0 (sync), SM1 (pixel), SM2 (YUV DAC).
// Must be called from Core 0 before video_raster_loop().
void video_init(void);

// Core 0 main loop — never returns.
// Runs one iteration per pixel clock (36 system cycles at 252 MHz).
// Owns: hc/vc counters, pixel pipeline, DRAM command dispatch,
//       YUV DAC writes, RGBi GPIO writes, /INT, /ROM_CS, contention bit.
[[noreturn]] void video_scanline_loop(void);
