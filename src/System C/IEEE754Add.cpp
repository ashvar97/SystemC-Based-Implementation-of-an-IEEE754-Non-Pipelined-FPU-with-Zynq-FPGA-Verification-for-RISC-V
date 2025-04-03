#include <systemc.h>

//==============================================================================
//
// Module: ieee754_extractor
//
SC_MODULE(ieee754_extractor) {
    sc_in<sc_uint<32>>  a;
    sc_out<bool>        sign;
    sc_out<sc_uint<8>>  exponent;
    sc_out<sc_uint<24>> mantissa;

    void extract() {
        sign.write(a.read()[31]);
        exponent.write(a.read().range(30, 23));
        
        // Handle denormal numbers (exponent == 0)
        if (exponent.read() == 0) {
            mantissa.write((0, a.read().range(22, 0))); // No implicit leading 1
        } else {
            mantissa.write((1, a.read().range(22, 0))); // Add implicit leading 1
        }
    }

    SC_CTOR(ieee754_extractor) {
        SC_METHOD(extract);
        sensitive << a;
    }
};

//==============================================================================
//
// Module: ieee754_adder_core
//
SC_MODULE(ieee754_adder_core) {
    sc_in<sc_uint<8>>   exp_a, exp_b;
    sc_in<sc_uint<24>>  mant_a, mant_b;
    sc_in<bool>         sign_a, sign_b;
    sc_out<bool>        out_sign;
    sc_out<sc_uint<8>>  out_exponent;
    sc_out<sc_uint<25>> out_mantissa;

    void add() {
        // Special value detection
        const bool a_is_nan = (exp_a.read() == 0xFF) && (mant_a.read().range(22, 0) != 0);
        const bool b_is_nan = (exp_b.read() == 0xFF) && (mant_b.read().range(22, 0) != 0);
        const bool a_is_inf = (exp_a.read() == 0xFF) && (mant_a.read().range(22, 0) == 0);
        const bool b_is_inf = (exp_b.read() == 0xFF) && (mant_b.read().range(22, 0) == 0);
        const bool a_is_zero = (exp_a.read() == 0) && (mant_a.read() == 0);
        const bool b_is_zero = (exp_b.read() == 0) && (mant_b.read() == 0);

        // Handle special cases
        if (a_is_nan || b_is_nan) {
            out_exponent.write(0xFF);
            out_mantissa.write(0x400000); // Quiet NaN
            out_sign.write(false);
            return;
        }

        if (a_is_inf || b_is_inf) {
            out_exponent.write(0xFF);
            out_mantissa.write(0);
            
            if (a_is_inf && b_is_inf) {
                out_sign.write(sign_a.read() == sign_b.read() ? sign_a.read() : false);
                if (sign_a.read() != sign_b.read()) {
                    out_mantissa.write(0x400000); // inf - inf = NaN
                }
            } else {
                out_sign.write(a_is_inf ? sign_a.read() : sign_b.read());
            }
            return;
        }

        // Handle zero cases
        if (a_is_zero) {
            out_sign.write(sign_b.read());
            out_exponent.write(exp_b.read());
            out_mantissa.write((0, mant_b.read())));
            return;
        }

        if (b_is_zero) {
            out_sign.write(sign_a.read());
            out_exponent.write(exp_a.read());
            out_mantissa.write((0, mant_a.read())));
            return;
        }

        // Normal number processing
        sc_uint<8> exp_diff;
        sc_uint<24> shifted_mant;
        
        // Determine larger exponent
        if (exp_a.read() > exp_b.read()) {
            out_exponent.write(exp_a.read());
            exp_diff = exp_a.read() - exp_b.read();
            shifted_mant = mant_b.read() >> exp_diff;
            process_mantissas(mant_a.read(), shifted_mant, sign_a.read(), sign_b.read());
        } 
        else if (exp_b.read() > exp_a.read()) {
            out_exponent.write(exp_b.read());
            exp_diff = exp_b.read() - exp_a.read();
            shifted_mant = mant_a.read() >> exp_diff;
            process_mantissas(mant_b.read(), shifted_mant, sign_b.read(), sign_a.read());
        } 
        else { // Equal exponents
            out_exponent.write(exp_a.read());
            process_mantissas(mant_a.read(), mant_b.read(), sign_a.read(), sign_b.read());
        }

        // Handle zero result
        if (out_mantissa.read() == 0) {
            out_sign.write(false);
            out_exponent.write(0);
        }
    }

    void process_mantissas(sc_uint<24> mant1, sc_uint<24> mant2, bool sign1, bool sign2) {
        sc_uint<25> m1 = (0, mant1);
        sc_uint<25> m2 = (0, mant2);
        sc_uint<25> res;

        if (sign1 == sign2) { // Addition
            res = m1 + m2;
            out_sign.write(sign1);
        } 
        else { // Subtraction
            if (mant1 >= mant2) {
                res = m1 - m2;
                out_sign.write(sign1);
            } else {
                res = m2 - m1;
                out_sign.write(sign2);
            }
        }
        out_mantissa.write(res);
    }

    SC_CTOR(ieee754_adder_core) {
        SC_METHOD(add);
        sensitive << exp_a << exp_b << mant_a << mant_b << sign_a << sign_b;
    }
};

