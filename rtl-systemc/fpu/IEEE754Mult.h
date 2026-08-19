#include <systemc.h>
#include <iostream>
#include <cstring>



// FloatingPointExtractor Module
SC_MODULE(FloatingPointExtractor) {
    sc_in<sc_uint<32>> in;
    sc_in<bool> reset;
    sc_out<bool> sign;
    sc_out<sc_uint<8>> exponent;
    sc_out<sc_uint<24>> mantissa;

    void extract() {
        if (reset.read()) {
            sign.write(false);
            exponent.write(0);
            mantissa.write(0);
        } else {
            sign.write(in.read()[31]);
            exponent.write(in.read().range(30, 23));
            mantissa.write((sc_uint<24>(1) << 23) | in.read().range(22, 0));
        }
    }

    SC_CTOR(FloatingPointExtractor) {
        SC_METHOD(extract);
        sensitive << in << reset;
    }
};

// FloatingPointMultiplier Module
SC_MODULE(FloatingPointMultiplier) {
    sc_in<sc_uint<24>> A_Mantissa;
    sc_in<sc_uint<24>> B_Mantissa;
    sc_in<sc_uint<8>> A_Exponent;
    sc_in<sc_uint<8>> B_Exponent;
    sc_in<bool> A_sign;
    sc_in<bool> B_sign;
    sc_in<bool> reset;
    sc_out<sc_uint<48>> Temp_Mantissa;
    // Wide enough to hold the true (un-truncated) biased exponent sum minus the double bias
    // (up to 254+254-127 = 381, or as low as 0+0-127 = -127) -- the old sc_uint<8> here wrapped
    // around silently for any operand pair whose true exponent fell outside 0..255, corrupting
    // the overflow/underflow decision the normalizer stage below makes.
    sc_out<sc_int<12>> Temp_Exponent;
    sc_out<bool> Sign;

    void multiply() {
        if (reset.read()) {
            Temp_Mantissa.write(0);
            Temp_Exponent.write(0);
            Sign.write(false);
        } else {
            Temp_Mantissa.write(A_Mantissa.read() * B_Mantissa.read());
            Temp_Exponent.write(sc_int<12>(A_Exponent.read()) + sc_int<12>(B_Exponent.read()) - 127);
            Sign.write(A_sign.read() ^ B_sign.read());
        }
    }

    SC_CTOR(FloatingPointMultiplier) {
        SC_METHOD(multiply);
        sensitive << A_Mantissa << B_Mantissa << A_Exponent << B_Exponent << A_sign << B_sign << reset;
    }
};

// FloatingPointNormalizer Module
SC_MODULE(FloatingPointNormalizer) {
    sc_in<sc_uint<48>> Temp_Mantissa;
    sc_in<sc_int<12>> Temp_Exponent;
    sc_in<bool> Sign;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    void normalize() {
        if (reset.read()) {
            result.write(0);
        } else {
            sc_uint<23> Mantissa;
            sc_int<12> Exponent;

            if (Temp_Mantissa.read()[47]) {
                Mantissa = Temp_Mantissa.read().range(46, 24);
                Exponent = Temp_Exponent.read() + 1;
            } else {
                Mantissa = Temp_Mantissa.read().range(45, 23);
                Exponent = Temp_Exponent.read();
            }

            if (Exponent >= 255) {
                // Overflow: true result magnitude exceeds what a float can represent.
                result.write((sc_uint<32>)((sc_uint<1>(Sign.read()), sc_uint<8>(0xFF), sc_uint<23>(0))));
            } else if (Exponent <= 0) {
                // Underflow: flush to zero (no subnormal support here, matching the rest of
                // this FPU's add/sub/div paths).
                result.write((sc_uint<32>)((sc_uint<1>(Sign.read()), sc_uint<8>(0), sc_uint<23>(0))));
            } else {
                result.write((sc_uint<32>)((sc_uint<1>(Sign.read()), sc_uint<8>(Exponent), Mantissa)));
            }
        }
    }

    SC_CTOR(FloatingPointNormalizer) {
        SC_METHOD(normalize);
        sensitive << Temp_Mantissa << Temp_Exponent << Sign << reset;
    }
};



