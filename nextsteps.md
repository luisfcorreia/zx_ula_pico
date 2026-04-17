# Next Steps

## Goal

The core issue is that the C++ video loop takes ~130+ cycles when only 36 are available per pixel at 7 MHz. The solution requires implementing a PIO-based DRAM controller.

## Key Technical Requirements

The DRAM controller must exactly replicate the Verilog, with no frame buffer, no clock changes, and data sourced live from DRAM. The system runs at 252 MHz with 36 cycles per pixel tick at 7 MHz, and uses three PIO state machines (PIO0 SM0 for the 7 MHz tick, PIO0 SM1 for the 3.5 MHz CPU clock, and PIO1 for the DRAM controller), along with Core 0 for the video loop and Core 1 for I/O handling.

## Implementation Gaps

The main blocker is the incomplete `dram.pio` file, which only contains placeholder code. The video loop needs optimization to fit within the 20-cycle target, and the PIO1 IRQ and Core 0 ISR for data capture remain unimplemented. The Verilog reference (lines 261-286 for DRAM timing, lines 185-219 for control signals, and lines 291-311 for the pixel pipeline) should guide the full implementation.