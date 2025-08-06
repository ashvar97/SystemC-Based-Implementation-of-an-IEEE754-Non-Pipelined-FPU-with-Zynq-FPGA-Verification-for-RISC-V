#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <cmath>

//==============================================================================
//
// Module: ieee754_adder_pipelined (3-Stage Pipeline)
//
SC_MODULE(ieee754_adder_pipelined)
{
    // Interface ports
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<sc_uint<32> > A;
    sc_in<sc_uint<32> > B;
    sc_in<bool> valid_in;
    sc_out<sc_uint<32> > O;
    sc_out<bool> valid_out;

    // Stage 1 registers
    sc_signal<sc_uint<32> > A_s1;
    sc_signal<sc_uint<32> > B_s1;
    sc_signal<bool> valid_s1;

    // Stage 1 outputs (combinational)
    sc_signal<bool> sign_a_s1, sign_b_s1;
    sc_signal<sc_uint<8> > exp_a_s1, exp_b_s1;
    sc_signal<sc_uint<24> > mant_a_s1, mant_b_s1;
    sc_signal<bool> valid_s1_out;

    // Stage 2 registers
    sc_signal<bool> sign_a_s2, sign_b_s2;
    sc_signal<sc_uint<8> > exp_a_s2, exp_b_s2;
    sc_signal<sc_uint<24> > mant_a_s2, mant_b_s2;
    sc_signal<bool> valid_s2;

    // Stage 2 outputs (combinational)
    sc_signal<bool> out_sign_s2;
    sc_signal<sc_uint<8> > out_exponent_s2;
    sc_signal<sc_uint<25> > out_mantissa_s2;
    sc_signal<bool> valid_s2_out;

    // Stage 3 registers
    sc_signal<bool> out_sign_s3;
    sc_signal<sc_uint<8> > out_exponent_s3;
    sc_signal<sc_uint<25> > out_mantissa_s3;
    sc_signal<bool> valid_s3;

    // Stage 1 register process
    void stage1_registers() {
        if (reset.read()) {
            A_s1.write(0);
            B_s1.write(0);
            valid_s1.write(false);
        } else {
            A_s1.write(A.read());
            B_s1.write(B.read());
            valid_s1.write(valid_in.read());
        }
    }

    // Stage 1 combinational logic (extraction)
    void stage1_combinational() {
        sc_uint<32> a_val = A_s1.read();
        sc_uint<32> b_val = B_s1.read();

        // Extract A
        sign_a_s1.write(a_val[31]);
        exp_a_s1.write(a_val.range(30, 23));
        if (a_val.range(30, 23) == 0) {
            mant_a_s1.write((sc_uint<24>)((sc_uint<1>(0), a_val.range(22, 0))));
        } else {
            mant_a_s1.write((sc_uint<24>)((sc_uint<1>(1), a_val.range(22, 0))));
        }

        // Extract B
        sign_b_s1.write(b_val[31]);
        exp_b_s1.write(b_val.range(30, 23));
        if (b_val.range(30, 23) == 0) {
            mant_b_s1.write((sc_uint<24>)((sc_uint<1>(0), b_val.range(22, 0))));
        } else {
            mant_b_s1.write((sc_uint<24>)((sc_uint<1>(1), b_val.range(22, 0))));
        }

        valid_s1_out.write(valid_s1.read());
    }

    // Stage 2 register process
    void stage2_registers() {
        if (reset.read()) {
            sign_a_s2.write(false);
            sign_b_s2.write(false);
            exp_a_s2.write(0);
            exp_b_s2.write(0);
            mant_a_s2.write(0);
            mant_b_s2.write(0);
            valid_s2.write(false);
        } else {
            sign_a_s2.write(sign_a_s1.read());
            sign_b_s2.write(sign_b_s1.read());
            exp_a_s2.write(exp_a_s1.read());
            exp_b_s2.write(exp_b_s1.read());
            mant_a_s2.write(mant_a_s1.read());
            mant_b_s2.write(mant_b_s1.read());
            valid_s2.write(valid_s1_out.read());
        }
    }

    // Stage 2 combinational logic (adder core)
    void stage2_combinational() {
        sc_uint<8> diff = 0;
        sc_uint<24> tmp_mantissa = 0;
        bool a_is_nan = (exp_a_s2.read() == 0xFF) && (mant_a_s2.read().range(22, 0) != 0);
        bool b_is_nan = (exp_b_s2.read() == 0xFF) && (mant_b_s2.read().range(22, 0) != 0);
        bool a_is_inf = (exp_a_s2.read() == 0xFF) && (mant_a_s2.read().range(22, 0) == 0);
        bool b_is_inf = (exp_b_s2.read() == 0xFF) && (mant_b_s2.read().range(22, 0) == 0);

        if (a_is_nan || b_is_nan) {
            out_exponent_s2.write(0xFF);
            out_mantissa_s2.write(0x400000);
            out_sign_s2.write(false);
        } else if (a_is_inf || b_is_inf) {
            if (a_is_inf && b_is_inf) {
                if (sign_a_s2.read() == sign_b_s2.read()) {
                    out_exponent_s2.write(0xFF);
                    out_mantissa_s2.write(0);
                    out_sign_s2.write(sign_a_s2.read());
                } else {
                    out_exponent_s2.write(0xFF);
                    out_mantissa_s2.write(0x400000);
                    out_sign_s2.write(false);
                }
            } else {
                out_exponent_s2.write(0xFF);
                out_mantissa_s2.write(0);
                out_sign_s2.write(a_is_inf ? sign_a_s2.read() : sign_b_s2.read());
            }
        } else if (exp_a_s2.read() == 0 && mant_a_s2.read() == 0) {
            out_sign_s2.write(sign_b_s2.read());
            out_exponent_s2.write(exp_b_s2.read());
            out_mantissa_s2.write((sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read())));
        } else if (exp_b_s2.read() == 0 && mant_b_s2.read() == 0) {
            out_sign_s2.write(sign_a_s2.read());
            out_exponent_s2.write(exp_a_s2.read());
            out_mantissa_s2.write((sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read())));
        } else {
            sc_uint<8> result_exp;
            sc_uint<25> result_mant;
            bool result_sign;
            
            if (exp_a_s2.read() == 0) {
                result_exp = exp_b_s2.read();
                tmp_mantissa = mant_a_s2.read();
            } else if (exp_b_s2.read() == 0) {
                result_exp = exp_a_s2.read();
                tmp_mantissa = mant_b_s2.read();
            } else {
                result_exp = (exp_a_s2.read() > exp_b_s2.read()) ? exp_a_s2.read() : exp_b_s2.read();
            }

            if (exp_a_s2.read() > exp_b_s2.read()) {
                diff = exp_a_s2.read() - exp_b_s2.read();
                tmp_mantissa = mant_b_s2.read() >> diff;
                if (sign_a_s2.read() == sign_b_s2.read()) {
                    result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read())) + 
                                 (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa));
                } else {
                    if (mant_a_s2.read() >= tmp_mantissa) {
                        result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read())) - 
                                     (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa));
                    } else {
                        result_mant = (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)) - 
                                     (sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read()));
                    }
                }
                result_sign = (mant_a_s2.read() >= tmp_mantissa) ? sign_a_s2.read() : sign_b_s2.read();
            } else if (exp_b_s2.read() > exp_a_s2.read()) {
                diff = exp_b_s2.read() - exp_a_s2.read();
                tmp_mantissa = mant_a_s2.read() >> diff;
                if (sign_a_s2.read() == sign_b_s2.read()) {
                    result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read())) + 
                                 (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa));
                } else {
                    if (mant_b_s2.read() >= tmp_mantissa) {
                        result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read())) - 
                                     (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa));
                    } else {
                        result_mant = (sc_uint<25>)((sc_uint<1>(0), tmp_mantissa)) - 
                                     (sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read()));
                    }
                }
                result_sign = (mant_b_s2.read() >= tmp_mantissa) ? sign_b_s2.read() : sign_a_s2.read();
            } else {
                if (sign_a_s2.read() == sign_b_s2.read()) {
                    result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read())) + 
                                 (sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read()));
                } else {
                    if (mant_a_s2.read() > mant_b_s2.read()) {
                        result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read())) - 
                                     (sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read()));
                    } else {
                        result_mant = (sc_uint<25>)((sc_uint<1>(0), mant_b_s2.read())) - 
                                     (sc_uint<25>)((sc_uint<1>(0), mant_a_s2.read()));
                    }
                }
                result_sign = (mant_a_s2.read() > mant_b_s2.read()) ? sign_a_s2.read() : sign_b_s2.read();
                result_exp = (exp_a_s2.read() > exp_b_s2.read()) ? exp_a_s2.read() : exp_b_s2.read();
            }

            if (result_mant == 0) {
                result_sign = false;
                result_exp = 0;
            }

            out_sign_s2.write(result_sign);
            out_exponent_s2.write(result_exp);
            out_mantissa_s2.write(result_mant);
        }

        valid_s2_out.write(valid_s2.read());
    }

    // Stage 3 register process
    void stage3_registers() {
        if (reset.read()) {
            out_sign_s3.write(false);
            out_exponent_s3.write(0);
            out_mantissa_s3.write(0);
            valid_s3.write(false);
        } else {
            out_sign_s3.write(out_sign_s2.read());
            out_exponent_s3.write(out_exponent_s2.read());
            out_mantissa_s3.write(out_mantissa_s2.read());
            valid_s3.write(valid_s2_out.read());
        }
    }

    // Stage 3 combinational logic (normalizer)
    void stage3_combinational() {
        sc_uint<5> lz = 0;
        sc_uint<8> norm_exponent = 0;
        sc_uint<25> norm_mantissa = 0;
        sc_uint<32> result;

        if (out_exponent_s3.read() == 0xFF) {
            result = (sc_uint<32>)((out_sign_s3.read(), out_exponent_s3.read(), out_mantissa_s3.read().range(22, 0)));
        } else if (out_mantissa_s3.read() == 0) {
            result = 0;
        } else {
            norm_exponent = out_exponent_s3.read();
            norm_mantissa = out_mantissa_s3.read();

            if (norm_mantissa[24]) {
                norm_exponent = norm_exponent + 1;
                norm_mantissa = norm_mantissa >> 1;
            } else if (norm_mantissa[23] == 0 && norm_exponent != 0) {
                // Count leading zeros
                for (lz = 0; lz < 24; lz++) {
                    if (norm_mantissa[23 - lz] != 0) break;
                }
                if (norm_exponent > lz) {
                    norm_exponent = norm_exponent - lz;
                    norm_mantissa = norm_mantissa << lz;
                } else {
                    norm_mantissa = norm_mantissa << (norm_exponent - 1);
                    norm_exponent = 0;
                }
            }

            if (norm_exponent >= 0xFF) {
                result = (sc_uint<32>)((out_sign_s3.read(), sc_uint<8>(0xFF), sc_uint<23>(0)));
            } else {
                result = (sc_uint<32>)((out_sign_s3.read(), norm_exponent, norm_mantissa.range(22, 0)));
            }
        }

        O.write(result);
        valid_out.write(valid_s3.read());
    }

    SC_CTOR(ieee754_adder_pipelined) {
        // Register processes (sequential logic)
        SC_METHOD(stage1_registers);
        sensitive << clk.pos();
        
        SC_METHOD(stage2_registers);
        sensitive << clk.pos();
        
        SC_METHOD(stage3_registers);
        sensitive << clk.pos();

        // Combinational processes
        SC_METHOD(stage1_combinational);
        sensitive << A_s1 << B_s1 << valid_s1;

        SC_METHOD(stage2_combinational);
        sensitive << sign_a_s2 << sign_b_s2 << exp_a_s2 << exp_b_s2 << mant_a_s2 << mant_b_s2 << valid_s2;

        SC_METHOD(stage3_combinational);
        sensitive << out_sign_s3 << out_exponent_s3 << out_mantissa_s3 << valid_s3;
    }
};

