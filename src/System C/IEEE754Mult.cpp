#include <systemc.h>
#include <iostream>
#include <cstring>

//==============================================================================
//
// Module: ieee754_extractor
//
SC_MODULE(ieee754_extractor) {
    sc_in<sc_uint<32>>  operand;
    sc_in<bool>         reset;
    sc_out<bool>        sign;
    sc_out<sc_uint<8>>  exponent;
    sc_out<sc_uint<24>> mantissa;

    void extract() {
        if (reset.read()) {
            sign.write(false);
            exponent.write(0);
            mantissa.write(0);
            return;
        }

        sc_uint<32> val = operand.read();
        sign.write(val[31]);
        exponent.write(val.range(30, 23));
        
        // Add implicit leading 1 unless exponent is zero (denormal number)
        mantissa.write((exponent.read() == 0) ? 
                      (0, val.range(22, 0)) : 
                      (1, val.range(22, 0)));
    }

    SC_CTOR(ieee754_extractor) {
        SC_METHOD(extract);
        sensitive << operand << reset;
    }
};

//==============================================================================
//
// Module: ieee754_multiplier_core
//
SC_MODULE(ieee754_multiplier_core) {
    sc_in<sc_uint<24>>  mant_a, mant_b;
    sc_in<sc_uint<8>>   exp_a, exp_b;
    sc_in<bool>         sign_a, sign_b;
    sc_in<bool>         reset;
    sc_out<sc_uint<48>> temp_mantissa;
    sc_out<sc_uint<8>>  temp_exponent;
    sc_out<bool>        result_sign;

    void multiply() {
        if (reset.read()) {
            temp_mantissa.write(0);
            temp_exponent.write(0);
            result_sign.write(false);
            return;
        }

        // Check for special cases
        bool a_is_nan = (exp_a.read() == 0xFF) && (mant_a.read().range(22, 0) != 0);
        bool b_is_nan = (exp_b.read() == 0xFF) && (mant_b.read().range(22, 0) != 0);
        bool a_is_inf = (exp_a.read() == 0xFF) && (mant_a.read().range(22, 0) == 0);
        bool b_is_inf = (exp_b.read() == 0xFF) && (mant_b.read().range(22, 0) == 0);

        // Handle NaN cases
        if (a_is_nan || b_is_nan) {
            temp_mantissa.write(0x400000000000ull); // Quiet NaN
            temp_exponent.write(0xFF);
            result_sign.write(false);
            return;
        }

        // Handle infinity cases
        if (a_is_inf || b_is_inf) {
            temp_mantissa.write(0);
            temp_exponent.write(0xFF);
            result_sign.write(sign_a.read() ^ sign_b.read());
            
            // Infinity * 0 = NaN
            if ((a_is_inf && (exp_b.read() == 0 && mant_b.read() == 0)) ||
                (b_is_inf && (exp_a.read() == 0 && mant_a.read() == 0))) {
                temp_mantissa.write(0x400000000000ull);
            }
            return;
        }

        // Handle zero cases
        if ((exp_a.read() == 0 && mant_a.read() == 0) || 
            (exp_b.read() == 0 && mant_b.read() == 0)) {
            temp_mantissa.write(0);
            temp_exponent.write(0);
            result_sign.write(false);
            return;
        }

        // Normal multiplication
        temp_mantissa.write(mant_a.read() * mant_b.read());
        temp_exponent.write(exp_a.read() + exp_b.read() - 127); // Remove bias
        result_sign.write(sign_a.read() ^ sign_b.read());
    }

    SC_CTOR(ieee754_multiplier_core) {
        SC_METHOD(multiply);
        sensitive << mant_a << mant_b << exp_a << exp_b << sign_a << sign_b << reset;
    }
};

