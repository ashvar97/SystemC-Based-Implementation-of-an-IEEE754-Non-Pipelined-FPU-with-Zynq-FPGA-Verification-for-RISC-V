#include <systemc.h>
#include <iostream>
#include <iomanip>
#include <cmath>  // Added for std::isnan and std::isinf


// FloatingPointExtractor Module - Stage 1
SC_MODULE(FloatingPointExtractor) {
    sc_in<sc_uint<32>> in;
    sc_in<bool> reset;
    sc_out<bool> sign;
    sc_out<sc_uint<8>> exponent;
    sc_out<sc_uint<24>> mantissa;
    sc_out<bool> is_nan;        // NEW: NaN detection
    sc_out<bool> is_zero;       // NEW: Zero detection
    sc_out<bool> is_inf;        // NEW: Infinity detection

    void extract() {
        if (reset.read()) {
            sign.write(false);
            exponent.write(0);
            mantissa.write(0);
            is_nan.write(false);
            is_zero.write(false);
            is_inf.write(false);
        } else {
            sc_uint<32> val = in.read();
            sc_uint<8> exp = val.range(30, 23);
            sc_uint<23> mant = val.range(22, 0);
            
            sign.write(val[31]);
            exponent.write(exp);
            mantissa.write((sc_uint<24>(1) << 23) | mant); 
            
            // NEW: Special case detection
            is_nan.write(exp == 0xFF && mant != 0);
            is_zero.write(exp == 0 && mant == 0);
            is_inf.write(exp == 0xFF && mant == 0);
        }
    }

    SC_CTOR(FloatingPointExtractor) {
        SC_METHOD(extract);
        sensitive << in << reset;
    }
};

// FloatingPointMultiplier Module - Stage 2
SC_MODULE(FloatingPointMultiplier) {
    sc_in<sc_uint<24>> A_Mantissa;
    sc_in<sc_uint<24>> B_Mantissa;
    sc_in<sc_uint<8>> A_Exponent;
    sc_in<sc_uint<8>> B_Exponent;
    sc_in<bool> A_sign;
    sc_in<bool> B_sign;
    sc_in<bool> A_is_nan;       // NEW
    sc_in<bool> A_is_zero;      // NEW
    sc_in<bool> A_is_inf;       // NEW
    sc_in<bool> B_is_nan;       // NEW
    sc_in<bool> B_is_zero;      // NEW
    sc_in<bool> B_is_inf;       // NEW
    sc_in<bool> reset;
    sc_out<sc_uint<48>> Temp_Mantissa;
    sc_out<sc_uint<8>> Temp_Exponent;
    sc_out<bool> Sign;
    sc_out<bool> result_is_nan; // NEW

    void multiply() {
        if (reset.read()) {
            Temp_Mantissa.write(0);
            Temp_Exponent.write(0);
            Sign.write(false);
            result_is_nan.write(false);
        } else {
            // NEW: NaN propagation and invalid operation detection
            bool nan_result = A_is_nan.read() || B_is_nan.read() || 
                             (A_is_zero.read() && B_is_inf.read()) ||
                             (A_is_inf.read() && B_is_zero.read());
            
            result_is_nan.write(nan_result);
            
            // Your existing logic (unchanged)
            Temp_Mantissa.write(A_Mantissa.read() * B_Mantissa.read());
            Temp_Exponent.write(A_Exponent.read() + B_Exponent.read() - 127);
            Sign.write(A_sign.read() ^ B_sign.read());
        }
    }

    SC_CTOR(FloatingPointMultiplier) {
        SC_METHOD(multiply);
        sensitive << A_Mantissa << B_Mantissa << A_Exponent << B_Exponent 
                 << A_sign << B_sign << A_is_nan << A_is_zero << A_is_inf
                 << B_is_nan << B_is_zero << B_is_inf << reset;
    }
};

