// =============================================================================
// ZX Spectrum 48K ULA — Drop-in CPLD Replacement
// Target: Xilinx XC95144XL (144 macrocells, 5V-tolerant I/O)
// Pin-compatible with Ferranti ULA 6C001E-7 (40-pin DIP)
//
// Input clock: 14 MHz exact (PAL timing: 448 cycles/line = 64 µs)
//
// 40-PIN SIGNAL MAPPING (Ferranti 6C001E-7 — exact)
// =============================================================================
//
//  Pin  1  cas_n      /CAS  DRAM column address strobe    (output)
//  Pin  2  wr_n       /WR   CPU write strobe              (input)
//  Pin  3  rd_n       /RD   CPU read  strobe              (input)
//  Pin  4  we_n       /WE   DRAM write enable             (output)
//  Pin  5  a[0]       A0    DRAM multiplexed address 0    (output)
//  Pin  6  a[1]       A1    DRAM multiplexed address 1    (output)
//  Pin  7  a[2]       A2    DRAM multiplexed address 2    (output)
//  Pin  8  a[3]       A3    DRAM multiplexed address 3    (output)
//  Pin  9  a[4]       A4    DRAM multiplexed address 4    (output)
//  Pin 10  a[5]       A5    DRAM multiplexed address 5    (output)
//  Pin 11  a[6]       A6    DRAM multiplexed address 6    (output)
//  Pin 12  int_n      /INT  Interrupt to CPU              (output)
//  Pin 13  —          +5V   Power (not in module)
//  Pin 14  —          +5V   Power (not in module)
//  Pin 15  u          U     Colour-difference Cb          (output)
//  Pin 16  v          V     Colour-difference Cr          (output)
//  Pin 17  y_n        /Y    Inverted luma + composite sync(output)
//  Pin 18  d[0]       D0    Data bus bit 0                (bidirectional)
//  Pin 19  t[0]       T0    Keyboard column 0             (input)
//  Pin 20  t[1]       T1    Keyboard column 1             (input)
//  Pin 21  d[1]       D1    Data bus bit 1                (bidirectional)
//  Pin 22  d[2]       D2    Data bus bit 2                (bidirectional)
//  Pin 23  t[2]       T2    Keyboard column 2             (input)
//  Pin 24  t[3]       T3    Keyboard column 3             (input)
//  Pin 25  d[3]       D3    Data bus bit 3                (bidirectional)
//  Pin 26  t[4]       T4    Keyboard column 4             (input)
//  Pin 27  d[4]       D4    Data bus bit 4                (bidirectional)
//  Pin 28  sound      SOUND Audio I/O: EAR in / MIC+SPK out (bidirectional)
//  Pin 29  d[5]       D5    Data bus bit 5                (bidirectional)
//  Pin 30  d[6]       D6    Data bus bit 6                (bidirectional)
//  Pin 31  d[7]       D7    Data bus bit 7                (bidirectional)
//  Pin 32  clock      CLOCK 3.5 MHz contention-gated CPU clock (output)
//  Pin 33  io_ula_n   /IO-ULA Pre-decoded port select: A0|/IORQ (input)
//  Pin 34  rom_cs_n   /ROM CS ROM chip select (output, decoded by ULA)
//  Pin 35  ras_n      /RAS  DRAM row address strobe       (output)
//  Pin 36  a14        A14   Z80 address bit 14            (input)
//  Pin 37  a15        A15   Z80 address bit 15            (input)
//  Pin 38  mreq_n     /MREQ CPU memory request            (input)
//  Pin 39  q          Q     14.318 MHz oscillator input → GCK pin
//  Pin 40  —          GND   Ground (not in module)
//
`timescale 1ns / 1ps

`define cyclestart(a,b)  ((a)==(b))
`define cycleend(a,b)    ((a)==(b+1))
`define phase            (hc[3:0])

