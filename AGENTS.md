# ZX Spectrum 48K ULA — RP2350B Drop-in Replacement

Replace the Ferranti 6C001E-7 ULA with an RP2350B on an unmodified ZX Spectrum 48K PCB.

## Build

```bash
mkdir -p build && cd build
cmake ..
make
```

Flash `build/zx_ula_pico.uf2` to the device.

- Board: `weact_studio_rp2350b_core`, platform: `rp2350-arm-s`
- pico-sdk is a git submodule — `git clone --recursive` or `git submodule update --init`
- USB and UART are disabled (no stdio, no printf debugging)
- Custom 252 MHz PLL: VCO=1512 MHz, POSTDIV1=6, POSTDIV2=1 (exact 7 MHz pixel clock)
- No test framework, no CI, no linter — bare-metal firmware, verify on hardware

## Architecture

| Resource | Responsibility |
|---|---|
| Core 0 | Capture data bus bytes + compute DRAM words on-the-fly (PIO1 SM1), compute video words during hblank |
| Core 1 | Port 0xFE I/O, contention, border, keyboard, sound |
| PIO0 SM0 | 7 MHz tick generator (gates Core 0 loop) |
| PIO0 SM1 | 3.5 MHz CPU clock, contention-gated |
| PIO1 SM0 | Video output — DMA-streamed 32-bit GPIO HI words to GP33–GP47 at 7 MHz |
| PIO1 SM1 | DRAM control — Core 0 pushes 32-bit GPIO LO words to GP0–GP9 at 7 MHz |
| DMA CH0 | Streams precomputed video buffer → PIO1 SM0 TX FIFO (DREQ-paced) |
| DMA CH1 | Chain-reload for double-buffer swap |

**Pipeline (one-line delay):**
1. Line N active display → Core 0 captures data bus bytes (phases 0x0, 0xB) + computes DRAM words on-the-fly
2. Hblank (hc 320-415) → Core 0 computes 448 video words from capture buffer + template
3. Line N+1 active display → DMA streams video words to PIO1 SM0, Core 0 captures line N+1

Reference Verilog: `reference/ula_zx48k.v`. Hardware details (DAC levels, RGBi circuit): `HARDWARE.md`.

## Execution model

- `main.cpp`: 252 MHz init → GPIO setup → subsystem init → start PIO0 SM0+SM1 + PIO1 SM0+SM1 in sync → launch Core 1 → `video_run()` (never returns)
- `video_run()`: capture loop + DRAM word computation on-the-fly, hblank video word computation + DMA swap
- `io_core1_entry()`: contention polling + port 0xFE read/write on Core 1, uses `tight_loop_contents()` busy-waits for timing
- Shared state (all `volatile`):
  - `shared_border_color` (uint8_t) — Core 1 writes, Core 0 reads each tick
  - `shared_sound_out` (uint8_t) — Core 1 writes, drives SOUND pin direction
  - `ula_fetch_window` (bool) — Core 0 sets each tick, Core 1 reads for contention gating

## Code structure

```
include/
  pinmap.h          — GPIO assignments, timing constants, DMA/SM constants
  colour_lut.h      — 16-entry YUV palette (3-bit U/V quantized)
src/
  main.cpp          — bootstrap
  clock/clock.cpp   — 252 MHz PLL
  video/video.cpp   — Core 0 capture loop + hblank compute + DMA swap
  video/video_init.cpp — PIO0 SM0 tick + PIO1 SM0 video + DMA init, colour/template tables
  video/video.h     — build_video_word(), build_colour_word(), extern shared state
  video/video.pio   — PIO1 SM0 video output program (out pins, 32)
  cpu/cpu.cpp       — PIO0 SM1 init (3.5 MHz CPU clock)
  cpu/sync.pio      — 7 MHz tick generator
  cpu/cpu_clock.pio — 3.5 MHz CPU clock
  dram/dram.cpp     — PIO1 SM1 DRAM control init
  dram/dram.pio     — PIO1 SM1 DRAM control program (out pins, 32)
  io/io.cpp         — Core 1: port 0xFE, contention, keyboard, sound
```

## Conventions

- Video/DRAM outputs stream through PIO1 — Core 0 pushes 32-bit words to FIFOs, never writes GPIO directly
- `/INT` and `/ROM_CS` still driven by SIO GPIO from Core 0 (low-frequency signals)
- Data bus GP10–GP17: bidirectional SIO, driven by Core 1 for port 0xFE I/O
- YUV video: GP33–36=YN[3:0], GP37–39=UO[2:0], GP40–42=VO[2:0]
- RGBi bonus: GP43=R, GP44=G, GP45=B, GP46=BRIGHT, GP47=CSYNC (active low)
- `.pio` files auto-generated to `${CMAKE_BINARY_DIR}` by `pico_generate_pio_header()`
- PIO programs use `out pins, 32` with `autopull=true` — Core 0 pushes full 32-bit words
- No emojis, minimal comments, concise variable names, match existing style
