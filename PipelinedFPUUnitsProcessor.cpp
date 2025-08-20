#include <systemc.h>
#include <cmath>
#include <iomanip>
#include <cstring>
using namespace std;

// ======= RTL-SAFE HEADERS ONLY =======
// (No <vector>, <string>, <iostream> in RTL logic)
// We’ll use them only in the testbench section guarded below.
#ifdef SC_INCLUDE_FX
#undef SC_INCLUDE_FX
#endif

// ---------------- Common definitions (RTL-safe) ----------------
enum fp_exceptions {
    FP_INVALID_OP     = 0x1,
    FP_OVERFLOW       = 0x2,
    FP_UNDERFLOW      = 0x4,
    FP_DIVIDE_BY_ZERO = 0x8,
    FP_INEXACT        = 0x10
};

struct ieee754_components {
    bool        sign;
    sc_uint<8>  exponent;
    sc_uint<23> mantissa;
    bool is_zero;
    bool is_infinity;
    bool is_nan;
    bool is_denormalized;
    sc_uint<24> effective_mantissa; // with hidden 1 when normalized
};

static inline ieee754_components decompose_ieee754_rtl(sc_uint<32> value) {
    ieee754_components comp;
    comp.sign     = value[31];
    comp.exponent = (value >> 23) & 0xFF;
    comp.mantissa = value & 0x7FFFFF;

    comp.is_zero         = (comp.exponent == 0) && (comp.mantissa == 0);
    comp.is_infinity     = (comp.exponent == 0xFF) && (comp.mantissa == 0);
    comp.is_nan          = (comp.exponent == 0xFF) && (comp.mantissa != 0);
    comp.is_denormalized = (comp.exponent == 0) && (comp.mantissa != 0);

    if (comp.is_zero || comp.is_infinity || comp.is_nan) {
        comp.effective_mantissa = comp.mantissa;
    } else if (comp.is_denormalized) {
        comp.effective_mantissa = comp.mantissa;
    } else {
        comp.effective_mantissa = comp.mantissa | 0x800000; // add hidden 1
    }
    return comp;
}

static inline sc_uint<32> compose_ieee754_rtl(bool sign, sc_int<12> exp_signed, sc_uint<24> mantissa, sc_uint<8>& exceptions) {
    // Overflow to infinity
    if (exp_signed >= 255) {
        exceptions |= FP_OVERFLOW;
        return (sc_uint<32>(sign) << 31) | 0x7F800000;
    }

    // Subnormal / underflow path
    if (exp_signed <= 0) {
        // create a denormal when in range and mantissa != 0
        if (exp_signed >= -22 && mantissa != 0) {
            exceptions |= FP_UNDERFLOW;
            int shift_amount = 1 - exp_signed.to_int();
            if (shift_amount > 0 && shift_amount < 24) {
                sc_uint<24> m = mantissa >> shift_amount;
                if (m == 0) {
                    return (sc_uint<32>(sign) << 31);
                }
                return (sc_uint<32>(sign) << 31) | (m & 0x7FFFFF);
            }
        }
        exceptions |= FP_UNDERFLOW;
        return (sc_uint<32>(sign) << 31);
    }

    sc_uint<8>  exp  = sc_uint<8>(exp_signed);
    sc_uint<23> frac = mantissa & 0x7FFFFF;
    return (sc_uint<32>(sign) << 31) | (sc_uint<32>(exp) << 23) | sc_uint<32>(frac);
}

static inline sc_uint<32> generate_nan_rtl(bool sign = false) {
    return (sc_uint<32>(sign) << 31) | 0x7FC00000;
}
static inline sc_uint<32> generate_infinity_rtl(bool sign = false) {
    return (sc_uint<32>(sign) << 31) | 0x7F800000;
}

// ---------------- ISA encoding (RTL-safe) ----------------
struct fp_instruction_t {
    sc_uint<4>  opcode;
    sc_uint<5>  rd;
    sc_uint<5>  rs1;
    sc_uint<5>  rs2;
    sc_uint<13> unused;

    fp_instruction_t() : opcode(0), rd(0), rs1(0), rs2(0), unused(0) {}
    fp_instruction_t(sc_uint<4> op, sc_uint<5> dst, sc_uint<5> src1, sc_uint<5> src2)
        : opcode(op), rd(dst), rs1(src1), rs2(src2), unused(0) {}

