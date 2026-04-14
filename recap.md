# ZX Spectrum RP2350B ULA — Project Status Summary

## Hardware

Board: WeAct Studio RP2350B, 252 MHz (VCO=1512 MHz, POSTDIV1=6, POSTDIV2=1)
Target: Replace Ferranti ULA with RP2350B acting as memory controller, video generator, CPU clock, and I/O decoder

### Pin Map (final)
GPIOFunctionGP0–6RA[6:0] DRAM row/col addressGP7–9/RAS, /CAS, /WEGP10–17D[7:0] bidirectional data busGP18–23/WR, /RD, /MREQ, /IO_ULA, A14, A15GP24/INT (PIO0 SM0 OUT)GP25CLOCK (PIO0 SM3 SET, free-running 3.5 MHz)GP26/ROM_CS (GPIO, Core 0)GP27SOUND (bidirectional)GP28–32T[4:0] keyboard matrixGP33–36YN[3:0] luma DACGP37UO (Cb sign)GP38VO (Cr sign)GP39–42R, G, B, BRIGHTGP43–44/HSYNC, /VSYNCGP45–47spare

## Architecture
Core 0 — video scanline loop

Waits on scanline_ready flag set by DMA IRQ
Outputs precomputed colour_out[] via ula_gpio_put_hi() (456 pixels per scanline)
Updates /ROM_CS and flash_cnt once per scanline
Prepares the idle ScanlineBuffer for the next scanline

Core 1 — I/O and contention

Port 0xFE decode (border colour, speaker, keyboard)
Contention: checks ula_fetch_active && !MREQ_N && !A15 && A14 every loop iteration. When true: spins until PIN_CLOCK HIGH, disables SM3 (clock frozen HIGH = Z80 wait state). Re-enables SM3 when condition clears.

PIO0 SM0 — sync_gen.pio — /INT bitstream, DMA-fed, 7 MHz
PIO0 SM1 — pixel_shift.pio — pixel bitstream, DMA-fed, 7 MHz (FIFO drain only; Core 0 owns video GPIO)
PIO0 SM3 — cpu_clock.pio — free-running SET toggle, 3.5 MHz, no FIFO
PIO1 SM0 — dram_ctrl.pio — DRAM sequencer, 252 MHz, DMA-fed command words
PIO1 SM1 — D-bus capture trigger (waits IRQ4, fires PIO1 IRQ0 → Core 0 ISR)
PIO1 SM2 — refresh (reuses dram_ctrl program)

DMA Channels
ChannelPurposech_syncint_n[] → PIO0 SM0 TX, triggers DMA_IRQ_0 on completionch_pixelpixel[] → PIO0 SM1 TXch_dramdram_cmds[] → PIO1 SM0 TX
Clock SM3 has no DMA — free-running.

Double-Buffered Scanline Buffers
ctypedef struct {
    uint32_t int_n[15];              // /INT bitstream
    uint32_t pixel[15];              // pixel bitstream
    uint32_t dram_cmds[66];          // DRAM command words
    uint32_t dram_cmd_count;
    uint32_t colour_out[456];        // precomputed GPIO HI values
} ScanlineBuffer;
ScanlineBuffer scanline_buf[2];
IRQ handler (minimal — fires every ~68 µs):

Clear dma_hw->ints0
Advance current_line, toggle active_buf
Set ula_fetch_active = (current_line < 192)
Call dram_begin_line(), ula_dma_start(), set scanline_ready = true
Does not call ula_scanline_prepare — Core 0 does that


# Shadow VRAM
volatile uint8_t shadow_vram[0x1B00] in dram.cpp

Written by D-bus capture ISR (PIO1 IRQ0 → Core 0)
dram_begin_line(line) arms capture at the start of each active scanline
Capture ISR tracks cap_col (0–31) and cap_phase (0=bitmap, 1=attr) to index into the ZX-scrambled address layout
ula_scanline_prepare snapshots 64 volatile bytes into bmp_cache[32] / atr_cache[32] before the pixel loop (avoids 512 volatile reads in the hot path)


Colour Output

All video pins GP33–44 are in the RP2350 high GPIO bank
ula_gpio_put_hi(mask, value) = hw_write_masked(&sio_hw->gpio_hi_out, value, mask)
colour_gpio_hi(lut_idx, hsync_n, vsync_n) builds the word from colour_lut[16]
sync_gpio_hi(hsync_n, vsync_n) outputs YN=15 (sync tip), UV neutral


Video Timing (PAL, pixel clocks at 7 MHz)
ConstantValuePIXELS_PER_LINE456TOTAL_LINES312ACTIVE_LINES192ACTIVE_PIXELS256HBLANK_START320HSYNC_START/END344/375VBLANK/VSYNC_START248

# Bugs Fixed (cumulative)

include/ula_dma.h (stale duplicate header) deleted — was causing dual ScanlineBuffer ABI split
dram.h command builder return types void → uint32_t
VIDEO_GPIO_MASK with shifts ≥32 → VIDEO_GPIO_HI_MASK with ula_gpio_put_hi()
scanline_ready, active_buf, current_line were static/undefined — fixed
flash_cnt double-increment (Core 0 and Core 1) — removed from Core 1
SM_SYNC/SM_PIXEL redefined locally in video_init.cpp — removed
pio_gpio_init(pio0, PIN_R) in SM1 conflicting with Core 0 GPIO ownership — removed
gpio_put_masked_hi SDK availability — replaced with hw_write_masked wrapper
PLL_SYS_VCO_FREQ_HZ etc. missing from CMakeLists — added to target_compile_definitions
ula_scanline_prepare called inside IRQ handler — moved to Core 0 only
Buffer indexing: Core 0 now correctly reads idle_buf (the just-finished buffer) and prepares it for next_line
512 volatile shadow_vram reads per active line — replaced with 64-read cache
CPU clock SM stalling on empty FIFO (DMA-fed approach) — replaced with free-running set pins toggle
Contention: was precomputed (wrong — can't know CPU address ahead of time) — now real-time via Core 1 SM pause/resume


# Outstanding / Not Yet Tested

Contention timing precision (Core 1 GPIO polling latency ~few ns vs. 142 ns half-period — should be fine)
DRAM RAS/CAS timing on actual 4116 chips
Colour LUT values (flagged as approximate pending oscilloscope measurement of real 6C001E-7)
U/V DAC expansion from 1-bit to 4-bit once measurements done
/ROM_CS logic (currently combinatorial once per scanline — may need tightening)
