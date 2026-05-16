#pragma once

// Initialise PIO1 SM1 for DRAM control output to GP0-GP9 at 7 MHz.
// Core 0 pushes computed DRAM words to the TX FIFO each pixel tick.
void dram_init(void);