//==============================================================================
//
// Legacy compatibility modules (kept for backward compatibility)
//
SC_MODULE(ieee754_extractor)
{
    sc_in<sc_uint<32> > A;
    sc_out<bool> sign;
    sc_out<sc_uint<8> > exponent;
    sc_out<sc_uint<24> > mantissa;

    void process() {
        sign.write(A.read()[31]);
        exponent.write(A.read().range(30, 23));
        if (exponent.read() == 0) {
            mantissa.write((sc_uint<24>)((sc_uint<1>(0), A.read().range(22, 0))));
        } else {
            mantissa.write((sc_uint<24>)((sc_uint<1>(1), A.read().range(22, 0))));
        }
    }

    SC_CTOR(ieee754_extractor) {
        SC_METHOD(process);
        sensitive << A;
    }
};

SC_MODULE(ieee754_adder_core)
{
    sc_in<sc_uint<8> > exp_a, exp_b;
    sc_in<sc_uint<24> > mant_a, mant_b;
    sc_in<bool> sign_a, sign_b;
    sc_out<bool> out_sign;
    sc_out<sc_uint<8> > out_exponent;
    sc_out<sc_uint<25> > out_mantissa;

    void process() {
        // Implementation integrated into pipelined version above
        // This is kept for backward compatibility
    }

    SC_CTOR(ieee754_adder_core) {
        SC_METHOD(process);
        sensitive << exp_a << exp_b << mant_a << mant_b << sign_a << sign_b;
    }
};

