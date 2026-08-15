// Verification harness for RiscVCore: independent test programs covering the ALU, shifts,
// branches, a multi-iteration loop, loads/stores (incl. sign/zero extension), JAL, JALR, LUI/
// AUIPC, RV32F arithmetic, and the x0-hardwired-to-zero invariant.
//
// Each test gets its own RiscVCore instance (auto_stop_on_halt=false) sharing one clock, since
// sc_stop() is global and would otherwise kill every other test's simulation the moment the
// first one halts. All cores run for a fixed, generously-sized duration, then every test's
// final register state is checked. Exits 0 if every test passes, 1 otherwise (suitable for CI).
//
// Build & run:
//   g++ -std=c++17 test_riscv_core.cpp -o test_riscv_core -lsystemc -lpthread
//   ./test_riscv_core
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../riscv_core.h"

using namespace rv32;

struct TestCase {
    std::string name;
    std::vector<uint32_t> program;
    std::function<bool(const RiscVCore &, std::string &)> check;
};

static bool expect_eq(std::string &fail, const char *label, uint32_t got, uint32_t want) {
    if (got == want) return true;
    fail += std::string(label) + ": got 0x" + std::to_string(got) + " want 0x" + std::to_string(want) + "; ";
    return false;
}

static bool expect_feq(std::string &fail, const char *label, uint32_t got_bits, float want, float tol = 1e-3f) {
    float got = bits_to_float(got_bits);
    if (std::fabs(got - want) < tol) return true;
    fail += std::string(label) + ": got " + std::to_string(got) + " want " + std::to_string(want) + "; ";
    return false;
}

