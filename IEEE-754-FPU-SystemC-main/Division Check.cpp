#include <systemc.h>
#include <bitset>

SC_MODULE(ExtractModule) {
    sc_in<sc_uint<32>> a;
    sc_in<sc_uint<32>> b;
    sc_out<sc_uint<32>> a_significand;
    sc_out<sc_uint<32>> b_significand;
    sc_out<bool> a_sign;
    sc_out<bool> b_sign;
    sc_out<sc_uint<8>> a_exp; // Change to 8 bits for exponent
    sc_out<sc_uint<8>> b_exp; // Change to 8 bits for exponent
    sc_in_clk clock; // Clock input

    void extract() {
        while (true) {
            wait(); // Wait for the rising edge of the clock

            uint32_t a_val = a.read();
            uint32_t b_val = b.read();

            // Extract biased exponents and sign bits
            a_exp.write((a_val & 0x7F800000) >> 23);
            b_exp.write((b_val & 0x7F800000) >> 23);
            a_sign.write((a_val & 0x80000000) != 0);
            b_sign.write((b_val & 0x80000000) != 0);

            // Extract significands
            a_significand.write((a_val & 0x007FFFFF) | 0x00800000);
            b_significand.write((b_val & 0x007FFFFF) | 0x00800000);
        }
    }

    SC_CTOR(ExtractModule) {
        SC_THREAD(extract);
        sensitive << clock.pos();
    }
};

SC_MODULE(ComputeModule) {
    sc_in<sc_uint<32>> a_significand;
    sc_in<sc_uint<32>> b_significand;
    sc_in<bool> a_sign;
    sc_in<bool> b_sign;
    sc_in<sc_uint<8>> a_exp; // Change to 8 bits for exponent
    sc_in<sc_uint<8>> b_exp; // Change to 8 bits for exponent
    sc_out<sc_uint<32>> result;
    sc_in_clk clock; // Clock input

    void compute() { int check=0,check1=0;
        while (true) {
        

            wait(); // Wait for the rising edge of the clock

            uint32_t r, result_exp;
            uint8_t i, odd, rnd, sticky;

            // Compute exponent of result
            result_exp = a_exp.read() - b_exp.read() + 127;

            // Dividend may not be smaller than divisor: normalize
            sc_uint<32> x_val = a_significand.read();
            sc_uint<32> y_val = b_significand.read();

            if (x_val < y_val) {
                x_val = x_val << 1;
                result_exp--;
            }

            // Generate quotient one bit at a time
            r = 0;
            for (i = 0; i < 25; i++) {
                r = r << 1;
                if (x_val >= y_val) {
                    x_val = x_val - y_val;
                    r = r | 1;
                }
                x_val = x_val << 1;
            }
            if(check==3)
{          std::bitset<25> binary_representation(r);
         std::cout << "Binary representation before rounding:" << binary_representation<<endl;
}
check++;
            sticky = (x_val != 0);
            if ((result_exp >= 1) && (result_exp <= 254)) { // normal, may overflow to infinity
                // Extract round and lsb bits
                rnd = (r & 0x1000000) >> 24;
                odd = (r & 0x2) != 0;

                // Remove round bit from quotient and round to-nearest-even
                r = (r >> 1) + (rnd & (sticky | odd));

                // Combine exponent and significand
                r = (result_exp << 23) + (r - 0x00800000);
            } else if (result_exp > 254) { // overflow: infinity
                r = 0x7F800000;
            } else { // underflow: result is zero, subnormal, or smallest normal
                uint8_t shift = (uint8_t)(1 - result_exp);

                // Clamp shift count
                if (shift > 25) shift = 25;

                // OR shifted-off bits of significand into sticky bit
                sticky = sticky | ((r & ~(~0 << shift)) != 0);

                // Denormalize significand
                r = r >> shift;

                // Extract round and lsb bits
                rnd = (r & 0x1000000) >> 24;
                odd = (r & 0x2) != 0;

                // Remove round bit from quotient and round to-nearest-even
                r = (r >> 1) + (rnd & (sticky | odd));
            }
          uint32_t temp;
          temp=r;

            // Print the binary representation
            if(check1==3){
              std::bitset<23> binary_representation2(temp);
             std::cout << "Binary representation after rounding:" << binary_representation2<<endl;    }
             check1++;
            // Combine sign bit with combo of exponent and significand
            r = r | (a_sign.read() ? 0x80000000 : 0);
            result.write(r);
        }
    }

    SC_CTOR(ComputeModule) {
        SC_THREAD(compute);
        sensitive << clock.pos();
    }
};

