#include <systemc.h>
#include <iostream>
#include <cstring>

// ExtractModule: Extracts sign, exponent, and significand from floating-point inputs
SC_MODULE(ExtractModule) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset;
    sc_in<bool> clk;
    sc_in<bool> enable;
    sc_out<sc_uint<32>> a_significand, b_significand;
    sc_out<bool> a_sign, b_sign;
    sc_out<sc_uint<8>> a_exp, b_exp;
    sc_out<bool> valid_out;

    void extract() {
        if (reset.read()) {
            a_significand.write(0);
            b_significand.write(0);
            a_sign.write(false);
            b_sign.write(false);
            a_exp.write(0);
            b_exp.write(0);
            valid_out.write(false);
        } else if (clk.posedge() && enable.read()) {
            // Extract biased exponents and sign bits
            a_exp.write((a.read() & 0x7F800000) >> 23);
            b_exp.write((b.read() & 0x7F800000) >> 23);
            a_sign.write((a.read() & 0x80000000) != 0);
            b_sign.write((b.read() & 0x80000000) != 0);

            // Extract significands (with implicit leading 1)
            sc_uint<32> a_sig = (a.read() & 0x007FFFFF);
            sc_uint<32> b_sig = (b.read() & 0x007FFFFF);
            
            // Add implicit leading 1 for normalized numbers
            if ((a.read() & 0x7F800000) != 0) a_sig = a_sig | 0x00800000;
            if ((b.read() & 0x7F800000) != 0) b_sig = b_sig | 0x00800000;
            
            a_significand.write(a_sig);
            b_significand.write(b_sig);
            valid_out.write(true);
        } else if (clk.posedge()) {
            valid_out.write(false);
        }
    }

    SC_CTOR(ExtractModule) {
        SC_METHOD(extract);
        sensitive << clk.pos() << reset;
    }
};

// Multi-cycle ComputeModule with state machine
SC_MODULE(ComputeModule) {
    sc_in<sc_uint<32>> a_significand, b_significand;
    sc_in<bool> a_sign, b_sign;
    sc_in<sc_uint<8>> a_exp, b_exp;
    sc_in<bool> reset, clk;
    sc_in<bool> start;
    sc_out<sc_uint<32>> result;
    sc_out<bool> done;

    // State machine
    enum state_t {
        IDLE,
        INIT,
        DIVIDING,
        ROUNDING,
        DONE_STATE
    };

    // Registers
    sc_signal<state_t> current_state, next_state;
    sc_signal<sc_uint<32>> r_reg, x_val_reg, y_val_reg;
    sc_signal<sc_uint<8>> result_exp_reg;
    sc_signal<sc_uint<5>> i_reg;
    sc_signal<bool> result_sign_reg;
    sc_signal<sc_uint<32>> result_reg;

    void state_reg() {
        if (reset.read()) {
            current_state.write(IDLE);
            r_reg.write(0);
            x_val_reg.write(0);
            y_val_reg.write(0);
            result_exp_reg.write(0);
            i_reg.write(0);
            result_sign_reg.write(false);
            result_reg.write(0);
        } else if (clk.posedge()) {
            current_state.write(next_state.read());
            
            switch (current_state.read()) {
                case IDLE:
                    if (start.read()) {
                        result_sign_reg.write(a_sign.read() ^ b_sign.read());
                        result_exp_reg.write(a_exp.read() - b_exp.read() + 127);
                        x_val_reg.write(a_significand.read());
                        y_val_reg.write(b_significand.read());
                        r_reg.write(0);
                        i_reg.write(0);
                    }
                    break;
                    
                case INIT:
                    // Check if normalization needed
                    if (x_val_reg.read() < y_val_reg.read()) {
                        x_val_reg.write(x_val_reg.read() << 1);
                        result_exp_reg.write(result_exp_reg.read() - 1);
                    }
                    break;
                    
                case DIVIDING:
                    // One division iteration per clock cycle
                    {
                        sc_uint<32> r_temp = r_reg.read() << 1;
                        sc_uint<32> x_temp = x_val_reg.read();
                        
                        if (x_val_reg.read() >= y_val_reg.read()) {
                            x_temp = x_val_reg.read() - y_val_reg.read();
                            r_temp = r_temp | 1;
                        }
                        
                        r_reg.write(r_temp);
                        x_val_reg.write(x_temp << 1);
                        i_reg.write(i_reg.read() + 1);
                    }
                    break;
                    
                case ROUNDING:
                    // Perform rounding
                    {
                        bool sticky = (x_val_reg.read() != 0);
                        sc_uint<32> r_temp = r_reg.read();
                        
                        if ((result_exp_reg.read() >= 1) && (result_exp_reg.read() <= 254)) {
                            // Normal case
                            bool rnd = (r_temp & 0x1000000) >> 24;
                            bool odd = (r_temp & 0x2) != 0;
                            r_temp = (r_temp >> 1) + (rnd & (sticky | odd));
                            r_temp = (result_exp_reg.read() << 23) + (r_temp - 0x00800000);
                        } else if (result_exp_reg.read() > 254) {
                            // Overflow
                            r_temp = 0x7F800000;
                        } else {
                            // Underflow
                            sc_uint<8> shift = 1 - result_exp_reg.read();
                            if (shift > 25) shift = 25;
                            sticky = sticky | ((r_temp & ~(~0U << shift)) != 0);
                            r_temp = r_temp >> shift;
                            bool rnd = (r_temp & 0x1000000) >> 24;
                            bool odd = (r_temp & 0x2) != 0;
                            r_temp = (r_temp >> 1) + (rnd & (sticky | odd));
                        }
                        
                        // Apply sign
                        r_temp = r_temp | (result_sign_reg.read() ? 0x80000000 : 0);
                        result_reg.write(r_temp);
                    }
                    break;
                    
                case DONE_STATE:
                    // Stay in done state for one cycle
                    break;
            }
        }
    }

    void next_state_logic() {
        switch (current_state.read()) {
            case IDLE:
                if (start.read()) {
                    next_state.write(INIT);
                } else {
                    next_state.write(IDLE);
                }
                break;
                
            case INIT:
                next_state.write(DIVIDING);
                break;
                
            case DIVIDING:
                if (i_reg.read() >= 24) { // 0-24 = 25 iterations
                    next_state.write(ROUNDING);
                } else {
                    next_state.write(DIVIDING);
                }
                break;
                
            case ROUNDING:
                next_state.write(DONE_STATE);
                break;
                
            case DONE_STATE:
                next_state.write(IDLE);
                break;
                
            default:
                next_state.write(IDLE);
                break;
        }
    }

    void output_logic() {
        result.write(result_reg.read());
        done.write(current_state.read() == DONE_STATE);
    }

    SC_CTOR(ComputeModule) {
        SC_METHOD(state_reg);
        sensitive << clk.pos() << reset;
        
        SC_METHOD(next_state_logic);
        sensitive << current_state << start << i_reg;
        
        SC_METHOD(output_logic);
        sensitive << current_state << result_reg;
    }
};