std::vector<TestCase> build_tests() {
    std::vector<TestCase> tests;

    // --- ALU (R-type) ---
    {
        std::vector<uint32_t> p = {
            ADDI(1, 0, 10), ADDI(2, 0, 3),
            ADD(3, 1, 2), SUB(4, 1, 2), AND(5, 1, 2), OR(6, 1, 2), XOR(7, 1, 2),
            SLT(8, 2, 1), SLT(9, 1, 2),
            HALT,
        };
        tests.push_back({"alu_r_type", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x3(ADD)", c.int_reg[3], 13);
            ok &= expect_eq(f, "x4(SUB)", c.int_reg[4], 7);
            ok &= expect_eq(f, "x5(AND)", c.int_reg[5], 2);
            ok &= expect_eq(f, "x6(OR)", c.int_reg[6], 11);
            ok &= expect_eq(f, "x7(XOR)", c.int_reg[7], 9);
            ok &= expect_eq(f, "x8(SLT true)", c.int_reg[8], 1);
            ok &= expect_eq(f, "x9(SLT false)", c.int_reg[9], 0);
            return ok;
        }});
    }

    // --- Shifts ---
    {
        std::vector<uint32_t> p;
        p.push_back(ADDI(1, 0, 8));
        p.push_back(SLLI(2, 1, 2));       // 8 << 2 = 32
        p.push_back(SRLI(3, 1, 2));       // 8 >> 2 = 2
        LI(p, 4, -8);                     // x4 = 0xFFFFFFF8
        p.push_back(SRAI(5, 4, 1));       // arithmetic: -8 >> 1 = -4
        p.push_back(SRLI(6, 4, 28));      // logical: top nibble of 0xFFFFFFF8 = 0xF
        p.push_back(HALT);
        tests.push_back({"shifts", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x2(SLLI)", c.int_reg[2], 32);
            ok &= expect_eq(f, "x3(SRLI)", c.int_reg[3], 2);
            ok &= expect_eq(f, "x5(SRAI)", c.int_reg[5], uint32_t(-4));
            ok &= expect_eq(f, "x6(SRLI logical)", c.int_reg[6], 0xF);
            return ok;
        }});
    }

    // --- Branches (each pattern: branch skips exactly the next instruction when taken) ---
    {
        std::vector<uint32_t> p = {
            ADDI(1, 0, 5), ADDI(2, 0, 5), ADDI(3, 0, 3),
            BEQ(1, 2, 8), ADDI(10, 0, 999), ADDI(11, 0, 111),   // BEQ taken (5==5)
            BNE(1, 3, 8), ADDI(12, 0, 999), ADDI(13, 0, 222),   // BNE taken (5!=3)
            BLT(3, 1, 8), ADDI(14, 0, 999), ADDI(15, 0, 333),   // BLT taken (3<5)
            BGE(1, 3, 8), ADDI(16, 0, 999), ADDI(17, 0, 444),   // BGE taken (5>=3)
            BEQ(1, 3, 8), ADDI(18, 0, 555), ADDI(19, 0, 999),   // BEQ NOT taken (5!=3): x18 executes
            HALT,
        };
        tests.push_back({"branches", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x10(skipped)", c.int_reg[10], 0);
            ok &= expect_eq(f, "x11(BEQ target)", c.int_reg[11], 111);
            ok &= expect_eq(f, "x13(BNE target)", c.int_reg[13], 222);
            ok &= expect_eq(f, "x15(BLT target)", c.int_reg[15], 333);
            ok &= expect_eq(f, "x17(BGE target)", c.int_reg[17], 444);
            ok &= expect_eq(f, "x18(not-taken fallthrough)", c.int_reg[18], 555);
            ok &= expect_eq(f, "x19(should also run, no branch)", c.int_reg[19], 999);
            return ok;
        }});
    }

    // --- Multi-iteration loop: sum 1..5 via BNE ---
    {
        std::vector<uint32_t> p;
        p.push_back(ADDI(1, 0, 0));
        p.push_back(ADDI(2, 0, 0));
        p.push_back(ADDI(3, 0, 5));
        size_t loop_start = p.size();
        p.push_back(ADDI(1, 1, 1));
        p.push_back(ADD(2, 2, 1));
        size_t branch_idx = p.size();
        p.push_back(BNE(1, 3, int32_t(loop_start * 4) - int32_t(branch_idx * 4)));
        p.push_back(HALT);
        tests.push_back({"loop_sum_1_to_5", p, [](const RiscVCore &c, std::string &f) {
            return expect_eq(f, "x2(sum)", c.int_reg[2], 15);
        }});
    }

    // --- Loads/stores: word roundtrip, byte sign/zero extension, halfword ---
    {
        std::vector<uint32_t> p;
        LI(p, 1, int32_t(0x12345678));
        p.push_back(SW(0, 1, 0));
        p.push_back(LW(2, 0, 0));
        p.push_back(ADDI(3, 0, -1));      // x3 = 0xFFFFFFFF
        p.push_back(SB(0, 3, 4));         // store low byte 0xFF at addr 4
        p.push_back(LB(4, 0, 4));         // sign-extend -> 0xFFFFFFFF
        p.push_back(LBU(5, 0, 4));        // zero-extend -> 0x000000FF
        p.push_back(ADDI(6, 0, 0x7F));
        p.push_back(SH(0, 6, 8));
        p.push_back(LH(7, 0, 8));
        p.push_back(LHU(8, 0, 8));
        p.push_back(HALT);
        tests.push_back({"loads_stores", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x2(LW roundtrip)", c.int_reg[2], 0x12345678);
            ok &= expect_eq(f, "x4(LB sign-extend)", c.int_reg[4], uint32_t(-1));
            ok &= expect_eq(f, "x5(LBU zero-extend)", c.int_reg[5], 0xFF);
            ok &= expect_eq(f, "x7(LH)", c.int_reg[7], 127);
            ok &= expect_eq(f, "x8(LHU)", c.int_reg[8], 127);
            return ok;
        }});
    }

    // --- JAL: saves return address, jumps forward, skips instructions in between ---
    {
        std::vector<uint32_t> p = {
            JAL(1, 12),          // idx0: x1 = 4, jump to byte 12 (idx3)
            ADDI(10, 0, 999),    // idx1: skipped
            ADDI(11, 0, 999),    // idx2: skipped
            ADDI(2, 0, 55),      // idx3: landed here
            HALT,
        };
        tests.push_back({"jal", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x1(return addr)", c.int_reg[1], 4);
            ok &= expect_eq(f, "x2(jump target ran)", c.int_reg[2], 55);
            ok &= expect_eq(f, "x10(skipped)", c.int_reg[10], 0);
            ok &= expect_eq(f, "x11(skipped)", c.int_reg[11], 0);
            return ok;
        }});
    }

    // --- JALR: jumps to a register-computed address ---
    {
        std::vector<uint32_t> p = {
            ADDI(5, 0, 16),      // idx0: x5 = 16 (byte address of idx4)
            JALR(1, 5, 0),       // idx1: x1 = 8 (return addr), jump to x5+0 = 16 (idx4)
            ADDI(10, 0, 999),    // idx2: skipped
            ADDI(11, 0, 999),    // idx3: skipped
            ADDI(2, 0, 66),      // idx4 (byte 16): landed here
            HALT,
        };
        tests.push_back({"jalr", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x1(return addr)", c.int_reg[1], 8);
            ok &= expect_eq(f, "x2(jump target ran)", c.int_reg[2], 66);
            ok &= expect_eq(f, "x10(skipped)", c.int_reg[10], 0);
            ok &= expect_eq(f, "x11(skipped)", c.int_reg[11], 0);
            return ok;
        }});
    }

    // --- LUI / AUIPC ---
    {
        std::vector<uint32_t> p = {
            LUI(1, 0x12345),     // x1 = 0x12345000
            AUIPC(2, 0x1),       // idx1, pc=4: x2 = 4 + 0x1000
            HALT,
        };
        tests.push_back({"lui_auipc", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x1(LUI)", c.int_reg[1], 0x12345000);
            ok &= expect_eq(f, "x2(AUIPC)", c.int_reg[2], 4 + 0x1000);
            return ok;
        }});
    }

    // --- RV32F arithmetic on negative/fractional operands ---
    {
        std::vector<uint32_t> p;
        LI(p, 5, int32_t(float_to_bits(-2.0f)));
        p.push_back(SW(0, 5, 0));
        p.push_back(FLW(1, 0, 0));
        LI(p, 5, int32_t(float_to_bits(0.5f)));
        p.push_back(SW(0, 5, 4));
        p.push_back(FLW(2, 0, 4));
        p.push_back(FADD_S(3, 1, 2));
        p.push_back(FSUB_S(4, 1, 2));
        p.push_back(FMUL_S(5, 1, 2));
        p.push_back(FDIV_S(6, 1, 2));
        p.push_back(HALT);
        tests.push_back({"fp_arithmetic", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_feq(f, "f3(FADD.S)", c.fp_reg[3], -1.5f);
            ok &= expect_feq(f, "f4(FSUB.S)", c.fp_reg[4], -2.5f);
            ok &= expect_feq(f, "f5(FMUL.S)", c.fp_reg[5], -1.0f);
            ok &= expect_feq(f, "f6(FDIV.S)", c.fp_reg[6], -4.0f);
            return ok;
        }});
    }

    // --- x0 is hardwired to zero ---
    {
        std::vector<uint32_t> p = { ADDI(0, 0, 5), ADD(1, 0, 0), HALT };
        tests.push_back({"x0_hardwired_zero", p, [](const RiscVCore &c, std::string &f) {
            bool ok = true;
            ok &= expect_eq(f, "x0", c.int_reg[0], 0);
            ok &= expect_eq(f, "x1", c.int_reg[1], 0);
            return ok;
        }});
    }

    return tests;
}