SC_MODULE(NormalizationModule) {
    sc_in<sc_uint<32>> result;
    sc_in<sc_uint<8>> a_exp; // Change to 8 bits for exponent
    sc_out<bool> normalized;
    sc_in_clk clock; // Clock input

    void normalize() {
        while (true) {
            wait(); // Wait for the rising edge of the clock

            uint32_t result_val = result.read();
            uint8_t a_exp_val = a_exp.read();

            // Perform normalization check
            if ((result_val & 0x7F800000) == 0x7F800000) {
                // Exponent is all 1s, indicating infinity or NaN
                normalized.write(false);
            } else if ((result_val & 0x7F800000) == 0) {
                // Exponent is all 0s, indicating a subnormal or zero
                normalized.write(false);
            } else {
                // Normalized result
                normalized.write(true);
            }
        }
    }

    SC_CTOR(NormalizationModule) {
        SC_THREAD(normalize);
        sensitive << clock.pos();
    }
};

int sc_main(int argc, char* argv[]) {
    // Instantiate modules
    sc_clock clock("clock", 10, SC_NS); // Creating a 10ns period clock
    sc_signal<sc_uint<32>> a_signal, b_signal, a_significand_signal, b_significand_signal, result_signal;
    sc_signal<bool> a_sign_signal, b_sign_signal, normalized_signal;
    sc_signal<sc_uint<8>> a_exp_signal, b_exp_signal; // Change to 8 bits for exponent

    ExtractModule extract_module("ExtractModule");
    extract_module.a(a_signal);
    extract_module.b(b_signal);
    extract_module.a_significand(a_significand_signal);
    extract_module.b_significand(b_significand_signal);
    extract_module.a_sign(a_sign_signal);
    extract_module.b_sign(b_sign_signal);
    extract_module.a_exp(a_exp_signal);
    extract_module.b_exp(b_exp_signal);
    extract_module.clock(clock);

    ComputeModule compute_module("ComputeModule");
    compute_module.a_significand(a_significand_signal);
    compute_module.b_significand(b_significand_signal);
    compute_module.a_sign(a_sign_signal);
    compute_module.b_sign(b_sign_signal);
    compute_module.a_exp(a_exp_signal);
    compute_module.b_exp(b_exp_signal);
    compute_module.result(result_signal);
    compute_module.clock(clock);

    NormalizationModule normalization_module("NormalizationModule");
    normalization_module.result(result_signal);
    normalization_module.a_exp(a_exp_signal);
    normalization_module.normalized(normalized_signal);
    normalization_module.clock(clock);

    // Get user inputs
    float a_float, b_float;
    cout << "Enter the value for a: ";
    cin >> a_float;
    cout << "Enter the value for b: ";
    cin >> b_float;

    // Convert float values to their binary representation
    unsigned int a_binary, b_binary;
    memcpy(&a_binary, &a_float, sizeof(a_binary));
    memcpy(&b_binary, &b_float, sizeof(b_binary));

    // Set input values
    a_signal.write(a_binary);
    b_signal.write(b_binary);

    // Run the simulation
    sc_start(100, SC_NS); // Run for 100ns
    unsigned int result = result_signal.read();
    float result_float;
    memcpy(&result_float, &result, sizeof(result_float));
    cout << "Result: " << result_float << endl;

    return 0;
}
