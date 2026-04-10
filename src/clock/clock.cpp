#include "clock.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "pico/stdlib.h"

// 252 MHz = 12 MHz XOSC × 126 ÷ 6
// VCO = 1512 MHz, POSTDIV1=6, POSTDIV2=1
void clock_init_252mhz(void) {
    clock_configure(clk_sys,
        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_XOSC_CLKSRC,
        12 * MHZ, 12 * MHZ);
    pll_deinit(pll_sys);
    pll_init(pll_sys, 1, 1512 * MHZ, 6, 1);
    clock_configure(clk_sys,
        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        252 * MHZ, 252 * MHZ);
}
