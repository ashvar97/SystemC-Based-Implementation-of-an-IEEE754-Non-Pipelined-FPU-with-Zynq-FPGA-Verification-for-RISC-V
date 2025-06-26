#include <systemc.h>
#include <iostream>
#include <cstring>



// ExtractModule: Extracts sign, exponent, and significand from floating-point inputs
SC_MODULE(ExtractModule) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> a_significand, b_significand;
    sc_out<bool> a_sign, b_sign;
    sc_out<sc_uint<8>> a_exp, b_exp;

    void extract() {
        if (reset.read()) {
            a_significand.write(0);
            b_significand.write(0);
            a_sign.write(false);
            b_sign.write(false);
            a_exp.write(0);
            b_exp.write(0);
        } else {
            // Extract biased exponents and sign bits
            a_exp.write((a.read() & 0x7F800000) >> 23);
            b_exp.write((b.read() & 0x7F800000) >> 23);
            a_sign.write((a.read() & 0x80000000) != 0);
            b_sign.write((b.read() & 0x80000000) != 0);

            // Extract significands (with implicit leading 1)
            a_significand.write((a.read() & 0x007FFFFF) | 0x00800000);
            b_significand.write((b.read() & 0x007FFFFF) | 0x00800000);
        }
    }

    SC_CTOR(ExtractModule) {
        SC_METHOD(extract);
        sensitive << a << b << reset;
    }
};

// ComputeModule: Performs floating-point division
SC_MODULE(ComputeModule) {
    sc_in<sc_uint<32>> a_significand, b_significand;
    sc_in<bool> a_sign, b_sign;
    sc_in<sc_uint<8>> a_exp, b_exp;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    void compute() {
        if (reset.read()) {
            result.write(0);
        } else {
            sc_uint<32> r;
            sc_uint<8> result_exp;
            sc_uint<5> i;
            bool odd, rnd, sticky;
            sc_uint<32> x_val, y_val;
            sc_uint<8> shift;
            bool result_sign;

            // Compute sign of the result
            result_sign = a_sign.read() ^ b_sign.read();

            // Compute exponent of result
            result_exp = a_exp.read() - b_exp.read() + 127;

            // Normalize dividend if smaller than divisor
            x_val = a_significand.read();
            y_val = b_significand.read();

            if (x_val < y_val) {
                x_val = x_val << 1;
                result_exp = result_exp - 1;
            }

            // Perform division (restoring algorithm)
            r = 0;
            for (i = 0; i < 25; i++) {
                r = r << 1;
                if (x_val >= y_val) {
                    x_val = x_val - y_val;
                    r = r | 1;
                }
                x_val = x_val << 1;
            }

            sticky = (x_val != 0);
            
            // Handle normal/overflow/underflow cases
            if ((result_exp >= 1) && (result_exp <= 254)) { // Normal case
                rnd = (r & 0x1000000) >> 24;
                odd = (r & 0x2) != 0;
                r = (r >> 1) + (rnd & (sticky | odd));
                r = (result_exp << 23) + (r - 0x00800000);
            } 
            else if (result_exp > 254) { // Overflow to infinity
                r = 0x7F800000;
            } 
            else { // Underflow (zero or subnormal)
                shift = 1 - result_exp;
                if (shift > 25) shift = 25;
                sticky = sticky | ((r & ~(~0 << shift)) != 0);
                r = r >> shift;
                rnd = (r & 0x1000000) >> 24;
                odd = (r & 0x2) != 0;
                r = (r >> 1) + (rnd & (sticky | odd));
            }

            // Combine sign bit
            r = r | (result_sign ? 0x80000000 : 0);
            result.write(r);
        }
    }

    SC_CTOR(ComputeModule) {
        SC_METHOD(compute);
        sensitive << a_significand << b_significand << a_sign << b_sign << a_exp << b_exp << reset;
    }
};

// NormalizationModule: Checks if result is normalized (kept internally)
SC_MODULE(NormalizationModule) {
    sc_in<sc_uint<32>> result;
    sc_in<sc_uint<8>> a_exp;
    sc_in<bool> reset;

    void normalize() {
        if (reset.read()) {
        } else {
            // Check exponent range for normalization
            sc_uint<8> exp = (result.read() & 0x7F800000) >> 23;
        }
    }

    SC_CTOR(NormalizationModule) {
        SC_METHOD(normalize);
        sensitive << result << a_exp << reset;
    }
};

SC_MODULE(ieee754_div) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<sc_uint<32>> a_significand, b_significand;
    sc_signal<bool> a_sign, b_sign;
    sc_signal<sc_uint<8>> a_exp, b_exp;
    sc_signal<bool> normalized_int;  // Internal signal (unused externally)

    // Submodules
    ExtractModule extract_module;
    ComputeModule compute_module;
    NormalizationModule normalization_module;

    SC_CTOR(ieee754_div) : 
        extract_module("extract_module"),
        compute_module("compute_module"),
        normalization_module("normalization_module") 
    {
        // Connect ExtractModule
        extract_module.a(a);
        extract_module.b(b);
        extract_module.reset(reset);
        extract_module.a_significand(a_significand);
        extract_module.b_significand(b_significand);
        extract_module.a_sign(a_sign);
        extract_module.b_sign(b_sign);
        extract_module.a_exp(a_exp);
        extract_module.b_exp(b_exp);

        // Connect ComputeModule
        compute_module.a_significand(a_significand);
        compute_module.b_significand(b_significand);
        compute_module.a_sign(a_sign);
        compute_module.b_sign(b_sign);
        compute_module.a_exp(a_exp);
        compute_module.b_exp(b_exp);
        compute_module.reset(reset);
        compute_module.result(result);

        // Connect NormalizationModule (internally only)
        normalization_module.result(result);
        normalization_module.a_exp(a_exp);
        normalization_module.reset(reset);
    }
};


