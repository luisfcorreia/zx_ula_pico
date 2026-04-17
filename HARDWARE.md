# Hardware Pinout

## 40-pin Ferranti ULA to RP2350B GPIO mapping

Direct drop-in: RP2350B mounts on the same 40-pin DIP footprint as the Ferranti 6C001E-7.

| ULA Pin | Signal | Direction | RP2350B GPIO | Notes |
|---|---|---|---|---|
| 1 | /CAS | out | GP8 | DRAM column address strobe |
| 2 | /WR | in | GP18 | Z80 write strobe |
| 3 | /RD | in | GP19 | Z80 read strobe |
| 4 | /WE | out | GP9 | DRAM write enable |
| 5 | A0 | out | GP0 | DRAM address bit 0 |
| 6 | A1 | out | GP1 | DRAM address bit 1 |
| 7 | A2 | out | GP2 | DRAM address bit 2 |
| 8 | A3 | out | GP3 | DRAM address bit 3 |
| 9 | A4 | out | GP4 | DRAM address bit 4 |
| 10 | A5 | out | GP5 | DRAM address bit 5 |
| 11 | A6 | out | GP6 | DRAM address bit 6 |
| 12 | /INT | out | GP24 | Interrupt to Z80 |
| 15 | U (Cb) | out | GP37-GP39 | R-2R DAC, 3-bit |
| 16 | V (Cr) | out | GP40-GP42 | R-2R DAC, 3-bit |
| 17 | /Y | out | GP33-GP36 | R-2R DAC, 4-bit |
| 18 | D0 | bidir | GP10 | Data bus |
| 19 | T0 | in | GP28 | Keyboard column 0 |
| 20 | T1 | in | GP29 | Keyboard column 1 |
| 21 | D1 | bidir | GP11 | Data bus |
| 22 | D2 | bidir | GP12 | Data bus |
| 23 | T2 | in | GP30 | Keyboard column 2 |
| 24 | T3 | in | GP31 | Keyboard column 3 |
| 25 | D3 | bidir | GP13 | Data bus |
| 26 | T4 | in | GP32 | Keyboard column 4 |
| 27 | D4 | bidir | GP14 | Data bus |
| 28 | SOUND | bidir | GP27 | EAR in / MIC+SPK out |
| 29 | D5 | bidir | GP15 | Data bus |
| 30 | D6 | bidir | GP16 | Data bus |
| 31 | D7 | bidir | GP17 | Data bus |
| 32 | CLOCK | out | GP26 | 3.5 MHz contention-gated CPU clock |
| 33 | /IO-ULA | in | GP21 | Pre-decoded port select |
| 34 | /ROM CS | out | GP25 | ROM chip select (decoded by RP2350B) |
| 35 | /RAS | out | GP7 | DRAM row address strobe |
| 36 | A14 | in | GP22 | Z80 address bit 14 |
| 37 | A15 | in | GP23 | Z80 address bit 15 |
| 38 | /MREQ | in | GP20 | Z80 memory request |
| 39 | Q | in | - | 14 MHz crystal — not used, RP2350B generates own clocks |
| — | R (bonus) | out | GP43 | RGBi TTL, active during display only |
| — | G (bonus) | out | GP45 | RGBi TTL, active during display only |
| — | B (bonus) | out | GP44 | RGBi TTL, active during display only |
| — | BRIGHT (bonus) | out | GP46 | RGBi TTL, active during display only |
| — | CSYNC (bonus) | out | GP47 | RGBi TTL, composite sync (HSync AND VSync, active low) |
| — | — | — | — | (no spare pins remaining) |

Pins 13, 14, 40 (power/ground) are handled by the PCB as before.

---

## R-2R DAC for Composite Video

Three R-2R resistor ladder DACs. /Y is 4-bit. U and V are 3-bit (one pin freed per channel for RGBi bonus output). No external voltage reference needed.

### DAC bit assignment

```
GP33 = YN[0]  (LSB)      GP37 = UO[0]  (LSB)      GP40 = VO[0]  (LSB)
GP34 = YN[1]              GP38 = UO[1]              GP41 = VO[1]
GP35 = YN[2]              GP39 = UO[2]  (MSB)      GP42 = VO[2]  (MSB)
GP36 = YN[3]  (MSB)
```

### R-2R recommended values

```
R  = 1 kΩ   (feedback resistor)
2R = 2 kΩ   (bit switches: 1× 2k per bit, or 2× 1k in series)
```

All inputs should have pull-down resistors (~10 kΩ) to guarantee defined state at boot before GPIO is configured.

### Voltage levels (VREF = 3.3 V)

/Y: DAC output = VREF × DAC_value / 16 (4-bit, 16 levels)
U/V: DAC output = VREF × DAC_value / 8 (3-bit, 8 levels)

