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
| 15 | U (Cb) | out | GP37-GP40 | R-2R DAC, see below |
| 16 | V (Cr) | out | GP41-GP44 | R-2R DAC, see below |
| 17 | /Y | out | GP33-GP36 | R-2R DAC, see below |
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
| — | — | — | GP45-GP47 | Spare |

Pins 13, 14, 40 (power/ground) are handled by the PCB as before.

---

## R-2R DAC for Composite Video

Three independent 4-bit R-2R resistor ladder DACs, one per component. Each uses 4 GPIO pins at 3.3V logic levels. No external voltage reference needed.

### DAC bit assignment

```
GP33 = YN[0]  (LSB)      GP37 = UO[0]  (LSB)      GP41 = VO[0]  (LSB)
GP34 = YN[1]              GP38 = UO[1]              GP42 = VO[1]
GP35 = YN[2]              GP39 = UO[2]              GP43 = VO[2]
GP36 = YN[3]  (MSB)       GP40 = UO[3]  (MSB)       GP44 = VO[3]  (MSB)
```

### R-2R recommended values

```
R  = 1 kΩ   (feedback resistor)
2R = 2 kΩ   (bit switches: 1× 2k per bit, or 2× 1k in series)
```

All inputs should have pull-down resistors (~10 kΩ) to guarantee defined state at boot before GPIO is configured.

### Voltage levels (VREF = 3.3 V)

DAC output = VREF × DAC_value / 16

| DAC value | /Y (YN) voltage | U (UO) voltage | V (VO) voltage |
|---|---|---|---|
| 15 | 3.062 V | 3.062 V | 3.062 V |
| 14 | 2.869 V | 2.869 V | 2.869 V |
| 13 | 2.675 V | 2.675 V | 2.675 V |
| 12 | 2.481 V | 2.481 V | 2.481 V |
| 11 | 2.288 V | 2.288 V | 2.288 V |
| 10 | 2.094 V | 2.094 V | 2.094 V |
| 9 | 1.900 V | 1.900 V | 1.900 V |
| 8 | 1.706 V | **neutral** (0 V chroma) | **neutral** (0 V chroma) |
| 7 | 1.513 V | 1.513 V | 1.513 V |
| 6 | 1.319 V | 1.319 V | 1.319 V |
| 5 | 1.125 V | 1.125 V | 1.125 V |
| 4 | 0.931 V | 0.931 V | 0.931 V |
| 3 | 0.738 V | 0.738 V | 0.738 V |
| 2 | 0.544 V | 0.544 V | 0.544 V |
| 1 | 0.350 V | 0.350 V | 0.350 V |
| 0 | 0.156 V | 0.156 V | 0.156 V |

### Colour output levels (per ZX Spectrum 16-colour palette)

DAC values are set per attribute byte {BRIGHT, G, R, B} in GRB order.

| Colour | BRIGHT | yn | uo | vo | /Y voltage | U voltage | V voltage |
|---|---|---|---|---|---|---|---|
| Black | 0 | 11 | 8 | 8 | 2.288 V | 1.706 V | 1.706 V |
| Blue | 0 | 10 | 14 | 7 | 2.094 V | 2.869 V | 1.513 V |
| Red | 0 | 8 | 6 | 14 | 1.706 V | 1.319 V | 2.869 V |
| Magenta | 0 | 7 | 12 | 13 | 1.513 V | 2.481 V | 2.675 V |
| Green | 0 | 6 | 4 | 3 | 1.319 V | 0.931 V | 0.738 V |
| Cyan | 0 | 5 | 10 | 2 | 1.125 V | 2.094 V | 0.544 V |
| Yellow | 0 | 4 | 2 | 9 | 0.931 V | 0.544 V | 1.900 V |
| White | 0 | 3 | 8 | 8 | 0.738 V | 1.706 V | 1.706 V |
| Black | 1 | 11 | 8 | 8 | 2.288 V | 1.706 V | 1.706 V |
| Blue | 1 | 9 | 14 | 7 | 1.900 V | 2.869 V | 1.513 V |
| Red | 1 | 7 | 6 | 14 | 1.513 V | 1.319 V | 2.869 V |
| Magenta | 1 | 6 | 12 | 13 | 1.319 V | 2.481 V | 2.675 V |
| Green | 1 | 4 | 4 | 3 | 0.931 V | 0.931 V | 0.738 V |
| Cyan | 1 | 3 | 10 | 2 | 0.738 V | 2.094 V | 0.544 V |
| Yellow | 1 | 1 | 2 | 9 | 0.350 V | 0.544 V | 1.900 V |
| White | 1 | 0 | 8 | 8 | 0.156 V | 1.706 V | 1.706 V |

### Composite video generation

Route /Y, U, V to three separate R-2R ladders. Combine the three DAC outputs with external resistors to produce a composite video signal:

```
/Y ──[75Ω]──+── COMP
U  ──[470Ω]──+── COMP
V  ──[470Ω]──+
             |
           [75Ω]
             |
            GND
```

The /Y output is **inverted**: high DAC value = low voltage = sync tip. The external resistor network adds the chroma subcarrier to the luma. The 75Ω output resistor provides the 75Ω source impedance required by the composite video standard.

For a clean composite output, buffer with an op-amp (e.g. LM733 or equivalent) before the TV input.

### Sync and blanking levels

- **Sync tip**: yn = 15 → 3.062 V on /Y (lowest voltage on composite after resistor mixing)
- **Black pedestal**: yn = 11 → 2.288 V on /Y (blanking level)
- **Bright white**: yn = 0 → 0.156 V on /Y (peak luma)