module ula_zx48k (
    // Clocks
    input        q,          // 14 MHz oscillator (must be on GCK pin)
    output       clock,      // 3.5 MHz contention-gated CPU clock

    // Z80 CPU interface
    input        wr_n,
    input        rd_n,
    input        mreq_n,
    input        io_ula_n,
    input        a14,
    input        a15,
    output       int_n,

    // DRAM interface (Bank 0: 0x4000–0x7FFF)
    output [6:0] a,
    output       ras_n,
    output       cas_n,
    output       we_n,

    // ROM chip select
    output       rom_cs_n,

    // Data bus (bidirectional)
    inout  [7:0] d,

    // Keyboard columns
    input  [4:0] t,

    // Sound (bidirectional)
    inout        sound,

    // Video outputs (original 6C001E pins) — 4-bit DAC words
    // Connect each [3:0] bus to a 4-bit R-2R resistor ladder (R=1kΩ, see below).
    // bit3=MSB, bit0=LSB.  Add 75Ω series output resistor for TV/composite load.
    //
    // /Y R-2R ladder (inverted luma + sync):
    //   DAC 15 = sync tip (most +ve on /Y pin)   DAC 0 = bright white (most –ve)
    //   DAC 11 = black pedestal / blanking level
    //   PAL levels: sync=0 mV, black=300 mV, bright white=1000 mV on composite out
    //
    // U/V R-2R ladders (Cb, Cr colour difference):
    //   DAC  8 = neutral (no chroma, achromatic colours)
    //   DAC 14 = maximum positive excursion  (Blue for U, Red for V)
    //   DAC  2 = maximum negative excursion  (Yellow for U, Cyan for V)
    output [3:0] yn,         // /Y  inverted luma + sync (4-bit DAC)
    output [3:0] uo,         // U   Cb colour-difference (4-bit DAC)
    output [3:0] vo,         // V   Cr colour-difference (4-bit DAC)

    // Bonus RGBi outputs (not on original 6C001E — route to spare CPLD pins)
    // These are the primary source of luminance information.
    // For composite video, mix through external resistor network:
    //   R  → 750 Ω  → luma bus
    //   G  → 390 Ω  → luma bus   (Green weighted ~2× Red/Blue for BT.601 luma)
    //   B  → 1k5 Ω  → luma bus
    //   BRIGHT → adds ~40% amplitude boost when combined with the above
    //   /Y → 75  Ω  → luma bus   (adds sync tip and black pedestal offset)
    // Resulting composite: proper multi-level luma + sync on a single wire.
    output       r,          // Red   (TTL, gated off during blanking)
    output       g,          // Green (TTL, gated off during blanking)
    output       b,          // Blue  (TTL, gated off during blanking)
    output       bright      // Intensity/Bright (TTL, gated off during blanking)
);

// =========================================================================
// SECTION 1: PIXEL CLOCK (7 MHz from 14 MHz input)
// =========================================================================
reg clk7 = 0;
always @(posedge q) clk7 <= ~clk7;

// =========================================================================
// SECTION 2: RASTER COUNTERS (448 cycles/line, 312 lines/frame)
// =========================================================================
reg [8:0] hc = 0;
always @(posedge clk7) begin
    if (hc == 447) hc <= 0;
    else           hc <= hc + 1;
end

reg [8:0] vc = 0;
always @(posedge clk7) begin
    if (hc == 447) begin
        if (vc == 311) vc <= 0;
        else           vc <= vc + 1;
    end
end

// =========================================================================
// SECTION 3: SYNC, BLANKING AND INTERRUPT
// =========================================================================
reg HBlank_n = 1;
always @(negedge clk7) begin
    if      (`cyclestart(hc, 320)) HBlank_n <= 0;
    else if (`cycleend(hc, 415))   HBlank_n <= 1;
end

reg HSync_n = 1;
always @(negedge clk7) begin
    if      (`cyclestart(hc, 344)) HSync_n <= 0;
    else if (`cycleend(hc, 375))   HSync_n <= 1;
end

reg VBlank_n = 1;
always @(negedge clk7) begin
    if      (`cyclestart(vc, 248)) VBlank_n <= 0;
    else if (`cycleend(vc, 255))   VBlank_n <= 1;
end

reg VSync_n = 1;
always @(negedge clk7) begin
    if      (`cyclestart(vc, 248)) VSync_n <= 0;
    else if (`cycleend(vc, 251))   VSync_n <= 1;
end

wire csync_n = HSync_n & VSync_n;

reg INT_r = 1;
assign int_n = INT_r;
always @(negedge clk7) begin
    if      (`cyclestart(vc,248) && `cyclestart(hc, 0))  INT_r <= 0;
    else if (`cyclestart(vc,248) && `cycleend(hc, 31))   INT_r <= 1;
end

assign rom_cs_n = a15 | a14 | mreq_n;

// =========================================================================
// SECTION 4: DISPLAY WINDOW AND FETCH CONTROL SIGNALS
// =========================================================================
reg Border_n = 1;
always @(negedge clk7) begin
    if ((vc[7] & vc[6]) | vc[8] | hc[8]) Border_n <= 0;
    else                                 Border_n <= 1;
end

reg VidEN_n = 1;
always @(negedge clk7) begin
    if (hc[3]) VidEN_n <= ~Border_n;
end