    sc_uint<32> to_word() const {
        return (sc_uint<32>(opcode) << 28) | (sc_uint<32>(rd) << 23) |
               (sc_uint<32>(rs1) << 18) | (sc_uint<32>(rs2) << 13);
    }
};

// ============================================================
//                       RTL MODULES
// ============================================================

SC_MODULE(Fetch) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;

    sc_out<sc_uint<32>> pc_out;
    sc_out<sc_uint<32>> instruction_out;
    sc_out<bool>        valid_out;

    // Fixed-size ROM for synthesis. Size can be adjusted.
    sc_uint<32> imem[256];
    sc_uint<9>  imem_size;
    sc_uint<32> pc;

    // Simulation-only helper (not used by synth tools)
    void load_program(const sc_uint<32>* program, int size) {
        int s = (size > 256) ? 256 : size;
        for (int i = 0; i < s; ++i) imem[i] = program[i];
        imem_size = s;
    }

    void fetch_process() {
        if (reset.read()) {
            pc = 0;
            pc_out.write(0);
            instruction_out.write(0);
            valid_out.write(false);
        } else if (!stall.read()) {
            if (pc < imem_size) {
                pc_out.write(pc * 4);
                instruction_out.write(imem[pc]);
                valid_out.write(true);
                pc = pc + 1;
            } else {
                valid_out.write(false);
            }
        }
    }

    SC_CTOR(Fetch) : imem_size(0), pc(0) {
        // Initialize ROM to zeros
        for (int i = 0; i < 256; ++i) imem[i] = 0;
        SC_METHOD(fetch_process);
        sensitive << clk.pos();
        // synchronous reset via if (reset) inside
    }
};

SC_MODULE(Decode) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;

    sc_in<sc_uint<32>> pc_in;
    sc_in<sc_uint<32>> instruction_in;
    sc_in<bool>        valid_in;

    sc_out<sc_uint<32>> pc_out;
    sc_out<sc_uint<4>>  opcode_out;
    sc_out<sc_uint<5>>  rd_out;
    sc_out<sc_uint<32>> operand1_out;
    sc_out<sc_uint<32>> operand2_out;
    sc_out<bool>        valid_out;

    sc_uint<32> fp_registers[32];
    sc_uint<8>  exception_flags;

    void decode_process() {
        if (reset.read()) {
            pc_out.write(0);
            opcode_out.write(0);
            rd_out.write(0);
            operand1_out.write(0);
            operand2_out.write(0);
            valid_out.write(false);
            exception_flags = 0;
            for (int i = 0; i < 32; i++) fp_registers[i] = 0;
        } else if (!stall.read()) {
            if (valid_in.read()) {
                sc_uint<32> inst = instruction_in.read();
                sc_uint<4> opcode = (inst >> 28) & 0xF;
                sc_uint<5> rd     = (inst >> 23) & 0x1F;
                sc_uint<5> rs1    = (inst >> 18) & 0x1F;
                sc_uint<5> rs2    = (inst >> 13) & 0x1F;

                sc_uint<32> op1 = fp_registers[rs1.to_uint()];
                sc_uint<32> op2 = fp_registers[rs2.to_uint()];

                pc_out.write(pc_in.read());
                opcode_out.write(opcode);
                rd_out.write(rd);
                operand1_out.write(op1);
                operand2_out.write(op2);
                valid_out.write(true);
            } else {
                valid_out.write(false);
            }
        }
    }

    // RTL-safe register write (used by Writeback)
    void write_register(sc_uint<5> reg, sc_uint<32> value) {
        if (reg.to_uint() != 0) {
            fp_registers[reg.to_uint()] = value;
        }
    }

    // Simulation-only helpers for TB (by raw bits; synthesizable if used at reset init)
    void set_register_bits(int reg, sc_uint<32> bits) {
        if (reg > 0 && reg < 32) fp_registers[reg] = bits;
    }

    void set_exception_flag(sc_uint<8> flag) { exception_flags |= flag; }
    sc_uint<8> get_exception_flags() const { return exception_flags; }
    void clear_exception_flags() { exception_flags = 0; }

    SC_CTOR(Decode) : exception_flags(0) {
        for (int i = 0; i < 32; i++) fp_registers[i] = 0;
        SC_METHOD(decode_process);
        sensitive << clk.pos();
    }
};

