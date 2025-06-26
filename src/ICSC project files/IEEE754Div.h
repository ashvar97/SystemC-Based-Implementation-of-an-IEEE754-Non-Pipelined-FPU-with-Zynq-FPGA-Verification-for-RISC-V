#include <systemc.h>
#include <iostream>

// Individual pipeline stage module
SC_MODULE(DivisionStage) {
    // Input signals
    sc_in<sc_uint<32>> in_a_sig, in_b_sig;
    sc_in<bool> in_a_sign, in_b_sign;
    sc_in<sc_uint<8>> in_a_exp, in_b_exp;
    sc_in<sc_uint<32>> in_partial_quotient;
    sc_in<sc_uint<32>> in_remainder;
    sc_in<sc_uint<8>> in_result_exp;
    sc_in<bool> in_result_sign;
    sc_in<sc_uint<5>> in_iteration;
    sc_in<bool> in_valid;
    sc_in<bool> reset;
    sc_in<bool> clk;

    // Output signals
    sc_out<sc_uint<32>> out_a_sig, out_b_sig;
    sc_out<bool> out_a_sign, out_b_sign;
    sc_out<sc_uint<8>> out_a_exp, out_b_exp;
    sc_out<sc_uint<32>> out_partial_quotient;
    sc_out<sc_uint<32>> out_remainder;
    sc_out<sc_uint<8>> out_result_exp;
    sc_out<bool> out_result_sign;
    sc_out<sc_uint<5>> out_iteration;
    sc_out<bool> out_valid;

    void division_process() {
        while (true) {
            wait();
            
            if (reset.read()) {
                out_a_sig.write(0);
                out_b_sig.write(0);
                out_a_sign.write(false);
                out_b_sign.write(false);
                out_a_exp.write(0);
                out_b_exp.write(0);
                out_partial_quotient.write(0);
                out_remainder.write(0);
                out_result_exp.write(0);
                out_result_sign.write(false);
                out_iteration.write(0);
                out_valid.write(false);
            } else if (in_valid.read()) {
                // Pass through unchanged data
                out_a_sig.write(in_a_sig.read());
                out_b_sig.write(in_b_sig.read());
                out_a_sign.write(in_a_sign.read());
                out_b_sign.write(in_b_sign.read());
                out_a_exp.write(in_a_exp.read());
                out_b_exp.write(in_b_exp.read());
                out_result_sign.write(in_result_sign.read());

                // Perform division step
                sc_uint<32> new_quotient = in_partial_quotient.read() << 1;
                sc_uint<32> new_remainder = in_remainder.read();
                sc_uint<8> new_exp = in_result_exp.read();

                if (new_remainder >= in_b_sig.read()) {
                    new_remainder = new_remainder - in_b_sig.read();
                    new_quotient = new_quotient | 1;
                }

                new_remainder = new_remainder << 1;

                out_partial_quotient.write(new_quotient);
                out_remainder.write(new_remainder);
                out_result_exp.write(new_exp);
                out_iteration.write(in_iteration.read() + 1);
                out_valid.write(true);
            } else {
                out_a_sig.write(0);
                out_b_sig.write(0);
                out_a_sign.write(false);
                out_b_sign.write(false);
                out_a_exp.write(0);
                out_b_exp.write(0);
                out_partial_quotient.write(0);
                out_remainder.write(0);
                out_result_exp.write(0);
                out_result_sign.write(false);
                out_iteration.write(0);
                out_valid.write(false);
            }
        }
    }

    SC_CTOR(DivisionStage) {
        SC_CTHREAD(division_process, clk.pos());
        reset_signal_is(reset, true);
    }
};

