#pragma once

// Initialise DRAM GPIO pins (GP0-GP9) as SIO outputs, all deasserted.
// No PIO, no DMA — Core 0 drives these directly in video_run().
void dram_init(void);
