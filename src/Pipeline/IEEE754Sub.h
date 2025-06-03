#include <systemc.h>

SC_MODULE(ieee754_subtractor) {
    // Input ports
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> enable;
    
    // Output port
    sc_out<sc_uint<32>> ans;

    void compute() {
        if (enable.read()) {
            sc_uint<32> val_b, val_s, result;
            sc_uint<24> aligned;
            sc_uint<25> sum, sum_norm;
            sc_uint<5> lead0 = 0;
            bool sig_a, sig_b, result_sign;
            
            sig_a = a.read()[31]; // Sign of a
            sig_b = !b.read()[31]; // Invert sign of b for subtraction
            
            // Sorting: Determine the larger operand
            if (a.read().range(30,0) > b.read().range(30,0)) {
                val_b = a.read();
                val_s = b.read();
                result_sign = sig_a;
            } else {
                val_b = b.read();
                val_s = a.read();
                result_sign = sig_b;
            }
            
            // Align the smaller number
            aligned = (sc_uint<24>(1) << 23) | val_s.range(22,0);
            aligned >>= (val_b.range(30,23) - val_s.range(30,23));
            
            // Perform subtraction or addition based on sign
            if (sig_a == sig_b) {
                sum = (sc_uint<25>(1) << 23) | val_b.range(22,0);
                sum += aligned;
            } else {
                sum = (sc_uint<25>(1) << 23) | val_b.range(22,0);
                sum -= aligned;
            }
            
            // Normalize result
            if (sum == 0) {
                result = 0;
            } else {
                for (int i = 23; i >= 0; --i) {
                    if (sum[i]) {
                        lead0 = 23 - i;
                        break;
                    }
                }
                sum_norm = sum << lead0;
                
                // Set the result
                if (sum[24]) {
                    result.range(30,23) = val_b.range(30,23) + 1;
                    result.range(22,0) = sum.range(23,1);
                } else {
                    if (lead0 > val_b.range(30,23)) {
                        result = 0;
                    } else {
                        result.range(30,23) = val_b.range(30,23) - lead0;
                        result.range(22,0) = sum_norm.range(22,0);
                    }
                }
                result[31] = result_sign;
            }
            ans.write(result);
        } else {
            ans.write(0);
        }
    }

    SC_CTOR(ieee754_subtractor) {
        SC_METHOD(compute);
        sensitive << a << b << enable;
    }
};