// First stage: Extract IEEE 754 components and initialize
SC_MODULE(ExtractStage) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_in<bool> clk;
    sc_in<bool> start;

    // Output signals for first pipeline stage
    sc_out<sc_uint<32>> out_a_sig, out_b_sig;
    sc_out<bool> out_a_sign, out_b_sign;
    sc_out<sc_uint<8>> out_a_exp, out_b_exp;
    sc_out<sc_uint<32>> out_partial_quotient;
    sc_out<sc_uint<32>> out_remainder;
    sc_out<sc_uint<8>> out_result_exp;
    sc_out<bool> out_result_sign;
    sc_out<sc_uint<5>> out_iteration;
    sc_out<bool> out_valid;

    void extract_process() {
        while (true) {
            wait();
            
            if (reset.read()) {
                out_a_sig.write(0);
                out_b_sig.write(0);
                out_a_sign.write(false);
                out_b_sign.write(false);
                out_a_exp.write(0);
                out_b_exp.write(0);
                out_partial_quotient.write(0);
                out_remainder.write(0);
                out_result_exp.write(0);
                out_result_sign.write(false);
                out_iteration.write(0);
                out_valid.write(false);
            } else if (start.read()) {
                // Extract IEEE 754 components
                sc_uint<8> a_exp = (a.read() & 0x7F800000) >> 23;
                sc_uint<8> b_exp = (b.read() & 0x7F800000) >> 23;
                bool a_sign = (a.read() & 0x80000000) != 0;
                bool b_sign = (b.read() & 0x80000000) != 0;
                sc_uint<32> a_sig = (a.read() & 0x007FFFFF) | 0x00800000;
                sc_uint<32> b_sig = (b.read() & 0x007FFFFF) | 0x00800000;

                // Initialize computation - use logical XOR instead of bitwise
                bool result_sign = (a_sign && !b_sign) || (!a_sign && b_sign);
                sc_uint<8> result_exp = a_exp - b_exp + 127;
                sc_uint<32> remainder = a_sig;

                // Normalize if dividend < divisor
                if (remainder < b_sig) {
                    remainder = remainder << 1;
                    result_exp = result_exp - 1;
                }

                // Write outputs
                out_a_sig.write(a_sig);
                out_b_sig.write(b_sig);
                out_a_sign.write(a_sign);
                out_b_sign.write(b_sign);
                out_a_exp.write(a_exp);
                out_b_exp.write(b_exp);
                out_partial_quotient.write(0);
                out_remainder.write(remainder);
                out_result_exp.write(result_exp);
                out_result_sign.write(result_sign);
                out_iteration.write(0);
                out_valid.write(true);
            } else {
                out_a_sig.write(0);
                out_b_sig.write(0);
                out_a_sign.write(false);
                out_b_sign.write(false);
                out_a_exp.write(0);
                out_b_exp.write(0);
                out_partial_quotient.write(0);
                out_remainder.write(0);
                out_result_exp.write(0);
                out_result_sign.write(false);
                out_iteration.write(0);
                out_valid.write(false);
            }
        }
    }

    SC_CTOR(ExtractStage) {
        SC_CTHREAD(extract_process, clk.pos());
        reset_signal_is(reset, true);
    }
};

