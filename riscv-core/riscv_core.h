// riscv_core.h -- a single-cycle RV32I + RV32F (single-precision float) RISC-V core.
//
// This is the "proper" processor referenced in the README: real RISC-V instruction encoding
// (standard opcode/funct3/funct7 fields, not an ad-hoc field reuse), verified against hand
// -assembled test programs in tests/test_riscv_core.cpp. Floating-point instructions are
// executed by the same IEEE-754 adder/subtractor/multiplier/divider modules used in
// ../rtl-systemc/fpu/ -- those are reused here, not reimplemented, and were verified
// independently (see the FPU numeric self-check in tests/test_riscv_core.cpp).
//
// Supported instructions:
//   RV32I: LUI, AUIPC, JAL, JALR, BEQ/BNE/BLT/BGE/BLTU/BGEU,
//          LB/LH/LW/LBU/LHU, SB/SH/SW,
//          ADDI/SLTI/SLTIU/XORI/ORI/ANDI/SLLI/SRLI/SRAI,
//          ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND
//   RV32F: FADD.S, FSUB.S, FMUL.S, FDIV.S, FLW, FSW
//
// Not implemented: FENCE/ECALL/EBREAK/CSR instructions, RV32M (mul/div), other FP formats/
// rounding modes. Program termination is signalled the same way the original imem design used:
// an all-zero instruction word halts the core (real RV32 opcodes never have bits[1:0] == 00,
// so 0x00000000 -- also the reset value of unwritten memory -- can't be a valid instruction).
//
// Usage: build a RiscVCore, load word-addressed machine code into core.imem[], run the clock,
// and inspect core.int_reg[]/core.fp_reg[] (or the bit patterns via fp_bits_to_float()).

#pragma once

#include <systemc.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../rtl-systemc/fpu/IEEE754Add.h"
#include "../rtl-systemc/fpu/IEEE754Div.h"
#include "../rtl-systemc/fpu/IEEE754Mult.h"
#include "../rtl-systemc/fpu/IEEE754Sub.h"

// ---- bit-level float <-> uint32 helpers (RV32F registers hold raw IEEE-754 bit patterns) ----

inline uint32_t float_to_bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline float bits_to_float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// ---- instruction encoders: build machine code words from mnemonics, so test/demo programs
// ---- are written in terms of the ISA rather than hand-computed hex ----

namespace rv32 {

constexpr uint32_t OP_R = 0x33, OP_I = 0x13, OP_LOAD = 0x03, OP_STORE = 0x23,
                    OP_BRANCH = 0x63, OP_JAL = 0x6F, OP_JALR = 0x67,
                    OP_LUI = 0x37, OP_AUIPC = 0x17, OP_FP = 0x53, OP_FLW = 0x07, OP_FSW = 0x27;

inline uint32_t r_type(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) {
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

inline uint32_t i_type(int32_t imm12, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) {
    return ((uint32_t(imm12) & 0xFFF) << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

inline uint32_t s_type(int32_t imm12, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode) {
    uint32_t imm = uint32_t(imm12) & 0xFFF;
    return ((imm >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | ((imm & 0x1F) << 7) | opcode;
}

inline uint32_t b_type(int32_t imm13, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode) {
    uint32_t imm = uint32_t(imm13);  // bit 0 must be 0
    uint32_t b12 = (imm >> 12) & 0x1, b11 = (imm >> 11) & 0x1, b10_5 = (imm >> 5) & 0x3F, b4_1 = (imm >> 1) & 0xF;
    return (b12 << 31) | (b10_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (b4_1 << 8) | (b11 << 7) | opcode;
}

inline uint32_t u_type(uint32_t imm20, uint32_t rd, uint32_t opcode) {
    return (imm20 << 12) | (rd << 7) | opcode;
}

inline uint32_t j_type(int32_t imm21, uint32_t rd, uint32_t opcode) {
    uint32_t imm = uint32_t(imm21);  // bit 0 must be 0
    uint32_t b20 = (imm >> 20) & 0x1, b19_12 = (imm >> 12) & 0xFF, b11 = (imm >> 11) & 0x1, b10_1 = (imm >> 1) & 0x3FF;
    return (b20 << 31) | (b10_1 << 21) | (b11 << 20) | (b19_12 << 12) | (rd << 7) | opcode;
}

// RV32I
inline uint32_t ADD(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x00, rs2, rs1, 0x0, rd, OP_R); }
inline uint32_t SUB(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x20, rs2, rs1, 0x0, rd, OP_R); }
inline uint32_t SLL(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x00, rs2, rs1, 0x1, rd, OP_R); }
inline uint32_t SLT(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x00, rs2, rs1, 0x2, rd, OP_R); }
inline uint32_t SLTU(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r_type(0x00, rs2, rs1, 0x3, rd, OP_R); }
inline uint32_t XOR(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x00, rs2, rs1, 0x4, rd, OP_R); }
inline uint32_t SRL(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x00, rs2, rs1, 0x5, rd, OP_R); }
inline uint32_t SRA(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x20, rs2, rs1, 0x5, rd, OP_R); }
inline uint32_t OR(uint32_t rd, uint32_t rs1, uint32_t rs2)   { return r_type(0x00, rs2, rs1, 0x6, rd, OP_R); }
inline uint32_t AND(uint32_t rd, uint32_t rs1, uint32_t rs2)  { return r_type(0x00, rs2, rs1, 0x7, rd, OP_R); }