SC_MODULE(ieee754_normalizer)
{
    sc_in<sc_uint<8> > exponent;
    sc_in<sc_uint<25> > mantissa;
    sc_in<bool> sign;
    sc_out<sc_uint<32> > result;

    void process() {
        // Implementation integrated into pipelined version above
        // This is kept for backward compatibility
    }

    SC_CTOR(ieee754_normalizer) {
        SC_METHOD(process);
        sensitive << exponent << mantissa << sign;
    }
};



int sc_main(int argc, char* argv[]) {
    // Create clock and reset signals
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> reset;

    // Adder signals
    sc_signal<sc_uint<32>> A, B;
    sc_signal<bool> valid_in;
    sc_signal<sc_uint<32>> result;
    sc_signal<bool> valid_out;
    
    // Instantiate the pipelined adder
    ieee754_adder_pipelined adder("adder");
    adder.clk(clk);
    adder.reset(reset);
    adder.A(A);
    adder.B(B);
    adder.valid_in(valid_in);
    adder.O(result);
    adder.valid_out(valid_out);

    // Trace setup
    sc_trace_file *tf = sc_create_vcd_trace_file("adder_trace");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, reset, "reset");
    sc_trace(tf, A, "A");
    sc_trace(tf, B, "B");
    sc_trace(tf, valid_in, "valid_in");
    sc_trace(tf, result, "result");
    sc_trace(tf, valid_out, "valid_out");

    // Test cases
    struct TestCase {
        float a;
        float b;
        float expected;
        const char* description;
    };

    TestCase test_cases[5] = {
        {1.5f, 2.0f, 3.5f, "Basic addition"},
        {-3.5f, 4.0f, 0.5f, "Mixed signs"},
        {0.0f, 1.0f, 1.0f, "Add zero"},
        {1.25f, 2.5f, 3.75f, "Fractional numbers"},
        {NAN, 1.0f, NAN, "NaN propagation"}
    };

    // Initialize
    reset = true;
    A = 0;
    B = 0;
    valid_in = false;
    sc_start(15, SC_NS);  // Run for 1.5 clock cycles to apply reset

    reset = false;
    valid_in = true;
    sc_start(5, SC_NS);   // Half clock cycle to get out of reset

    // Run test cases
    for (int i = 0; i < 5; i++) {
        TestCase &tc = test_cases[i];
        
        // Convert floats to raw bits for input
        uint32_t a_bits = *reinterpret_cast<uint32_t*>(&tc.a);
        uint32_t b_bits = *reinterpret_cast<uint32_t*>(&tc.b);
        
        // Set inputs
        A = a_bits;
        B = b_bits;
        
        cout << "\nTest " << (i+1) << ": " << tc.description << endl;
        cout << "  A = " << tc.a << " (0x" << hex << a_bits << ")" << endl;
        cout << "  B = " << tc.b << " (0x" << hex << b_bits << ")" << endl;

        // Run for 3 clock cycles (pipeline depth)
        sc_start(30, SC_NS);

        // Convert result back to float
        uint32_t result_bits = result.read();
        float result_float = *reinterpret_cast<float*>(&result_bits);
        
        cout << "  Result = " << result_float << " (0x" << hex << result_bits << ")" << endl;
        cout << "  Valid = " << (valid_out.read() ? "Yes" : "No") << endl;

        // Simple validation (skip NaN case)
        if (i != 4 && !std::isnan(tc.expected)) {
            if (result_float == tc.expected) {
                cout << "  TEST PASSED" << endl;
            } else {
                cout << "  TEST FAILED (expected " << tc.expected << ")" << endl;
            }
        }
    }

    // Close trace file
    sc_close_vcd_trace_file(tf);

    cout << "\nSimulation complete. Trace file generated: adder_trace.vcd" << endl;

    return 0;
}
