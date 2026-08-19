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

// ComputeModule: Performs floating-point division (combinatorial version)
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
            // Wide enough to hold the true (un-truncated) exponent difference plus bias --
            // a_exp/b_exp each range 0..255, so a_exp - b_exp + 127 can range from -128 to 382,
            // well outside sc_uint<8>. The old sc_uint<8> here wrapped around silently for any
            // operand pair with a large exponent gap, corrupting the overflow/underflow checks
            // below (which then compared the *already-wrapped* value against 254/1).
            sc_int<12> result_exp;
            sc_uint<5> i;
            bool odd, rnd, sticky;
            sc_uint<32> x_val, y_val;
            sc_uint<8> shift;
            bool result_sign;

            // Compute sign of the result
            result_sign = a_sign.read() ^ b_sign.read();

            // Compute exponent of result
            result_exp = sc_int<12>(a_exp.read()) - sc_int<12>(b_exp.read()) + 127;

            // Normalize dividend if smaller than divisor
            x_val = a_significand.read();
            y_val = b_significand.read();

            if (x_val < y_val) {
                x_val = x_val << 1;
                result_exp = result_exp - 1;
            }

            // Perform division (restoring algorithm) - COMBINATORIAL VERSION
            r = 0;
            for (i = 0; i < 25; i++) {
                r = r << 1;
                if (x_val >= y_val) {
                    x_val = x_val - y_val;
                    r = r | 1;
                }
                x_val = x_val << 1;
                // NO wait() in SC_METHOD - all iterations happen in one clock cycle
            }

            sticky = (x_val != 0);

            // Handle normal/overflow/underflow cases
            if ((result_exp >= 1) && (result_exp <= 254)) { // Normal case
                rnd = (r & 0x1000000) >> 24;
                odd = (r & 0x2) != 0;
                r = (r >> 1) + (rnd & (sticky | odd));
                r = (sc_uint<32>(result_exp) << 23) + (r - 0x00800000);
            }
            else if (result_exp > 254) { // Overflow to infinity
                r = 0x7F800000;
            }
            else { // Underflow (flush to zero -- no subnormal support here)
                r = 0;
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

SC_MODULE(ieee754_div) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<sc_uint<32>> a_significand, b_significand;
    sc_signal<bool> a_sign, b_sign;
    sc_signal<sc_uint<8>> a_exp, b_exp;
    sc_signal<sc_uint<32>> core_result;

    // Submodules (removed NormalizationModule)
    ExtractModule extract_module;
    ComputeModule compute_module;

    void special_case_override() {
        sc_uint<32> av = a.read(), bv = b.read();
        bool a_exp_ff = (av.range(30, 23) == 0xFF);
        bool b_exp_ff = (bv.range(30, 23) == 0xFF);
        bool a_is_nan = a_exp_ff && (av.range(22, 0) != 0);
        bool b_is_nan = b_exp_ff && (bv.range(22, 0) != 0);
        bool a_is_inf = a_exp_ff && (av.range(22, 0) == 0);
        bool b_is_inf = b_exp_ff && (bv.range(22, 0) == 0);
        bool a_is_zero = (av.range(30, 0) == 0);
        bool b_is_zero = (bv.range(30, 0) == 0);
        bool result_sign = av[31] ^ bv[31];

        if (reset.read()) {
            result.write(0);
        } else if (a_is_nan || b_is_nan || (a_is_zero && b_is_zero) || (a_is_inf && b_is_inf)) {
            // NaN propagates; 0/0 and inf/inf are both undefined -> NaN.
            result.write((sc_uint<32>)((sc_uint<1>(0), sc_uint<8>(0xFF), sc_uint<23>(0x400000))));
        } else if (a_is_inf || b_is_zero) {
            // finite/0 or inf/finite (inf/inf already handled above) -> infinity.
            result.write((sc_uint<32>)((sc_uint<1>(result_sign), sc_uint<8>(0xFF), sc_uint<23>(0))));
        } else if (a_is_zero || b_is_inf) {
            // 0/finite or finite/inf -> zero.
            result.write((sc_uint<32>)((sc_uint<1>(result_sign), sc_uint<8>(0), sc_uint<23>(0))));
        } else {
            result.write(core_result.read());
        }
    }

    SC_CTOR(ieee754_div) :
        extract_module("extract_module"),
        compute_module("compute_module")
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
        compute_module.result(core_result);

        SC_METHOD(special_case_override);
        sensitive << a << b << reset << core_result;
    }
};