// Final stage: Normalize and round result
SC_MODULE(FinalStage) {
    // Input signals from last division stage
    sc_in<sc_uint<32>> in_a_sig, in_b_sig;
    sc_in<bool> in_a_sign, in_b_sign;
    sc_in<sc_uint<8>> in_a_exp, in_b_exp;
    sc_in<sc_uint<32>> in_partial_quotient;
    sc_in<sc_uint<32>> in_remainder;
    sc_in<sc_uint<8>> in_result_exp;
    sc_in<bool> in_result_sign;
    sc_in<sc_uint<5>> in_iteration;
    sc_in<bool> in_valid;
    sc_in<bool> reset;
    sc_in<bool> clk;

    sc_out<sc_uint<32>> result;
    sc_out<bool> done;

    void final_process() {
        while (true) {
            wait();
            
            if (reset.read()) {
                result.write(0);
                done.write(false);
            } else if (in_valid.read()) {
                // Get final values
                sc_uint<32> quotient = in_partial_quotient.read();
                sc_uint<32> remainder = in_remainder.read();
                sc_uint<8> result_exp = in_result_exp.read();
                bool result_sign = in_result_sign.read();

                // Compute sticky bit
                bool sticky = (remainder != 0);
                
                // Final result computation with rounding
                sc_uint<32> final_result;
                bool rnd, odd;

                if ((result_exp >= 1) && (result_exp <= 254)) {
                    // Normal case
                    rnd = (quotient & 0x01000000) != 0;
                    odd = (quotient & 0x00000002) != 0;
                    quotient = (quotient >> 1) + (rnd && (sticky || odd) ? 1 : 0);
                    final_result = (result_exp << 23) + (quotient - 0x00800000);
                } 
                else if (result_exp > 254) {
                    // Overflow - infinity
                    final_result = 0x7F800000;
                } 
                else {
                    // Underflow - denormalized
                    sc_uint<8> shift = 1 - result_exp;
                    if (shift > 25) shift = 25;
                    
                    // Update sticky bit
                    if (shift > 0) {
                        sc_uint<32> mask = (1 << shift) - 1;
                        sticky = sticky || ((quotient & mask) != 0);
                        quotient = quotient >> shift;
                    }
                    
                    rnd = (quotient & 0x01000000) != 0;
                    odd = (quotient & 0x00000002) != 0;
                    final_result = (quotient >> 1) + (rnd && (sticky || odd) ? 1 : 0);
                }

                // Add sign bit
                if (result_sign) {
                    final_result = final_result | 0x80000000;
                }

                result.write(final_result);
                done.write(true);
            } else {
                result.write(0);
                done.write(false);
            }
        }
    }

    SC_CTOR(FinalStage) {
        SC_CTHREAD(final_process, clk.pos());
        reset_signal_is(reset, true);
    }
};