// Pipeline stage for final result
SC_MODULE(OutputStage) {
    sc_in<sc_uint<32>> result_in;
    sc_in<bool> valid_in;
    sc_in<bool> clk, reset;
    sc_out<sc_uint<32>> result_out;
    sc_out<bool> valid_out;

    void pipeline_reg() {
        if (reset.read()) {
            result_out.write(0);
            valid_out.write(false);
        } else if (clk.posedge()) {
            result_out.write(result_in.read());
            valid_out.write(valid_in.read());
        }
    }

    SC_CTOR(OutputStage) {
        SC_METHOD(pipeline_reg);
        sensitive << clk.pos() << reset;
    }
};

// Top-level pipelined IEEE 754 divider
SC_MODULE(ieee754_div) {
    sc_in<sc_uint<32>> a, b;
    sc_in<bool> reset, clk;
    sc_in<bool> start;
    sc_out<sc_uint<32>> result;
    sc_out<bool> done;

    // Internal signals
    sc_signal<sc_uint<32>> a_significand, b_significand;
    sc_signal<bool> a_sign, b_sign;
    sc_signal<sc_uint<8>> a_exp, b_exp;
    sc_signal<bool> extract_valid;
    sc_signal<sc_uint<32>> compute_result;
    sc_signal<bool> compute_done;

    // Submodules
    ExtractModule extract_module;
    ComputeModule compute_module;
    OutputStage output_stage;

    SC_CTOR(ieee754_div) : 
        extract_module("extract_module"),
        compute_module("compute_module"),
        output_stage("output_stage")
    {
        // Connect ExtractModule (Stage 1)
        extract_module.a(a);
        extract_module.b(b);
        extract_module.reset(reset);
        extract_module.clk(clk);
        extract_module.enable(start);
        extract_module.a_significand(a_significand);
        extract_module.b_significand(b_significand);
        extract_module.a_sign(a_sign);
        extract_module.b_sign(b_sign);
        extract_module.a_exp(a_exp);
        extract_module.b_exp(b_exp);
        extract_module.valid_out(extract_valid);

        // Connect ComputeModule (Stages 2-28)
        compute_module.a_significand(a_significand);
        compute_module.b_significand(b_significand);
        compute_module.a_sign(a_sign);
        compute_module.b_sign(b_sign);
        compute_module.a_exp(a_exp);
        compute_module.b_exp(b_exp);
        compute_module.reset(reset);
        compute_module.clk(clk);
        compute_module.start(extract_valid);
        compute_module.result(compute_result);
        compute_module.done(compute_done);

        // Connect OutputStage (Stage 29)
        output_stage.result_in(compute_result);
        output_stage.valid_in(compute_done);
        output_stage.clk(clk);
        output_stage.reset(reset);
        output_stage.result_out(result);
        output_stage.valid_out(done);
    }
};