int sc_main(int argc, char *argv[]) {
    auto tests = build_tests();

    sc_clock clk("clk", 10, SC_NS);
    std::vector<std::unique_ptr<RiscVCore>> cores;
    for (size_t i = 0; i < tests.size(); i++) {
        auto core = std::make_unique<RiscVCore>(sc_core::sc_module_name(("core" + std::to_string(i)).c_str()));
        core->clk(clk);
        core->auto_stop_on_halt = false;
        for (size_t j = 0; j < tests[i].program.size(); j++) core->imem[j] = tests[i].program[j];
        cores.push_back(std::move(core));
    }

    sc_start(5000, SC_NS);  // 500 cycles -- every test program here is well under 30 instructions

    int failures = 0;
    for (size_t i = 0; i < tests.size(); i++) {
        std::string fail;
        bool halted = cores[i]->halted;
        bool ok = halted;
        if (!halted) fail = "did not halt within the simulation window; ";
        else ok = tests[i].check(*cores[i], fail);

        cout << (ok ? "PASS" : "FAIL") << "  " << tests[i].name;
        if (!ok) {
            cout << "  -- " << fail;
            failures++;
        }
        cout << endl;
    }

    cout << "\n" << (tests.size() - failures) << "/" << tests.size() << " tests passed." << endl;
    return failures == 0 ? 0 : 1;
}