// Top level pipelined divider
SC_MODULE(ieee754_div) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_in<bool> clk;
    sc_in<bool> start;
    sc_out<sc_uint<32>> result;
    sc_out<bool> done;

    // Pipeline signals - 26 stages total (extract + 24 division + final)
    static const int NUM_DIVISION_STAGES = 24;
    
    // Signals between pipeline stages
    sc_signal<sc_uint<32>> stage_a_sig[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<32>> stage_b_sig[NUM_DIVISION_STAGES + 1];
    sc_signal<bool> stage_a_sign[NUM_DIVISION_STAGES + 1];
    sc_signal<bool> stage_b_sign[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<8>> stage_a_exp[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<8>> stage_b_exp[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<32>> stage_partial_quotient[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<32>> stage_remainder[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<8>> stage_result_exp[NUM_DIVISION_STAGES + 1];
    sc_signal<bool> stage_result_sign[NUM_DIVISION_STAGES + 1];
    sc_signal<sc_uint<5>> stage_iteration[NUM_DIVISION_STAGES + 1];
    sc_signal<bool> stage_valid[NUM_DIVISION_STAGES + 1];

    // Pipeline stage modules
    ExtractStage* extract_stage;
    DivisionStage* division_stages[NUM_DIVISION_STAGES];
    FinalStage* final_stage;

    SC_CTOR(ieee754_div) {
        // Create extract stage
        extract_stage = new ExtractStage("extract_stage");
        extract_stage->a(a);
        extract_stage->b(b);
        extract_stage->reset(reset);
        extract_stage->clk(clk);
        extract_stage->start(start);
        extract_stage->out_a_sig(stage_a_sig[0]);
        extract_stage->out_b_sig(stage_b_sig[0]);
        extract_stage->out_a_sign(stage_a_sign[0]);
        extract_stage->out_b_sign(stage_b_sign[0]);
        extract_stage->out_a_exp(stage_a_exp[0]);
        extract_stage->out_b_exp(stage_b_exp[0]);
        extract_stage->out_partial_quotient(stage_partial_quotient[0]);
        extract_stage->out_remainder(stage_remainder[0]);
        extract_stage->out_result_exp(stage_result_exp[0]);
        extract_stage->out_result_sign(stage_result_sign[0]);
        extract_stage->out_iteration(stage_iteration[0]);
        extract_stage->out_valid(stage_valid[0]);

        // Create division stages
        for (int i = 0; i < NUM_DIVISION_STAGES; i++) {
            char name[32];
            sprintf(name, "division_stage_%d", i);
            division_stages[i] = new DivisionStage(name);
            
            // Connect inputs
            division_stages[i]->in_a_sig(stage_a_sig[i]);
            division_stages[i]->in_b_sig(stage_b_sig[i]);
            division_stages[i]->in_a_sign(stage_a_sign[i]);
            division_stages[i]->in_b_sign(stage_b_sign[i]);
            division_stages[i]->in_a_exp(stage_a_exp[i]);
            division_stages[i]->in_b_exp(stage_b_exp[i]);
            division_stages[i]->in_partial_quotient(stage_partial_quotient[i]);
            division_stages[i]->in_remainder(stage_remainder[i]);
            division_stages[i]->in_result_exp(stage_result_exp[i]);
            division_stages[i]->in_result_sign(stage_result_sign[i]);
            division_stages[i]->in_iteration(stage_iteration[i]);
            division_stages[i]->in_valid(stage_valid[i]);
            division_stages[i]->reset(reset);
            division_stages[i]->clk(clk);
            
            // Connect outputs
            division_stages[i]->out_a_sig(stage_a_sig[i + 1]);
            division_stages[i]->out_b_sig(stage_b_sig[i + 1]);
            division_stages[i]->out_a_sign(stage_a_sign[i + 1]);
            division_stages[i]->out_b_sign(stage_b_sign[i + 1]);
            division_stages[i]->out_a_exp(stage_a_exp[i + 1]);
            division_stages[i]->out_b_exp(stage_b_exp[i + 1]);
            division_stages[i]->out_partial_quotient(stage_partial_quotient[i + 1]);
            division_stages[i]->out_remainder(stage_remainder[i + 1]);
            division_stages[i]->out_result_exp(stage_result_exp[i + 1]);
            division_stages[i]->out_result_sign(stage_result_sign[i + 1]);
            division_stages[i]->out_iteration(stage_iteration[i + 1]);
            division_stages[i]->out_valid(stage_valid[i + 1]);
        }

        // Create final stage
        final_stage = new FinalStage("final_stage");
        final_stage->in_a_sig(stage_a_sig[NUM_DIVISION_STAGES]);
        final_stage->in_b_sig(stage_b_sig[NUM_DIVISION_STAGES]);
        final_stage->in_a_sign(stage_a_sign[NUM_DIVISION_STAGES]);
        final_stage->in_b_sign(stage_b_sign[NUM_DIVISION_STAGES]);
        final_stage->in_a_exp(stage_a_exp[NUM_DIVISION_STAGES]);
        final_stage->in_b_exp(stage_b_exp[NUM_DIVISION_STAGES]);
        final_stage->in_partial_quotient(stage_partial_quotient[NUM_DIVISION_STAGES]);
        final_stage->in_remainder(stage_remainder[NUM_DIVISION_STAGES]);
        final_stage->in_result_exp(stage_result_exp[NUM_DIVISION_STAGES]);
        final_stage->in_result_sign(stage_result_sign[NUM_DIVISION_STAGES]);
        final_stage->in_iteration(stage_iteration[NUM_DIVISION_STAGES]);
        final_stage->in_valid(stage_valid[NUM_DIVISION_STAGES]);
        final_stage->reset(reset);
        final_stage->clk(clk);
        final_stage->result(result);
        final_stage->done(done);
    }

    ~ieee754_div() {
        delete extract_stage;
        for (int i = 0; i < NUM_DIVISION_STAGES; i++) {
            delete division_stages[i];
        }
        delete final_stage;
    }
};