SC_MODULE(Execute) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;

    sc_in<sc_uint<32>> pc_in;
    sc_in<sc_uint<4>>  opcode_in;
    sc_in<sc_uint<5>>  rd_in;
    sc_in<sc_uint<32>> operand1_in;
    sc_in<sc_uint<32>> operand2_in;
    sc_in<bool>        valid_in;

    sc_out<sc_uint<32>> pc_out;
    sc_out<sc_uint<4>>  opcode_out;
    sc_out<sc_uint<5>>  rd_out;
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<8>>  exceptions_out;
    sc_out<bool>        valid_out;

private:
    enum opcodes { OP_FADD = 0x0, OP_FSUB = 0x1, OP_FMUL = 0x2, OP_FDIV = 0x3 };

    struct stage_t {
        sc_uint<32> pc;
        sc_uint<4>  opcode;
        sc_uint<5>  rd;
        sc_uint<32> operand_a;
        sc_uint<32> operand_b;
        bool        valid;

        // decoded components (registered)
        ieee754_components comp_a, comp_b;

        // results
        sc_uint<32> result;
        sc_uint<8>  exceptions;

        stage_t() : pc(0), opcode(0), rd(0), operand_a(0), operand_b(0), valid(false), result(0), exceptions(0) {}
    };

    // 3-stage simple pipeline (F->D->X)
    stage_t pipe[3];

    // ---------------- Division unit pool (fixed, synthesizable) ----------------
    struct div_entry_t {
        bool        valid;
        sc_uint<32> pc;
        sc_uint<4>  opcode;
        sc_uint<5>  rd;
        ieee754_components a, b;

        // iterative restoring division state
        bool        div_sign;
        sc_int<12>  div_exp;
        sc_uint<48> dividend;    // shifted numerator
        sc_uint<24> divisor;     // denominator
        sc_uint<24> quotient;    // building result
        sc_int<6>   cycles;      // 24 down to 0
        sc_uint<32> result;
        sc_uint<8>  exceptions;

        div_entry_t() : valid(false), opcode(0), rd(0), div_sign(0), div_exp(0), dividend(0),
                        divisor(0), quotient(0), cycles(0), result(0), exceptions(0) {}
    };

    static const int DIV_SLOTS = 4;
    div_entry_t divq[DIV_SLOTS];

    // Helper: find free slot / ready slot
    int find_free_divslot() {
        for (int i = 0; i < DIV_SLOTS; ++i) if (!divq[i].valid) return i;
        return -1;
    }
    int find_ready_divslot() {
        for (int i = 0; i < DIV_SLOTS; ++i) {
            if (divq[i].valid && divq[i].cycles == 0) return i;
        }
        return -1;
    }

    // ---------------- Arithmetic ----------------
    sc_uint<32> do_addsub(const ieee754_components& a, const ieee754_components& b_in, bool subtract, sc_uint<8>& exceptions) {
        // Handle NaNs/Infs/Zeros
        if (a.is_nan || b_in.is_nan) {
            exceptions |= FP_INVALID_OP;
            return generate_nan_rtl();
        }

        bool bsign_eff = subtract ? !b_in.sign : b_in.sign;

        if (a.is_infinity || b_in.is_infinity) {
            if (a.is_infinity && b_in.is_infinity && (a.sign != bsign_eff)) {
                exceptions |= FP_INVALID_OP;
                return generate_nan_rtl();
            }
            return a.is_infinity
                ? ((sc_uint<32>(a.sign) << 31) | 0x7F800000)
                : ((sc_uint<32>(bsign_eff) << 31) | 0x7F800000);
        }

        if (a.is_zero && b_in.is_zero) {
            bool rsign = subtract ? (a.sign && !b_in.sign) : (a.sign && b_in.sign);
            return sc_uint<32>(rsign) << 31;
        }
        if (a.is_zero) {
            return (sc_uint<32>(bsign_eff) << 31) | (sc_uint<32>(b_in.exponent) << 23) | b_in.mantissa;
        }
        if (b_in.is_zero) {
            return (sc_uint<32>(a.sign) << 31) | (sc_uint<32>(a.exponent) << 23) | a.mantissa;
        }

        // Unpack exponents and mantissas
        sc_int<12> exp_a = a.is_denormalized ? sc_int<12>(1) : sc_int<12>(a.exponent);
        sc_int<12> exp_b = b_in.is_denormalized ? sc_int<12>(1) : sc_int<12>(b_in.exponent);
sc_uint<24> mant_a = a.is_denormalized ? 
                     (sc_uint<24>) a.mantissa : 
                     (sc_uint<24>) (a.mantissa | 0x800000);

sc_uint<24> mant_b = b_in.is_denormalized ? 
                     (sc_uint<24>) b_in.mantissa : 
                     (sc_uint<24>) (b_in.mantissa | 0x800000);

        // Align
        sc_int<12> diff = exp_a - exp_b;
        sc_int<12> rexp;
        if (diff >= 0) {
            rexp = exp_a;
            int s = diff.to_int();
            if (s > 0 && s < 24) mant_b >>= s;
            else if (s >= 24) mant_b = 0;
        } else {
            rexp = exp_b;
            int s = -diff.to_int();
            if (s > 0 && s < 24) mant_a >>= s;
            else if (s >= 24) mant_a = 0;
        }

        // Add/Sub
        sc_uint<25> rmant;
        bool rsign;
        if (a.sign == bsign_eff) {
            rmant = sc_uint<25>(mant_a) + sc_uint<25>(mant_b);
            rsign = a.sign;
        } else {
            if (mant_a >= mant_b) {
                rmant = sc_uint<25>(mant_a) - sc_uint<25>(mant_b);
                rsign = a.sign;
            } else {
                rmant = sc_uint<25>(mant_b) - sc_uint<25>(mant_a);
                rsign = bsign_eff;
            }
        }

        if (rmant == 0) return 0;

        // Normalize
        if (rmant & 0x1000000) { // carry
            rmant >>= 1;
            rexp = rexp + 1;
        } else {
            // shift left until MSB hits bit23 or exponent underflows to 1
            for (int i = 0; i < 24; ++i) {
                if ((rmant & 0x800000) || (rexp <= 1)) break;
                rmant <<= 1;
                rexp = rexp - 1;
            }
        }

        sc_uint<24> fmant = rmant & 0x7FFFFF;
        return compose_ieee754_rtl(rsign, rexp, fmant, exceptions);
    }

    sc_uint<32> do_mul(const ieee754_components& a, const ieee754_components& b, sc_uint<8>& exceptions) {
        if (a.is_nan || b.is_nan) { exceptions |= FP_INVALID_OP; return generate_nan_rtl(); }
        if ((a.is_infinity && b.is_zero) || (a.is_zero && b.is_infinity)) { exceptions |= FP_INVALID_OP; return generate_nan_rtl(); }
        if (a.is_infinity || b.is_infinity) return generate_infinity_rtl(a.sign ^ b.sign);
        if (a.is_zero || b.is_zero) return sc_uint<32>(a.sign ^ b.sign) << 31;

        bool rsign = a.sign ^ b.sign;
        sc_int<12> ea = a.is_denormalized ? sc_int<12>(1) : sc_int<12>(a.exponent);
        sc_int<12> eb = b.is_denormalized ? sc_int<12>(1) : sc_int<12>(b.exponent);
        sc_int<12> rexp = ea + eb - 127;

        sc_uint<48> prod = sc_uint<48>(a.effective_mantissa) * sc_uint<48>(b.effective_mantissa);
        if (prod & 0x800000000000ULL) { // bit 47
            prod >>= 24;
            rexp = rexp + 1;
        } else {
            prod >>= 23;
        }
        sc_uint<24> fmant = prod & 0xFFFFFF;
        return compose_ieee754_rtl(rsign, rexp, fmant, exceptions);
    }

    void div_start(div_entry_t& e) {
        const ieee754_components &a = e.a, &b = e.b;

        if (a.is_nan || b.is_nan) { e.exceptions |= FP_INVALID_OP; e.result = generate_nan_rtl(); e.cycles = 0; return; }
        if (b.is_zero) {
            e.exceptions |= FP_DIVIDE_BY_ZERO;
            if (a.is_zero) { e.exceptions |= FP_INVALID_OP; e.result = generate_nan_rtl(); }
            else { e.result = generate_infinity_rtl(a.sign ^ b.sign); }
            e.cycles = 0; return;
        }
        if (a.is_zero) { e.result = sc_uint<32>(a.sign ^ b.sign) << 31; e.cycles = 0; return; }
        if (a.is_infinity) {
            if (b.is_infinity) { e.exceptions |= FP_INVALID_OP; e.result = generate_nan_rtl(); }
            else { e.result = generate_infinity_rtl(a.sign ^ b.sign); }
            e.cycles = 0; return;
        }
        if (b.is_infinity) { e.result = sc_uint<32>(a.sign ^ b.sign) << 31; e.cycles = 0; return; }

        e.div_sign = a.sign ^ b.sign;
        sc_int<12> ea = a.is_denormalized ? sc_int<12>(1) : sc_int<12>(a.exponent);
        sc_int<12> eb = b.is_denormalized ? sc_int<12>(1) : sc_int<12>(b.exponent);
        e.div_exp = ea - eb + 127;

        e.dividend = sc_uint<48>(a.effective_mantissa) << 23;
        e.divisor  = b.effective_mantissa;
        e.quotient = 0;
        e.cycles   = 24;
    }

    void div_step(div_entry_t& e) {
        if (!e.valid || e.cycles <= 0) return;

        // restoring division step
        e.dividend = e.dividend << 1;
        sc_uint<48> dsh = sc_uint<48>(e.divisor) << 24;
        if (e.dividend >= dsh) {
            e.dividend = e.dividend - dsh;
            e.quotient = (e.quotient << 1) | 1;
        } else {
            e.quotient = e.quotient << 1;
        }

        e.cycles = e.cycles - 1;

        if (e.cycles == 0) {
            sc_uint<24> q = e.quotient;
            sc_int<12>  ex = e.div_exp;

            // Normalize (bounded loop for synthesis)
            for (int i = 0; i < 24; ++i) {
                if ((q == 0) || (q & 0x800000) || (ex <= 1)) break;
                q <<= 1;
                ex = ex - 1;
            }

            e.result = compose_ieee754_rtl(e.div_sign, ex, q, e.exceptions);
        }
    }

    sc_uint<32> do_op(sc_uint<4> opc, const ieee754_components& a, const ieee754_components& b, sc_uint<8>& exc) {
        switch (opc.to_uint()) {
            case OP_FADD: return do_addsub(a, b, false, exc);
            case OP_FSUB: return do_addsub(a, b, true,  exc);
            case OP_FMUL: return do_mul(a, b, exc);
            case OP_FDIV: return 0; // handled by division pool
            default: exc |= FP_INVALID_OP; return generate_nan_rtl();
        }
    }