inline uint32_t ADDI(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x0, rd, OP_I); }
inline uint32_t SLTI(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x2, rd, OP_I); }
inline uint32_t SLTIU(uint32_t rd, uint32_t rs1, int32_t imm) { return i_type(imm, rs1, 0x3, rd, OP_I); }
inline uint32_t XORI(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x4, rd, OP_I); }
inline uint32_t ORI(uint32_t rd, uint32_t rs1, int32_t imm)   { return i_type(imm, rs1, 0x6, rd, OP_I); }
inline uint32_t ANDI(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x7, rd, OP_I); }
inline uint32_t SLLI(uint32_t rd, uint32_t rs1, uint32_t shamt) { return i_type(shamt & 0x1F, rs1, 0x1, rd, OP_I); }
inline uint32_t SRLI(uint32_t rd, uint32_t rs1, uint32_t shamt) { return i_type(shamt & 0x1F, rs1, 0x5, rd, OP_I); }
inline uint32_t SRAI(uint32_t rd, uint32_t rs1, uint32_t shamt) { return i_type((0x20 << 5) | (shamt & 0x1F), rs1, 0x5, rd, OP_I); }

inline uint32_t LB(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x0, rd, OP_LOAD); }
inline uint32_t LH(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x1, rd, OP_LOAD); }
inline uint32_t LW(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x2, rd, OP_LOAD); }
inline uint32_t LBU(uint32_t rd, uint32_t rs1, int32_t imm) { return i_type(imm, rs1, 0x4, rd, OP_LOAD); }
inline uint32_t LHU(uint32_t rd, uint32_t rs1, int32_t imm) { return i_type(imm, rs1, 0x5, rd, OP_LOAD); }

inline uint32_t SB(uint32_t rs1, uint32_t rs2, int32_t imm) { return s_type(imm, rs2, rs1, 0x0, OP_STORE); }
inline uint32_t SH(uint32_t rs1, uint32_t rs2, int32_t imm) { return s_type(imm, rs2, rs1, 0x1, OP_STORE); }
inline uint32_t SW(uint32_t rs1, uint32_t rs2, int32_t imm) { return s_type(imm, rs2, rs1, 0x2, OP_STORE); }

inline uint32_t BEQ(uint32_t rs1, uint32_t rs2, int32_t imm)  { return b_type(imm, rs2, rs1, 0x0, OP_BRANCH); }
inline uint32_t BNE(uint32_t rs1, uint32_t rs2, int32_t imm)  { return b_type(imm, rs2, rs1, 0x1, OP_BRANCH); }
inline uint32_t BLT(uint32_t rs1, uint32_t rs2, int32_t imm)  { return b_type(imm, rs2, rs1, 0x4, OP_BRANCH); }
inline uint32_t BGE(uint32_t rs1, uint32_t rs2, int32_t imm)  { return b_type(imm, rs2, rs1, 0x5, OP_BRANCH); }
inline uint32_t BLTU(uint32_t rs1, uint32_t rs2, int32_t imm) { return b_type(imm, rs2, rs1, 0x6, OP_BRANCH); }
inline uint32_t BGEU(uint32_t rs1, uint32_t rs2, int32_t imm) { return b_type(imm, rs2, rs1, 0x7, OP_BRANCH); }

inline uint32_t JAL(uint32_t rd, int32_t imm)  { return j_type(imm, rd, OP_JAL); }
inline uint32_t JALR(uint32_t rd, uint32_t rs1, int32_t imm) { return i_type(imm, rs1, 0x0, rd, OP_JALR); }

