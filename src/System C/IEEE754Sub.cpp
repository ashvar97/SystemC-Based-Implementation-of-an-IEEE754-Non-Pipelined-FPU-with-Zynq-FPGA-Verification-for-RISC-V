#include <systemc.h>
#include <iostream>
#include <cstring>

//==============================================================================
//
// Module: ieee754_subtractor
//
SC_MODULE(ieee754_subtractor) {
    // Input ports
    sc_in<sc_uint<32>>  minuend;    // Renamed from 'a' for clarity
    sc_in<sc_uint<32>>  subtrahend; // Renamed from 'b' for clarity
    sc_in<bool>         enable;
    
    // Output port
    sc_out<sc_uint<32>> result;

    void subtract() {
        if (!enable.read()) {
            result.write(0);
            return;
        }

        // Extract and prepare operands
        bool min_sign = minuend.read()[31];
        bool sub_sign = !subtrahend.read()[31]; // Invert sign for subtraction
        sc_uint<32> larger, smaller;
        bool result_sign;

        // Determine larger and smaller operands (in magnitude)
        if (minuend.read().range(30,0) > subtrahend.read().range(30,0)) {
            larger = minuend.read();
            smaller = subtrahend.read();
            result_sign = min_sign;
        } else {
            larger = subtrahend.read();
            smaller = minuend.read();
            result_sign = sub_sign;
        }

        // Extract components of larger number
        sc_uint<8>  larger_exp = larger.range(30,23);
        sc_uint<23> larger_mant = larger.range(22,0);
        
        // Prepare smaller number's mantissa with implicit leading 1
        sc_uint<24> smaller_mant = (sc_uint<24>(1) << 23 | smaller.range(22,0);
        
        // Align exponents by shifting smaller mantissa
        sc_uint<8> exp_diff = larger_exp - smaller.range(30,23);
        smaller_mant >>= exp_diff;

        // Perform the mantissa operation
        sc_uint<25> mant_result;
        if (min_sign == sub_sign) { // Same signs = addition
            mant_result = (sc_uint<25>(1) << 23 | larger_mant) + smaller_mant;
        } else { // Different signs = subtraction
            mant_result = (sc_uint<25>(1) << 23 | larger_mant) - smaller_mant;
        }

        // Handle zero result
        if (mant_result == 0) {
            result.write(0);
            return;
        }

        // Normalize the result
        sc_uint<32> final_result;
        sc_uint<5> leading_zeros = 0;
        
        // Count leading zeros if needed
        if (!mant_result[24]) {
            for (int i = 23; i >= 0; --i) {
                if (mant_result[i]) break;
                leading_zeros++;
            }
        }

        // Adjust exponent and shift mantissa
        if (mant_result[24]) { // Overflow case
            larger_exp++;
            mant_result >>= 1;
        } else if (leading_zeros > 0) {
            if (larger_exp > leading_zeros) {
                larger_exp -= leading_zeros;
                mant_result <<= leading_zeros;
            } else {
                mant_result <<= (larger_exp - 1);
                larger_exp = 0;
            }
        }

        // Check for exponent overflow
        if (larger_exp >= 0xFF) {
            final_result = 0;
        } else {
            final_result.range(30,23) = larger_exp;
            final_result.range(22,0) = mant_result.range(22,0);
            final_result[31] = result_sign;
        }

        result.write(final_result);
    }

    SC_CTOR(ieee754_subtractor) {
        SC_METHOD(subtract);
        sensitive << minuend << subtrahend << enable;
    }
};

int sc_main(int argc, char* argv[]) {
    // Create signals
    sc_signal<sc_uint<32>> a, b, ans;
    sc_signal<bool> enable;

    // Instantiate the subtractor
    ieee754_subtractor subtractor("subtractor");
    subtractor.minuend(a);
    subtractor.subtrahend(b);
    subtractor.enable(enable);
    subtractor.result(ans);

    // Test case 1: 3.0 - 2.0 = 1.0
    enable.write(true);
    a.write(0x40400000); // 3.0
    b.write(0x40000000); // 2.0
    sc_start(1, SC_NS);
    
    float result;
    memcpy(&result, &ans.read(), sizeof(float));
    cout << "3.0 - 2.0 = " << result << " (0x" << hex << ans.read() << ")" << endl;

    // Test case 2: 2.0 - 3.0 = -1.0
    a.write(0x40000000); // 2.0
    b.write(0x40400000); // 3.0
    sc_start(1, SC_NS);
    
    memcpy(&result, &ans.read(), sizeof(float));
    cout << "2.0 - 3.0 = " << result << " (0x" << hex << ans.read() << ")" << endl;

    return 0;
}