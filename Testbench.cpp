///Replace this code in lower part of the Piepline file to run and test it 
// ============================================================
//                    TESTBENCH (Simulation Only)
// ============================================================

static inline sc_uint<32> float_to_ieee754_bits(float f) {
    union { float f; uint32_t i; } u;
    u.f = f;
    return sc_uint<32>(u.i);
}
static inline float ieee754_bits_to_float(sc_uint<32> ieee) {
    union { float f; uint32_t i; } u;
    u.i = ieee.to_uint();
    return u.f;
}

// A TB-only version of decomposition for messages
static inline ieee754_components decompose_ieee754_dbg(sc_uint<32> value) {
    return decompose_ieee754_rtl(value);
}

// Opcodes for TB readability
enum { OP_FADD = 0x0, OP_FSUB = 0x1, OP_FMUL = 0x2, OP_FDIV = 0x3 };

SC_MODULE(ComprehensiveTestbench) {
    sc_clock clk;
    sc_signal<bool> reset, stall;

    FPU_Pipeline_Top* fpu_top;

    int tests_passed = 0;
    int tests_failed = 0;

    void create_program() {
        vector<sc_uint<32>> program;

        fp_instruction_t inst1(OP_FADD, 3, 1, 2);
        fp_instruction_t inst2(OP_FSUB, 4, 1, 2);
        fp_instruction_t inst3(OP_FMUL, 5, 1, 2);
        fp_instruction_t inst4(OP_FDIV, 6, 1, 2);
        fp_instruction_t inst5(OP_FDIV, 7, 1, 8);
        fp_instruction_t inst6(OP_FADD, 9, 10, 11);
        fp_instruction_t inst7(OP_FMUL, 12, 13, 14);
        fp_instruction_t inst8(OP_FMUL, 15, 16, 17);
        fp_instruction_t inst9(OP_FADD, 18, 19, 20);
        fp_instruction_t inst10(OP_FMUL, 21, 22, 23);

        program.push_back(inst1.to_word());
        program.push_back(inst2.to_word());
        program.push_back(inst3.to_word());
        program.push_back(inst4.to_word());
        program.push_back(inst5.to_word());
        program.push_back(inst6.to_word());
        program.push_back(inst7.to_word());
        program.push_back(inst8.to_word());
        program.push_back(inst9.to_word());
        program.push_back(inst10.to_word());

        // load to Fetch ROM through top module
        fpu_top->fetch_stage->load_program(program.data(), (int)program.size());
    }

    void setup_regs() {
        // f1=3.0f, f2=2.0f
        fpu_top->decode_stage->set_register_bits(1, float_to_ieee754_bits(3.0f));
        fpu_top->decode_stage->set_register_bits(2, float_to_ieee754_bits(2.0f));

        // Special
        fpu_top->decode_stage->fp_registers[8]  = 0;            // zero
        fpu_top->decode_stage->fp_registers[10] = 0x7F800000;   // +inf
        fpu_top->decode_stage->fp_registers[11] = 0xFF800000;   // -inf

        // Large (overflow when multiplied)
        fpu_top->decode_stage->fp_registers[16] = 0x7F000000;
        fpu_top->decode_stage->fp_registers[17] = 0x7F000000;

        // Tiny (underflow when multiplied)
        fpu_top->decode_stage->fp_registers[13] = 0x00800000;
        fpu_top->decode_stage->fp_registers[14] = 0x00800000;

        // Denorms
        fpu_top->decode_stage->fp_registers[19] = 0x00400000;
        fpu_top->decode_stage->fp_registers[20] = 0x00200000;
        fpu_top->decode_stage->fp_registers[22] = 0x00100000;
        fpu_top->decode_stage->fp_registers[23] = 0x3F800000; // 1.0f

        cout << "\nTest register setup complete.\n";
    }

    bool check_result_f(int reg, float expected, const string& name) {
        float actual = ieee754_bits_to_float(fpu_top->decode_stage->fp_registers[reg]);
        bool pass = fabs(actual - expected) < 1e-6f;
        cout << name << ": f" << reg << " = " << actual << " (exp " << expected << ") - "
             << (pass ? "PASS" : "FAIL") << "\n";
        if (pass) tests_passed++; else tests_failed++;
        return pass;
    }

    void check_excs(const string& phase) {
        sc_uint<8> flags = fpu_top->decode_stage->get_exception_flags();
        cout << "\n--- Exception Status (" << phase << ") ---\n";
        if (flags & FP_INVALID_OP)     cout << "⚠️  Invalid Operation\n";
        if (flags & FP_OVERFLOW)       cout << "⚠️  Overflow\n";
        if (flags & FP_UNDERFLOW)      cout << "⚠️  Underflow\n";
        if (flags & FP_DIVIDE_BY_ZERO) cout << "⚠️  Divide by Zero\n";
        if (flags & FP_INEXACT)        cout << "⚠️  Inexact\n";
        if (flags == 0) cout << "✅ No exceptions\n";
    }

    void test_thread() {
        cout << "\n=== FPU PIPELINE (Synthesizable RTL + TB) ===\n";

        // Reset
        reset.write(true);
        stall.write(false);
        wait(5, SC_NS);
        reset.write(false);
        wait(5, SC_NS);

        setup_regs();
        create_program();

        cout << "Running...\n";
        int max_cycles = 140;
        for (int c = 0; c < max_cycles; ++c) {
            wait(10, SC_NS);

            if (c == 40) {
                cout << "\n--- Basic Ops @ cycle " << c << " ---\n";
                check_result_f(3, 5.0f, "FADD 3+2");
                check_result_f(4, 1.0f, "FSUB 3-2");
                check_result_f(5, 6.0f, "FMUL 3*2");
                check_excs("Basic");
            }
            if (c == 75) {
                cout << "\n--- Division & Exceptions @ cycle " << c << " ---\n";
                check_result_f(6, 1.5f, "FDIV 3/2");

                sc_uint<32> f7 = fpu_top->decode_stage->fp_registers[7];
                ieee754_components comp7 = decompose_ieee754_dbg(f7);
                if (comp7.is_infinity && !comp7.sign) { cout << "FDIV by zero -> +inf : PASS\n"; tests_passed++; }
                else { cout << "FDIV by zero wrong\n"; tests_failed++; }
                check_excs("Division");
            }
            if (c == 100) {
                cout << "\n--- Special Cases @ cycle " << c << " ---\n";
                sc_uint<32> f9  = fpu_top->decode_stage->fp_registers[9];
                ieee754_components c9 = decompose_ieee754_dbg(f9);
                if (c9.is_nan) { cout << "inf + (-inf) -> NaN : PASS\n"; tests_passed++; }
                else { cout << "inf + (-inf) failed\n"; tests_failed++; }

                sc_uint<32> f12 = fpu_top->decode_stage->fp_registers[12];
                ieee754_components c12 = decompose_ieee754_dbg(f12);
                if (c12.is_zero || c12.is_denormalized) { cout << "Underflow MUL tiny*tiny : PASS\n"; tests_passed++; }
                else { cout << "Underflow test failed\n"; tests_failed++; }

                sc_uint<32> f15 = fpu_top->decode_stage->fp_registers[15];
                ieee754_components c15 = decompose_ieee754_dbg(f15);
                if (c15.is_infinity) { cout << "Overflow MUL large*large : PASS\n"; tests_passed++; }
                else { cout << "Overflow test failed\n"; tests_failed++; }

                check_excs("Special");
            }
            if (c == 120) {
                cout << "\n--- Denorm tests @ cycle " << c << " ---\n";
                sc_uint<32> f18 = fpu_top->decode_stage->fp_registers[18];
                ieee754_components c18 = decompose_ieee754_dbg(f18);
                cout << "Denorm ADD f18 = " << ieee754_bits_to_float(f18)
                     << " (" << (c18.is_denormalized ? "denorm" : (c18.is_zero ? "zero" : "normal")) << ")\n";

                sc_uint<32> f21 = fpu_top->decode_stage->fp_registers[21];
                ieee754_components c21 = decompose_ieee754_dbg(f21);
                cout << "Denorm MUL f21 = " << ieee754_bits_to_float(f21)
                     << " (" << (c21.is_denormalized ? "denorm" : (c21.is_zero ? "zero" : "normal")) << ")\n";

                check_excs("Denorm");
            }
        }

        cout << "\n=== FINAL SUMMARY ===\n";
        cout << "Passed: " << tests_passed << "  Failed: " << tests_failed << "\n";
        sc_stop();
    }

    SC_CTOR(ComprehensiveTestbench) : clk("clk", 10, SC_NS) {
        // Create the top-level FPU pipeline module
        fpu_top = new FPU_Pipeline_Top("fpu_top");
        
        // Connect clock, reset, and stall to the top module
        fpu_top->clk(clk);
        fpu_top->reset(reset);
        fpu_top->stall(stall);

        SC_THREAD(test_thread);
    }

    ~ComprehensiveTestbench() {
        delete fpu_top;
    }
};

// ---------------- MAIN (TB only) ----------------
int sc_main(int argc, char* argv[]) {
    cout << "=== FPU PIPELINE (Synth-Ready RTL + TB) ===\n";
    ComprehensiveTestbench tb("tb");
    sc_start();
    cout << "\nSimulation done.\n";
    return 0;
}
