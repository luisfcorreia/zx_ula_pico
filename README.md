# ZX Spectrum 48K ULA — RP2350B Drop-in Replacement

Project goal
Replace the Ferranti 6C001E-7 ULA chip in an unmodified ZX Spectrum 48K PCB with an RP2350B. The Pico boots before the Z80 gets a clock (ULA drives Z80 clock, so no clock until Pico is ready). RP2350B is 5V tolerant — no level shifting needed. Target 252 MHz overclock.

## Hardware facts established

*No pin 39 (14 MHz crystal) — RP2350B generates its own clocks via PLL
*252 MHz system clock — 12 MHz XOSC × 126 ÷ 6 = 252 MHz = 36 × 7 MHz (exact integer ratio, zero jitter pixel clock)
*7 MHz pixel clock — PIO divider = 36
*3.5 MHz CPU clock — contention-gated, driven by PIO
*RP2350B has 48 GPIO — all allocated, tight but sufficient
*3.3V outputs — 4-bit R-2R DAC per YUV channel, neutral = code 8 = 1.65V
*YUV lookup tables — 16 entries, indexed by {BRIGHT,G,R,B}, values pending oscilloscope measurement on real 6C001E-7

## Architecture
BlockResourceResponsibilityCore 0ARM Cortex-M33Raster loop, runs forever, one iteration per pixel clock (36 sys cycles budget)Core 1ARM Cortex-M33Port 0xFE read/write, border colour, sound, flash counter, keyboardPIO0 SM07 MHzSync generator (HSync, VSync, /INT)PIO0 SM17 MHzPixel shift register, RGBi outputPIO0 SM27 MHzYUV DAC outputPIO0 SM37 MHzCPU clock, contention-gatedPIO1 SM0252 MHzDRAM controller (RAS/CAS/WE, 4 ns resolution)PIO1 SM1252 MHzD bus capture (DataLatch/AttrLatch timing)PIO1 SM2252 MHzRAS-only refresh cycles

GPIO pin map
GP0–GP6   RA[6:0]      DRAM multiplexed address (output)
GP7       /RAS         DRAM row strobe (output)
GP8       /CAS         DRAM col strobe (output)
GP9       /WE          DRAM write enable (output)
GP10–GP17 D[7:0]       Data bus (bidirectional)
GP18      /WR          Z80 write (input)
GP19      /RD          Z80 read (input)
GP20      /MREQ        Z80 memory request (input)
GP21      /IO-ULA      Pre-decoded port select A0|/IORQ (input)
GP22      A14          Z80 address bit 14 (input)
GP23      A15          Z80 address bit 15 (input)
GP24      /INT         Interrupt to Z80 (output)
GP25      CLOCK        3.5 MHz CPU clock (output)
GP26      /ROM_CS      ROM chip select (output)
GP27      SOUND        EAR in / MIC+SPK out (bidirectional)
GP28–GP32 T[4:0]       Keyboard columns (input)
GP33–GP36 yn[3:0]      /Y DAC (output)
GP37–GP40 uo[3:0]      U Cb DAC (output)
GP41–GP44 vo[3:0]      V Cr DAC (output)
GP45      R            RGBi Red bonus (output)
GP46      G            RGBi Green bonus (output)
GP47      B            RGBi Blue bonus (output)

## Project file structure
```
zx_pico_ula/
├── CMakeLists.txt
├── include/
│   ├── pinmap.h          — all pin numbers and timing constants
│   └── colour_lut.h      — 16-entry YUV lookup table (TODO: scope values)
└── src/
    ├── main.cpp           — PLL init, GPIO init, PIO load, core launch
    ├── clock/
    │   └── clock.cpp      — 252 MHz PLL configuration
    ├── video/
    │   ├── video.cpp      — Core 0 raster loop (main engine)
    │   ├── sync.pio       — HSync/VSync/INT generator
    │   └── pixel.pio      — pixel shift register + RGBi
    ├── dram/
    │   ├── dram.cpp       — PIO1 init stub (TODO: complete)
    │   └── dram.pio       — RAS/CAS/WE sequencer at 252 MHz
    ├── cpu/
    │   ├── cpu.cpp        — PIO0 SM3 init stub (TODO: complete)
    │   └── cpu_clock.pio  — contention-gated 3.5 MHz clock
    └── io/
        └── io.cpp         — Core 1: port 0xFE, keyboard, sound, flash
```

## What works / what's next
Designed and stubbed:

Full GPIO map and direction logic
252 MHz PLL init
Core 0 raster loop structure with correct hc/vc counters, DRAM phase dispatch, pixel pipeline, colour LUT lookup, single masked GPIO write for all video outputs
Core 1 port 0xFE read/write, sound pin direction switching, flash counter
All four PIO programs (logic correct, not yet assembled/tested)
DRAM timing constants verified against 4116-4 datasheet

TODO for next session:

Complete dram_init() and cpu_init() — load PIO programs, configure SM pins, start SMs
Wire PIO0 SM0 IRQ as the pixel clock sync point for Core 0 (replace busy-wait placeholder)
Implement video_init() — load sync.pio and pixel.pio into PIO0
Fill colour_lut.h with real values from oscilloscope measurements on working 6C001E-7
Test DRAM RAS/CAS waveforms on bench before connecting to live Spectrum PCB
Carrier PCB design: 40-pin DIP adapter, R-2R ladders for YUV DAC, U/V attenuator for LM1889N compatibility