//==============================================================================
//
// Module: ieee754_normalizer
//
SC_MODULE(ieee754_normalizer) {
    sc_in<sc_uint<8>>   exponent;
    sc_in<sc_uint<25>>  mantissa;
    sc_in<bool>         sign;
    sc_out<sc_uint<32>> result;

    void normalize() {
        // Handle special cases
        if (exponent.read() == 0xFF) { // Infinity/NaN
            result.write((sign.read(), exponent.read(), mantissa.read().range(22, 0)));
            return;
        }

        if (mantissa.read() == 0) { // Zero
            result.write(0);
            return;
        }

        // Normalize the result
        sc_uint<8> norm_exp = exponent.read();
        sc_uint<25> norm_mant = mantissa.read();

        // Handle overflow (mantissa too large)
        if (norm_mant[24]) {
            norm_exp++;
            norm_mant >>= 1;
        } 
        // Handle underflow (mantissa too small)
        else if (!norm_mant[23] && norm_exp != 0) {
            sc_uint<5> leading_zeros = 0;
            
            // Count leading zeros
            for (int i = 23; i >= 0; i--) {
                if (norm_mant[i]) break;
                leading_zeros++;
            }

            // Adjust exponent and shift mantissa
            if (norm_exp > leading_zeros) {
                norm_exp -= leading_zeros;
                norm_mant <<= leading_zeros;
            } else {
                norm_mant <<= (norm_exp - 1);
                norm_exp = 0;
            }
        }

        // Check for exponent overflow
        if (norm_exp >= 0xFF) {
            result.write(0); // Underflow to zero
        } else {
            result.write((sign.read(), norm_exp, norm_mant.range(22, 0)));
        }
    }

    SC_CTOR(ieee754_normalizer) {
        SC_METHOD(normalize);
        sensitive << exponent << mantissa << sign;
    }
};

//==============================================================================
//
// Module: ieee754_adder
//
SC_MODULE(ieee754_adder) {
    sc_in<sc_uint<32>>  a, b;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<bool>        sign_a, sign_b, out_sign;
    sc_signal<sc_uint<8>>  exp_a, exp_b, out_exponent;
    sc_signal<sc_uint<24>> mant_a, mant_b;
    sc_signal<sc_uint<25>> out_mantissa;

    // Submodules
    ieee754_extractor*   extract_a;
    ieee754_extractor*   extract_b;
    ieee754_adder_core*  adder_core;
    ieee754_normalizer*  normalizer;

    SC_CTOR(ieee754_adder) : 
        extract_a(new ieee754_extractor("extract_a")),
        extract_b(new ieee754_extractor("extract_b")),
        adder_core(new ieee754_adder_core("adder_core")),
        normalizer(new ieee754_normalizer("normalizer")) 
    {
        // Connect extractors
        extract_a->a(a);
        extract_a->sign(sign_a);
        extract_a->exponent(exp_a);
        extract_a->mantissa(mant_a);

        extract_b->a(b);
        extract_b->sign(sign_b);
        extract_b->exponent(exp_b);
        extract_b->mantissa(mant_b);

        // Connect adder core
        adder_core->exp_a(exp_a);
        adder_core->exp_b(exp_b);
        adder_core->mant_a(mant_a);
        adder_core->mant_b(mant_b);
        adder_core->sign_a(sign_a);
        adder_core->sign_b(sign_b);
        adder_core->out_sign(out_sign);
        adder_core->out_exponent(out_exponent);
        adder_core->out_mantissa(out_mantissa);

        // Connect normalizer
        normalizer->exponent(out_exponent);
        normalizer->mantissa(out_mantissa);
        normalizer->sign(out_sign);
        normalizer->result(result);
    }

    ~ieee754_adder() {
        delete extract_a;
        delete extract_b;
        delete adder_core;
        delete normalizer;
    }
};

int sc_main(int argc, char* argv[]) {
    // Create signals
    sc_signal<sc_uint<32>> a_sig, b_sig, result_sig;
    
    // Instantiate the adder
    ieee754_adder adder("float_adder");
    
    // Connect the module
    adder.a(a_sig);
    adder.b(b_sig);
    adder.result(result_sig);
    
    // Test cases
    a_sig.write(0x3f800000);  // 1.0
    b_sig.write(0x40000000);  // 2.0
    
    // Run simulation
    sc_start(1, SC_NS);
    
    // Display results
    cout << "1.0 + 2.0 = 0x" << hex << result_sig.read() << " (" 
         << *(float*)&result_sig.read() << ")" << endl;
    
    return 0;
}