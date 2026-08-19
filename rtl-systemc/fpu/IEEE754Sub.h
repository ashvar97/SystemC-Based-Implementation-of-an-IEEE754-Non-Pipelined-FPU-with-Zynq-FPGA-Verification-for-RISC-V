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
            sig_b = !b.read()[31]; // Invert sign of b for subtraction (a - b == a + (-b))

            // NaN/Infinity special cases: the bit-manipulation path below assumes both operands
            // are finite, so NaN/Inf have to be caught here first, treating b as already
            // sign-flipped (sig_b above), i.e. exactly as if this were an addition of a and -b.
            bool a_exp_ff = (a.read().range(30, 23) == 0xFF);
            bool b_exp_ff = (b.read().range(30, 23) == 0xFF);
            bool a_is_nan = a_exp_ff && (a.read().range(22, 0) != 0);
            bool b_is_nan = b_exp_ff && (b.read().range(22, 0) != 0);
            bool a_is_inf = a_exp_ff && (a.read().range(22, 0) == 0);
            bool b_is_inf = b_exp_ff && (b.read().range(22, 0) == 0);

            if (a_is_nan || b_is_nan) {
                ans.write((sc_uint<32>)((sc_uint<1>(0), sc_uint<8>(0xFF), sc_uint<23>(0x400000))));
                return;
            }
            if (a_is_inf || b_is_inf) {
                if (a_is_inf && b_is_inf) {
                    // sig_a/sig_b are the *effective* signs being added (a, and -b); this is
                    // exactly an addition of two infinities, so it follows the same rule as
                    // ieee754_adder_core: same effective sign -> that infinity, opposite -> NaN
                    // (e.g. (+inf) - (+inf) == (+inf) + (-inf), which is undefined).
                    if (sig_a == sig_b) {
                        ans.write((sc_uint<32>)((sc_uint<1>(sig_a), sc_uint<8>(0xFF), sc_uint<23>(0))));
                    } else {
                        ans.write((sc_uint<32>)((sc_uint<1>(0), sc_uint<8>(0xFF), sc_uint<23>(0x400000))));
                    }
                } else {
                    bool result_is_a = a_is_inf;
                    ans.write((sc_uint<32>)((sc_uint<1>(result_is_a ? sig_a : sig_b), sc_uint<8>(0xFF), sc_uint<23>(0))));
                }
                return;
            }

            bool a_is_zero = (a.read().range(30, 0) == 0);
            bool b_is_zero = (b.read().range(30, 0) == 0);
            if (a_is_zero || b_is_zero) {
                // The bit-manipulation path below always assumes an implicit leading-1
                // mantissa bit, which is wrong for an exactly-zero operand -- has to be
                // special-cased rather than left to fall through.
                if (a_is_zero && b_is_zero) {
                    // Round-to-nearest sign-of-zero rule: -0 only when a=-0 and b=+0 (i.e. the
                    // two *effective* addition operands, a and -b, are both negative).
                    ans.write((sc_uint<32>)((sc_uint<1>(sig_a && sig_b), sc_uint<8>(0), sc_uint<23>(0))));
                } else if (a_is_zero) {
                    ans.write((sc_uint<32>)((sc_uint<1>(sig_b), b.read().range(30, 0))));
                } else {
                    ans.write(a.read());
                }
                return;
            }

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
                    sc_uint<8> new_exp = val_b.range(30,23) + 1;
                    if (new_exp >= 0xFF) {
                        // Overflow: magnitude exceeds what a float can represent -> infinity.
                        result.range(30,23) = 0xFF;
                        result.range(22,0) = 0;
                    } else {
                        result.range(30,23) = new_exp;
                        result.range(22,0) = sum.range(23,1);
                    }
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

