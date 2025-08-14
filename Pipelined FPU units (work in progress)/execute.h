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
    
    // Internal signals for FP operations
    sc_signal<sc_uint<32>> fp_add_result;
    sc_signal<bool> fp_add_valid_out;
    sc_signal<bool> fp_add_overflow;
    sc_signal<bool> fp_add_underflow;
    
    sc_signal<sc_uint<32>> fp_sub_result;
    sc_signal<bool> fp_sub_valid_out;
    sc_signal<bool> fp_sub_overflow;
    sc_signal<bool> fp_sub_underflow;
    
    sc_signal<sc_uint<32>> fp_mul_result;
    sc_signal<bool> fp_mul_valid_out;
    sc_signal<bool> fp_mul_overflow;
    sc_signal<bool> fp_mul_underflow;
    
    // Updated division signals for pipelined divider
    sc_signal<sc_uint<32>> fp_div_result;
    sc_signal<bool> fp_div_valid_out;
    sc_signal<bool> fp_div_overflow;
    sc_signal<bool> fp_div_underflow;
    sc_signal<bool> fp_div_divide_by_zero;
    
    // Extended pipeline structure to handle multi-cycle operations
    struct PipelineStage {
        bool valid;
        sc_uint<5> rd;
        bool reg_write;
        sc_uint<32> instruction;
        sc_uint<7> opcode;
    };
    
    // Extended pipeline to handle division (27 cycles) plus other operations
    PipelineStage stage[28];  // 28 stages to handle division (27 cycles) + buffer
    
    // Submodules
    ieee754add* fp_adder;           // Pipelined (3 cycles)
    ieee754_subtractor* fp_subtractor;  // Pipelined (3 cycles)
    ieee754mult* fp_multiplier;     // Pipelined (3 cycles)
    ieee754div* fp_divider;         // Pipelined (27 cycles)
    
    void pipeline_process() {
        // Initialize pipeline stages
        for (int i = 0; i < 28; i++) {
            stage[i].valid = false;
            stage[i].rd = 0;
            stage[i].reg_write = false;
            stage[i].instruction = 0;
            stage[i].opcode = 0;
        }
        
        result_out.write(0);
        rd_out.write(0);
        reg_write_out.write(false);
        valid_out.write(false);
        instruction_out.write(0);
        
        wait();
        
        while (true) {
            if (reset.read()) {
                for (int i = 0; i < 28; i++) {
                    stage[i].valid = false;
                    stage[i].rd = 0;
                    stage[i].reg_write = false;
                    stage[i].instruction = 0;
                    stage[i].opcode = 0;
                }
                
                result_out.write(0);
                rd_out.write(0);
                reg_write_out.write(false);
                valid_out.write(false);
                instruction_out.write(0);
            }
            else if (!stall.read()) {
                // OUTPUT STAGE: Determine which pipeline stage to use based on operation
                bool output_valid = false;
                sc_uint<5> output_rd = 0;
                bool output_reg_write = false;
                sc_uint<32> output_instruction = 0;
                sc_uint<32> output_result = 0;
                
                // Check each pipeline stage for ready results
                // FP Add (3 cycles) - check stage[2] (3 cycles after input)
                if (stage[2].valid && stage[2].reg_write && stage[2].opcode == 0x00 && fp_add_valid_out.read()) {
                    output_valid = true;
                    output_rd = stage[2].rd;
                    output_reg_write = stage[2].reg_write;
                    output_instruction = stage[2].instruction;
                    output_result = fp_add_result.read();
                    
                    cout << "EX @" << sc_time_stamp() << ": FP Add result = 0x" << hex 
                         << output_result << dec << " -> f" << output_rd << endl;
                }
                // FP Subtract (3 cycles) - check stage[2] (3 cycles after input)
                else if (stage[2].valid && stage[2].reg_write && stage[2].opcode == 0x04 && fp_sub_valid_out.read()) {
                    output_valid = true;
                    output_rd = stage[2].rd;
                    output_reg_write = stage[2].reg_write;
                    output_instruction = stage[2].instruction;
                    output_result = fp_sub_result.read();
                    
                    cout << "EX @" << sc_time_stamp() << ": FP Sub result = 0x" << hex 
                         << output_result << dec << " -> f" << output_rd << endl;
                }
                // FP Multiply (3 cycles) - check stage[2] (3 cycles after input)
                else if (stage[2].valid && stage[2].reg_write && stage[2].opcode == 0x08 && fp_mul_valid_out.read()) {
                    output_valid = true;
                    output_rd = stage[2].rd;
                    output_reg_write = stage[2].reg_write;
                    output_instruction = stage[2].instruction;
                    output_result = fp_mul_result.read();
                    
                    cout << "EX @" << sc_time_stamp() << ": FP Mul result = 0x" << hex 
                         << output_result << dec << " -> f" << output_rd << endl;
                }
                // FP Division (27 cycles) - check stage[26] (27 cycles after input)
                else if (stage[26].valid && stage[26].reg_write && stage[26].opcode == 0x0C && fp_div_valid_out.read()) {
                    output_valid = true;
                    output_rd = stage[26].rd;
                    output_reg_write = stage[26].reg_write;
                    output_instruction = stage[26].instruction;
                    output_result = fp_div_result.read();
                    
                    cout << "EX @" << sc_time_stamp() << ": FP Div result = 0x" << hex 
                         << output_result << dec << " -> f" << output_rd;
                    if (fp_div_divide_by_zero.read()) cout << " (DIV_BY_ZERO)";
                    if (fp_div_overflow.read()) cout << " (OVERFLOW)";
                    if (fp_div_underflow.read()) cout << " (UNDERFLOW)";
                    cout << endl;
                }
                
                // Write outputs
                valid_out.write(output_valid);
                rd_out.write(output_rd);
                reg_write_out.write(output_reg_write);
                instruction_out.write(output_instruction);
                result_out.write(output_result);
                
                // PIPELINE SHIFT: Shift all stages
                for (int i = 27; i > 0; i--) {
                    stage[i] = stage[i-1];
                }
                
                // INPUT STAGE: Process new input
                stage[0].valid = valid_in.read();
                stage[0].rd = rd_in.read();
                stage[0].reg_write = reg_write_in.read();
                stage[0].instruction = instruction_in.read();
                stage[0].opcode = opcode.read();
                
                // Debug input operations
                if (valid_in.read() && reg_write_in.read()) {
                    sc_uint<7> current_opcode = opcode.read();
                    cout << "EX @" << sc_time_stamp() << ": Input - opcode=0x" << hex 
                         << current_opcode << " rd=f" << dec << rd_in.read();
                    
                    switch(current_opcode) {
                        case 0x00: cout << " (FP ADD, 3 cycles)"; break;
                        case 0x04: cout << " (FP SUB, 3 cycles)"; break;
                        case 0x08: cout << " (FP MUL, 3 cycles)"; break;
                        case 0x0C: cout << " (FP DIV, 27 cycles)"; break;
                        default: cout << " (UNKNOWN)"; break;
                    }
                    cout << endl;
                }
            }
            
            wait();
        }
    }
    
    SC_CTOR(Execute) {
        // Create and connect pipelined fp_adder
        fp_adder = new ieee754add("fp_adder");
        fp_adder->A(op1);
        fp_adder->B(op2);
        fp_adder->reset(reset);
        fp_adder->clk(clk);
        fp_adder->result(fp_add_result);
        fp_adder->valid_out(fp_add_valid_out);
        fp_adder->overflow(fp_add_overflow);
        fp_adder->underflow(fp_add_underflow);
        
        // Create and connect pipelined fp_subtractor
        fp_subtractor = new ieee754_subtractor("fp_subtractor");
        fp_subtractor->A(op1);
        fp_subtractor->B(op2);
        fp_subtractor->reset(reset);
        fp_subtractor->clk(clk);
        fp_subtractor->result(fp_sub_result);
        fp_subtractor->valid_out(fp_sub_valid_out);
        fp_subtractor->overflow(fp_sub_overflow);
        fp_subtractor->underflow(fp_sub_underflow);
        
        // Create and connect fp_multiplier (pipelined)
        fp_multiplier = new ieee754mult("fp_multiplier");
        fp_multiplier->A(op1);
        fp_multiplier->B(op2);
        fp_multiplier->reset(reset);
        fp_multiplier->clk(clk);
        fp_multiplier->result(fp_mul_result);
        fp_multiplier->valid_out(fp_mul_valid_out);
        fp_multiplier->overflow(fp_mul_overflow);
        fp_multiplier->underflow(fp_mul_underflow);
        
        // Create and connect fp_divider (pipelined - 27 cycles)
        fp_divider = new ieee754div("fp_divider");
        fp_divider->a(op1);
        fp_divider->b(op2);
        fp_divider->reset(reset);
        fp_divider->clk(clk);
        fp_divider->result(fp_div_result);
        fp_divider->valid_out(fp_div_valid_out);
        fp_divider->overflow(fp_div_overflow);
        fp_divider->underflow(fp_div_underflow);
        fp_divider->divide_by_zero(fp_div_divide_by_zero);
        
        // Main pipeline process
        SC_CTHREAD(pipeline_process, clk.pos());
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