//==============================================================================
//
// Module: ieee754_normalizer
//
SC_MODULE(ieee754_normalizer) {
    sc_in<sc_uint<48>>  temp_mantissa;
    sc_in<sc_uint<8>>   temp_exponent;
    sc_in<bool>         sign;
    sc_in<bool>         reset;
    sc_out<sc_uint<32>> result;

    void normalize() {
        if (reset.read()) {
            result.write(0);
            return;
        }

        sc_uint<23> final_mantissa;
        sc_uint<8> final_exponent = temp_exponent.read();

        // Check for overflow in multiplication
        if (temp_mantissa.read()[47]) { // If bit 47 is set (overflow)
            final_mantissa = temp_mantissa.read().range(46, 24);
            final_exponent++;
        } else {
            final_mantissa = temp_mantissa.read().range(45, 23);
        }

        // Check for exponent overflow
        if (final_exponent >= 0xFF) {
            result.write((sign.read(), sc_uint<8>(0xFF), sc_uint<23>(0))); // Infinity
        } 
        // Check for exponent underflow
        else if (final_exponent == 0) {
            result.write(0); // Flush to zero
        } 
        else {
            result.write((sign.read(), final_exponent, final_mantissa));
        }
    }

    SC_CTOR(ieee754_normalizer) {
        SC_METHOD(normalize);
        sensitive << temp_mantissa << temp_exponent << sign << reset;
    }
};

//==============================================================================
//
// Module: ieee754_multiplier (Top-Level)
//
SC_MODULE(ieee754_multiplier) {
    sc_in<sc_uint<32>>  operand_a;
    sc_in<sc_uint<32>>  operand_b;
    sc_in<bool>         reset;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<bool>        sign_a, sign_b, result_sign;
    sc_signal<sc_uint<8>>  exp_a, exp_b, temp_exponent;
    sc_signal<sc_uint<24>> mant_a, mant_b;
    sc_signal<sc_uint<48>> temp_mantissa;

    // Submodules
    ieee754_extractor*     extractor_a;
    ieee754_extractor*     extractor_b;
    ieee754_multiplier_core* multiplier;
    ieee754_normalizer*   normalizer;

    SC_CTOR(ieee754_multiplier) {
        // Create submodules
        extractor_a = new ieee754_extractor("extractor_a");
        extractor_a->operand(operand_a);
        extractor_a->reset(reset);
        extractor_a->sign(sign_a);
        extractor_a->exponent(exp_a);
        extractor_a->mantissa(mant_a);

        extractor_b = new ieee754_extractor("extractor_b");
        extractor_b->operand(operand_b);
        extractor_b->reset(reset);
        extractor_b->sign(sign_b);
        extractor_b->exponent(exp_b);
        extractor_b->mantissa(mant_b);

        multiplier = new ieee754_multiplier_core("multiplier");
        multiplier->mant_a(mant_a);
        multiplier->mant_b(mant_b);
        multiplier->exp_a(exp_a);
        multiplier->exp_b(exp_b);
        multiplier->sign_a(sign_a);
        multiplier->sign_b(sign_b);
        multiplier->reset(reset);
        multiplier->temp_mantissa(temp_mantissa);
        multiplier->temp_exponent(temp_exponent);
        multiplier->result_sign(result_sign);

        normalizer = new ieee754_normalizer("normalizer");
        normalizer->temp_mantissa(temp_mantissa);
        normalizer->temp_exponent(temp_exponent);
        normalizer->sign(result_sign);
        normalizer->reset(reset);
        normalizer->result(result);
    }

    ~ieee754_multiplier() {
        delete extractor_a;
        delete extractor_b;
        delete multiplier;
        delete normalizer;
    }
};

// Testbench
int sc_main(int argc, char* argv[]) {
    // Create signals
    sc_signal<sc_uint<32>> a, b, result;
    sc_signal<bool> reset;

    // Instantiate the multiplier
    ieee754_multiplier multiplier("multiplier");
    multiplier.operand_a(a);
    multiplier.operand_b(b);
    multiplier.reset(reset);
    multiplier.result(result);

    // Test case 1: 2.0 * 3.0 = 6.0
    reset.write(false);
    a.write(0x40000000); // 2.0
    b.write(0x40400000); // 3.0
    sc_start(1, SC_NS);
    
    float result_float;
    memcpy(&result_float, &result.read(), sizeof(float));
    cout << "2.0 * 3.0 = " << result_float << " (0x" << hex << result.read() << ")" << endl;

    // Test case 2: -1.5 * 4.0 = -6.0
    a.write(0xBFC00000); // -1.5
    b.write(0x40800000); // 4.0
    sc_start(1, SC_NS);
    
    memcpy(&result_float, &result.read(), sizeof(float));
    cout << "-1.5 * 4.0 = " << result_float << " (0x" << hex << result.read() << ")" << endl;

    return 0;
}