#ifndef EXECUTE_H
#define EXECUTE_H

#include <systemc.h>

// Forward declarations for the pipelined FP units
// You'll need to include the appropriate headers
// #include "ieee754_adder_pipelined.h"
// #include "ieee754_subtractor_pipelined.h" 
// #include "ieee754mult_pipelined.h"
// #include "ieee754_div.h"

SC_MODULE(Execute) {
    // Port declarations
    sc_in<bool> clk;
    sc_in<bool> reset;
    sc_in<bool> stall;
    sc_in<bool> valid_in;
    sc_in<sc_uint<32>> op1;
    sc_in<sc_uint<32>> op2;
    sc_in<sc_uint<7>> opcode;
    sc_in<sc_uint<5>> rd_in;
    sc_in<bool> reg_write_in;
    sc_in<sc_uint<32>> instruction_in;
    
    sc_out<sc_uint<32>> result_out;
    sc_out<sc_uint<5>> rd_out;
    sc_out<bool> reg_write_out;
    sc_out<bool> valid_out;
    sc_out<sc_uint<32>> instruction_out;
    
    // Internal signals for pipelined FP units
    sc_signal<sc_uint<32>> fp_add_result;
    sc_signal<sc_uint<32>> fp_sub_result;
    sc_signal<sc_uint<32>> fp_mul_result;
    sc_signal<sc_uint<32>> fp_div_result;
    
    // Valid signals from pipelined units
    sc_signal<bool> fp_add_valid;
    sc_signal<bool> fp_sub_valid;
    sc_signal<bool> fp_mul_valid;
    sc_signal<bool> fp_div_valid;
    
    // Input valid signals for each unit (could be optimized based on opcode)
    sc_signal<bool> add_valid_in;
    sc_signal<bool> sub_valid_in;
    sc_signal<bool> mul_valid_in;
    sc_signal<bool> div_valid_in;
    
    // Submodules - using pipelined versions
    ieee754_adder_pipelined* fp_adder;
    ieee754_subtractor_pipelined* fp_subtractor;
    ieee754mult_pipelined* fp_multiplier;
    ieee754_div* fp_divider;

    void execute_process() {
        // Initialize outputs
        result_out.write(0);
        rd_out.write(0);
        reg_write_out.write(false);
        valid_out.write(false);
        instruction_out.write(0);
        
        // Initialize valid inputs
        add_valid_in.write(false);
        sub_valid_in.write(false);
        mul_valid_in.write(false);
        div_valid_in.write(false);
        
        wait();
        
        while (true) {
            if (reset.read()) {
                result_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                valid_out.write(false);
                instruction_out.write(0);
                
                // Reset valid inputs
                add_valid_in.write(false);
                sub_valid_in.write(false);
                mul_valid_in.write(false);
                div_valid_in.write(false);
            }
            else if (!stall.read()) {
                // Pass through control signals (with pipeline delay consideration)
                rd_out.write(rd_in.read());
                reg_write_out.write(reg_write_in.read());
                instruction_out.write(instruction_in.read());
                
                if (valid_in.read()) {
                    // Enable the appropriate FP unit based on opcode
                    switch(opcode.read()) {
                        case 0x00: // FADD
                            add_valid_in.write(true);
                            sub_valid_in.write(false);
                            mul_valid_in.write(false);
                            div_valid_in.write(false);
                            break;
                        case 0x04: // FSUB
                            add_valid_in.write(false);
                            sub_valid_in.write(true);
                            mul_valid_in.write(false);
                            div_valid_in.write(false);
                            break;
                        case 0x08: // FMUL
                            add_valid_in.write(false);
                            sub_valid_in.write(false);
                            mul_valid_in.write(true);
                            div_valid_in.write(false);
                            break;
                        case 0x0C: // FDIV
                            add_valid_in.write(false);
                            sub_valid_in.write(false);
                            mul_valid_in.write(false);
                            div_valid_in.write(true);
                            break;
                        default:
                            add_valid_in.write(false);
                            sub_valid_in.write(false);
                            mul_valid_in.write(false);
                            div_valid_in.write(false);
                            break;
                    }
                } else {
                    // No valid input, disable all units
                    add_valid_in.write(false);
                    sub_valid_in.write(false);
                    mul_valid_in.write(false);
                    div_valid_in.write(false);
                }
                
                // Select result based on which unit has valid output
                // Note: This is a simplified approach. In a real implementation,
                // you'd need proper pipeline management to match results with operations
                if (fp_add_valid.read()) {
                    result_out.write(fp_add_result.read());
                    valid_out.write(true);
                } else if (fp_sub_valid.read()) {
                    result_out.write(fp_sub_result.read());
                    valid_out.write(true);
                } else if (fp_mul_valid.read()) {
                    result_out.write(fp_mul_result.read());
                    valid_out.write(true);
                } else if (fp_div_valid.read()) {
                    result_out.write(fp_div_result.read());
                    valid_out.write(true);
                } else {
                    valid_out.write(false);
                }
            }
            wait();
        }
    }

    SC_CTOR(Execute) {
        // Create and connect pipelined fp_adder
        fp_adder = new ieee754_adder_pipelined("fp_adder");
        fp_adder->clk(clk);
        fp_adder->reset(reset);
        fp_adder->A(op1);
        fp_adder->B(op2);
        fp_adder->valid_in(add_valid_in);
        fp_adder->O(fp_add_result);
        fp_adder->valid_out(fp_add_valid);

        // Create and connect pipelined fp_subtractor
        fp_subtractor = new ieee754_subtractor_pipelined("fp_subtractor");
        fp_subtractor->clk(clk);
        fp_subtractor->reset(reset);
        fp_subtractor->A(op1);
        fp_subtractor->B(op2);
        fp_subtractor->valid_in(sub_valid_in);
        fp_subtractor->O(fp_sub_result);
        fp_subtractor->valid_out(fp_sub_valid);

        // Create and connect pipelined fp_multiplier
        fp_multiplier = new ieee754mult_pipelined("fp_multiplier");
        fp_multiplier->clk(clk);
        fp_multiplier->reset(reset);
        fp_multiplier->A(op1);
        fp_multiplier->B(op2);
        fp_multiplier->result(fp_mul_result);
        fp_multiplier->valid_out(fp_mul_valid);

        // Create and connect fp_divider
        fp_divider = new ieee754_div("fp_divider");
        fp_divider->a(op1);
        fp_divider->b(op2);
        fp_divider->reset(reset);
        fp_divider->result(fp_div_result);
        // Note: The division module doesn't seem to have valid_out in the provided code
        // You might need to add it or handle it differently

        SC_CTHREAD(execute_process, clk.pos());
        reset_signal_is(reset, true);

        // Initialize outputs
        result_out.initialize(0);
        rd_out.initialize(0);
        reg_write_out.initialize(false);
        valid_out.initialize(false);
        instruction_out.initialize(0);
    }

    ~Execute() {
        delete fp_adder;
        delete fp_subtractor;
        delete fp_multiplier;
        delete fp_divider;
    }
};

#endif // EXECUTE_H