inline uint32_t LUI(uint32_t rd, uint32_t imm20)   { return u_type(imm20, rd, OP_LUI); }
inline uint32_t AUIPC(uint32_t rd, uint32_t imm20) { return u_type(imm20, rd, OP_AUIPC); }

// RV32F (rs2 field unused/zero for these; the "rm" bits in funct3 are ignored -- the reused
// FPU modules always round the way they round)
inline uint32_t FADD_S(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r_type(0x00, rs2, rs1, 0x0, rd, OP_FP); }
inline uint32_t FSUB_S(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r_type(0x04, rs2, rs1, 0x0, rd, OP_FP); }
inline uint32_t FMUL_S(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r_type(0x08, rs2, rs1, 0x0, rd, OP_FP); }
inline uint32_t FDIV_S(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r_type(0x0C, rs2, rs1, 0x0, rd, OP_FP); }
inline uint32_t FLW(uint32_t rd, uint32_t rs1, int32_t imm)  { return i_type(imm, rs1, 0x2, rd, OP_FLW); }
inline uint32_t FSW(uint32_t rs1, uint32_t rs2, int32_t imm) { return s_type(imm, rs2, rs1, 0x2, OP_FSW); }

constexpr uint32_t HALT = 0x00000000;

// LI is a pseudo-instruction (standard RISC-V "li" expansion, not a real opcode): loads any
// 32-bit immediate into rd using LUI+ADDI, with the usual +0x800 rounding trick so ADDI's
// sign-extension of its 12-bit immediate reconstructs the exact value. Appends 1 or 2 words.
inline void LI(std::vector<uint32_t> &prog, uint32_t rd, int32_t value) {
    uint32_t upper = (uint32_t(value) + 0x800) >> 12;
    int32_t lower = value - int32_t(upper << 12);
    if (upper != 0) {
        prog.push_back(u_type(upper & 0xFFFFF, rd, OP_LUI));
        if (lower != 0) prog.push_back(i_type(lower, rd, 0x0, rd, OP_I));
    } else {
        prog.push_back(i_type(lower, 0, 0x0, rd, OP_I));
    }
}

}  // namespace rv32

inline int32_t sign_extend(uint32_t value, int bits) {
    uint32_t m = 1u << (bits - 1);
    return int32_t((value ^ m) - m);
}

