#include <systemc.h>

//==============================================================================
//
// Module: ieee754_extractor
//
SC_MODULE(ieee754_extractor) {
    sc_in<sc_uint<32>>  dividend;
    sc_in<sc_uint<32>>  divisor;
    sc_in<bool>         reset;
    sc_out<sc_uint<32>> dividend_sig;
    sc_out<sc_uint<32>> divisor_sig;
    sc_out<bool>        dividend_sign;
    sc_out<bool>        divisor_sign;
    sc_out<sc_uint<8>>  dividend_exp;
    sc_out<sc_uint<8>>  divisor_exp;

    void extract() {
        if (reset.read()) {
            dividend_sig.write(0);
            divisor_sig.write(0);
            dividend_sign.write(false);
            divisor_sign.write(false);
            dividend_exp.write(0);
            divisor_exp.write(0);
        } else {
            // Extract exponents (8 bits)
            dividend_exp.write((dividend.read() & 0x7F800000) >> 23);
            divisor_exp.write((divisor.read() & 0x7F800000) >> 23);
            
            // Extract signs
            dividend_sign.write(dividend.read()[31]);
            divisor_sign.write(divisor.read()[31]);
            
            // Extract and normalize significands (add implicit leading 1)
            dividend_sig.write((dividend.read() & 0x007FFFFF) | 0x00800000);
            divisor_sig.write((divisor.read() & 0x007FFFFF) | 0x00800000);
        }
    }

    SC_CTOR(ieee754_extractor) {
        SC_METHOD(extract);
        sensitive << dividend << divisor << reset;
    }
};

//==============================================================================
//
// Module: ieee754_divider_core
//
SC_MODULE(ieee754_divider_core) {
    sc_in<sc_uint<32>>  dividend_sig;
    sc_in<sc_uint<32>>  divisor_sig;
    sc_in<bool>         dividend_sign;
    sc_in<bool>         divisor_sign;
    sc_in<sc_uint<8>>   dividend_exp;
    sc_in<sc_uint<8>>   divisor_exp;
    sc_in<bool>         reset;
    sc_out<sc_uint<32>> result;

    void divide() {
        if (reset.read()) {
            result.write(0);
            return;
        }

        sc_uint<32> quotient = 0;
        sc_uint<8> result_exp;
        sc_uint<5> i;
        bool odd, rnd, sticky;
        sc_uint<32> x_val, y_val;
        sc_uint<8> shift;
        bool result_sign;

        // Determine result sign (XOR of input signs)
        result_sign = dividend_sign.read() ^ divisor_sign.read();
        
        // Calculate result exponent (dividend_exp - divisor_exp + bias)
        result_exp = dividend_exp.read() - divisor_exp.read() + 127;
        
        // Initialize values for division
        x_val = dividend_sig.read();
        y_val = divisor_sig.read();
        
        // Normalize dividend if smaller than divisor
        if (x_val < y_val) {
            x_val = x_val << 1;
            result_exp = result_exp - 1;
        }
        
        // Perform division (restoring algorithm)
        for (i = 0; i < 25; i++) {
            quotient = quotient << 1;
            if (x_val >= y_val) {
                x_val = x_val - y_val;
                quotient = quotient | 1;
            }
            x_val = x_val << 1;
        }
        
        // Determine sticky bit (remainder != 0)
        sticky = x_val != 0;
        
        // Handle normal numbers
        if ((result_exp >= 1) && (result_exp <= 254)) {
            rnd = (quotient & 0x01000000) >> 24;  // Round bit
            odd = (quotient & 0x00000002) != 0;   // Least significant bit
            quotient = (quotient >> 1) + (rnd & (sticky | odd));
            quotient = (result_exp << 23) + (quotient - 0x00800000);
        } 
        // Handle overflow/underflow
        else {
            if (result_exp > 254) {  // Overflow
                quotient = 0x7F800000;  // Infinity
            } else {                // Underflow
                shift = 1 - result_exp;
                if (shift > 25) shift = 25;
                
                // Calculate sticky bit for denormal numbers
                sticky = sticky | ((quotient & ~(~0 << shift)) != 0;
                quotient = quotient >> shift;
                
                // Rounding
                rnd = (quotient & 0x01000000) >> 24;
                odd = (quotient & 0x00000002) != 0;
                quotient = (quotient >> 1) + (rnd & (sticky | odd));
            }
        }
        
        // Apply sign bit
        result.write(quotient | (result_sign ? 0x80000000 : 0));
    }

    SC_CTOR(ieee754_divider_core) {
        SC_METHOD(divide);
        sensitive << dividend_sig << divisor_sig << dividend_sign 
                 << divisor_sign << dividend_exp << divisor_exp << reset;
    }
};

//==============================================================================
//
// Module: ieee754_divider
//
SC_MODULE(ieee754_divider) {
    sc_in<sc_uint<32>>  dividend;
    sc_in<sc_uint<32>>  divisor;
    sc_in<bool>         reset;
    sc_out<sc_uint<32>> result;

    // Internal signals
    sc_signal<sc_uint<32>> dividend_sig, divisor_sig;
    sc_signal<bool> dividend_sign, divisor_sign;
    sc_signal<sc_uint<8>> dividend_exp, divisor_exp;

    // Submodules
    ieee754_extractor* extractor;
    ieee754_divider_core* divider;

    SC_CTOR(ieee754_divider) : 
        extractor(new ieee754_extractor("extractor")),
        divider(new ieee754_divider_core("divider")) 
    {
        // Connect extractor
        extractor->dividend(dividend);
        extractor->divisor(divisor);
        extractor->reset(reset);
        extractor->dividend_sig(dividend_sig);
        extractor->divisor_sig(divisor_sig);
        extractor->dividend_sign(dividend_sign);
        extractor->divisor_sign(divisor_sign);
        extractor->dividend_exp(dividend_exp);
        extractor->divisor_exp(divisor_exp);

        // Connect divider core
        divider->dividend_sig(dividend_sig);
        divider->divisor_sig(divisor_sig);
        divider->dividend_sign(dividend_sign);
        divider->divisor_sign(divisor_sign);
        divider->dividend_exp(dividend_exp);
        divider->divisor_exp(divisor_exp);
        divider->reset(reset);
        divider->result(result);
    }

    ~ieee754_divider() {
        delete extractor;
        delete divider;
    }
};

// Testbench
int sc_main(int argc, char* argv[]) {
    sc_signal<sc_uint<32>> dividend, divisor, result;
    sc_signal<bool> reset;

    ieee754_divider divider("divider");
    divider.dividend(dividend);
    divider.divisor(divisor);
    divider.reset(reset);
    divider.result(result);

    // Test cases
    reset.write(false);
    
    // Test 1: 4.0 / 2.0 = 2.0
    dividend.write(0x40800000);  // 4.0
    divisor.write(0x40000000);   // 2.0
    sc_start(1, SC_NS);
    cout << "4.0 / 2.0 = 0x" << hex << result.read() 
         << " (" << *(float*)&result.read() << ")" << endl;

    // Test 2: 1.0 / 4.0 = 0.25
    dividend.write(0x3F800000);  // 1.0
    divisor.write(0x40800000);   // 4.0
    sc_start(1, SC_NS);
    cout << "1.0 / 4.0 = 0x" << hex << result.read() 
         << " (" << *(float*)&result.read() << ")" << endl;

    return 0;
}