public:
    void exec_process() {
        if (reset.read()) {
            for (int i = 0; i < 3; ++i) pipe[i] = stage_t();
            for (int i = 0; i < DIV_SLOTS; ++i) divq[i] = div_entry_t();

            pc_out.write(0);
            opcode_out.write(0);
            rd_out.write(0);
            result_out.write(0);
            exceptions_out.write(0);
            valid_out.write(false);
            return;
        }

        // If stalled, still advance division micro-steps (optional choice)
        // but do not change pipe registers or outputs.
        if (stall.read()) {
            for (int i = 0; i < DIV_SLOTS; ++i) if (divq[i].valid && divq[i].cycles > 0) div_step(divq[i]);
            valid_out.write(false);
            return;
        }

        // 1) Advance division pool one micro-step
        for (int i = 0; i < DIV_SLOTS; ++i) if (divq[i].valid && divq[i].cycles > 0) div_step(divq[i]);

        // 2) Drive outputs: prefer ready division results, otherwise pipe[2]
        bool out_valid = false;
        sc_uint<32> out_pc = 0, out_res = 0; sc_uint<4> out_op = 0; sc_uint<5> out_rd = 0; sc_uint<8> out_exc = 0;
        int ready_idx = find_ready_divslot();
        if (ready_idx >= 0) {
            out_valid = true;
            out_pc = divq[ready_idx].pc;
            out_op = divq[ready_idx].opcode;
            out_rd = divq[ready_idx].rd;
            out_res = divq[ready_idx].result;
            out_exc = divq[ready_idx].exceptions;
            divq[ready_idx].valid = false; // free the slot
        } else if (pipe[2].valid) {
            out_valid = true;
            out_pc = pipe[2].pc;
            out_op = pipe[2].opcode;
            out_rd = pipe[2].rd;
            out_res = pipe[2].result;
            out_exc = pipe[2].exceptions;
        }

        pc_out.write(out_pc);
        opcode_out.write(out_op);
        rd_out.write(out_rd);
        result_out.write(out_res);
        exceptions_out.write(out_exc);
        valid_out.write(out_valid);

        // 3) Stage 1 -> Stage 2
        if (pipe[1].valid) {
            pipe[2] = pipe[1];
            pipe[2].exceptions = 0;

            if (pipe[1].opcode == OP_FDIV) {
                // enqueue a division if slot available; otherwise (backpressure) drop result from pipe[2]
                int slot = find_free_divslot();
                if (slot >= 0) {
                    divq[slot] = div_entry_t();
                    divq[slot].valid   = true;
                    divq[slot].pc      = pipe[1].pc;
                    divq[slot].opcode  = pipe[1].opcode;
                    divq[slot].rd      = pipe[1].rd;
                    divq[slot].a       = pipe[1].comp_a;
                    divq[slot].b       = pipe[1].comp_b;
                    divq[slot].exceptions = 0;
                    div_start(divq[slot]);
                }
                // Do not forward to pipe[2]; it’s handled by division queue
                pipe[2].valid = false;
            } else {
                pipe[2].result = do_op(pipe[1].opcode, pipe[1].comp_a, pipe[1].comp_b, pipe[2].exceptions);
            }
        } else {
            pipe[2].valid = false;
        }

        // 4) Stage 0 -> Stage 1 (decode IEEE components)
        if (pipe[0].valid) {
            pipe[1] = pipe[0];
            pipe[1].comp_a = decompose_ieee754_rtl(pipe[0].operand_a);
            pipe[1].comp_b = decompose_ieee754_rtl(pipe[0].operand_b);
            pipe[1].exceptions = 0;
        } else {
            pipe[1].valid = false;
        }

        // 5) Input -> Stage 0
        if (valid_in.read()) {
            pipe[0].pc        = pc_in.read();
            pipe[0].opcode    = opcode_in.read();
            pipe[0].rd        = rd_in.read();
            pipe[0].operand_a = operand1_in.read();
            pipe[0].operand_b = operand2_in.read();
            pipe[0].valid     = true;
        } else {
            pipe[0].valid = false;
        }
    }

    SC_CTOR(Execute) {
        for (int i = 0; i < 3; ++i) pipe[i] = stage_t();
        for (int i = 0; i < DIV_SLOTS; ++i) divq[i] = div_entry_t();
        SC_METHOD(exec_process);
        sensitive << clk.pos();
    }
};

