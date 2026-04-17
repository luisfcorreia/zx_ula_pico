# ZX Spectrum 48K ULA — RP2350B Drop-in Replacement

Replace the Ferranti 6C001E-7 ULA with an RP2350B on an unmodified ZX Spectrum 48K PCB. Mount the RP2350B on the same 40-pin DIP footprint.

## Build

```bash
mkdir -p build && cd build
cmake ..
make
```

Flash `build/zx_ula_pico.uf2` to the device.

Requirements:
- Board: `weact_studio_rp2350b_core`, platform: `rp2350-arm-s`
- pico-sdk is a git submodule: `git clone --recursive` or `git submodule update --init`
- USB and UART are disabled (no stdio)

## Clocks

Custom 252 MHz PLL: VCO=1512 MHz, POSTDIV1=6, POSTDIV2=1. 252 MHz = 36 x 7 MHz — exact integer ratio gives zero-jitter pixel clock.

## Architecture

| Resource | Responsibility |
|---|---|
| Core 0 | Raster loop — one iteration per 7 MHz pixel clock tick |
| Core 1 | Port 0xFE read/write, CPU contention, border colour, sound, keyboard |
| PIO0 SM0 | 7 MHz tick generator — gates Core 0 pixel loop via RX FIFO |
| PIO0 SM3 | 3.5 MHz CPU clock, contention-gated (disabled during ULA fetch) |
| PIO1 | Reserved |

Core 0 drives all DRAM signals directly via SIO GPIO. No PIO DRAM controller yet.

Reference Verilog: `reference/ula_zx48k.v`.

## GPIO Pin Map

| Pins | Function | Notes |
|---|---|---|
| GP0–GP6 | DRAM address A[6:0] | Output |
| GP7 | /RAS | DRAM row strobe, output |
| GP8 | /CAS | DRAM column strobe, output |
| GP9 | /WE | DRAM write enable, output |
| GP10–GP17 | D[7:0] | Data bus, bidirectional |
| GP18 | /WR | Z80 write strobe, input |
| GP19 | /RD | Z80 read strobe, input |
| GP20 | /MREQ | Z80 memory request, input |
| GP21 | /IO-ULA | Pre-decoded port select, input |
| GP22 | A14 | Z80 address bit 14, input |
| GP23 | A15 | Z80 address bit 15, input |
| GP24 | /INT | Interrupt to Z80, output |
| GP25 | /ROM CS | ROM chip select, output |
| GP26 | CLOCK | 3.5 MHz CPU clock, output |
| GP27 | SOUND | EAR in / MIC+SPK out, bidirectional |
| GP28–GP32 | T[4:0] | Keyboard columns, input |
| GP33–GP36 | YN[3:0] | /Y luma DAC (4-bit R-2R), output |
| GP37–GP39 | UO[2:0] | U Cb DAC (3-bit R-2R), output |
| GP40–GP42 | VO[2:0] | V Cr DAC (3-bit R-2R), output |
| GP43 | R | RGBi bonus, TTL, output |
| GP44 | B | RGBi bonus, TTL, output |
| GP45 | G | RGBi bonus, TTL, output |
| GP46 | BRIGHT | RGBi bonus, TTL, output |
| GP47 | CSYNC | RGBi bonus, composite sync, active low, output |

Pins 13, 14, 40 (power/ground) handled by PCB as before. Pin 39 (14 MHz crystal input) not used — RP2350B generates all clocks internally.

## Video Output

Two output modes:

**Composite video**: /Y, U, V go to three R-2R resistor ladder DACs on the ZX Spectrum PCB. The existing ZX Spectrum resistor network produces composite video from these signals. See `HARDWARE.md` for DAC levels.

**RGBi bonus**: Five TTL outputs (R, G, B, BRIGHT, CSYNC) on GP43–GP47. Active during display area only. CSYNC is HSync AND VSync, active low — the inverse of the sync embedded in /Y.

See `HARDWARE.md` for DAC values, colour palette, and recommended RGBi circuit.

## File Structure

```
.
├── CMakeLists.txt
├── HARDWARE.md           — pinout, DAC levels, RGBi circuit
├── reference/
│   └── ula_zx48k.v      — reference Verilog model
├── include/
│   ├── pinmap.h          — GPIO pin assignments and timing constants
│   └── colour_lut.h      — 16-entry YUV palette (3-bit U/V quantized)
└── src/
    ├── main.cpp          — PLL init, GPIO setup, core launch
    ├── clock/
    │   └── clock.cpp     — 252 MHz PLL configuration
    ├── video/
    │   ├── video.cpp     — Core 0 raster loop
    │   └── video_init.cpp— PIO0 SM0 init, colour tables
    ├── cpu/
    │   ├── cpu.cpp       — PIO0 SM3 init (3.5 MHz clock)
    │   ├── sync.pio      — 7 MHz tick generator
    │   └── cpu_clock.pio — 3.5 MHz CPU clock
    ├── dram/
    │   └── dram.cpp      — DRAM GPIO init (SIO, not PIO)
    └── io/
        └── io.cpp        — Core 1: port 0xFE, contention, keyboard, sound
```