SC_MODULE(ieee754mult) {
    sc_in<sc_uint<32>> A;
    sc_in<sc_uint<32>> B;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<bool> A_sign, B_sign, Sign;
    sc_signal<sc_uint<8>> A_Exponent, B_Exponent;
    sc_signal<sc_int<12>> Temp_Exponent;
    sc_signal<sc_uint<24>> A_Mantissa, B_Mantissa;
    sc_signal<sc_uint<48>> Temp_Mantissa;
    sc_signal<sc_uint<32>> core_result;

    // Submodule instances
    FloatingPointExtractor extractA;
    FloatingPointExtractor extractB;
    FloatingPointMultiplier multiply;
    FloatingPointNormalizer normalize;

    void special_case_override() {
        sc_uint<32> a = A.read(), b = B.read();
        bool a_exp_ff = (a.range(30, 23) == 0xFF);
        bool b_exp_ff = (b.range(30, 23) == 0xFF);
        bool a_is_nan = a_exp_ff && (a.range(22, 0) != 0);
        bool b_is_nan = b_exp_ff && (b.range(22, 0) != 0);
        bool a_is_inf = a_exp_ff && (a.range(22, 0) == 0);
        bool b_is_inf = b_exp_ff && (b.range(22, 0) == 0);
        bool a_is_zero = (a.range(30, 0) == 0);
        bool b_is_zero = (b.range(30, 0) == 0);
        bool result_sign = a[31] ^ b[31];

        if (reset.read()) {
            result.write(0);
        } else if (a_is_nan || b_is_nan || (a_is_inf && b_is_zero) || (b_is_inf && a_is_zero)) {
            // NaN propagates; 0 * infinity is undefined -> NaN.
            result.write((sc_uint<32>)((sc_uint<1>(0), sc_uint<8>(0xFF), sc_uint<23>(0x400000))));
        } else if (a_is_inf || b_is_inf) {
            result.write((sc_uint<32>)((sc_uint<1>(result_sign), sc_uint<8>(0xFF), sc_uint<23>(0))));
        } else if (a_is_zero || b_is_zero) {
            result.write((sc_uint<32>)((sc_uint<1>(result_sign), sc_uint<8>(0), sc_uint<23>(0))));
        } else {
            result.write(core_result.read());
        }
    }

    SC_CTOR(ieee754mult)
        : extractA("extractA"), extractB("extractB"), multiply("multiply"), normalize("normalize") {
        // Connect extractA
        extractA.in(A);
        extractA.reset(reset);
        extractA.sign(A_sign);
        extractA.exponent(A_Exponent);
        extractA.mantissa(A_Mantissa);

        // Connect extractB
        extractB.in(B);
        extractB.reset(reset);
        extractB.sign(B_sign);
        extractB.exponent(B_Exponent);
        extractB.mantissa(B_Mantissa);

        // Connect multiply
        multiply.A_Mantissa(A_Mantissa);
        multiply.B_Mantissa(B_Mantissa);
        multiply.A_Exponent(A_Exponent);
        multiply.B_Exponent(B_Exponent);
        multiply.A_sign(A_sign);
        multiply.B_sign(B_sign);
        multiply.reset(reset);
        multiply.Temp_Mantissa(Temp_Mantissa);
        multiply.Temp_Exponent(Temp_Exponent);
        multiply.Sign(Sign);

        // Connect normalize
        normalize.Temp_Mantissa(Temp_Mantissa);
        normalize.Temp_Exponent(Temp_Exponent);
        normalize.Sign(Sign);
        normalize.reset(reset);
        normalize.result(core_result);

        SC_METHOD(special_case_override);
        sensitive << A << B << reset << core_result;
    }
};
