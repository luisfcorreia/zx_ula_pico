#pragma once

// Configure RP2350 PLL for 252 MHz system clock.
// Must be called before any PIO, GPIO, or peripheral initialisation.
// 252 MHz = 36 × 7 MHz — exact integer ratio gives zero-jitter pixel clock.
void clock_init_252mhz(void);