| DAC value | /Y (4-bit) voltage | U/V (3-bit) voltage |
|---|---|---|
| 15 | 3.062 V | — |
| 14 | 2.869 V | — |
| 13 | 2.675 V | — |
| 12 | 2.481 V | — |
| 11 | 2.288 V | — |
| 10 | 2.094 V | — |
| 9 | 1.900 V | — |
| 8 | 1.706 V | **3.300 V** (neutral) |
| 7 | 1.513 V | 2.887 V |
| 6 | 1.319 V | 2.475 V |
| 5 | 1.125 V | 2.062 V |
| 4 | 0.931 V | 1.650 V |
| 3 | 0.738 V | 1.237 V |
| 2 | 0.544 V | 0.825 V |
| 1 | 0.350 V | 0.412 V |
| 0 | 0.156 V | 0.000 V |

### Colour output levels (per ZX Spectrum 16-colour palette, 3-bit U/V quantized)

DAC values: yn=4-bit, uo/vo=3-bit (quantized: round(x/2)).

| Colour | BRIGHT | yn | uo | vo | /Y voltage | U voltage | V voltage |
|---|---|---|---|---|---|---|---|
| Black | 0 | 11 | 8 | 8 | 2.288 V | 3.300 V | 3.300 V |
| Blue | 0 | 10 | 7 | 4 | 2.094 V | 2.887 V | 1.650 V |
| Red | 0 | 8 | 3 | 7 | 1.706 V | 1.237 V | 2.887 V |
| Magenta | 0 | 7 | 6 | 7 | 1.513 V | 2.475 V | 2.887 V |
| Green | 0 | 6 | 2 | 2 | 1.319 V | 0.825 V | 0.825 V |
| Cyan | 0 | 5 | 5 | 1 | 1.125 V | 2.062 V | 0.412 V |
| Yellow | 0 | 4 | 1 | 5 | 0.931 V | 0.412 V | 2.062 V |
| White | 0 | 3 | 4 | 4 | 0.738 V | 1.650 V | 1.650 V |
| Black | 1 | 11 | 8 | 8 | 2.288 V | 3.300 V | 3.300 V |
| Blue | 1 | 9 | 7 | 4 | 1.900 V | 2.887 V | 1.650 V |
| Red | 1 | 7 | 3 | 7 | 1.513 V | 1.237 V | 2.887 V |
| Magenta | 1 | 6 | 6 | 7 | 1.319 V | 2.475 V | 2.887 V |
| Green | 1 | 4 | 2 | 2 | 0.931 V | 0.825 V | 0.825 V |
| Cyan | 1 | 3 | 5 | 1 | 0.738 V | 2.062 V | 0.412 V |
| Yellow | 1 | 1 | 1 | 5 | 0.350 V | 0.412 V | 2.062 V |
| White | 1 | 0 | 4 | 4 | 0.156 V | 1.650 V | 1.650 V |

### Composite video

Composite video is handled by the existing ZX Spectrum PCB circuit. The /Y, U, V outputs connect directly to the original circuit.

---

## RGBi Bonus TTL Outputs

Five TTL-level outputs for direct RGB drive. All are **active during the display area only** (gated off during blanking).

| Pin | Signal | Description |
|---|---|---|
| GP43 | R | Red — asserted when pixel colour has R=1 |
| GP44 | B | Blue — asserted when pixel colour has B=1 |
| GP45 | G | Green — asserted when pixel colour has G=1 |
| GP46 | BRIGHT | Intensity — asserted when BRIGHT=1 |
| GP47 | CSYNC | Composite sync — active low, mirrors HSync AND VSync |

### CSYNC polarity

CSYNC is **active low** (0 during sync, 1 otherwise). It reflects the logical AND of the horizontal and vertical sync pulses — the same composite sync signal used in standard PAL/RGBi circuits. This is the inverse of the sync embedded in /Y.

### RGBi recommended circuit

```
RP2350B               Monitor / SCART
GP43 (R)  ───330Ω───► Pin 15 (R)
GP44 (B)  ───330Ω───► Pin 13 (B)
GP45 (G)  ───330Ω───► Pin 11 (G)
GP46 (I)  ───330Ω───► Pin  5 (FBK) via 330Ω (add ~40% to all colours)
GP47 (CSYNC) ──────► Pin 20 (BLNK) or CSYNC input
GND        ─────────► Pin 17 (GND)
```

Series resistors (330Ω) limit current and provide approximately 75Ω source impedance when combined with the 75Ω termination of the monitor.

### Sync and blanking levels

- **Sync tip**: yn = 15 → 3.062 V on /Y (lowest voltage on composite after resistor mixing)
- **Black pedestal**: yn = 11 → 2.288 V on /Y (blanking level)
- **Bright white**: yn = 0 → 0.156 V on /Y (peak luma)
