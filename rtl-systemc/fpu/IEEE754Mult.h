
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
    sc_out<sc_uint<8>> Temp_Exponent;
    sc_out<bool> Sign;

    void multiply() {
        if (reset.read()) {
            Temp_Mantissa.write(0);
            Temp_Exponent.write(0);
            Sign.write(false);
        } else {
            Temp_Mantissa.write(A_Mantissa.read() * B_Mantissa.read());
            Temp_Exponent.write(A_Exponent.read() + B_Exponent.read() - 127);
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
    sc_in<sc_uint<8>> Temp_Exponent;
    sc_in<bool> Sign;
    sc_in<bool> reset;
    sc_out<sc_uint<32>> result;

    void normalize() {
        if (reset.read()) {
            result.write(0);
        } else {
            sc_uint<23> Mantissa;
            sc_uint<8> Exponent;

            if (Temp_Mantissa.read()[47]) {
                Mantissa = Temp_Mantissa.read().range(46, 24);
                Exponent = Temp_Exponent.read() + 1;
            } else {
                Mantissa = Temp_Mantissa.read().range(45, 23);
                Exponent = Temp_Exponent.read();
            }

            result.write((Sign.read(), Exponent, Mantissa));
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
    sc_signal<sc_uint<8>> A_Exponent, B_Exponent, Temp_Exponent;
    sc_signal<sc_uint<24>> A_Mantissa, B_Mantissa;
    sc_signal<sc_uint<48>> Temp_Mantissa;

    // Submodule instances
    FloatingPointExtractor extractA;
    FloatingPointExtractor extractB;
    FloatingPointMultiplier multiply;
    FloatingPointNormalizer normalize;

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
        normalize.result(result);
    }
};



