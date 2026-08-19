// Demo program for RiscVCore: an integer loop (sum 1..5) plus RV32F arithmetic on two float
// constants loaded through memory (there's no int-to-float move instruction implemented, so
// constants go through dmem via SW + FLW, same as a real assembler's `la`-into-float trick).
//
// Build & run:
//   g++ -std=c++17 riscv_core_demo.cpp -o riscv_core_demo -lsystemc -lpthread
//   ./riscv_core_demo
#include "riscv_core.h"

using namespace rv32;

int sc_main(int argc, char *argv[]) {
    std::vector<uint32_t> prog;

    // --- integer part: sum 1..5 into x2 ---
    prog.push_back(ADDI(1, 0, 0));  // x1 = 0 (i)
    prog.push_back(ADDI(2, 0, 0));  // x2 = 0 (sum)
    prog.push_back(ADDI(3, 0, 5));  // x3 = 5 (loop limit -- the body runs for i=1..5 inclusive,
                                     // since the branch check comes *after* the increment+add,
                                     // so the i==limit iteration still executes before stopping)
    size_t loop_start = prog.size();
    prog.push_back(ADDI(1, 1, 1));  // i++
    prog.push_back(ADD(2, 2, 1));   // sum += i
    size_t branch_idx = prog.size();
    prog.push_back(BNE(1, 3, int32_t(loop_start * 4) - int32_t(branch_idx * 4)));  // loop while i != 5

    // --- float part: f3=f1+f2, f4=f1-f2, f5=f1*f2, f6=f1/f2 for f1=1.5, f2=2.5 ---
    LI(prog, 5, int32_t(float_to_bits(1.5f)));
    prog.push_back(SW(0, 5, 0));   // dmem[0..3] = bits(1.5)
    prog.push_back(FLW(1, 0, 0));  // f1 = 1.5

    LI(prog, 5, int32_t(float_to_bits(2.5f)));
    prog.push_back(SW(0, 5, 4));   // dmem[4..7] = bits(2.5)
    prog.push_back(FLW(2, 0, 4));  // f2 = 2.5

    prog.push_back(FADD_S(3, 1, 2));
    prog.push_back(FSUB_S(4, 1, 2));
    prog.push_back(FMUL_S(5, 1, 2));
    prog.push_back(FDIV_S(6, 1, 2));

    prog.push_back(HALT);

    sc_clock clk("clk", 10, SC_NS);
    RiscVCore core("core");
    core.clk(clk);
    for (size_t i = 0; i < prog.size(); i++) core.imem[i] = prog[i];

    cout << "================ RISC-V (RV32IF) core demo ================" << endl;
    sc_start();

    cout << "\n-- integer result --" << endl;
    cout << "sum(1..5) = x2 = " << core.int_reg[2] << " (expected 15)" << endl;

    cout << "\n-- float results (f1=1.5, f2=2.5) --" << endl;
    cout << "f3 = f1 + f2 = " << bits_to_float(core.fp_reg[3]) << " (expected 4)" << endl;
    cout << "f4 = f1 - f2 = " << bits_to_float(core.fp_reg[4]) << " (expected -1)" << endl;
    cout << "f5 = f1 * f2 = " << bits_to_float(core.fp_reg[5]) << " (expected 3.75)" << endl;
    cout << "f6 = f1 / f2 = " << bits_to_float(core.fp_reg[6]) << " (expected 0.6)" << endl;

    return 0;
}
