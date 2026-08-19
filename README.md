# Custom Pipelined Floating-Point Core for Numerical and ML Workloads

Hardware-description-language and high-level-model work on IEEE-754 single-precision floating-
point arithmetic units, a small pipelined datapath built around them, and — new in this cleanup —
a single-cycle RISC-V (RV32IF) processor core that actually executes a real instruction set
end-to-end.

The repo holds three separate, independently buildable projects. They share the same IEEE-754
arithmetic *design* but are not wired together, and are kept clearly apart on purpose (see
[Why three projects](#why-three-separate-projects) below).

```
.
├── rtl-systemc/         # SystemC: IEEE-754 FPU modules + a pipelined FP datapath shell
│   ├── fpu/              #   adder, subtractor, multiplier, divider (bit-accurate vs. `float`)
│   └── core/              #   5-stage pipeline wrapper around the FPU modules (demo/waveform tool)
├── rtl-systemverilog/    # SystemVerilog: RTL translation of the same pipeline + UVM testbench
│   └── uvm/                #   UVM environment + a SystemC reference model for scoreboarding
└── riscv-core/           # A single-cycle RV32IF RISC-V core (SystemC), reusing the fpu/ modules
    └── tests/             #   verification harness, 10/10 passing
```

## Why three separate projects

Earlier revisions of this repo mixed a few different, overlapping attempts at "a pipelined FP
processor" together in `src/` and `docs/` — some working, some not, without a clear line between
them. This cleanup:

1. Split the two *working* stacks that were tangled together into clearly separated projects
   (SystemC high-level model vs. SystemVerilog RTL+UVM), each buildable on its own.
2. Deleted three abandoned, non-functional draft files that were previous attempts at a "next
   step" RISC-V processor and never worked (`src/Temp.cpp`, `src/core/PipelinedFPUUnitsProcessor.cpp`,
   `docs/1.c`).
3. Replaced them with `riscv-core/`, one file (`riscv_core.h`) implementing a real, tested
   RISC-V core, built from scratch rather than patched from the failed drafts.

---

## 1. `rtl-systemc/` — IEEE-754 FPU + pipelined datapath (SystemC)

A SystemC model of IEEE-754 single-precision floating-point arithmetic, plus a 5-stage pipeline
shell (fetch/decode/execute/memory/writeback) that drives those units from a tiny instruction
memory.

- `fpu/IEEE754Add.h`, `IEEE754Sub.h`, `IEEE754Mult.h`, `IEEE754Div.h` — combinational adder,
  subtractor, multiplier, and divider modules operating on raw 32-bit IEEE-754 bit patterns.
  **Verified bit-accurate**: exercised against real C++ `float` arithmetic (add/sub/mul/div over
  several positive, negative, and fractional operand pairs) — see the same verification approach
  applied more rigorously in `riscv-core/tests/`.
- `core/fp_pipeline_top.cpp` — `FPPipelinedProcessor`: wraps the four FPU modules in
  fetch/decode/execute/memory/writeback stages (`imem.h`, `execute.h`, `mem_wb.h`). The "decode"
  stage here only extracts register operands and a 7-bit opcode field (bits 31:25) selecting
  which of the four FP operations to run — it is **not** a RISC-V decoder. Useful as a pipeline
  timing/hazard model and VCD waveform generator, not as an instruction-set implementation.

Build & run:

```bash
g++ -std=c++17 rtl-systemc/core/fp_pipeline_top.cpp -o fp_pipeline_top -lsystemc -lpthread
./fp_pipeline_top
# generates fp_system.vcd (view with gtkwave or similar)
```

Requires `libsystemc-dev` (SystemC 2.3+).

## 2. `rtl-systemverilog/` — RTL translation + UVM testbench

A SystemVerilog translation of the same pipelined datapath, with a UVM verification environment.

- `fpu_pipeline.sv` — `FPPipelinedProcessor` module, structurally mirroring `core/fp_pipeline_top.cpp`.
- `testbench.sv` — a plain directed testbench (clock generation, reset sequence, stimulus).
- `uvm/uvm_env.sv` — UVM environment (interface, driver, monitor, scoreboard, agent, env, test)
  for the same DUT.
- `uvm/uvm_systemc_ref.cpp` — a SystemC reference/transaction model used for scoreboarding
  expected results against the SystemVerilog DUT.

Build & run (requires a SystemVerilog simulator with UVM support, e.g. Questa/VCS/Xcelium, or
Verilator for the non-UVM testbench):

```bash
# Directed testbench, e.g. with Verilator:
verilator --binary -sv rtl-systemverilog/fpu_pipeline.sv rtl-systemverilog/testbench.sv --top-module FPPipelinedProcessor_tb
./obj_dir/VFPPipelinedProcessor_tb

# UVM environment: needs a UVM-capable simulator, e.g.
vlog +incdir+$UVM_HOME/src rtl-systemverilog/fpu_pipeline.sv rtl-systemverilog/uvm/uvm_env.sv
vsim -c work.top -do "run -all"
```

## 3. `riscv-core/` — single-cycle RV32IF RISC-V core (new)

A from-scratch, single-cycle (not pipelined) RISC-V core implementing the RV32I base integer ISA
plus the RV32F single-precision floating-point extension's four arithmetic ops, in one header:
`riscv_core.h`. It reuses the `rtl-systemc/fpu/` IEEE-754 modules for FADD.S/FSUB.S/FMUL.S/FDIV.S.

Single-cycle was chosen deliberately over a pipelined design: the three prior attempts at "the
next step" for this processor were all pipelined and never worked. Removing pipeline hazards
(forwarding, stalls, flushes) from the picture made it possible to get a core that is small enough
to verify by hand, instruction by instruction, and to actually trust.

**Supported instructions:**

| Format | Instructions |
|---|---|
| R-type | `ADD SUB SLL SLT SLTU XOR SRL SRA OR AND` |
| I-type (ALU) | `ADDI SLTI SLTIU XORI ORI ANDI SLLI SRLI SRAI` |
| Loads | `LB LH LW LBU LHU` |
| Stores | `SB SH SW` |
| Branches | `BEQ BNE BLT BGE BLTU BGEU` |
| Jumps | `JAL JALR` |
| Upper immediate | `LUI AUIPC` |
| RV32F | `FADD.S FSUB.S FMUL.S FDIV.S FLW FSW` |
| Pseudo | `LI` (expands to `LUI`+`ADDI`, with the standard +0x800 rounding trick) |

**Not implemented** (kept out on purpose, to keep the core small and verifiable): `RV32M`
(hardware multiply/divide on integers), `FENCE`, `ECALL`/`EBREAK`, CSR instructions, and any
floating-point comparison/conversion/sqrt ops beyond the four arithmetic ones above. There is also
no int↔float move instruction — the demo/tests load float constants into `dmem` via `SW` and then
`FLW` them, the same trick a real assembler's `la`-into-float sequence uses.

**Memory model:** 1024-word instruction memory, 4096-byte data memory, 32 integer registers
(`x0` hardwired to zero) and 32 floating-point registers, all as plain arrays on the module —
no caches, no virtual memory, byte-addressed loads/stores with correct sign/zero extension.

### Build & run

```bash
cd riscv-core
g++ -std=c++17 riscv_core_demo.cpp -o riscv_core_demo -lsystemc -lpthread
./riscv_core_demo
```

The demo assembles a small integer loop (sum 1..5) and four RV32F operations on two float
constants directly as encoded instruction words (there is no assembler — programs are built in
C++ using the encoder helpers in `riscv_core.h`, e.g. `ADDI(rd, rs1, imm)`, `BNE(rs1, rs2, offset)`).

### Verification

```bash
cd riscv-core/tests
g++ -std=c++17 test_riscv_core.cpp -o test_riscv_core -lsystemc -lpthread
./test_riscv_core
```

10 independent test programs, each with hand-computed expected register values, covering:
R-type ALU ops, shifts (including arithmetic vs. logical right-shift), all six branch conditions,
a multi-iteration loop, loads/stores with sign/zero extension, `JAL`, `JALR`, `LUI`/`AUIPC`, RV32F
arithmetic on negative/fractional operands, and the `x0`-hardwired-to-zero invariant. Each test
gets its own core instance sharing one clock (`sc_stop()` is process-global in SystemC, so a
single shared "auto-stop on halt" would kill every other test's simulation the moment the first
one finished) and is checked after a fixed simulation window.

```
PASS  alu_r_type
PASS  shifts
PASS  branches
PASS  loop_sum_1_to_5
PASS  loads_stores
PASS  jal
PASS  jalr
PASS  lui_auipc
PASS  fp_arithmetic
PASS  x0_hardwired_zero

10/10 tests passed.
```

The underlying FPU modules (`ieee754_adder`, `ieee754_subtractor`, `ieee754mult`, `ieee754_div`)
were separately re-verified against real C++ `float` arithmetic before being reused here, so the
`fp_arithmetic` test is checking correct *integration* (instruction decode -> FPU wiring -> register
writeback), not re-deriving the arithmetic's correctness from scratch.

### Design notes

- `SC_THREAD` + `wait(clk.posedge_event())` is used for the core's main loop, not `SC_CTHREAD`:
  `SC_CTHREAD` only reliably supports `wait()` and `wait(N)` — the delta-cycle
  `wait(SC_ZERO_TIME)` settling loop needed to read back combinational FPU results within the
  same clock edge is deprecated/unreliable under `SC_CTHREAD` and was observed to silently drop
  an iteration in an early prototype.
- `auto_stop_on_halt` (default `true`) lets a standalone program call `sc_stop()` on `HALT`;
  the test harness sets it to `false` per-core and instead runs a fixed simulation window, since
  `sc_stop()` affects the whole simulation, not just one module.

---

## Requirements

- SystemC 2.3+ (`apt-get install libsystemc-dev` on Debian/Ubuntu), `g++` with C++17.
- A SystemVerilog simulator (Verilator, Questa, VCS, or Xcelium) only for `rtl-systemverilog/`.

## License

[MIT](LICENSE)
