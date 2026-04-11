#pragma once

// Load cpu_clock.pio into PIO0 SM3 and configure the CLOCK output pin.
// The SM is started paused; video_raster_loop() feeds contention bits
// into its TX FIFO each half pixel-clock to drive the output.
void cpu_init(void);