SC_MODULE(RiscVCore) {
    sc_in<bool> clk;

    static constexpr int IMEM_WORDS = 1024;
    static constexpr int DMEM_BYTES = 4096;

    uint32_t imem[IMEM_WORDS] = {};
    uint8_t dmem[DMEM_BYTES] = {};
    uint32_t int_reg[32] = {};   // int_reg[0] is hardwired to 0 (enforced on write)
    uint32_t fp_reg[32] = {};    // raw IEEE-754 bit patterns
    uint32_t pc = 0;
    bool halted = false;
    bool trace = false;          // set true to print a line per retired instruction
    bool auto_stop_on_halt = true;  // set false when running several cores in one simulation
                                     // (sc_stop() is global -- one core halting would kill the
                                     // others' simulation too); the harness then drives sc_start()
                                     // for a fixed duration itself and checks `halted` per core.
    uint64_t max_cycles = 100000;  // safety cap so a decode bug can't hang the simulation

    // The four IEEE-754 units from rtl-systemc/fpu -- combinational, shared A/B inputs, and we
    // just read whichever output the current instruction selects.
    sc_signal<sc_uint<32>> fpu_a, fpu_b, fpu_add_out, fpu_sub_out, fpu_mul_out, fpu_div_out;
    sc_signal<bool> fpu_sub_enable, fpu_reset;
    ieee754_adder *fpu_adder;
    ieee754_subtractor *fpu_sub;
    ieee754mult *fpu_mul;
    ieee754_div *fpu_div;

    void write_int(uint32_t rd, uint32_t value) {
        if (rd != 0) int_reg[rd] = value;
    }

    void write_fp(uint32_t rd, uint32_t bits) {
        fp_reg[rd] = bits;
    }

    uint32_t fp_execute(uint32_t funct7, uint32_t a_bits, uint32_t b_bits) {
        fpu_a.write(a_bits);
        fpu_b.write(b_bits);
        // Let the (multi-level) combinational FPU submodules settle through however many delta
        // cycles their internal extractor -> core -> normalizer chain needs.
        for (int i = 0; i < 8; i++) wait(SC_ZERO_TIME);
        switch (funct7) {
            case 0x00: return fpu_add_out.read().to_uint();
            case 0x04: return fpu_sub_out.read().to_uint();
            case 0x08: return fpu_mul_out.read().to_uint();
            case 0x0C: return fpu_div_out.read().to_uint();
            default:   return 0;
        }
    }

    void run() {
        fpu_sub_enable.write(true);
        fpu_reset.write(false);

        for (uint64_t cycle = 0; cycle < max_cycles; cycle++) {
            wait(clk.posedge_event());

            if (pc >= IMEM_WORDS * 4) {
                cout << "RiscVCore: PC 0x" << hex << pc << dec << " out of range, halting." << endl;
                halted = true;
                break;
            }

            uint32_t instr = imem[pc / 4];
            if (instr == rv32::HALT) {
                halted = true;
                break;
            }

            uint32_t opcode = instr & 0x7F;
            uint32_t rd = (instr >> 7) & 0x1F;
            uint32_t funct3 = (instr >> 12) & 0x7;
            uint32_t rs1 = (instr >> 15) & 0x1F;
            uint32_t rs2 = (instr >> 20) & 0x1F;
            uint32_t funct7 = (instr >> 25) & 0x7F;

            int32_t imm_i = sign_extend(instr >> 20, 12);
            int32_t imm_s = sign_extend(((instr >> 25) << 5) | ((instr >> 7) & 0x1F), 12);
            int32_t imm_b = sign_extend((((instr >> 31) & 0x1) << 12) | (((instr >> 7) & 0x1) << 11) |
                                         (((instr >> 25) & 0x3F) << 5) | (((instr >> 8) & 0xF) << 1), 13);
            uint32_t imm_u = instr & 0xFFFFF000;
            int32_t imm_j = sign_extend((((instr >> 31) & 0x1) << 20) | (((instr >> 12) & 0xFF) << 12) |
                                         (((instr >> 20) & 0x1) << 11) | (((instr >> 21) & 0x3FF) << 1), 21);

            uint32_t next_pc = pc + 4;
            uint32_t a = int_reg[rs1], b = int_reg[rs2];

            switch (opcode) {
                case rv32::OP_R: {
                    switch (funct3) {
                        case 0x0: write_int(rd, (funct7 & 0x20) ? (a - b) : (a + b)); break;
                        case 0x1: write_int(rd, a << (b & 0x1F)); break;
                        case 0x2: write_int(rd, int32_t(a) < int32_t(b) ? 1 : 0); break;
                        case 0x3: write_int(rd, a < b ? 1 : 0); break;
                        case 0x4: write_int(rd, a ^ b); break;
                        case 0x5: write_int(rd, (funct7 & 0x20) ? uint32_t(int32_t(a) >> (b & 0x1F)) : (a >> (b & 0x1F))); break;
                        case 0x6: write_int(rd, a | b); break;
                        case 0x7: write_int(rd, a & b); break;
                    }
                    break;
                }
                case rv32::OP_I: {
                    switch (funct3) {
                        case 0x0: write_int(rd, a + imm_i); break;
                        case 0x2: write_int(rd, int32_t(a) < imm_i ? 1 : 0); break;
                        case 0x3: write_int(rd, a < uint32_t(imm_i) ? 1 : 0); break;
                        case 0x4: write_int(rd, a ^ uint32_t(imm_i)); break;
                        case 0x6: write_int(rd, a | uint32_t(imm_i)); break;
                        case 0x7: write_int(rd, a & uint32_t(imm_i)); break;
                        case 0x1: write_int(rd, a << (imm_i & 0x1F)); break;
                        case 0x5: write_int(rd, (funct7 & 0x20) ? uint32_t(int32_t(a) >> (imm_i & 0x1F)) : (a >> (imm_i & 0x1F))); break;
                    }
                    break;
                }
                case rv32::OP_LOAD: {
                    uint32_t addr = a + imm_i;
                    if (addr + 4 > DMEM_BYTES) { cout << "RiscVCore: load address out of range\n"; halted = true; break; }
                    uint32_t byte0 = dmem[addr];
                    switch (funct3) {
                        case 0x0: write_int(rd, uint32_t(sign_extend(byte0, 8))); break;
                        case 0x1: write_int(rd, uint32_t(sign_extend(dmem[addr] | (dmem[addr+1] << 8), 16))); break;
                        case 0x2: write_int(rd, dmem[addr] | (dmem[addr+1] << 8) | (dmem[addr+2] << 16) | (dmem[addr+3] << 24)); break;
                        case 0x4: write_int(rd, byte0); break;
                        case 0x5: write_int(rd, dmem[addr] | (dmem[addr+1] << 8)); break;
                    }
                    break;
                }
                case rv32::OP_STORE: {
                    uint32_t addr = a + imm_s;
                    if (addr + 4 > DMEM_BYTES) { cout << "RiscVCore: store address out of range\n"; halted = true; break; }
                    switch (funct3) {
                        case 0x0: dmem[addr] = b & 0xFF; break;
                        case 0x1: dmem[addr] = b & 0xFF; dmem[addr+1] = (b >> 8) & 0xFF; break;
                        case 0x2:
                            dmem[addr] = b & 0xFF; dmem[addr+1] = (b >> 8) & 0xFF;
                            dmem[addr+2] = (b >> 16) & 0xFF; dmem[addr+3] = (b >> 24) & 0xFF;
                            break;
                    }
                    break;
                }
                case rv32::OP_BRANCH: {
                    bool take = false;
                    switch (funct3) {
                        case 0x0: take = (a == b); break;
                        case 0x1: take = (a != b); break;
                        case 0x4: take = (int32_t(a) < int32_t(b)); break;
                        case 0x5: take = (int32_t(a) >= int32_t(b)); break;
                        case 0x6: take = (a < b); break;
                        case 0x7: take = (a >= b); break;
                    }
                    if (take) next_pc = pc + imm_b;
                    break;
                }
                case rv32::OP_JAL:
                    write_int(rd, pc + 4);
                    next_pc = pc + imm_j;
                    break;
                case rv32::OP_JALR:
                    write_int(rd, pc + 4);
                    next_pc = (a + imm_i) & ~1u;
                    break;
                case rv32::OP_LUI:
                    write_int(rd, imm_u);
                    break;
                case rv32::OP_AUIPC:
                    write_int(rd, pc + imm_u);
                    break;
                case rv32::OP_FP:
                    write_fp(rd, fp_execute(funct7, fp_reg[rs1], fp_reg[rs2]));
                    break;
                case rv32::OP_FLW: {
                    uint32_t addr = a + imm_i;
                    if (addr + 4 > DMEM_BYTES) { cout << "RiscVCore: FLW address out of range\n"; halted = true; break; }
                    write_fp(rd, dmem[addr] | (dmem[addr+1] << 8) | (dmem[addr+2] << 16) | (dmem[addr+3] << 24));
                    break;
                }
                case rv32::OP_FSW: {
                    uint32_t addr = a + imm_s;
                    if (addr + 4 > DMEM_BYTES) { cout << "RiscVCore: FSW address out of range\n"; halted = true; break; }
                    uint32_t v = fp_reg[rs2];
                    dmem[addr] = v & 0xFF; dmem[addr+1] = (v >> 8) & 0xFF;
                    dmem[addr+2] = (v >> 16) & 0xFF; dmem[addr+3] = (v >> 24) & 0xFF;
                    break;
                }
                default:
                    cout << "RiscVCore: unimplemented opcode 0x" << hex << opcode
                         << " at PC 0x" << pc << dec << ", halting." << endl;
                    halted = true;
                    break;
            }

            if (trace) {
                cout << "PC=0x" << hex << pc << " instr=0x" << instr << dec << endl;
            }
            if (halted) break;
            pc = next_pc;
        }

        if (!halted) {
            cout << "RiscVCore: hit the " << max_cycles << "-cycle safety limit without halting "
                 << "(likely an infinite loop in the program)." << endl;
        }
        if (auto_stop_on_halt) sc_stop();
    }

    SC_CTOR(RiscVCore) {
        fpu_adder = new ieee754_adder("fpu_adder");
        fpu_adder->A(fpu_a); fpu_adder->B(fpu_b); fpu_adder->O(fpu_add_out);

        fpu_sub = new ieee754_subtractor("fpu_sub");
        fpu_sub->a(fpu_a); fpu_sub->b(fpu_b); fpu_sub->enable(fpu_sub_enable); fpu_sub->ans(fpu_sub_out);

        fpu_mul = new ieee754mult("fpu_mul");
        fpu_mul->A(fpu_a); fpu_mul->B(fpu_b); fpu_mul->reset(fpu_reset); fpu_mul->result(fpu_mul_out);

        fpu_div = new ieee754_div("fpu_div");
        fpu_div->a(fpu_a); fpu_div->b(fpu_b); fpu_div->reset(fpu_reset); fpu_div->result(fpu_div_out);

        SC_THREAD(run);
    }

    ~RiscVCore() {
        delete fpu_adder;
        delete fpu_sub;
        delete fpu_mul;
        delete fpu_div;
    }
};