// FloatingPointNormalizer Module - Stage 3
SC_MODULE(FloatingPointNormalizer) {
    sc_in<sc_uint<48>> Temp_Mantissa;
    sc_in<sc_uint<8>> Temp_Exponent;
    sc_in<bool> Sign;
    sc_in<bool> result_is_nan;  // NEW
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    void normalize() {
        if (reset.read()) {
            result.write(0);
        } else {
            // NEW: Handle NaN first
            if (result_is_nan.read()) {
                // Return quiet NaN: sign=0, exp=0xFF, mantissa with MSB=1
                result.write((sc_uint<1>(0), sc_uint<8>(0xFF), sc_uint<23>(0x400000)));
                return;
            }
            
            // Your existing normalization logic (unchanged)
            sc_uint<23> Mantissa;
            sc_uint<8> Exponent;
            sc_uint<48> temp_mant = Temp_Mantissa.read();

            if (temp_mant[47]) {
                Mantissa = temp_mant.range(46, 24);
                Exponent = Temp_Exponent.read() + 1;
            } else {
                Mantissa = temp_mant.range(45, 23);
                Exponent = Temp_Exponent.read();
            }

            // MODIFIED: Better special case handling
            if (Temp_Exponent.read() == 0) {
                // Zero case
                result.write(0);
            } else if (Exponent == 0xFF) {
                // Infinity case - preserve sign
                result.write((Sign.read(), sc_uint<8>(0xFF), sc_uint<23>(0)));
            } else {
                // Normal case
                result.write((Sign.read(), Exponent, Mantissa));
            }
        }
    }

    SC_CTOR(FloatingPointNormalizer) {
        SC_METHOD(normalize);
        sensitive << Temp_Mantissa << Temp_Exponent << Sign << result_is_nan << reset;
    }
};

// Top-level pipelined multiplier
SC_MODULE(ieee754mult_pipelined) {
    sc_in<sc_uint<32>> A;
    sc_in<sc_uint<32>> B;
    sc_in<bool> reset;
    sc_in<bool> clk;
    sc_out<sc_uint<32>> result;
    sc_out<bool> valid_out;

    // Pipeline registers between stages
    sc_signal<bool> A_sign_stage1, B_sign_stage1;
    sc_signal<sc_uint<8>> A_Exponent_stage1, B_Exponent_stage1;
    sc_signal<sc_uint<24>> A_Mantissa_stage1, B_Mantissa_stage1;
    sc_signal<bool> A_is_nan_stage1, A_is_zero_stage1, A_is_inf_stage1;  // NEW
    sc_signal<bool> B_is_nan_stage1, B_is_zero_stage1, B_is_inf_stage1;  // NEW
    sc_signal<bool> valid_stage1;

    sc_signal<bool> Sign_stage2;
    sc_signal<sc_uint<8>> Temp_Exponent_stage2;
    sc_signal<sc_uint<48>> Temp_Mantissa_stage2;
    sc_signal<bool> result_is_nan_stage2;  // NEW
    sc_signal<bool> valid_stage2;

    // Submodule instances
    FloatingPointExtractor extractA;
    FloatingPointExtractor extractB;
    FloatingPointMultiplier multiply;
    FloatingPointNormalizer normalize;

    SC_CTOR(ieee754mult_pipelined) : 
        extractA("extractA"), extractB("extractB"), 
        multiply("multiply"), normalize("normalize") 
    {
        // Stage 1: Extraction
        extractA.in(A);
        extractA.reset(reset);
        extractA.sign(A_sign_stage1);
        extractA.exponent(A_Exponent_stage1);
        extractA.mantissa(A_Mantissa_stage1);
        extractA.is_nan(A_is_nan_stage1);      // NEW
        extractA.is_zero(A_is_zero_stage1);    // NEW
        extractA.is_inf(A_is_inf_stage1);      // NEW

        extractB.in(B);
        extractB.reset(reset);
        extractB.sign(B_sign_stage1);
        extractB.exponent(B_Exponent_stage1);
        extractB.mantissa(B_Mantissa_stage1);
        extractB.is_nan(B_is_nan_stage1);      // NEW
        extractB.is_zero(B_is_zero_stage1);    // NEW
        extractB.is_inf(B_is_inf_stage1);      // NEW

        // Stage 2: Multiplication
        multiply.A_Mantissa(A_Mantissa_stage1);
        multiply.B_Mantissa(B_Mantissa_stage1);
        multiply.A_Exponent(A_Exponent_stage1);
        multiply.B_Exponent(B_Exponent_stage1);
        multiply.A_sign(A_sign_stage1);
        multiply.B_sign(B_sign_stage1);
        multiply.A_is_nan(A_is_nan_stage1);    // NEW
        multiply.A_is_zero(A_is_zero_stage1);  // NEW
        multiply.A_is_inf(A_is_inf_stage1);    // NEW
        multiply.B_is_nan(B_is_nan_stage1);    // NEW
        multiply.B_is_zero(B_is_zero_stage1);  // NEW
        multiply.B_is_inf(B_is_inf_stage1);    // NEW
        multiply.reset(reset);
        multiply.Temp_Mantissa(Temp_Mantissa_stage2);
        multiply.Temp_Exponent(Temp_Exponent_stage2);
        multiply.Sign(Sign_stage2);
        multiply.result_is_nan(result_is_nan_stage2);  // NEW

        // Stage 3: Normalization
        normalize.Temp_Mantissa(Temp_Mantissa_stage2);
        normalize.Temp_Exponent(Temp_Exponent_stage2);
        normalize.Sign(Sign_stage2);
        normalize.result_is_nan(result_is_nan_stage2); // NEW
        normalize.reset(reset);
        normalize.result(result);

        // Pipeline control (unchanged)
        SC_CTHREAD(pipeline_control, clk.pos());
        reset_signal_is(reset, true);
    }

    void pipeline_control() {
        valid_stage1 = false;
        valid_stage2 = false;
        valid_out = false;
        wait();

        while (true) {
            // Stage 1 valid
            valid_stage1 = !reset.read();
            
            // Stage 2 valid (delayed by 1 cycle)
            valid_stage2 = valid_stage1;
            
            // Output valid (delayed by 2 cycles)
            valid_out = valid_stage2;
            
            wait();
        }
    }
};

