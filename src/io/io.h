#pragma once

// Core 1 entry point — launched via multicore_launch_core1().
// Polls /IO-ULA, /RD, /WR continuously.
// Owns: port 0xFE read/write, border_colour, sound pin direction,
//       flash counter increment on VSync edge, keyboard column sampling.
// Never returns.
[[noreturn]] void io_core1_entry(void);