SC_MODULE(Writeback) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;

    sc_in<sc_uint<32>> pc_in;
    sc_in<sc_uint<4>>  opcode_in;
    sc_in<sc_uint<5>>  rd_in;
    sc_in<sc_uint<32>> result_in;
    sc_in<sc_uint<8>>  exceptions_in;
    sc_in<bool>        valid_in;

    // Hook to Decode to write registers and accumulate flags
    Decode* decode_stage;

    void writeback_process() {
        if (reset.read()) {
            if (decode_stage) decode_stage->clear_exception_flags();
        } else if (!stall.read() && valid_in.read()) {
            sc_uint<5>  rd  = rd_in.read();
            sc_uint<32> res = result_in.read();
            sc_uint<8>  exc = exceptions_in.read();
            if (decode_stage) {
                decode_stage->write_register(rd, res);
                if (exc != 0) decode_stage->set_exception_flag(exc);
            }
        }
    }

    void set_decode_stage(Decode* p) { decode_stage = p; }

    SC_CTOR(Writeback) : decode_stage(nullptr) {
        SC_METHOD(writeback_process);
        sensitive << clk.pos();
    }
};



SC_MODULE(FPU_Pipeline_Top) {
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;

    // Internal pipeline stage instances
    Fetch*     fetch_stage;
    Decode*    decode_stage;
    Execute*   execute_stage;
    Writeback* writeback_stage;

    // Internal signals connecting the stages
    sc_signal<sc_uint<32>> fetch_pc, fetch_inst;
    sc_signal<bool>        fetch_valid;

    sc_signal<sc_uint<32>> decode_pc;
    sc_signal<sc_uint<4>>  decode_opcode;
    sc_signal<sc_uint<5>>  decode_rd;
    sc_signal<sc_uint<32>> decode_op1, decode_op2;
    sc_signal<bool>        decode_valid;

    sc_signal<sc_uint<32>> execute_pc, execute_result;
    sc_signal<sc_uint<4>>  execute_opcode;
    sc_signal<sc_uint<5>>  execute_rd;
    sc_signal<sc_uint<8>>  execute_exceptions;
    sc_signal<bool>        execute_valid;

    SC_CTOR(FPU_Pipeline_Top) {
        // Create pipeline stage instances
        fetch_stage     = new Fetch("fetch");
        decode_stage    = new Decode("decode");
        execute_stage   = new Execute("execute");
        writeback_stage = new Writeback("writeback");

        // Connect Fetch stage
        fetch_stage->clk(clk);
        fetch_stage->reset(reset);
        fetch_stage->stall(stall);
        fetch_stage->pc_out(fetch_pc);
        fetch_stage->instruction_out(fetch_inst);
        fetch_stage->valid_out(fetch_valid);

        // Connect Decode stage
        decode_stage->clk(clk);
        decode_stage->reset(reset);
        decode_stage->stall(stall);
        decode_stage->pc_in(fetch_pc);
        decode_stage->instruction_in(fetch_inst);
        decode_stage->valid_in(fetch_valid);
        decode_stage->pc_out(decode_pc);
        decode_stage->opcode_out(decode_opcode);
        decode_stage->rd_out(decode_rd);
        decode_stage->operand1_out(decode_op1);
        decode_stage->operand2_out(decode_op2);
        decode_stage->valid_out(decode_valid);

        // Connect Execute stage
        execute_stage->clk(clk);
        execute_stage->reset(reset);
        execute_stage->stall(stall);
        execute_stage->pc_in(decode_pc);
        execute_stage->opcode_in(decode_opcode);
        execute_stage->rd_in(decode_rd);
        execute_stage->operand1_in(decode_op1);
        execute_stage->operand2_in(decode_op2);
        execute_stage->valid_in(decode_valid);
        execute_stage->pc_out(execute_pc);
        execute_stage->opcode_out(execute_opcode);
        execute_stage->rd_out(execute_rd);
        execute_stage->result_out(execute_result);
        execute_stage->exceptions_out(execute_exceptions);
        execute_stage->valid_out(execute_valid);

        // Connect Writeback stage
        writeback_stage->clk(clk);
        writeback_stage->reset(reset);
        writeback_stage->stall(stall);
        writeback_stage->pc_in(execute_pc);
        writeback_stage->opcode_in(execute_opcode);
        writeback_stage->rd_in(execute_rd);
        writeback_stage->result_in(execute_result);
        writeback_stage->exceptions_in(execute_exceptions);
        writeback_stage->valid_in(execute_valid);
        writeback_stage->set_decode_stage(decode_stage);
    }

    ~FPU_Pipeline_Top() {
        delete fetch_stage;
        delete decode_stage;
        delete execute_stage;
        delete writeback_stage;
    }
};

int sc_main(int argc, char* argv[]) {
    // Create clock - synthesis tools will replace this with their clock
    sc_clock clk("clk", 10, SC_NS);
    
    // Create control signals
    sc_signal<bool> reset;
    sc_signal<bool> stall;
    
    // Instantiate the existing FPU pipeline top module
    FPU_Pipeline_Top fpu("fpu_pipeline");
    fpu.clk(clk);
    fpu.reset(reset);
    fpu.stall(stall);
    
    // Initialize control signals
    reset.write(true);
    stall.write(false);
    
    // Run for minimal time - synthesis tools will analyze the structure
    sc_start(100, SC_NS);
    
    return 0;
}