reg DataLatch_n = 1;
always @(negedge clk7) begin
    if (`phase == 4'b1011 && Border_n) DataLatch_n <= 0;
    else                               DataLatch_n <= 1;
end

reg AttrLatch_n = 1;
always @(negedge clk7) begin
    if (`phase == 4'b0000 && Border_n) AttrLatch_n <= 0;
    else                               AttrLatch_n <= 1;
end

reg SLoad = 0;
always @(negedge clk7) begin
    if (`phase == 4'b0100 && ~VidEN_n) SLoad <= 1;
    else                               SLoad <= 0;
end

reg AOLatch_n = 1;
always @(negedge clk7) begin
    if (`phase == 4'b0101) AOLatch_n <= 0;
    else                   AOLatch_n <= 1;
end

// =========================================================================
// SECTION 5: DRAM CONTROLLER (ULTRA-COMPACT FOR XC95144XL)
// =========================================================================
// Snapshot only needed bits of vc and hc (13 flip‑flops total)
reg [1:0] v_hi = 0;
reg [2:0] v_mid = 0;
reg [2:0] v_lo = 0;
reg [4:0] c_hi = 0;

always @(negedge clk7) begin
    if (Border_n && (`phase == 4'b0111 || `phase == 4'b1011)) begin
        c_hi <= hc[7:3];
        v_hi <= vc[7:6];
        v_mid <= vc[5:3];
        v_lo <= vc[2:0];
    end
end

wire [13:0] pix_addr  = {1'b0, v_hi, v_lo, v_mid, c_hi};
wire [13:0] attr_addr = {4'b0110, v_hi, v_mid, c_hi};
wire cpu_dram = ~mreq_n & ~a15 & a14;

// Phase bits
wire ph3 = hc[3];
wire ph2 = hc[2];
wire ph1 = hc[1];
wire ph0 = hc[0];
wire cpu_cycle = ~ph3 & (ph2 | ph1 | ph0);
wire cpu_ras_act = cpu_cycle & (ph2 & ~ph1);
wire cpu_cas_act = cpu_cycle & (ph2 & ph1 & ~ph0);

reg [6:0] a_r   = 0;
reg       ras_r = 1;
reg       cas_r = 1;
reg       we_r  = 1;
assign a = a_r;
assign ras_n = ras_r;
assign cas_n = cas_r;
assign we_n  = we_r;

always @(negedge clk7) begin
    // Default idle state
    a_r <= 0; ras_r <= 1; cas_r <= 1; we_r <= 1;

    if (cpu_cycle) begin
        if (cpu_ras_act) ras_r <= cpu_dram ? 0 : 1;
        if (cpu_cas_act) begin
            cas_r <= cpu_dram ? 0 : 1;
            we_r  <= (cpu_dram & ~wr_n) ? 0 : 1;
        end
    end else if (Border_n) begin
        // ULA fetch in active display
        ras_r <= 0;
        if (~ph2) begin   // phases 8-11: pixel
            a_r <= (ph0 ? pix_addr[6:0] : pix_addr[13:7]);
            if (ph1 & ~ph0) cas_r <= 0; // phases 10,11
        end else begin     // phases 12-15: attribute
            a_r <= (ph0 ? attr_addr[6:0] : attr_addr[13:7]);
            if (ph1 & ph0) cas_r <= 0;  // phase 15
        end
    end else if (ph3 & ~ph2 & ~ph1) begin   // phases 8,9,10 during blanking (RAS-only refresh)
        a_r <= hc[6:0];
        ras_r <= (ph1 & ~ph0) ? 1 : 0;     // phase 10: /RAS high, else low
        // cas_r stays high
    end
end

// =========================================================================
// SECTION 6: PIXEL DATA PIPELINE
// =========================================================================
reg [7:0] BitmapReg = 0;
always @(negedge clk7) if (~DataLatch_n) BitmapReg <= d;

reg [7:0] SRegister = 0;
always @(negedge clk7) begin
    if (SLoad) SRegister <= BitmapReg;
    else       SRegister <= {SRegister[6:0], 1'b0};
end

reg [7:0] AttrReg = 0;
always @(negedge clk7) if (~AttrLatch_n) AttrReg <= d;

reg [2:0] BorderColor = 3'b100;   // blue border at reset

reg [7:0] AttrOut = 0;
always @(negedge clk7) begin
    if (~AOLatch_n) begin
        if (~VidEN_n) AttrOut <= AttrReg;
        else          AttrOut <= {2'b00, BorderColor, BorderColor};
    end
end

reg [4:0] FlashCnt = 0;
reg VSync_n_prev = 1;
always @(negedge clk7) begin
    VSync_n_prev <= VSync_n;
    if (VSync_n_prev & ~VSync_n) FlashCnt <= FlashCnt + 1;
end

wire Pixel = SRegister[7] ^ (AttrOut[7] & FlashCnt[4]);

// =========================================================================
// SECTION 7: RGBi AND YUV VIDEO OUTPUT
//
// Colour bit extraction — ZX Spectrum attribute byte (GRB order, not RGB):
//   [7] FLASH  [6] BRIGHT  [5]=G [4]=R [3]=B (paper)  [2]=G [1]=R [0]=B (ink)
// =========================================================================

wire ink_G   = AttrOut[2];  wire paper_G = AttrOut[5];
wire ink_R   = AttrOut[1];  wire paper_R = AttrOut[4];
wire ink_B   = AttrOut[0];  wire paper_B = AttrOut[3];

// Select displayed colour (ink or paper, after flash XOR)
wire col_G = Pixel ? ink_G : paper_G;
wire col_R = Pixel ? ink_R : paper_R;
wire col_B = Pixel ? ink_B : paper_B;
wire col_I = AttrOut[6];   // BRIGHT/intensity

wire active_video = HBlank_n & VBlank_n;
wire sync_active  = ~csync_n;

// =========================================================================
// RGBi BONUS OUTPUTS — gated off during blanking
// =========================================================================
assign r      = active_video & col_R;
assign g      = active_video & col_G;
assign b      = active_video & col_B;
assign bright = active_video & col_I;

// =========================================================================
// /Y — 4-bit DAC lookup (indexed by {col_I, col_G, col_R, col_B})
//
// Derived from PAL composite video levels and BT.601 luma coefficients:
//   Y = 0.299·R + 0.587·G + 0.114·B
// Normal (N) colours use 71.4% amplitude; Bright (B) use 100%.
// Pedestal (black/blank) = 300 mV = DAC 11.  Sync tip = 0 mV = DAC 15.
// Mapping: DAC = round(15 × (1 - V_composite/1000mV))
//
//   {I,G,R,B}  Colour         Composite(mV)  DAC
//   0000        Black/N        300            11
//   0001        Blue/N         357            10
//   0010        Red/N          449             8
//   0011        Magenta/N      506             7
//   0100        Green/N        593             6
//   0101        Cyan/N         650             5
//   0110        Yellow/N       743             4
//   0111        White/N        800             3
//   1000        Black/B        300            11
//   1001        Blue/B         380             9
//   1010        Red/B          509             7
//   1011        Magenta/B      589             6
//   1100        Green/B        711             4
//   1101        Cyan/B         791             3
//   1110        Yellow/B       920             1
//   1111        White/B       1000             0
//
// During blanking (no sync): DAC 11 (black pedestal level).
// During sync:               DAC 15 (sync tip, most positive on /Y pin).
// =========================================================================
reg [3:0] yn_color;
always @(*) begin
    case ({col_I, col_G, col_R, col_B})
        4'b0000: yn_color = 4'd11;  // Black  (normal)
        4'b0001: yn_color = 4'd10;  // Blue   (normal)
        4'b0010: yn_color = 4'd8;   // Red    (normal)
        4'b0011: yn_color = 4'd7;   // Magenta(normal)
        4'b0100: yn_color = 4'd6;   // Green  (normal)
        4'b0101: yn_color = 4'd5;   // Cyan   (normal)
        4'b0110: yn_color = 4'd4;   // Yellow (normal)
        4'b0111: yn_color = 4'd3;   // White  (normal)
        4'b1000: yn_color = 4'd11;  // Black  (bright — same as normal black)
        4'b1001: yn_color = 4'd9;   // Blue   (bright)
        4'b1010: yn_color = 4'd7;   // Red    (bright)
        4'b1011: yn_color = 4'd6;   // Magenta(bright)
        4'b1100: yn_color = 4'd4;   // Green  (bright)
        4'b1101: yn_color = 4'd3;   // Cyan   (bright)
        4'b1110: yn_color = 4'd1;   // Yellow (bright)
        4'b1111: yn_color = 4'd0;   // White  (bright)
        default: yn_color = 4'd11;
    endcase
end

// Priority: sync tip > blanking pedestal > active picture colour
assign yn = sync_active  ? 4'd15 :   // sync tip overrides all
            ~active_video ? 4'd11 :   // blanking pedestal
            yn_color;                 // active picture luma

// =========================================================================
// U (Cb) — 4-bit DAC lookup (indexed by {col_G, col_R, col_B})
//
// BT.601: U = 0.500·B − 0.169·R − 0.331·G
// Scaled to 0–15 with 8 = neutral (0 chroma).
// Scale factor: ±0.500 → ±6 steps.  DAC = round(8 + 12 × U_bt601)
//
//   {G,R,B}  Colour    U(BT.601)  DAC
//   000       Black      0.000      8
//   001       Blue      +0.500     14
//   010       Red       −0.169      6
//   011       Magenta   +0.331     12
//   100       Green     −0.331      4
//   101       Cyan      +0.169     10
//   110       Yellow    −0.500      2
//   111       White      0.000      8
//
// BRIGHT does not change U (only luma amplitude changes with brightness).
// Forced to 8 (neutral) during blanking and sync.
// =========================================================================
reg [3:0] uo_color;
always @(*) begin
    case ({col_G, col_R, col_B})
        3'b000: uo_color = 4'd8;   // Black  / neutral
        3'b001: uo_color = 4'd14;  // Blue   / max positive Cb
        3'b010: uo_color = 4'd6;   // Red    / slight negative Cb
        3'b011: uo_color = 4'd12;  // Magenta/ moderate positive Cb
        3'b100: uo_color = 4'd4;   // Green  / moderate negative Cb
        3'b101: uo_color = 4'd10;  // Cyan   / slight positive Cb
        3'b110: uo_color = 4'd2;   // Yellow / max negative Cb
        3'b111: uo_color = 4'd8;   // White  / neutral
        default: uo_color = 4'd8;
    endcase
end

assign uo = (active_video & ~sync_active) ? uo_color : 4'd8;

// =========================================================================
// V (Cr) — 4-bit DAC lookup (indexed by {col_G, col_R, col_B})
//
// BT.601: V = 0.500·R − 0.419·G − 0.081·B
// Scaled to 0–15 with 8 = neutral.
// Scale factor: ±0.500 → ±6 steps.  DAC = round(8 + 12 × V_bt601)
//
//   {G,R,B}  Colour    V(BT.601)  DAC
//   000       Black      0.000      8
//   001       Blue      −0.081      7
//   010       Red       +0.500     14
//   011       Magenta   +0.419     13
//   100       Green     −0.419      3
//   101       Cyan      −0.500      2
//   110       Yellow    +0.081      9
//   111       White      0.000      8
//
// BRIGHT does not change V.
// Forced to 8 (neutral) during blanking and sync.
// =========================================================================
reg [3:0] vo_color;
always @(*) begin
    case ({col_G, col_R, col_B})
        3'b000: vo_color = 4'd8;   // Black  / neutral
        3'b001: vo_color = 4'd7;   // Blue   / slight negative Cr
        3'b010: vo_color = 4'd14;  // Red    / max positive Cr
        3'b011: vo_color = 4'd13;  // Magenta/ strong positive Cr
        3'b100: vo_color = 4'd3;   // Green  / strong negative Cr
        3'b101: vo_color = 4'd2;   // Cyan   / max negative Cr
        3'b110: vo_color = 4'd9;   // Yellow / slight positive Cr
        3'b111: vo_color = 4'd8;   // White  / neutral
        default: vo_color = 4'd8;
    endcase
end

assign vo = (active_video & ~sync_active) ? vo_color : 4'd8;

// =========================================================================
// SECTION 8: CPU CLOCK CONTENTION (Corrected: hold HIGH to stall)
// =========================================================================
wire mem_contend = ~mreq_n & ~a15 & a14;
wire io_contend  = ~io_ula_n;
wire display_phase = hc[3] & ~hc[2];
wire contend = (mem_contend | io_contend) & display_phase & Border_n;

reg CPUClk = 0;
always @(posedge clk7) begin
    if (~contend) CPUClk <= ~CPUClk;
    else          CPUClk <= 1'b1;      // hold HIGH to stall CPU
end
assign clock = CPUClk;

// =========================================================================
// SECTION 9: PORT 0xFE READ, WRITE AND SOUND
// =========================================================================
wire ula_read  = ~io_ula_n & ~rd_n;
wire ula_write = ~io_ula_n & ~wr_n;
wire ear_in = sound;

wire [7:0] d_out = {1'b1, ear_in, 1'b1, t[4], t[3], t[2], t[1], t[0]};
assign d = ula_read ? d_out : 8'bz;

reg rMic = 0, rSpk = 0;
always @(negedge clk7) begin
    if (ula_write) begin
        rMic        <= d[5];
        rSpk        <= d[4];
        BorderColor <= d[2:0];
    end
end
assign sound = ula_write ? (rSpk | rMic) : 1'bz;

endmodule
