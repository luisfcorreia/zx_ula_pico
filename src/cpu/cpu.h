#pragma once

// Load cpu_clock.pio into PIO0 SM3 and configure PIN_CLOCK.
// SM is left stopped; main.cpp starts it via pio_enable_sm_mask_in_sync().
void cpu_init(void);

// SM index and program offset — needed by io.cpp for contention pause/resume
#define SM_CPUCLK           3
extern unsigned int cpu_clock_program_offset;
