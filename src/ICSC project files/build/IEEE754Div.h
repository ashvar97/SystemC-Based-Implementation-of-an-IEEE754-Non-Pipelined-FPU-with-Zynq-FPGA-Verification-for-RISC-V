#include <systemc.h>

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
            a_exp.write((a.read() & 0x7F800000) >> 23);
            b_exp.write((b.read() & 0x7F800000) >> 23);
            a_sign.write((a.read() & 0x80000000) != 0);
            b_sign.write((b.read() & 0x80000000) != 0);
            a_significand.write((a.read() & 0x007FFFFF) | 0x00800000);
            b_significand.write((b.read() & 0x007FFFFF) | 0x00800000);
        }
    }

    SC_CTOR(ExtractModule) {
        SC_METHOD(extract);
        sensitive << a << b << reset;
    }
};

SC_MODULE(ComputeModule) {
    sc_in<sc_uint<32>> a_significand, b_significand;
    sc_in<bool> a_sign, b_sign;
    sc_in<sc_uint<8>> a_exp, b_exp;
    sc_in<bool> reset;
    sc_in<bool> clk;
    sc_out<sc_uint<32>> result;
    sc_out<bool> done;

    void division_fsm() {
        while (true) {
            if (reset.read()) {
                result.write(0);
                done.write(false);
                wait();
            } else {
                // Setup phase
                sc_uint<32> x_val = a_significand.read();
                sc_uint<32> y_val = b_significand.read();
                sc_uint<32> r = 0;
                bool sign = a_sign.read() ^ b_sign.read();
                sc_uint<8> exp = a_exp.read() - b_exp.read() + 127;

                if (x_val < y_val) {
                    x_val = x_val << 1;
                    exp = exp - 1;
                }

                wait();  // 1st cycle (setup)

                // Division phase: 25 cycles
		for (int i = 0; i < 25; ++i) {
                    r = r << 1;
                    if (x_val >= y_val) {
                        x_val = x_val - y_val;
                        r = r | 1;
                    }
                    x_val = x_val << 1;
                    wait();  // 2nd–26th cycles
                }

                // Finalization (27th cycle)
                bool sticky = (x_val != 0);
                sc_uint<32> final_r = r;
                bool rnd = false, odd = false;

                if ((exp >= 1) && (exp <= 254)) {
                    rnd = (final_r & 0x1000000) >> 24;
                    odd = (final_r & 0x2) != 0;
                    final_r = (final_r >> 1) + (rnd & (sticky | odd));
                    final_r = (exp << 23) + (final_r - 0x00800000);
                } else if (exp > 254) {
                    final_r = 0x7F800000; // Inf
                } else {
                    sc_uint<8> shift = 1 - exp;
                    if (shift > 25) shift = 25;
                    sticky |= ((final_r & ~(~0 << shift)) != 0);
                    final_r = final_r >> shift;
                    rnd = (final_r & 0x1000000) >> 24;
                    odd = (final_r & 0x2) != 0;
                    final_r = (final_r >> 1) + (rnd & (sticky | odd));
                }

                final_r |= (sign ? 0x80000000 : 0);
                result.write(final_r);
                done.write(true);
                wait();  // 28th cycle
                done.write(false);  // Reset `done` signal for next run
            }
        }
    }

    SC_CTOR(ComputeModule) {
        SC_CTHREAD(division_fsm, clk.pos());
        reset_signal_is(reset, true);
    }
};


SC_MODULE(ieee754_div) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_in<bool> clk;
    sc_out<sc_uint<32>> result;
    sc_out<bool> done;

    sc_signal<sc_uint<32>> a_significand, b_significand;
    sc_signal<bool> a_sign, b_sign;
    sc_signal<sc_uint<8>> a_exp, b_exp;

    ExtractModule extract_module;
    ComputeModule compute_module;

    SC_CTOR(ieee754_div) :
        extract_module("extract_module"),
        compute_module("compute_module")
    {
        extract_module.a(a);
        extract_module.b(b);
        extract_module.reset(reset);
        extract_module.a_significand(a_significand);
        extract_module.b_significand(b_significand);
        extract_module.a_sign(a_sign);
        extract_module.b_sign(b_sign);
        extract_module.a_exp(a_exp);
        extract_module.b_exp(b_exp);

        compute_module.a_significand(a_significand);
        compute_module.b_significand(b_significand);
        compute_module.a_sign(a_sign);
        compute_module.b_sign(b_sign);
        compute_module.a_exp(a_exp);
        compute_module.b_exp(b_exp);
        compute_module.reset(reset);
        compute_module.clk(clk);
        compute_module.result(result);
        compute_module.done(done);
    }
}; 