int sc_main(int argc, char* argv[]) {
    // Create signals
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<sc_uint<32>> A, B;
    sc_signal<sc_uint<32>> result;
    sc_signal<bool> reset, valid_out;

    // Instantiate the pipelined multiplier
    ieee754mult_pipelined multiplier("multiplier");
    multiplier.A(A);
    multiplier.B(B);
    multiplier.reset(reset);
    multiplier.clk(clk);
    multiplier.result(result);
    multiplier.valid_out(valid_out);

    // Open VCD file for tracing
    sc_trace_file *tf = sc_create_vcd_trace_file("multiplier_trace");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, reset, "reset");
    sc_trace(tf, A, "A");
    sc_trace(tf, B, "B");
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
        {1.5f, 2.0f, 3.0f, "Normal multiplication"},
        {-3.5f, 4.0f, -14.0f, "Negative multiplication"},
        {0.0f, 1.0f, 0.0f, "Multiply by zero"},
        {NAN, 1.0f, NAN, "NaN propagation"}
    };

    // Initialize
    reset = true;
    A = 0;
    B = 0;
    sc_start(15, SC_NS);  // Run for 1.5 clock cycles to apply reset

    reset = false;
    sc_start(5, SC_NS);   // Half clock cycle to get out of reset

    // Run test cases
    for (int i = 0; i < 5; i++) {
        TestCase &tc = test_cases[i];
        
        // Convert floats to raw bits for input
        uint32_t a_bits = *reinterpret_cast<uint32_t*>(&tc.a);
        uint32_t b_bits = *reinterpret_cast<uint32_t*>(&tc.b);
        A = a_bits;
        B = b_bits;
        
        cout << "Test " << (i+1) << ": " << tc.description << endl;
        cout << "  A = " << tc.a << " (0x" << hex << A.read() << ")" << endl;
        cout << "  B = " << tc.b << " (0x" << hex << B.read() << ")" << endl;

        // Run for 3 clock cycles (pipeline depth)
        sc_start(30, SC_NS);

        // Convert result back to float
        uint32_t result_bits = result.read();
        float result_float = *reinterpret_cast<float*>(&result_bits);
        
        cout << "  Result = " << result_float << " (0x" << hex << result.read() << ")" << endl;
        
        // Check if result is valid (pipeline has filled)
        if (valid_out.read()) {
            cout << "  VALID output" << endl;
        } else {
            cout << "  INVALID output (pipeline not filled yet)" << endl;
        }

        // Simple validation (skip NaN case)
        if (i != 4 && !std::isnan(tc.expected)) {
            if (result_float == tc.expected || 
                (std::isinf(result_float) && std::isinf(tc.expected))) {
                cout << "  TEST PASSED" << endl;
            } else {
                cout << "  TEST FAILED (expected " << tc.expected << ")" << endl;
            }
        }
        cout << endl;
    }

    // Close trace file
    sc_close_vcd_trace_file(tf);

    return 0;
}
